/*
    Muestra un color del teclado y permite cambiarlo con un selector.

    Se usa así:
        KeyboardColorButton {
            swatchColor: keyboardLook.color
            onPicked: (color) => keyboardLook.color = color
        }
*/
import QtQuick
import QtQuick.Dialogs

import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

PlasmaComponents3.AbstractButton {
    id: root

    /* Color mostrado (solo lectura: el valor real vive en el modulo) */
    property color swatchColor: "transparent"

    /* Se emite cuando el usuario elige un color */
    signal picked(color color)

    implicitWidth: Kirigami.Units.iconSizes.small
    implicitHeight: implicitWidth
    focusPolicy: Qt.StrongFocus

    Accessible.name: i18nc("Placeholder is keyboard color", "Keyboard color")

    background: Rectangle {
        radius: width / 2
        color: root.swatchColor
        border.width: 1
        border.color: Kirigami.Theme.activeTextColor
        opacity: root.enabled ? 1 : 0.4
    }

    onClicked: {
        colorDialog.selectedColor = root.swatchColor;
        colorDialog.open();
    }

    ColorDialog {
        id: colorDialog
        title: i18nc("Placeholder is keyboard color", "Keyboard color")
        onAccepted: root.picked(selectedColor)
    }
}
