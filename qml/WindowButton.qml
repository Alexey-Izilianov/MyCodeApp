import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Кнопка окна в строке заголовка: свернуть, развернуть/восстановить, закрыть
AbstractButton {
    id: button
    property string kind // "minimize" | "maximize" | "restore" | "close"
    readonly property bool danger: kind === "close" && (hovered || pressed)

    implicitWidth: 46
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    contentItem: Item {
        readonly property color ink: button.danger ? "white" : Theme.text
        Rectangle {
            visible: button.kind === "minimize"
            anchors.centerIn: parent
            width: 10
            height: 1
            color: parent.ink
        }
        Rectangle {
            visible: button.kind === "maximize" || button.kind === "restore"
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: button.kind === "restore" ? -1 : 0
            anchors.verticalCenterOffset: button.kind === "restore" ? 1 : 0
            width: button.kind === "restore" ? 8 : 10
            height: width
            radius: 1
            color: "transparent"
            border.color: parent.ink
        }
        Item { // задний квадрат «восстановить»: видны только верх и правый край
            visible: button.kind === "restore"
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 1
            anchors.verticalCenterOffset: -1
            width: 8
            height: 8
            Rectangle { width: 8; height: 1; color: parent.parent.ink }
            Rectangle { x: 7; width: 1; height: 8; color: parent.parent.ink }
        }
        CrossIcon {
            visible: button.kind === "close"
            anchors.centerIn: parent
            implicitWidth: 10
            implicitHeight: 10
            color: parent.ink
        }
    }
    background: Rectangle {
        color: button.kind === "close" ? (button.pressed ? "#a3241b" : button.hovered ? "#c42b1c" : "transparent")
                                       : (button.pressed ? Theme.pressed : button.hovered ? Theme.hover : "transparent")
    }
}
