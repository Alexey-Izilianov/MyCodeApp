import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Плавающая строка поиска в правом верхнем углу редактора.
// Режимы: подстрока / целое слово / регэксп (SearchEngine::Mode по числу включённых).
// Ошибка регэкспа приходит через onSearchError и гасится сообщением.
Rectangle {
    id: bar
    required property EditorView editor
    property int current: 0
    property int total: 0
    // 0 — plain, 1 — целое слово, 2 — регэксп
    readonly property int mode: regexButton.checked ? 2 : wordButton.checked ? 1 : 0

    function open() {
        visible = true
        input.forceActiveFocus()
        input.selectAll()
        if (input.text !== "")
            editor.find(input.text, caseButton.checked, mode)
    }
    function close() {
        visible = false
        editor.clearSearch()
        editor.forceActiveFocus()
    }
    function run() {
        if (input.text !== "")
            editor.find(input.text, caseButton.checked, mode)
    }

    visible: false
    width: 434
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
        function onSearchError(message) {
            if (bar.visible)
                status.text = qsTr("Ошибка: %1").arg(message)
        }
    }

    Timer {
        id: debounce
        interval: 200
        onTriggered: bar.run()
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 6
        spacing: 2

        TextField {
            id: input
            width: parent.width - status.width - 6 * 26 - 14
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

            onTextChanged: status.text = "", debounce.restart()
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
            elide: Label.ElideRight
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
            id: wordButton
            anchors.verticalCenter: parent.verticalCenter
            glyph: "ab"
            checkable: true
            tip: qsTr("Целое слово")
            checked: false
            onToggled: {
                if (checked)
                    regexButton.checked = false
                debounce.restart()
            }
        }
        IconButton {
            id: regexButton
            anchors.verticalCenter: parent.verticalCenter
            glyph: ".*"
            checkable: true
            tip: qsTr("Регэксп")
            checked: false
            onToggled: {
                if (checked)
                    wordButton.checked = false
                debounce.restart()
            }
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
