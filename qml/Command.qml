import QtQuick
import QtQuick.Controls.Basic
import MyCodeApp

// Пункт меню с настраиваемым сочетанием клавиш: id команды и сочетание по
// умолчанию; действующее сочетание — из Keymap (правится в настройках)
Action {
    required property string command
    property string title: text // название в списке горячих клавиш
    property string defaultShortcut: ""

    shortcut: Keymap.revision >= 0 ? Keymap.sequence(command, defaultShortcut) : ""
    Component.onCompleted: Keymap.declare(command, title, defaultShortcut)
    onTitleChanged: Keymap.declare(command, title, defaultShortcut) // смена языка
    Component.onDestruction: Keymap.undeclare(command) // команда выгруженного плагина
}
