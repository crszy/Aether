// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick

// The desktop menu: right-click the desktop (where no window is). It opens where you clicked:
//   your wallpapers in a row on top (click one; the last tile opens all of them), a few quick switches
//   (next wallpaper, night light, Do Not Disturb, gaming mode, lock), then the places you change most
//   (Themes, Wallpaper & colors, the bar, the dock, widgets, monitors) and Settings.
// rice-deskmenu (the "deskmenu-open" file) opens it in the middle of the screen, e.g. from a key.
Scope {
    id: dm
    property bool editing: false              // the widget editor is on: the desktop is busy, no menu
    property bool open: false
    property real px: 0                       // where it was opened (in the screen)
    property real py: 0
    // Material 3 motion, like the control center: t runs evenly in time and every part eases on its own.
    // In: the card unfolds out of the pointer on emphasized-decelerate (a long, soft landing) with a slight
    // spring in size, its corners settle, then the rows come in one after another from below.
    // Out: a short emphasized-accelerate fade that sinks back a little.
    // t is moved by hand, with the length chosen right there (a Behavior with "open ? 900 : 170" sometimes started
    // before its length had switched, so after a few tries the opening ran in 0.17 s)
    property real t: 0
    NumberAnimation { id: tAnim; target: dm; property: "t"; easing.type: Easing.Linear }
    function animTo(v, ms) {
        tAnim.stop()
        tAnim.from = t; tAnim.to = v
        tAnim.duration = Math.max(1, Math.round(ms * Math.abs(v - t)))
        tAnim.start()
    }
    // the window is put on the screen first; the animation starts a moment later, once the compositor shows it.
    // When it has faded out, the window goes away again (nothing over your games)
    property bool shown: false
    Timer { id: goT; interval: 90; onTriggered: if (dm.shown) { dm.open = true; dm.animTo(1, 900) } }
    onTChanged: if (!open && !goT.running && t < 0.005) shown = false
    function quart(p) { p = Math.max(0, Math.min(1, p)); return 1 - Math.pow(1 - p, 4) }
    function back(p) { p = Math.max(0, Math.min(1, p)); const c = 1.25; return 1 + (c + 1) * Math.pow(p - 1, 3) + c * Math.pow(p - 1, 2) }
    function accel(p) { p = Math.max(0, Math.min(1, p)); return p * p * p }
    readonly property real unfold: open ? quart(t / 0.62) : 1                       // 0 a small pill .. 1 all of it
    readonly property real spring: open ? back(t / 0.6) : 0.96 + 0.04 * t     // the size, a little bounce
    readonly property real fade: open ? Math.min(1, t * 6) : t * t
    function stg(i) { return open ? quart((t - 0.16 - i * 0.045) / 0.58) : 1 }
    // the motion of the parts you touch (state layers, switches): unhurried, never snappy
    readonly property int quick: 260
    readonly property int shape: 420

    NPal { id: pal; theme: "shell"; alpha: 0.8 }

    function show(x, y) {
        if (editing) return
        px = x; py = y
        if (shown && open) return
        shown = true
        goT.restart()
        wallsP.running = true
        stateP.running = true
        Sfx.play("click")
    }
    function close() { goT.stop(); if (open) { open = false; animTo(0, 170) } else shown = false }
    // Hyprland: the menu's blur and "no layer animation" rules (desktop-set writes them; once, if they are missing)
    Component.onCompleted: if (Quickshell.env("HYPRLAND_INSTANCE_SIGNATURE"))
        DssWin.run(["bash", "-c", "grep -q rice-deskmenu \"$HOME/.config/hypr/rice-dock.lua\" 2>/dev/null || \"$HOME/.local/bin/desktop-set\" dock-blur"])
    function run(c) { DssWin.run(["bash", "-c", c]); close() }
    function settings(page) { run("\"$HOME/.local/bin/rice-settings\" " + page) }

    // ---------- what it shows ----------
    property var walls: []
    property string current: ""
    // Settings > Wallpaper > Wallpapers from the shell is off: no wallpapers in this menu
    property bool wallsOn: true
    DssProc {
        id: wallsP
        command: ["bash", "-c", "\"$HOME/.local/bin/rice-walls\" list 2>/dev/null"]
        stdout: DssCollector { onStreamFinished: {
            try {
                const j = JSON.parse(text)
                dm.current = j.current || ""
                dm.wallsOn = j.manage !== false
                // the one you have first, then the others (videos too: they are live wallpapers)
                const l = (j.walls || []).slice(0)
                l.sort(function(a, b) { return (b.path === dm.current) - (a.path === dm.current) })
                dm.walls = l.slice(0, 24)
            } catch (e) { }
        } }
    }
    property var st: ({ night: false, dnd: false, gaming: false })
    // a switch flips at once (its animation starts on the click), then the real state is read back
    function flip(k) { const n = Object.assign({}, st); n[k] = !n[k]; st = n }
    DssProc {
        id: stateP
        command: ["bash", "-c", "s=\"$HOME/.local/bin/rice-services\"; echo \"$($s is nightlight) $($s is dnd) $(\"$HOME/.local/bin/rice-effects\" state 2>/dev/null | grep -o '\"gaming\": *true' >/dev/null && echo true || echo false)\""]
        stdout: DssCollector { onStreamFinished: {
            const p = text.trim().split(/\s+/)
            dm.st = { night: p[0] === "true", dnd: p[1] === "true", gaming: p[2] === "true" }
        } }
    }
    function setWall(p) {
        current = p
        DssWin.run(["bash", "-c", "\"$HOME/.local/bin/rice-dashboard\" wall \"$1\"", "_", p])
    }
    Trigger {
        name: "deskmenu-open"
        onFired: function(l) {
            if (dm.shown) { dm.close(); return }
            const s = Mon.screen
            dm.show(s ? s.width / 2 - 170 : 600, s ? s.height / 2 - 260 : 300)
        }
    }

    // ---------- the right-click catcher: the whole desktop, under every window, over the wallpaper ----------
    PanelWindow {
        screen: Mon.screen
        color: "transparent"
        anchors { top: true; bottom: true; left: true; right: true }
        exclusionMode: ExclusionMode.Ignore
        // Bottom: under the windows and under the desktop widgets (made after it), over the wallpaper
        WlrLayershell.layer: WlrLayer.Bottom
        WlrLayershell.namespace: "rice-desktop-click"
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton | Qt.LeftButton
            onPressed: function(m) {
                if (m.button === Qt.RightButton) dm.show(m.x, m.y)
                else if (dm.shown) dm.close()
            }
        }
    }

    // ---- pieces, in the control center's Material 3 look
    // a quick switch: the icon over a short name; on, its shape closes in and it takes your key color
    component Tile: Rectangle {
        id: tl
        property string icon: ""
        property string glyph: ""
        property string label: ""
        property bool on: false
        signal clicked()
        height: 72
        radius: tm.pressed ? 14 : (on ? 22 : 36)
        color: on ? pal.primary : (tm.containsMouse ? pal.cardHi : pal.card)
        scale: tm.pressed ? 0.94 : 1
        Behavior on radius { NumberAnimation { duration: dm.shape; easing.type: Easing.OutBack; easing.overshoot: 1.5 } }
        Behavior on color { ColorAnimation { duration: dm.quick; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Column {
            anchors.centerIn: parent
            spacing: 5
            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 24; height: 24
                MIcon { anchors.centerIn: parent; size: 21; name: tl.icon; glyph: tl.glyph; color: tl.on ? pal.inkPrimary : pal.fg
                    // a small turn and pop when it flips
                    rotation: tl.on ? 0 : -8; scale: tl.on ? 1.08 : 1
                    Behavior on rotation { NumberAnimation { duration: dm.shape; easing.type: Easing.OutBack } }
                    Behavior on scale { NumberAnimation { duration: dm.shape; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                    Behavior on color { ColorAnimation { duration: dm.quick } } }
            }
            Text { anchors.horizontalCenter: parent.horizontalCenter; width: tl.width - 8; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                text: tl.label; color: tl.on ? pal.inkPrimary : pal.fg; font.family: Fonts.main; font.pixelSize: 11; font.weight: Font.Medium
                Behavior on color { ColorAnimation { duration: dm.quick } } }
        }
        MouseArea { id: tm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); tl.clicked() } }
    }
    // a place in Settings: a tonal row with a round icon, a name, a line under it and an arrow
    component Place: Rectangle {
        id: pl
        property string icon: ""
        property string glyph: ""
        property string label: ""
        property string sub: ""
        property bool first: false
        property bool last: false
        signal clicked()
        height: 54
        radius: pm.pressed ? 12 : 18
        color: pm.containsMouse ? pal.cardHi : pal.card
        Behavior on color { ColorAnimation { duration: dm.quick; easing.type: Easing.OutCubic } }
        Behavior on radius { NumberAnimation { duration: dm.shape; easing.type: Easing.OutBack } }
        Rectangle {
            id: pi
            x: 9; anchors.verticalCenter: parent.verticalCenter
            width: 36; height: 36
            radius: pm.containsMouse ? 12 : 18
            color: pm.containsMouse ? pal.primary : pal.container
            Behavior on radius { NumberAnimation { duration: dm.shape; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
            Behavior on color { ColorAnimation { duration: dm.quick } }
            MIcon { anchors.centerIn: parent; size: 18; name: pl.icon; glyph: pl.glyph; color: pm.containsMouse ? pal.inkPrimary : pal.inkContainer
                Behavior on color { ColorAnimation { duration: dm.quick } } }
        }
        Column {
            anchors.left: pi.right; anchors.leftMargin: 12; anchors.right: arrow.left; anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            Text { width: parent.width; elide: Text.ElideRight; text: pl.label; color: pal.fg; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.DemiBold }
            Text { width: parent.width; elide: Text.ElideRight; text: pl.sub; color: pal.dim; font.family: Fonts.main; font.pixelSize: 11 }
        }
        MIcon { id: arrow; anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
            size: 18; name: "chevron_right"; glyph: ""; color: pal.dim
            transform: Translate { x: pm.containsMouse ? 3 : 0; Behavior on x { NumberAnimation { duration: dm.quick; easing.type: Easing.OutCubic } } } }
        MouseArea { id: pm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); pl.clicked() } }
    }

    // ---------- the menu ----------
    PanelWindow {
        id: win
        screen: Mon.screen
        visible: dm.shown
        color: "transparent"
        anchors { top: true; bottom: true; left: true; right: true }
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.namespace: "rice-deskmenu"
        WlrLayershell.keyboardFocus: dm.shown ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
        // niri: frosted glass behind the card once it has settled (gaming mode and Light mode: none, Look.flat)
        NiriBlur { win: win; items: [card]; radius: card.radius; on: dm.open && dm.t > 0.98 }

        // a click anywhere else closes it
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onPressed: dm.close() }
        Item { focus: dm.shown; Keys.onEscapePressed: dm.close() }

        Shadow { x: card.x; y: card.y; width: card.width; height: card.height; radius: card.radius; opacity: card.opacity
            on: Look.shadows && Look.shadowPanels; size: Look.shadowSize; strength: Look.shadowStrength }
        Rectangle {
            id: card
            readonly property real w: 372
            readonly property real fullH: col.implicitHeight + 28
            // it opens where you clicked, and always stays on the screen; near the bottom it opens upwards
            readonly property real baseY: Math.max(10, Math.min(win.height - fullH - 10, dm.py + 4))
            readonly property bool up: baseY < dm.py - 20
            width: w
            // it unfolds from the edge next to the pointer
            height: 64 + (fullH - 64) * dm.unfold
            x: Math.max(10, Math.min(win.width - width - 10, dm.px + 4))
            y: up ? baseY + fullH - height : baseY
            clip: true
            radius: 32 + 8 * (1 - dm.unfold)
            color: pal.surface
            border.width: 1
            border.color: pal.line
            opacity: dm.fade
            transform: Scale {
                origin.x: Math.max(0, Math.min(card.width, dm.px - card.x)); origin.y: card.up ? card.height : 0
                xScale: 0.88 + 0.12 * dm.spring; yScale: 0.92 + 0.08 * dm.spring
            }
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }     // clicks inside do not close it

            Column {
                id: col
                x: 14; y: 14; width: parent.width - 28
                spacing: 8

                // ---- header: the wallpapers, and All
                Item {
                    visible: dm.wallsOn
                    width: parent.width; height: 32
                    opacity: dm.stg(0); transform: Translate { y: (1 - dm.stg(0)) * -10 }
                    Column { x: 6; anchors.verticalCenter: parent.verticalCenter
                        Text { text: Tr.t("Wallpapers"); color: pal.fg; font.family: Fonts.main; font.pixelSize: 15; font.weight: Font.DemiBold }
                    }
                    Rectangle {
                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        width: allRow.implicitWidth + 24; height: 32
                        radius: allM.pressed ? 10 : 16
                        color: allM.containsMouse ? pal.primary : pal.container
                        Behavior on color { ColorAnimation { duration: dm.quick } }
                        Behavior on radius { NumberAnimation { duration: dm.shape; easing.type: Easing.OutBack } }
                        Row { id: allRow; anchors.centerIn: parent; spacing: 4
                            Text { anchors.verticalCenter: parent.verticalCenter; text: Tr.t("All"); color: allM.containsMouse ? pal.inkPrimary : pal.inkContainer
                                font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.DemiBold }
                            MIcon { anchors.verticalCenter: parent.verticalCenter; size: 16; name: "arrow_forward"; glyph: ""; color: allM.containsMouse ? pal.inkPrimary : pal.inkContainer } }
                        MouseArea { id: allM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: { Sfx.play("click"); dm.run("\"$HOME/.local/bin/rice-dashboard\" wallpaper") } }
                    }
                }
                // ---- the wallpapers: the one you have first; the wheel scrolls sideways
                ListView {
                    id: wl
                    visible: dm.wallsOn
                    width: parent.width; height: 72
                    orientation: ListView.Horizontal
                    spacing: 8
                    clip: true
                    model: dm.walls
                    boundsBehavior: Flickable.StopAtBounds
                    property real goal: 0
                    NumberAnimation { id: wlGlide; target: wl; property: "contentX"; duration: 420; easing.type: Easing.OutCubic }
                    WheelHandler { onWheel: function(e) {
                        const max = Math.max(0, wl.contentWidth - wl.width)
                        wl.goal = Math.max(0, Math.min(max, (wlGlide.running ? wl.goal : wl.contentX) - (e.angleDelta.y || e.angleDelta.x) * 0.9))
                        wlGlide.stop(); wlGlide.from = wl.contentX; wlGlide.to = wl.goal; wlGlide.start()
                    } }
                    delegate: Item {
                        id: wt
                        width: 108; height: 72
                        readonly property bool cur: modelData.path === dm.current
                        readonly property real it: dm.stg(1 + Math.min(index, 4) * 0.6)
                        opacity: it
                        transform: Translate { x: (1 - wt.it) * 30 }
                        scale: tm.pressed ? 0.93 : 1
                        Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                        Item {
                            anchors.fill: parent
                            anchors.margins: wt.cur ? 4 : 0
                            Behavior on anchors.margins { NumberAnimation { duration: dm.shape; easing.type: Easing.OutBack } }
                            RoundImage { anchors.fill: parent; source: modelData.thumb ? "file:///" + modelData.thumb : ""; cornerRadius: wt.cur ? 14 : 18 }
                            // a soft state layer while the pointer is on it
                            Rectangle { anchors.fill: parent; radius: wt.cur ? 14 : 18; color: "white"; opacity: tm.containsMouse && !wt.cur ? 0.12 : 0
                                Behavior on opacity { NumberAnimation { duration: dm.quick } } }
                        }
                        Rectangle { anchors.fill: parent; radius: 18; color: "transparent"; border.width: 2.5; border.color: pal.primary
                            opacity: wt.cur ? 1 : 0; Behavior on opacity { NumberAnimation { duration: dm.quick } } }
                        // the one you have: a check that pops in
                        Rectangle {
                            x: parent.width - width - 8; y: 8; width: 20; height: 20; radius: 10; color: pal.primary
                            scale: wt.cur ? 1 : 0
                            Behavior on scale { NumberAnimation { duration: dm.shape; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                            MIcon { anchors.centerIn: parent; size: 13; name: "check"; glyph: ""; color: pal.inkPrimary }
                        }
                        Rectangle { visible: modelData.live === true; x: 8; y: parent.height - 24; width: 34; height: 16; radius: 8; color: Qt.rgba(0, 0, 0, 0.55)
                            Text { anchors.centerIn: parent; text: "LIVE"; color: "white"; font.family: Fonts.main; font.pixelSize: 8; font.weight: Font.Bold } }
                        MouseArea { id: tm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); dm.setWall(modelData.path) } }
                    }
                    Text { visible: dm.walls.length === 0; anchors.centerIn: parent; text: Tr.t("No wallpapers yet"); color: pal.dim; font.family: Fonts.main; font.pixelSize: 12 }
                }

                // ---- quick switches
                Grid {
                    id: tiles
                    width: parent.width
                    columns: 5
                    spacing: 6
                    readonly property real tw: (width - 4 * spacing) / 5
                    Tile { width: tiles.tw; icon: "shuffle"; glyph: ""; label: Tr.t("Next")
                        opacity: dm.stg(2); transform: Translate { y: (1 - dm.stg(2)) * 22 }
                        onClicked: DssWin.run(["bash", "-c", "\"$HOME/.local/bin/rice-walls\" next"]) }
                    Tile { width: tiles.tw; icon: "nightlight"; glyph: ""; label: Tr.t("Night light"); on: dm.st.night
                        opacity: dm.stg(2.4); transform: Translate { y: (1 - dm.stg(2.4)) * 22 }
                        onClicked: { dm.flip("night"); DssWin.run(["bash", "-c", "\"$HOME/.local/bin/rice-services\" toggle nightlight"]); again.restart() } }
                    Tile { width: tiles.tw; icon: "do_not_disturb_on"; glyph: ""; label: Tr.t("Silence"); on: dm.st.dnd
                        opacity: dm.stg(2.8); transform: Translate { y: (1 - dm.stg(2.8)) * 22 }
                        onClicked: { dm.flip("dnd"); DssWin.run(["bash", "-c", "\"$HOME/.local/bin/rice-services\" toggle dnd"]); again.restart() } }
                    Tile { width: tiles.tw; icon: "sports_esports"; glyph: ""; label: Tr.t("Gaming"); on: dm.st.gaming
                        opacity: dm.stg(3.2); transform: Translate { y: (1 - dm.stg(3.2)) * 22 }
                        onClicked: { dm.flip("gaming"); DssWin.run(["bash", "-c", "\"$HOME/.local/bin/rice-effects\" gaming toggle"]); again.restart() } }
                    Tile { width: tiles.tw; icon: "lock"; glyph: ""; label: Tr.t("Lock")
                        opacity: dm.stg(3.6); transform: Translate { y: (1 - dm.stg(3.6)) * 22 }
                        onClicked: dm.run("\"$HOME/.local/bin/rice-services\" session lock") }
                }
                // the real state a moment after a switch (it changes the look of the whole shell first)
                Timer { id: again; interval: 1500; onTriggered: stateP.running = true }

                // ---- places in Settings
                Column {
                    width: parent.width
                    spacing: 4
                    Repeater {
                        model: [
                            { i: "palette", g: "", n: "Themes", d: "A whole look in one click", p: "34" },
                            { i: "wallpaper", g: "", n: "Wallpaper & colors", d: "Animation, slideshow, live wallpapers", p: "18" },
                            { i: "widgets", g: "", n: "Edit widgets", d: "Add, move and resize them", p: "edit" },
                            { i: "toolbar", g: "", n: "Bar", d: "Style, position, modules", p: "2" },
                            { i: "dock_to_bottom", g: "", n: "Dock", d: "Apps on a screen edge", p: "14" },
                            { i: "desktop_windows", g: "", n: "Monitors", d: "Resolution, size, arrangement", p: "28" }
                        ]
                        delegate: Place {
                            width: col.width
                            icon: modelData.i; glyph: modelData.g; label: Tr.t(modelData.n); sub: Tr.t(modelData.d)
                            opacity: dm.stg(4 + index * 0.55); transform: Translate { y: (1 - dm.stg(4 + index * 0.55)) * 26 }
                            onClicked: modelData.p === "edit" ? dm.run("\"$HOME/.local/bin/desktop-set\" edit on") : dm.settings(modelData.p)
                        }
                    }
                }

                // ---- Settings: a filled button in your key color
                Rectangle {
                    width: parent.width; height: 50
                    radius: sm.pressed ? 14 : 25
                    color: sm.containsMouse ? Qt.lighter(pal.primary, 1.07) : pal.primary
                    opacity: dm.stg(7.6); transform: Translate { y: (1 - dm.stg(7.6)) * 26 }
                    Behavior on radius { NumberAnimation { duration: dm.shape; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
                    Behavior on color { ColorAnimation { duration: dm.quick } }
                    Row { anchors.centerIn: parent; spacing: 8
                        MIcon { anchors.verticalCenter: parent.verticalCenter; size: 19; name: "settings"; glyph: ""; color: pal.inkPrimary
                            rotation: sm.containsMouse ? 60 : 0; Behavior on rotation { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } } }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: Tr.t("Settings"); color: pal.inkPrimary; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold } }
                    MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); dm.settings("0") } }
                }
            }
        }
    }
}
