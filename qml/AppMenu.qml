import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Выпадающее меню: пункты — Action с текстовым shortcut, он же подсказка справа
Menu {
    id: menu
    topPadding: 4
    bottomPadding: 4

    background: Rectangle {
        implicitWidth: 240
        color: Theme.popup
        border.color: Theme.border
        radius: 6
    }

    delegate: MenuItem {
        id: item
        implicitHeight: 28
        leftPadding: 12
        rightPadding: 12

        contentItem: Item {
            implicitWidth: label.implicitWidth + hint.implicitWidth + 48
            Label {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                text: item.text
                color: item.enabled ? Theme.text : Theme.faint
            }
            Label {
                id: hint
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                text: item.action && item.action.shortcut ? String(item.action.shortcut) : ""
                color: Theme.faint
                font.pixelSize: Theme.smallFontSize
            }
        }
        background: Rectangle {
            x: 4
            width: item.width - 8
            height: item.height
            radius: 4
            color: item.highlighted && item.enabled ? Theme.hover : "transparent"
        }
    }
}
