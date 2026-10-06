import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import MyCodeApp

// Настройки: тема, горячие клавиши, плагины
Popup {
    id: dialog
    property string page: "appearance"
    property var plugins: null // PluginManager
    signal openFile(string path)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(760, parent.width - 80)
    height: Math.min(560, parent.height - 80)
    modal: true
    padding: 0
    closePolicy: capture.active ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: commandList.refresh()

    Overlay.modal: Rectangle { color: "#80000000" }
    background: Rectangle { color: Theme.chrome; border.color: Theme.border; radius: 8 }

    Column {
        id: pages
        x: 8
        y: 12
        width: 170
        spacing: 2
        Repeater {
            model: [{ id: "appearance", title: qsTr("Внешний вид") },
                    { id: "keys", title: qsTr("Горячие клавиши") },
                    { id: "plugins", title: qsTr("Плагины") }]
            AbstractButton {
                id: pageButton
                required property var modelData
                width: pages.width
                height: 30
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                contentItem: Label {
                    leftPadding: 10
                    text: pageButton.modelData.title
                    color: dialog.page === pageButton.modelData.id ? Theme.text : Theme.muted
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 4
                    color: dialog.page === pageButton.modelData.id ? Theme.pressed
                         : pageButton.hovered ? Theme.hover : "transparent"
                }
                onClicked: dialog.page = modelData.id
            }
        }
    }
    Rectangle {
        x: pages.x + pages.width + 8
        width: 1
        height: parent.height
        color: Theme.border
    }

    StackLayout {
        x: pages.x + pages.width + 9
        width: parent.width - x
        height: parent.height
        currentIndex: ["appearance", "keys", "plugins"].indexOf(dialog.page)

        // ── Внешний вид
        Item {
            Column {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8
                Label {
                    text: qsTr("Язык")
                    color: Theme.text
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: Localization.languages
                    AbstractButton {
                        id: languageRow
                        required property var modelData
                        readonly property bool current: modelData.code === Localization.language
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
                                color: languageRow.current ? Theme.accent : "transparent"
                                border.color: languageRow.current ? Theme.accent : Theme.faint
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                text: languageRow.modelData.name
                                color: Theme.text
                            }
                        }
                        background: Rectangle {
                            radius: 4
                            color: languageRow.hovered ? Theme.hover : "transparent"
                        }
                        onClicked: Localization.setLanguage(modelData.code)
                    }
                }
                Item { width: 1; height: 8 }
                Label {
                    text: qsTr("Тема")
                    color: Theme.text
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: ThemeManager.themes
                    AbstractButton {
                        id: themeRow
                        required property var modelData
                        readonly property bool current: modelData.id === ThemeManager.current
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
                                color: themeRow.current ? Theme.accent : "transparent"
                                border.color: themeRow.current ? Theme.accent : Theme.faint
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                text: Localization.language === "en" && themeRow.modelData.nameEn
                                      ? themeRow.modelData.nameEn : themeRow.modelData.name
                                color: Theme.text
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: themeRow.modelData.user
                                text: qsTr("своя")
                                color: Theme.faint
                                font.pixelSize: Theme.smallFontSize
                            }
                        }
                        background: Rectangle {
                            radius: 4
                            color: themeRow.hovered ? Theme.hover : "transparent"
                        }
                        onClicked: ThemeManager.setTheme(modelData.id)
                    }
                }
                Item { width: 1; height: 8 }
                Row {
                    spacing: 8
                    FlatButton {
                        text: qsTr("Создать свою тему")
                        onClicked: {
                            const path = ThemeManager.createUserTheme()
                            if (path !== "") {
                                dialog.close()
                                dialog.openFile(path)
                            }
                        }
                    }
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("Своя тема — копия текущей в JSON. Она откроется во вкладке: правьте цвета и сохраняйте, изменения видны сразу.")
                    color: Theme.faint
                    font.pixelSize: Theme.smallFontSize
                }
            }
        }

        // ── Горячие клавиши
        Item {
            id: keysPage
            SearchField {
                id: keyFilter
                x: 16
                y: 16
                width: parent.width - 32 - resetAll.width - 8
                placeholderText: qsTr("Команда или сочетание")
            }
            FlatButton {
                id: resetAll
                anchors.verticalCenter: keyFilter.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 16
                text: qsTr("Сбросить все")
                onClicked: Keymap.resetAll()
            }

            ListView {
                id: commandList
                property var all: []
                function refresh() { all = Keymap.commands }
                Connections {
                    target: Keymap
                    function onChanged() { commandList.refresh() }
                }

                anchors.top: keyFilter.bottom
                anchors.topMargin: 8
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: hint.top
                anchors.bottomMargin: 6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                model: {
                    const q = keyFilter.text.toLowerCase()
                    return q === "" ? all : all.filter(c => c.title.toLowerCase().includes(q)
                                                            || c.shortcut.toLowerCase().includes(q))
                }

                delegate: Rectangle {
                    id: commandRow
                    required property var modelData
                    readonly property bool capturing: capture.active && capture.commandId === modelData.id
                    width: commandList.width
                    height: 30
                    color: capturing ? Theme.pressed : rowHover.hovered ? Theme.hover : "transparent"
                    HoverHandler { id: rowHover }

                    Label {
                        x: 16
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 16 - keyCell.width - 60
                        text: commandRow.modelData.title
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    AbstractButton {
                        id: keyCell
                        anchors.right: resetButton.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: 200
                        height: 24
                        hoverEnabled: true
                        focusPolicy: Qt.NoFocus
                        contentItem: Label {
                            leftPadding: 8
                            text: commandRow.capturing ? qsTr("Нажмите сочетание…")
                                  : commandRow.modelData.shortcut !== "" ? commandRow.modelData.shortcut : "—"
                            color: commandRow.capturing ? Theme.accent
                                   : commandRow.modelData.custom ? Theme.text : Theme.muted
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        background: Rectangle {
                            radius: 4
                            color: "transparent"
                            border.color: commandRow.capturing ? Theme.accent
                                          : keyCell.hovered ? Theme.border : "transparent"
                        }
                        onClicked: capture.start(commandRow.modelData)
                    }
                    IconButton {
                        id: resetButton
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        visible: commandRow.modelData.custom
                        glyph: "↺"
                        tip: qsTr("По умолчанию: %1").arg(commandRow.modelData.defaultShortcut || "—")
                        onClicked: Keymap.reset(commandRow.modelData.id)
                    }
                }
            }

            Label {
                id: hint
                x: 16
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                width: parent.width - 32
                wrapMode: Text.Wrap
                text: capture.message !== "" ? capture.message
                      : qsTr("Щёлкните по сочетанию и нажмите новое. Esc — отмена, Backspace — убрать сочетание. Двойные сочетания (Ctrl+K, D) задаются в keybindings.json.")
                color: capture.message !== "" ? Theme.warning : Theme.faint
                font.pixelSize: Theme.smallFontSize
            }

            // Захват нажатия: пока он активен, сочетания меню не срабатывают
            Item {
                id: capture
                property bool active: false
                property string commandId: ""
                property string commandTitle: ""
                property string message: ""
                function start(command) {
                    commandId = command.id
                    commandTitle = command.title
                    message = ""
                    active = true
                    forceActiveFocus()
                }
                function stop() {
                    active = false
                    commandId = ""
                }
                Keys.onShortcutOverride: (event) => event.accepted = active
                Keys.onPressed: (event) => {
                    if (!active)
                        return
                    event.accepted = true
                    if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) {
                        stop()
                        return
                    }
                    if ((event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) && event.modifiers === Qt.NoModifier) {
                        Keymap.assign(commandId, "")
                        stop()
                        return
                    }
                    const shortcut = Keymap.shortcutFromKey(event.key, event.modifiers, event.nativeScanCode)
                    if (shortcut === "")
                        return // пока только модификаторы
                    const other = Keymap.assign(commandId, shortcut)
                    message = other !== "" ? qsTr("Сочетание %1 снято с команды «%2»").arg(shortcut).arg(other) : ""
                    stop()
                }
            }
        }

        // ── Плагины
        PluginsPage {
            plugins: dialog.plugins
        }
    }
}
