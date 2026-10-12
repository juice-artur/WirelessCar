import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

Item {
    id: root
    implicitWidth: 380

    readonly property var devices: AppController.devices
    readonly property var nus: AppController.nus

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Scan controls
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 56
            color: Theme.surface
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Button {
                    text: root.devices.scanning ? qsTr("Stop") : qsTr("Scan")
                    highlighted: !root.devices.scanning
                    enabled: root.devices.scanning
                             || (root.nus.state === NusClient.Disconnected
                                 || root.nus.state === NusClient.Error)
                    onClicked: {
                        if (root.devices.scanning) {
                            root.devices.stopScan()
                        } else {
                            root.devices.clear()
                            root.devices.startScan()
                        }
                    }
                }

                BusyIndicator {
                    running: root.devices.scanning
                    visible: running
                    implicitWidth: 24
                    implicitHeight: 24
                }

                Item { Layout.fillWidth: true }

                CheckBox {
                    text: qsTr("NUS only")
                    checked: root.devices.nusOnly
                    onToggled: root.devices.nusOnly = checked
                }
            }
        }

        // Discovered device list
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 1
            model: root.devices
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: delegateItem
                required property int index
                required property string name
                required property string address
                required property int rssi
                required property bool isNus

                width: list.width
                implicitHeight: 58
                color: ListView.isCurrentItem ? Theme.surfaceAlt : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            Layout.fillWidth: true
                            text: name.length > 0 ? name : qsTr("(unnamed)")
                            color: Theme.text
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }

                        Label {
                            text: address
                            color: Theme.textMuted
                            font.family: Theme.mono
                            font.pixelSize: 11
                        }
                    }

                    Rectangle {
                        visible: isNus
                        implicitWidth: 40
                        implicitHeight: 18
                        radius: 9
                        color: Qt.rgba(0.24, 0.48, 1, 0.15)
                        border.color: Theme.accent

                        Label {
                            anchors.centerIn: parent
                            text: "NUS"
                            color: Theme.accent
                            font.pixelSize: 10
                        }
                    }

                    Label {
                        text: rssi !== 0 ? rssi + " dBm" : ""
                        color: Theme.textMuted
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 58
                    }
                }

                TapHandler {
                    onTapped: list.currentIndex = delegateItem.index
                }
            }
        }

        // Connect / disconnect
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 68
            color: Theme.surface
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                Button {
                    readonly property int st: root.nus.state
                    readonly property bool connecting: st === NusClient.Connecting
                                                      || st === NusClient.Discovering
                    readonly property bool connected: st === NusClient.Ready
                                                      || st === NusClient.Connected

                    Layout.fillWidth: true
                    text: connecting ? qsTr("Connecting\u2026")
                                     : connected ? qsTr("Disconnect")
                                                 : qsTr("Connect")
                    enabled: connecting || connected
                             || (list.currentIndex >= 0 && !root.devices.scanning)

                    onClicked: {
                        if (connected || connecting)
                            AppController.disconnect()
                        else
                            AppController.connectToDevice(list.currentIndex)
                    }
                }

                Label {
                    Layout.maximumWidth: 120
                    text: root.nus.connectedName
                    color: Theme.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    visible: text.length > 0
                }
            }
        }
    }
}
