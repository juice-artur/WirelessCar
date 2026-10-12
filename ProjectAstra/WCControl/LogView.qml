import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

Item {
    id: root

    readonly property var log: AppController.log

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 32
            color: Theme.surfaceAlt
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10

                Label {
                    text: qsTr("Log")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: root.log.count + qsTr(" lines")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
            }
        }

        ListView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.log
            onCountChanged: Qt.callLater(view.positionViewAtEnd)

            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: entry
                required property string time
                required property int direction // 0 = rx, 1 = tx, 2 = info
                required property string hex
                required property string ascii
                required property string message
                required property int level // 0 = normal, 1 = warning, 2 = error

                width: view.width
                implicitHeight: row.implicitHeight + 6

                RowLayout {
                    id: row
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10

                    Label {
                        text: entry.time
                        color: Theme.textMuted
                        font.family: Theme.mono
                        font.pixelSize: 11
                    }

                    Label {
                        visible: entry.direction !== 2
                        text: entry.direction === 1 ? "\u2192" : "\u2190"
                        color: entry.direction === 1 ? Theme.accent : Theme.ok
                        font.pixelSize: 12
                    }

                    Label {
                        visible: entry.direction !== 2
                        Layout.fillWidth: true
                        text: entry.hex
                        color: Theme.text
                        font.family: Theme.mono
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    Label {
                        visible: entry.direction !== 2 && entry.ascii.length > 0
                        text: entry.ascii
                        color: Theme.textMuted
                        font.family: Theme.mono
                        font.pixelSize: 12
                    }

                    Label {
                        visible: entry.direction === 2
                        Layout.fillWidth: true
                        text: entry.message
                        color: entry.level === 2 ? Theme.error
                             : entry.level === 1 ? Theme.warn
                             : Theme.textMuted
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
