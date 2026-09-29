import QtQuick

// Крестик из двух тонких линий — без зависимости от глифов шрифта
Item {
    property color color: "white"
    implicitWidth: 8
    implicitHeight: 8

    Repeater {
        model: [45, -45]
        Rectangle {
            required property int modelData
            anchors.centerIn: parent
            width: parent.width * 1.3
            height: 1.2
            antialiasing: true
            rotation: modelData
            color: parent.color
        }
    }
}
