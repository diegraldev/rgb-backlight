#!/usr/bin/env bash
#
# rgb-backlight: deja el teclado MSI apagado al iniciar la sesion de Plasma.
#
# POR QUE HAY QUE HACERLO AQUI Y NO EN EL ARRANQUE DEL KERNEL
#
# El modulo del kernel apaga el teclado en cuanto lo detecta (durante el
# arranque), pero el firmware del Mystic Light aplica su color por defecto
# (blanco) DESPUES, cuando el dispositivo termina de inicializarse.  Por eso
# al iniciar sesion el teclado ya estaba blanco otra vez.
#
# La solucion es apagar el teclado cuando la sesion grafica ya esta en
# marcha, que es cuando el firmware ya ha terminado de aplicarse.
#
# El brillo se cambia por la misma via que usa el deslizador de Plasma
# (UPower -> PowerDevil), para no pelearnos con el gestor de energia.
#
set -uo pipefail

LED=/sys/class/leds/msi::kbd_backlight
BUSCTL=/usr/bin/busctl

# 1) Espera a que el modulo del kernel y el LED esten disponibles
for _ in $(seq 1 30); do
	[[ -e "${LED}/brightness" ]] && break
	sleep 1
done

if [[ ! -e "${LED}/brightness" ]]; then
	echo "rgb-backlight: el teclado no esta disponible, nada que hacer" >&2
	exit 0
fi

# 2) Apaga el teclado y comprueba que se queda apagado.  Se reintenta un
#    poco porque el firmware puede aplicar su color por defecto con retraso.
brillo=""
for intento in $(seq 1 15); do
	"${BUSCTL}" --system call org.freedesktop.UPower \
		/org/freedesktop/UPower/KbdBacklight \
		org.freedesktop.UPower.KbdBacklight SetBrightness i 0 >/dev/null 2>&1

	sleep 2

	brillo="$(cat "${LED}/brightness" 2>/dev/null || echo -1)"
	if [[ "${brillo}" == "0" ]]; then
		echo "rgb-backlight: teclado apagado (intento ${intento})"
		exit 0
	fi
done

echo "rgb-backlight: no se ha podido dejar el teclado apagado (brillo=${brillo})" >&2
exit 0
