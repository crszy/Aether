// A shell's or look's mark in a round tile: the live Aether logo for "aether", the shell's own logo from logos/
// when there is one, otherwise its Material icon on its colour (the old look).
import QtQuick
import qs.modules.aether as Ae
import "catalog.js" as Cat

Rectangle {
    id: sl

    property string shell: ""
    property string icon: "circle"
    property color colour: M3.primary
    property real size: 48
    property bool chosen: false
    readonly property string file: Cat.logoFile(shell)
    readonly property bool drawn: shell === "aether"
    readonly property bool hasLogo: drawn || file !== ""

    width: size
    height: size
    radius: chosen ? size / 3 : size / 2
    color: !hasLogo ? colour : Cat.darkLogos[shell] ? "#e6e3e8" : M3.surfaceHighest
    border.width: hasLogo ? Math.max(1, Math.round(size / 24)) : 0
    border.color: M3.alpha(colour, 0.7)
    Behavior on radius { NumberAnimation { duration: M3.d(300); easing.type: M3.springy } }

    Image {
        visible: !sl.drawn && sl.file !== ""
        anchors.centerIn: parent
        width: sl.size * 0.64
        height: width
        source: sl.file ? "logos/" + sl.file : ""
        sourceSize.width: Math.ceil(width * 2)
        sourceSize.height: Math.ceil(height * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: true
    }
    Loader {
        active: sl.drawn
        anchors.centerIn: parent
        sourceComponent: Ae.AetherLogo {
            size: sl.size * 0.46
            arcColour: M3.primary
        }
    }
    Icon {
        visible: !sl.hasLogo
        anchors.centerIn: parent
        name: sl.icon
        glyph: "●"
        size: sl.size * 0.46
        color: "#1b1b1f"
    }
}
