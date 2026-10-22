#!/usr/bin/env bash
#
# Restaura los cambios del widget KDE Control Station (seccion "Keyboard RGB").
#
#   ~/msi-kbd-backlight/restore-widget.sh
#
# Util cuando el widget se actualiza desde GitHub y se pierden los cambios.
#
set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/widget-patch"
DST="${HOME}/.local/share/plasma/plasmoids/KdeControlStation/contents"

if [[ ! -d "${DST}" ]]; then
	echo "No encuentro el widget en ${DST}" >&2
	exit 1
fi

echo "==> Restaurando los cambios del widget"
cp -v "${SRC}"/ui/FullRepresentation.qml "${DST}/ui/" 2>/dev/null || true
cp -v "${SRC}"/ui/pages/*.qml "${DST}/ui/pages/" 2>/dev/null || true
cp -v "${SRC}"/ui/components/Rgb.qml "${DST}/ui/components/" 2>/dev/null || true
cp -v "${SRC}"/ui/pages/components/brightness/KeyboardColorButton.qml "${DST}/ui/pages/components/brightness/" 2>/dev/null || true
cp -v "${SRC}"/ui/config/configAppearance.qml "${DST}/ui/config/" 2>/dev/null || true
cp -v "${SRC}"/ui/layouts/*.qml "${DST}/ui/layouts/" 2>/dev/null || true
cp -v "${SRC}"/ui/layouts/components/Custom/Model.qml "${DST}/ui/layouts/components/Custom/" 2>/dev/null || true
cp -v "${SRC}"/config/main.xml "${DST}/config/" 2>/dev/null || true

echo
echo "Listo. Reinicia Plasma para aplicarlo:  systemctl --user restart plasma-plasmashell.service"
