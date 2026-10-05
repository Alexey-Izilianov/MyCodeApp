import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Ctrl+P: быстрый переход к файлу проекта по части имени или пути
Popup {
    id: popup
    required property Workspace workspace
    signal openFile(string path)

    function show() {
        workspace.rescan() // подхватить новые файлы; список обновится, когда обход закончится
        input.text = ""
        open()
        input.forceActiveFocus()
    }
    function accept() {
        if (list.currentIndex >= 0 && list.currentIndex < list.count) {
            close()
            openFile(list.model[list.currentIndex].path)
        }
    }

    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: 48
    width: Math.min(600, parent.width - 40)
    padding: 6
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        color: Theme.popup
        border.color: Theme.border
        radius: 8
    }

    contentItem: Column {
        spacing: 6

        SearchField {
            id: input
            width: parent.width
            placeholderText: popup.workspace.rootPath === "" ? qsTr("Сначала откройте папку")
                                                            : qsTr("Имя файла")
            onTextChanged: list.currentIndex = 0
            onAccepted: popup.accept()
            Keys.onUpPressed: list.currentIndex = Math.max(0, list.currentIndex - 1)
            Keys.onDownPressed: list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
        }

        ListView {
            id: list
            width: parent.width
            height: Math.min(count, 12) * 26
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            // Условие на fileCount — пересчёт, когда закончится обход папки
            model: popup.visible && popup.workspace.fileCount >= 0 ? popup.workspace.findFiles(input.text, 50) : []
            highlightMoveDuration: 0

            delegate: Rectangle {
                id: item
                required property var modelData
                required property int index
                width: list.width
                height: 26
                radius: 4
                color: ListView.isCurrentItem ? Theme.pressed : hover.hovered ? Theme.hover : "transparent"
                HoverHandler { id: hover }

                FileIcon {
                    id: icon
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    name: item.modelData.name
                }
                Label {
                    id: name
                    anchors.left: icon.right
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: item.modelData.name
                    color: Theme.text
                }
                Label {
                    anchors.left: name.right
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: item.modelData.relative
                    color: Theme.faint
                    font.pixelSize: Theme.smallFontSize
                    elide: Text.ElideLeft
                }
                TapHandler {
                    onTapped: { list.currentIndex = item.index; popup.accept() }
                }
            }
        }

        Label {
            visible: list.count === 0 && popup.workspace.rootPath !== ""
            leftPadding: 8
            text: popup.workspace.scanning ? qsTr("Индексация файлов…") : qsTr("Ничего не найдено")
            color: Theme.faint
        }
    }
}
