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
    property bool showMinimap: workspace.setting("minimapVisible", true)
    onShowMinimapChanged: workspace.setSetting("minimapVisible", showMinimap)

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
    // Переходы к определению запоминаются: команда «Назад» возвращает
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

    ShellIntegration { id: shell }
    PluginManager {
        id: plugins
        editor: editor
        onMessage: (text) => statusBar.flash(text)
    }
    Connections {
        target: ThemeManager
        function onMessage(text) { statusBar.flash(text) }
    }
    Connections {
        target: SnippetStore
        function onMessage(text) { statusBar.flash(text) }
    }

    GitService {
        id: git
        rootPath: workspace.rootPath
        documents: manager
        onMessage: (text) => statusBar.flash(text)
        onStateChanged: marksTimer.restart()
    }
    // Полоски изменений пересчитываются после паузы в правке
    Timer {
        id: marksTimer
        interval: 400
        onTriggered: git.updateMarks(editor)
    }
    Connections {
        target: editor
        function onContentChanged() { marksTimer.restart() }
    }
    Connections {
        target: manager
        function onTabsChanged() { git.refresh() } // в т.ч. сохранение файла
    }
    onActiveChanged: if (active) git.refresh()

    function showFileDiff() {
        if (!hasDocument || editor.filePath === "")
            return
        diffView.show(editor.filePath, false)
    }
    function stepChange(direction) {
        if (diffView.visible)
            diffView.step(direction)
        else if (hasDocument)
            editor.gotoChange(direction)
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
            Command { command: "file.open"; text: qsTr("Открыть…"); defaultShortcut: "Ctrl+O"; onTriggered: fileDialog.open() }
            Command { command: "file.openFolder"; text: qsTr("Открыть папку…"); defaultShortcut: "Ctrl+K, Ctrl+O"; onTriggered: folderDialog.open() }
            Command { command: "file.closeFolder"; text: qsTr("Закрыть папку"); enabled: workspace.rootPath !== ""; onTriggered: window.closeFolder() }
            Command {
                command: "file.save"
                text: qsTr("Сохранить"); defaultShortcut: "Ctrl+S"
                enabled: window.hasDocument
                onTriggered: editor.save()
            }
            AppMenuSeparator {}
            Command {
                command: "file.closeTab"
                text: qsTr("Закрыть вкладку"); defaultShortcut: "Ctrl+W"
                enabled: window.hasDocument
                onTriggered: window.requestClose(manager.currentIndex)
            }
            AppMenuSeparator {}
            Command { command: "app.settings"; text: qsTr("Настройки…"); defaultShortcut: "Ctrl+,"; onTriggered: settingsDialog.open() }
            Action {
                text: shell.registered ? qsTr("Убрать из «Открыть с помощью»") : qsTr("Добавить в «Открыть с помощью»")
                enabled: shell.supported
                onTriggered: {
                    const register = !shell.registered
                    const ok = shell.setRegistered(register)
                    statusBar.flash(!ok ? qsTr("Не удалось изменить реестр")
                                        : register ? qsTr("MyCodeApp добавлен в «Открыть с помощью» и меню папок")
                                                   : qsTr("MyCodeApp убран из меню Проводника"))
                }
            }
            AppMenuSeparator {}
            Command { command: "app.quit"; text: qsTr("Выход"); defaultShortcut: "Ctrl+Q"; onTriggered: window.close() }
        }
        AppMenu {
            title: qsTr("Правка")
            Command { command: "edit.undo"; text: qsTr("Отменить"); defaultShortcut: "Ctrl+Z"; enabled: editor.canUndo; onTriggered: editor.undo() }
            Command { command: "edit.redo"; text: qsTr("Повторить"); defaultShortcut: "Ctrl+Y"; enabled: editor.canRedo; onTriggered: editor.redo() }
            AppMenuSeparator {}
            Command { command: "edit.cut"; text: qsTr("Вырезать"); defaultShortcut: "Ctrl+X"; enabled: window.hasDocument; onTriggered: editor.cut() }
            Command { command: "edit.copy"; text: qsTr("Копировать"); defaultShortcut: "Ctrl+C"; enabled: window.hasDocument; onTriggered: editor.copy() }
            Command { command: "edit.paste"; text: qsTr("Вставить"); defaultShortcut: "Ctrl+V"; enabled: window.hasDocument; onTriggered: editor.paste() }
            AppMenuSeparator {}
            Command { command: "edit.selectAll"; text: qsTr("Выделить всё"); defaultShortcut: "Ctrl+A"; enabled: window.hasDocument; onTriggered: editor.selectAll() }
            AppMenuSeparator {}
            Command { command: "edit.selectNextOccurrence"; text: qsTr("Выделить следующее вхождение"); defaultShortcut: "Ctrl+D"; enabled: window.hasDocument; onTriggered: editor.selectNextOccurrence() }
            Command { command: "edit.addCaretAbove"; text: qsTr("Добавить курсор выше"); defaultShortcut: "Ctrl+Alt+Up"; enabled: window.hasDocument; onTriggered: editor.addCaretVertical(-1) }
            Command { command: "edit.addCaretBelow"; text: qsTr("Добавить курсор ниже"); defaultShortcut: "Ctrl+Alt+Down"; enabled: window.hasDocument; onTriggered: editor.addCaretVertical(1) }
        }
        AppMenu {
            title: qsTr("Поиск")
            Command { command: "find.find"; text: qsTr("Найти…"); defaultShortcut: "Ctrl+F"; enabled: window.hasDocument; onTriggered: findBar.open() }
            Command { command: "find.next"; text: qsTr("Следующее совпадение"); defaultShortcut: "F3"; enabled: window.hasDocument; onTriggered: editor.findNext() }
            Command { command: "find.previous"; text: qsTr("Предыдущее совпадение"); defaultShortcut: "Shift+F3"; enabled: window.hasDocument; onTriggered: editor.findPrev() }
            AppMenuSeparator {}
            Command { command: "find.goToLine"; text: qsTr("Перейти к строке…"); defaultShortcut: "Ctrl+G"; enabled: window.hasDocument; onTriggered: goToLine.show() }
            Command { command: "find.quickOpen"; text: qsTr("Перейти к файлу…"); defaultShortcut: "Ctrl+P"; onTriggered: quickOpen.show() }
            Command { command: "find.inProject"; text: qsTr("Найти в проекте…"); defaultShortcut: "Ctrl+Shift+F"; onTriggered: sidebar.showSearch("") }
        }
        AppMenu {
            title: qsTr("Код")
            Command { command: "code.complete"; text: qsTr("Предложить варианты"); defaultShortcut: "Ctrl+Space"; enabled: window.hasDocument; onTriggered: completion.trigger() }
            Command { command: "code.definition"; text: qsTr("Перейти к определению"); defaultShortcut: "F12"; enabled: window.hasDocument; onTriggered: lsp.gotoDefinition(editor, editor.cursorLine, editor.cursorColumn) }
            Command { command: "code.references"; text: qsTr("Найти ссылки"); defaultShortcut: "Shift+F12"; enabled: window.hasDocument; onTriggered: lsp.findReferences(editor) }
            Command { command: "code.back"; text: qsTr("Назад"); defaultShortcut: "Alt+Left"; onTriggered: window.goBack() }
            AppMenuSeparator {}
            Command { command: "code.rename"; text: qsTr("Переименовать символ"); defaultShortcut: "F2"; enabled: window.hasDocument && !editor.readOnly; onTriggered: renamePopup.show() }
            Command {
                command: "code.userSnippets"
                text: qsTr("Свои сниппеты для этого языка…")
                enabled: window.hasDocument
                onTriggered: window.openAt(SnippetStore.userFile(editor.language), 0, 0, 0)
            }
            Command { command: "code.format"; text: qsTr("Форматировать документ"); defaultShortcut: "Shift+Alt+F"; enabled: window.hasDocument && !editor.readOnly; onTriggered: lsp.format(editor) }
            AppMenuSeparator {}
            Command { command: "view.problems"; text: qsTr("Проблемы"); defaultShortcut: "Ctrl+Shift+M"; onTriggered: bottomPanel.visible && bottomPanel.mode === "problems" ? bottomPanel.visible = false : bottomPanel.show("problems") }
        }
        AppMenu {
            title: qsTr("Git")
            Command { command: "git.panel"; text: qsTr("Панель Git"); defaultShortcut: "Ctrl+Shift+G"; onTriggered: sidebar.showGit() }
            Command { command: "git.fileDiff"; text: qsTr("Изменения файла"); defaultShortcut: "Ctrl+K, D"; enabled: window.hasDocument && git.available; onTriggered: window.showFileDiff() }
            AppMenuSeparator {}
            Command { command: "git.nextChange"; text: qsTr("Следующее изменение"); defaultShortcut: "Alt+F5"; onTriggered: window.stepChange(1) }
            Command { command: "git.previousChange"; text: qsTr("Предыдущее изменение"); defaultShortcut: "Shift+Alt+F5"; onTriggered: window.stepChange(-1) }
            AppMenuSeparator {}
            Command {
                command: "git.stageFile"
                text: qsTr("Добавить файл в индекс")
                enabled: window.hasDocument && git.available
                onTriggered: { editor.save(); git.stage([editor.filePath]) }
            }
            Command { command: "git.refresh"; text: qsTr("Обновить состояние"); enabled: git.available; onTriggered: git.refresh() }
        }
        AppMenu {
            id: pluginMenu
            title: qsTr("Плагины")
            Instantiator {
                model: plugins.commands
                delegate: Command {
                    required property var modelData
                    command: "plugin." + modelData.id
                    text: modelData.title
                    defaultShortcut: modelData.shortcut
                    onTriggered: plugins.run(modelData.id)
                }
                onObjectAdded: (index, object) => pluginMenu.insertAction(index, object)
                onObjectRemoved: (index, object) => pluginMenu.removeAction(object)
            }
            AppMenuSeparator {}
            Command { command: "app.plugins"; text: qsTr("Управление плагинами…"); onTriggered: { settingsDialog.page = "plugins"; settingsDialog.open() } }
        }
        AppMenu {
            title: qsTr("Вид")
            Command { command: "view.fold"; text: qsTr("Свернуть блок"); defaultShortcut: "Ctrl+Shift+["; enabled: window.hasDocument; onTriggered: editor.foldAtCursor() }
            Command { command: "view.unfold"; text: qsTr("Развернуть блок"); defaultShortcut: "Ctrl+Shift+]"; enabled: window.hasDocument; onTriggered: editor.unfoldAtCursor() }
            AppMenuSeparator {}
            Command { command: "view.foldAll"; text: qsTr("Свернуть все"); defaultShortcut: "Ctrl+K, Ctrl+0"; enabled: window.hasDocument; onTriggered: editor.foldAll() }
            Command { command: "view.unfoldAll"; text: qsTr("Развернуть все"); defaultShortcut: "Ctrl+K, Ctrl+J"; enabled: window.hasDocument; onTriggered: editor.unfoldAll() }
            AppMenuSeparator {}
            Command { command: "view.focusSidebar"; text: qsTr("Перейти в боковую панель"); defaultShortcut: "Ctrl+0"; onTriggered: sidebar.focusPanel() }
            Command { command: "view.focusEditor"; text: qsTr("Перейти в редактор"); defaultShortcut: "Ctrl+1"; onTriggered: editor.forceActiveFocus() }
            Command { command: "view.explorer"; text: qsTr("Проводник"); defaultShortcut: "Ctrl+Shift+E"; onTriggered: { sidebar.mode = "files"; sidebar.visible = true } }
            Command { command: "view.sidebar"; title: qsTr("Показать или скрыть боковую панель"); text: sidebar.visible ? qsTr("Скрыть боковую панель") : qsTr("Показать боковую панель"); defaultShortcut: "Ctrl+B"; onTriggered: sidebar.visible = !sidebar.visible }
            Command { command: "view.minimap"; title: qsTr("Показать или скрыть миникарту"); text: window.showMinimap ? qsTr("Скрыть миникарту") : qsTr("Показать миникарту"); onTriggered: window.showMinimap = !window.showMinimap }
            Command { command: "view.minimapLarger"; text: qsTr("Миникарта крупнее"); enabled: window.showMinimap && minimap.level < 3; onTriggered: minimap.level++ }
            Command { command: "view.minimapSmaller"; text: qsTr("Миникарта мельче"); enabled: window.showMinimap && minimap.level > 1; onTriggered: minimap.level-- }
            AppMenuSeparator {}
            AppMenu {
                id: themeMenu
                title: qsTr("Тема")
                Instantiator {
                    model: ThemeManager.themes
                    delegate: Action {
                        required property var modelData
                        readonly property string title: Localization.language === "en" && modelData.nameEn ? modelData.nameEn : modelData.name
                        text: modelData.id === ThemeManager.current ? title + qsTr("  (текущая)") : title
                        onTriggered: ThemeManager.setTheme(modelData.id)
                    }
                    onObjectAdded: (index, object) => themeMenu.insertAction(index, object)
                    onObjectRemoved: (index, object) => themeMenu.removeAction(object)
                }
                AppMenuSeparator {}
                Action {
                    text: qsTr("Создать свою тему…")
                    onTriggered: {
                        const path = ThemeManager.createUserTheme()
                        if (path !== "")
                            window.openAt(path, 0, 0, 0)
                    }
                }
            }
        }
        AppMenu {
            title: qsTr("Справка")
            Command { command: "help.shortcuts"; text: qsTr("Горячие клавиши…"); onTriggered: { settingsDialog.page = "keys"; settingsDialog.open() } }
            Command { command: "help.dataFolder"; text: qsTr("Открыть папку данных"); onTriggered: Qt.openUrlExternally("file:///" + AppInfo.dataDir) }
            AppMenuSeparator {}
            Command { command: "help.about"; text: qsTr("О программе"); onTriggered: aboutDialog.open() }
        }
    }

    Shortcut { sequence: "Ctrl+Tab"; onActivated: window.cycleTab(1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: window.cycleTab(-1) }

    Sidebar {
        id: sidebar
        workspace: workspace
        editor: editor
        git: git
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.bottom: statusBar.top
        onOpenFile: (path) => window.openAt(path, 0, 0, 0)
        onOpenDiff: (path, staged) => diffView.show(path, staged)
        onOpenMatch: (path, line, column, length) => window.openAt(path, line, column, length)
        onOpenFolderRequested: folderDialog.open()
        onLeave: editor.forceActiveFocus()
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
        Keys.onPressed: (event) => {
            completion.handleKey(event) // пока открыт список, стрелки и Enter — его
            if (!event.accepted && event.key === Qt.Key_Tab && event.modifiers === Qt.NoModifier
                    && !editor.snippetActive && SnippetStore.expandAtCursor(editor))
                event.accepted = true // слово перед курсором — префикс сниппета
        }
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
        width: visible ? implicitWidth : 0
        level: workspace.setting("minimapLevel", 3)
        onLevelChanged: workspace.setSetting("minimapLevel", level)
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

    DiffView {
        id: diffView
        git: git
        anchors.top: editor.top
        anchors.left: editor.left
        anchors.right: parent.right
        anchors.bottom: editor.bottom
        onClosed: editor.forceActiveFocus()
        onVisibleChanged: if (visible) hoverPopup.close()
        onOpenFile: (path) => window.openAt(path, 0, 0, 0)
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

    GoToLinePopup {
        id: goToLine
        editor: editor
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
        branch: git.branch
        onProblemsRequested: bottomPanel.show("problems")
        onBranchRequested: sidebar.showGit()
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

    AboutDialog { id: aboutDialog }

    SettingsDialog {
        id: settingsDialog
        plugins: plugins
        onOpenFile: (path) => window.openAt(path, 0, 0, 0)
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
