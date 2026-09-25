import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import MyCodeApp

ApplicationWindow {
    id: window
    width: 1000
    height: 700
    visible: true
    title: manager.currentIndex >= 0
           ? manager.tabs[manager.currentIndex].name + " — MyCodeApp"
           : qsTr("MyCodeApp (M1)")

    function requestClose(index) {
        if (manager.isDirty(index)) {
            closeConfirm.pendingIndex = index
            closeConfirm.open()
        } else {
            manager.close(index)
        }
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
        anchors.top: tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusbar.top

        onErrorOccurred: (message) => console.log("ERROR:", message)
        onFindRequested: {
            searchbar.visible = true
            searchInput.focus = true
            searchInput.selectAll()
        }
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
        }
    }

    Rectangle {
        id: toolbar
        height: 36
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        color: "#2d2d2d"

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 8
            spacing: 12

            Button {
                text: qsTr("Открыть")
                onClicked: fileDialog.open()
            }
            Button {
                text: qsTr("Сохранить")
                onClicked: editor.save()
            }
            Button {
                text: qsTr("Закрыть")
                enabled: manager.currentIndex >= 0
                onClicked: window.requestClose(manager.currentIndex)
            }
            Button {
                text: qsTr("Найти")
                enabled: manager.currentIndex >= 0
                onClicked: editor.findRequested()
            }
        }
    }

    Rectangle {
        id: searchbar
        visible: false
        height: 34
        anchors.top: toolbar.bottom
        anchors.right: parent.right
        anchors.rightMargin: 8
        width: searchRow.implicitWidth + 16
        color: "#2d2d2d"
        border.color: "#555555"

        function close() {
            visible = false
            editor.clearSearch()
            editor.focus = true
        }

        Row {
            id: searchRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 8
            spacing: 6

            TextField {
                id: searchInput
                width: 180
                placeholderText: qsTr("Поиск")
                selectByMouse: true

                Timer {
                    id: searchDebounce
                    interval: 250
                    onTriggered: editor.find(searchInput.text,
                                             caseToggle.checked)
                }
                onTextChanged: searchDebounce.restart()
                onAccepted: editor.findNext()

                Keys.onEscapePressed: searchbar.close()
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
            ToolButton {
                id: caseToggle
                text: "Aa"
                checkable: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Учитывать регистр")
                onToggled: searchDebounce.restart()
            }
            Label {
                id: searchCount
                anchors.verticalCenter: parent.verticalCenter
                color: "#bbbbbb"
                text: qsTr("-/-")
            }
            ToolButton {
                text: "↑"
                onClicked: editor.findPrev()
            }
            ToolButton {
                text: "↓"
                onClicked: editor.findNext()
            }
            ToolButton {
                text: "✕"
                onClicked: searchbar.close()
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

    TabBar {
        id: tabBar
        height: 28
        visible: manager.tabs.length > 0
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        onCurrentIndexChanged: manager.activate(tabBar.currentIndex)

        Connections {
            target: manager
            function onCurrentIndexChanged() {
                tabBar.currentIndex = manager.currentIndex
            }
        }

        Repeater {
            model: manager.tabs
            TabButton {
                text: modelData.name + (modelData.dirty ? " *" : "")
                width: Math.max(120, tabText.implicitWidth + 40)

                Label {
                    id: tabText
                    visible: false
                    text: modelData.name + (modelData.dirty ? " *" : "")
                }
                ToolButton {
                    text: "✕"
                    width: 16
                    height: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: window.requestClose(index)
                }
            }
        }
    }

    // Оверлей прогресса загрузки больших файлов (порог — 20 МБ)
    Rectangle {
        id: loadOverlay
        visible: manager.loading
        anchors.fill: parent
        color: "#a8000000"
        z: 10

        Column {
            anchors.centerIn: parent
            spacing: 12

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#ffffff"
                text: manager.loading
                      ? qsTr("Загрузка: %1").arg(manager.loadingName)
                      : ""
            }
            ProgressBar {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 320
                from: 0
                to: 100
                value: manager.loadPercent
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#dddddd"
                text: manager.loadPercent + "%"
            }
        }
    }

    Rectangle {
        id: statusbar
        height: 24
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        color: "#007acc"

        Label {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 8
            color: "#ffffff"
            text: editor.filePath === ""
                  ? qsTr("Файл не выбран")
                  : editor.filePath
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 8
            spacing: 16

            Label {
                anchors.verticalCenter: parent.verticalCenter
                color: "#ffffff"
                text: editor.encoding + " · " + editor.lineEnding
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                color: "#ffffff"
                text: "Стр " + (editor.cursorLine + 1) + ", Стлб "
                      + (editor.cursorColumn + 1)
            }
        }
    }
}