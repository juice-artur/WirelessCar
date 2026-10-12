import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

Item {
    id: root
    property string title: qsTr("WCControl")
    property var nav

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 360)
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("WCControl")
            color: Theme.text
            font.pixelSize: 32
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Оновлення прошивки STM32 через Bluetooth LE")
            color: Theme.textMuted
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Item { Layout.preferredHeight: 8 }

        Button {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            text: qsTr("Підключитися")
            highlighted: true
            onClicked: if (nav) nav.push(Qt.resolvedUrl("ConnectPage.qml"), { nav: nav })
        }

        Button {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            text: qsTr("Про застосунок")
            onClicked: if (nav) nav.push(Qt.resolvedUrl("AboutPage.qml"), { nav: nav })
        }
    }
}
