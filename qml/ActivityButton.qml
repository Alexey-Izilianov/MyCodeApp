import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Кнопка колонки слева: значок панели, у активной — заливка; badge — небольшой счётчик
AbstractButton {
    id: button
    property string iconName
    property string tip
    property int badge: 0

    implicitWidth: 35
    implicitHeight: 35
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    contentItem: Item {
        LineIcon {
            anchors.centerIn: parent
            width: 20
            height: 20
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

    // Справа от кнопки, а не над ней: сверху подсказка закрывала соседний значок
    ToolTip {
        visible: button.hovered && button.tip !== ""
        text: button.tip
        delay: 500
        x: button.width + 8
        y: (button.height - height) / 2
    }
}
