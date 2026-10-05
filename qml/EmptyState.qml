import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Экран без открытых файлов: подсказки действий с клавишами
Rectangle {
    id: empty
    signal actionRequested(string action)
    color: Theme.editor

    Column {
        anchors.centerIn: parent
        spacing: 10

        Repeater {
            model: [
                { text: qsTr("Открыть файл"), keys: "Ctrl+O", action: "file" },
                { text: qsTr("Открыть папку"), keys: "Ctrl+K Ctrl+O", action: "folder" },
                { text: qsTr("Перейти к файлу"), keys: "Ctrl+P", action: "quickOpen" },
                { text: qsTr("Найти в проекте"), keys: "Ctrl+Shift+F", action: "search" },
                { text: qsTr("Перетащите файл или папку в окно"), keys: "", action: "" }
            ]
            Row {
                id: hint
                required property var modelData
                spacing: 24

                Label {
                    width: 240
                    horizontalAlignment: Text.AlignRight
                    text: hint.modelData.text
                    color: hint.modelData.action !== "" && link.containsMouse ? Theme.text : Theme.muted
                    font.underline: hint.modelData.action !== "" && link.containsMouse
                    MouseArea {
                        id: link
                        anchors.fill: parent
                        enabled: hint.modelData.action !== ""
                        hoverEnabled: true
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: empty.actionRequested(hint.modelData.action)
                    }
                }
                Label {
                    width: 120
                    text: hint.modelData.keys
                    color: Theme.faint
                }
            }
        }
    }
}
