import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// F2: новое имя символа под курсором
Popup {
    id: popup
    required property EditorView editor
    signal renameRequested(string name)

    function show() {
        const word = editor.wordAtCursor()
        if (word === "")
            return
        input.text = word
        const caret = editor.caretRectangle()
        const at = editor.mapToItem(parent, caret.x, caret.y + caret.height)
        x = Math.min(at.x, parent.width - width - 8)
        y = at.y + 4
        open()
        input.forceActiveFocus()
        input.selectAll()
    }

    parent: Overlay.overlay
    width: 280
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
        placeholderText: qsTr("Новое имя")
        onAccepted: {
            const name = text.trim()
            popup.close()
            if (name !== "")
                popup.renameRequested(name)
        }
    }
}
