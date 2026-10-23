import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp
import "fileicons.js" as FileIcons

// Папка — шеврон (повёрнут, если раскрыта). Файл — знак языка его цветом, как в теме
// значков Seti у VS Code: буквы ("C++", "PY", "{}") или линейный значок (картинка, архив)
Item {
    id: icon
    property string name: ""
    property bool isDir: false
    property bool expanded: false

    readonly property var spec: isDir ? null : FileIcons.specOf(name)
    readonly property bool glyph: spec !== null && spec[0].startsWith("@")

    implicitWidth: 18
    implicitHeight: 16

    Label {
        anchors.centerIn: parent
        visible: icon.isDir
        text: "›"
        rotation: icon.expanded ? 90 : 0
        color: Theme.muted
        font.pixelSize: 16
    }
    Label {
        anchors.centerIn: parent
        visible: icon.spec !== null && !icon.glyph
        text: icon.spec?.[0] ?? ""
        color: icon.spec?.[1] ?? "transparent"
        font.family: Theme.fontFamily
        font.pixelSize: text.length >= 3 ? 8.5 : text.length === 2 ? 10.5 : 13
        font.weight: Font.Bold
        font.letterSpacing: text.length >= 3 ? -0.5 : 0
    }
    LineIcon {
        anchors.centerIn: parent
        visible: icon.glyph
        width: 15
        height: 15
        stroke: 2
        name: icon.glyph ? icon.spec[0].slice(1) : ""
        color: icon.spec?.[1] ?? "transparent"
    }
    // Неизвестный тип — контур листа
    Rectangle {
        anchors.centerIn: parent
        visible: !icon.isDir && icon.spec === null
        width: 9
        height: 11
        radius: 2
        color: "transparent"
        border.color: Theme.faint
    }
}
