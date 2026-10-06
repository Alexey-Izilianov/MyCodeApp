import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Плавающая строка поиска в правом верхнем углу редактора
Rectangle {
    id: bar
    required property EditorView editor
    property int current: 0
    property int total: 0

    function open() {
        visible = true
        input.forceActiveFocus()
        input.selectAll()
        if (input.text !== "")
            editor.find(input.text, caseButton.checked)
    }
    function close() {
        visible = false
        editor.clearSearch()
        editor.forceActiveFocus()
    }

    visible: false
    width: 380
    height: 36
    radius: 6
    color: Theme.chrome
    border.color: Theme.border

    Connections {
        target: bar.editor
        function onSearchUpdated(current, total) {
            bar.current = current
            bar.total = total
        }
    }

    Timer {
        id: debounce
        interval: 200
        onTriggered: bar.editor.find(input.text, caseButton.checked)
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 6
        spacing: 2

        TextField {
            id: input
            width: parent.width - status.width - 4 * 26 - 12
            height: parent.height
            leftPadding: 0
            placeholderText: qsTr("Найти")
            placeholderTextColor: Theme.faint
            color: Theme.text
            selectionColor: Theme.selection
            selectedTextColor: Theme.text
            selectByMouse: true
            verticalAlignment: TextInput.AlignVCenter
            background: null

            onTextChanged: debounce.restart()
            onAccepted: bar.editor.findNext()
            Keys.onEscapePressed: bar.close()
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_F3 || (event.key === Qt.Key_Return && event.modifiers & Qt.ShiftModifier)) {
                    event.modifiers & Qt.ShiftModifier ? bar.editor.findPrev() : bar.editor.findNext()
                    event.accepted = true
                }
            }
        }

        Label {
            id: status
            anchors.verticalCenter: parent.verticalCenter
            width: 84
            horizontalAlignment: Text.AlignRight
            rightPadding: 6
            font.pixelSize: Theme.smallFontSize
            color: input.text !== "" && bar.total === 0 ? Theme.error : Theme.faint
            text: input.text === "" ? ""
                : bar.total === 0 ? qsTr("Нет совпадений")
                : (bar.current > 0 ? bar.current : "?") + qsTr(" из ") + bar.total
        }

        IconButton {
            id: caseButton
            anchors.verticalCenter: parent.verticalCenter
            glyph: "Aa"
            checkable: true
            tip: qsTr("Учитывать регистр")
            onToggled: debounce.restart()
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: "↑"
            tip: qsTr("Предыдущее (Shift+F3)")
            onClicked: bar.editor.findPrev()
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: "↓"
            tip: qsTr("Следующее (F3)")
            onClicked: bar.editor.findNext()
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            tip: qsTr("Закрыть (Esc)")
            onClicked: bar.close()
        }
    }
}
