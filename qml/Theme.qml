pragma Singleton
import QtQuick

// Палитра интерфейса. Цвета редактора — в editerview.cpp (kBackground и др.)
QtObject {
    readonly property color editor: "#1e1f22"
    readonly property color chrome: "#2b2d30"
    readonly property color popup: "#2b2d30"
    readonly property color border: "#393b40"
    readonly property color hover: "#393b40"
    readonly property color pressed: "#43454a"
    readonly property color text: "#dfe1e5"
    readonly property color muted: "#9da0a8"
    readonly property color faint: "#6f737a"
    readonly property color accent: "#3574f0"
    readonly property color scrollThumb: "#46484d"

    readonly property string fontFamily: "Segoe UI"
    readonly property int fontSize: 13
    readonly property int smallFontSize: 12
}
