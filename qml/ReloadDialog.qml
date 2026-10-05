import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Файл изменили снаружи, а в редакторе есть несохранённые правки
Popup {
    id: dialog
    property int tabIndex: -1
    property string fileName: ""
    signal reloadRequested(int index)
    signal keepRequested(int index)

    function ask(index, name) {
        tabIndex = index
        fileName = name
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 440
    modal: true
    padding: 20
    closePolicy: Popup.NoAutoClose

    Overlay.modal: Rectangle { color: "#80000000" }
    background: Rectangle {
        color: Theme.chrome
        border.color: Theme.border
        radius: 8
    }

    contentItem: Column {
        spacing: 8

        Label {
            width: parent.width
            text: qsTr("«%1» изменён на диске").arg(dialog.fileName)
            color: Theme.text
            font.pixelSize: 14
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }
        Label {
            width: parent.width
            text: qsTr("В редакторе есть несохранённые правки. Загрузить версию с диска (правки пропадут) или оставить свою?")
            color: Theme.muted
            wrapMode: Text.Wrap
        }
        Item { width: 1; height: 12 }
        Row {
            anchors.right: parent.right
            spacing: 8
            FlatButton {
                text: qsTr("Оставить мою")
                onClicked: { dialog.close(); dialog.keepRequested(dialog.tabIndex) }
            }
            FlatButton {
                primary: true
                text: qsTr("Загрузить с диска")
                onClicked: { dialog.close(); dialog.reloadRequested(dialog.tabIndex) }
            }
        }
    }
}
