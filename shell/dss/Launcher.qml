// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick
import QtQuick.Effects

// The DarkShadow Launcher: the app launcher of this shell (Settings > Rofi > Launcher). It looks like Rofi does and
// uses the same settings (theme, style, position, width, corners, transparency, icons, font size, lines).
// Type to search, arrows or Tab to move, Enter to open, Esc to close. "2+2" calculates and "10 km in mi" or "100 usd pln" converts (Enter copies the result),
// files in your home show after three letters ("/" in front: only files), anything else can be searched on the web. The apps you open most come first.
PanelWindow {
    screen: Mon.screen
    id: ln
    property bool open: false
    property real t: 0
    Behavior on t { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
    onOpenChanged: t = open ? 1 : 0

    // ---------- settings (rice-rofi state) ----------
    property var rs: ({})
    readonly property var fallbackTh: ({ bg: "#0d0d0d", alt: "#1a1a1a", fg: "#F6F5F4", dim: "#8a8a8a", sel: "#F6F5F4", selfg: "#0d0d0d", bd: "#ffffff1a" })
    readonly property var th: rs.themes && rs.themes[rs.theme] ? rs.themes[rs.theme] : fallbackTh
    readonly property string lay: rs.layout === "grid" || rs.layout === "bar" || rs.layout === "split" ? rs.layout : "list"
    readonly property string where: rs.position === "top" || rs.position === "bottom" ? rs.position : "center"
    readonly property real alpha: rs.transparency === false ? 1 : (rs.opacity !== undefined ? rs.opacity : 0.82)
    readonly property bool icons: rs.icons !== false
    readonly property int fs: rs.font_size ? rs.font_size : 13
    // the same font as Rofi: rice-rofi's font (your mono font) at the same size (Rofi sizes are points)
    readonly property string ff: Fonts.main
    readonly property int px: Math.round(fs * 4 / 3)
    readonly property int nlines: rs.lines ? rs.lines : 8
    readonly property real rad: lay === "bar" ? 0 : (rs.radius !== undefined ? rs.radius : 18)
    readonly property real cw: lay === "bar" ? width : (lay === "grid" ? Math.max(rs.width ? rs.width : 620, 720) : (lay === "split" ? Math.max(rs.width ? rs.width : 620, 860) : (rs.width ? rs.width : 620)))
    readonly property bool listy: lay === "list" || lay === "split"
    readonly property real leftW: lay === "split" ? Math.round(cw * 0.36) : 0
    readonly property string wall: rs.wallpaper ? "file:///" + rs.wallpaper : ""
    function col(h, a) {            // "#rrggbb" or "#rrggbbaa" (like rofi) to a color
        const s = String(h).replace("#", "")
        const al = s.length >= 8 ? parseInt(s.substr(6, 2), 16) / 255 : 1
        return Qt.rgba(parseInt(s.substr(0, 2), 16) / 255, parseInt(s.substr(2, 2), 16) / 255, parseInt(s.substr(4, 2), 16) / 255, a !== undefined ? a : al)
    }

    // ---------- the apps ----------
    property string query: ""
    property var usage: ({})
    // your favorites: first in the list (and ahead in a search). Right click an app (or Ctrl + D) adds or takes it
    // away; ~/.config/rice/launcher-favs.json
    property var favs: []
    function isFav(id) { return favs.indexOf(id) >= 0 }
    function toggleFav(r) {
        if (!r || r.kind !== "app" || !r.id) return
        const l = favs.slice(), i = l.indexOf(r.id)
        if (i >= 0) l.splice(i, 1); else l.unshift(r.id)
        favs = l
        DssWin.run(["bash", "-c", "mkdir -p \"$HOME/.config/rice\" && printf '%s\\n' \"$1\" > \"$HOME/.config/rice/launcher-favs.json\"", "_", JSON.stringify(l)])
        Sfx.play("click")
        const keep = cur; recompute(); cur = Math.min(keep, Math.max(0, results.length - 1))
    }
    FileView {
        path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/rice/launcher-favs.json"
        printErrors: false
        onLoaded: { try { const l = JSON.parse(text()); if (Array.isArray(l)) ln.favs = l.filter(x => typeof x === "string") } catch (e) { } }
    }
    property int cur: 0
    readonly property var apps: DesktopEntries.applications.values.filter(function(e) { return !e.noDisplay })
    property var results: []
    function sub(s, q) {            // letters of q appear in s in order
        let i = 0
        for (let k = 0; k < s.length && i < q.length; k++) if (s[k] === q[i]) i++
        return i === q.length
    }
    function score(e, q) {
        const n = (e.name || "").toLowerCase()
        if (n === q) return 1000
        if (n.indexOf(q) === 0) return 900 - n.length * 0.1
        if (n.indexOf(" " + q) >= 0) return 800
        const at = n.indexOf(q)
        if (at >= 0) return 700 - at
        const g = ((e.genericName || "") + " " + (e.keywords ? e.keywords.join(" ") : "")).toLowerCase()
        if (g.indexOf(q) >= 0) return 400
        if ((e.comment || "").toLowerCase().indexOf(q) >= 0) return 250
        if (q.length >= 2 && sub(n, q)) return 150
        return -1
    }
    function calc(q) {
        const s = q.replace(/^=/, "").trim()
        if (s === "" || !/^[0-9+\-*\/%^().,\s]+$/.test(s) || !/[+\-*\/%^]/.test(s.replace(/^[-+]/, ""))) return null
        try {
            const v = Function('"use strict"; return (' + s.replace(/\^/g, "**").replace(/,/g, ".") + ")")()
            if (typeof v === "number" && isFinite(v)) return String(Math.round(v * 1e10) / 1e10)
        } catch (e) { }
        return null
    }
    function recompute() {
        const q = query.trim().toLowerCase()
        const out = []
        const list = []
        for (let i = 0; i < apps.length; i++) {
            const e = apps[i]
            const u = Math.min(usage[e.id] || 0, 30)
            const fv = favs.indexOf(e.id)
            const fb = fv >= 0 ? 100000 - fv : 0
            if (q === "") list.push({ e: e, s: u * 10 + fb })
            else { const sc = score(e, q); if (sc >= 0) list.push({ e: e, s: sc + u * 4 + (fv >= 0 ? 150 : 0) }) }
        }
        list.sort(function(a, b) { return b.s !== a.s ? b.s - a.s : (a.e.name || "").localeCompare(b.e.name || "") })
        const c = q !== "" ? calc(query) : null
        if (c !== null) out.push({ kind: "calc", name: "= " + c, sub: Tr.t("Press Enter to copy the result"), val: c })
        // "10 km in mi", "100 usd pln": units and money (Conv.qml; the rates are fetched only when you type money)
        if (q !== "" && /^\s*-?[\d.,]/.test(query)) {
            if (Conv.looksLikeMoney(query)) Conv.ask()
            const cvr = Conv.parse(query)
            if (cvr) out.push({ kind: "calc", name: "= " + cvr.text, sub: cvr.from + "  ·  " + Tr.t("Press Enter to copy the result"), val: cvr.val })
        }
        for (let i = 0; i < list.length && i < 80; i++)
            out.push({ kind: "app", name: list[i].e.name, sub: list[i].e.genericName || list[i].e.comment || "", entry: list[i].e, id: list[i].e.id })
        // files in your home (rice-tools files), after a few letters; "/" in front: only files
        const fq = query.trim().replace(/^\//, "").trim()
        if (fq === filesFor) for (let i = 0; i < files.length; i++)
            out.push({ kind: "file", name: files[i].name, sub: files[i].where, path: files[i].path, dir: files[i].dir })
        if (q !== "") out.push({ kind: "web", name: "Search the web for \"" + query.trim() + "\"", sub: Tr.t("DuckDuckGo"), q: query.trim() })
        if (query.trim().indexOf("/") === 0) { for (let i = out.length - 1; i >= 0; i--) if (out[i].kind !== "file") out.splice(i, 1) }
        results = out
        resetScroll()
        cur = 0
    }
    onQueryChanged: { recompute(); fileT.restart() }
    // ---------- files: asked a moment after you stop typing ----------
    property var files: []
    property string filesFor: ""
    Timer {
        id: fileT
        interval: 260
        onTriggered: {
            const fq = ln.query.trim().replace(/^\//, "").trim()
            if (fq.length < 3 && !(ln.query.trim().indexOf("/") === 0 && fq.length >= 2)) { ln.files = []; ln.filesFor = ""; return }
            if (fq === ln.filesFor) return
            if (fileP.running) { fileT.restart(); return }     // one at a time: asked again when this one is done
            fileP.want = fq
            fileP.running = true
        }
    }
    DssProc {
        id: fileP
        property string want: ""
        command: ["bash", "-c", "exec \"$HOME/.local/bin/rice-tools\" files \"$1\" 2>/dev/null", "_", want]
        stdout: DssCollector { onStreamFinished: {
            let l = []
            try { l = JSON.parse(text) } catch (e) { l = [] }
            if (fileP.want !== ln.query.trim().replace(/^\//, "").trim()) { fileT.restart(); return }
            ln.files = l; ln.filesFor = fileP.want
            const keep = ln.cur; ln.recompute(); ln.cur = Math.min(keep, Math.max(0, ln.results.length - 1))
        } }
    }
    Connections { target: Conv; function onRatesChanged() { if (ln.open && ln.query !== "") ln.recompute() } }
    onAppsChanged: if (open) recompute()
    onUsageChanged: if (open) recompute()
    function iconOf(r) {
        if (r.kind === "calc") return ""
        if (r.kind === "web") return Icons.get("web-browser", true)
        if (r.kind === "file") {
            if (r.dir) return Icons.get("folder", true)
            const ext = (r.name.split(".").pop() || "").toLowerCase()
            const m = { png: "image-x-generic", jpg: "image-x-generic", jpeg: "image-x-generic", webp: "image-x-generic", gif: "image-x-generic",
                        mp4: "video-x-generic", mkv: "video-x-generic", webm: "video-x-generic", mp3: "audio-x-generic", flac: "audio-x-generic",
                        pdf: "application-pdf", zip: "package-x-generic", gz: "package-x-generic", tar: "package-x-generic",
                        sh: "text-x-script", py: "text-x-script", qml: "text-x-script" }
            return Icons.get(m[ext] || "text-x-generic", true) || Icons.get("text-x-generic", true)
        }
        const n = r.entry && r.entry.icon ? r.entry.icon : ""
        const p = n !== "" ? Icons.get(n, true) : ""
        return p !== "" ? p : Icons.get("application-x-executable", true)
    }
    function glyphOf(r) { return r.kind === "calc" ? "=" : "" }

    // ---------- opening and closing ----------
    function show() {
        query = ""
        rsProc.running = true
        usageProc.running = true
        open = true
        recompute()
        focusTimer.restart()
    }
    function hide() { open = false }
    Timer { id: focusTimer; interval: 40; onTriggered: input.forceActiveFocus() }
    function bump(id) {
        const n = Object.assign({}, usage); n[id] = (n[id] || 0) + 1; usage = n
        DssWin.run(["bash", "-c", "python3 - \"$1\" <<'PY'\nimport json, os, sys\np = os.path.expanduser('~/.cache/rice/launcher-usage.json')\ntry:\n    d = json.load(open(p))\nexcept Exception:\n    d = {}\nos.makedirs(os.path.dirname(p), exist_ok=True)\nd[sys.argv[1]] = d.get(sys.argv[1], 0) + 1\njson.dump(d, open(p, 'w'))\nPY", "_", id])
    }
    function activate(r) {
        if (!r) return
        // close first (and give the keyboard back), then start the app: the new window gets the focus and the
        // launcher never stays on top of it, even if something below fails
        hide()
        try { Sfx.play("select") } catch (e) { }
        try {
            if (r.kind === "app") { r.entry.execute(); bump(r.id) }
            else if (r.kind === "calc") DssWin.run(["bash", "-c", "printf %s \"$1\" | wl-copy", "_", r.val])
            else if (r.kind === "web") DssWin.run(["xdg-open", "https://duckduckgo.com/?q=" + encodeURIComponent(r.q)])
            else if (r.kind === "file") DssWin.run(["xdg-open", r.path])
        } catch (e) { console.warn("launcher: could not start " + r.name + ": " + e) }
    }
    function move(d) { if (results.length > 0) cur = Math.max(0, Math.min(results.length - 1, cur + d)) }

    DssProc {
        id: rsProc
        command: ["bash", "-c", "\"$HOME/.local/bin/rice-rofi\" state 2>/dev/null"]
        running: true
        stdout: DssCollector { onStreamFinished: { try { ln.rs = JSON.parse(text) } catch (e) { console.warn("launcher: bad rice-rofi state") } } }
    }
    // the launcher settings are re-read as soon as Settings > Launcher changes them, not only when it opens
    Trigger { path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/rice/rofi.json"; every: 3000
        onContent: function(t) { if (!rsProc.running) rsProc.running = true } }
    DssProc {
        id: usageProc
        command: ["bash", "-c", "cat \"$HOME/.cache/rice/launcher-usage.json\" 2>/dev/null"]
        stdout: DssCollector { onStreamFinished: { try { ln.usage = JSON.parse(text) } catch (e) { } } }
    }
    // rice-launch writes a line into this file: open it, or close it when it is already open
    Trigger { name: "launcher-open"; fresh: true; onFired: function(o) { if (ln.open) ln.hide(); else ln.show() } }

    // ---------- window ----------
    visible: open || (t > 0.01 && !closeT.done)
    // if the closing animation ever stalls (the compositor sent no frames for a moment), it still goes away
    Timer { id: closeT; property bool done: false; interval: 600; running: !open; onRunningChanged: if (running) done = false; onTriggered: done = true }
    // niri: blur behind the card (Hyprland blurs this layer with its own rule)
    NiriBlur { win: ln; items: [card]; radius: card.radius; on: ln.open && ln.t > 0.98 }
    color: "transparent"
    anchors { top: true; left: true; right: true; bottom: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-launcher"
    WlrLayershell.keyboardFocus: open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None

    MouseArea { anchors.fill: parent; onClicked: ln.hide() }

    readonly property real pad: lay === "bar" ? 10 : 14
    readonly property real inputH: lay === "bar" ? 44 : 50
    readonly property real rowH: Math.max(46, px * 2.8)
    readonly property int gcols: Math.max(3, Math.floor((cw - 2 * pad) / 120))
    readonly property int grows: Math.min(4, Math.max(2, Math.round(nlines / 2.5)))
    readonly property real cellH: 104
    readonly property int shownRows: lay === "grid" ? Math.min(Math.ceil(results.length / gcols), grows) : (lay === "split" ? nlines : Math.min(results.length, nlines))
    readonly property real listH: lay === "grid" ? shownRows * cellH : shownRows * rowH
    readonly property real cardH: lay === "bar" ? inputH + 2 * pad : 2 * pad + inputH + (listH > 0 || lay === "split" ? 10 + listH : 0)
    readonly property real maxH: lay === "bar" ? cardH : 2 * pad + inputH + 10 + (lay === "grid" ? grows * cellH : nlines * rowH)

    Rectangle {
        id: card
        width: ln.cw
        height: ln.cardH
        x: (ln.width - width) / 2
        y: ln.where === "top" ? (ln.lay === "bar" ? 0 : 64) : (ln.where === "bottom" ? ln.height - (ln.lay === "bar" ? ln.cardH : 64 + ln.maxH) : (ln.height - (ln.lay === "bar" ? ln.cardH : ln.maxH)) / 2)
        radius: ln.rad
        color: ln.col(ln.th.bg, ln.alpha)
        border.width: ln.lay === "bar" ? 0 : 1
        border.color: ln.col(ln.th.bd)
        opacity: ln.t
        scale: 0.965 + 0.035 * ln.t
        transform: Translate { y: (1 - ln.t) * 10 }
        Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        MouseArea { anchors.fill: parent }   // clicks on the card do not close it

        // split: the wallpaper on the left, with the apps you use most as round buttons
        Item {
            id: leftPane
            visible: ln.lay === "split"
            x: 0; y: 0; width: ln.leftW; height: parent.height
            Image { id: wallImg; anchors.fill: parent; source: ln.wall; fillMode: Image.PreserveAspectCrop; visible: false; asynchronous: true; sourceSize.width: 1200 }
            Item { id: wallMask; anchors.fill: parent; visible: false; layer.enabled: true
                Rectangle { anchors.fill: parent; radius: ln.rad }
                Rectangle { x: parent.width / 2; width: parent.width / 2; height: parent.height } }
            MultiEffect { anchors.fill: parent; source: wallImg; maskEnabled: true; maskSource: wallMask; visible: wallImg.status === Image.Ready }
            Rectangle { anchors.fill: parent; radius: ln.rad; color: ln.col(ln.th.alt); visible: wallImg.status !== Image.Ready
                Rectangle { x: parent.width / 2; width: parent.width / 2; height: parent.height; color: parent.color } }
            Column {
                x: 14; anchors.verticalCenter: parent.verticalCenter; spacing: 12
                Repeater {
                    model: ln.query === "" ? ln.results.filter(function(r) { return r.kind === "app" }).slice(0, 4) : []
                    delegate: Rectangle {
                        id: fav
                        width: 42; height: 42; radius: 21
                        color: favM.containsMouse ? ln.col(ln.th.sel) : ln.col(ln.th.bg, 0.55)
                        border.width: 1; border.color: ln.col(ln.th.bd)
                        scale: favM.containsMouse ? 1.12 : 1
                        Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }
                        Behavior on color { ColorAnimation { duration: 140 } }
                        Image { anchors.centerIn: parent; width: 24; height: 24; source: ln.iconOf(modelData); sourceSize.width: 48; sourceSize.height: 48; asynchronous: true }
                        MouseArea { id: favM; anchors.fill: parent; hoverEnabled: true; onClicked: ln.activate(modelData) }
                    }
                }
            }
        }

        Rectangle {
            id: field
            x: ln.leftW + ln.pad; y: ln.pad
            width: ln.lay === "bar" ? Math.min(340, parent.width * 0.3) : parent.width - ln.leftW - 2 * ln.pad
            height: ln.inputH
            radius: ln.lay === "bar" ? 10 : Math.max(8, ln.rad - 6)
            color: ln.col(ln.th.alt, ln.alpha < 1 ? Math.min(1, ln.alpha + 0.08) : 1)
            Rectangle {    // line under the field that grows while you type
                anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                height: 2; radius: 1; color: ln.col(ln.th.sel)
                width: ln.query !== "" ? parent.width - 2 * ln.rad : 0
                opacity: ln.query !== "" ? 0.9 : 0
                Behavior on width { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                Behavior on opacity { NumberAnimation { duration: 200 } }
            }
            TextInput {
                id: input
                x: 18; width: parent.width - 36; anchors.verticalCenter: parent.verticalCenter
                text: ln.query
                color: ln.col(ln.th.fg); selectionColor: ln.col(ln.th.sel); selectedTextColor: ln.col(ln.th.selfg)
                font.family: ln.ff; font.pixelSize: ln.px
                clip: true
                onTextChanged: { if (text !== ln.query) { ln.query = text; if (activeFocus) Sfx.play("type") } }
                Keys.onPressed: function(e) {
                    const ctrl = (e.modifiers & Qt.ControlModifier) !== 0
                    const grid = ln.lay === "grid", bar = ln.lay === "bar"
                    if (e.key === Qt.Key_Escape) { ln.hide(); e.accepted = true }
                    else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) { ln.activate(ln.results[ln.cur]); e.accepted = true }
                    else if (ctrl && e.key === Qt.Key_D) { ln.toggleFav(ln.results[ln.cur]); e.accepted = true }
                    else if (e.key === Qt.Key_Down || (e.key === Qt.Key_Tab && !(e.modifiers & Qt.ShiftModifier)) || (ctrl && (e.key === Qt.Key_J || e.key === Qt.Key_N))) { ln.move(grid ? ln.gcols : 1); e.accepted = true }
                    else if (e.key === Qt.Key_Up || e.key === Qt.Key_Backtab || (ctrl && (e.key === Qt.Key_K || e.key === Qt.Key_P))) { ln.move(grid ? -ln.gcols : -1); e.accepted = true }
                    else if ((grid || bar) && e.key === Qt.Key_Right && input.cursorPosition === input.text.length) { ln.move(1); e.accepted = true }
                    else if ((grid || bar) && e.key === Qt.Key_Left && input.cursorPosition === 0) { ln.move(-1); e.accepted = true }
                    else if (e.key === Qt.Key_PageDown) { ln.move(ln.nlines); e.accepted = true }
                    else if (e.key === Qt.Key_PageUp) { ln.move(-ln.nlines); e.accepted = true }
                }
                DText { visible: input.text.length === 0; text: Tr.t("Search"); font.family: ln.ff; font.pixelSize: ln.px; color: ln.col(ln.th.dim) }
            }
        }

        // ---------- list ----------
        ListView {
            id: listV
            visible: ln.listy
            x: ln.leftW + ln.pad; y: ln.pad + ln.inputH + 10
            width: parent.width - ln.leftW - 2 * ln.pad; height: ln.listH
            clip: true
            interactive: false
            model: ln.listy ? ln.results : []
            currentIndex: ln.cur
            highlightMoveDuration: 150
            highlightFollowsCurrentItem: true
            boundsBehavior: Flickable.StopAtBounds
            NumberAnimation { id: listAnim; target: listV; property: "contentY"; duration: 150; easing.type: Easing.OutCubic }
            WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function(e) { ln.wheel(listV, listAnim, e, false) } }
            highlight: Rectangle { radius: Math.max(6, ln.rad - 8); color: ln.col(ln.th.sel); Behavior on y { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } } }
            delegate: Item {
                id: row
                width: listV.width; height: ln.rowH
                readonly property bool sel: index === ln.cur
                opacity: 0
                property real slide: 18
                transform: Translate { y: row.slide }
                Component.onCompleted: enter.start()
                SequentialAnimation {
                    id: enter
                    PauseAnimation { duration: Math.min(index, 9) * 24 }
                    ParallelAnimation {
                        NumberAnimation { target: row; property: "opacity"; to: 1; duration: 220; easing.type: Easing.OutCubic }
                        NumberAnimation { target: row; property: "slide"; to: 0; duration: 300; easing.type: Easing.OutBack; easing.overshoot: 1.2 }
                    }
                }
                Image {
                    id: ico
                    visible: ln.icons && modelData.kind !== "calc"
                    x: 14 + (row.sel ? 3 : 0); anchors.verticalCenter: parent.verticalCenter
                    width: ln.rowH - 18; height: width
                    scale: row.sel ? 1.1 : 1
                    Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } }
                    Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                    source: ln.icons ? ln.iconOf(modelData) : ""
                    sourceSize.width: 64; sourceSize.height: 64; asynchronous: true
                }
                DText { visible: modelData.kind === "calc"; x: 14; width: ln.rowH - 18; anchors.verticalCenter: parent.verticalCenter; horizontalAlignment: Text.AlignHCenter
                    text: "="; font.family: ln.ff; font.pixelSize: ln.fs + 8; font.weight: Font.DemiBold; color: parent.sel ? ln.col(ln.th.selfg) : ln.col(ln.th.dim) }
                DText {
                    x: (ln.icons || modelData.kind === "calc" ? ln.rowH : 0) + 8 + (row.sel ? 3 : 0)
                    Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                    width: parent.width - x - 16
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight; text: modelData.name; font.family: ln.ff; font.pixelSize: ln.px
                    color: parent.sel ? ln.col(ln.th.selfg) : ln.col(ln.th.fg)
                }
                // a favorite: a small star
                DText { visible: modelData.kind === "app" && ln.isFav(modelData.id); anchors.right: parent.right; anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter; text: "★"; font.pixelSize: ln.fs; color: parent.sel ? ln.col(ln.th.selfg) : ln.col(ln.th.sel) }
                MouseArea { anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPositionChanged: { ln.noScroll = true; ln.cur = index; ln.noScroll = false }
                    onClicked: function(m) { if (m.button === Qt.RightButton) ln.toggleFav(modelData); else ln.activate(modelData) } }
            }
        }

        // ---------- grid ----------
        GridView {
            id: gridV
            visible: ln.lay === "grid"
            x: ln.pad; y: ln.pad + ln.inputH + 10
            width: parent.width - 2 * ln.pad; height: ln.listH
            clip: true
            interactive: false
            cellWidth: width / ln.gcols; cellHeight: ln.cellH
            model: ln.lay === "grid" ? ln.results : []
            currentIndex: ln.cur
            highlightMoveDuration: 150
            boundsBehavior: Flickable.StopAtBounds
            NumberAnimation { id: gridAnim; target: gridV; property: "contentY"; duration: 150; easing.type: Easing.OutCubic }
            WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function(e) { ln.wheel(gridV, gridAnim, e, false) } }
            highlight: Rectangle { radius: Math.max(6, ln.rad - 8); color: ln.col(ln.th.sel) }
            delegate: Item {
                id: cell
                width: gridV.cellWidth; height: ln.cellH
                readonly property bool sel: index === ln.cur
                opacity: 0; scale: 0.8
                Component.onCompleted: enterG.start()
                SequentialAnimation {
                    id: enterG
                    PauseAnimation { duration: Math.min(index, 14) * 18 }
                    ParallelAnimation {
                        NumberAnimation { target: cell; property: "opacity"; to: 1; duration: 200 }
                        NumberAnimation { target: cell; property: "scale"; to: 1; duration: 280; easing.type: Easing.OutBack }
                    }
                }
                Image {
                    visible: modelData.kind !== "calc"
                    anchors.horizontalCenter: parent.horizontalCenter; y: 10
                    width: 48; height: 48
                    source: ln.iconOf(modelData)
                    sourceSize.width: 96; sourceSize.height: 96; asynchronous: true
                }
                DText { visible: modelData.kind === "calc"; anchors.horizontalCenter: parent.horizontalCenter; y: 10; height: 48; verticalAlignment: Text.AlignVCenter
                    text: "="; font.pixelSize: 30; font.weight: Font.DemiBold; color: parent.sel ? ln.col(ln.th.selfg) : ln.col(ln.th.dim) }
                DText {
                    x: 6; y: 64; width: parent.width - 12; horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap
                    text: modelData.name; font.family: ln.ff; font.pixelSize: ln.px - 2
                    color: parent.sel ? ln.col(ln.th.selfg) : ln.col(ln.th.fg)
                }
                MouseArea { anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPositionChanged: { ln.noScroll = true; ln.cur = index; ln.noScroll = false }
                    onClicked: function(m) { if (m.button === Qt.RightButton) ln.toggleFav(modelData); else ln.activate(modelData) } }
            }
        }

        // ---------- bar ----------
        ListView {
            id: barV
            visible: ln.lay === "bar"
            x: field.x + field.width + 10; y: ln.pad
            width: parent.width - x - ln.pad; height: ln.inputH
            orientation: ListView.Horizontal
            clip: true
            spacing: 6
            model: ln.lay === "bar" ? ln.results : []
            currentIndex: ln.cur
            highlightMoveDuration: 150
            boundsBehavior: Flickable.StopAtBounds
            highlight: Rectangle { radius: 10; color: ln.col(ln.th.sel) }
            delegate: Item {
                id: chip
                width: Math.min(220, bt.implicitWidth + (ln.icons ? 54 : 28)); height: barV.height
                readonly property bool sel: index === ln.cur
                opacity: 0; scale: 0.85
                Component.onCompleted: enterB.start()
                SequentialAnimation {
                    id: enterB
                    PauseAnimation { duration: Math.min(index, 10) * 22 }
                    ParallelAnimation {
                        NumberAnimation { target: chip; property: "opacity"; to: 1; duration: 200 }
                        NumberAnimation { target: chip; property: "scale"; to: 1; duration: 260; easing.type: Easing.OutBack }
                    }
                }
                Image { visible: ln.icons && modelData.kind !== "calc"; x: 10; anchors.verticalCenter: parent.verticalCenter; width: 26; height: 26
                    source: ln.icons ? ln.iconOf(modelData) : ""; sourceSize.width: 52; sourceSize.height: 52; asynchronous: true }
                DText { id: bt; x: ln.icons ? 44 : 14; width: parent.width - x - 10; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
                    text: modelData.name; font.family: ln.ff; font.pixelSize: ln.px - 1; color: parent.sel ? ln.col(ln.th.selfg) : ln.col(ln.th.fg) }
                MouseArea { anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPositionChanged: { ln.noScroll = true; ln.cur = index; ln.noScroll = false }
                    onClicked: function(m) { if (m.button === Qt.RightButton) ln.toggleFav(modelData); else ln.activate(modelData) } }
            }
        }

        DText { visible: ln.query !== "" && ln.results.length === 0 && ln.lay !== "bar"; x: ln.leftW + ln.pad + 16; y: ln.pad + ln.inputH + 20; text: Tr.t("Nothing found"); font.family: ln.ff; font.pixelSize: ln.px; color: ln.col(ln.th.dim) }
    }
    // smooth scrolling: the wheel and the keys move the view with an easing animation
    property bool noScroll: false
    function clampY(v, view) { return Math.max(0, Math.min(v, view.contentHeight - view.height)) }
    function glide(view, anim, y) {
        const to = clampY(y, view)
        anim.stop(); anim.from = view.contentY; anim.to = to; anim.start()
    }
    function wheel(view, anim, e, h) {
        // a touchpad: the list follows the fingers (a glide per event lagged behind them)
        if (e.pixelDelta.y !== 0) { anim.stop(); view.contentY = clampY(view.contentY - e.pixelDelta.y * 1.4, view); return }
        const d = e.angleDelta.y * 1.9
        const base = anim.running ? anim.to : view.contentY
        glide(view, anim, base - d)
    }
    function resetScroll() { listAnim.stop(); gridAnim.stop(); listV.contentY = 0; gridV.contentY = 0 }
    onCurChanged: {
        if (noScroll) return
        if (listy) {
            const y = cur * rowH, base = listAnim.running ? listAnim.to : listV.contentY
            if (y < base) glide(listV, listAnim, y)
            else if (y + rowH > base + listV.height) glide(listV, listAnim, y + rowH - listV.height)
        } else if (lay === "grid") {
            const y = Math.floor(cur / gcols) * cellH, base = gridAnim.running ? gridAnim.to : gridV.contentY
            if (y < base) glide(gridV, gridAnim, y)
            else if (y + cellH > base + gridV.height) glide(gridV, gridAnim, y + cellH - gridV.height)
        } else barV.positionViewAtIndex(cur, ListView.Contain)
    }
}
