import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

Popup {
    id: dialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 440
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: "#80000000" }
    background: Rectangle { color: Theme.chrome; border.color: Theme.border; radius: 8 }

    contentItem: Column {
        spacing: 6
        Label {
            text: "MyCodeApp"
            color: Theme.text
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }
        Label {
            text: qsTr("Версия %1 · Qt %2").arg(AppInfo.version).arg(AppInfo.qtVersion)
            color: Theme.muted
        }
        Item { width: 1; height: 8 }
        Label {
            width: parent.width
            text: (AppInfo.portable ? qsTr("Портативный режим. ") : "") + qsTr("Данные: %1").arg(AppInfo.dataDir)
            color: Theme.faint
            font.pixelSize: Theme.smallFontSize
            wrapMode: Text.WrapAnywhere
        }
        Item { width: 1; height: 12 }
        Row {
            anchors.right: parent.right
            spacing: 8
            FlatButton {
                text: qsTr("Открыть папку данных")
                onClicked: Qt.openUrlExternally("file:///" + AppInfo.dataDir)
            }
            FlatButton {
                primary: true
                text: qsTr("Закрыть")
                onClicked: dialog.close()
            }
        }
    }
}
