import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 960
    height: 640
    minimumWidth: 640
    minimumHeight: 420
    visible: true
    title: qsTr("WCControl \u2014 STM32 OTA over BLE")

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AppHeader {
            Layout.fillWidth: true
            canGoBack: stack.depth > 1
            title: stack.currentItem ? stack.currentItem.title : qsTr("WCControl")
            onBackRequested: stack.pop()
        }

        StackView {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: HomePage { nav: stack }

            pushEnter: Transition {
                NumberAnimation {
                    property: "x"; from: stack.width; to: 0
                    duration: 220; easing.type: Easing.OutCubic
                }
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 220 }
            }
            pushExit: Transition {
                NumberAnimation {
                    property: "x"; from: 0; to: -stack.width * 0.3
                    duration: 220; easing.type: Easing.OutCubic
                }
                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 220 }
            }
            popEnter: Transition {
                NumberAnimation {
                    property: "x"; from: -stack.width * 0.3; to: 0
                    duration: 220; easing.type: Easing.OutCubic
                }
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 220 }
            }
            popExit: Transition {
                NumberAnimation {
                    property: "x"; from: 0; to: stack.width
                    duration: 220; easing.type: Easing.OutCubic
                }
                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 220 }
            }
        }
    }
}
