// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick

// The Task view (SUPER + Tab) and the window switcher (ALT + Tab). rice-taskview sends "overview", "switch",
// "back" and "commit" in ~/.cache/rice/taskview-open and gives the workspaces and windows (rice-taskview data).
//   Task view: every workspace as a small screen with its windows where they are (live pictures of them).
//     Type to find a window (Enter goes to it), arrows pick a workspace, click a window to go to it, drag it onto
//     another workspace to move it there, middle click (or the x) closes it, Esc closes the view.
//   Switcher: the windows in the order you used them. Hold ALT, Tab goes on, SHIFT + Tab back, let go of ALT
//     to go to the picked one.
PanelWindow {
    id: tv
    screen: Mon.screen
    property bool open: false
    property string mode: "overview"
    property var d: null
    property int sel: 0                // switcher: the picked window; overview: the picked workspace
    property bool pendingCommit: false
    property string query: ""
    readonly property var cfg: d && d.cfg ? d.cfg : ({ live: true, count: 10, empty: true })

    NPal { id: pal; theme: "shell"; alpha: 0.86 }

    // ---------- coming and going ----------
    property real t: 0
    NumberAnimation { id: tAnim; target: tv; property: "t"; easing.type: Easing.BezierSpline }
    function show(m) {
        mode = m; query = ""; open = true
        tAnim.stop(); tAnim.to = 1; tAnim.duration = 380; tAnim.easing.bezierCurve = Look.decel; tAnim.start()
        load()
        input.forceActiveFocus()
    }
    function hide() {
        if (!open) return
        open = false; pendingCommit = false
        tAnim.stop(); tAnim.to = 0; tAnim.duration = 170; tAnim.easing.bezierCurve = Look.accel; tAnim.start()
    }
    function run(args) { DssWin.run([((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-taskview"].concat(args)) }

    DssProc {
        id: dataProc
        command: [((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-taskview", "data"]
        stdout: DssCollector {
            onStreamFinished: {
                try { tv.d = JSON.parse(text) } catch (e) { return }
                if (tv.mode !== "overview") {
                    const n = tv.mru.length
                    if (n === 0) { tv.hide(); return }
                    tv.sel = tv.mode === "back" ? n - 1 : (n > 1 ? 1 : 0)
                    if (tv.pendingCommit) tv.commit()
                } else {
                    tv.sel = Math.max(0, tv.wsList.indexOf(tv.d.active))
                }
            }
        }
    }
    function load() { if (!dataProc.running) dataProc.running = true }
    Timer { id: reload; interval: 180; onTriggered: tv.load() }

    Trigger {
        name: "taskview-open"
        onFired: function(o) {
            const m = (o.trim().split(" ")[1] || "overview")
            if (m === "overview") { if (tv.open && tv.mode === "overview") tv.hide(); else tv.show("overview"); return }
            if (m === "commit") { if (tv.open && tv.mode !== "overview") { if (tv.d) tv.commit(); else tv.pendingCommit = true } return }
            // switch / back
            if (tv.open && tv.mode !== "overview" && tv.d) {
                const n = tv.mru.length
                if (n > 0) tv.sel = (tv.sel + (m === "back" ? n - 1 : 1)) % n
                return
            }
            tv.show(m === "back" ? "back" : "switch")
        }
    }

    // ---------- what it shows ----------
    readonly property real aspect: d ? Math.max(0.3, d.w / Math.max(1, d.h)) : 16 / 9
    // only while it is on the screen: closed, every card and every live picture of a window is let go. (Before
    // V4.84 they stayed after the first use, and each window kept being captured, every frame, behind everything:
    // lag and a lot of memory)
    readonly property bool alive: open || t > 0.01
    readonly property var wins: d && alive ? d.wins : []
    readonly property var mru: wins.slice().sort(function(a, b) { return a.order - b.order })
    readonly property var wsList: {
        if (!d || !alive) return []
        const o = []
        const top = Math.max(cfg.count || 10, d.used.length ? d.used[d.used.length - 1] : 1, d.active)
        if (cfg.empty !== false) for (let i = 1; i <= top; i++) o.push(i)
        else { for (const u of d.used) o.push(u); if (o.indexOf(d.active) < 0) o.push(d.active); o.sort(function(a, b) { return a - b }) }
        return o
    }
    function hit(w) { const q = query.toLowerCase(); return q === "" || (w.title + " " + w.cls).toLowerCase().indexOf(q) >= 0 }
    readonly property var found: query === "" ? [] : mru.filter(hit)
    function iconOf(cls) {
        const e = cls ? DesktopEntries.heuristicLookup(cls) : null
        const n = e && e.icon ? e.icon : (cls || "").toLowerCase()
        const p = n !== "" ? Icons.get(n, true) : ""
        return p !== "" ? p : Icons.get("application-x-executable", true)
    }
    function nameOf(cls) { const e = cls ? DesktopEntries.heuristicLookup(cls) : null; return e && e.name ? e.name : cls }
    // the live picture of a window: the toplevel with the same app and title
    function tlFor(w) {
        const l = ToplevelManager.toplevels.values
        for (let i = 0; i < l.length; i++) if (l[i].appId === w.cls && l[i].title === w.title) return l[i]
        for (let i = 0; i < l.length; i++) if (l[i].appId === w.cls) return l[i]
        return null
    }
    property int hoverWs: -1
    function wsAt(x, y) {
        for (let i = 0; i < cards.count; i++) {
            const it = cards.itemAt(i)
            if (!it) continue
            const p = it.mapToItem(sheet, 0, 0)
            if (x >= p.x && x <= p.x + it.width && y >= p.y && y <= p.y + it.height) return it.ws
        }
        return -1
    }
    // moves a window to workspace n: the view first (so it answers at once), then the window manager, then a fresh
    // look a moment later
    function moveNow(w, n) {
        const nd = JSON.parse(JSON.stringify(d))
        const there = nd.wins.filter(function(x) { return x.ws === n })
        for (let i = 0; i < nd.wins.length; i++) {
            const x = nd.wins[i]
            if (String(x.id) !== String(w.id)) continue
            x.ws = n
            if (there.length === 0 && !x.float) { x.x = 0.01; x.y = 0.02; x.w = 0.98; x.h = 0.96 }
            else if (!x.float) {
                // beside the ones already there: each gets an equal share
                const all = there.concat([x])
                for (let j = 0; j < all.length; j++) { all[j].x = j / all.length + 0.005; all[j].w = 1 / all.length - 0.01; all[j].y = 0.02; all[j].h = 0.96 }
            }
        }
        if (nd.used.indexOf(n) < 0) { nd.used.push(n); nd.used.sort(function(a, b) { return a - b }) }
        d = nd
        run(["move", String(w.id), String(n)])
        reload.interval = 450; reload.restart()
    }
    function goWin(w) { if (!w) return; run(["focus", String(w.id)]); hide() }
    function goWs(n) { run(["ws", String(n)]); hide() }
    function commit() { pendingCommit = false; const w = mru[sel]; if (w) run(["focus", String(w.id)]); hide() }

    // ---------- the window ----------
    visible: t > 0.01
    color: "transparent"
    anchors { top: true; bottom: true; left: true; right: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-taskview"
    WlrLayershell.keyboardFocus: open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    NiriBlur { win: tv; items: [tv.mode === "overview" ? sheet : strip]; radius: 30; on: tv.open && tv.t > 0.98 }

    // a dim veil over everything; a click on it closes
    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.38 * tv.t) }
    MouseArea { anchors.fill: parent; onClicked: tv.hide() }

    TextInput {
        id: input
        width: 0; height: 0; opacity: 0
        text: tv.query
        onTextChanged: if (text !== tv.query) tv.query = text
        Keys.onPressed: function(e) {
            const k = e.key
            if (k === Qt.Key_Escape) { tv.hide(); e.accepted = true; return }
            if (tv.mode !== "overview") {
                const n = tv.mru.length
                if (k === Qt.Key_Tab || k === Qt.Key_Right || k === Qt.Key_Down) { tv.sel = (tv.sel + 1) % Math.max(1, n); e.accepted = true }
                else if (k === Qt.Key_Backtab || k === Qt.Key_Left || k === Qt.Key_Up) { tv.sel = (tv.sel + n - 1) % Math.max(1, n); e.accepted = true }
                else if (k === Qt.Key_Return || k === Qt.Key_Enter || k === Qt.Key_Space) { tv.commit(); e.accepted = true }
                return
            }
            const L = tv.wsList.length, C = Math.min(5, L)
            if (k === Qt.Key_Right) { tv.sel = Math.min(L - 1, tv.sel + 1); e.accepted = true }
            else if (k === Qt.Key_Left) { tv.sel = Math.max(0, tv.sel - 1); e.accepted = true }
            else if (k === Qt.Key_Down) { tv.sel = Math.min(L - 1, tv.sel + C); e.accepted = true }
            else if (k === Qt.Key_Up) { tv.sel = Math.max(0, tv.sel - C); e.accepted = true }
            else if (k === Qt.Key_Return || k === Qt.Key_Enter) {
                if (tv.found.length) tv.goWin(tv.found[0]); else if (tv.wsList[tv.sel] !== undefined) tv.goWs(tv.wsList[tv.sel])
                e.accepted = true
            }
            else if (k >= Qt.Key_1 && k <= Qt.Key_9 && (e.modifiers & Qt.MetaModifier)) { tv.goWs(k - Qt.Key_0); e.accepted = true }
        }
        Keys.onReleased: function(e) { if (tv.mode !== "overview" && (e.key === Qt.Key_Alt || e.key === Qt.Key_Meta)) { tv.commit(); e.accepted = true } }
    }

    // ================= Task view =================
    readonly property int cols: Math.max(1, Math.min(5, wsList.length))
    readonly property int rows: Math.max(1, Math.ceil(wsList.length / cols))
    readonly property real gap: 14
    readonly property real cardW: Math.max(80, Math.min((width * 0.88 - gap * (cols - 1)) / cols, ((height * 0.66 - gap * (rows - 1)) / rows) * aspect))
    readonly property real cardH: cardW / aspect

    // the search pill
    Rectangle {
        id: search
        visible: tv.mode === "overview"
        anchors.horizontalCenter: parent.horizontalCenter
        y: sheet.y - height - 16 + (1 - tv.t) * -20
        opacity: tv.t
        width: 460; height: 50; radius: 25
        color: pal.surface
        border.width: 1; border.color: pal.line
        Row {
            anchors.verticalCenter: parent.verticalCenter; x: 8; spacing: 10
            Rectangle { width: 34; height: 34; radius: 17; color: pal.container
                MIcon { anchors.centerIn: parent; size: 18; name: "search"; glyph: String.fromCodePoint(0xF0349); color: pal.inkContainer } }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: search.width - 130; elide: Text.ElideRight
                text: tv.query !== "" ? tv.query : Tr.t("Type to find a window")
                color: tv.query !== "" ? pal.fg : pal.dim
                font.family: Fonts.main; font.pixelSize: 14
            }
        }
        Rectangle {
            anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
            height: 30; width: cnt.implicitWidth + 20; radius: 15; color: pal.card
            Text { id: cnt; anchors.centerIn: parent; color: pal.dim; font.family: Fonts.main; font.pixelSize: 11
                text: tv.query !== "" ? tv.found.length + " " + Tr.t("found") : tv.wins.length + " " + Tr.t("windows") }
        }
    }

    // the sheet with the workspaces
    Rectangle {
        id: sheet
        visible: tv.mode === "overview"
        width: grid.width + 32; height: grid.height + 32
        anchors.horizontalCenter: parent.horizontalCenter
        y: (parent.height - height) / 2 + 20
        radius: 30
        color: Qt.rgba(pal.surface.r, pal.surface.g, pal.surface.b, 0.72)
        border.width: 1; border.color: pal.line
        opacity: Math.min(1, tv.t * 1.5)
        scale: 0.94 + 0.06 * tv.t
        MouseArea { anchors.fill: parent }
        Grid {
            id: grid
            x: 16; y: 16
            columns: tv.cols; spacing: tv.gap
            Repeater {
                id: cards
                model: tv.wsList
                delegate: Item {
                    id: card
                    readonly property int ws: modelData
                    readonly property bool active: tv.d && ws === tv.d.active
                    readonly property bool picked: index === tv.sel
                    readonly property var mine: tv.wins.filter(function(w) { return w.ws === card.ws }).sort(function(a, b) { return (a.float ? 1 : 0) - (b.float ? 1 : 0) })
                    width: tv.cardW; height: tv.cardH
                    // each one comes in a little after the one before
                    readonly property real k: Math.max(0, Math.min(1, tv.t * 1.6 - index * 0.05))
                    opacity: k
                    transform: Translate { y: (1 - card.k) * 18 }
                    Rectangle {
                        anchors.fill: parent
                        radius: 18
                        color: tv.hoverWs === card.ws || cm.containsMouse ? Qt.rgba(pal.card.r, pal.card.g, pal.card.b, 0.95) : Qt.rgba(pal.card.r, pal.card.g, pal.card.b, 0.6)
                        border.width: card.active || card.picked || tv.hoverWs === card.ws ? 2 : 1
                        border.color: card.active ? pal.primary : (card.picked ? Qt.rgba(pal.primary.r, pal.primary.g, pal.primary.b, 0.5) : pal.line)
                        Behavior on color { ColorAnimation { duration: 140 } }
                    }
                    MouseArea { id: cm; anchors.fill: parent; hoverEnabled: true; onClicked: tv.goWs(card.ws) }
                    // empty: its number, big and faint
                    Text { anchors.centerIn: parent; visible: card.mine.length === 0; text: card.ws; color: pal.dim; opacity: 0.45
                        font.family: Fonts.numbers; font.pixelSize: Math.round(tv.cardH * 0.34); font.weight: Font.Medium }
                    // the windows, where they are on that workspace
                    Item {
                        id: area
                        anchors.fill: parent; anchors.margins: 5
                        clip: true
                        Repeater {
                            model: card.mine
                            delegate: Item {
                                id: wv
                                readonly property var w: modelData
                                readonly property bool lit: tv.query !== "" && tv.hit(w)
                                readonly property bool dimmed: tv.query !== "" && !tv.hit(w)
                                x: Math.max(0, Math.min(1, w.x)) * area.width
                                y: Math.max(0, Math.min(1, w.y)) * area.height
                                width: Math.max(24, Math.min(1, w.w) * area.width - 3)
                                height: Math.max(18, Math.min(1, w.h) * area.height - 3)
                                z: w.float ? 2 : 1
                                opacity: dimmed ? 0.3 : 1
                                Behavior on opacity { NumberAnimation { duration: 160 } }
                                // dragged: it leaves its small screen (which clips it) and floats over the sheet
                                property bool dragging: wm2.drag.active
                                property var home: null
                                onDraggingChanged: {
                                    if (dragging) {
                                        home = parent
                                        const p = wv.mapToItem(sheet, 0, 0)
                                        wv.parent = sheet; wv.x = p.x; wv.y = p.y; wv.z = 50
                                    } else if (home) {
                                        // let go over another workspace: the window moves there at once (the view
                                        // changes first, the window manager follows)
                                        const to = tv.hoverWs
                                        tv.hoverWs = -1
                                        if (to > 0 && to !== card.ws) { tv.moveNow(wv.w, to); return }
                                        wv.parent = home; wv.z = w.float ? 2 : 1
                                        wv.x = Qt.binding(function() { return Math.max(0, Math.min(1, wv.w.x)) * area.width })
                                        wv.y = Qt.binding(function() { return Math.max(0, Math.min(1, wv.w.y)) * area.height })
                                    }
                                }
                                // a window comes in with a small pop (opening, or moved here)
                                scale: 1
                                Component.onCompleted: { scale = 0.82; popIn.start() }
                                NumberAnimation { id: popIn; target: wv; property: "scale"; to: 1; duration: 260; easing.type: Easing.OutBack; easing.overshoot: 1.4 }
                                // when the others on its workspace change, it slides to its new place
                                Behavior on x { enabled: !wv.dragging && wv.home === null; NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                                Behavior on width { enabled: !wv.dragging; NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                                Rectangle {
                                    id: tile
                                    anchors.fill: parent
                                    radius: 10
                                    color: pal.surface
                                    border.width: wv.lit || wm2.containsMouse ? 2 : 1
                                    border.color: wv.lit || wm2.containsMouse ? pal.primary : pal.line
                                    clip: true
                                    scale: wm2.drag.active ? 1.06 : (wm2.containsMouse ? 1.03 : 1)
                                    Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } }
                                    // a live picture of the window (Settings > Desktop > Task view > Live pictures)
                                    Item {
                                        id: live
                                        anchors.fill: parent; anchors.margins: 1
                                        property var obj: null
                                        Component.onCompleted: {
                                            if (tv.cfg.live === false) return
                                            const tl = tv.tlFor(wv.w)
                                            if (!tl) return
                                            try {
                                                obj = Qt.createQmlObject('import Quickshell.Wayland\nScreencopyView { anchors.fill: parent; live: true; paintCursor: false }', live, "shot")
                                                obj.live = Qt.binding(function() { return tv.open })
                                                obj.captureSource = tl
                                            } catch (e) { obj = null }
                                        }
                                    }
                                    Image {
                                        anchors.centerIn: parent
                                        readonly property real sz: Math.min(parent.width, parent.height) * 0.42
                                        width: sz; height: sz; sourceSize.width: 96; sourceSize.height: 96
                                        source: tv.iconOf(wv.w.cls)
                                        visible: !live.obj || !live.obj.hasContent
                                        smooth: true; mipmap: true
                                    }
                                }
                                // the app on top of its picture, small
                                Image { visible: live.obj !== null && wv.width > 70; x: 6; y: wv.height - height - 6; width: 18; height: 18
                                    sourceSize.width: 36; sourceSize.height: 36; source: tv.iconOf(wv.w.cls) }
                                // close
                                Rectangle {
                                    visible: wm2.containsMouse && !wm2.drag.active && wv.width > 50
                                    width: 22; height: 22; radius: 11; x: wv.width - 26; y: 4; z: 3
                                    color: xm.containsMouse ? "#e06c75" : Qt.rgba(0, 0, 0, 0.6)
                                    MIcon { anchors.centerIn: parent; size: 13; name: "close"; glyph: String.fromCodePoint(0xF0156); color: "white" }
                                    MouseArea { id: xm; anchors.fill: parent; hoverEnabled: true; onClicked: { tv.run(["close", String(wv.w.id)]); reload.restart() } }
                                }
                                MouseArea {
                                    id: wm2
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                    drag.target: wv
                                    drag.threshold: 8
                                    // which workspace is under it (no drag-and-drop machinery: a plain look, cheap)
                                    onPositionChanged: if (wv.dragging) tv.hoverWs = tv.wsAt(wv.x + wv.width / 2, wv.y + wv.height / 2)
                                    onClicked: function(m) {
                                        if (m.button === Qt.MiddleButton) { tv.run(["close", String(wv.w.id)]); reload.restart(); return }
                                        tv.goWin(wv.w)
                                    }
                                }
                            }
                        }
                    }
                    // the number, in a pill
                    Rectangle {
                        visible: card.mine.length > 0 || card.active
                        x: 8; y: 8; z: 5
                        height: 24; width: Math.max(24, wsn.implicitWidth + 14); radius: 12
                        color: card.active ? pal.primary : Qt.rgba(0, 0, 0, 0.55)
                        Text { id: wsn; anchors.centerIn: parent; text: card.ws; color: card.active ? pal.inkPrimary : "white"
                            font.family: Fonts.numbers; font.pixelSize: 12; font.weight: Font.Bold }
                    }
                }
            }
        }
    }
    // what you can do
    Row {
        visible: tv.mode === "overview"
        anchors.horizontalCenter: parent.horizontalCenter
        y: sheet.y + sheet.height + 16
        opacity: tv.t * 0.9
        spacing: 8
        Repeater {
            model: [["Enter", Tr.t("go")], [Tr.t("Drag"), Tr.t("move to a workspace")], [Tr.t("Middle click"), Tr.t("close")], ["Esc", Tr.t("back")]]
            delegate: Rectangle {
                height: 28; width: hr.implicitWidth + 20; radius: 14; color: Qt.rgba(pal.surface.r, pal.surface.g, pal.surface.b, 0.8)
                Row { id: hr; anchors.centerIn: parent; spacing: 6
                    Text { text: modelData[0]; color: pal.primary; font.family: Fonts.main; font.pixelSize: 11; font.weight: Font.Bold }
                    Text { text: modelData[1]; color: pal.dim; font.family: Fonts.main; font.pixelSize: 11 } }
            }
        }
    }

    // ================= Switcher (ALT + Tab) =================
    readonly property real tileW: Math.min(250, (width * 0.9 - 24 - 10 * Math.max(0, mru.length - 1)) / Math.max(1, mru.length))
    Rectangle {
        id: strip
        visible: tv.mode !== "overview"
        anchors.centerIn: parent
        width: srow.width + 24; height: srow.height + 24
        radius: 30
        color: Qt.rgba(pal.surface.r, pal.surface.g, pal.surface.b, 0.8)
        border.width: 1; border.color: pal.line
        opacity: Math.min(1, tv.t * 1.5)
        scale: 0.9 + 0.1 * tv.t
        Row {
            id: srow
            x: 12; y: 12; spacing: 10
            Repeater {
                model: tv.mode !== "overview" ? tv.mru : []
                delegate: Item {
                    id: sw
                    readonly property var w: modelData
                    readonly property bool on: index === tv.sel
                    width: tv.tileW; height: tv.tileW / tv.aspect + 34
                    Rectangle {
                        id: sbox
                        width: parent.width; height: parent.width / tv.aspect
                        radius: 16
                        color: pal.card
                        border.width: sw.on ? 3 : 1
                        border.color: sw.on ? pal.primary : pal.line
                        scale: sw.on ? 1.04 : 1
                        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
                        clip: true
                        Item {
                            id: slive
                            anchors.fill: parent; anchors.margins: 3
                            property var obj: null
                            Component.onCompleted: {
                                if (tv.cfg.live === false) return
                                const tl = tv.tlFor(sw.w)
                                if (!tl) return
                                try {
                                    obj = Qt.createQmlObject('import Quickshell.Wayland\nScreencopyView { anchors.fill: parent; live: true; paintCursor: false }', slive, "shot")
                                    obj.live = Qt.binding(function() { return tv.open })
                                    obj.captureSource = tl
                                } catch (e) { obj = null }
                            }
                        }
                        Image { anchors.centerIn: parent; width: parent.height * 0.4; height: width; sourceSize.width: 128; sourceSize.height: 128
                            source: tv.iconOf(sw.w.cls); visible: !slive.obj || !slive.obj.hasContent; mipmap: true }
                        MouseArea { anchors.fill: parent; hoverEnabled: true; onEntered: tv.sel = index; onClicked: { tv.sel = index; tv.commit() } }
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: sbox.height + 8; spacing: 6
                        Image { width: 18; height: 18; sourceSize.width: 36; sourceSize.height: 36; source: tv.iconOf(sw.w.cls); anchors.verticalCenter: parent.verticalCenter }
                        Text { width: Math.min(implicitWidth, sw.width - 26); elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter
                            text: sw.w.title !== "" ? sw.w.title : tv.nameOf(sw.w.cls)
                            color: sw.on ? pal.fg : pal.dim; font.family: Fonts.main; font.pixelSize: 12; font.weight: sw.on ? Font.DemiBold : Font.Normal }
                    }
                }
            }
        }
    }
}
