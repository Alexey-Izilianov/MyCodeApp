import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import MyCodeApp

ApplicationWindow {
    id: window
    width: 1000
    height: 700
    visible: true
    title: qsTr("MyCodeApp (M1)")

    EditorView {
        id: editor
        objectName: "editor" // main.cpp открывает файл из аргумента
        focus: true
        anchors.top: toolbar.bottom
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
        onAccepted: editor.filePath = selectedFile
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