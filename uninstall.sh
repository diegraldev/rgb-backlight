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

echo "==> Eliminando el servicio de sesion"
USER_NAME="${SUDO_USER:-${USER}}"
USER_UID="$(id -u "${USER_NAME}" 2>/dev/null || echo 1000)"
USER_HOME="$(getent passwd "${USER_NAME}" 2>/dev/null | cut -d: -f6)"
runuser -u "${USER_NAME}" -- env \
	XDG_RUNTIME_DIR="/run/user/${USER_UID}" \
	DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${USER_UID}/bus" \
	systemctl --user disable --now rgb-backlight-session.service >/dev/null 2>&1 || true
rm -f "${USER_HOME}/.config/systemd/user/rgb-backlight-session.service"
rm -rf "${USER_HOME}/.local/share/rgb-backlight"

echo "==> Descargando el modulo"
rmmod rgb_backlight 2>/dev/null || true

echo "==> Eliminando de DKMS"
dkms remove -m "${PKG}" -v "${VER}" --all || true

echo "==> Limpiando configuracion"
rm -f /etc/modules-load.d/rgb-backlight.conf
rm -f /etc/udev/rules.d/99-rgb-backlight.rules
rm -rf "/usr/src/${PKG}-${VER}"

echo "==> Reactivando la gestion de brillo de systemd"
systemctl unmask systemd-backlight@leds:msi::kbd_backlight.service >/dev/null 2>&1 || true
systemctl unmask systemd-backlight@leds:razer::logo_backlight.service >/dev/null 2>&1 || true

systemctl restart upower 2>/dev/null || true

echo "Hecho."
