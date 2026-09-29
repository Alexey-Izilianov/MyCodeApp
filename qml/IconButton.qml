import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Квадратная плоская кнопка: текстовый глиф или крестик (glyph пустой)
AbstractButton {
    id: button
    property string glyph: ""
    property string tip: ""

    implicitWidth: 24
    implicitHeight: 24
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    contentItem: Item {
        Label {
            anchors.centerIn: parent
            visible: button.glyph !== ""
            text: button.glyph
            color: button.checked ? Theme.text : (button.hovered ? Theme.text : Theme.muted)
            font.pixelSize: Theme.smallFontSize
        }
        CrossIcon {
            anchors.centerIn: parent
            visible: button.glyph === ""
            color: button.hovered ? Theme.text : Theme.muted
        }
    }
    background: Rectangle {
        radius: 4
        color: button.pressed ? Theme.pressed
             : button.checked ? Theme.pressed
             : button.hovered ? Theme.hover : "transparent"
        border.width: button.checked ? 1 : 0
        border.color: Theme.accent
    }

    ToolTip.visible: hovered && tip !== ""
    ToolTip.text: tip
    ToolTip.delay: 500
}
