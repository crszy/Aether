pragma ComponentBehavior: Bound

// Your own volume / brightness pop-up: windows.json "mixOsd": "user:<Name>" = ~/.config/caelestia/user/osd/<Name>.qml
// It shows when the volume, the mic or the brightness changes and hides after a moment, on the main monitor
// ("Popups on all monitors" on: every monitor). Clicks only land on the pop-up itself.
// The file gets `property var osd` (osd.kind = "volume" | "mic" | "brightness", osd.volume, osd.muted,
// osd.sourceVolume, osd.sourceMuted, osd.brightness - all 0..1) and may set
// `property string position` ("bottom" | "top" | "left" | "right" | "center" | "top-left" ..., default "bottom")
// and `property int margin` (px from the edge, default 80).
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.components.containers
import qs.services

Scope {
    id: scope

    readonly property string name: UserExt.userName(WinSettings.mixOsd)

    QtObject {
        id: osdState

        property string kind: "volume"
        property real volume: Audio.volume
        property bool muted: Audio.muted
        property real sourceVolume: Audio.sourceVolume
        property bool sourceMuted: Audio.sourceMuted
        property real brightness: Brightness.getMonitorForScreen(Quickshell.screens[0])?.brightness ?? 0
        property bool shown: false
        property bool alive: false      // the window exists only while showing (and fading out)
    }

    function pop(kind: string): void {
        if (scope.name === "")
            return;
        osdState.kind = kind;
        osdState.alive = true;
        osdState.shown = true;
        dieTimer.stop();
        hideTimer.restart();
    }

    Timer {
        id: hideTimer

        interval: 1800
        onTriggered: {
            osdState.shown = false;
            dieTimer.restart();
        }
    }

    Timer {
        id: dieTimer

        interval: 400
        onTriggered: osdState.alive = false
    }

    Connections {
        target: Audio

        function onVolumeChanged(): void {
            scope.pop("volume");
        }

        function onMutedChanged(): void {
            scope.pop("volume");
        }

        function onSourceVolumeChanged(): void {
            scope.pop("mic");
        }

        function onSourceMutedChanged(): void {
            scope.pop("mic");
        }
    }

    Connections {
        target: Brightness.getMonitorForScreen(Quickshell.screens[0]) ?? null
        ignoreUnknownSignals: true

        function onBrightnessChanged(): void {
            scope.pop("brightness");
        }
    }

    Variants {
        model: scope.name === "" || !osdState.alive ? [] : WinSettings.osdAllMonitors ? Quickshell.screens : Quickshell.screens.slice(0, 1)

        StyledWindow {
            id: win

            required property ShellScreen modelData

            screen: modelData
            name: "aether-user-osd"
            exclusionMode: ExclusionMode.Ignore
            color: "transparent"
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
            mask: Region {
                item: osdState.shown ? piece : null
            }

            anchors.top: true
            anchors.bottom: true
            anchors.left: true
            anchors.right: true

            readonly property string pos: String(piece.item?.position ?? "bottom")
            readonly property int margin: Number(piece.item?.margin ?? 80)

            UserPiece {
                id: piece

                kind: "osd"
                name: scope.name
                context: ({ osd: osdState, screen: win.modelData })
                opacity: osdState.shown ? 1 : 0

                x: win.pos.includes("left") ? win.margin : win.pos.includes("right") ? parent.width - width - win.margin : (parent.width - width) / 2
                y: win.pos.includes("top") ? win.margin : win.pos.includes("bottom") || win.pos === "" ? parent.height - height - win.margin : (parent.height - height) / 2
                width: implicitWidth
                height: implicitHeight

                // slides in from the screen edge it sits on (like real rices' panels), no bounce: M3 emphasized
                // decelerate in, accelerate out. Centred = a short rise.
                readonly property real awayX: win.pos.includes("left") ? -(width + win.margin) : win.pos.includes("right") ? width + win.margin : 0
                readonly property real awayY: win.pos.includes("top") ? -(height + win.margin) : win.pos.includes("bottom") || win.pos === "" ? height + win.margin : (awayX === 0 ? 24 : 0)
                transform: Translate {
                    x: osdState.shown ? 0 : piece.awayX
                    y: osdState.shown ? 0 : piece.awayY

                    Behavior on x {
                        NumberAnimation {
                            duration: osdState.shown ? 240 : 200
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: osdState.shown ? [0.05, 0.7, 0.1, 1, 1, 1] : [0.3, 0, 0.8, 0.15, 1, 1]
                        }
                    }
                    Behavior on y {
                        NumberAnimation {
                            duration: osdState.shown ? 240 : 200
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: osdState.shown ? [0.05, 0.7, 0.1, 1, 1, 1] : [0.3, 0, 0.8, 0.15, 1, 1]
                        }
                    }
                }

                Behavior on opacity {
                    NumberAnimation {
                        duration: 160
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
    }

    // try your pop-up without touching the volume: qs ipc call userosd preview volume|mic|brightness
    IpcHandler {
        function preview(kind: string): void {
            scope.pop(kind || "volume");
        }

        target: "userosd"
    }
}
