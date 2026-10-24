pragma Singleton
import QtQuick
import MyCodeApp

// Палитра интерфейса из текущей темы (ThemeManager, раздел ui).
// Цвета самого редактора — Palette в C++
QtObject {
    readonly property var ui: ThemeManager.ui

    readonly property color ground: ui.ground // подложка окна под «островами» панелей
    readonly property color islandBorder: ui.islandBorder ?? "transparent" // контур островов (светлая тема)
    readonly property color editor: ui.editor
    readonly property color chrome: ui.chrome
    readonly property color popup: ui.popup
    readonly property color border: ui.border
    readonly property color hover: ui.hover
    readonly property color pressed: ui.pressed
    readonly property color text: ui.text
    readonly property color muted: ui.muted
    readonly property color faint: ui.faint
    readonly property color accent: ui.accent
    readonly property color selection: ui.selection
    readonly property color scrollThumb: ui.scrollThumb
    readonly property color error: ui.error
    readonly property color warning: ui.warning
    readonly property color info: ui.info

    // Состояния git: буквы в дереве и панели
    readonly property color gitAdded: ui.gitAdded
    readonly property color gitModified: ui.gitModified
    readonly property color gitDeleted: ui.gitDeleted
    readonly property color gitConflict: ui.gitConflict
    function gitColor(status) {
        switch (status) {
        case "A": case "U": return gitAdded
        case "M": case "R": case "T": return gitModified
        case "D": return gitDeleted
        case "C": return gitConflict
        default: return muted
        }
    }

    // На Linux — шрифт системной темы (Fly: PT Astra Sans, на 1.6 — Inter/Noto Sans)
    readonly property string fontFamily: Qt.platform.os === "windows" ? "Segoe UI" : Qt.application.font.family
    readonly property string monoFamily: Qt.platform.os === "windows" ? "Cascadia Mono" : "DejaVu Sans Mono"
    readonly property int fontSize: 13
    readonly property int smallFontSize: 12

    readonly property int islandGap: 6
    readonly property int islandRadius: 8
}
