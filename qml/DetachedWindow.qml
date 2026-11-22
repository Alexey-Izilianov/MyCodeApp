import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import MyCodeApp

// Отделённая вкладка: своё окно со своим списком вкладок. В главном окне
// остаются проект, Git и поиск по проекту; здесь — полный редактор с
// языковыми серверами и ассистентом. Рамка системная (не QWindowKit).
ApplicationWindow {
    id: detached
    width: 860
    height: 620
    minimumWidth: 420
    minimumHeight: 280
    visible: true
    color: Theme.ground
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    // Стандартные подсказки (ToolTip) берут цвета из палитры окна — пусть следуют теме
    palette.toolTipBase: Theme.popup
    palette.toolTipText: Theme.text
    palette.dark: Theme.border
    title: hasDocument
           ? manager.tabs[manager.currentIndex].name + " — MyCodeApp" : "MyCodeApp"

    // Общий Workspace главного окна (настройки, корень проекта) — только чтение
    required property QtObject workspace
    signal returnDocument(var docMoved, var editorState) // вернуть вкладку главному окну
    readonly property bool hasDocument: manager.currentIndex >= 0
    readonly property bool showMinimap: workspace.setting("minimapVisible", true)
    property bool assistantVisible: false
    readonly property EditorView activeEditor: editor
    property var pendingNavigation: null // переход по LSP к ещё не открытому файлу

    // Вкладка приехала из главного окна: документ и состояние редактора
    function openDocument(document, state) {
        manager.adopt(document) // станет текущей вкладкой
        editor.setViewStateOf(document, state)
        editor.forceActiveFocus()
    }

    function save() {
        if (editor.save())
            statusBar.flash(qsTr("Сохранено"))
    }

    function requestClose(index) {
        if (index < 0)
            return
        if (manager.isDirty(index))
            closeDialog.ask(manager.documentAt(index), manager.tabs[index].name)
        else
            manager.close(index)
    }

    // Переход по LSP; line < 0 — файл просто открыть
    function navigateTo(path, line, column) {
        const index = manager.openPath(path)
        if (index < 0) {
            pendingNavigation = line >= 0 ? { line, column } : null
            return
        }
        manager.activate(index)
        if (line >= 0) {
            editor.forceActiveFocus()
            editor.goTo(line, column, 0)
        }
    }

    function updateDiagnostics() {
        editor.setDiagnostics(lsp.diagnosticsFor(editor.filePath))
    }

    function editorKey(view, event) {
        completion.handleKey(event) // пока открыт список, стрелки и Enter — его
        if (!event.accepted && event.key === Qt.Key_Tab && event.modifiers === Qt.NoModifier
                && !view.snippetActive && SnippetStore.expandAtCursor(view))
            event.accepted = true // слово перед курсором — префикс сниппета
    }
    function editorTyped(view, text) {
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
    function diagnosticsForCursor(view) {
        const line = view.cursorLine
        const list = lsp.diagnosticsFor(view.filePath)
        return list.filter(d => d.startLine <= line && line <= d.endLine)
    }

    onClosing: (closeEvent) => {
        // Последнюю вкладку закрыли — окно больше не нужно; с несохранённым —
        // диалог «Сохранить / Не сохранять / Отмена», дальше решат его кнопки
        if (manager.tabs.length === 0 || !manager.isDirty(manager.currentIndex))
            closeEvent.accepted = true
        else {
            closeEvent.accepted = false
            requestClose(manager.currentIndex)
        }
    }
    // Закрытое окно уничтожаем: его менеджер и Lsp-серверы больше не нужны
    onVisibleChanged: if (!visible)
        detached.destroy()

    DocumentManager {
        id: manager
        onErrorOccurred: (message) => {
            documentErrorDialog.text = message
            documentErrorDialog.open()
        }
        onExternalChange: (index, name) => reloadDialog.ask(index, name)
        onTabsChanged: if (manager.tabs.length === 0)
            detached.close() // последнюю вкладку закрыли
        onAsyncOpenFinished: (key, document, index) => {
            if (!document)
                return
            manager.activate(index)
            if (pendingNavigation) {
                editor.forceActiveFocus()
                editor.goTo(pendingNavigation.line, pendingNavigation.column, 0)
                pendingNavigation = null
            }
        }
    }

    LspManager {
        id: lsp
        documents: manager
        rootPath: workspace.rootPath
        onDiagnosticsChanged: updateDiagnostics()
        onNavigate: (path, line, column) => navigateTo(path, line, column)
        onHoverReady: (markdown, line, column) => hoverPopup.showAt(markdown, hoverPopup.pending)
        onReferencesChanged: if (lsp.references.length > 0)
            statusBar.flash(qsTr("Ссылки: %1").arg(lsp.references.length))
        onMessage: (text) => statusBar.flash(text)
    }

    Assistant {
        id: assistant
        documents: manager
        onMessage: (text) => statusBar.flash(text)
    }

    MenuBar {
        id: menuBar
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
            Action { text: qsTr("Открыть…"); shortcut: "Ctrl+O"; onTriggered: fileDialog.open() }
            Action {
                text: qsTr("Сохранить"); shortcut: "Ctrl+S"
                enabled: detached.hasDocument
                onTriggered: detached.save()
            }
            AppMenuSeparator {}
            Action {
                text: qsTr("Закрыть вкладку"); shortcut: "Ctrl+W"
                enabled: detached.hasDocument
                onTriggered: detached.requestClose(manager.currentIndex)
            }
            AppMenuSeparator {}
            Action {
                text: qsTr("Вернуть в главное окно")
                enabled: detached.hasDocument
                onTriggered: tabStrip.returnRequested(manager.currentIndex)
            }
        }
        AppMenu {
            title: qsTr("Вид")
            Action {
                text: qsTr("Панель ассистента")
                shortcut: "Ctrl+Shift+A"
                onTriggered: {
                    detached.assistantVisible = !detached.assistantVisible
                    if (detached.assistantVisible)
                        assistantPanel.focusInput()
                }
            }
        }
        AppMenu {
            title: qsTr("Код")
            Action {
                text: qsTr("Автодополнение")
                shortcut: "Ctrl+Space"
                enabled: detached.hasDocument
                onTriggered: completion.trigger()
            }
            Action {
                text: qsTr("Перейти к определению")
                shortcut: "F12"
                enabled: detached.hasDocument
                onTriggered: lsp.gotoDefinition(editor, editor.cursorLine, editor.cursorColumn)
            }
            Action {
                text: qsTr("Найти ссылки")
                shortcut: "Shift+F12"
                enabled: detached.hasDocument
                onTriggered: lsp.findReferences(editor)
            }
            Action {
                text: qsTr("Переименовать символ")
                shortcut: "F2"
                enabled: detached.hasDocument
                onTriggered: lsp.rename(editor, editor.wordAtCursor())
            }
            Action {
                text: qsTr("Форматировать документ")
                shortcut: "Shift+Alt+F"
                enabled: detached.hasDocument
                onTriggered: lsp.format(editor)
            }
        }
        AppMenu {
            title: qsTr("Ассистент")
            Action {
                text: qsTr("Объяснить код")
                enabled: detached.hasDocument
                onTriggered: {
                    detached.assistantVisible = true
                    assistant.explainCode(editor)
                }
            }
            Action {
                text: qsTr("Комментарий к выделенному")
                enabled: detached.hasDocument && editor.hasSelection
                onTriggered: {
                    detached.assistantVisible = true
                    assistant.documentCode(editor)
                }
            }
            Action {
                text: qsTr("Объяснить ошибку в строке")
                enabled: detached.hasDocument
                onTriggered: {
                    detached.assistantVisible = true
                    const here = diagnosticsForCursor(editor)
                    for (let i = 0; i < here.length; ++i)
                        assistant.explainProblem(editor.filePath, here[i].startLine, here[i].message)
                }
            }
            AppMenuSeparator {}
            Action { text: qsTr("Остановить ответ"); enabled: assistant.busy; onTriggered: assistant.stop() }
            Action { text: qsTr("Новый разговор"); onTriggered: assistant.clear() }
        }
    }

    Rectangle {
        id: editorArea
        color: Theme.editor
        anchors.top: menuBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top

        TabStrip {
            id: tabStrip
            manager: manager
            detached: true // в отдельном окне вкладку можно только вернуть
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            visible: manager.tabs.length > 0
            height: visible ? implicitHeight : 0
            onCloseRequested: (index) => detached.requestClose(index)
            onReturnRequested: (index) => {
                const doc = manager.takeDocument(index)
                if (!doc)
                    return
                returnDocument(doc, editor.viewStateOf(doc))
                detached.destroy() // у окна отдали последнюю вкладку
            }
        }

        EditorView {
            id: editor
            objectName: "detachedEditor"
            anchors.top: tabStrip.bottom
            anchors.left: parent.left
            anchors.right: vbar.left
            rightMargin: minimap.width // миникарта лежит поверх; место под неё есть
            anchors.bottom: parent.bottom
            document: manager.currentDocument
            focus: true
            onDocumentChanged: {
                forceActiveFocus()
                updateDiagnostics()
            }
            Keys.onPressed: (event) => detached.editorKey(editor, event)
            onTextTyped: (text) => detached.editorTyped(editor, text)
            onHoverRest: (line, column, x, y) => detached.editorHover(editor, line, column, x, y)
            onHoverLeft: hoverPopup.leave()
            onDefinitionClicked: (line, column) => lsp.gotoDefinition(editor, line, column)
            onErrorOccurred: (message) => {
                documentErrorDialog.text = message
                documentErrorDialog.open()
            }
        }

        // Одна миникарта
        Minimap {
            id: minimap
            editor: editor
            anchors.top: editor.top
            anchors.right: vbar.left
            anchors.bottom: editor.bottom
            visible: detached.showMinimap && detached.hasDocument
            width: visible ? implicitWidth : 0
            z: 1
            background: Theme.editor
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

        EmptyState {
            anchors.fill: editor
            anchors.rightMargin: -vbar.width
            visible: manager.tabs.length === 0
            onActionRequested: (action) => {
                if (action === "file")
                    fileDialog.open()
            }
        }

        // Панель ассистента — справа вместо боковой панели главного окна
        AssistantPanel {
            id: assistantPanel
            z: 4
            assistant: assistant
            editor: editor
            visible: detached.assistantVisible
            anchors.top: editor.top
            anchors.right: editor.right
            anchors.bottom: editor.bottom
            width: 380
            onVisibleChanged: if (visible) activate()
            onInsertCode: (code) => {
                editor.replaceSelection(code)
                editor.forceActiveFocus()
            }
            onInsertComment: (row) => {
                assistant.insertComment(row, editor)
                editor.forceActiveFocus()
            }
            onLeave: {
                detached.assistantVisible = false
                editor.forceActiveFocus()
            }
        }

        FindBar {
            id: findBar
            editor: editor
            anchors.top: editor.top
            anchors.right: editor.right
            anchors.topMargin: 8
            anchors.rightMargin: 12 + editor.rightMargin
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
    }

    StatusBar {
        id: statusBar
        editor: editor
        lsp: lsp
        branch: "" // панель Git остаётся в главном окне
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

    CloseDialog {
        id: closeDialog
        onSaveRequested: (document) => manager.saveAndClose(document)
        onDiscardRequested: (document) => manager.closeDocument(document)
    }

    MessageDialog {
        id: documentErrorDialog
        title: qsTr("Ошибка файла")
    }

    FileDialog {
        id: fileDialog
        onAccepted: manager.open(selectedFile)
    }

    Shortcut {
        sequences: [StandardKey.Find]
        context: Qt.WindowShortcut
        onActivated: findBar.open()
    }
    Shortcut {
        sequences: ["Ctrl+G"]
        context: Qt.WindowShortcut
        onActivated: goToLine.open()
    }
}
