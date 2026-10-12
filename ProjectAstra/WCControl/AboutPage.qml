import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

Item {
    id: root
    property string title: qsTr("Про застосунок")
    property var nav

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: content
            width: Math.min(flick.width - 64, 680)
            x: (flick.width - width) / 2
            y: 24
            spacing: 12

            Label {
                text: qsTr("WCControl")
                color: Theme.text
                font.pixelSize: 24
                font.bold: true
            }

            Label {
                text: qsTr("Версія %1").arg(Qt.application.version)
                color: Theme.textMuted
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text
                text: qsTr("Застосунок для оновлення прошивки STM32 (формати .elf та .bin) через Bluetooth LE.")
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.border
            }

            Label {
                text: qsTr("Транспорт")
                color: Theme.text
                font.bold: true
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                text: qsTr("BLE-централь через сервіс Nordic UART (NUS) "
                        + "6e400001-b5a3-f393-e0a9-e50e24dcca9e. "
                        + "TX (notify) 6e400003-\u2026, RX (write) 6e400002-\u2026.")
            }

            Label {
                text: qsTr("Протокол завантажувача")
                color: Theme.text
                font.bold: true
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                text: qsTr("Байтові статуси: 0xA5 — STM32 готовий; 0x06 — ACK; 0x15 — NACK; "
                        + "0xA6 — стирання виконано; 0x4F — IMAGE OK; 0xFE — IMAGE BAD. "
                        + "Заголовок образу — 20 байтів (magic 0x52545341). "
                        + "Сторінка Flash — 2048 байтів.")
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.border
            }

            Button {
                Layout.alignment: Qt.AlignLeft
                text: qsTr("Назад")
                onClicked: if (nav) nav.pop()
            }

            Item { Layout.preferredHeight: 8 }
        }
    }
}
