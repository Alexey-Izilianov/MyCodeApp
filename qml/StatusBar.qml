import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

Rectangle {
    id: statusBar
    required property EditorView editor
    required property LspManager lsp
    property string branch: ""
    readonly property bool hasFile: editor.filePath !== ""
    signal problemsRequested()
    signal branchRequested()

    // Сообщение на несколько секунд вместо пути файла («Определение не найдено» и т.п.)
    function flash(text) {
        message.text = text
        messageTimer.restart()
    }

    // Условия на problems и statusTick — чтобы пересчитать при новой диагностике и смене статуса
    readonly property var fileDiagnostics: lsp.problems ? lsp.diagnosticsFor(editor.filePath) : []
    readonly property int errors: fileDiagnostics.filter(d => d.severity === 1).length
    readonly property int warnings: fileDiagnostics.filter(d => d.severity === 2).length
    readonly property string serverStatus: statusTick >= 0 ? lsp.statusFor(editor.filePath) : ""
    property int statusTick: 0
    Connections {
        target: statusBar.lsp
        function onStatusChanged() { statusBar.statusTick++ }
    }

    implicitHeight: 24
    color: Theme.chrome

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
    }

    Timer {
        id: messageTimer
        interval: 3500
        onTriggered: message.text = ""
    }

    Label {
        id: message
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: details.left
        anchors.rightMargin: 24
        visible: text !== ""
        color: Theme.text
        font.pixelSize: Theme.smallFontSize
        elide: Text.ElideRight
    }
    Label {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: details.left
        anchors.rightMargin: 24
        visible: message.text === ""
        text: statusBar.editor.filePath
        elide: Text.ElideMiddle
        color: Theme.muted
        font.pixelSize: Theme.smallFontSize
    }

    Row {
        id: details
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 12
        spacing: 18
        visible: statusBar.hasFile

        Label {
            visible: statusBar.branch !== ""
            text: qsTr("Ветка: %1").arg(statusBar.branch)
            color: branchLink.hovered ? Theme.text : Theme.muted
            font.pixelSize: Theme.smallFontSize
            HoverHandler { id: branchLink; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: statusBar.branchRequested() }
        }
        Label {
            visible: statusBar.errors + statusBar.warnings > 0
            text: [statusBar.errors > 0 ? qsTr("Ошибок: %1").arg(statusBar.errors) : "",
                   statusBar.warnings > 0 ? qsTr("Предупреждений: %1").arg(statusBar.warnings) : ""]
                  .filter(item => item !== "").join(", ")
            color: problemsLink.hovered ? Theme.text : Theme.muted
            font.pixelSize: Theme.smallFontSize
            HoverHandler { id: problemsLink; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: statusBar.problemsRequested() }
        }

        Repeater {
            model: [
                statusBar.serverStatus,
                statusBar.editor.readOnly ? qsTr("Только чтение") : "",
                statusBar.editor.cursorCount > 1 ? qsTr("Курсоров: %1").arg(statusBar.editor.cursorCount) : "",
                qsTr("Стр %1, Стлб %2").arg(statusBar.editor.cursorLine + 1)
                                       .arg(statusBar.editor.cursorColumn + 1),
                statusBar.editor.encoding,
                statusBar.editor.lineEnding,
                statusBar.editor.language
            ].filter(item => item !== "")
            Label {
                required property string modelData
                text: modelData
                color: Theme.muted
                font.pixelSize: Theme.smallFontSize
            }
        }
    }
}
