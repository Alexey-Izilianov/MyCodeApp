import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import MyCodeApp

// Левая панель: проводник и поиск по проекту; ширина меняется перетаскиванием края
Rectangle {
    id: sidebar
    required property Workspace workspace
    required property EditorView editor
    property string mode: "files" // "files" | "search"
    signal openFile(string path)
    signal openMatch(string path, int line, int column, int length)
    signal openFolderRequested()

    function showSearch(text) {
        mode = "search"
        visible = true
        searchPanel.focusInput(text)
    }

    width: 260
    color: Theme.chrome

    Row {
        id: tabs
        x: 4
        height: 32
        spacing: 2
        Repeater {
            model: [{ mode: "files", text: qsTr("Проводник") }, { mode: "search", text: qsTr("Поиск") }]
            AbstractButton {
                id: tab
                required property var modelData
                readonly property bool active: sidebar.mode === modelData.mode
                height: tabs.height
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                leftPadding: 8
                rightPadding: 8
                contentItem: Label {
                    text: tab.modelData.text
                    color: tab.active || tab.hovered ? Theme.text : Theme.muted
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 2
                        color: tab.active ? Theme.accent : "transparent"
                    }
                }
                onClicked: sidebar.mode = modelData.mode
            }
        }
    }
    Rectangle {
        anchors.top: tabs.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    StackLayout {
        anchors.top: tabs.bottom
        anchors.topMargin: 1
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        currentIndex: sidebar.mode === "files" ? 0 : 1

        FileTree {
            workspace: sidebar.workspace
            editor: sidebar.editor
            onOpenFile: (path) => sidebar.openFile(path)
            onOpenFolderRequested: sidebar.openFolderRequested()
        }
        SearchPanel {
            id: searchPanel
            rootPath: sidebar.workspace.rootPath
            onOpenMatch: (path, line, column, length) => sidebar.openMatch(path, line, column, length)
        }
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }
    MouseArea {
        anchors.right: parent.right
        anchors.rightMargin: -3
        width: 6
        height: parent.height
        cursorShape: Qt.SizeHorCursor
        property real startX
        onPressed: (mouse) => startX = mouse.x
        onPositionChanged: (mouse) => {
            if (pressed)
                sidebar.width = Math.max(160, Math.min(600, sidebar.width + mouse.x - startX))
        }
    }
}
