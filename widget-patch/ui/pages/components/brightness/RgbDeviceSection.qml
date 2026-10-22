/*
    Sección plegable de un dispositivo RGB (teclado o ratón) dentro de la
    página "Keyboard RGB": brillo, colores, efecto y velocidad.

    Se usa así:
        RgbDeviceSection {
            title: i18n("Keyboard")
            iconName: "input-keyboard"
            control: keyboardControl
        }
*/
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: section

    /* RgbDeviceControl del dispositivo */
    property var control
    property string title
    property string iconName

    /* Plegado por defecto */
    property bool expanded: false

    spacing: Kirigami.Units.smallSpacing
    visible: control ? control.available : false

    /* Cabecera: icono + nombre + flechita */
    PlasmaComponents3.ItemDelegate {
        id: header

        Layout.fillWidth: true
        highlighted: section.expanded
        down: section.expanded
        onClicked: section.expanded = !section.expanded

        Accessible.name: section.title
        Accessible.description: i18n("Show or hide the settings of this device")

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: section.iconName
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
            }

            PlasmaComponents3.Label {
                text: section.title
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Kirigami.Icon {
                source: "arrow-down"
                rotation: section.expanded ? 0 : -90
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small

                Behavior on rotation {
                    NumberAnimation {
                        duration: Kirigami.Units.veryShortDuration
                        easing.type: Easing.InOutQuad
                    }
                }
            }
        }
    }

    /* Contenido (se muestra al desplegar) */
    ColumnLayout {
        id: content

        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.topMargin: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        visible: section.expanded
        opacity: visible ? 1 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.InOutQuad
            }
        }

        /* Brillo */
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true

                PlasmaComponents3.Label {
                    text: i18n("Brightness")
                }

                Item {
                    Layout.fillWidth: true
                }

                PlasmaComponents3.Label {
                    text: section.control ? Math.round(section.control.brightness) + "%" : ""
                }
            }

            PlasmaComponents3.Slider {
                id: brightnessSlider
                Layout.fillWidth: true

                from: 0
                to: 100
                stepSize: 1
                value: section.control ? section.control.brightness : 0

                Accessible.name: i18n("Brightness")

                onMoved: if (section.control) section.control.brightness = value

                // Al arrastrar el slider se rompe el binding
                Connections {
                    target: section.control
                    function onBrightnessChanged() {
                        brightnessSlider.value = section.control.brightness;
                    }
                }
            }
        }

        /* Colores */
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.Label {
                text: section.control && section.control.hasSecondaryColor
                      ? i18n("Colors")
                      : i18n("Color")
                Layout.alignment: Qt.AlignTop
            }

            Item {
                Layout.fillWidth: true
            }

            KeyboardColorButton {
                swatchColor: section.control ? section.control.color : "transparent"
                onPicked: (color) => { if (section.control) section.control.color = color; }
            }

            KeyboardColorButton {
                visible: section.control ? section.control.hasSecondaryColor : false
                swatchColor: section.control ? section.control.secondaryColor : "transparent"
                onPicked: (color) => { if (section.control) section.control.secondaryColor = color; }
            }
        }

        /* Efecto */
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.Label {
                text: i18n("Effect")
                Layout.alignment: Qt.AlignTop
            }

            Item {
                Layout.fillWidth: true
            }

            PlasmaComponents3.ComboBox {
                id: effectCombo
                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                model: section.control ? section.control.effects : []
                currentIndex: section.control ? section.control.effect : 0

                onActivated: (index) => { if (section.control) section.control.effect = index; }

                // Al cambiar el efecto se rompe el binding
                Connections {
                    target: section.control
                    function onEffectChanged() {
                        effectCombo.currentIndex = section.control.effect;
                    }
                }
            }
        }

        /* Velocidad (solo el teclado) */
        RowLayout {
            Layout.fillWidth: true
            visible: section.control ? section.control.hasSpeed : false
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.Label {
                text: i18n("Effect speed")
                Layout.alignment: Qt.AlignTop
            }

            Item {
                Layout.fillWidth: true
            }

            PlasmaComponents3.ComboBox {
                id: speedCombo
                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                model: [i18n("Slow"), i18n("Medium"), i18n("Fast")]
                currentIndex: section.control ? section.control.speed : 1

                onActivated: (index) => { if (section.control) section.control.speed = index; }

                Connections {
                    target: section.control
                    function onSpeedChanged() {
                        speedCombo.currentIndex = section.control.speed;
                    }
                }
            }
        }
    }
}
