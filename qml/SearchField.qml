import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

TextField {
    id: field
    implicitHeight: 28
    leftPadding: 8
    rightPadding: 8
    color: Theme.text
    placeholderTextColor: Theme.faint
    selectionColor: "#2e436e"
    selectedTextColor: Theme.text
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    background: Rectangle {
        radius: 4
        color: Theme.editor
        border.color: field.activeFocus ? Theme.accent : Theme.border
    }
}
