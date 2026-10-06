import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Панель Git: ветка, сообщение и коммит, индексированные и прочие изменения
Item {
    id: panel
    required property GitService git
    required property string rootPath
    signal openDiff(string path, bool staged)
    signal openFile(string path)
    signal leave()

    function focusMessage() { message.forceActiveFocus() }
    function commit() { panel.git.commit(message.text) }

    Connections {
        target: panel.git
        function onCommitted() { message.text = "" }
    }

    // Плоский список: заголовок раздела, его файлы, следующий раздел
    readonly property var rows: {
        const result = []
        const add = (title, staged, entries) => {
            result.push({ header: true, title: title, staged: staged, count: entries.length })
            for (const e of entries)
                result.push(Object.assign({ header: false, staged: staged }, e))
        }
        if (git.stagedChanges.length > 0)
            add(qsTr("Индексированные"), true, git.stagedChanges)
        add(qsTr("Изменения"), false, git.changes)
        return result
    }

    Column {
        id: header
        visible: panel.git.available
        width: parent.width
        topPadding: 8
        spacing: 6

        Row {
            x: 8
            width: parent.width - 16
            spacing: 6
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Ветка")
                color: Theme.muted
                font.pixelSize: Theme.smallFontSize
            }
            FlatButton {
                id: branchButton
                width: Math.min(implicitWidth, parent.width - x)
                implicitHeight: 24
                leftPadding: 8
                rightPadding: 8
                text: panel.git.branch
                onClicked: branchMenu.popup(branchButton, 0, branchButton.height + 2)
            }
        }

        Rectangle {
            x: 8
            width: parent.width - 16
            height: Math.min(Math.max(56, message.implicitHeight), 160)
            radius: 4
            color: Theme.editor
            border.color: message.activeFocus ? Theme.accent : Theme.border
            ScrollView {
                anchors.fill: parent
                TextArea {
                    id: message
                    placeholderText: qsTr("Сообщение коммита (Ctrl+Enter)")
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
                                && (event.modifiers & Qt.ControlModifier)) {
                            panel.commit()
                            event.accepted = true
                        }
                    }
                }
            }
        }

        FlatButton {
            x: 8
            width: parent.width - 16
            primary: panel.git.stagedChanges.length > 0
            enabled: !panel.git.busy
            text: panel.git.stagedChanges.length > 0
                  ? qsTr("Коммит (%1)").arg(panel.git.stagedChanges.length) : qsTr("Коммит")
            onClicked: panel.commit()
        }
    }

    ListView {
        id: list
        visible: panel.git.available
        anchors.top: header.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: panel.rows
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
            id: row
            required property var modelData
            readonly property bool header: modelData.header
            width: list.width
            height: header ? 26 : 24
            color: !header && hover.hovered ? Theme.hover : "transparent"
            HoverHandler { id: hover }

            Label {
                visible: row.header
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.title + "  " + row.modelData.count
                color: Theme.muted
                font.pixelSize: Theme.smallFontSize
                font.weight: Font.DemiBold
            }

            FileIcon {
                id: icon
                visible: !row.header
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                name: row.header ? "" : row.modelData.name
            }
            Label {
                id: name
                visible: !row.header
                anchors.left: icon.right
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, (actions.visible ? actions.x : statusLetter.x) - x - 8)
                text: row.header ? "" : row.modelData.name
                color: row.header ? Theme.text : Theme.gitColor(row.modelData.status)
                font.strikeout: !row.header && row.modelData.status === "D"
                elide: Text.ElideRight
            }
            Label {
                visible: !row.header
                anchors.left: name.right
                anchors.leftMargin: 8
                anchors.right: actions.visible ? actions.left : statusLetter.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: row.header ? "" : row.modelData.folder
                color: Theme.faint
                font.pixelSize: Theme.smallFontSize
                elide: Text.ElideLeft
            }

            TapHandler {
                enabled: !row.header
                onTapped: panel.openDiff(row.modelData.path, row.modelData.staged)
                onDoubleTapped: if (row.modelData.status !== "D") panel.openFile(row.modelData.path)
            }

            Label {
                id: statusLetter
                visible: !row.header && !actions.visible
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: row.header ? "" : row.modelData.status
                color: Theme.gitColor(text)
                font.pixelSize: Theme.smallFontSize
            }

            Row {
                id: actions
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                // Кнопки видны при наведении; у заголовка — всегда
                visible: row.header ? row.modelData.count > 0 : hover.hovered
                IconButton {
                    visible: !row.header && row.modelData.status !== "D"
                    width: 22; height: 22
                    glyph: "↗"
                    tip: qsTr("Открыть файл")
                    onClicked: panel.openFile(row.modelData.path)
                }
                IconButton {
                    visible: !row.modelData.staged
                    width: 22; height: 22
                    glyph: "↺"
                    tip: row.header ? qsTr("Отменить все изменения") : qsTr("Отменить изменения")
                    onClicked: row.header ? discardDialog.ask(panel.git.changes)
                                          : discardDialog.ask([row.modelData])
                }
                IconButton {
                    width: 22; height: 22
                    glyph: row.modelData.staged ? "−" : "+"
                    tip: row.modelData.staged
                         ? (row.header ? qsTr("Убрать всё из индекса") : qsTr("Убрать из индекса"))
                         : (row.header ? qsTr("Добавить всё в индекс") : qsTr("Добавить в индекс"))
                    onClicked: {
                        if (row.header)
                            row.modelData.staged ? panel.git.unstageAll() : panel.git.stageAll()
                        else
                            row.modelData.staged ? panel.git.unstage([row.modelData.path])
                                                 : panel.git.stage([row.modelData.path])
                    }
                }
            }
        }
    }

    Column {
        visible: !panel.git.available
        anchors.centerIn: parent
        width: parent.width - 32
        spacing: 12
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: panel.rootPath === "" ? qsTr("Папка не открыта")
                                        : qsTr("Папка не является репозиторием Git")
            color: Theme.muted
        }
        FlatButton {
            visible: panel.rootPath !== ""
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Создать репозиторий")
            onClicked: panel.git.initRepository()
        }
    }

    AppMenu {
        id: branchMenu
        Instantiator {
            model: panel.git.branches
            delegate: Action {
                required property string modelData
                text: modelData === panel.git.branch ? modelData + qsTr("  (текущая)") : modelData
                onTriggered: panel.git.checkout(modelData)
            }
            onObjectAdded: (index, object) => branchMenu.insertAction(index, object)
            onObjectRemoved: (index, object) => branchMenu.removeAction(object)
        }
        AppMenuSeparator {}
        Action {
            text: qsTr("Новая ветка…")
            onTriggered: newBranch.open()
        }
    }

    Popup {
        id: newBranch
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 360
        modal: true
        padding: 16
        onOpened: { branchName.text = ""; branchName.forceActiveFocus() }
        Overlay.modal: Rectangle { color: "#80000000" }
        background: Rectangle { color: Theme.chrome; border.color: Theme.border; radius: 8 }
        contentItem: Column {
            spacing: 10
            Label {
                text: qsTr("Новая ветка от «%1»").arg(panel.git.branch)
                color: Theme.text
                font.weight: Font.DemiBold
            }
            SearchField {
                id: branchName
                width: parent.width
                placeholderText: qsTr("Имя ветки")
                onAccepted: { panel.git.createBranch(text); newBranch.close() }
            }
        }
    }

    Popup {
        id: discardDialog
        property var entries: []
        function ask(list) {
            entries = list
            open()
        }
        readonly property bool hasNew: entries.some(e => e.status === "U")

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        modal: true
        padding: 20
        closePolicy: Popup.CloseOnEscape
        Overlay.modal: Rectangle { color: "#80000000" }
        background: Rectangle { color: Theme.chrome; border.color: Theme.border; radius: 8 }

        contentItem: Column {
            spacing: 8
            Label {
                width: parent.width
                text: discardDialog.entries.length === 1
                      ? qsTr("Отменить изменения в «%1»?").arg(discardDialog.entries[0].name)
                      : qsTr("Отменить изменения в файлах: %1?").arg(discardDialog.entries.length)
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideMiddle
            }
            Label {
                width: parent.width
                text: discardDialog.hasNew
                      ? qsTr("Новые файлы будут удалены. Действие нельзя отменить.")
                      : qsTr("Файлы вернутся к версии из индекса. Действие нельзя отменить.")
                color: Theme.muted
                wrapMode: Text.Wrap
            }
            Item { width: 1; height: 12 }
            Row {
                anchors.right: parent.right
                spacing: 8
                FlatButton {
                    text: qsTr("Отмена")
                    onClicked: discardDialog.close()
                }
                FlatButton {
                    primary: true
                    text: qsTr("Отменить изменения")
                    onClicked: {
                        discardDialog.close()
                        panel.git.discard(discardDialog.entries.map(e => e.path))
                    }
                }
            }
        }
    }
}
