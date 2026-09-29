import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Экран без открытых файлов: подсказки действий с клавишами
Rectangle {
    id: empty
    signal openRequested()
    color: Theme.editor

    Column {
        anchors.centerIn: parent
        spacing: 10

        Repeater {
            model: [
                { text: qsTr("Открыть файл"), keys: "Ctrl+O", action: true },
                { text: qsTr("Найти в файле"), keys: "Ctrl+F", action: false },
                { text: qsTr("Перетащите файл в окно"), keys: "", action: false }
            ]
            Row {
                required property var modelData
                spacing: 24

                Label {
                    width: 200
                    horizontalAlignment: Text.AlignRight
                    text: parent.modelData.text
                    color: parent.modelData.action && link.containsMouse ? Theme.text : Theme.muted
                    font.underline: parent.modelData.action && link.containsMouse
                    MouseArea {
                        id: link
                        anchors.fill: parent
                        enabled: parent.parent.modelData.action
                        hoverEnabled: true
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: empty.openRequested()
                    }
                }
                Label {
                    width: 120
                    text: parent.modelData.keys
                    color: Theme.faint
                }
            }
        }
    }
}
