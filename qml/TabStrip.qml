import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

Rectangle {
    id: strip
    required property DocumentManager manager
    // true у отдельного окна: вместо «Открепить» — «Вернуть в главное окно»
    property bool detached: false
    signal closeRequested(int index)
    signal detachRequested(int index)   // открепить в отдельное окно
    signal returnRequested(int index)   // вернуть вкладку в главное окно
    // Вкладку тянут мышью: точка — в координатах сцены
    signal tabDragged(int index, point point)
    signal tabDropped(int index, point point)

    Menu {
        id: contextMenu
        property int index: -1
        MenuItem {
            text: strip.detached ? qsTr("Вернуть в главное окно") : qsTr("Открепить в отдельное окно")
            onTriggered: strip.detached
                         ? strip.returnRequested(contextMenu.index)
                         : strip.detachRequested(contextMenu.index)
        }
        MenuItem { text: qsTr("Закрыть вкладку"); onTriggered: strip.closeRequested(contextMenu.index) }
    }

    implicitHeight: 36
    color: Theme.editor

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        orientation: ListView.Horizontal
        spacing: 2
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        model: strip.manager.tabs
        currentIndex: strip.manager.currentIndex
        highlightFollowsCurrentItem: false
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

        delegate: Item {
            id: tab
            required property int index
            required property var modelData
            readonly property bool active: index === strip.manager.currentIndex
            readonly property bool showClose: active || mouse.containsMouse

            width: Math.min(260, Math.max(116, title.implicitWidth + 74))
            height: list.height

            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 4
                anchors.bottomMargin: 4
                radius: 6
                color: Theme.hover
                opacity: tab.active ? 1 : mouse.containsMouse ? 0.5 : 0
            }

            FileIcon {
                id: tabIcon
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 10
                name: tab.modelData.name
            }
            Label {
                id: title
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: tabIcon.right
                anchors.leftMargin: 6
                anchors.right: closeButton.left
                anchors.rightMargin: 6
                text: tab.modelData.name
                elide: Text.ElideMiddle
                color: tab.active ? Theme.text : Theme.muted
            }

            // Кнопка закрытия; у изменённого файла вместо неё точка, пока нет наведения
            Item {
                id: closeButton
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 8
                width: 18
                height: 18

                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    visible: closeMouse.containsMouse
                    color: Theme.pressed
                }
                CrossIcon {
                    anchors.centerIn: parent
                    visible: tab.showClose && (!tab.modelData.dirty || closeMouse.containsMouse)
                    color: closeMouse.containsMouse ? Theme.text : Theme.muted
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 8
                    height: 8
                    radius: 4
                    visible: tab.modelData.dirty && !closeMouse.containsMouse
                    color: Theme.muted
                }
                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: strip.closeRequested(tab.index)
                }
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                z: -1
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                preventStealing: true
                property point pressedAt
                property bool dragging: false
                onPressed: (event) => {
                    pressedAt = Qt.point(event.x, event.y)
                    dragging = false
                }
                onPositionChanged: (event) => {
                    if (!(event.buttons & Qt.LeftButton))
                        return
                    if (!dragging && Math.hypot(event.x - pressedAt.x, event.y - pressedAt.y) > 12)
                        dragging = true
                    if (dragging)
                        strip.tabDragged(tab.index, mapToItem(null, event.x, event.y))
                }
                onReleased: (event) => {
                    if (dragging)
                        strip.tabDropped(tab.index, mapToItem(null, event.x, event.y))
                }
                onCanceled: if (dragging) strip.tabDropped(tab.index, Qt.point(-1, -1))
                onClicked: (event) => {
                    if (dragging)
                        return
                    if (event.button === Qt.MiddleButton)
                        strip.closeRequested(tab.index)
                    else if (event.button === Qt.RightButton) {
                        contextMenu.index = tab.index
                        contextMenu.popup()
                    } else
                        strip.manager.activate(tab.index)
                }
            }

            ToolTip.visible: mouse.containsMouse && title.truncated
            ToolTip.text: modelData.path
            ToolTip.delay: 700
        }
    }
}
