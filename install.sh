#!/usr/bin/env bash
#
# Instalador de rgb-backlight (modulo kernel DKMS + plugin QML + reglas udev)
#
#   sudo ~/msi-kbd-backlight/install.sh
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

echo "==> [1/6] Modulo kernel"
rm -rf "${DEST}"
mkdir -p "${DEST}"
cp "${SRC}/rgb-backlight.c" "${SRC}/Makefile" "${SRC}/dkms.conf" "${DEST}/"
dkms add -m "${PKG}" -v "${VER}" 2>/dev/null || true
dkms build --force -m "${PKG}" -v "${VER}"
dkms install --force -m "${PKG}" -v "${VER}"

echo "==> [2/6] Plugin QML (org.rgb.backlight)"
make -C "${SRC}/plugin" clean >/dev/null 2>&1 || true
make -C "${SRC}/plugin"
rm -rf /usr/lib64/qt6/qml/org/msi
install -d "${QMLDIR}"
install -m 644 "${SRC}/plugin/librgbbacklightplugin.so" "${QMLDIR}/"
install -m 644 "${SRC}/plugin/qmldir" "${QMLDIR}/"

echo "==> [3/6] Reglas udev"
sed "s/@GROUP@/${GROUP}/" "${SRC}/udev/99-rgb-backlight.rules" > /etc/udev/rules.d/99-rgb-backlight.rules
udevadm control --reload-rules

echo "==> [4/6] Carga automatica"
cat > /etc/modules-load.d/rgb-backlight.conf <<'EOF'
rgb-backlight
EOF

# Si el modulo ya esta cargado, la version nueva se aplicara al reiniciar.
if lsmod | grep -q rgb_backlight; then
	echo
	echo "!! El modulo ya estaba cargado: reinicia para aplicar la version nueva."
	echo "      sudo reboot"
	exit 0
fi

echo "==> [5/6] Cargando el modulo"
modprobe rgb-backlight || true

udevadm trigger --subsystem-match=leds --action=add || true
systemctl restart upower || true

echo "==> [6/6] Reiniciando upower y powerdevil"
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
echo "  Verificacion:  ~/msi-kbd-backlight/verify.sh"
echo "  Desinstalar:   sudo ~/msi-kbd-backlight/uninstall.sh"
