import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Подсказка языкового сервера, диагностика или образец цвета под мышью. Закрывается,
// когда мышь ушла из точки показа и не на самой подсказке (её можно прокрутить)
Popup {
    id: popup
    required property EditorView editor
    property point anchor
    property bool showSwatch: false
    property color swatch: "transparent"

    function showAt(markdown, point) {
        showSwatch = false
        place(markdown, point)
    }
    // info — результат EditorView.colorAt
    function showColor(info, point) {
        showSwatch = true
        swatch = info.color
        place("<b>" + info.hex + "</b><br>" + info.rgb + "<br>" + info.hsl, point)
    }
    function place(markdown, point) {
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

    readonly property int swatchSize: 48
    readonly property int swatchSpace: showSwatch ? swatchSize + 10 : 0

    parent: Overlay.overlay
    width: Math.min(text.implicitWidth, 560) + swatchSpace + leftPadding + rightPadding
    height: Math.min(Math.max(text.implicitHeight, showSwatch ? swatchSize + 20 : 0), 320) + topPadding + bottomPadding
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

        // Шахматка под образцом видна сквозь полупрозрачный цвет
        Rectangle {
            visible: popup.showSwatch
            x: 10
            y: 10
            width: popup.swatchSize
            height: popup.swatchSize
            radius: 4
            clip: true
            color: "#ffffff"
            Grid {
                columns: 6
                Repeater {
                    model: 36
                    Rectangle {
                        required property int index
                        width: 8
                        height: 8
                        color: (index % 6 + Math.floor(index / 6)) % 2 ? "#cfcfcf" : "#ffffff"
                    }
                }
            }
            Rectangle {
                anchors.fill: parent
                radius: 4
                color: popup.swatch
                border.color: Qt.alpha(Theme.text, 0.15)
            }
        }
        Label {
            id: text
            x: popup.swatchSpace
            width: Math.min(implicitWidth, 560)
            padding: 10
            textFormat: popup.showSwatch ? Text.StyledText : Text.MarkdownText
            lineHeight: popup.showSwatch ? 1.25 : 1
            wrapMode: Text.Wrap
            color: Theme.text
            font.pixelSize: Theme.smallFontSize
        }
    }
}
