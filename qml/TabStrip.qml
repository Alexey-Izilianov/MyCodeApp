import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

Rectangle {
    id: strip
    required property DocumentManager manager
    signal closeRequested(int index)

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

            width: Math.min(240, Math.max(96, title.implicitWidth + 52))
            height: list.height

            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 4
                anchors.bottomMargin: 4
                radius: 6
                color: Theme.hover
                opacity: tab.active ? 1 : mouse.containsMouse ? 0.5 : 0
            }

            Label {
                id: title
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 12
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
                acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                onClicked: (event) => {
                    if (event.button === Qt.MiddleButton)
                        strip.closeRequested(tab.index)
                    else
                        strip.manager.activate(tab.index)
                }
            }

            ToolTip.visible: mouse.containsMouse && title.truncated
            ToolTip.text: modelData.path
            ToolTip.delay: 700
        }
    }
}
