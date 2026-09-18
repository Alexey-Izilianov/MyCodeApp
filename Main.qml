import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import MyCodeApp

ApplicationWindow {
    id: window
    width: 1000
    height: 700
    visible: true
    title: qsTr("Spike: рендер редактора(Этап 0)")

    TextSpikeItem {
        id: editor
        objectName: "editor" // C++ ищет редактор по этому имени (main.cpp)
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }

    WheelHandler {
        onWheel: (event) => {
                     editor.scrollY = Math.max(0, editor.scrollY -
                     event.angleDelta.y * 0.5)
                 }
    }

    FileDialog {
        id: fileDialog
        onAccepted: {
            console.log("DIAG: файл выбран:", selectedFile)
            editor.filePath = selectedFile
        }
    }

    Rectangle {
        id: toolbar
        height: 40
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
                text: qsTr("Открыть файл ... ")
                onClicked: {
                    console.log("DIAG: кнопка нажата, открываю диалог")
                    fileDialog.open()
                }
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                color: "#aaaaaa"
                text: editor.filePath === "" ? qsTr("Файл не выбран")
                    : editor.filePath + "| scrollY "
                    + editor.scrollY.toFixed(0)
            }
        }
    }
}
