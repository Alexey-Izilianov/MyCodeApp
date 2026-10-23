import QtQuick
import MyCodeApp

// Скругляет углы «острова» без обрезки содержимого: кольцо цвета подложки поверх краёв.
// Толщина кольца должна быть не меньше 0,42 радиуса, иначе угол острова выглядывает
Rectangle {
    required property Item target
    readonly property int ring: 4

    anchors.fill: target
    anchors.margins: -ring
    visible: target.visible
    radius: Theme.islandRadius + ring
    color: "transparent"
    border.width: ring
    border.color: Theme.ground

    Rectangle {
        anchors.fill: parent
        anchors.margins: parent.ring
        radius: Theme.islandRadius
        color: "transparent"
        border.color: Theme.islandBorder
    }
}
