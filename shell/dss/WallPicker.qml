// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick
import QtQuick.Effects

// SUPER + W: your wallpapers on a Material 3 hero carousel. The one in focus is big in the middle, the ones next to
// it get narrower the farther they are, so you see a lot of them without a wall of pictures. Wheel, arrows or a
// click move the focus; Enter (or a click on the big one) sets it. Type to search; Tab goes through All, Pictures
// and Live. Esc or a click outside closes it.
// Light on memory: only 15 cards are ever made (they show whatever is near the focus), the pictures are the small
// previews of rice-walls, and only the big card loads the wallpaper itself (sharp). The cards are cut round by one
// mask for all of them, not one graphics effect per card.
// Settings > Wallpaper can turn this picker off, or the shell's wallpapers altogether (rice-walls picker|manage).
PanelWindow {
    screen: Mon.screen
    id: wp
    property bool open: false
    property real t: 0
    Behavior on t { NumberAnimation { duration: wp.open ? (Look.lite ? 320 : 620) : 220; easing.type: Easing.Linear } }
    function ease(p) { return 1 - Math.pow(1 - Math.max(0, Math.min(1, p)), 4) }
    function clamp01(p) { return Math.max(0, Math.min(1, p)) }
    readonly property real et: ease(t)
    onOpenChanged: {
        t = open ? 1 : 0
        if (open) { query = ""; search.text = ""; kind = "all"; centered = false; list.running = true; focusT.restart() }
    }
    NPal { id: pal; theme: "shell"; alpha: 0.94 }

    visible: open || (t > 0.01 && !closeT.done)
    // if the closing animation ever stalls (the compositor sent no frames for a moment), it still goes away
    Timer { id: closeT; property bool done: false; interval: 600; running: !open; onRunningChanged: if (running) done = false; onTriggered: done = true }
    NiriBlur { win: wp; items: [bar]; radius: bar.radius; on: wp.open && wp.et > 0.97 }
    color: "transparent"
    anchors { top: true; left: true; right: true; bottom: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-walls"
    WlrLayershell.keyboardFocus: open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None

    Trigger { name: "walls-open"; fresh: true; onFired: function(l) { wp.open = !wp.open } }

    // ---------- the wallpapers ----------
    property var all: []
    property string listText: ""
    property string current: ""
    property int dirs: 1
    property bool loaded: false
    property string query: ""
    property string kind: "all"                       // all | pics | live
    readonly property int nLive: all.filter(function(w) { return w.live === true }).length
    readonly property var shown: {
        const q = query.toLowerCase()
        return all.filter(function(w) {
            if (wp.kind === "pics" && w.live === true) return false
            if (wp.kind === "live" && w.live !== true) return false
            return q === "" || w.name.toLowerCase().indexOf(q) >= 0
        })
    }
    property int sel: 0
    // where the carousel is (it glides to `sel`); `snap` puts it there at once (opening, a new search)
    property real pos: 0
    property bool snap: true
    Behavior on pos { enabled: !wp.snap; NumberAnimation { duration: Look.lite ? 220 : 460; easing.type: Easing.OutQuint } }
    function go(i, now) {
        if (shown.length === 0) { sel = 0; return }
        sel = Math.max(0, Math.min(shown.length - 1, i))
        snap = !!now; pos = sel; snap = false
    }
    property bool centered: false
    function center() {
        for (let i = 0; i < shown.length; i++) if (shown[i].path === current) { go(i, true); return }
        go(0, true)
    }
    onShownChanged: { if (centered) go(Math.min(sel, shown.length - 1), true) }
    onQueryChanged: if (open) { go(0, true); ftA.restart() }
    onKindChanged: if (open) { centered = false; center(); centered = true; ftA.restart() }
    // the list changed (you typed, another chip): the cards come in again
    property real ft: 1
    NumberAnimation { id: ftA; target: wp; property: "ft"; from: 0; to: 1; duration: Look.lite ? 260 : 480; easing.type: Easing.Linear }

    DssProc {
        id: list
        command: ["bash", "-c", "\"$HOME/.local/bin/rice-walls\" list"]
        stdout: DssCollector { onStreamFinished: {
            // only when something changed: a new list makes every card load its picture again
            if (text !== wp.listText) {
                try {
                    const j = JSON.parse(text)
                    wp.listText = text
                    wp.all = j.walls || []; wp.current = j.current || ""; wp.dirs = (j.dirs || []).length
                } catch (e) { }
            }
            wp.loaded = true
            if (!wp.centered) { wp.center(); wp.centered = true }
        } }
    }
    // previews are made in the background the first time: look again a bit later (only while some are missing)
    readonly property bool missing: all.some(function(w) { return !w.thumb || w.thumb === w.path })
    Timer { interval: 3000; running: wp.open && wp.missing; repeat: true; onTriggered: if (!list.running) list.running = true }
    function apply(path) {
        current = path
        Sfx.play("click")
        pulse.restart()
        DssWin.run(["bash", "-c", "\"$HOME/.local/bin/rice-dashboard\" wall \"$1\"", "_", path])
    }
    function shuffle() { if (shown.length > 1) { let i = sel; while (i === sel) i = Math.floor(Math.random() * shown.length); go(i); apply(shown[i].path) } }
    Timer { id: focusT; interval: 60; onTriggered: search.forceActiveFocus() }
    // the wheel: one wallpaper per notch; a touchpad adds up until it is one
    property real wheelAcc: 0
    function wheel(dx) {
        wheelAcc += dx
        while (Math.abs(wheelAcc) >= 120) { const s = wheelAcc > 0 ? 1 : -1; go(sel + s); wheelAcc -= 120 * s }
    }

    // ---------- the scrim: a click outside closes ----------
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.5 * wp.et)
        MouseArea { anchors.fill: parent; onClicked: wp.open = false
            onWheel: function(e) { wp.wheel(-(e.angleDelta.y !== 0 ? e.angleDelta.y : e.angleDelta.x)) } }
    }

    // ---------- the carousel ----------
    // hero card H wide; the next ones L1, then thin strips S (all as tall as the hero, a little shorter farther out)
    readonly property real carH: Math.min(height * 0.5, 470)
    readonly property real heroW: Math.min(width * 0.5, carH * 1.62)
    readonly property real l1W: heroW * 0.36
    readonly property real stripW: Math.max(40, heroW * 0.12)
    readonly property real gap: 10
    readonly property int reach: 7                       // cards on each side of the focus
    // the middle of the card `a` places from the focus, and its width (between whole places: in between)
    function anchorC(a) {
        if (a <= 0) return 0
        const c1 = heroW / 2 + gap + l1W / 2
        if (a <= 1) return c1
        return c1 + l1W / 2 + gap + stripW / 2 + (a - 2) * (stripW + gap)
    }
    function anchorW(a) { return a <= 0 ? heroW : (a <= 1 ? l1W : stripW) }
    function lerp(f, a, b) { return a + (b - a) * f }
    function cx(d) { const ad = Math.abs(d), a = Math.floor(ad), f = ad - a; return (d < 0 ? -1 : 1) * lerp(f, anchorC(a), anchorC(a + 1)) }
    function cw(d) { const ad = Math.abs(d), a = Math.floor(ad), f = ad - a; return lerp(f, anchorW(a), anchorW(a + 1)) }
    function chh(d) { return carH * (1 - 0.1 * Math.min(1, Math.abs(d)) - 0.025 * Math.max(0, Math.min(3, Math.abs(d) - 1))) }
    // opening: the hero rises first, the others follow outwards; a new list: the same, quicker
    function cin(d) { const k = Math.min(Math.abs(d), 6); return Math.min(wp.ease((wp.t - k * 0.06) / 0.6), wp.ease((wp.ft - k * 0.05) / 0.55)) }
    readonly property int base: Math.round(pos)
    readonly property var slots: { const l = []; for (let k = -reach; k <= reach; k++) l.push(k); return l }

    Item {
        id: car
        width: parent.width; height: wp.carH
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 10
        opacity: Math.min(1, wp.et * 1.6)

        // the pictures (drawn into one layer) ...
        Item {
            id: pics
            anchors.fill: parent
            visible: false
            layer.enabled: true
            Repeater {
                model: wp.slots
                delegate: Item {
                    id: pc
                    readonly property int idx: wp.base + modelData
                    readonly property var w: idx >= 0 && idx < wp.shown.length ? wp.shown[idx] : null
                    readonly property real d: idx - wp.pos
                    readonly property real it: wp.cin(d)
                    // (no `visible` here: inside a hidden layer source, items with their own visible were left out
                    // of the picture; an empty slot is see-through instead)
                    opacity: w !== null && Math.abs(d) <= wp.reach + 0.5 ? it * (1 - wp.clamp01((Math.abs(d) - wp.reach + 1.5) / 2)) : 0
                    width: wp.cw(d); height: wp.chh(d)
                    x: car.width / 2 + wp.cx(d) - width / 2
                    y: (car.height - height) / 2 + (1 - it) * 70
                    Rectangle { anchors.fill: parent; color: pal.card }
                    Image {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        sourceSize.width: 640; sourceSize.height: 400
                        // the small preview; a picture without one yet is read small (never at full size)
                        source: pc.w && pc.w.thumb ? "file:///" + pc.w.thumb : ""
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 180 } }
                    }
                }
            }
            // the big one, sharp: the wallpaper itself (a picture), once the carousel has stopped on it
            Image {
                id: hero
                readonly property var w: wp.shown.length > 0 && wp.sel < wp.shown.length ? wp.shown[wp.sel] : null
                readonly property bool still: Math.abs(wp.pos - wp.sel) < 0.01
                width: wp.heroW; height: wp.carH
                x: (car.width - width) / 2 + wp.cx(wp.sel - wp.pos)
                y: (1 - wp.cin(0)) * 70
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                sourceSize.width: 1400; sourceSize.height: 900
                source: still && w && w.live !== true ? "file:///" + w.path : ""
                opacity: status === Image.Ready && still ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 220 } }
            }
        }
        // ... and their round shapes, all in one mask
        Item {
            id: masks
            anchors.fill: parent
            visible: false
            layer.enabled: true
            Repeater {
                model: wp.slots
                delegate: Rectangle {
                    readonly property int idx: wp.base + modelData
                    readonly property real d: idx - wp.pos
                    readonly property real it: wp.cin(d)
                    opacity: idx >= 0 && idx < wp.shown.length && Math.abs(d) <= wp.reach + 0.5 ? 1 : 0
                    width: wp.cw(d); height: wp.chh(d)
                    x: car.width / 2 + wp.cx(d) - width / 2
                    y: (car.height - height) / 2 + (1 - it) * 70
                    radius: Math.min(width / 2, wp.lerp(Math.min(1, Math.abs(d)), 30, 22))
                    color: "white"
                }
            }
        }
        MultiEffect {
            anchors.fill: parent
            source: pics
            maskEnabled: true
            maskSource: masks
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        // what sits on the cards: the one you have, LIVE, the name, the pointer
        Repeater {
            model: wp.slots
            delegate: Item {
                id: card
                readonly property int idx: wp.base + modelData
                readonly property var w: idx >= 0 && idx < wp.shown.length ? wp.shown[idx] : null
                readonly property real d: idx - wp.pos
                readonly property real it: wp.cin(d)
                readonly property bool focus0: idx === wp.sel
                readonly property bool cur: w !== null && w.path === wp.current
                readonly property real rad: Math.min(width / 2, wp.lerp(Math.min(1, Math.abs(d)), 30, 22))
                visible: w !== null && Math.abs(d) <= wp.reach + 0.5
                width: wp.cw(d); height: wp.chh(d)
                x: car.width / 2 + wp.cx(d) - width / 2
                y: (car.height - height) / 2 + (1 - it) * 70
                opacity: it * (1 - wp.clamp01((Math.abs(d) - wp.reach + 1.5) / 2))

                // picking it: a flash of your key color
                Rectangle { anchors.fill: parent; radius: card.rad; color: pal.primary; opacity: card.focus0 ? flashS.v : 0 }
                // a soft light while the pointer is on a card that is not in focus
                Rectangle { anchors.fill: parent; radius: card.rad; color: "white"; opacity: cm.containsMouse && !card.focus0 ? 0.1 : 0
                    Behavior on opacity { NumberAnimation { duration: 150 } } }
                // the far ones fade into the dark
                Rectangle { anchors.fill: parent; radius: card.rad; color: "black"; opacity: 0.38 * wp.clamp01(Math.abs(card.d) - 0.6) }
                // the one you have: your key color around it
                Rectangle { anchors.fill: parent; radius: card.rad; color: "transparent"; border.width: 3; border.color: pal.primary
                    opacity: card.cur ? 1 : 0; Behavior on opacity { NumberAnimation { duration: 200 } } }
                // a video without its preview yet
                MIcon { anchors.centerIn: parent; visible: card.w !== null && card.w.live === true && !card.w.thumb && card.width > 60
                    size: 30; filled: true; name: "movie"; glyph: ""; color: pal.dim }
                // LIVE
                Rectangle {
                    visible: card.w !== null && card.w.live === true && card.width > 110
                    anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 14
                    width: lvT.implicitWidth + 30; height: 26; radius: 13; color: Qt.rgba(0, 0, 0, 0.58)
                    Rectangle { x: 11; anchors.verticalCenter: parent.verticalCenter; width: 7; height: 7; radius: 3.5; color: "#ff5a5a"
                        SequentialAnimation on opacity { running: wp.open && !Look.lite && card.focus0; loops: Animation.Infinite
                            NumberAnimation { to: 0.25; duration: 700 } NumberAnimation { to: 1; duration: 700 } } }
                    Text { id: lvT; x: 23; anchors.verticalCenter: parent.verticalCenter; text: "LIVE"; color: "white"
                        font.family: Fonts.main; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 1 }
                }
                // the one you have: a check in your shape, it spins in
                Item {
                    visible: scale > 0.01 && card.width > 70
                    width: 34; height: 34; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
                    scale: card.cur ? 1 : 0; rotation: card.cur ? 0 : -120
                    Behavior on scale { NumberAnimation { duration: 460; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                    Behavior on rotation { NumberAnimation { duration: 460; easing.type: Easing.OutCubic } }
                    Blob { anchors.fill: parent; kind: Look.shape; color: pal.primary }
                    MIcon { anchors.centerIn: parent; size: 18; filled: true; name: "check"; glyph: ""; color: pal.inkPrimary }
                }
                // the name, on the big one
                Rectangle {
                    anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 14
                    width: Math.min(parent.width - 28, nmT.implicitWidth + 26); height: 30; radius: 15
                    color: Qt.rgba(0, 0, 0, 0.5)
                    opacity: card.focus0 && Math.abs(card.d) < 0.3 ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                    Text { id: nmT; anchors.centerIn: parent; width: Math.min(implicitWidth, parent.width - 26); elide: Text.ElideRight
                        text: card.w ? card.w.name : ""; color: "white"; font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.Medium }
                }
                MouseArea {
                    id: cm
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { if (card.focus0) { if (card.w) wp.apply(card.w.path) } else wp.go(card.idx) }
                    onWheel: function(e) { wp.wheel(-(e.angleDelta.y !== 0 ? e.angleDelta.y : e.angleDelta.x)) }
                }
            }
        }

        // nothing to show
        Column {
            anchors.centerIn: parent
            spacing: 12
            visible: wp.shown.length === 0 && wp.loaded
            opacity: wp.et
            Item { width: 72; height: 72; anchors.horizontalCenter: parent.horizontalCenter
                Blob { anchors.fill: parent; kind: Look.shape; color: pal.container }
                MIcon { anchors.centerIn: parent; size: 32; filled: true; name: wp.all.length === 0 ? "wallpaper" : "search_off"; glyph: ""; color: pal.inkContainer } }
            Text { anchors.horizontalCenter: parent.horizontalCenter
                text: wp.all.length === 0 ? Tr.t("No wallpapers yet") : (wp.kind === "live" && wp.nLive === 0 ? Tr.t("No live wallpapers: put videos in a wallpaper folder") : Tr.t("Nothing matches"))
                color: "white"; font.family: Fonts.main; font.pixelSize: 15; font.weight: Font.Medium }
        }
        // still reading the list: three soft cards breathe
        Row {
            anchors.centerIn: parent
            spacing: wp.gap
            visible: !wp.loaded
            Repeater { model: [0.36, 1, 0.36]
                Rectangle { width: wp.heroW * modelData; height: wp.carH * (modelData === 1 ? 1 : 0.9); anchors.verticalCenter: parent.verticalCenter
                    radius: 26; color: pal.card
                    SequentialAnimation on opacity { running: !wp.loaded && wp.open; loops: Animation.Infinite
                        NumberAnimation { to: 0.45; duration: 650 } NumberAnimation { to: 1; duration: 650 } } } }
        }
    }
    // picking one: a flash of your key color on the big card
    QtObject { id: flashS; property real v: 0 }
    SequentialAnimation {
        id: pulse
        NumberAnimation { target: flashS; property: "v"; to: 0.42; duration: 90; easing.type: Easing.OutQuad }
        NumberAnimation { target: flashS; property: "v"; to: 0; duration: 620; easing.type: Easing.OutCubic }
    }

    // ---------- the toolbar above: search, All / Pictures / Live, shuffle, close ----------
    Shadow { anchors.fill: bar; radius: bar.radius; on: Look.shadows && Look.shadowPanels; opacity: bar.opacity; size: Look.shadowSize; strength: Look.shadowStrength }
    Rectangle {
        id: bar
        width: barRow.implicitWidth + 16; height: 60
        radius: 30
        anchors.horizontalCenter: parent.horizontalCenter
        y: car.y - height - 34 - (1 - wp.et) * 40
        color: pal.surface
        opacity: wp.clamp01(wp.et * 1.4)
        MouseArea { anchors.fill: parent }
        Row {
            id: barRow
            anchors.centerIn: parent
            spacing: 8
            // search
            Rectangle {
                width: 260; height: 44; radius: 22; color: pal.card
                border.width: search.activeFocus && search.text !== "" ? 2 : 0; border.color: pal.primary
                MIcon { x: 14; anchors.verticalCenter: parent.verticalCenter; size: 18; name: "search"; glyph: ""; color: pal.dim }
                TextInput {
                    id: search
                    x: 42; width: parent.width - 56; anchors.verticalCenter: parent.verticalCenter
                    color: pal.fg; font.family: Fonts.main; font.pixelSize: 14; clip: true
                    selectionColor: pal.primary; selectedTextColor: pal.inkPrimary
                    onTextChanged: wp.query = text
                    Text { visible: search.text === ""; text: Tr.t("Search wallpapers"); color: pal.dim; font: search.font }
                    Keys.onPressed: function(e) {
                        const k = e.key
                        if (k === Qt.Key_Escape) { if (search.text !== "") search.text = ""; else wp.open = false }
                        else if (k === Qt.Key_Return || k === Qt.Key_Enter) { if (wp.shown[wp.sel]) wp.apply(wp.shown[wp.sel].path) }
                        else if (k === Qt.Key_Right || k === Qt.Key_Down) wp.go(wp.sel + 1)
                        else if (k === Qt.Key_Left || k === Qt.Key_Up) wp.go(wp.sel - 1)
                        else if (k === Qt.Key_PageDown) wp.go(wp.sel + 5)
                        else if (k === Qt.Key_PageUp) wp.go(wp.sel - 5)
                        else if (k === Qt.Key_Home && search.text === "") wp.go(0)
                        else if (k === Qt.Key_End && search.text === "") wp.go(wp.shown.length - 1)
                        else if (k === Qt.Key_Tab) wp.kind = wp.kind === "all" ? "pics" : (wp.kind === "pics" ? "live" : "all")
                        else if (k === Qt.Key_Backtab) wp.kind = wp.kind === "all" ? "live" : (wp.kind === "live" ? "pics" : "all")
                        else return
                        e.accepted = true
                    }
                }
            }
            // All / Pictures / Live: a connected button group, the chosen one is filled
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Repeater {
                    model: [{ k: "all", n: Tr.t("All"), i: "apps" }, { k: "pics", n: Tr.t("Pictures"), i: "image" }, { k: "live", n: Tr.t("Live"), i: "play_circle" }]
                    delegate: Rectangle {
                        id: ch
                        readonly property bool on: wp.kind === modelData.k
                        width: chRow.implicitWidth + 28; height: 44
                        // the chosen one is a full pill, the others a softer square (Material 3 button group)
                        radius: on ? 22 : 12
                        color: on ? pal.primary : (chM.containsMouse ? pal.cardHi : pal.card)
                        Behavior on radius { NumberAnimation { duration: 320; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
                        Behavior on color { ColorAnimation { duration: 180 } }
                        Row {
                            id: chRow
                            anchors.centerIn: parent
                            spacing: 6
                            MIcon { anchors.verticalCenter: parent.verticalCenter; size: 17; filled: ch.on; name: ch.on ? "check" : modelData.i; glyph: ""; color: ch.on ? pal.inkPrimary : pal.fg }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: modelData.n; color: ch.on ? pal.inkPrimary : pal.fg
                                font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.DemiBold }
                        }
                        scale: chM.pressed ? 0.94 : 1
                        Behavior on scale { NumberAnimation { duration: 160 } }
                        MouseArea { id: chM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: wp.kind = modelData.k }
                    }
                }
            }
            IconBtn { icon: "shuffle"; onHit: wp.shuffle() }
            IconBtn { icon: "close"; onHit: wp.open = false }
        }
    }

    // ---------- under the carousel: where you are, and Set ----------
    Item {
        id: foot
        width: Math.max(wp.heroW, 420); height: 56
        anchors.horizontalCenter: parent.horizontalCenter
        y: car.y + car.height + 26 + (1 - wp.et) * 30
        opacity: wp.clamp01((wp.et - 0.3) * 1.6)
        visible: wp.shown.length > 0
        Column {
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text { text: (wp.sel + 1) + " / " + wp.shown.length + (wp.dirs > 1 ? "  ·  " + wp.dirs + Tr.t(" folders") : "")
                color: "white"; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold }
            Text { text: Tr.t("Enter sets it  ·  Tab: All, Pictures, Live"); color: Qt.rgba(1, 1, 1, 0.6); font.family: Fonts.main; font.pixelSize: 11 }
        }
        // Set as wallpaper, or "In use" when it is the one you have (the button turns into a pill with a check)
        Rectangle {
            id: setB
            readonly property bool inUse: wp.shown[wp.sel] !== undefined && wp.shown[wp.sel].path === wp.current
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: setRow.implicitWidth + 40; height: 52
            radius: setM.pressed ? 14 : 26
            color: inUse ? pal.container : pal.primary
            Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
            Behavior on color { ColorAnimation { duration: 220 } }
            Behavior on width { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
            Row {
                id: setRow
                anchors.centerIn: parent
                spacing: 8
                MIcon { anchors.verticalCenter: parent.verticalCenter; size: 20; filled: true; name: setB.inUse ? "check_circle" : "wallpaper"; glyph: ""
                    color: setB.inUse ? pal.inkContainer : pal.inkPrimary }
                Text { anchors.verticalCenter: parent.verticalCenter; text: setB.inUse ? Tr.t("In use") : Tr.t("Set as wallpaper")
                    color: setB.inUse ? pal.inkContainer : pal.inkPrimary; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold }
            }
            MouseArea { id: setM; anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                onClicked: if (wp.shown[wp.sel] && !setB.inUse) wp.apply(wp.shown[wp.sel].path) }
        }
    }

    component IconBtn: Item {
        id: ib
        property string icon: ""
        signal hit()
        width: 44; height: 44
        Rectangle { anchors.fill: parent; radius: ibm.pressed ? 14 : 22; color: ibm.containsMouse ? pal.cardHi : pal.card
            Behavior on radius { NumberAnimation { duration: 200 } } }
        MIcon { anchors.centerIn: parent; size: 20; filled: true; name: ib.icon; glyph: ""; color: pal.fg }
        MouseArea { id: ibm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ib.hit() }
    }
}
