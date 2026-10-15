import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Кнопка колонки слева: значок панели, у активной — заливка; badge — небольшой счётчик
AbstractButton {
    id: button
    property string iconName
    property string tip
    property int badge: 0

    implicitWidth: 32
    implicitHeight: 32
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    contentItem: Item {
        LineIcon {
            anchors.centerIn: parent
            name: button.iconName
            color: button.checked || button.hovered ? Theme.text : Theme.muted
        }
        Rectangle {
            visible: button.badge > 0
            x: parent.width - width + 2
            y: parent.height - height + 2
            width: Math.max(14, count.implicitWidth + 6)
            height: 14
            radius: 7
            color: Theme.pressed
            Label {
                id: count
                anchors.centerIn: parent
                text: button.badge > 99 ? "99+" : button.badge
                color: Theme.text
                font.pixelSize: 9
            }
        }
    }
    background: Rectangle {
        radius: 6
        color: button.checked ? Theme.hover : button.hovered ? Qt.alpha(Theme.hover, 0.6) : "transparent"
    }

    ToolTip.visible: hovered && tip !== ""
    ToolTip.text: tip
    ToolTip.delay: 500
}
