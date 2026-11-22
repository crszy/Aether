// AetherBars - music visualizer bars along the bottom of your desktop, behind your apps (like the rices with cava
// under the windows). Colours follow your scheme (so the wallpaper) or your skin. Only runs while something plays.
// Switch it on: rename to AetherBars.qml, and place it in desktop.json (full width at the bottom):
//   "AetherBars": { "screen": "main", "corner": "bottom-left", "x": 0, "y": 0 }
// Caelestia's bar drawing leaves the middle of the screen free: half the bars on the left, half on the right.
// Make it yours: height, how see-through, rounding, gap between bars, mirrored (grows up and down from the middle).
import QtQuick
import Quickshell
import Caelestia.Components
import Caelestia.Services
import qs.services

Item {
    id: root

    readonly property real barsHeight: 220     // the strip the bars grow in (taller = taller bars)
    readonly property real see: 0.55           // 0 = invisible, 1 = solid
    readonly property real rounding: 6
    readonly property real gap: 4
    readonly property bool mirrored: false

    readonly property bool playing: Players.active?.isPlaying ?? false

    implicitWidth: Screen.width
    implicitHeight: barsHeight

    opacity: playing ? 1 : 0
    Behavior on opacity {
        NumberAnimation { duration: 400; easing.type: Easing.OutCubic }
    }

    // keep the visualizer running only while you can see the bars
    ServiceRef {
        service: root.opacity > 0 ? Audio.cava : null
    }

    VisualiserBars {
        id: bars

        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        values: Audio.cava.values
        primaryColor: Qt.alpha(AetherStyle.palette.m3primary, root.see)
        secondaryColor: Qt.alpha(AetherStyle.palette.m3tertiary, root.see)
        rounding: root.rounding
        spacing: root.gap
        animationDuration: 120
        transform: Scale {
            origin.y: bars.height / 2
            yScale: root.mirrored ? 0.5 : 1
        }
    }

    FrameAnimation {
        running: root.opacity > 0 && !bars.settled
        onTriggered: bars.advance(frameTime)
    }
}
