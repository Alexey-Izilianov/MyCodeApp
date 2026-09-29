import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import MyCodeApp

ApplicationWindow {
    id: window
    width: 1200
    height: 760
    minimumWidth: 640
    minimumHeight: 400
    visible: true
    color: Theme.editor
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    title: manager.currentIndex >= 0
           ? manager.tabs[manager.currentIndex].name + " — MyCodeApp"
           : "MyCodeApp"

    readonly property bool hasDocument: manager.currentIndex >= 0

    function requestClose(index) {
        if (index < 0)
            return
        if (manager.isDirty(index))
            closeDialog.ask(index, manager.tabs[index].name)
        else
            manager.close(index)
    }

    function cycleTab(step) {
        const n = manager.tabs.length
        if (n > 1)
            manager.activate((manager.currentIndex + step + n) % n)
    }

    DocumentManager {
        id: manager
        objectName: "manager"
        onErrorOccurred: (message) => console.warn(message)
    }

    menuBar: MenuBar {
        background: Rectangle {
            color: Theme.chrome
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }
        delegate: MenuBarItem {
            id: menuBarItem
            leftPadding: 10
            rightPadding: 10
            topPadding: 6
            bottomPadding: 6
            contentItem: Label {
                text: menuBarItem.text
                color: Theme.text
            }
            background: Rectangle {
                radius: 4
                color: menuBarItem.highlighted ? Theme.hover : "transparent"
            }
        }

        AppMenu {
            title: qsTr("Файл")
            Action { text: qsTr("Открыть…"); shortcut: "Ctrl+O"; onTriggered: fileDialog.open() }
            Action {
                text: qsTr("Сохранить"); shortcut: "Ctrl+S"
                enabled: window.hasDocument
                onTriggered: editor.save()
            }
            AppMenuSeparator {}
            Action {
                text: qsTr("Закрыть вкладку"); shortcut: "Ctrl+W"
                enabled: window.hasDocument
                onTriggered: window.requestClose(manager.currentIndex)
            }
            AppMenuSeparator {}
            Action { text: qsTr("Выход"); shortcut: "Ctrl+Q"; onTriggered: window.close() }
        }
        AppMenu {
            title: qsTr("Правка")
            Action { text: qsTr("Отменить"); shortcut: "Ctrl+Z"; enabled: editor.canUndo; onTriggered: editor.undo() }
            Action { text: qsTr("Повторить"); shortcut: "Ctrl+Y"; enabled: editor.canRedo; onTriggered: editor.redo() }
            AppMenuSeparator {}
            Action { text: qsTr("Вырезать"); shortcut: "Ctrl+X"; enabled: window.hasDocument; onTriggered: editor.cut() }
            Action { text: qsTr("Копировать"); shortcut: "Ctrl+C"; enabled: window.hasDocument; onTriggered: editor.copy() }
            Action { text: qsTr("Вставить"); shortcut: "Ctrl+V"; enabled: window.hasDocument; onTriggered: editor.paste() }
            AppMenuSeparator {}
            Action { text: qsTr("Выделить всё"); shortcut: "Ctrl+A"; enabled: window.hasDocument; onTriggered: editor.selectAll() }
        }
        AppMenu {
            title: qsTr("Поиск")
            Action { text: qsTr("Найти…"); shortcut: "Ctrl+F"; enabled: window.hasDocument; onTriggered: findBar.open() }
            Action { text: qsTr("Следующее совпадение"); shortcut: "F3"; enabled: window.hasDocument; onTriggered: editor.findNext() }
            Action { text: qsTr("Предыдущее совпадение"); shortcut: "Shift+F3"; enabled: window.hasDocument; onTriggered: editor.findPrev() }
        }
    }

    Shortcut { sequence: "Ctrl+Tab"; onActivated: window.cycleTab(1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: window.cycleTab(-1) }

    TabStrip {
        id: tabStrip
        manager: manager
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: manager.tabs.length > 0
        height: visible ? implicitHeight : 0
        onCloseRequested: (index) => window.requestClose(index)
    }

    EditorView {
        id: editor
        objectName: "editor"
        anchors.top: tabStrip.bottom
        anchors.left: parent.left
        anchors.right: vbar.left
        anchors.bottom: statusBar.top
        document: manager.currentDocument
        focus: true
        onErrorOccurred: (message) => console.warn(message)
        onDocumentChanged: forceActiveFocus()
    }

    ScrollBar {
        id: vbar
        anchors.top: editor.top
        anchors.right: parent.right
        anchors.bottom: editor.bottom
        width: 12
        orientation: Qt.Vertical
        policy: size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        size: editor.contentHeight > 0 ? Math.min(1, editor.height / editor.contentHeight) : 1
        onPositionChanged: if (pressed) editor.scrollY = position * editor.contentHeight
        contentItem: Rectangle {
            implicitWidth: 6
            radius: 3
            color: Theme.scrollThumb
            opacity: vbar.pressed ? 1 : (vbar.hovered ? 0.9 : 0.6)
        }
        background: Rectangle { color: Theme.editor }
    }
    Binding {
        target: vbar
        property: "position"
        value: editor.contentHeight > 0 ? editor.scrollY / editor.contentHeight : 0
        when: !vbar.pressed
    }

    FindBar {
        id: findBar
        editor: editor
        anchors.top: editor.top
        anchors.right: editor.right
        anchors.topMargin: 8
        anchors.rightMargin: 12
    }

    EmptyState {
        anchors.fill: editor
        anchors.rightMargin: -vbar.width
        visible: manager.tabs.length === 0
        onOpenRequested: fileDialog.open()
    }

    StatusBar {
        id: statusBar
        editor: editor
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }

    LoadOverlay {
        manager: manager
        anchors.fill: parent
    }

    CloseDialog {
        id: closeDialog
        onSaveRequested: (index) => { manager.save(index); manager.close(index) }
        onDiscardRequested: (index) => manager.close(index)
    }

    FileDialog {
        id: fileDialog
        onAccepted: manager.open(selectedFile)
    }

    DropArea {
        anchors.fill: parent
        onEntered: (drag) => drag.accept(drag.hasUrls)
        onDropped: (drop) => {
            for (const url of drop.urls)
                manager.open(url)
        }
    }
}
