import QtQuick 2.15
import QtQuick.Controls 2.0
import QtQuick.Layouts 1.15

import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

import org.rgb.backlight

import "../lib" as Lib
import "components/brightness" as BrightnessComponents

/*
    Página "Keyboard RGB": control del aspecto del teclado MSI Mystic Light
    y del ratón Razer Viper Mini.

    Todo se escribe en el módulo del kernel (rgb-backlight), que es el
    único que habla con el hardware; el brillo del teclado pasa por UPower
    para no pelearnos con PowerDevil.
*/
PageTemplate {
    id: rgbPage

    sectionTitle: i18n("RGB")

    property int contentItemHeight: contentItem.implicitHeight

    RgbDeviceControl {
        id: keyboardControl
        device: "keyboard"
    }

    RgbDeviceControl {
        id: mouseControl
        device: "mouse"
    }

    PlasmaComponents3.ScrollView {
        id: scrollView

        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            id: contentItem

            width: scrollView.availableWidth
            spacing: Kirigami.Units.largeSpacing

            BrightnessComponents.RgbDeviceSection {
                title: i18n("Keyboard")
                iconName: "input-keyboard"
                control: keyboardControl
            }

            BrightnessComponents.RgbDeviceSection {
                title: i18n("Mouse")
                iconName: "input-mouse"
                control: mouseControl
            }
        }
    }

    /* Sincroniza con el estado real de los dispositivos al abrir la página */
    onVisibleChanged: {
        if (visible) {
            keyboardControl.refresh();
            mouseControl.refresh();
        }
    }
    Component.onCompleted: {
        keyboardControl.refresh();
        mouseControl.refresh();
    }
}
