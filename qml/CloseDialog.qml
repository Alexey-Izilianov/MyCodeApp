import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Закрытие вкладки с несохранёнными изменениями
Popup {
    id: dialog
    property int tabIndex: -1
    property string fileName: ""
    signal saveRequested(int index)
    signal discardRequested(int index)

    function ask(index, name) {
        tabIndex = index
        fileName = name
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 420
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape

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
            text: qsTr("Сохранить изменения в «%1»?").arg(dialog.fileName)
            color: Theme.text
            font.pixelSize: 14
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }
        Label {
            width: parent.width
            text: qsTr("Если не сохранить, изменения будут потеряны.")
            color: Theme.muted
            wrapMode: Text.Wrap
        }
        Item { width: 1; height: 12 }
        Row {
            anchors.right: parent.right
            spacing: 8
            FlatButton {
                text: qsTr("Не сохранять")
                onClicked: { dialog.close(); dialog.discardRequested(dialog.tabIndex) }
            }
            FlatButton {
                text: qsTr("Отмена")
                onClicked: dialog.close()
            }
            FlatButton {
                primary: true
                text: qsTr("Сохранить")
                onClicked: { dialog.close(); dialog.saveRequested(dialog.tabIndex) }
            }
        }
    }
}
