import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Ctrl+G: переход к строке, «строка:столбец» — ещё и к столбцу
Popup {
    id: popup
    required property EditorView editor

    function show() {
        input.text = ""
        x = editor.mapToItem(parent, 0, 0).x + (editor.width - width) / 2
        y = editor.mapToItem(parent, 0, 0).y + 8
        open()
        input.forceActiveFocus()
    }

    parent: Overlay.overlay
    width: 320
    padding: 6
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onClosed: editor.forceActiveFocus()
    background: Rectangle {
        color: Theme.popup
        border.color: Theme.border
        radius: 6
    }

    contentItem: SearchField {
        id: input
        placeholderText: qsTr("Строка %1, или строка:столбец").arg(popup.editor.cursorLine + 1)
        validator: RegularExpressionValidator { regularExpression: /\d*(:\d*)?/ }
        onAccepted: {
            const parts = text.split(":").map(n => parseInt(n))
            popup.close()
            if (parts[0] > 0)
                popup.editor.goTo(parts[0] - 1, parts[1] > 0 ? parts[1] - 1 : 0)
        }
    }
}
