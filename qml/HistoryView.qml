import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// История поверх редактора: граф коммитов всех веток, внизу — сообщение и файлы
// выбранного коммита. Щелчок по файлу — его изменения в коммите, ПКМ по коммиту — действия
Rectangle {
    id: view
    required property GitService git
    signal closed()
    signal openCommitFile(string id, var entry)

    readonly property HistoryModel history: git.history
    readonly property string selectedId: commits.currentItem ? commits.currentItem.commitId : ""
    property var files: []

    readonly property int rowHeight: 26
    readonly property int laneWidth: 14
    readonly property int graphWidth: Math.min(view.history.maxLanes, 16) * laneWidth + 10
    // Приглушённые цвета дорожек: читаются на тёмной и светлой подложке
    readonly property var laneColors: ["#4f8fd6", "#c9883a", "#5aa66a", "#b062ab", "#3fa3a3", "#c4605a", "#8a7fd0", "#9a9a3c"]
    function laneColor(index) { return laneColors[index % laneColors.length] }

    function open() {
        visible = true
        git.loadHistory()
        commits.forceActiveFocus()
    }
    function close() {
        if (!visible)
            return
        visible = false
        closed()
    }
    function focusList() { commits.forceActiveFocus() }
    function findNext() {
        const row = history.find(search.text, commits.currentIndex + 1)
        if (row < 0)
            return
        commits.currentIndex = row
        commits.positionViewAtIndex(row, ListView.Contain)
    }
    function copyText(text) {
        clipboard.text = text
        clipboard.selectAll()
        clipboard.copy()
    }

    visible: false
    color: Theme.editor

    onSelectedIdChanged: {
        files = []
        if (selectedId !== "")
            git.loadCommitFiles(selectedId)
    }
    Connections {
        target: view.git
        function onCommitFilesReady(id, files) {
            if (id === view.selectedId)
                view.files = files
        }
    }
    Connections {
        target: view.history
        property string keep: "" // выбранный коммит переживает перечитывание истории
        function onModelAboutToBeReset() { keep = view.selectedId }
        function onModelReset() {
            const row = keep === "" ? -1 : view.history.find(keep, 0)
            commits.currentIndex = row >= 0 ? row : (view.history.count > 0 ? 0 : -1)
        }
    }

    TextEdit { id: clipboard; visible: false }

    Rectangle {
        id: bar
        width: parent.width
        height: 32
        color: Theme.chrome
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }

        Label {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: search.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: view.git.available
                  ? qsTr("История  ·  %1  ·  ветка %2").arg(view.git.kind).arg(view.git.branch)
                  : qsTr("История")
            color: Theme.text
            elide: Text.ElideRight
        }
        SearchField {
            id: search
            anchors.right: buttons.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 240
            placeholderText: qsTr("Сообщение, автор или хэш (Enter)")
            onAccepted: view.findNext()
            Keys.onEscapePressed: commits.forceActiveFocus()
        }
        Row {
            id: buttons
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            FlatButton {
                implicitHeight: 24
                enabled: view.git.available && !view.git.historyLoading
                text: qsTr("Обновить")
                onClicked: view.git.loadHistory()
            }
            IconButton { tip: qsTr("Закрыть (Esc)"); onClicked: view.close() }
        }
    }

    ListView {
        id: commits
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: details.top
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: view.history
        highlightMoveDuration: 0
        ScrollBar.vertical: ScrollBar {}
        Keys.onEscapePressed: view.close()
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_F && (event.modifiers & Qt.ControlModifier)) {
                search.forceActiveFocus()
                search.selectAll()
                event.accepted = true
            }
        }

        delegate: Rectangle {
            id: row
            required property int index
            required property string commitId
            required property string shortId
            required property string summary
            required property string message
            required property string author
            required property string email
            required property string date
            required property string branch
            required property var refs
            required property var graph
            readonly property bool selected: ListView.isCurrentItem
            readonly property bool head: refs.some(r => r.current)

            width: commits.width
            height: view.rowHeight
            color: selected ? Theme.selection : rowHover.hovered ? Theme.hover : "transparent"
            HoverHandler { id: rowHover }
            TapHandler {
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onTapped: (point, button) => {
                    commits.currentIndex = row.index
                    commits.forceActiveFocus()
                    if (button === Qt.RightButton)
                        commitMenu.popup()
                }
            }

            Canvas {
                id: graphCanvas
                width: view.graphWidth
                height: parent.height
                readonly property var graph: row.graph
                readonly property bool head: row.head
                onGraphChanged: requestPaint()
                onHeadChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const x = lane => 9 + lane * view.laneWidth
                    const mid = height / 2
                    const edges = graph.edges
                    ctx.lineWidth = 1.6
                    for (let i = 0; i < edges.length; i += 4) {
                        const from = edges[i], to = edges[i + 1], kind = edges[i + 3]
                        ctx.strokeStyle = view.laneColor(edges[i + 2])
                        ctx.beginPath()
                        if (kind === 0) {        // мимо
                            ctx.moveTo(x(from), 0)
                            ctx.bezierCurveTo(x(from), mid, x(to), mid, x(to), height)
                        } else if (kind === 1) { // сверху в точку
                            ctx.moveTo(x(from), 0)
                            ctx.bezierCurveTo(x(from), mid / 2, x(graph.lane), mid / 2, x(graph.lane), mid)
                        } else {                 // из точки к родителю
                            ctx.moveTo(x(graph.lane), mid)
                            ctx.bezierCurveTo(x(graph.lane), mid * 1.5, x(to), mid * 1.5, x(to), height)
                        }
                        ctx.stroke()
                    }
                    // Точка коммита; рабочая копия — кольцом
                    ctx.beginPath()
                    ctx.arc(x(graph.lane), mid, head ? 4.5 : 4, 0, 2 * Math.PI)
                    ctx.fillStyle = head ? Theme.editor : view.laneColor(graph.color)
                    ctx.fill()
                    if (head) {
                        ctx.lineWidth = 2
                        ctx.strokeStyle = view.laneColor(graph.color)
                        ctx.stroke()
                    }
                }
            }

            Row {
                id: refLabels
                anchors.left: graphCanvas.right
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, (authorLabel.visible ? authorLabel.x : dateLabel.x) - x - 60)
                clip: true
                spacing: 4
                rightPadding: row.refs.length > 0 ? 6 : 0
                Repeater {
                    model: row.refs
                    delegate: Rectangle {
                        required property var modelData
                        width: refText.implicitWidth + 10
                        height: 18
                        radius: 3
                        color: "transparent"
                        border.color: modelData.current ? Theme.accent : Theme.border
                        Label {
                            id: refText
                            anchors.centerIn: parent
                            text: modelData.name
                            color: modelData.kind === "remote" || modelData.kind === "tag" ? Theme.muted : Theme.text
                            font.pixelSize: Theme.smallFontSize - 1
                            font.italic: modelData.kind === "tag"
                            font.weight: modelData.current ? Font.DemiBold : Font.Normal
                        }
                    }
                }
            }
            Label {
                anchors.left: refLabels.right
                anchors.right: authorLabel.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: row.summary
                color: Theme.text
                elide: Text.ElideRight
            }
            Label {
                id: authorLabel
                anchors.right: dateLabel.left
                anchors.rightMargin: visible ? 12 : 0
                anchors.verticalCenter: parent.verticalCenter
                visible: commits.width >= 720 // в узком окне место отдаётся сообщению
                width: visible ? 150 : 0
                text: row.author
                color: Theme.muted
                font.pixelSize: Theme.smallFontSize
                elide: Text.ElideRight
            }
            Label {
                id: dateLabel
                anchors.right: hashLabel.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 110
                text: row.date
                color: Theme.muted
                font.pixelSize: Theme.smallFontSize
            }
            Label {
                id: hashLabel
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: 64
                text: row.shortId
                color: Theme.faint
                font.family: Theme.monoFamily
                font.pixelSize: Theme.smallFontSize
            }
        }

        Label {
            anchors.centerIn: parent
            visible: commits.count === 0
            text: !view.git.available ? qsTr("Папка проекта не под контролем версий")
                  : view.git.historyLoading ? qsTr("Загрузка истории…") : qsTr("Коммитов пока нет")
            color: Theme.muted
        }
    }

    // Подробности выбранного коммита; высота — перетаскиванием верхней границы
    Rectangle {
        id: details
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.min(Math.max(120, detailsHeight), view.height - bar.height - 80)
        property real detailsHeight: 220
        visible: commits.currentItem !== null
        color: Theme.editor

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }
        MouseArea {
            width: parent.width
            height: 5
            cursorShape: Qt.SizeVerCursor
            property real startY
            onPressed: (mouse) => startY = mouse.y
            onPositionChanged: (mouse) => {
                if (pressed)
                    details.detailsHeight = details.height - (mouse.y - startY)
            }
        }

        ScrollView {
            id: messageView
            anchors.top: parent.top
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            width: parent.width * 0.5 - 18
            clip: true
            Column {
                width: messageView.availableWidth
                spacing: 6
                TextEdit {
                    width: parent.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    text: commits.currentItem ? commits.currentItem.message : ""
                    color: Theme.text
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: {
                        const c = commits.currentItem
                        if (!c)
                            return ""
                        let line = c.author + (c.email !== "" ? " <" + c.email + ">" : "") + "  ·  " + c.date
                        if (c.branch !== "")
                            line += "  ·  " + qsTr("ветка %1").arg(c.branch)
                        return line
                    }
                    color: Theme.muted
                    font.pixelSize: Theme.smallFontSize
                }
                TextEdit {
                    width: parent.width
                    readOnly: true
                    selectByMouse: true
                    text: view.selectedId
                    color: Theme.faint
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.text
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.smallFontSize
                }
            }
        }

        Rectangle {
            id: divider
            anchors.left: messageView.right
            anchors.leftMargin: 12
            anchors.top: parent.top
            anchors.topMargin: 8
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            width: 1
            color: Theme.border
        }

        ListView {
            id: fileList
            anchors.left: divider.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 6
            anchors.bottom: parent.bottom
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: view.files
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: fileRow
                required property var modelData
                width: fileList.width
                height: 24
                color: fileHover.hovered ? Theme.hover : "transparent"
                HoverHandler { id: fileHover }
                TapHandler { onTapped: view.openCommitFile(view.selectedId, fileRow.modelData) }

                FileIcon {
                    id: fileIcon
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    name: fileRow.modelData.name
                }
                Label {
                    id: fileName
                    anchors.left: fileIcon.right
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, fileStatus.x - x - 8)
                    text: fileRow.modelData.name
                    color: Theme.gitColor(fileRow.modelData.status)
                    font.strikeout: fileRow.modelData.status === "D"
                    elide: Text.ElideRight
                }
                Label {
                    anchors.left: fileName.right
                    anchors.leftMargin: 8
                    anchors.right: fileStatus.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: fileRow.modelData.folder
                    color: Theme.faint
                    font.pixelSize: Theme.smallFontSize
                    elide: Text.ElideLeft
                }
                Label {
                    id: fileStatus
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: fileRow.modelData.status
                    color: Theme.gitColor(text)
                    font.pixelSize: Theme.smallFontSize
                }
            }
        }
    }

    AppMenu {
        id: commitMenu
        Action {
            text: qsTr("Переключиться на этот коммит")
            onTriggered: view.git.checkoutCommit(view.selectedId)
        }
        Action {
            text: qsTr("Новая ветка отсюда…")
            onTriggered: newBranch.open()
        }
        AppMenuSeparator {}
        Action {
            text: qsTr("Копировать хэш")
            onTriggered: view.copyText(view.selectedId)
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
                text: qsTr("Новая ветка от коммита %1").arg(view.selectedId.slice(0, 8))
                color: Theme.text
                font.weight: Font.DemiBold
            }
            SearchField {
                id: branchName
                width: parent.width
                placeholderText: qsTr("Имя ветки")
                onAccepted: { view.git.createBranchAt(text, view.selectedId); newBranch.close() }
            }
        }
    }
}
