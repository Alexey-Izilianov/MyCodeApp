import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Квадратная плоская кнопка: значок LineIcon, текстовый глиф или крестик (оба пустые)
AbstractButton {
    id: button
    property string glyph: ""
    property string iconName: ""
    property string tip: ""

    implicitWidth: 24
    implicitHeight: 24
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    contentItem: Item {
        LineIcon {
            anchors.centerIn: parent
            visible: button.iconName !== ""
            width: 16
            height: 16
            name: button.iconName
            color: button.hovered ? Theme.text : Theme.muted
        }
        Label {
            anchors.centerIn: parent
            visible: button.glyph !== "" && button.iconName === ""
            text: button.glyph
            color: button.checked ? Theme.text : (button.hovered ? Theme.text : Theme.muted)
            font.pixelSize: Theme.smallFontSize
        }
        CrossIcon {
            anchors.centerIn: parent
            visible: button.glyph === "" && button.iconName === ""
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
