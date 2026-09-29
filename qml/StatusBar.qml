import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

Rectangle {
    id: statusBar
    required property EditorView editor
    readonly property bool hasFile: editor.filePath !== ""

    implicitHeight: 24
    color: Theme.chrome

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
    }

    Label {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: details.left
        anchors.rightMargin: 24
        text: statusBar.editor.filePath
        elide: Text.ElideMiddle
        color: Theme.muted
        font.pixelSize: Theme.smallFontSize
    }

    Row {
        id: details
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 12
        spacing: 18
        visible: statusBar.hasFile

        Repeater {
            model: [
                qsTr("Стр %1, Стлб %2").arg(statusBar.editor.cursorLine + 1)
                                       .arg(statusBar.editor.cursorColumn + 1),
                statusBar.editor.encoding,
                statusBar.editor.lineEnding,
                statusBar.editor.language
            ]
            Label {
                required property string modelData
                text: modelData
                color: Theme.muted
                font.pixelSize: Theme.smallFontSize
            }
        }
    }
}
