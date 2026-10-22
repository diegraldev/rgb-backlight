#!/usr/bin/env bash
#
# Desinstala rgb-backlight (modulo kernel + plugin QML + reglas udev)
#
#   sudo ~/msi-kbd-backlight/uninstall.sh
#
set -euo pipefail

PKG=rgb-backlight
VER=1.0

if [[ ${EUID} -ne 0 ]]; then
	echo "Este script necesita root:  sudo $0" >&2
	exit 1
fi

echo "==> Eliminando el plugin QML"
rm -rf /usr/lib64/qt6/qml/org/rgb /usr/lib64/qt6/qml/org/msi

echo "==> Descargando el modulo"
rmmod rgb_backlight 2>/dev/null || true

echo "==> Eliminando de DKMS"
dkms remove -m "${PKG}" -v "${VER}" --all || true

echo "==> Limpiando configuracion"
rm -f /etc/modules-load.d/rgb-backlight.conf
rm -f /etc/udev/rules.d/99-rgb-backlight.rules
rm -rf "/usr/src/${PKG}-${VER}"

systemctl restart upower 2>/dev/null || true

echo "Hecho."
