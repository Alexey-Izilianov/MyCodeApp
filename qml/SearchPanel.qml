import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Поиск по всем файлам проекта: результаты сгруппированы по файлам
Item {
    id: panel
    required property string rootPath
    signal openMatch(string path, int line, int column, int length)
    signal leave() // Esc — обратно в редактор

    function focusInput(text) {
        if (text)
            input.text = text
        input.forceActiveFocus()
        input.selectAll()
    }
    function run() {
        errorText = ""
        if (input.text.length > 0)
            search.start(input.text, caseButton.checked, panel.mode)
        else
            search.clear()
    }
    // 0 — plain, 1 — целое слово, 2 — регэксп
    readonly property int mode: regexButton.checked ? 2 : wordButton.checked ? 1 : 0
    property string errorText: "" // ошибка регэкспа

    ProjectSearch {
        id: search
        rootPath: panel.rootPath
        onRootPathChanged: clear()
        onErrorOccurred: (message) => { panel.errorText = message }
    }

    Timer {
        id: debounce
        interval: 500
        onTriggered: panel.run()
    }

    Column {
        id: header
        width: parent.width
        topPadding: 8
        spacing: 6

        Row {
            x: 8
            spacing: 4
            SearchField {
                id: input
                width: panel.width - 16 - caseButton.width - wordButton.width - regexButton.width - 8
                placeholderText: panel.rootPath === "" ? qsTr("Откройте папку") : qsTr("Найти в проекте")
                enabled: panel.rootPath !== ""
                onTextChanged: debounce.restart()
                onAccepted: { debounce.stop(); panel.run() }
                Keys.onEscapePressed: text !== "" ? (text = "", search.clear()) : panel.leave()
                Keys.onDownPressed: if (results.count > 0) { results.currentIndex = 0; results.forceActiveFocus() }
            }
            IconButton {
                id: caseButton
                anchors.verticalCenter: input.verticalCenter
                glyph: "Aa"
                checkable: true
                tip: qsTr("Учитывать регистр")
                onToggled: panel.run()
            }
            IconButton {
                id: wordButton
                anchors.verticalCenter: input.verticalCenter
                glyph: "ab"
                checkable: true
                tip: qsTr("Целое слово")
                onToggled: {
                    if (checked)
                        regexButton.checked = false
                    panel.run()
                }
            }
            IconButton {
                id: regexButton
                anchors.verticalCenter: input.verticalCenter
                glyph: ".*"
                checkable: true
                tip: qsTr("Регэксп")
                onToggled: {
                    if (checked)
                        wordButton.checked = false
                    panel.run()
                }
            }
        }
        Label {
            x: 12
            width: parent.width - 24
            visible: text !== ""
            color: Theme.faint
            font.pixelSize: Theme.smallFontSize
            elide: Text.ElideRight
            text: search.running ? qsTr("Поиск… файлов: %1 из %2").arg(search.filesDone).arg(search.filesTotal)
                : search.matchCount > 0 ? qsTr("%1 совпадений в %2 файлах%3").arg(search.matchCount)
                                              .arg(search.fileCount).arg(search.truncated ? qsTr(" (показаны первые)") : "")
                : input.text !== "" && search.filesTotal > 0 ? qsTr("Ничего не найдено") : ""
        }
        Label {
            x: 12
            width: parent.width - 24
            visible: panel.errorText !== ""
            color: Theme.error
            font.pixelSize: Theme.smallFontSize
            elide: Text.ElideRight
            text: panel.errorText !== "" ? qsTr("Ошибка регэкспа: %1").arg(panel.errorText) : ""
        }
    }

    ListView {
        id: results
        anchors.top: header.bottom
        anchors.topMargin: 6
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: search
        ScrollBar.vertical: ScrollBar {}
        highlightMoveDuration: 0
        Keys.onReturnPressed: currentItem.open()
        Keys.onEscapePressed: panel.leave()
        Keys.onUpPressed: currentIndex > 0 ? decrementCurrentIndex() : input.forceActiveFocus()

        delegate: Rectangle {
            id: row
            required property bool isFile
            required property string path
            required property string relative
            required property int line
            required property int column
            required property int length
            required property string preview
            required property int count

            function open() { panel.openMatch(path, line, column, isFile ? 0 : length) }

            width: results.width
            height: 24
            color: ListView.isCurrentItem && results.activeFocus ? Theme.pressed
                 : hover.hovered ? Theme.hover : "transparent"
            HoverHandler { id: hover }

            Row {
                visible: row.isFile
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                width: parent.width - 16
                FileIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: row.relative
                }
                Label {
                    id: fileName
                    text: row.relative.split("/").pop()
                    color: Theme.text
                }
                Label {
                    width: parent.width - x - countLabel.width - 6
                    text: row.relative.split("/").slice(0, -1).join("/")
                    color: Theme.faint
                    font.pixelSize: Theme.smallFontSize
                    elide: Text.ElideLeft
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    id: countLabel
                    text: row.count
                    color: Theme.faint
                    font.pixelSize: Theme.smallFontSize
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Label {
                visible: !row.isFile
                x: 30
                width: parent.width - x - 8
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.RichText
                text: row.preview
                color: Theme.muted
                clip: true // RichText не поддерживает elide
            }
            TapHandler {
                onTapped: row.open()
            }
        }
    }
}
