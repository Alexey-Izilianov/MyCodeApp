import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Прогресс фоновой загрузки большого файла
Rectangle {
    id: overlay
    required property DocumentManager manager

    visible: manager.loading
    color: "#99000000"

    MouseArea { // глушим клики по интерфейсу под оверлеем
        anchors.fill: parent
        hoverEnabled: true
    }

    Rectangle {
        anchors.centerIn: parent
        width: 360
        height: 84
        radius: 6
        color: Theme.chrome
        border.color: Theme.border

        Column {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            Row {
                width: parent.width
                Label {
                    width: parent.width - percent.width
                    text: qsTr("Открытие %1").arg(overlay.manager.loadingName)
                    elide: Text.ElideMiddle
                    color: Theme.text
                }
                Label {
                    id: percent
                    text: overlay.manager.loadPercent + " %"
                    color: Theme.muted
                }
            }
            Rectangle {
                width: parent.width
                height: 2
                color: Theme.border
                Rectangle {
                    width: parent.width * overlay.manager.loadPercent / 100
                    height: parent.height
                    color: Theme.accent
                }
            }
        }
    }
}
