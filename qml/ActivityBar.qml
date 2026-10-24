import QtQuick
import MyCodeApp

// Колонка значков слева от боковой панели: повторное нажатие на активный значок прячет панель
Item {
    id: bar
    required property Sidebar sidebar
    property int gitChanges: 0
    signal pluginsRequested()
    signal settingsRequested()

    implicitWidth: 48

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 5
        Repeater {
            model: [{ mode: "files", icon: "files", tip: qsTr("Проводник") },
                    { mode: "search", icon: "search", tip: qsTr("Поиск по проекту") },
                    { mode: "git", icon: "git", tip: qsTr("Git") },
                    { mode: "assistant", icon: "assistant", tip: qsTr("Ассистент") }]
            ActivityButton {
                required property var modelData
                iconName: modelData.icon
                tip: modelData.tip
                badge: modelData.mode === "git" ? bar.gitChanges : 0
                checked: bar.sidebar.visible && bar.sidebar.mode === modelData.mode
                onClicked: {
                    if (checked) {
                        bar.sidebar.visible = false
                    } else {
                        bar.sidebar.mode = modelData.mode
                        bar.sidebar.focusPanel()
                    }
                }
            }
        }
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        spacing: 5
        ActivityButton { iconName: "plugins"; tip: qsTr("Плагины"); onClicked: bar.pluginsRequested() }
        ActivityButton { iconName: "settings"; tip: qsTr("Настройки"); onClicked: bar.settingsRequested() }
    }
}
