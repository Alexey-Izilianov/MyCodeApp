import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

Button {
    id: button
    property bool primary: false

    implicitHeight: 28
    leftPadding: 14
    rightPadding: 14
    hoverEnabled: true

    contentItem: Label {
        text: button.text
        color: button.primary ? "white" : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 4
        color: button.primary ? (button.pressed ? Qt.darker(Theme.accent, 1.15)
                                 : button.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
                              : (button.pressed ? Theme.pressed
                                 : button.hovered ? Theme.hover : "transparent")
        border.width: button.primary ? 0 : 1
        border.color: Theme.border
    }
}
