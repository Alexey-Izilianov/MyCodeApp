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
    property bool showMinimap: true

    Connections {
        target: Qt.application
        function onAboutToQuit() { window.saveSession() } // любой выход: окно, Ctrl+Q, завершение сеанса
    }

    function openFolder(url) {
        saveSession()
        const opened = workspace.open(url)
        if (opened)
            restoreSession()
        return opened
    }
    function closeFolder() {
        saveSession()
        workspace.close()
    }
    function saveSession() {
        if (workspace.rootPath === "")
            return
        const files = manager.tabs.map((tab, i) =>
            Object.assign({ path: tab.path }, editor.viewStateOf(manager.documentAt(i))))
        workspace.saveSession({ files: files, current: hasDocument ? manager.tabs[manager.currentIndex].path : "" })
    }
    function restoreSession() {
        const session = workspace.loadSession()
        for (const file of session.files ?? []) {
            const index = manager.openPath(file.path)
            if (index >= 0)
                editor.setViewStateOf(manager.documentAt(index), file)
        }
        const current = manager.tabs.findIndex(tab => tab.path === session.current)
        if (current >= 0)
            manager.activate(current)
    }
    // Переходы к определению запоминаются: Alt+← возвращает назад
    property var history: []
    function navigateTo(path, line, column) {
        if (hasDocument)
            history.push({ path: editor.filePath, line: editor.cursorLine, column: editor.cursorColumn })
        openAt(path, line, column, 0)
    }
    function goBack() {
        const place = history.pop()
        if (place)
            openAt(place.path, place.line, place.column, 0)
    }
    function updateDiagnostics() {
        editor.setDiagnostics(lsp.diagnosticsFor(editor.filePath))
    }

    function openAt(path, line, column, length) {
        if (manager.openPath(path) >= 0)
            editor.goTo(line, column, length)
        editor.forceActiveFocus()
    }

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
        Component.onCompleted: restoreUnsaved() // несохранённое после сбоя или выхода
        onExternalChange: (index, name) => reloadDialog.ask(index, name)
    }

    LspManager {
        id: lsp
        documents: manager
        rootPath: workspace.rootPath
        onDiagnosticsChanged: window.updateDiagnostics()
        onNavigate: (path, line, column) => window.navigateTo(path, line, column)
        onHoverReady: (markdown, line, column) => hoverPopup.showAt(markdown, hoverPopup.pending)
        onReferencesChanged: if (references.length > 0) bottomPanel.show("references")
        onMessage: (text) => statusBar.flash(text)
    }

    Workspace {
        id: workspace
        Component.onCompleted: {
            const last = lastRoot()
            if (last.toString() !== "")
                window.openFolder(last)
        }
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
            Action { text: qsTr("Открыть папку…"); shortcut: "Ctrl+K, Ctrl+O"; onTriggered: folderDialog.open() }
            Action { text: qsTr("Закрыть папку"); enabled: workspace.rootPath !== ""; onTriggered: window.closeFolder() }
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
            AppMenuSeparator {}
            Action { text: qsTr("Выделить следующее вхождение"); shortcut: "Ctrl+D"; enabled: window.hasDocument; onTriggered: editor.selectNextOccurrence() }
            Action { text: qsTr("Добавить курсор выше"); shortcut: "Ctrl+Alt+Up"; enabled: window.hasDocument; onTriggered: editor.addCaretVertical(-1) }
            Action { text: qsTr("Добавить курсор ниже"); shortcut: "Ctrl+Alt+Down"; enabled: window.hasDocument; onTriggered: editor.addCaretVertical(1) }
        }
        AppMenu {
            title: qsTr("Поиск")
            Action { text: qsTr("Найти…"); shortcut: "Ctrl+F"; enabled: window.hasDocument; onTriggered: findBar.open() }
            Action { text: qsTr("Следующее совпадение"); shortcut: "F3"; enabled: window.hasDocument; onTriggered: editor.findNext() }
            Action { text: qsTr("Предыдущее совпадение"); shortcut: "Shift+F3"; enabled: window.hasDocument; onTriggered: editor.findPrev() }
            AppMenuSeparator {}
            Action { text: qsTr("Перейти к файлу…"); shortcut: "Ctrl+P"; onTriggered: quickOpen.show() }
            Action { text: qsTr("Найти в проекте…"); shortcut: "Ctrl+Shift+F"; onTriggered: sidebar.showSearch("") }
        }
        AppMenu {
            title: qsTr("Код")
            Action { text: qsTr("Предложить варианты"); shortcut: "Ctrl+Space"; enabled: window.hasDocument; onTriggered: completion.trigger() }
            Action { text: qsTr("Перейти к определению"); shortcut: "F12"; enabled: window.hasDocument; onTriggered: lsp.gotoDefinition(editor, editor.cursorLine, editor.cursorColumn) }
            Action { text: qsTr("Найти ссылки"); shortcut: "Shift+F12"; enabled: window.hasDocument; onTriggered: lsp.findReferences(editor) }
            Action { text: qsTr("Назад"); shortcut: "Alt+Left"; onTriggered: window.goBack() }
            AppMenuSeparator {}
            Action { text: qsTr("Переименовать символ"); shortcut: "F2"; enabled: window.hasDocument && !editor.readOnly; onTriggered: renamePopup.show() }
            Action { text: qsTr("Форматировать документ"); shortcut: "Shift+Alt+F"; enabled: window.hasDocument && !editor.readOnly; onTriggered: lsp.format(editor) }
            AppMenuSeparator {}
            Action { text: qsTr("Проблемы"); shortcut: "Ctrl+Shift+M"; onTriggered: bottomPanel.visible && bottomPanel.mode === "problems" ? bottomPanel.visible = false : bottomPanel.show("problems") }
        }
        AppMenu {
            title: qsTr("Вид")
            Action { text: qsTr("Свернуть блок"); shortcut: "Ctrl+Shift+["; enabled: window.hasDocument; onTriggered: editor.foldAtCursor() }
            Action { text: qsTr("Развернуть блок"); shortcut: "Ctrl+Shift+]"; enabled: window.hasDocument; onTriggered: editor.unfoldAtCursor() }
            AppMenuSeparator {}
            Action { text: qsTr("Свернуть все"); shortcut: "Ctrl+K, Ctrl+0"; enabled: window.hasDocument; onTriggered: editor.foldAll() }
            Action { text: qsTr("Развернуть все"); shortcut: "Ctrl+K, Ctrl+J"; enabled: window.hasDocument; onTriggered: editor.unfoldAll() }
            AppMenuSeparator {}
            Action { text: qsTr("Проводник"); shortcut: "Ctrl+Shift+E"; onTriggered: { sidebar.mode = "files"; sidebar.visible = true } }
            Action { text: sidebar.visible ? qsTr("Скрыть боковую панель") : qsTr("Показать боковую панель"); shortcut: "Ctrl+B"; onTriggered: sidebar.visible = !sidebar.visible }
            Action { text: window.showMinimap ? qsTr("Скрыть миникарту") : qsTr("Показать миникарту"); onTriggered: window.showMinimap = !window.showMinimap }
        }
    }

    Shortcut { sequence: "Ctrl+Tab"; onActivated: window.cycleTab(1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: window.cycleTab(-1) }

    Sidebar {
        id: sidebar
        workspace: workspace
        editor: editor
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.bottom: statusBar.top
        onOpenFile: (path) => window.openAt(path, 0, 0, 0)
        onOpenMatch: (path, line, column, length) => window.openAt(path, line, column, length)
        onOpenFolderRequested: folderDialog.open()
    }

    TabStrip {
        id: tabStrip
        manager: manager
        anchors.top: parent.top
        anchors.left: sidebar.visible ? sidebar.right : parent.left
        anchors.right: parent.right
        visible: manager.tabs.length > 0
        height: visible ? implicitHeight : 0
        onCloseRequested: (index) => window.requestClose(index)
    }

    EditorView {
        id: editor
        objectName: "editor"
        anchors.top: tabStrip.bottom
        anchors.left: sidebar.visible ? sidebar.right : parent.left
        anchors.right: minimap.left
        anchors.bottom: bottomPanel.visible ? bottomPanel.top : statusBar.top
        document: manager.currentDocument
        focus: true
        onErrorOccurred: (message) => console.warn(message)
        onDocumentChanged: {
            forceActiveFocus()
            window.updateDiagnostics()
        }
        Keys.onPressed: (event) => completion.handleKey(event) // пока открыт список, стрелки и Enter — его
        onTextTyped: (text) => {
            // Запрос на каждый символ идентификатора и после . -> :: — сервер сам
            // сужает выдачу по префиксу, а список уже на экране фильтруется локально
            const before = wordBeforeCursor()
            if (/^[A-Za-z_]$/.test(text) || text === "." || (text === ">" || text === ":") && before === "")
                completion.trigger()
            else if (!/^[A-Za-z0-9_]$/.test(text))
                completion.close()
        }
        onHoverRest: (line, column, x, y) => {
            hoverPopup.pending = Qt.point(x, y)
            lsp.hover(editor, line, column)
        }
        onHoverLeft: hoverPopup.leave()
        onDefinitionClicked: (line, column) => lsp.gotoDefinition(editor, line, column)
    }

    Minimap {
        id: minimap
        editor: editor
        background: Theme.editor
        anchors.top: editor.top
        anchors.right: vbar.left
        anchors.bottom: editor.bottom
        visible: window.showMinimap && window.hasDocument
        width: visible ? 96 : 0
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
        onActionRequested: (action) => {
            if (action === "file") fileDialog.open()
            else if (action === "folder") folderDialog.open()
            else if (action === "quickOpen") quickOpen.show()
            else if (action === "search") sidebar.showSearch("")
        }
    }

    BottomPanel {
        id: bottomPanel
        lsp: lsp
        workspace: workspace
        visible: false
        anchors.left: editor.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        onOpenAt: (path, line, column) => window.openAt(path, line, column, 0)
    }

    CompletionPopup {
        id: completion
        editor: editor
        lsp: lsp
    }

    HoverPopup {
        id: hoverPopup
        editor: editor
        property point pending
    }

    RenamePopup {
        id: renamePopup
        editor: editor
        onRenameRequested: (name) => lsp.rename(editor, name)
    }

    StatusBar {
        id: statusBar
        editor: editor
        lsp: lsp
        onProblemsRequested: bottomPanel.show("problems")
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }

    LoadOverlay {
        manager: manager
        anchors.fill: parent
    }

    ReloadDialog {
        id: reloadDialog
        onReloadRequested: (index) => manager.reloadFromDisk(index)
        onKeepRequested: (index) => manager.keepLocal(index)
    }

    QuickOpen {
        id: quickOpen
        workspace: workspace
        onOpenFile: (path) => window.openAt(path, 0, 0, 0)
    }

    FolderDialog {
        id: folderDialog
        onAccepted: window.openFolder(selectedFolder)
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
                if (!window.openFolder(url)) // не папка — значит файл
                    manager.open(url)
        }
    }
}
