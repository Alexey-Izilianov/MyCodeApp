import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Просмотр изменений файла поверх редактора: удалённые строки — красным фоном
// перед добавленными (зелёным), номера строк — по новой версии
Rectangle {
    id: view
    required property GitService git
    property string path: ""
    property string title: ""
    property int changeCount: 0
    signal closed()
    signal openFile(string path)

    function show(filePath, staged) {
        path = filePath
        git.showDiff(filePath, staged, diffEditor)
    }
    function close() {
        if (!visible)
            return
        visible = false
        closed()
    }
    function step(direction) { diffEditor.gotoChange(direction) }

    visible: false
    color: Theme.editor

    Connections {
        target: view.git
        function onDiffReady(title, changeCount) {
            view.title = title
            view.changeCount = changeCount
            view.visible = true
            diffEditor.forceActiveFocus()
            diffEditor.gotoChange(0)
        }
    }

    Rectangle {
        id: bar
        width: parent.width
        height: 32
        color: Theme.chrome
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }

        Label {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: buttons.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: view.title + (view.changeCount === 0 ? qsTr("  ·  без изменений")
                                                       : qsTr("  ·  участков: %1").arg(view.changeCount))
            color: Theme.text
            elide: Text.ElideMiddle
        }
        Row {
            id: buttons
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            IconButton { glyph: "↑"; tip: qsTr("Предыдущее изменение (Shift+Alt+F5)"); onClicked: view.step(-1) }
            IconButton { glyph: "↓"; tip: qsTr("Следующее изменение (Alt+F5)"); onClicked: view.step(1) }
            FlatButton {
                implicitHeight: 24
                text: qsTr("Открыть файл")
                onClicked: { view.close(); view.openFile(view.path) }
            }
            IconButton { tip: qsTr("Закрыть (Esc)"); onClicked: view.close() }
        }
    }

    EditorView {
        id: diffEditor
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        Keys.onEscapePressed: view.close()
    }
}
