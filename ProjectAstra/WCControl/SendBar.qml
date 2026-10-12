import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: 52
    color: Theme.surface
    border.color: Theme.border

    readonly property var nus: AppController.nus

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        TextField {
            id: hexField
            Layout.fillWidth: true
            font.family: Theme.mono
            placeholderText: qsTr("hex, e.g. A5 or 06 00 01")
            enabled: root.nus.canSend
            onAccepted: if (sendButton.enabled) sendButton.clicked()
        }

        Button {
            id: sendButton
            text: qsTr("Send")
            enabled: hexField.text.length > 0 && root.nus.canSend
            onClicked: {
                if (root.nus.sendHex(hexField.text))
                    hexField.clear()
            }
        }

        Button {
            text: qsTr("Clear log")
            onClicked: AppController.log.clear()
        }
    }
}
