import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Малый QML: объекты, свойства, сигналы, JavaScript внутри
ApplicationWindow {
    id: root
    width: 640
    height: 480
    visible: true
    title: qsTr("Samples — %1").arg(counter)

    property int counter: 0
    readonly property color accent: "#3574f0"
    required property var model
    signal reset(int value)

    function increment(step) {
        counter += step ?? 1
        if (counter > 10 && !root.fullScreen)
            console.log(`counter = ${counter}`)
    }

    onReset: (value) => counter = value

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label {
            text: "Clicks: " + root.counter
            font.pixelSize: 18
            color: root.accent
        }
        Button {
            text: qsTr("Click")
            onClicked: root.increment(1)
        }
        Repeater {
            model: [1, 2, 3]
            delegate: Rectangle {
                required property int modelData
                Layout.fillWidth: true
                height: 24
                radius: 4
                color: modelData % 2 === 0 ? "#2b2d30" : "transparent"
            }
        }
    }
}
