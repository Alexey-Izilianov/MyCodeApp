import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Подсказка языкового сервера и диагностика под мышью. Закрывается, когда
// мышь ушла из точки показа и не на самой подсказке (её можно прокрутить)
Popup {
    id: popup
    required property EditorView editor
    property point anchor

    function showAt(markdown, point) {
        text.text = markdown
        anchor = point
        const at = editor.mapToItem(parent, point.x, point.y)
        x = Math.max(8, Math.min(at.x - 12, parent.width - width - 8))
        y = at.y + 20 + height > parent.height ? at.y - height - 8 : at.y + 20
        open()
    }
    function leave() {
        if (visible)
            closeTimer.restart()
    }

    Timer {
        id: closeTimer
        interval: 250
        onTriggered: if (!inside.hovered) popup.close()
    }
    HoverHandler {
        id: inside
        parent: popup.contentItem
        onHoveredChanged: if (!hovered) closeTimer.restart()
    }

    parent: Overlay.overlay
    width: Math.min(text.implicitWidth, 560) + leftPadding + rightPadding
    height: Math.min(text.implicitHeight, 320) + topPadding + bottomPadding
    padding: 0
    focus: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        color: Theme.popup
        border.color: Theme.border
        radius: 6
    }

    contentItem: Flickable {
        clip: true
        contentHeight: text.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        Label {
            id: text
            width: Math.min(implicitWidth, 560)
            padding: 10
            textFormat: Text.MarkdownText
            wrapMode: Text.Wrap
            color: Theme.text
            font.pixelSize: Theme.smallFontSize
        }
    }
}
