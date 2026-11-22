// A shell's or look's own logo (from logos/) filling a rounded square, or its Material icon when it has none.
// Square icons with their own background (.png/.jpg) cover the tile; marks (.svg) sit on a dark backing.
import QtQuick
import QtQuick.Effects
import "catalog.js" as Cat

Item {
    id: sl

    property string shell: ""
    property string icon: "circle"
    property color colour: M3.primary
    property real size: 48
    property bool chosen: false
    readonly property string file: Cat.logoFile(shell)
    readonly property bool hasLogo: file !== ""
    readonly property real corner: size * 0.24

    width: size
    height: size

    Item {
        id: content
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }

        Rectangle {   // backing: dark for every mark so light and dark logos read the same
            anchors.fill: parent
            color: Qt.tint("#16181c", M3.alpha(sl.colour, 0.22))
        }
        Image {
            visible: sl.file !== ""
            anchors.centerIn: parent
            // square icons with their own background cover the tile; marks fill it with a thin margin
            readonly property bool fullBleed: sl.file.slice(-4) !== ".svg"
            width: sl.size * (fullBleed ? 1 : 0.86)
            height: width
            source: sl.file ? "logos/" + sl.file : ""
            sourceSize.width: Math.ceil(width * 2)
            sourceSize.height: Math.ceil(height * 2)
            fillMode: fullBleed ? Image.PreserveAspectCrop : Image.PreserveAspectFit
            smooth: true
            mipmap: true
            asynchronous: true
            layer.enabled: Cat.darkLogos[sl.shell] === true
            layer.effect: MultiEffect { brightness: 0.9 }
        }
        Icon {
            visible: !sl.hasLogo
            anchors.centerIn: parent
            name: sl.icon
            glyph: "●"
            size: sl.size * 0.8
            color: sl.colour
        }
    }
    Item {
        id: mask
        visible: false
        anchors.fill: parent
        layer.enabled: true
        Rectangle {
            anchors.fill: parent
            radius: sl.corner
            antialiasing: true
        }
    }
    Rectangle {   // outline; the chosen look gets a thick primary one
        anchors.fill: parent
        radius: sl.corner
        color: "transparent"
        border.width: sl.chosen ? Math.max(2, Math.round(sl.size / 20)) : Math.max(1, Math.round(sl.size / 48))
        border.color: sl.chosen ? M3.primary : M3.alpha(sl.colour, 0.35)
        Behavior on border.width { NumberAnimation { duration: 150 } }
    }
}
