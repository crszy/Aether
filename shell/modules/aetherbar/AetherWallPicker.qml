// Aether shell (AGPL-3.0-or-later). The wallpaper picker (Super+W, or the desktop menu).
// A looping carousel of your wallpapers. Press the key again (or Left / Right, or the wheel) for the next one:
// the cards glide, and the faster you press, the faster they glide. Hold the key and every motion freezes
// until you let go. Hovering a card fades it up; clicking a card or pressing Enter uses the wallpaper at once
// (no animation); Esc closes and puts the old wallpaper back. The desktop previews the picked one as you go.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import Quickshell
import Quickshell.Wayland
import Quickshell.Widgets
import qs.services
import qs.utils

Scope {
    id: wp

    readonly property var cfg: AetherBarConfig
    readonly property var walls: Wallpapers.list
    readonly property int n: walls.length
    readonly property bool held: cfg.pickerHeld

    property real sel: 0          // the picked card, unbounded (the carousel loops)
    property real pos: 0          // where the carousel is now, chasing sel
    property real t: 0            // opening
    property real tau: 0.13       // how quickly pos chases sel (seconds); shorter while the key is spammed
    signal tick(real dt)

    function idx(v) {
        return n > 0 ? ((Math.round(v) % n) + n) % n : 0;
    }
    function delta(i) {           // how far card i is from the middle, wrapped round the loop
        if (n === 0)
            return 0;
        let d = (i - pos) % n;
        if (d > n / 2)
            d -= n;
        if (d < -n / 2)
            d += n;
        return d;
    }
    function step(dir, interval) {
        if (n === 0)
            return;
        // the glide takes about as long as the gap between presses
        tau = interval > 0 ? Math.max(0.02, Math.min(0.13, interval / 1000 * 0.28)) : 0.13;
        sel += dir;
        calmT.restart();
        previewT.restart();
    }
    function use(i) {
        if (n === 0)
            return;
        Wallpapers.setWallpaper(walls[idx(i)].path);
        cfg.pickerOpen = false;
    }
    function cancel() {
        Wallpapers.stopPreview();
        cfg.pickerOpen = false;
    }

    Component.onCompleted: {
        const cur = Wallpapers.norm(Wallpapers.actualCurrent);
        for (let i = 0; i < n; i++)
            if (Wallpapers.norm(walls[i].path) === cur) {
                sel = i;
                break;
            }
        pos = sel;
    }

    Connections {
        target: wp.cfg
        function onPickerAdvance(interval) {
            wp.step(1, interval);
        }
    }
    Timer {
        id: calmT
        interval: 700
        onTriggered: wp.tau = 0.13
    }
    Timer {
        id: previewT
        interval: 220
        onTriggered: if (wp.n > 0) Wallpapers.preview(wp.walls[wp.idx(wp.sel)].path)
    }

    // one clock drives every motion, so holding the key can freeze them all
    FrameAnimation {
        running: true
        onTriggered: {
            if (wp.held)
                return;
            const dt = Math.min(0.05, frameTime);
            wp.pos += (wp.sel - wp.pos) * (1 - Math.exp(-dt / wp.tau));
            if (Math.abs(wp.sel - wp.pos) < 0.001)
                wp.pos = wp.sel;
            wp.t += (1 - wp.t) * (1 - Math.exp(-dt / 0.09));
            wp.tick(dt);
        }
    }

    PanelWindow {
        id: win
        screen: wp.cfg.dashScreen || Quickshell.screens[0]
        color: "transparent"
        anchors { top: true; left: true; right: true; bottom: true }
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.namespace: "aether-wallpicker"
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive

        Rectangle {
            anchors.fill: parent
            color: "#000000"
            opacity: 0.4 * wp.t
            MouseArea {
                anchors.fill: parent
                onClicked: wp.cancel()
                onWheel: w => wp.step(w.angleDelta.y > 0 ? -1 : 1, 0)
            }
        }

        Item {
            id: stage
            anchors.fill: parent
            focus: true
            Keys.onPressed: e => {
                if (e.key === Qt.Key_Escape)
                    wp.cancel();
                else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space)
                    wp.use(wp.sel);
                else if (e.key === Qt.Key_Left)
                    wp.step(-1, e.isAutoRepeat ? 60 : 0);
                else if (e.key === Qt.Key_Right)
                    wp.step(1, e.isAutoRepeat ? 60 : 0);
                else
                    return;
                e.accepted = true;
            }

            readonly property real cw: Math.min(520, width * 0.32)
            readonly property real ch: cw * 0.6
            readonly property real cy: height * 0.5 + (1 - wp.t) * 60

            Repeater {
                model: wp.walls
                Item {
                    id: card
                    required property var modelData
                    required property int index
                    readonly property real d: wp.delta(index)
                    readonly property real ad: Math.abs(d)
                    property real hv: 0
                    visible: ad < 3.6
                    width: stage.cw
                    height: stage.ch
                    x: stage.width / 2 - width / 2 + Math.sign(d) * Math.pow(ad, 0.85) * stage.cw * 0.62
                    y: stage.cy - height / 2
                    z: 100 - ad * 10
                    scale: (1 - Math.min(ad, 3) * 0.17) * (0.85 + 0.15 * wp.t)
                    opacity: Math.max(0, Math.min(1, 1.25 - ad * 0.32)) * wp.t
                    transform: Rotation {
                        origin.x: card.width / 2
                        origin.y: card.height / 2
                        axis { x: 0; y: 1; z: 0 }
                        angle: Math.max(-40, Math.min(40, -card.d * 22))
                    }
                    Connections {
                        target: wp
                        function onTick(dt) {
                            card.hv += ((cm.containsMouse ? 1 : 0) - card.hv) * (1 - Math.exp(-dt / 0.08));
                        }
                    }

                    ClippingRectangle {
                        id: pic
                        anchors.fill: parent
                        radius: 24
                        color: AetherStyle.palette.m3surfaceContainerHigh
                        Image {
                            anchors.fill: parent
                            source: card.modelData.path
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 640
                        }
                        // the hover fade: the only motion a card has of its own
                        Rectangle {
                            anchors.fill: parent
                            color: "#000000"
                            opacity: (card.ad < 0.5 ? 0 : 0.35) * (1 - card.hv)
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 24
                        color: "transparent"
                        border.width: card.ad < 0.5 ? 3 : 0
                        border.color: AetherStyle.primary
                    }
                    MouseArea {
                        id: cm
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: wp.use(card.index)
                        onWheel: w => wp.step(w.angleDelta.y > 0 ? -1 : 1, 0)
                    }
                }
            }

            // the picked wallpaper's name and a hint
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                y: stage.cy + stage.ch / 2 + 28
                spacing: 6
                opacity: wp.t
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: wp.n === 0 ? "No wallpapers in " + Paths.shortenHome(Paths.wallsdir) : String(wp.walls[wp.idx(wp.sel)].path).split("/").pop().replace(/\.[^.]+$/, "")
                    color: "#ffffff"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: wp.held ? "Paused · let go to carry on"
                        : (wp.n === 0 ? "Put pictures in that folder, then open this again" : (wp.idx(wp.sel) + 1) + " of " + wp.n + "  ·  Super+W again for the next  ·  hold to pause  ·  Enter to use  ·  Esc to cancel")
                    color: Qt.rgba(1, 1, 1, 0.7)
                    font.pixelSize: 12
                }
            }
        }
    }
}
