// Aether shell (AGPL-3.0-or-later). The wallpaper picker (Super+W, or the desktop menu).
// A panel in the DSS / Caelestia style: the picked wallpaper large on the left, every wallpaper as a tile on the
// right. Press the key again (or the arrows, or the wheel) to move the pick: the ring glides to it, and the faster
// you press, the faster it glides. Hold the key and every motion freezes until you let go. Hovering a tile fades
// it up (the only motion a tile has of its own); clicking a tile or pressing Enter uses that wallpaper at once,
// no animation. Esc (or a click outside) closes and puts the old wallpaper back. The desktop previews the pick.
pragma ComponentBehavior: Bound
import QtQuick
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
    readonly property int cols: 4

    property int sel: 0           // the picked tile
    property real pos: 0          // where the ring is now, chasing sel
    property real t: 0            // opening, 0..1
    property real tau: 0.13       // how quickly the ring chases sel (s); shorter while the key is spammed
    property int shown: -1        // the wallpaper in the big preview
    property int shownOld: -1     // the one fading out under it
    property real fade: 1         // the preview cross-fade, 0..1
    property bool hovering: false // the mouse is over the panel (tiles may be fading)
    property real hoverLeft: 0    // seconds of hover fade still to finish after the mouse leaves
    property bool scrollBusy: false
    signal tick(real dt)

    // the clock only runs while something moves (and never while the key is held)
    readonly property bool busy: !held && (Math.abs(sel - pos) > 0.001 || t < 0.999 || fade < 0.999 || hovering || hoverLeft > 0 || scrollBusy)

    function name(i) {
        return i < 0 || i >= n ? "" : String(walls[i].path).split("/").pop().replace(/\.[^.]+$/, "");
    }
    function step(d, interval) {
        if (n === 0)
            return;
        // the glide takes about as long as the gap between presses
        tau = interval > 0 ? Math.max(0.02, Math.min(0.13, interval / 1000 * 0.28)) : 0.13;
        let s = sel + d;
        if (s < 0 || s >= n) {    // round the end: jump straight there, no long glide back
            s = ((s % n) + n) % n;
            pos = s;
        }
        sel = s;
        calmT.restart();
        previewT.restart();
        if (shown !== sel) {
            shownOld = shown;
            shown = sel;
            fade = shownOld < 0 ? 1 : 0;
        }
    }
    function use(i) {
        if (n === 0)
            return;
        Wallpapers.setWallpaper(walls[i].path);
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
        shown = sel;
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
        onTriggered: if (wp.n > 0) Wallpapers.preview(wp.walls[wp.sel].path)
    }

    // one clock drives every motion, so holding the key freezes them all
    FrameAnimation {
        running: wp.busy
        onTriggered: {
            const dt = Math.min(0.05, frameTime);
            wp.pos += (wp.sel - wp.pos) * (1 - Math.exp(-dt / wp.tau));
            if (Math.abs(wp.sel - wp.pos) < 0.001)
                wp.pos = wp.sel;
            wp.t += (1 - wp.t) * (1 - Math.exp(-dt / 0.09));
            if (wp.t > 0.999)
                wp.t = 1;
            wp.fade += (1 - wp.fade) * (1 - Math.exp(-dt / Math.max(0.05, wp.tau)));
            if (wp.fade > 0.999)
                wp.fade = 1;
            wp.hoverLeft = Math.max(0, wp.hoverLeft - dt);
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

        // the dim behind; a click on it cancels
        Rectangle {
            anchors.fill: parent
            color: "#000000"
            opacity: 0.45 * wp.t
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
                const rep = e.isAutoRepeat ? 60 : 0;
                if (e.key === Qt.Key_Escape)
                    wp.cancel();
                else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space)
                    wp.use(wp.sel);
                else if (e.key === Qt.Key_Left)
                    wp.step(-1, rep);
                else if (e.key === Qt.Key_Right || e.key === Qt.Key_Tab)
                    wp.step(1, rep);
                else if (e.key === Qt.Key_Up)
                    wp.step(-wp.cols, rep);
                else if (e.key === Qt.Key_Down)
                    wp.step(wp.cols, rep);
                else
                    return;
                e.accepted = true;
            }

            Item {
                id: panel
                width: Math.min(1180, stage.width - 120)
                height: Math.min(600, stage.height - 160)
                anchors.horizontalCenter: parent.horizontalCenter
                y: (stage.height - height) / 2 + (1 - wp.t) * 40
                opacity: wp.t
                scale: 0.96 + 0.04 * wp.t

                HoverHandler {
                    onHoveredChanged: {
                        wp.hovering = hovered;
                        if (!hovered)
                            wp.hoverLeft = 0.4;
                    }
                }
                MouseArea {    // eats clicks on the panel so they don't cancel
                    anchors.fill: parent
                    onWheel: w => wp.step(w.angleDelta.y > 0 ? -1 : 1, 0)
                }

                // soft shadow: three plain layers (cheaper than a blur effect)
                Repeater {
                    model: 3
                    Rectangle {
                        required property int index
                        anchors.fill: parent
                        anchors.leftMargin: -(index + 1) * 6
                        anchors.rightMargin: -(index + 1) * 6
                        anchors.topMargin: -(index + 1) * 4
                        anchors.bottomMargin: -(index + 1) * 9
                        radius: 30 + (index + 1) * 6
                        color: "#000000"
                        opacity: 0.11 - index * 0.03
                    }
                }

                Rectangle {
                    id: card
                    anchors.fill: parent
                    radius: 30
                    color: AetherStyle.surface
                    border.width: 1
                    border.color: Qt.alpha(AetherStyle.outlineVar, 0.55)

                    // header
                    Row {
                        x: 26
                        y: 20
                        spacing: 12
                        Rectangle {
                            width: 40
                            height: 40
                            radius: 14
                            color: Qt.alpha(AetherStyle.primary, 0.16)
                            Text {
                                anchors.centerIn: parent
                                font.family: "Material Symbols Rounded"
                                font.pixelSize: 22
                                text: "wallpaper"
                                color: AetherStyle.primary
                            }
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                text: "Wallpapers"
                                color: AetherStyle.textMain
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: Paths.shortenHome(Paths.wallsdir)
                                color: AetherStyle.textSub
                                font.pixelSize: 12
                            }
                        }
                    }
                    Rectangle {    // "3 of 24"
                        anchors.right: parent.right
                        anchors.rightMargin: 26
                        y: 26
                        height: 28
                        width: chipT.implicitWidth + 24
                        radius: 14
                        color: AetherStyle.palette.m3surfaceContainerHigh
                        Text {
                            id: chipT
                            anchors.centerIn: parent
                            text: wp.held ? "Paused" : (wp.n === 0 ? "Empty" : (wp.sel + 1) + " of " + wp.n)
                            color: wp.held ? AetherStyle.primary : AetherStyle.textMain
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }

                    // ---- the big preview ----
                    ClippingRectangle {
                        id: big
                        x: 26
                        y: 80
                        width: Math.round(card.width * 0.46)
                        height: card.height - y - 52
                        radius: 22
                        color: AetherStyle.palette.m3surfaceContainerHigh
                        Image {
                            anchors.fill: parent
                            source: wp.shownOld >= 0 && wp.shownOld < wp.n ? wp.walls[wp.shownOld].path : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 1280
                            visible: wp.fade < 1
                        }
                        Image {
                            anchors.fill: parent
                            source: wp.shown >= 0 && wp.shown < wp.n ? wp.walls[wp.shown].path : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 1280
                            opacity: wp.fade
                            scale: 1.04 - 0.04 * wp.fade    // settles in as it fades up
                        }
                        Rectangle {    // soft gradient under the name
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 90
                            gradient: Gradient {
                                GradientStop { position: 0; color: "transparent" }
                                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.6) }
                            }
                        }
                        Text {
                            x: 18
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 14
                            width: parent.width - 36
                            elide: Text.ElideRight
                            text: wp.n === 0 ? "No wallpapers yet" : wp.name(wp.shown)
                            color: "#ffffff"
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: wp.use(wp.sel)
                        }
                    }

                    // ---- the tiles ----
                    Flickable {
                        id: fl
                        anchors.left: big.right
                        anchors.leftMargin: 18
                        anchors.right: parent.right
                        anchors.rightMargin: 26
                        y: big.y
                        height: big.height
                        clip: true
                        contentHeight: grid.height
                        boundsBehavior: Flickable.StopAtBounds
                        readonly property real gap: 12
                        readonly property real tw: (width - gap * (wp.cols - 1)) / wp.cols
                        readonly property real th: tw * 0.62
                        function want() {    // keep the picked row in view
                            const row = Math.floor(wp.sel / wp.cols) * (th + gap);
                            if (row < contentY)
                                return row;
                            if (row + th > contentY + height)
                                return row + th - height;
                            return contentY;
                        }
                        Connections {
                            target: wp
                            function onTick(dt) {
                                if (fl.moving)
                                    return;
                                const w = fl.want();
                                const d = w - fl.contentY;
                                fl.contentY = Math.abs(d) < 0.5 ? w : fl.contentY + d * (1 - Math.exp(-dt / Math.max(0.04, wp.tau)));
                                wp.scrollBusy = Math.abs(fl.want() - fl.contentY) > 0.5;
                            }
                            function onSelChanged() {
                                wp.scrollBusy = Math.abs(fl.want() - fl.contentY) > 0.5;
                            }
                        }

                        Item {
                            id: grid
                            width: fl.width
                            height: Math.ceil(wp.n / wp.cols) * (fl.th + fl.gap)

                            Repeater {
                                model: wp.walls
                                Item {
                                    id: tile
                                    required property var modelData
                                    required property int index
                                    readonly property bool picked: index === wp.sel
                                    property real hv: 0
                                    x: (index % wp.cols) * (fl.tw + fl.gap)
                                    y: Math.floor(index / wp.cols) * (fl.th + fl.gap)
                                    width: fl.tw
                                    height: fl.th
                                    Connections {
                                        target: wp
                                        function onTick(dt) {
                                            tile.hv += ((tm.containsMouse ? 1 : 0) - tile.hv) * (1 - Math.exp(-dt / 0.08));
                                        }
                                    }
                                    ClippingRectangle {
                                        anchors.fill: parent
                                        radius: 14
                                        color: AetherStyle.palette.m3surfaceContainerHigh
                                        Image {
                                            anchors.fill: parent
                                            source: tile.modelData.path
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            sourceSize.width: 320
                                        }
                                        Rectangle {    // the hover fade
                                            anchors.fill: parent
                                            color: "#000000"
                                            opacity: (tile.picked ? 0 : 0.38) * (1 - tile.hv)
                                        }
                                    }
                                    MouseArea {
                                        id: tm
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: wp.use(tile.index)
                                    }
                                }
                            }

                            // the ring: glides between tiles at the pace of the key presses
                            Rectangle {
                                readonly property int a: Math.floor(wp.pos)
                                readonly property real f: wp.pos - a
                                readonly property real ax: (a % wp.cols) * (fl.tw + fl.gap)
                                readonly property real ay: Math.floor(a / wp.cols) * (fl.th + fl.gap)
                                readonly property real bx: ((a + 1) % wp.cols) * (fl.tw + fl.gap)
                                readonly property real by: Math.floor((a + 1) / wp.cols) * (fl.th + fl.gap)
                                visible: wp.n > 0
                                x: ax + (bx - ax) * f - 4
                                y: ay + (by - ay) * f - 4
                                width: fl.tw + 8
                                height: fl.th + 8
                                radius: 18
                                color: "transparent"
                                border.width: 3
                                border.color: AetherStyle.primary
                            }
                        }
                    }

                    Text {    // hint
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 18
                        text: wp.held ? "Paused · let go to carry on"
                            : (wp.n === 0 ? "Put pictures in " + Paths.shortenHome(Paths.wallsdir) + ", then open this again"
                            : "Super+W again for the next  ·  arrows to move  ·  hold to pause  ·  Enter or click to use  ·  Esc to cancel")
                        color: AetherStyle.textSub
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
