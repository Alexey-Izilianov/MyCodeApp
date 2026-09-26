import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import MyCodeApp

ApplicationWindow {
    id: window
    width: 1080
    height: 720
    visible: true
    color: theme.deepest
    font.family: "Segoe UI"
    title: manager.currentIndex >= 0
           ? manager.tabs[manager.currentIndex].name + " — MyCodeApp"
           : qsTr("MyCodeApp")

    // ── Фирменная палитра «Amber Ember» ──────────────────────────────
    // Глубокий сине-графитовый контур + один тёплый янтарный акцент.
    QtObject {
        id: theme
        readonly property color deepest: "#0d1017" // окно, статус-бар
        readonly property color chrome:  "#10141d" // тулбар, полоса табов
        readonly property color stage:   "#151823" // фон редактора (синхрон. с C++)
        readonly property color surface: "#1b2130" // активный таб, панели
        readonly property color hover:   "#171d2a"
        readonly property color line:    "#222a3c" // разделители
        readonly property color accent:  "#ffb454" // янтарь
        readonly property color accent2: "#ff8a3d" // градиент к нему
        readonly property color text:    "#e6eaf2"
        readonly property color muted:   "#98a1b3"
        readonly property color faint:   "#5f6879"
    }

    property bool _uiDebugSearch: false // временный хук для скриншота поиска

    function openSearch() {
        searchPanel.visible = true
        searchInput.focus = true
        searchInput.selectAll()
    }

    function requestClose(index) {
        if (manager.isDirty(index)) {
            closeConfirm.pendingIndex = index
            closeConfirm.open()
        } else {
            manager.close(index)
        }
    }

    Component.onCompleted: if (_uiDebugSearch) openSearch()

    // Круглая иконочная кнопка навигации в поисковой панели
    component SearchNavButton: Rectangle {
        property string glyph: ""
        property string tip: ""
        signal activated()
        width: 24
        height: 24
        radius: 6
        color: navMouse.containsMouse ? theme.hover : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }
        Label {
            anchors.centerIn: parent
            text: parent.glyph
            color: navMouse.containsMouse ? theme.text : theme.muted
            font.pixelSize: 13
        }
        MouseArea {
            id: navMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.activated()
        }
        ToolTip.visible: navMouse.containsMouse && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 400
    }

    DocumentManager {
        id: manager
        objectName: "manager"
        onErrorOccurred: (message) => console.log("ERROR:", message)
    }

    EditorView {
        id: editor
        objectName: "editor"
        document: manager.currentDocument
        focus: true
        anchors.top: searchPanel.visible ? searchPanel.bottom : tabStrip.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusbar.top

        onErrorOccurred: (message) => console.log("ERROR:", message)
        onFindRequested: window.openSearch()
    }

    WheelHandler {
        onWheel: (event) => {
                     editor.scrollY = Math.max(0, editor.scrollY -
                     event.angleDelta.y * 0.5)
                 }
    }

    DropArea {
        anchors.fill: parent
        onEntered: (drag) => drag.accept(drag.hasUrls)
        onDropped: (drop) => {
            for (const url of drop.urls)
                manager.open(url)
        }
    }

    FileDialog {
        id: fileDialog
        onAccepted: manager.open(selectedFile)
    }

    Dialog {
        id: closeConfirm
        property int pendingIndex: -1
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Несохранённые изменения")
        width: 320
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: manager.close(pendingIndex)
        Label {
            text: qsTr("Документ изменён. Закрыть без сохранения?")
            color: theme.text
        }
    }

    // ── Тулбар ───────────────────────────────────────────────────────
    Rectangle {
        id: toolbar
        height: 46
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        color: theme.chrome

        Rectangle { // нижний разделитель
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: theme.line
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 12
            spacing: 10

            // Фирменный знак
            Rectangle {
                width: 24
                height: 24
                radius: 7
                anchors.verticalCenter: parent.verticalCenter
                gradient: Gradient {
                    GradientStop { position: 0; color: theme.accent }
                    GradientStop { position: 1; color: theme.accent2 }
                }
                Label {
                    anchors.centerIn: parent
                    text: "M"
                    color: "#241505"
                    font.pixelSize: 14
                    font.bold: true
                }
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "MyCodeApp"
                color: theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                rightPadding: 6
            }
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 12
            spacing: 8

            Repeater {
                model: [
                    { label: qsTr("Открыть"), primary: true,  act: "open"   },
                    { label: qsTr("Сохранить"), primary: false, act: "save"   },
                    { label: qsTr("Закрыть"), primary: false, act: "close"  },
                    { label: qsTr("Найти"), primary: false, act: "find"   }
                ]
                delegate: Rectangle {
                    id: tb
                    required property var modelData
                    readonly property bool hov: btnMouse.containsMouse
                    readonly property bool on: manager.currentIndex >= 0
                                               || modelData.act === "open"
                    width: btnLabel.implicitWidth + 28
                    height: 28
                    radius: 7
                    anchors.verticalCenter: parent.verticalCenter
                    color: modelData.primary ? (hov ? theme.accent2 : theme.accent)
                           : (hov ? theme.hover : "transparent")
                    border.width: modelData.primary ? 0 : 1
                    border.color: hov ? theme.line : "#1a2130"
                    opacity: on ? 1 : 0.4

                    Behavior on color { ColorAnimation { duration: 120 } }
                    Behavior on opacity { NumberAnimation { duration: 120 } }

                    Label {
                        id: btnLabel
                        anchors.centerIn: parent
                        text: parent.modelData.label
                        color: parent.modelData.primary ? "#241505" : theme.text
                        font.pixelSize: 12
                        font.weight: parent.modelData.primary
                                     ? Font.DemiBold : Font.Normal
                    }
                    MouseArea {
                        id: btnMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        enabled: tb.on
                        onClicked: {
                            if (modelData.act === "open")   fileDialog.open()
                            if (modelData.act === "save")   editor.save()
                            if (modelData.act === "close")  window.requestClose(manager.currentIndex)
                            if (modelData.act === "find")   editor.findRequested()
                        }
                    }
                }
            }
        }
    }

    // ── Полоса табов ─────────────────────────────────────────────────
    Rectangle {
        id: tabStrip
        height: 38
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        color: theme.deepest

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: theme.line
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 14
            visible: manager.tabs.length === 0
            text: qsTr("Нет открытых файлов")
            color: theme.faint
            font.pixelSize: 12
            font.italic: true
        }

        Flickable {
            anchors.fill: parent
            clip: true
            contentWidth: tabRow.width
            interactive: tabRow.width > width
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: tabRow
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                spacing: 2
                leftPadding: 6

                Repeater {
                    model: manager.tabs

                    Rectangle {
                        id: tabRoot
                        required property int index
                        required property var modelData
                        readonly property bool active: index === manager.currentIndex
                        readonly property bool hov: tabMouse.containsMouse

                        width: Math.min(220,
                                        Math.max(128,
                                                 tabMetrics.width + 66))
                        height: 34
                        anchors.top: parent.top
                        radius: 8
                        // скруглить только верх: маскируем низ полосой
                        color: active ? theme.surface
                              : (hov ? theme.hover : "transparent")

                        Behavior on color { ColorAnimation { duration: 130 } }

                        // нижняя маска, чтобы радиус был только сверху
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 8
                            color: parent.color
                            Behavior on color { ColorAnimation { duration: 130 } }
                        }

                        TextMetrics {
                            id: tabMetrics
                            font.pixelSize: 12
                            text: tabRoot.modelData.name
                        }

                        // акцентная полоска активного таба (градиент)
                        Rectangle {
                            visible: tabRoot.active
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.topMargin: 2
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            height: 2
                            radius: 1
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: theme.accent2 }
                                GradientStop { position: 1; color: theme.accent }
                            }
                        }

                        // маркер типа файла
                        Rectangle {
                            id: typeBadge
                            width: 16
                            height: 16
                            radius: 5
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            color: window.extColor(modelData.path)
                            opacity: tabRoot.active ? 1 : 0.55
                            Label {
                                anchors.centerIn: parent
                                text: window.extLetter(modelData.path)
                                color: "#10141d"
                                font.pixelSize: 9
                                font.bold: true
                            }
                        }

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: typeBadge.right
                            anchors.leftMargin: 8
                            anchors.right: dirtyDot.visible ? dirtyDot.left
                                                            : closeBtn.left
                            anchors.rightMargin: 4
                            text: modelData.name
                            elide: Text.ElideMiddle
                            color: tabRoot.active ? theme.text : theme.muted
                            font.pixelSize: 12
                            font.weight: tabRoot.active ? Font.DemiBold
                                                        : Font.Normal
                            Behavior on color { ColorAnimation { duration: 130 } }
                        }

                        // индикатор несохранённых изменений
                        Rectangle {
                            id: dirtyDot
                            visible: modelData.dirty && !closeBtn.hov
                            width: 7
                            height: 7
                            radius: 3.5
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            anchors.rightMargin: 26
                            color: theme.accent
                        }

                        // крестик закрытия
                        Rectangle {
                            id: closeBtn
                            readonly property bool hov: closeMouse.containsMouse
                            visible: tabRoot.active || tabRoot.hov
                            width: 18
                            height: 18
                            radius: 5
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            color: closeMouse.pressed ? theme.surface
                                  : (hov ? theme.hover : "transparent")

                            Label {
                                anchors.centerIn: parent
                                text: "✕"
                                color: closeBtn.hov ? theme.text : theme.faint
                                font.pixelSize: 10
                            }
                            MouseArea {
                                id: closeMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: window.requestClose(tabRoot.index)
                            }
                        }

                        MouseArea {
                            id: tabMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: manager.activate(tabRoot.index)
                        }
                    }
                }
            }
        }
    }

    // ── Поисковая панель (строгая строка между табами и редактором) ──
    Rectangle {
        id: searchPanel
        visible: false
        height: 46
        anchors.top: tabStrip.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        color: theme.chrome
        z: 5

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: theme.line
        }

        function close() {
            visible = false
            editor.clearSearch()
            editor.focus = true
        }

        Rectangle {
            id: searchCard
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 12
            width: searchRow.width + 24
            height: 34
            radius: 9
            color: theme.deepest
            border.width: 1
            border.color: searchInput.activeFocus
                          ? theme.accent : theme.line

            Behavior on border.color { ColorAnimation { duration: 150 } }

            Row {
                id: searchRow
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 12
                spacing: 4

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "⌕"
                    color: theme.accent
                    font.pixelSize: 16
                    rightPadding: 4
                }

                TextField {
                    id: searchInput
                    width: 200
                    anchors.verticalCenter: parent.verticalCenter
                    placeholderText: qsTr("Поиск по файлу…")
                    placeholderTextColor: theme.faint
                    color: theme.text
                    font.pixelSize: 12
                    selectByMouse: true
                    background: Rectangle { color: "transparent" }
                    verticalAlignment: TextInput.AlignVCenter

                    Timer {
                        id: searchDebounce
                        interval: 250
                        onTriggered: editor.find(searchInput.text,
                                                 caseToggle.checked)
                    }
                    onTextChanged: searchDebounce.restart()
                    onAccepted: editor.findNext()

                    Keys.onEscapePressed: searchPanel.close()
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_F3) {
                            if (event.modifiers & Qt.ShiftModifier)
                                editor.findPrev()
                            else
                                editor.findNext()
                            event.accepted = true
                        }
                    }
                }

                // счётчик совпадений
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(28, searchCount.implicitWidth + 14)
                    height: 20
                    radius: 10
                    color: theme.surface
                    Label {
                        id: searchCount
                        anchors.centerIn: parent
                        color: theme.muted
                        text: qsTr("-/-")
                        font.pixelSize: 11
                    }
                }

                // переключатель регистра
                Rectangle {
                    id: caseToggle
                    property bool checked: false
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 24
                    radius: 6
                    color: caseToggle.checked ? theme.accent
                          : (caseHover.containsMouse ? theme.hover : "transparent")
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Label {
                        anchors.centerIn: parent
                        text: "Aa"
                        color: caseToggle.checked ? "#241505" : theme.muted
                        font.pixelSize: 11
                        font.bold: caseToggle.checked
                    }
                    MouseArea {
                        id: caseHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            caseToggle.checked = !caseToggle.checked
                            searchDebounce.restart()
                        }
                    }
                    ToolTip.visible: caseHover.containsMouse
                    ToolTip.text: qsTr("Учитывать регистр")
                    ToolTip.delay: 400
                }

                // навигация и закрытие
                SearchNavButton {
                    glyph: "↑"
                    tip: qsTr("Предыдущее (Shift+F3)")
                    onActivated: editor.findPrev()
                }
                SearchNavButton {
                    glyph: "↓"
                    tip: qsTr("Следующее (F3)")
                    onActivated: editor.findNext()
                }
                Rectangle {
                    width: 1
                    height: 18
                    anchors.verticalCenter: parent.verticalCenter
                    color: theme.line
                }
                SearchNavButton {
                    glyph: "✕"
                    onActivated: searchPanel.close()
                }
            }
        }

        Connections {
            target: editor
            function onSearchUpdated(current, total) {
                searchCount.text = total === 0 ? qsTr("нет")
                                               : (current === 0
                                                  ? "?/" + total
                                                  : current + "/" + total)
            }
        }
    }

    // ── Пустое состояние (нет открытых файлов) ───────────────────────
    Item {
        anchors.top: tabStrip.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusbar.top
        visible: manager.tabs.length === 0
        z: 4

        Column {
            anchors.centerIn: parent
            spacing: 14

            Rectangle {
                width: 56
                height: 56
                radius: 16
                anchors.horizontalCenter: parent.horizontalCenter
                gradient: Gradient {
                    GradientStop { position: 0; color: theme.accent }
                    GradientStop { position: 1; color: theme.accent2 }
                }
                Label {
                    anchors.centerIn: parent
                    text: "M"
                    color: "#241505"
                    font.pixelSize: 28
                    font.bold: true
                }
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "MyCodeApp"
                color: theme.text
                font.pixelSize: 22
                font.weight: Font.DemiBold
                font.letterSpacing: 0.5
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Перетащите файл в окно — или")
                color: theme.muted
                font.pixelSize: 12
            }
            Rectangle {
                width: openLabel.implicitWidth + 36
                height: 32
                radius: 9
                anchors.horizontalCenter: parent.horizontalCenter
                color: emptyBtn.containsMouse ? theme.accent2 : theme.accent
                Behavior on color { ColorAnimation { duration: 120 } }
                Label {
                    id: openLabel
                    anchors.centerIn: parent
                    text: qsTr("Открыть файл")
                    color: "#241505"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    id: emptyBtn
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: fileDialog.open()
                }
            }
        }
    }

    // ── Оверлей загрузки больших файлов ──────────────────────────────
    Rectangle {
        id: loadOverlay
        visible: manager.loading
        anchors.fill: parent
        color: "#b30d1017"
        z: 10

        // мягкая тень карточки
        Rectangle {
            anchors.centerIn: loadCard
            width: loadCard.width + 24
            height: loadCard.height + 28
            radius: 22
            color: "#4d000000"
            visible: loadOverlay.visible
        }

        Rectangle {
            id: loadCard
            width: 380
            height: 150
            radius: 14
            anchors.centerIn: parent
            color: theme.surface
            border.width: 1
            border.color: theme.line

            Column {
                anchors.centerIn: parent
                spacing: 12

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 330
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    color: theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    text: manager.loading
                          ? qsTr("Загрузка: %1").arg(manager.loadingName)
                          : ""
                }

                // фирменный прогресс-бар
                Rectangle {
                    width: 330
                    height: 6
                    radius: 3
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: theme.deepest
                    Rectangle {
                        width: parent.width * manager.loadPercent / 100
                        height: parent.height
                        radius: 3
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: theme.accent2 }
                            GradientStop { position: 1; color: theme.accent }
                        }
                        Behavior on width { NumberAnimation { duration: 150 } }
                    }
                }

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: theme.muted
                    font.pixelSize: 12
                    text: manager.loadPercent + " %"
                }
            }
        }
    }

    // ── Статус-бар ───────────────────────────────────────────────────
    Rectangle {
        id: statusbar
        height: 26
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        color: theme.deepest

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: theme.line
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 12
            spacing: 8
            Rectangle {
                width: 6
                height: 6
                radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: editor.filePath === "" ? theme.faint : theme.accent
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                color: theme.muted
                font.pixelSize: 11
                text: editor.filePath === ""
                      ? qsTr("Файл не выбран")
                      : editor.filePath
            }
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 12
            spacing: 14

            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: editor.filePath !== ""
                color: theme.muted
                font.pixelSize: 11
                text: editor.encoding + " · " + editor.lineEnding
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: posLabel.implicitWidth + 16
                height: 18
                radius: 9
                color: theme.surface
                Label {
                    id: posLabel
                    anchors.centerIn: parent
                    color: theme.accent
                    font.pixelSize: 11
                    text: "Стр " + (editor.cursorLine + 1) + ", Стлб "
                          + (editor.cursorColumn + 1)
                }
            }
        }
    }

    // ── Маркеры типа файла ───────────────────────────────────────────
    function extColor(path) {
        const p = String(path).toLowerCase()
        if (p.endsWith(".h") || p.endsWith(".hpp") || p.endsWith(".hh"))
            return "#5fd4c0"
        if (p.endsWith(".cpp") || p.endsWith(".cc") || p.endsWith(".c"))
            return "#ffb454"
        if (p.endsWith(".py")) return "#7db8ff"
        if (p.endsWith(".js") || p.endsWith(".ts")) return "#e8d06c"
        if (p.endsWith(".json") || p.endsWith(".xml") || p.endsWith(".yaml"))
            return "#c79bf2"
        if (p.endsWith(".md") || p.endsWith(".txt")) return "#9aa5b8"
        return "#8b93a5"
    }

    function extLetter(path) {
        const p = String(path).toLowerCase()
        if (p.endsWith(".h") || p.endsWith(".hpp") || p.endsWith(".hh"))
            return "H"
        if (p.endsWith(".cpp") || p.endsWith(".cc") || p.endsWith(".c"))
            return "C"
        if (p.endsWith(".py")) return "P"
        if (p.endsWith(".js")) return "J"
        if (p.endsWith(".ts")) return "T"
        if (p.endsWith(".json")) return "{"
        if (p.endsWith(".md")) return "M"
        return "•"
    }
}