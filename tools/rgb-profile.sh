#!/usr/bin/env bash
#
# rgb-backlight: perfil de estado de los dispositivos RGB.
#
#   rgb-profile.sh save    [keyboard|mouse|all]   guarda el estado actual
#   rgb-profile.sh restore [keyboard|mouse|all]   aplica el perfil guardado
#   rgb-profile.sh show                           enseña perfil y estado real
#
# El plugin del widget guarda el perfil automaticamente cada vez que cambias
# algo; este script sirve para hacerlo desde la linea de comandos y es el que
# usan los dos servicios que lo devuelven a su sitio:
#
#   rgb-backlight-restore.service  (sistema)  al arrancar la PC
#   rgb-backlight-session.service  (usuario)  al iniciar sesion
#
# El perfil vive en ~/.config/rgb-backlight/<dispositivo>.state, en formato
# clave=valor, de modo que se puede leer y editar a mano.
#
set -uo pipefail

KBD_LED=/sys/class/leds/msi::kbd_backlight
MOUSE_LED=/sys/class/leds/razer::logo_backlight
BUSCTL=/usr/bin/busctl
OWNER_FILE=/var/lib/rgb-backlight/owner

die() { echo "rgb-profile: $*" >&2; exit 1; }

# El perfil es del usuario, no del sistema.  Cuando el script lo ejecuta root
# al arrancar (todavia no hay sesion) se usa el perfil del usuario que anoto
# el instalador en /var/lib/rgb-backlight/owner; ejecutado por el usuario, el
# suyo.  RGB_PROFILE_DIR fuerza una ruta (pruebas).
profile_dir() {
	local owner home
	if [[ -n "${RGB_PROFILE_DIR:-}" ]]; then
		printf '%s' "${RGB_PROFILE_DIR}"
		return 0
	fi
	if [[ "${EUID}" -eq 0 && -r "${OWNER_FILE}" ]]; then
		owner="$(head -n1 "${OWNER_FILE}" 2>/dev/null)"
		home="$(getent passwd "${owner}" | cut -d: -f6)"
		if [[ -n "${home}" && -d "${home}" ]]; then
			printf '%s/.config/rgb-backlight' "${home}"
			return 0
		fi
	fi
	printf '%s' "${XDG_CONFIG_HOME:-${HOME}/.config}/rgb-backlight"
}

CFG_DIR="$(profile_dir)"

state_file() {
	case "$1" in
		keyboard) echo "${CFG_DIR}/keyboard.state" ;;
		mouse)    echo "${CFG_DIR}/mouse.state" ;;
		*)        die "dispositivo desconocido: $1" ;;
	esac
}

led_path() {
	case "$1" in
		keyboard) echo "${KBD_LED}" ;;
		mouse)    echo "${MOUSE_LED}" ;;
		*)        die "dispositivo desconocido: $1" ;;
	esac
}

profile_get() {   # <dispositivo> <clave>
	local file line key="$2"
	file="$(state_file "$1")"
	[[ -r "${file}" ]] || return 1
	while IFS= read -r line; do
		[[ "${line}" == "${key}="* ]] || continue
		printf '%s' "${line#*=}"
		return 0
	done < "${file}"
	return 1
}

valid_hex() { [[ "$1" =~ ^[0-9a-fA-F]{6}$ ]]; }
valid_hex_list() { [[ "$1" =~ ^[0-9a-fA-F]{6}(,[0-9a-fA-F]{6})?$ ]]; }
valid_int() { [[ "$1" =~ ^[0-9]+$ ]] && (( $1 >= $2 && $1 <= $3 )); }

read_attr() { cat "$1" 2>/dev/null; }

# Escribe solo si el valor cambia: menos paquetes USB y menos parpadeos.
write_attr() {   # <led> <atributo> <valor>
	local path="$1/$2"
	[[ "$(read_attr "${path}")" == "$3" ]] && return 0
	printf '%s' "$3" > "${path}" 2>/dev/null
}

wait_led() {   # <led> <segundos>
	local i
	for ((i = 0; i < $2; i++)); do
		[[ -e "$1/brightness" ]] && return 0
		sleep 1
	done
	return 1
}

set_keyboard_brightness() {   # <valor crudo 0..max>
	local i
	[[ "$(read_attr "${KBD_LED}/brightness")" == "$1" ]] && return 0

	# Se usa UPower (como el panel) para no dejar su valor cacheado desfasado.
	for ((i = 1; i <= 6; i++)); do
		"${BUSCTL}" --system call org.freedesktop.UPower \
			/org/freedesktop/UPower/KbdBacklight \
			org.freedesktop.UPower.KbdBacklight SetBrightness i "$1" >/dev/null 2>&1
		[[ "$(read_attr "${KBD_LED}/brightness")" == "$1" ]] && return 0
		sleep 0.5
	done

	# Muy pronto en el arranque UPower puede no estar listo: se escribe el
	# LED directamente (el modulo hace lo mismo por dentro).
	printf '%s' "$1" > "${KBD_LED}/brightness" 2>/dev/null
}

# --- guardar -------------------------------------------------------------

save_device() {
	local dev="$1" led file tmp max bright
	led="$(led_path "${dev}")"
	[[ -e "${led}/brightness" ]] || { echo "rgb-profile: ${dev} no disponible" >&2; return 1; }

	max="$(read_attr "${led}/max_brightness")"; max=${max:-1}
	bright="$(read_attr "${led}/brightness")"

	file="$(state_file "${dev}")"
	mkdir -p "${CFG_DIR}" || return 1

	# Se escribe a un temporal y se renombra: el perfil nunca queda a medias.
	tmp="$(mktemp "${CFG_DIR}/.${dev}.XXXXXX")" || return 1
	{
		echo "# rgb-backlight: estado del dispositivo (se restaura al iniciar sesion)"
		if [[ "${dev}" == keyboard ]]; then
			echo "colors=$(read_attr "${led}/colors")"
			echo "mode=$(read_attr "${led}/mode")"
		else
			echo "color=$(read_attr "${led}/color")"
			echo "color2=$(read_attr "${led}/color2")"
			echo "effect=$(read_attr "${led}/effect")"
		fi
		echo "speed=$(read_attr "${led}/speed")"
		echo "brightness=$(( bright * 100 / max ))"
	} > "${tmp}" || { rm -f "${tmp}"; return 1; }

	mv -f "${tmp}" "${file}"
}

# --- restaurar -----------------------------------------------------------

restore_device() {
	local dev="$1" led max colors color color2 effect speed pct raw
	led="$(led_path "${dev}")"

	[[ -r "$(state_file "${dev}")" ]] || return 2   # sin perfil todavia
	wait_led "${led}" 15 || { echo "rgb-profile: ${dev} no disponible" >&2; return 1; }

	max="$(read_attr "${led}/max_brightness")"; max=${max:-1}

	if [[ "${dev}" == keyboard ]]; then
		if colors="$(profile_get keyboard colors)" && valid_hex_list "${colors}"; then
			write_attr "${led}" colors "${colors}"
		fi
		if effect="$(profile_get keyboard mode)" && valid_int "${effect}" 0 6; then
			write_attr "${led}" mode "${effect}"
		fi
	else
		if color="$(profile_get mouse color)" && valid_hex "${color}"; then
			write_attr "${led}" color "${color}"
		fi
		if color2="$(profile_get mouse color2)" && valid_hex "${color2}"; then
			write_attr "${led}" color2 "${color2}"
		fi
		if effect="$(profile_get mouse effect)" && valid_int "${effect}" 0 6; then
			write_attr "${led}" effect "${effect}"
		fi
	fi

	if speed="$(profile_get "${dev}" speed)" && valid_int "${speed}" 0 2; then
		write_attr "${led}" speed "${speed}"
	fi

	if pct="$(profile_get "${dev}" brightness)" && valid_int "${pct}" 0 100; then
		raw=$(( (pct * max + 50) / 100 ))
		if [[ "${dev}" == keyboard ]]; then
			set_keyboard_brightness "${raw}"
		else
			write_attr "${led}" brightness "${raw}"
		fi
	fi

	return 0
}

# Sin perfil (primer arranque) el teclado se queda apagado, que es el
# comportamiento por defecto del proyecto.
keyboard_off() {
	local i
	wait_led "${KBD_LED}" 15 || return 0
	for ((i = 1; i <= 5; i++)); do
		set_keyboard_brightness 0
		sleep 1
		[[ "$(read_attr "${KBD_LED}/brightness")" == "0" ]] && return 0
	done
}

# --- mostrar -------------------------------------------------------------

show_device() {
	local dev="$1" led
	led="$(led_path "${dev}")"
	echo "== ${dev} =="
	if [[ -r "$(state_file "${dev}")" ]]; then
		echo "  perfil:  $(state_file "${dev}")"
		sed 's/^/    /' "$(state_file "${dev}")"
	else
		echo "  perfil:  (sin guardar)"
	fi
	if [[ -e "${led}/brightness" ]]; then
		echo "  actual:  brillo=$(read_attr "${led}/brightness")/$(read_attr "${led}/max_brightness")"
		if [[ "${dev}" == keyboard ]]; then
			echo "           colors=$(read_attr "${led}/colors") mode=$(read_attr "${led}/mode") speed=$(read_attr "${led}/speed")"
		else
			echo "           color=$(read_attr "${led}/color") color2=$(read_attr "${led}/color2") effect=$(read_attr "${led}/effect") speed=$(read_attr "${led}/speed")"
		fi
	else
		echo "  actual:  (dispositivo no presente)"
	fi
	echo
}

# --- principal -----------------------------------------------------------

action="${1:-restore}"
target="${2:-all}"

case "${target}" in
	keyboard|mouse) devices="${target}" ;;
	all)            devices="keyboard mouse" ;;
	*)              die "dispositivo desconocido: ${target} (keyboard, mouse o all)" ;;
esac

case "${action}" in
	save)
		for dev in ${devices}; do
			save_device "${dev}" || exit 1
		done
		echo "rgb-profile: perfil guardado en ${CFG_DIR}"
		;;
	restore)
		for dev in ${devices}; do
			restore_device "${dev}"
			case $? in
				0) echo "rgb-profile: ${dev} restaurado" ;;
				2) echo "rgb-profile: ${dev} sin perfil guardado" ;;
				*) echo "rgb-profile: no se pudo restaurar ${dev}" >&2 ;;
			esac
		done
		if [[ "${target}" != "mouse" && ! -r "$(state_file keyboard)" ]]; then
			keyboard_off
		fi
		;;
	show)
		for dev in ${devices}; do
			show_device "${dev}"
		done
		;;
	*)
		die "uso: $0 {save|restore|show} [keyboard|mouse|all]"
		;;
esac
