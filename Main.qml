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
    }

    WheelHandler {
        onWheel: (event) => {
                     editor.scrollY = Math.max(0, editor.scrollY -
                     event.angleDelta.y * 0.5)
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
        Label {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 8
            color: "#ffffff"
            text: "Стр " + (editor.cursorLine + 1) + ", Стлб "
                  + (editor.cursorColumn + 1)
        }
    }
}