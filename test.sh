#!/usr/bin/env bash
#
# Comprobacion rapida tras instalar/cargar rgb-backlight
#
#   ~/msi-kbd-backlight/test.sh
#
set -uo pipefail

KB=/sys/class/leds/msi::kbd_backlight
MO=/sys/class/leds/razer::logo_backlight

echo "=== Modulo ==="
lsmod | grep rgb_backlight || echo "  !! no cargado"

echo
echo "=== Dispositivos ==="
for d in /sys/bus/hid/devices/*1462:1562* /sys/bus/hid/devices/*1532:008A*; do
	[ -e "$d" ] || continue
	echo "  $(basename "$d") -> $(basename "$(readlink -f "$d/driver")")"
done

echo
echo "=== LEDs ==="
for led in "$KB" "$MO"; do
	if [[ -e "$led" ]]; then
		echo "  ${led}: brillo=$(cat "$led"/brightness)/$(cat "$led"/max_brightness)"
	else
		echo "  ${led}: NO EXISTE"
	fi
done

echo
echo "=== Prueba: raton en rojo al 50% (efecto estatico) ==="
if [[ -w "$MO/color" ]]; then
	printf "ff0000" > "$MO/color" && echo "  color OK" || echo "  ERROR al escribir el color"
	echo 50 > "$MO/brightness" && echo "  brillo OK" || echo "  ERROR al escribir el brillo"
	printf "1" > "$MO/effect" && echo "  efecto OK" || echo "  ERROR al escribir el efecto"
	printf "2" > "$MO/effect" && echo "  ciclo OK" || echo "  ERROR al escribir el ciclo"
	echo "  -> raton: color=$(cat "$MO"/color) brillo=$(cat "$MO"/brightness) efecto=$(cat "$MO"/effect)"
else
	echo "  (sin permiso de escritura en $MO)"
fi

echo
echo "=== Prueba: teclado en azul al 60% ==="
if [[ -w "$KB/colors" ]]; then
	printf "0000ff,ffffff" > "$KB/colors" && echo "  color OK" || echo "  ERROR al escribir el color"
	printf "1" > "$KB/mode" && echo "  modo OK" || echo "  ERROR al escribir el modo"
	busctl --system call org.freedesktop.UPower /org/freedesktop/UPower/KbdBacklight \
		org.freedesktop.UPower.KbdBacklight SetBrightness i 6 >/dev/null 2>&1 \
		&& echo "  brillo OK" || echo "  ERROR al escribir el brillo"
	echo "  -> teclado: color=$(cat "$KB"/colors) modo=$(cat "$KB"/mode) brillo=$(cat "$KB"/brightness)"
else
	echo "  (sin permiso de escritura en $KB)"
fi
