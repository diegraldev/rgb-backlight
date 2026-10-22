import QtQml 2.15
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.0

import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami

import "../lib" as Lib

/*
    Boton que abre la pagina "Keyboard RGB" (teclado y raton).
*/
Lib.CardButton {
    id: rgbButton

    Layout.fillHeight: true
    Layout.fillWidth: true

    title: i18n("RGB")
    shouldStickIconSize: true

    Kirigami.Icon {
        anchors.fill: parent
        source: "input-keyboard-color"
    }

    onClicked: {
        var pageHeight = rgbPage.contentItemHeight + rgbPage.headerHeight;
        fullRep.togglePage(fullRep.defaultInitialWidth, pageHeight, rgbPage);
    }
}
