pragma ComponentBehavior: Bound

// The loading screen, drawn at the top of each screen's drawers window (always shown, so the cover is too).
// State: qs.services SplashState. Look: Settings -> Look & feel (style, time, text, colours).
import QtQuick
import QtQuick.Effects
import Quickshell
import Caelestia.Config
import qs.components
import qs.services

Item {
    id: root

    visible: SplashState.showing || SplashState.lingering
    z: 10000

    readonly property var dots: WinSettings.splashColours.length ? WinSettings.splashColours
        : [Colours.palette.m3primary, Colours.palette.m3secondary, Colours.palette.m3tertiary, Colours.palette.m3primaryFixedDim, Colours.palette.m3error]

    readonly property bool aether: WinSettings.splashStyle === "aether"
    readonly property bool mainScreen: ((QsWindow.window as QsWindow)?.screen?.name ?? "") === (Quickshell.screens[0]?.name ?? "")

    Rectangle {
        id: cover

        anchors.fill: parent
        // "aether": the card floats over the desktop like the original splash window (nothing behind it)
        color: WinSettings.splashBackground || (root.aether ? "transparent" : Colours.palette.m3surface)
        opacity: SplashState.showing ? 1 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: 450
                easing.type: Easing.OutCubic
            }
        }

        // "aether": the Aether splash card (main monitor; the others only get the cover)
        AetherSplash {
            anchors.centerIn: parent
            visible: root.aether && root.mainScreen && root.visible
            // the progress line needs time to move like the original (it was made for a 10 s load)
            loadSeconds: Math.max(4, WinSettings.splashMs / 1000)
            onFinished: SplashState.finish()
        }

        // soft glow behind the centre piece
        Rectangle {
            visible: !root.aether
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height) * 0.55
            height: width
            radius: width / 2
            color: Colours.palette.m3primary
            opacity: 0.18
            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 1
                blurMax: 64
            }
            SequentialAnimation on scale {
                loops: Animation.Infinite
                running: root.visible && !root.aether // stop once the loading screen is gone, or the window redraws every frame forever (GPU)
                NumberAnimation { to: 1.12; duration: 1400; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.92; duration: 1400; easing.type: Easing.InOutSine }
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: 28

            // "logo": Caelestia's logo, breathing
            Image {
                visible: WinSettings.splashStyle === "logo"
                anchors.horizontalCenter: parent.horizontalCenter
                source: Quickshell.shellPath("assets/logo.svg")
                sourceSize: Qt.size(160, 160)
                width: 160
                height: 160
                layer.enabled: true
                layer.effect: MultiEffect {
                    colorization: 1
                    colorizationColor: Colours.palette.m3primary
                }
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    running: root.visible // stop once the loading screen is gone, or the window redraws every frame forever (GPU)
                    NumberAnimation { to: 0.55; duration: 700; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutSine }
                }
            }

            // "dots": a row of colour dots hopping in turn (Aether's loading cover)
            Row {
                visible: WinSettings.splashStyle === "dots"
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 18

                Repeater {
                    model: root.dots

                    Rectangle {
                        required property var modelData
                        required property int index

                        width: 22
                        height: 22
                        radius: 11
                        color: modelData

                        SequentialAnimation on y {
                            loops: Animation.Infinite
                            running: root.visible // stop once the loading screen is gone, or the window redraws every frame forever (GPU)
                            PauseAnimation { duration: 110 * index }
                            NumberAnimation { to: -18; duration: 300; easing.type: Easing.OutQuad }
                            NumberAnimation { to: 0; duration: 300; easing.type: Easing.InQuad }
                            PauseAnimation { duration: 110 * (root.dots.length - index) + 200 }
                        }
                    }
                }
            }

            // "minimal": a thin bar sweeping
            Item {
                visible: WinSettings.splashStyle === "minimal"
                anchors.horizontalCenter: parent.horizontalCenter
                width: 220
                height: 4

                Rectangle {
                    anchors.fill: parent
                    radius: 2
                    color: Colours.palette.m3surfaceContainerHighest
                }
                Rectangle {
                    id: sweep
                    width: 70
                    height: 4
                    radius: 2
                    color: Colours.palette.m3primary
                    SequentialAnimation on x {
                        loops: Animation.Infinite
                        running: root.visible // stop once the loading screen is gone, or the window redraws every frame forever (GPU)
                        NumberAnimation { from: 0; to: 150; duration: 700; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 150; to: 0; duration: 700; easing.type: Easing.InOutSine }
                    }
                }
            }

            StyledText {
                visible: !root.aether && WinSettings.splashText.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                text: WinSettings.splashText
                color: WinSettings.splashTextColour || Colours.palette.m3onSurface
                font.pixelSize: 26
                font.weight: Font.Medium
                font.letterSpacing: 4
            }
        }
    }
}
