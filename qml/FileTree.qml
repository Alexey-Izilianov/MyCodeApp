import QtQuick
import QtQuick.Controls.Basic
import QtQml.Models
import MyCodeApp

// Проводник: дерево папки проекта; с текстом в фильтре — плоский список
// подходящих файлов (нечёткий поиск по путям)
Item {
    id: explorer
    required property Workspace workspace
    required property EditorView editor
    required property GitService git
    signal openFile(string path)
    signal openFolderRequested()
    signal leave() // Esc — обратно в редактор

    function focusTree() {
        if (filter.text !== "")
            flat.forceActiveFocus()
        else
            tree.forceActiveFocus()
        if (!tree.selectionModel.currentIndex.valid)
            tree.selectionModel.setCurrentIndex(tree.index(0, 0), ItemSelectionModel.NoUpdate)
    }

    readonly property bool hasFolder: workspace.rootPath !== ""

    ProjectModel {
        id: projectModel
        rootPath: explorer.workspace.rootPath
        git: explorer.git
    }

    Column {
        id: header
        visible: explorer.hasFolder
        width: parent.width
        topPadding: 8
        spacing: 6

        Label {
            x: 12
            width: parent.width - 24
            text: explorer.workspace.rootName.toUpperCase()
            color: Theme.muted
            font.pixelSize: Theme.smallFontSize
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        SearchField {
            id: filter
            x: 8
            width: parent.width - 16
            placeholderText: qsTr("Фильтр файлов")
            Keys.onEscapePressed: { text = ""; explorer.leave() }
            Keys.onDownPressed: explorer.focusTree()
            onAccepted: if (flat.count > 0) explorer.openFile(flat.model[0].path)
        }
    }

    component Row24: Rectangle {
        property bool highlighted: false
        property alias hovered: hover.hovered
        implicitHeight: 24
        height: 24
        color: highlighted ? Theme.pressed : hover.hovered ? Theme.hover : "transparent"
        HoverHandler { id: hover }
    }

    TreeView {
        id: tree
        visible: explorer.hasFolder && filter.text === ""
        anchors.top: header.bottom
        anchors.topMargin: 6
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        model: projectModel
        rootIndex: projectModel.rootIndex
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        selectionModel: ItemSelectionModel {}
        Keys.onEscapePressed: explorer.leave()
        Keys.onReturnPressed: {
            const index = tree.selectionModel.currentIndex
            const node = tree.itemAtIndex(index)
            if (!node)
                return
            if (node.isDir)
                tree.toggleExpanded(tree.rowAtIndex(index))
            else
                explorer.openFile(node.path)
        }

        delegate: Row24 {
            id: node
            required property TreeView treeView
            required property bool expanded
            required property int depth
            required property int row
            required property string display
            required property string path
            required property bool isDir
            required property string gitStatus
            required property bool current

            implicitWidth: tree.width
            highlighted: !isDir && path === explorer.editor.filePath
            color: current && tree.activeFocus ? Theme.hover : highlighted ? Theme.pressed
                 : hovered ? Theme.hover : "transparent"
            border.width: current && tree.activeFocus ? 1 : 0
            border.color: Theme.accent

            FileIcon {
                id: nodeIcon
                x: 8 + node.depth * 14
                anchors.verticalCenter: parent.verticalCenter
                name: node.display
                isDir: node.isDir
                expanded: node.expanded
            }
            Label {
                anchors.left: nodeIcon.right
                anchors.leftMargin: 6
                anchors.right: statusLetter.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: node.display
                color: node.gitStatus !== "" ? Theme.gitColor(node.gitStatus)
                                             : node.highlighted ? Theme.text : Theme.muted
                elide: Text.ElideRight
            }
            // У файла — буква состояния, у папки с изменениями внутри — точка
            Label {
                id: statusLetter
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: node.gitStatus === "" ? "" : node.isDir ? "•" : node.gitStatus
                color: Theme.gitColor(node.gitStatus)
                font.pixelSize: Theme.smallFontSize
            }
            TapHandler {
                onTapped: node.isDir ? node.treeView.toggleExpanded(node.row) : explorer.openFile(node.path)
            }
        }
    }

    ListView {
        id: flat
        visible: explorer.hasFolder && filter.text !== ""
        anchors.fill: tree
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // Условие на fileCount — чтобы список пересчитался после обхода папки
        model: filter.text === "" || explorer.workspace.fileCount < 0 ? [] : explorer.workspace.findFiles(filter.text, 200)
        ScrollBar.vertical: ScrollBar {}
        highlightMoveDuration: 0
        Keys.onReturnPressed: if (currentIndex >= 0) explorer.openFile(model[currentIndex].path)
        Keys.onEscapePressed: explorer.leave()
        Keys.onUpPressed: currentIndex > 0 ? decrementCurrentIndex() : filter.forceActiveFocus()

        delegate: Row24 {
            id: item
            required property var modelData
            required property int index
            width: flat.width
            highlighted: modelData.path === explorer.editor.filePath || (index === flat.currentIndex && flat.activeFocus)

            FileIcon {
                id: itemIcon
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                name: item.modelData.name
            }
            Label {
                id: itemName
                anchors.left: itemIcon.right
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: item.modelData.name
                color: Theme.text
            }
            Label {
                anchors.left: itemName.right
                anchors.leftMargin: 8
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: item.modelData.relative.slice(0, -item.modelData.name.length - 1)
                color: Theme.faint
                font.pixelSize: Theme.smallFontSize
                elide: Text.ElideLeft
            }
            TapHandler { onTapped: explorer.openFile(item.modelData.path) }
        }
    }

    Column {
        visible: !explorer.hasFolder
        anchors.centerIn: parent
        spacing: 12
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Папка не открыта")
            color: Theme.muted
        }
        FlatButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Открыть папку")
            onClicked: explorer.openFolderRequested()
        }
    }
}
