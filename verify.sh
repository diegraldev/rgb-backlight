#!/usr/bin/env bash
#
# Verificacion de rgb-backlight
#
#   ~/msi-kbd-backlight/verify.sh
#
echo "=== Modulo cargado ==="
lsmod | grep -E "rgb_backlight|msi_kbd_backlight" || echo "  !! no cargado"

echo
echo "=== Drivers de los dispositivos ==="
for d in /sys/bus/hid/devices/*1462:1562* /sys/bus/hid/devices/*1532:008A*; do
	[ -e "$d" ] || continue
	echo "  $(basename "$d") -> $(basename "$(readlink -f "$d/driver")")"
done

for led in /sys/class/leds/msi::kbd_backlight /sys/class/leds/razer::logo_backlight; do
	echo
	echo "=== ${led} ==="
	if [[ -e "${led}" ]]; then
		echo "  brillo: $(cat ${led}/brightness) / $(cat ${led}/max_brightness)"
		for a in colors color mode speed effect; do
			[[ -e "${led}/${a}" ]] && echo "  ${a}: $(cat ${led}/${a})"
		done
		echo "  permisos: $(ls -l ${led} | grep -E 'colors|color|mode|speed|effect|brightness' | awk '{print $1, $3":"$4}' | tr '\n' ' ')"
	else
		echo "  !! no existe"
	fi
done

echo
echo "=== UPower (brillo del teclado) ==="
for m in GetMaxBrightness GetBrightness; do
	printf "  %-18s " "$m"
	busctl --system call org.freedesktop.UPower /org/freedesktop/UPower/KbdBacklight \
		org.freedesktop.UPower.KbdBacklight "$m" 2>&1 | head -1
done

echo
echo "=== Plasma (lo que ve el widget) ==="
for m in keyboardBrightness keyboardBrightnessMax; do
	printf "  %-22s " "$m"
	busctl --user call org.kde.Solid.PowerManagement \
		/org/kde/Solid/PowerManagement/Actions/KeyboardBrightnessControl \
		org.kde.Solid.PowerManagement.Actions.KeyboardBrightnessControl "$m" 2>&1 | head -1
done

echo
echo "=== Plugin QML ==="
ls -l /usr/lib64/qt6/qml/org/rgb/backlight/ 2>/dev/null | tail -3 || echo "  !! no instalado"

echo
echo "=== Reglas udev ==="
ls -l /etc/udev/rules.d/99-rgb-backlight.rules 2>/dev/null || echo "  (no configurado)"
