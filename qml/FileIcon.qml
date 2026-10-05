import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Папка — шеврон (повёрнут, если раскрыта), файл — контур цвета языка
Item {
    id: icon
    property string name: ""
    property bool isDir: false
    property bool expanded: false

    readonly property var languageColors: ({
        c: "#6c9bd2", h: "#6c9bd2", cc: "#6c9bd2", cpp: "#6c9bd2", hpp: "#6c9bd2", cxx: "#6c9bd2",
        py: "#d4b85f", js: "#d4b85f", ts: "#5f9ed4", qml: "#6fb57d",
        json: "#c98f5a", md: "#9da0a8", txt: "#9da0a8", cmake: "#b07ac9"
    })

    implicitWidth: 16
    implicitHeight: 16

    Label {
        anchors.centerIn: parent
        visible: icon.isDir
        text: "›"
        rotation: icon.expanded ? 90 : 0
        color: Theme.muted
        font.pixelSize: 16
    }
    Rectangle {
        anchors.centerIn: parent
        visible: !icon.isDir
        width: 9
        height: 11
        radius: 2
        color: "transparent"
        border.color: icon.languageColors[icon.name.split(".").pop().toLowerCase()] ?? Theme.faint
    }
}
