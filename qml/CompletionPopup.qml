import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Автодополнение у курсора: список от языкового сервера, фильтр по набранному
// префиксу, справа — документация выбранного варианта. Клавиши перехватывает
// handleKey (вызывается из Keys редактора раньше самого редактора).
Popup {
    id: popup
    required property EditorView editor
    required property LspManager lsp
    property var items: []
    property int startLine: -1
    property int startColumn: -1 // начало слова: курсор левее — список закрывается

    readonly property var kindNames: ({
        2: "method", 3: "fn", 4: "ctor", 5: "field", 6: "var", 7: "class", 8: "iface", 9: "module",
        10: "prop", 13: "enum", 14: "kw", 15: "snip", 17: "file", 20: "enum", 21: "const",
        22: "struct", 25: "type"
    })

    function trigger() {
        lsp.requestCompletion(editor)
    }
    function refresh() {
        items = lsp.completionItems(editor.wordBeforeCursor())
        if (items.length === 0) {
            close()
            return
        }
        list.currentIndex = 0
        const caret = editor.caretRectangle()
        const at = editor.mapToItem(parent, caret.x, caret.y + caret.height)
        x = Math.min(at.x - 24, parent.width - width - 8)
        y = at.y + height > parent.height ? at.y - caret.height - height : at.y
        open()
        documentation.text = ""
        lsp.requestDocumentation(items[0].index)
    }
    function accept() {
        if (list.currentIndex >= 0 && list.currentIndex < items.length)
            lsp.acceptCompletion(editor, items[list.currentIndex].index)
        close()
    }
    function move(step) {
        list.currentIndex = Math.max(0, Math.min(items.length - 1, list.currentIndex + step))
        lsp.requestDocumentation(items[list.currentIndex].index)
    }
    function handleKey(event) {
        if (!visible)
            return
        switch (event.key) {
        case Qt.Key_Up: move(-1); break
        case Qt.Key_Down: move(1); break
        case Qt.Key_PageUp: move(-8); break
        case Qt.Key_PageDown: move(8); break
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Tab: accept(); break
        case Qt.Key_Escape: close(); break
        default: return
        }
        event.accepted = true
    }

    Connections {
        target: popup.lsp
        function onCompletionReady() {
            popup.startLine = popup.editor.cursorLine
            popup.startColumn = popup.editor.cursorColumn - popup.editor.wordBeforeCursor().length
            popup.refresh()
        }
        function onDocumentationReady(index, markdown) {
            if (popup.visible && popup.items[list.currentIndex]?.index === index)
                documentation.text = markdown
        }
    }
    Connections {
        target: popup.editor
        function onCursorChanged() {
            if (!popup.visible)
                return
            if (popup.editor.cursorLine !== popup.startLine || popup.editor.cursorColumn < popup.startColumn)
                popup.close()
            else
                popup.refresh()
        }
        function onDocumentChanged() { popup.close() }
    }

    onClosed: lsp.cancelCompletion()

    parent: Overlay.overlay
    width: list.width + (documentation.text !== "" ? docPane.width + 1 : 0)
    height: Math.min(items.length, 10) * 24 + 8
    padding: 4
    focus: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        color: Theme.popup
        border.color: Theme.border
        radius: 6
    }

    contentItem: Row {
        ListView {
            id: list
            width: 360
            height: parent.height
            clip: true
            model: popup.items
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0

            delegate: Rectangle {
                id: entry
                required property var modelData
                required property int index
                width: list.width
                height: 24
                radius: 4
                color: ListView.isCurrentItem ? Theme.pressed : "transparent"

                Label {
                    id: kind
                    x: 6
                    width: 40
                    anchors.verticalCenter: parent.verticalCenter
                    text: popup.kindNames[entry.modelData.kind] ?? ""
                    color: Theme.faint
                    font.pixelSize: 11
                }
                Label {
                    id: label
                    anchors.left: kind.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, list.width - kind.width - 16)
                    text: entry.modelData.label
                    color: Theme.text
                    font.family: "Cascadia Mono"
                    elide: Text.ElideRight
                }
                Label {
                    anchors.left: label.right
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: entry.modelData.detail
                    color: Theme.faint
                    font.pixelSize: Theme.smallFontSize
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideLeft
                }
                TapHandler {
                    onTapped: { list.currentIndex = entry.index; popup.accept() }
                }
            }
        }
        Rectangle {
            visible: documentation.text !== ""
            width: 1
            height: parent.height
            color: Theme.border
        }
        Flickable {
            id: docPane
            visible: documentation.text !== ""
            width: 340
            height: parent.height
            clip: true
            contentHeight: documentation.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            Label {
                id: documentation
                width: docPane.width
                padding: 8
                textFormat: Text.MarkdownText
                wrapMode: Text.Wrap
                color: Theme.muted
                font.pixelSize: Theme.smallFontSize
            }
        }
    }
}
