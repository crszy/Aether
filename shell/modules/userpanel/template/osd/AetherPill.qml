// Aether's volume / brightness pop-up: a slim pill on the bottom edge, coloured by your scheme (so it follows the
// wallpaper). Aether slides it in from that edge and back out - quick, no bounce. The level is the splash's wavy
// line (it flattens out near 100%).
// Use it: Aether Settings > On-screen display > AetherPill, or windows.json "mixOsd": "user:AetherPill".
// osd.kind = "volume" | "mic" | "brightness"; osd.volume / osd.sourceVolume / osd.brightness are 0..1.
// Move it: set position to "top", "right", "bottom-left" ... and margin (px from that edge).
import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services
import qs.modules.aether

Item {
    id: root

    property var osd
    property string position: "bottom"   // bottom, top, left, right, center, top-left, bottom-right ...
    property int margin: 24

    readonly property var pal: Colours.palette
    readonly property real value: !osd ? 0 : osd.kind === "brightness" ? osd.brightness : osd.kind === "mic" ? osd.sourceVolume : osd.volume
    readonly property bool off: !!osd && ((osd.kind === "volume" && osd.muted) || (osd.kind === "mic" && osd.sourceMuted))

    implicitWidth: 300
    implicitHeight: 48

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Qt.alpha(root.pal.m3surfaceContainer, 0.92)
        border.width: 1
        border.color: Qt.alpha(root.pal.m3outlineVariant, 0.6)

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 18
            spacing: 12

            // the icon in a tonal circle
            Rectangle {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                radius: 17
                color: root.off ? root.pal.m3surfaceContainerHighest : root.pal.m3primaryContainer

                Behavior on color {
                    ColorAnimation { duration: 150 }
                }

                MaterialIcon {
                    anchors.centerIn: parent
                    text: !root.osd ? "volume_up" : root.osd.kind === "brightness" ? "brightness_6" : root.osd.kind === "mic" ? (root.off ? "mic_off" : "mic") : (root.off ? "volume_off" : root.value < 0.34 ? "volume_mute" : root.value < 0.67 ? "volume_down" : "volume_up")
                    color: root.off ? root.pal.m3onSurfaceVariant : root.pal.m3onPrimaryContainer
                    fill: 1
                    fontStyle: Tokens.font.icon.small
                }
            }

            AetherWavy {
                Layout.fillWidth: true
                value: root.off ? 0 : root.value
                colour: root.off ? root.pal.m3onSurfaceVariant : root.pal.m3primary
            }

            StyledText {
                Layout.preferredWidth: 28
                horizontalAlignment: Text.AlignRight
                text: root.off ? "off" : Math.round(root.value * 100)
                color: root.pal.m3onSurface
                font.pointSize: 10.5
                font.weight: 600
            }
        }
    }
}
