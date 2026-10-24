import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import MyCodeApp

// Левая панель: проводник, поиск по проекту, Git и ассистент (переключаются в ActivityBar);
// ширина меняется перетаскиванием края
Rectangle {
    id: sidebar
    required property Workspace workspace
    required property EditorView editor
    required property GitService git
    required property Assistant assistant
    property string mode: "files" // "files" | "search" | "git" | "assistant"
    signal openFile(string path)
    signal openDiff(string path, bool staged)
    signal openHistory()
    signal openMatch(string path, int line, int column, int length)
    signal openFolderRequested()
    signal insertCode(string code)
    signal insertComment(int row)
    signal openAssistantSettings()
    signal leave()

    function focusPanel() {
        visible = true
        if (mode === "files")
            fileTree.focusTree()
        else if (mode === "search")
            searchPanel.focusInput("")
        else if (mode === "git")
            gitPanel.focusMessage()
        else
            showAssistant(true)
    }

    function showSearch(text) {
        mode = "search"
        visible = true
        searchPanel.focusInput(text)
    }
    // Разговору нужна ширина: узкую панель расширяем
    function showAssistant(focus) {
        mode = "assistant"
        visible = true
        if (width < 340)
            width = 340
        assistantPanel.activate()
        if (focus)
            assistantPanel.focusInput()
    }
    function showGit() {
        mode = "git"
        visible = true
        gitPanel.focusMessage()
    }

    width: 260
    color: Theme.editor

    Label {
        id: header
        x: 14
        height: 34
        verticalAlignment: Text.AlignVCenter
        text: sidebar.mode === "files" ? qsTr("Проводник") : sidebar.mode === "search" ? qsTr("Поиск")
              : sidebar.mode === "assistant" ? qsTr("Ассистент")
              : sidebar.git.available ? sidebar.git.kind : qsTr("Контроль версий")
        color: Theme.muted
        font.pixelSize: Theme.smallFontSize - 1
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.6
    }

    StackLayout {
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        currentIndex: ["files", "search", "git", "assistant"].indexOf(sidebar.mode)

        FileTree {
            id: fileTree
            workspace: sidebar.workspace
            editor: sidebar.editor
            git: sidebar.git
            onOpenFile: (path) => sidebar.openFile(path)
            onOpenFolderRequested: sidebar.openFolderRequested()
            onLeave: sidebar.leave()
        }
        SearchPanel {
            id: searchPanel
            rootPath: sidebar.workspace.rootPath
            onOpenMatch: (path, line, column, length) => sidebar.openMatch(path, line, column, length)
            onLeave: sidebar.leave()
        }
        GitPanel {
            id: gitPanel
            git: sidebar.git
            assistant: sidebar.assistant
            rootPath: sidebar.workspace.rootPath
            onOpenDiff: (path, staged) => sidebar.openDiff(path, staged)
            onOpenHistory: sidebar.openHistory()
            onOpenFile: (path) => sidebar.openFile(path)
            onLeave: sidebar.leave()
        }
        AssistantPanel {
            id: assistantPanel
            assistant: sidebar.assistant
            editor: sidebar.editor
            onInsertCode: (code) => sidebar.insertCode(code)
            onInsertComment: (row) => sidebar.insertComment(row)
            onOpenSettings: sidebar.openAssistantSettings()
            onLeave: sidebar.leave()
        }
    }

    MouseArea { // ширина — перетаскиванием зазора справа
        anchors.left: parent.right
        width: Theme.islandGap
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
