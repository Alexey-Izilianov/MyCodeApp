import QtQuick
import QtQuick.Shapes

// Линейный значок на сетке 24×24 (в духе Lucide): контуры из SVG-путей, без шрифтов и картинок
Item {
    id: icon
    property string name
    property color color: "white"
    property real stroke: 1.8

    readonly property var paths: ({
        files: "M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z M14 3v5h5",
        search: "M18 11a7 7 0 1 1-14 0a7 7 0 1 1 14 0z M20.5 20.5l-4.5-4.5",
        git: "M6 3v12 M21 6a3 3 0 1 1-6 0a3 3 0 1 1 6 0z M9 18a3 3 0 1 1-6 0a3 3 0 1 1 6 0z M18 9a9 9 0 0 1-9 9",
        plugins: "M4 4h6v6H4z M14 4h6v6h-6z M4 14h6v6H4z M14 17h6 M17 14v6",
        settings: "M4 6h9 M17 6h3 M4 12h3 M11 12h9 M4 18h11 M19 18h1 M15 4v4 M9 10v4 M17 16v4"
    })

    implicitWidth: 18
    implicitHeight: 18

    Shape {
        width: 24
        height: 24
        scale: icon.width / 24
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: icon.color
            strokeWidth: icon.stroke
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.paths[icon.name] ?? "" }
        }
    }
}
