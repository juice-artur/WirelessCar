import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// Shared top bar: optional back button, current page title and BLE state badge.
// Kept decoupled from the StackView via canGoBack/backRequested.
Rectangle {
    id: root
    implicitHeight: 48
    color: Theme.surface
    border.color: Theme.border

    property bool canGoBack: false
    property string title: ""

    signal backRequested()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 12

        Button {
            visible: root.canGoBack
            text: "\u2190"
            // FluentWinUI3 buttons have a large implicit width (~98px); pin the
            // minimum too so the header button stays compact next to the title.
            Layout.minimumWidth: 40
            Layout.preferredWidth: 40
            onClicked: root.backRequested()
        }

        Label {
            text: root.title
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
            elide: Text.ElideRight
            Layout.maximumWidth: 320
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            implicitHeight: 26
            implicitWidth: badge.implicitWidth + 22
            radius: 13
            color: Theme.surfaceAlt
            border.color: Theme.border

            RowLayout {
                id: badge
                anchors.centerIn: parent
                spacing: 8

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: {
                        switch (AppController.nus.state) {
                        case NusClient.Ready: return Theme.ok
                        case NusClient.Error: return Theme.error
                        case NusClient.Disconnected: return Theme.textMuted
                        default: return Theme.warn
                        }
                    }
                }

                Label {
                    text: AppController.nus.stateText
                    color: Theme.text
                    font.pixelSize: 12
                }
            }
        }

    }
}
