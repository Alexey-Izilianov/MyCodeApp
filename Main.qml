import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QWindowKit
import MyCodeApp

ApplicationWindow {
    id: window
    width: 1200
    height: 760
    minimumWidth: 640
    minimumHeight: 400
    visible: false // показывается после установки своей рамки (WindowAgent)
    color: Theme.ground
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    // Стандартные подсказки (ToolTip) берут цвета из палитры окна — пусть следуют теме
    palette.toolTipBase: Theme.popup
    palette.toolTipText: Theme.text
    palette.dark: Theme.border
    title: manager.currentIndex >= 0
           ? manager.tabs[manager.currentIndex].name + " — MyCodeApp"
           : "MyCodeApp"

    readonly property bool hasDocument: manager.currentIndex >= 0

    // Правая половина экрана: свой документ и свой EditorView (тот же файл можно открыть
    // в обеих). Команды меню, поиск и строка состояния работают с половиной, где был фокус
    property QtObject splitDocument: null
    readonly property bool split: splitDocument !== null
    property real splitRatio: 0.5 // доля левой половины
    property EditorView activeEditor: editor
    onSplitChanged: if (!split) activeEditor = editor

    function openSplit(doc, index) {
        if (!doc)
            return
        splitDocument = doc
        // Перетащили текущую вкладку — слева остаётся соседняя
        if (index >= 0 && index === manager.currentIndex && manager.tabs.length > 1)
            manager.activate(index > 0 ? index - 1 : index + 1)
        editor2.forceActiveFocus()
    }
    function closeSplit() {
        splitDocument = null
        editor.forceActiveFocus()
    }
    function splitIndex() {
        for (let i = 0; i < manager.tabs.length; ++i)
            if (manager.documentAt(i) === splitDocument)
                return i
        return -1
    }

    // Общие обработчики обеих половин
    function editorKey(view, event) {
        completion.handleKey(event) // пока открыт список, стрелки и Enter — его
        if (!event.accepted && event.key === Qt.Key_Tab && event.modifiers === Qt.NoModifier
                && !view.snippetActive && SnippetStore.expandAtCursor(view))
            event.accepted = true // слово перед курсором — префикс сниппета
    }
    function editorTyped(view, text) {
        // Запрос на каждый символ идентификатора и после . -> :: — сервер сам
        // сужает выдачу по префиксу, а список уже на экране фильтруется локально
        const before = view.wordBeforeCursor()
        if (/^[A-Za-z_]$/.test(text) || text === "." || (text === ">" || text === ":") && before === "")
            completion.trigger()
        else if (!/^[A-Za-z0-9_]$/.test(text))
            completion.close()
    }
    function editorHover(view, line, column, x, y) {
        hoverPopup.editor = view
        hoverPopup.pending = Qt.point(x, y)
        const color = view.colorAt(line, column)
        if (color.hex)
            hoverPopup.showColor(color, hoverPopup.pending)
        else
            lsp.hover(view, line, column)
    }
    property bool showMinimap: workspace.setting("minimapVisible", true)
    onShowMinimapChanged: workspace.setSetting("minimapVisible", showMinimap)
    property bool wordWrap: workspace.setting("wordWrap", true)
    onWordWrapChanged: workspace.setSetting("wordWrap", wordWrap)

    WindowAgent { id: windowAgent }
    property bool agentReady: false // до setup() агент падает на любом вызове
    readonly property bool darkFrame: Theme.ground.hslLightness < 0.5
    onDarkFrameChanged: if (agentReady) windowAgent.setWindowAttribute("dark-mode", darkFrame) // цвет системной каймы и тени
    Component.onCompleted: {
        windowAgent.setup(window)
        agentReady = true
        windowAgent.setWindowAttribute("dark-mode", darkFrame)
        window.visible = true
    }

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
        const right = split ? splitIndex() : -1
        workspace.saveSession({ files: files, current: hasDocument ? manager.tabs[manager.currentIndex].path : "",
                                split: right >= 0 ? manager.tabs[right].path : "",
                                splitState: right >= 0 ? editor2.viewStateOf(splitDocument) : {} })
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
        const right = manager.tabs.findIndex(tab => tab.path === session.split)
        if (right >= 0 && session.split) {
            editor2.setViewStateOf(manager.documentAt(right), session.splitState ?? {})
            openSplit(manager.documentAt(right), -1)
        }
    }
    // Переходы к определению запоминаются: команда «Назад» возвращает
    property var history: []
    function navigateTo(path, line, column) {
        if (activeEditor.filePath !== "")
            history.push({ path: activeEditor.filePath, line: activeEditor.cursorLine, column: activeEditor.cursorColumn })
        openAt(path, line, column, 0)
    }
    function goBack() {
        const place = history.pop()
        if (place)
            openAt(place.path, place.line, place.column, 0)
    }
    function updateDiagnostics() {
        editor.setDiagnostics(lsp.diagnosticsFor(editor.filePath))
        if (split)
            editor2.setDiagnostics(lsp.diagnosticsFor(editor2.filePath))
    }

    // Открывается в половине с фокусом; правая не меняет вкладку левой
    function openAt(path, line, column, length) {
        if (activeEditor === editor2) {
            const left = manager.currentIndex
            const index = manager.openPath(path)
            if (index >= 0) {
                if (left >= 0 && left !== index)
                    manager.activate(left)
                splitDocument = manager.documentAt(index)
                editor2.goTo(line, column, length)
            }
            editor2.forceActiveFocus()
            return
        }
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
        editor: window.activeEditor
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
        onTriggered: {
            git.updateMarks(editor)
            if (window.split)
                git.updateMarks(editor2)
        }
    }
    Connections {
        target: editor
        function onContentChanged() { marksTimer.restart() }
    }
    Connections {
        target: editor2
        function onContentChanged() { marksTimer.restart() }
    }
    Connections {
        target: manager
        function onTabsChanged() {
            git.refresh() // в т.ч. сохранение файла
            if (window.split && window.splitIndex() < 0)
                window.closeSplit() // файл правой половины закрыли
        }
    }
    onActiveChanged: if (active) git.refresh()

    function showFileDiff() {
        if (activeEditor.filePath === "")
            return
        diffView.show(activeEditor.filePath, false)
    }
    function stepChange(direction) {
        if (diffView.visible)
            diffView.step(direction)
        else if (activeEditor.document)
            activeEditor.gotoChange(direction)
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

    header: TitleBar {
        id: titleBar
        agent: windowAgent
        window: window
        searchShortcut: Keymap.revision >= 0 ? Keymap.sequence("find.quickOpen", "Ctrl+P") : ""
        onSearchRequested: quickOpen.show()

        MenuBar {
            background: null
            delegate: MenuBarItem {
                id: menuBarItem
                leftPadding: 9
                rightPadding: 9
                topPadding: 5
                bottomPadding: 5
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
                    onTriggered: window.activeEditor.save()
                }
                AppMenuSeparator {}
                Command {
                    command: "file.closeTab"
                    text: qsTr("Закрыть вкладку"); defaultShortcut: "Ctrl+W"
                    enabled: window.hasDocument
                    onTriggered: window.activeEditor === editor2 ? window.closeSplit() : window.requestClose(manager.currentIndex)
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
                Command { command: "edit.undo"; text: qsTr("Отменить"); defaultShortcut: "Ctrl+Z"; enabled: window.activeEditor.canUndo; onTriggered: window.activeEditor.undo() }
                Command { command: "edit.redo"; text: qsTr("Повторить"); defaultShortcut: "Ctrl+Y"; enabled: window.activeEditor.canRedo; onTriggered: window.activeEditor.redo() }
                AppMenuSeparator {}
                Command { command: "edit.cut"; text: qsTr("Вырезать"); defaultShortcut: "Ctrl+X"; enabled: window.hasDocument; onTriggered: window.activeEditor.cut() }
                Command { command: "edit.copy"; text: qsTr("Копировать"); defaultShortcut: "Ctrl+C"; enabled: window.hasDocument; onTriggered: window.activeEditor.copy() }
                Command { command: "edit.paste"; text: qsTr("Вставить"); defaultShortcut: "Ctrl+V"; enabled: window.hasDocument; onTriggered: window.activeEditor.paste() }
                AppMenuSeparator {}
                Command { command: "edit.selectAll"; text: qsTr("Выделить всё"); defaultShortcut: "Ctrl+A"; enabled: window.hasDocument; onTriggered: window.activeEditor.selectAll() }
                AppMenuSeparator {}
                Command { command: "edit.selectNextOccurrence"; text: qsTr("Выделить следующее вхождение"); defaultShortcut: "Ctrl+D"; enabled: window.hasDocument; onTriggered: window.activeEditor.selectNextOccurrence() }
                Command { command: "edit.addCaretAbove"; text: qsTr("Добавить курсор выше"); defaultShortcut: "Ctrl+Alt+Up"; enabled: window.hasDocument; onTriggered: window.activeEditor.addCaretVertical(-1) }
                Command { command: "edit.addCaretBelow"; text: qsTr("Добавить курсор ниже"); defaultShortcut: "Ctrl+Alt+Down"; enabled: window.hasDocument; onTriggered: window.activeEditor.addCaretVertical(1) }
            }
            AppMenu {
                title: qsTr("Поиск")
                Command { command: "find.find"; text: qsTr("Найти…"); defaultShortcut: "Ctrl+F"; enabled: window.hasDocument; onTriggered: findBar.open() }
                Command { command: "find.next"; text: qsTr("Следующее совпадение"); defaultShortcut: "F3"; enabled: window.hasDocument; onTriggered: window.activeEditor.findNext() }
                Command { command: "find.previous"; text: qsTr("Предыдущее совпадение"); defaultShortcut: "Shift+F3"; enabled: window.hasDocument; onTriggered: window.activeEditor.findPrev() }
                AppMenuSeparator {}
                Command { command: "find.goToLine"; text: qsTr("Перейти к строке…"); defaultShortcut: "Ctrl+G"; enabled: window.hasDocument; onTriggered: goToLine.show() }
                Command { command: "find.quickOpen"; text: qsTr("Перейти к файлу…"); defaultShortcut: "Ctrl+P"; onTriggered: quickOpen.show() }
                Command { command: "find.inProject"; text: qsTr("Найти в проекте…"); defaultShortcut: "Ctrl+Shift+F"; onTriggered: sidebar.showSearch("") }
            }
            AppMenu {
                title: qsTr("Код")
                Command { command: "code.complete"; text: qsTr("Предложить варианты"); defaultShortcut: "Ctrl+Space"; enabled: window.hasDocument; onTriggered: completion.trigger() }
                Command { command: "code.definition"; text: qsTr("Перейти к определению"); defaultShortcut: "F12"; enabled: window.hasDocument; onTriggered: lsp.gotoDefinition(window.activeEditor, window.activeEditor.cursorLine, window.activeEditor.cursorColumn) }
                Command { command: "code.references"; text: qsTr("Найти ссылки"); defaultShortcut: "Shift+F12"; enabled: window.hasDocument; onTriggered: lsp.findReferences(window.activeEditor) }
                Command { command: "code.back"; text: qsTr("Назад"); defaultShortcut: "Alt+Left"; onTriggered: window.goBack() }
                AppMenuSeparator {}
                Command { command: "code.rename"; text: qsTr("Переименовать символ"); defaultShortcut: "F2"; enabled: window.hasDocument && !window.activeEditor.readOnly; onTriggered: renamePopup.show() }
                Command {
                    command: "code.userSnippets"
                    text: qsTr("Свои сниппеты для этого языка…")
                    enabled: window.hasDocument
                    onTriggered: window.openAt(SnippetStore.userFile(window.activeEditor.language), 0, 0, 0)
                }
                Command { command: "code.format"; text: qsTr("Форматировать документ"); defaultShortcut: "Shift+Alt+F"; enabled: window.hasDocument && !window.activeEditor.readOnly; onTriggered: lsp.format(window.activeEditor) }
                AppMenuSeparator {}
                Command { command: "view.problems"; text: qsTr("Проблемы"); defaultShortcut: "Ctrl+Shift+M"; onTriggered: bottomPanel.visible && bottomPanel.mode === "problems" ? bottomPanel.visible = false : bottomPanel.show("problems") }
            }
            AppMenu {
                title: qsTr("Git")
                Command { command: "git.panel"; text: qsTr("Панель Git"); defaultShortcut: "Ctrl+Shift+G"; onTriggered: sidebar.showGit() }
                Command { command: "git.fileDiff"; text: qsTr("Изменения файла"); defaultShortcut: "Ctrl+K, D"; enabled: window.hasDocument && git.available; onTriggered: window.showFileDiff() }
                Command { command: "git.history"; text: qsTr("История"); defaultShortcut: "Ctrl+K, H"; enabled: git.available; onTriggered: historyView.open() }
                AppMenuSeparator {}
                Command { command: "git.nextChange"; text: qsTr("Следующее изменение"); defaultShortcut: "Alt+F5"; onTriggered: window.stepChange(1) }
                Command { command: "git.previousChange"; text: qsTr("Предыдущее изменение"); defaultShortcut: "Shift+Alt+F5"; onTriggered: window.stepChange(-1) }
                AppMenuSeparator {}
                Command {
                    command: "git.stageFile"
                    text: qsTr("Добавить файл в индекс")
                    enabled: window.hasDocument && git.available
                    onTriggered: { window.activeEditor.save(); git.stage([window.activeEditor.filePath]) }
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
                Command { command: "view.fold"; text: qsTr("Свернуть блок"); defaultShortcut: "Ctrl+Shift+["; enabled: window.hasDocument; onTriggered: window.activeEditor.foldAtCursor() }
                Command { command: "view.unfold"; text: qsTr("Развернуть блок"); defaultShortcut: "Ctrl+Shift+]"; enabled: window.hasDocument; onTriggered: window.activeEditor.unfoldAtCursor() }
                AppMenuSeparator {}
                Command { command: "view.foldAll"; text: qsTr("Свернуть все"); defaultShortcut: "Ctrl+K, Ctrl+0"; enabled: window.hasDocument; onTriggered: window.activeEditor.foldAll() }
                Command { command: "view.unfoldAll"; text: qsTr("Развернуть все"); defaultShortcut: "Ctrl+K, Ctrl+J"; enabled: window.hasDocument; onTriggered: window.activeEditor.unfoldAll() }
                AppMenuSeparator {}
                Command { command: "view.splitRight"; text: qsTr("Открыть справа"); defaultShortcut: "Ctrl+\\"; enabled: window.hasDocument; onTriggered: window.openSplit(manager.currentDocument, -1) }
                Command { command: "view.closeSplit"; text: qsTr("Закрыть правую половину"); enabled: window.split; onTriggered: window.closeSplit() }
                AppMenuSeparator {}
                Command { command: "view.focusSidebar"; text: qsTr("Перейти в боковую панель"); defaultShortcut: "Ctrl+0"; onTriggered: sidebar.focusPanel() }
                Command { command: "view.focusEditor"; text: qsTr("Перейти в редактор"); defaultShortcut: "Ctrl+1"; onTriggered: window.activeEditor.forceActiveFocus() }
                Command { command: "view.explorer"; text: qsTr("Проводник"); defaultShortcut: "Ctrl+Shift+E"; onTriggered: { sidebar.mode = "files"; sidebar.visible = true } }
                Command { command: "view.sidebar"; title: qsTr("Показать или скрыть боковую панель"); text: sidebar.visible ? qsTr("Скрыть боковую панель") : qsTr("Показать боковую панель"); defaultShortcut: "Ctrl+B"; onTriggered: sidebar.visible = !sidebar.visible }
                Command { command: "view.wordWrap"; title: qsTr("Перенос строк"); text: window.wordWrap ? qsTr("Не переносить строки") : qsTr("Переносить строки"); defaultShortcut: "Alt+Z"; onTriggered: window.wordWrap = !window.wordWrap }
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
    }

    Shortcut { sequence: "Ctrl+Tab"; onActivated: window.cycleTab(1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: window.cycleTab(-1) }

    ActivityBar {
        id: activityBar
        sidebar: sidebar
        gitChanges: git.changes.length + git.stagedChanges.length
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.bottom: statusBar.top
        onPluginsRequested: { settingsDialog.page = "plugins"; settingsDialog.open() }
        onSettingsRequested: settingsDialog.open()
    }

    Sidebar {
        id: sidebar
        workspace: workspace
        editor: window.activeEditor
        git: git
        anchors.top: parent.top
        anchors.left: activityBar.right
        anchors.bottom: statusBar.top
        onOpenFile: (path) => window.openAt(path, 0, 0, 0)
        onOpenDiff: (path, staged) => diffView.show(path, staged)
        onOpenHistory: historyView.open()
        onOpenMatch: (path, line, column, length) => window.openAt(path, line, column, length)
        onOpenFolderRequested: folderDialog.open()
        onLeave: window.activeEditor.forceActiveFocus()
    }

    // Остров редактора: вкладки, текст, миникарта, полоса прокрутки и всё, что поверх них
    Rectangle {
        id: editorArea
        anchors.top: parent.top
        anchors.left: sidebar.visible ? sidebar.right : activityBar.right
        anchors.leftMargin: sidebar.visible ? Theme.islandGap : 0
        anchors.right: window.split ? splitArea.left : parent.right
        anchors.rightMargin: Theme.islandGap
        anchors.bottom: bottomPanel.visible ? bottomPanel.top : statusBar.top
        anchors.bottomMargin: bottomPanel.visible ? Theme.islandGap : 0
        color: Theme.editor
    }

    TabStrip {
        id: tabStrip
        manager: manager
        anchors.top: editorArea.top
        anchors.left: editorArea.left
        anchors.right: editorArea.right
        visible: manager.tabs.length > 0
        height: visible ? implicitHeight : 0
        onCloseRequested: (index) => window.requestClose(index)
        onTabDragged: (index, point) => dropZone.track(index, point)
        onTabDropped: (index, point) => {
            dropZone.track(index, point)
            if (dropZone.hot)
                window.openSplit(manager.documentAt(index), index)
            dropZone.index = -1
        }
    }

    EditorView {
        id: editor
        objectName: "editor"
        anchors.top: tabStrip.bottom
        anchors.left: editorArea.left
        anchors.right: vbar.left
        rightMargin: minimap.width // миникарта лежит поверх; место под неё есть в обеих половинах
        anchors.bottom: editorArea.bottom
        document: manager.currentDocument
        wordWrap: window.wordWrap
        focus: true
        onActiveFocusChanged: if (activeFocus) window.activeEditor = editor
        onErrorOccurred: (message) => console.warn(message)
        onDocumentChanged: {
            forceActiveFocus()
            window.updateDiagnostics()
        }
        Keys.onPressed: (event) => window.editorKey(editor, event)
        onTextTyped: (text) => window.editorTyped(editor, text)
        onHoverRest: (line, column, x, y) => window.editorHover(editor, line, column, x, y)
        onHoverLeft: hoverPopup.leave()
        onDefinitionClicked: (line, column) => lsp.gotoDefinition(editor, line, column)
    }

    // Одна миникарта — у половины с фокусом
    readonly property bool minimapRight: split && activeEditor === editor2
    Minimap {
        id: minimap
        readonly property EditorView host: window.minimapRight ? editor2 : editor
        editor: host
        z: 1 // поверх фона правой половины, объявленного ниже
        background: Theme.editor
        anchors.top: host.top
        anchors.right: window.minimapRight ? vbar2.left : vbar.left
        anchors.bottom: host.bottom
        visible: window.showMinimap && window.hasDocument
        width: visible ? implicitWidth : 0
        level: workspace.setting("minimapLevel", 3)
        onLevelChanged: workspace.setSetting("minimapLevel", level)

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton // левая кнопка — самой миникарте
            onClicked: minimapMenu.popup()
        }
        AppMenu {
            id: minimapMenu
            Action { text: qsTr("Закрыть мини-карту"); onTriggered: window.showMinimap = false }
        }
    }

    ScrollBar {
        id: vbar
        anchors.top: editor.top
        anchors.right: editorArea.right
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

    // Правая половина: заголовок с именем файла, текст, полоса прокрутки
    Rectangle {
        id: splitArea
        readonly property real editorsLeft: sidebar.visible ? sidebar.x + sidebar.width + Theme.islandGap
                                                            : activityBar.width
        readonly property real editorsWidth: parent.width - editorsLeft - Theme.islandGap
        visible: window.split
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.rightMargin: Theme.islandGap
        anchors.bottom: editorArea.bottom
        width: visible ? Math.round((editorsWidth - Theme.islandGap) * (1 - window.splitRatio)) : 0
        color: Theme.editor
    }
    Rectangle {
        id: splitHeader
        readonly property int index: window.split && manager.tabs.length >= 0 ? window.splitIndex() : -1
        readonly property var tab: index >= 0 ? manager.tabs[index] : null
        visible: window.split
        anchors.top: splitArea.top
        anchors.left: splitArea.left
        anchors.right: splitArea.right
        height: tabStrip.implicitHeight
        color: Theme.editor
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
        FileIcon {
            id: splitIcon
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 14
            name: splitHeader.tab?.name ?? ""
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: splitIcon.right
            anchors.leftMargin: 6
            anchors.right: splitClose.left
            anchors.rightMargin: 8
            text: (splitHeader.tab?.name ?? "") + (splitHeader.tab?.dirty ? "  •" : "")
            elide: Text.ElideMiddle
            color: window.activeEditor === editor2 ? Theme.text : Theme.muted
        }
        IconButton {
            id: splitClose
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 8
            tip: qsTr("Закрыть правую половину")
            onClicked: window.closeSplit()
        }
    }
    EditorView {
        id: editor2
        objectName: "editor2"
        visible: window.split
        anchors.top: splitHeader.bottom
        anchors.left: splitArea.left
        anchors.right: vbar2.left
        rightMargin: minimap.width
        anchors.bottom: splitArea.bottom
        document: window.splitDocument
        wordWrap: window.wordWrap
        onActiveFocusChanged: if (activeFocus) window.activeEditor = editor2
        onErrorOccurred: (message) => console.warn(message)
        onDocumentChanged: window.updateDiagnostics()
        Keys.onPressed: (event) => window.editorKey(editor2, event)
        onTextTyped: (text) => window.editorTyped(editor2, text)
        onHoverRest: (line, column, x, y) => window.editorHover(editor2, line, column, x, y)
        onHoverLeft: hoverPopup.leave()
        onDefinitionClicked: (line, column) => lsp.gotoDefinition(editor2, line, column)
    }
    ScrollBar {
        id: vbar2
        visible: window.split
        anchors.top: editor2.top
        anchors.right: splitArea.right
        anchors.bottom: editor2.bottom
        width: 12
        orientation: Qt.Vertical
        policy: size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        size: editor2.contentHeight > 0 ? Math.min(1, editor2.height / editor2.contentHeight) : 1
        onPositionChanged: if (pressed) editor2.scrollY = position * editor2.contentHeight
        contentItem: Rectangle {
            implicitWidth: 6
            radius: 3
            color: Theme.scrollThumb
            opacity: vbar2.pressed ? 1 : (vbar2.hovered ? 0.9 : 0.6)
        }
        background: Rectangle { color: Theme.editor }
    }
    Binding {
        target: vbar2
        property: "position"
        value: editor2.contentHeight > 0 ? editor2.scrollY / editor2.contentHeight : 0
        when: !vbar2.pressed
    }
    // Зазор между половинами — ручка ширины
    MouseArea {
        visible: window.split
        x: editorArea.x + editorArea.width
        width: Theme.islandGap
        anchors.top: editorArea.top
        anchors.bottom: editorArea.bottom
        cursorShape: Qt.SplitHCursor
        onPositionChanged: (mouse) => {
            const at = mapToItem(splitArea.parent, mouse.x, 0).x - splitArea.editorsLeft
            window.splitRatio = Math.max(0.2, Math.min(0.8, at / splitArea.editorsWidth))
        }
    }

    HistoryView {
        id: historyView
        git: git
        anchors.top: editor.top
        anchors.left: editor.left
        anchors.right: window.split ? splitArea.right : editorArea.right // на обе половины
        anchors.bottom: editor.bottom
        z: 2 // поверх миникарты (z: 1)
        onClosed: window.activeEditor.forceActiveFocus()
        onVisibleChanged: if (visible) hoverPopup.close()
        onOpenCommitFile: (id, entry) => diffView.showCommit(id, entry)
    }
    Connections {
        target: manager
        function onCurrentIndexChanged() { historyView.close() } // выбрали вкладку — к редактору
    }
    DiffView {
        id: diffView
        git: git
        anchors.top: editor.top
        anchors.left: editor.left
        anchors.right: historyView.visible ? historyView.right : editorArea.right
        anchors.bottom: editor.bottom
        z: 3 // поверх истории и миникарты
        onClosed: historyView.visible ? historyView.focusList() : window.activeEditor.forceActiveFocus()
        onVisibleChanged: if (visible) hoverPopup.close()
        onOpenFile: (path) => window.openAt(path, 0, 0, 0)
    }

    FindBar {
        id: findBar
        editor: window.activeEditor
        anchors.top: window.activeEditor.top
        anchors.right: window.activeEditor.right
        anchors.topMargin: 8
        anchors.rightMargin: 12 + window.activeEditor.rightMargin
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
        anchors.left: editorArea.left
        anchors.right: window.split ? splitArea.right : editorArea.right
        anchors.bottom: statusBar.top
        onOpenAt: (path, line, column) => window.openAt(path, line, column, 0)
    }

    IslandCorners { target: sidebar }
    IslandCorners { target: editorArea }
    IslandCorners { target: splitArea }

    // Куда бросить вкладку: правая половина области редактора (или уже открытая правая)
    Rectangle {
        id: dropZone
        property int index: -1
        property point point
        readonly property bool hot: index >= 0 && hit(point)
        function track(tabIndex, scenePoint) {
            index = tabIndex
            point = parent.mapFromItem(null, scenePoint.x, scenePoint.y)
        }
        function hit(p) {
            return p.x >= x && p.x < x + width && p.y >= y && p.y < y + height
        }
        visible: index >= 0
        x: window.split ? splitArea.x : editorArea.x + editorArea.width / 2
        y: tabStrip.y + tabStrip.height
        width: window.split ? splitArea.width : editorArea.width / 2
        height: editorArea.y + editorArea.height - y
        radius: Theme.islandRadius
        color: hot ? Qt.alpha(Theme.accent, 0.14) : "transparent"
        border.color: hot ? Theme.accent : Qt.alpha(Theme.accent, 0.4)
        border.width: 1
        Label {
            anchors.centerIn: parent
            text: qsTr("Открыть справа")
            color: dropZone.hot ? Theme.text : Theme.muted
        }
    }
    // Имя перетаскиваемой вкладки у курсора
    Rectangle {
        visible: dropZone.index >= 0
        x: dropZone.point.x + 12
        y: dropZone.point.y + 8
        width: ghostLabel.implicitWidth + 20
        height: 26
        radius: 6
        color: Theme.popup
        border.color: Theme.border
        Label {
            id: ghostLabel
            anchors.centerIn: parent
            text: dropZone.index >= 0 && dropZone.index < manager.tabs.length ? manager.tabs[dropZone.index].name : ""
            color: Theme.text
        }
    }
    IslandCorners { target: bottomPanel }

    CompletionPopup {
        id: completion
        editor: window.activeEditor
        lsp: lsp
    }

    HoverPopup {
        id: hoverPopup
        editor: editor
        property point pending
    }

    GoToLinePopup {
        id: goToLine
        editor: window.activeEditor
    }

    RenamePopup {
        id: renamePopup
        editor: window.activeEditor
        onRenameRequested: (name) => lsp.rename(window.activeEditor, name)
    }

    StatusBar {
        id: statusBar
        editor: window.activeEditor
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
