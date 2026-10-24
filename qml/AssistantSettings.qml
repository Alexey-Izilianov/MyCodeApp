import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Настройки ассистента: сервер модели, модель, размер контекста, проверка подключения
Flickable {
    id: page
    required property Assistant assistant

    function activate() { assistant.checkServer() }

    clip: true
    contentHeight: column.implicitHeight + 32
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    component Choice: AbstractButton {
        id: choice
        property bool current: false
        width: parent.width
        height: 30
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        contentItem: Row {
            spacing: 10
            leftPadding: 10
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 12; height: 12; radius: 6
                color: choice.current ? Theme.accent : "transparent"
                border.color: choice.current ? Theme.accent : Theme.faint
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: choice.text
                color: Theme.text
            }
        }
        background: Rectangle {
            radius: 4
            color: choice.hovered ? Theme.hover : "transparent"
        }
    }
    component Heading: Label {
        topPadding: 8
        color: Theme.text
        font.weight: Font.DemiBold
    }
    component Hint: Label {
        width: parent.width
        wrapMode: Text.Wrap
        color: Theme.faint
        font.pixelSize: Theme.smallFontSize
    }

    Column {
        id: column
        x: 16
        y: 16
        width: page.width - 32
        spacing: 8

        Heading { text: qsTr("Сервер модели"); topPadding: 0 }
        Choice {
            text: qsTr("Ollama")
            current: page.assistant.provider !== "openai"
            onClicked: page.assistant.provider = "ollama"
        }
        Choice {
            text: qsTr("OpenAI-совместимый сервер (llama.cpp, vLLM, LM Studio)")
            current: page.assistant.provider === "openai"
            onClicked: page.assistant.provider = "openai"
        }

        Heading { text: qsTr("Адрес") }
        SearchField {
            id: endpointField
            width: parent.width
            text: page.assistant.endpoint
            onEditingFinished: page.assistant.endpoint = text
        }

        Heading { text: qsTr("Модель") }
        Row {
            width: parent.width
            spacing: 8
            SearchField {
                id: modelField
                width: parent.width - pick.width - 8
                text: page.assistant.model
                onEditingFinished: page.assistant.model = text
            }
            FlatButton {
                id: pick
                text: qsTr("Выбрать")
                enabled: page.assistant.models.length > 0
                onClicked: modelMenu.popup(pick, 0, pick.height + 2)
            }
        }
        AppMenu {
            id: modelMenu
            Instantiator {
                model: page.assistant.models
                delegate: Action {
                    required property string modelData
                    text: modelData
                    onTriggered: {
                        page.assistant.model = modelData
                        page.assistant.checkServer()
                    }
                }
                onObjectAdded: (index, object) => modelMenu.insertAction(index, object)
                onObjectRemoved: (index, object) => modelMenu.removeAction(object)
            }
        }
        Hint {
            text: qsTr("Рекомендуемая модель — Qwen3-4B-Instruct (лицензия Apache 2.0). Установка в Ollama: ollama pull qwen3:4b-instruct")
        }

        Heading {
            visible: page.assistant.provider === "openai"
            text: qsTr("Ключ доступа")
        }
        SearchField {
            visible: page.assistant.provider === "openai"
            width: parent.width
            echoMode: TextInput.Password
            placeholderText: qsTr("Не нужен для локального сервера")
            text: page.assistant.apiKey
            onEditingFinished: page.assistant.apiKey = text
        }

        Heading { text: qsTr("Контекст модели") }
        Row {
            spacing: 8
            Repeater {
                model: [4096, 8192, 16384]
                FlatButton {
                    required property int modelData
                    text: qsTr("%1 тыс. токенов").arg(modelData / 1024)
                    primary: page.assistant.contextSize === modelData
                    onClicked: page.assistant.contextSize = modelData
                }
            }
        }
        Hint {
            text: qsTr("Больше контекст — длиннее фрагменты кода и журналов, но больше памяти: у Qwen3-4B около 3,1 ГБ при 4 тыс. и 3,8 ГБ при 8 тыс. токенов.")
        }

        Item { width: 1; height: 8 }
        Row {
            width: parent.width
            spacing: 10
            FlatButton {
                id: check
                text: qsTr("Проверить подключение")
                onClicked: {
                    // Поля применяются при потере фокуса — а кнопка фокус не забирает
                    page.assistant.endpoint = endpointField.text
                    page.assistant.model = modelField.text
                    page.assistant.checkServer()
                }
            }
            Label {
                anchors.verticalCenter: check.verticalCenter
                width: parent.width - check.width - 10
                wrapMode: Text.Wrap
                text: page.assistant.serverText
                color: page.assistant.serverState === "error" ? Theme.error
                     : page.assistant.serverState === "ok" ? Theme.text : Theme.muted
            }
        }
        Hint {
            text: qsTr("Без видеокарты ответ занимает от полуминуты до полутора минут. Модель можно запустить на более мощной машине в локальной сети и указать здесь её адрес.")
        }
    }
}
