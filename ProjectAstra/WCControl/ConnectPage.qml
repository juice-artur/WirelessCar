import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

Item {
    id: root
    property string title: qsTr("Підключення до пристрою")
    property var nav

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ConnectionView {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: Theme.border
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            LogView {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            SendBar {
                Layout.fillWidth: true
            }
        }
    }
}
