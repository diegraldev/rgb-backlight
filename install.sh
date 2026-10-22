#!/usr/bin/env bash
#
# Instalador de rgb-backlight (modulo kernel DKMS + plugin QML + reglas udev)
#
#   sudo ~/rgb-backlight/install.sh
#
set -euo pipefail

PKG=rgb-backlight
VER=1.0
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="/usr/src/${PKG}-${VER}"
QMLDIR="/usr/lib64/qt6/qml/org/rgb/backlight"

if [[ ${EUID} -ne 0 ]]; then
	echo "Este script necesita root:  sudo $0" >&2
	exit 1
fi

USER_NAME="${SUDO_USER:-${USER}}"
GROUP="$(id -gn "${USER_NAME}")"

echo "==> [1/7] Modulo kernel"
rm -rf "${DEST}"
mkdir -p "${DEST}"
cp "${SRC}/rgb-backlight.c" "${SRC}/Makefile" "${SRC}/dkms.conf" "${DEST}/"
dkms add -m "${PKG}" -v "${VER}" 2>/dev/null || true
dkms build --force -m "${PKG}" -v "${VER}"
dkms install --force -m "${PKG}" -v "${VER}"

echo "==> [2/7] Plugin QML (org.rgb.backlight)"
make -C "${SRC}/plugin" clean >/dev/null 2>&1 || true
make -C "${SRC}/plugin"
rm -rf /usr/lib64/qt6/qml/org/msi
install -d "${QMLDIR}"
install -m 644 "${SRC}/plugin/librgbbacklightplugin.so" "${QMLDIR}/"
install -m 644 "${SRC}/plugin/qmldir" "${QMLDIR}/"

echo "==> [3/7] Reglas udev"
sed "s/@GROUP@/${GROUP}/" "${SRC}/udev/99-rgb-backlight.rules" > /etc/udev/rules.d/99-rgb-backlight.rules
udevadm control --reload-rules

# systemd guarda el brillo de los LED al apagar y lo restaura al arrancar.
# Para estos LEDs eso pelea con el perfil (es lo que ponia el teclado blanco
# al arrancar), asi que se desactiva.
systemctl mask systemd-backlight@leds:msi::kbd_backlight.service >/dev/null 2>&1 || true
systemctl mask systemd-backlight@leds:razer::logo_backlight.service >/dev/null 2>&1 || true
rm -f /var/lib/systemd/backlight/*leds:msi::kbd_backlight \
      /var/lib/systemd/backlight/*leds:razer::logo_backlight 2>/dev/null || true

echo "==> [4/7] Carga automatica"
cat > /etc/modules-load.d/rgb-backlight.conf <<'EOF'
rgb-backlight
EOF

echo "==> [5/7] Perfil y servicios (aplicarlo al arrancar y al iniciar sesion)"
USER_HOME="$(getent passwd "${USER_NAME}" | cut -d: -f6)"
USER_UID="$(id -u "${USER_NAME}")"

# Copia del script en el sistema: la usa el servicio de arranque (root).
install -d -m 755 /usr/local/lib/rgb-backlight
install -m 755 "${SRC}/tools/rgb-profile.sh" /usr/local/lib/rgb-backlight/rgb-profile.sh

# El perfil es del usuario, no del sistema: se anota cual es para que el
# servicio de arranque sepa de quien leerlo (todavia no hay sesion).
install -d -m 755 /var/lib/rgb-backlight
printf '%s\n' "${USER_NAME}" > /var/lib/rgb-backlight/owner

# Servicio de sistema: aplica el perfil en cuanto arranca la PC.
install -m 644 "${SRC}/systemd/rgb-backlight-restore.service" \
	/etc/systemd/system/rgb-backlight-restore.service
systemctl daemon-reload
systemctl enable rgb-backlight-restore.service >/dev/null 2>&1 || true

install -d -m 755 "${USER_HOME}/.local/share/rgb-backlight"
install -m 755 "${SRC}/tools/rgb-profile.sh" "${USER_HOME}/.local/share/rgb-backlight/rgb-profile.sh"
rm -f "${USER_HOME}/.local/share/rgb-backlight/rgb-session-init.sh"
install -d -m 755 "${USER_HOME}/.config/systemd/user"
install -m 644 "${SRC}/systemd/rgb-backlight-session.service" "${USER_HOME}/.config/systemd/user/rgb-backlight-session.service"

runuser -u "${USER_NAME}" -- env \
	XDG_RUNTIME_DIR="/run/user/${USER_UID}" \
	DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${USER_UID}/bus" \
	systemctl --user daemon-reload >/dev/null 2>&1 || true
runuser -u "${USER_NAME}" -- env \
	XDG_RUNTIME_DIR="/run/user/${USER_UID}" \
	DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${USER_UID}/bus" \
	systemctl --user enable --now rgb-backlight-session.service || true

# Si el modulo ya esta cargado, la version nueva se aplicara al reiniciar.
if lsmod | grep -q rgb_backlight; then
	echo
	echo "!! El modulo ya estaba cargado: reinicia para aplicar la version nueva."
	echo "      sudo reboot"
	exit 0
fi

echo "==> [6/7] Cargando el modulo"
modprobe rgb-backlight || true

udevadm trigger --subsystem-match=leds --action=add || true
systemctl restart upower || true

echo "==> [7/7] Reiniciando upower y powerdevil"
systemctl start rgb-backlight-restore.service >/dev/null 2>&1 || true
if [[ -n "${XDG_RUNTIME_DIR:-}" ]] && systemctl --user is-system-running >/dev/null 2>&1; then
	systemctl --user restart plasma-powerdevil.service
else
	echo "   Ejecuta despues como usuario: systemctl --user restart plasma-powerdevil.service"
fi

sleep 1

echo
echo "=============================================================="
echo " Listo. Estado:"
echo "=============================================================="
for led in /sys/class/leds/msi::kbd_backlight /sys/class/leds/razer::logo_backlight; do
	if [[ -e "${led}" ]]; then
		echo "  ${led}"
		echo "     brillo=$(cat ${led}/brightness) max=$(cat ${led}/max_brightness)"
	else
		echo "  ${led}: no presente (dispositivo no conectado o modulo sin cargar)"
	fi
done
echo
echo "  Verificacion:  ~/rgb-backlight/verify.sh"
echo "  Desinstalar:   sudo ~/rgb-backlight/uninstall.sh"
