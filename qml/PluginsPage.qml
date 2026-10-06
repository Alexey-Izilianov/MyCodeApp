import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Настройки → Плагины: список, включение и выключение без перезапуска
Item {
    id: page
    property var plugins: null // PluginManager

    ListView {
        id: list
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: footer.top
        clip: true
        spacing: 4
        boundsBehavior: Flickable.StopAtBounds
        model: page.plugins ? page.plugins.plugins : []
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
            id: card
            required property var modelData
            x: 16
            width: list.width - 32
            height: info.implicitHeight + 20
            radius: 6
            color: "transparent"
            border.color: Theme.border

            Column {
                id: info
                x: 12
                y: 10
                width: parent.width - 24 - toggle.width - 12
                spacing: 4
                Row {
                    spacing: 8
                    Label {
                        text: card.modelData.name || card.modelData.id
                        color: Theme.text
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: card.modelData.version ? qsTr("версия %1").arg(card.modelData.version) : ""
                        color: Theme.faint
                        font.pixelSize: Theme.smallFontSize
                    }
                }
                Label {
                    width: parent.width
                    visible: text !== ""
                    text: card.modelData.description || ""
                    color: Theme.muted
                    wrapMode: Text.Wrap
                }
                Label {
                    width: parent.width
                    visible: text !== ""
                    text: card.modelData.error
                    color: Theme.error
                    wrapMode: Text.Wrap
                }
                Label {
                    width: parent.width
                    text: card.modelData.path
                    color: Theme.faint
                    font.pixelSize: Theme.smallFontSize
                    elide: Text.ElideMiddle
                }
            }
            FlatButton {
                id: toggle
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                implicitHeight: 26
                text: card.modelData.enabled ? qsTr("Выключить") : qsTr("Включить")
                onClicked: page.plugins.setEnabled(card.modelData.id, !card.modelData.enabled)
            }
        }
    }

    Label {
        anchors.centerIn: list
        visible: list.count === 0
        width: list.width - 64
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: qsTr("Плагинов нет. Положите DLL плагина в одну из папок ниже и нажмите «Обновить список».")
        color: Theme.muted
    }

    Column {
        id: footer
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        x: 16
        width: parent.width - 32
        spacing: 8
        Repeater {
            model: page.plugins ? page.plugins.folders : []
            Label {
                required property string modelData
                width: footer.width
                text: modelData
                color: Theme.faint
                font.pixelSize: Theme.smallFontSize
                elide: Text.ElideMiddle
            }
        }
        Row {
            spacing: 8
            FlatButton {
                text: qsTr("Обновить список")
                onClicked: page.plugins.rescan()
            }
            FlatButton {
                text: qsTr("Открыть папку плагинов")
                onClicked: Qt.openUrlExternally("file:///" + page.plugins.folders[1])
            }
        }
    }
}
