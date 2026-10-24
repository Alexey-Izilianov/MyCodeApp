import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Разговор с офлайн-ассистентом: ответы приходят потоком, под ответом — действия
// (копировать, вставить код или комментарий), внизу — поле вопроса
Item {
    id: panel
    required property Assistant assistant
    required property EditorView editor
    signal insertCode(string code)
    signal insertComment(int row)
    signal openSettings()
    signal leave()

    function focusInput() { input.forceActiveFocus() }
    function activate() {
        if (assistant.serverState === "" || assistant.serverState === "error")
            assistant.checkServer()
    }
    function send() {
        if (input.text.trim() === "")
            return
        assistant.ask(input.text, editor, attach.checked && hasSelection)
        input.text = ""
    }

    readonly property bool hasDocument: editor !== null && editor.document !== null
    // Выделение в редакторе можно приложить к вопросу
    readonly property bool hasSelection: hasDocument && editor.hasSelection

    // Секунды с начала ответа
    property int elapsed: 0
    readonly property bool busy: assistant.busy
    onBusyChanged: elapsed = 0
    Timer {
        interval: 1000
        repeat: true
        running: panel.busy
        onTriggered: panel.elapsed++
    }

    component LinkButton: AbstractButton {
        id: link
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        padding: 2
        contentItem: Label {
            text: link.text
            color: link.hovered ? Theme.text : Theme.muted
            font.pixelSize: Theme.smallFontSize
            font.underline: link.hovered
        }
        background: null
    }

    // Модель и состояние сервера
    Item {
        id: toolbar
        width: parent.width
        height: 28
        Rectangle {
            id: dot
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 6
            height: 6
            radius: 3
            color: panel.assistant.serverState === "ok" ? Theme.gitAdded
                 : panel.assistant.serverState === "error" ? Theme.error : Theme.faint
        }
        Label {
            anchors.left: dot.right
            anchors.leftMargin: 6
            anchors.right: buttons.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: panel.assistant.model
            color: Theme.muted
            font.pixelSize: Theme.smallFontSize
            elide: Text.ElideRight
        }
        Row {
            id: buttons
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            IconButton {
                iconName: "plus"
                tip: qsTr("Новый разговор")
                enabled: list.count > 0
                onClicked: panel.assistant.clear()
            }
            IconButton {
                iconName: "settings"
                tip: qsTr("Настройки ассистента")
                onClicked: panel.openSettings()
            }
        }
    }

    ListView {
        id: list
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: status.top
        clip: true
        spacing: 14
        topMargin: 4
        bottomMargin: 8
        boundsBehavior: Flickable.StopAtBounds
        model: panel.assistant.messages
        ScrollBar.vertical: ScrollBar {}

        // Пока читатель внизу — ответ прокручивается вместе с ним
        property bool follow: true
        onMovementEnded: follow = atYEnd
        onCountChanged: { follow = true; Qt.callLater(positionViewAtEnd) }
        onContentHeightChanged: if (follow) Qt.callLater(positionViewAtEnd)

        delegate: Column {
            id: message
            required property int index
            required property bool user
            required property string text
            required property string error
            required property string stats
            required property bool done
            required property bool comment
            required property bool hasCode
            x: 12
            width: list.width - 24
            spacing: 4

            Label {
                text: message.user ? qsTr("Вы") : qsTr("Ассистент")
                color: Theme.muted
                font.pixelSize: Theme.smallFontSize
                font.weight: Font.DemiBold
            }
            Rectangle {
                width: parent.width
                height: body.implicitHeight + (message.user ? 12 : 0)
                radius: 6
                color: message.user ? Theme.hover : "transparent"
                visible: message.text !== ""
                TextEdit {
                    id: body
                    x: message.user ? 8 : 0
                    y: message.user ? 6 : 0
                    width: parent.width - 2 * x
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: message.user ? TextEdit.PlainText : TextEdit.MarkdownText
                    text: message.text
                    color: Theme.text
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                }
            }
            Label {
                visible: message.error !== ""
                width: parent.width
                text: message.error
                color: Theme.error
                wrapMode: Text.Wrap
            }
            Flow {
                visible: !message.user && message.done && message.text !== ""
                width: parent.width
                spacing: 10
                LinkButton {
                    text: qsTr("Копировать")
                    onClicked: panel.assistant.copy(message.text)
                }
                LinkButton {
                    visible: message.comment
                    text: qsTr("Вставить комментарий")
                    onClicked: panel.insertComment(message.index)
                }
                LinkButton {
                    visible: message.hasCode && !message.comment
                    text: qsTr("Вставить код")
                    onClicked: panel.insertCode(panel.assistant.codeOf(message.index))
                }
                Label {
                    text: message.stats
                    color: Theme.faint
                    font.pixelSize: Theme.smallFontSize
                    topPadding: 2
                }
            }
        }
    }

    // Пустой разговор: что умеет ассистент и состояние сервера
    Column {
        visible: list.count === 0
        anchors.top: toolbar.bottom
        anchors.topMargin: 16
        x: 14
        width: parent.width - 28
        spacing: 10
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Спросите о коде или выберите действие. Модель работает на этом компьютере или на сервере в вашей сети — код в интернет не уходит.")
            color: Theme.muted
        }
        FlatButton {
            width: parent.width
            text: qsTr("Объяснить код")
            enabled: panel.hasDocument
            onClicked: panel.assistant.explainCode(panel.editor)
        }
        FlatButton {
            width: parent.width
            text: qsTr("Комментарий к выделенному")
            enabled: panel.hasSelection
            onClicked: panel.assistant.documentCode(panel.editor)
        }
        FlatButton {
            width: parent.width
            text: qsTr("Пересказать журнал")
            enabled: panel.hasDocument
            onClicked: panel.assistant.summarizeLog(panel.editor)
        }
        Item { width: 1; height: 4 }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            visible: text !== ""
            text: panel.assistant.serverText
            color: panel.assistant.serverState === "error" ? Theme.error : Theme.faint
            font.pixelSize: Theme.smallFontSize
        }
        Row {
            visible: panel.assistant.serverState === "error"
            spacing: 8
            FlatButton { text: qsTr("Проверить снова"); onClicked: panel.assistant.checkServer() }
            FlatButton { text: qsTr("Настройки"); onClicked: panel.openSettings() }
        }
    }

    // Что делает модель: на процессоре первый ответ бывает через полминуты и дольше
    Item {
        id: status
        anchors.bottom: composer.top
        anchors.bottomMargin: 6
        x: 14
        width: parent.width - 28
        height: panel.busy ? statusText.implicitHeight : 0
        visible: panel.busy
        Label {
            id: statusText
            anchors.left: parent.left
            anchors.right: stopLink.left
            anchors.rightMargin: 8
            wrapMode: Text.Wrap
            color: Theme.muted
            font.pixelSize: Theme.smallFontSize
            text: {
                const phase = panel.assistant.phase
                const seconds = panel.elapsed
                if (phase === "reading")
                    return qsTr("Готовлю текст для модели…")
                if (phase === "thinking")
                    return qsTr("Модель рассуждает… %1 с").arg(seconds)
                if (phase === "writing")
                    return qsTr("Модель пишет ответ… %1 с").arg(seconds)
                return seconds < 10 ? qsTr("Модель читает запрос… %1 с").arg(seconds)
                                    : qsTr("Модель читает запрос… %1 с (без видеокарты это бывает дольше минуты)").arg(seconds)
            }
        }
        LinkButton {
            id: stopLink
            anchors.right: parent.right
            anchors.top: parent.top
            text: qsTr("Стоп")
            onClicked: panel.assistant.stop()
        }
    }

    Column {
        id: composer
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        x: 8
        width: parent.width - 16
        spacing: 6

        Rectangle {
            width: parent.width
            height: Math.min(Math.max(56, input.implicitHeight), 140)
            radius: 4
            color: Theme.editor
            border.color: input.activeFocus ? Theme.accent : Theme.border
            ScrollView {
                anchors.fill: parent
                TextArea {
                    id: input
                    placeholderText: qsTr("Вопрос (Enter — отправить, Shift+Enter — новая строка)")
                    placeholderTextColor: Theme.faint
                    color: Theme.text
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.text
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    background: null
                    Keys.onEscapePressed: panel.leave()
                    Keys.onPressed: (event) => {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                && !(event.modifiers & Qt.ShiftModifier)) {
                            panel.send()
                            event.accepted = true
                        }
                    }
                }
            }
        }
        Item {
            width: parent.width
            height: 28
            AbstractButton {
                id: attach
                visible: panel.hasSelection
                anchors.verticalCenter: parent.verticalCenter
                checkable: true
                checked: true
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                contentItem: Row {
                    spacing: 6
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        radius: 2
                        color: attach.checked ? Theme.accent : "transparent"
                        border.color: attach.checked ? Theme.accent : Theme.faint
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("С выделенным кодом")
                        color: attach.hovered ? Theme.text : Theme.muted
                        font.pixelSize: Theme.smallFontSize
                    }
                }
                background: null
            }
            FlatButton {
                anchors.right: parent.right
                primary: !panel.busy
                text: panel.busy ? qsTr("Стоп") : qsTr("Отправить")
                onClicked: panel.busy ? panel.assistant.stop() : panel.send()
            }
        }
    }
}
