import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import MyCodeApp

// Нижняя панель: проблемы (диагностика всех открытых файлов) и найденные ссылки
Rectangle {
    id: panel
    required property LspManager lsp
    required property Workspace workspace
    property string mode: "problems" // "problems" | "references"
    signal openAt(string path, int line, int column)

    readonly property var severityColors: [Theme.error, Theme.warning, Theme.info, Theme.info]

    function show(newMode) {
        mode = newMode
        visible = true
    }

    height: 200
    color: Theme.chrome

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
    }
    MouseArea { // перетаскивание верхнего края
        width: parent.width
        height: 6
        y: -3
        cursorShape: Qt.SizeVerCursor
        property real startY
        onPressed: (mouse) => startY = mouse.y
        onPositionChanged: (mouse) => {
            if (pressed)
                panel.height = Math.max(80, Math.min(600, panel.height - (mouse.y - startY)))
        }
    }

    Row {
        id: tabs
        x: 4
        y: 1
        height: 30
        spacing: 2
        Repeater {
            model: [
                { mode: "problems", text: qsTr("Проблемы: %1").arg(panel.lsp.problems.length) },
                { mode: "references", text: qsTr("Ссылки: %1").arg(panel.lsp.references.length) }
            ]
            AbstractButton {
                id: tab
                required property var modelData
                readonly property bool active: panel.mode === modelData.mode
                height: tabs.height
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                leftPadding: 8
                rightPadding: 8
                contentItem: Label {
                    text: tab.modelData.text
                    color: tab.active || tab.hovered ? Theme.text : Theme.muted
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 2
                        color: tab.active ? Theme.accent : "transparent"
                    }
                }
                onClicked: panel.mode = modelData.mode
            }
        }
    }
    IconButton {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: tabs.verticalCenter
        tip: qsTr("Закрыть")
        onClicked: panel.visible = false
    }

    component Entry: Rectangle {
        id: entry
        property string path
        property int line
        property int column
        property color marker: "transparent"
        property string text
        width: ListView.view.width
        height: 24
        color: hover.hovered ? Theme.hover : "transparent"
        HoverHandler { id: hover }
        Rectangle {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: entry.marker
        }
        Label {
            id: message
            x: 28
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width * 0.65)
            text: entry.text
            color: Theme.text
            elide: Text.ElideRight
        }
        Label {
            anchors.left: message.right
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: panel.workspace.relativePath(entry.path) + ":" + (entry.line + 1)
            color: Theme.faint
            font.pixelSize: Theme.smallFontSize
            elide: Text.ElideLeft
        }
        TapHandler { onTapped: panel.openAt(entry.path, entry.line, entry.column) }
    }

    StackLayout {
        anchors.top: tabs.bottom
        anchors.topMargin: 2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        currentIndex: panel.mode === "problems" ? 0 : 1

        ListView {
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: panel.lsp.problems
            ScrollBar.vertical: ScrollBar {}
            delegate: Entry {
                required property var modelData
                path: modelData.path
                line: modelData.line
                column: modelData.column
                marker: panel.severityColors[Math.max(0, modelData.severity - 1)]
                text: modelData.message
            }
        }
        ListView {
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: panel.lsp.references
            ScrollBar.vertical: ScrollBar {}
            delegate: Entry {
                required property var modelData
                path: modelData.path
                line: modelData.line
                column: modelData.column
                text: modelData.preview
            }
        }
    }
}
