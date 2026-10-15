import QtQuick
import QtQuick.Controls.Basic
import QWindowKit
import MyCodeApp

// Строка заголовка вместо системной: значок, меню (вкладывается внутрь), поле перехода
// к файлу и кнопки окна. Перетаскивание, двойной щелчок и Snap Layouts даёт WindowAgent;
// на macOS кнопки окна системные (слева), свои не рисуются
Rectangle {
    id: bar
    required property WindowAgent agent
    required property Window window
    property string searchShortcut: ""
    default property alias menu: menuSlot.data
    signal searchRequested()

    readonly property bool mac: Qt.platform.os === "osx"
    readonly property bool maximized: window.visibility === Window.Maximized

    implicitHeight: 38
    color: Theme.ground

    Image {
        id: logo
        x: bar.mac ? 78 : 12
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        source: "qrc:/assets/icon/app-256.png"
        sourceSize: Qt.size(32, 32)
        mipmap: true
    }

    Item {
        id: menuSlot
        anchors.left: logo.right
        anchors.leftMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        width: childrenRect.width
        height: childrenRect.height
    }

    AbstractButton {
        id: search
        readonly property real side: Math.max(menuSlot.x + menuSlot.width, controls.width) + 16
        anchors.centerIn: parent
        width: Math.min(440, bar.width - 2 * side)
        height: 26
        visible: width >= 180
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        onClicked: bar.searchRequested()

        contentItem: Item {
            LineIcon {
                id: searchIcon
                anchors.verticalCenter: parent.verticalCenter
                x: 10
                width: 14
                height: 14
                name: "search"
                color: Theme.faint
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: searchIcon.right
                anchors.leftMargin: 8
                anchors.right: hint.left
                text: qsTr("Перейти к файлу")
                color: Theme.muted
                elide: Text.ElideRight
                font.pixelSize: Theme.smallFontSize
            }
            Label {
                id: hint
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 10
                text: bar.searchShortcut
                color: Theme.faint
                font.pixelSize: Theme.smallFontSize
            }
        }
        background: Rectangle {
            radius: 6
            color: search.hovered ? Theme.hover : Theme.editor
            border.color: Theme.border
        }
    }

    Row {
        id: controls
        anchors.right: parent.right
        height: parent.height
        visible: !bar.mac
        WindowButton {
            id: minimizeButton
            height: parent.height
            kind: "minimize"
            onClicked: bar.window.showMinimized()
        }
        WindowButton {
            id: maximizeButton
            height: parent.height
            kind: bar.maximized ? "restore" : "maximize"
            onClicked: bar.maximized ? bar.window.showNormal() : bar.window.showMaximized()
        }
        WindowButton {
            id: closeButton
            height: parent.height
            kind: "close"
            onClicked: bar.window.close()
        }
    }

    Component.onCompleted: {
        agent.setTitleBar(bar)
        agent.setHitTestVisible(menuSlot, true)
        agent.setHitTestVisible(search, true)
        if (!mac) {
            agent.setSystemButton(WindowAgent.WindowIcon, logo)
            agent.setSystemButton(WindowAgent.Minimize, minimizeButton)
            agent.setSystemButton(WindowAgent.Maximize, maximizeButton)
            agent.setSystemButton(WindowAgent.Close, closeButton)
        }
    }
}
