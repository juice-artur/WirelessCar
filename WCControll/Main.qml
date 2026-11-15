import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs


ApplicationWindow {
    id: window
    width: 780
    height: 720
    minimumWidth: 560
    minimumHeight: 520
    visible: true
    title: qsTr("Astra BLE OTA")

    OtaController {
        id: ota
    }

    FileDialog {
        id: imageDialog
        title: qsTr("Select an Astra .ota image")
        nameFilters: [
            qsTr("Astra OTA image (*.ota)"),
            qsTr("All files (*)")
        ]
        onAccepted: ota.loadImage(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTr("BLE devices")
                font.bold: true
                font.pixelSize: 16
            }

            Item { Layout.fillWidth: true }

            Label {
                text: ota.connected ? qsTr("Connected") : qsTr("Disconnected")
                color: ota.connected ? "#2e7d32" : "#9e9e9e"
            }

            Button {
                text: ota.scanning ? qsTr("Scanning...") : qsTr("Scan")
                enabled: !ota.scanning && !ota.busy && !ota.connected
                onClicked: ota.startScan()
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            padding: 1

            ListView {
                id: deviceList
                anchors.fill: parent
                clip: true
                model: ota.devices

                delegate: ItemDelegate {
                    width: ListView.view.width
                    enabled: !ota.busy && !ota.connected

                    contentItem: RowLayout {
                        Label {
                            text: modelData.name
                            font.bold: true
                        }
                        Label {
                            text: modelData.address
                            color: palette.mid
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: modelData.rssi + qsTr(" dBm")
                            color: palette.mid
                        }
                    }

                    onClicked: ota.connectDevice(modelData.address)
                }
            }

            Label {
                anchors.centerIn: parent
                visible: ota.devices.length === 0
                text: ota.scanning ? qsTr("Scanning...")
                                   : qsTr("No devices yet - press Scan.")
                color: palette.mid
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: ota.imageInfo.length > 0
                      ? ota.imageInfo
                      : qsTr("No .ota image loaded")
                color: ota.imageInfo.length > 0 ? palette.text : palette.mid
            }

            Button {
                text: qsTr("Select .ota...")
                enabled: !ota.busy
                onClicked: imageDialog.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: ota.busy ? qsTr("Updating...") : qsTr("Start OTA update")
                highlighted: true
                enabled: ota.connected && ota.imageInfo.length > 0 && !ota.busy
                onClicked: ota.startUpdate()
            }

            Button {
                text: qsTr("Disconnect")
                enabled: ota.connected && !ota.busy
                onClicked: ota.disconnectDevice()
            }

            Item { Layout.fillWidth: true }

            Label {
                text: qsTr("%1 %").arg(Math.round(ota.progress * 100))
                color: palette.mid
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 1
            value: ota.progress
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 1

            ScrollView {
                id: logScroll
                anchors.fill: parent
                clip: true

                TextArea {
                    id: logArea
                    width: logScroll.availableWidth
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    font.family: "Consolas"
                    font.pixelSize: 12
                    text: ota.statusText

                    onTextChanged: {
                        if (logScroll.contentItem) {
                            logScroll.contentItem.contentY =
                                Math.max(0, logScroll.contentItem.contentHeight
                                            - logScroll.height)
                        }
                    }
                }
            }
        }
    }
}
