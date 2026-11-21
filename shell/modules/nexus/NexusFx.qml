pragma ComponentBehavior: Bound

// Animated background of the Settings window. Settings -> Look & feel: style ("aurora", "particles", "waves",
// "none"), strength and speed. Colours follow the current scheme.
import QtQuick
import QtQuick.Effects
import Caelestia.Config
import Caelestia.Multimedia
import qs.services

Item {
    id: root

    readonly property string style: WinSettings.nexusFx
    readonly property real strength: WinSettings.nexusFxStrength
    readonly property real speed: Math.max(0.1, WinSettings.nexusFxSpeed)
    readonly property var tints: [Colours.palette.m3primary, Colours.palette.m3tertiary, Colours.palette.m3secondary, Colours.palette.m3primaryContainer]

    visible: style !== "none" && strength > 0
    clip: true

    // ---- video: your video / picture (Settings > Look & feel), else the wallpaper ----
    Visual {
        anchors.fill: parent
        visible: root.style === "video"
        source: visible ? (WinSettings.riceBackground || Wallpapers.current) : ""
        playing: visible && root.Window.window?.visible === true && !WinSettings.saving
        opacity: root.strength
    }

    // ---- aurora: big soft blobs drifting on slow Lissajous paths ----
    Item {
        id: aurora

        anchors.fill: parent
        visible: root.style === "aurora"
        opacity: root.strength
        layer.enabled: visible
        layer.effect: MultiEffect {
            blurEnabled: true
            blur: 1
            blurMax: 64
            saturation: 0.2
        }

        Repeater {
            model: 4

            Rectangle {
                id: blob

                required property int index
                property real t: 0

                width: root.width * (0.55 + 0.1 * index)
                height: width
                radius: width / 2
                color: root.tints[index % root.tints.length]
                opacity: 0.55
                x: root.width * (0.5 + 0.42 * Math.sin(t * (0.7 + 0.13 * index) + index * 1.7)) - width / 2
                y: root.height * (0.5 + 0.38 * Math.cos(t * (0.5 + 0.11 * index) + index * 2.3)) - height / 2

                NumberAnimation on t {
                    running: aurora.visible
                    from: 0
                    to: Math.PI * 2 * 20
                    duration: 20 * 60000 / root.speed
                    loops: Animation.Infinite
                }
            }
        }
    }

    // ---- particles: small glowing dots rising ----
    Item {
        anchors.fill: parent
        visible: root.style === "particles"
        opacity: root.strength

        Repeater {
            model: 46

            Rectangle {
                id: dot

                required property int index
                readonly property real seed: (index * 9301 + 49297) % 233280 / 233280

                width: 3 + seed * 6
                height: width
                radius: width / 2
                color: root.tints[index % root.tints.length]
                opacity: 0.25 + 0.5 * seed
                x: root.width * ((seed * 7.31) % 1)

                NumberAnimation on y {
                    running: dot.visible
                    from: root.height + 10
                    to: -20
                    duration: (9000 + 14000 * dot.seed) / root.speed
                    loops: Animation.Infinite
                }
            }
        }
    }

    // ---- waves: a few wide, soft bands sliding ----
    Item {
        anchors.fill: parent
        visible: root.style === "waves"
        opacity: root.strength
        layer.enabled: visible
        layer.effect: MultiEffect {
            blurEnabled: true
            blur: 0.8
            blurMax: 48
        }

        Repeater {
            model: 3

            Rectangle {
                id: band

                required property int index
                property real t: 0

                width: root.width * 1.6
                height: root.height * 0.22
                radius: height / 2
                rotation: -12 + index * 9
                color: root.tints[index % root.tints.length]
                opacity: 0.5
                x: -root.width * 0.3 + Math.sin(t + index) * root.width * 0.15
                y: root.height * (0.2 + 0.28 * index) + Math.cos(t * 0.8 + index * 2) * root.height * 0.06

                NumberAnimation on t {
                    running: band.visible
                    from: 0
                    to: Math.PI * 2 * 20
                    duration: 20 * 26000 / root.speed
                    loops: Animation.Infinite
                }
            }
        }
    }
}
