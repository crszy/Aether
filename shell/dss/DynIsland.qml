// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Services.Pipewire
import QtQuick

// The island of the Dynamic bar style: it sits where the clock is and grows into whatever happens right now.
//   idle    the time (and, while music plays, the cover in your shape and a small moving wave)
//   media   point at it (or click it): the player, with the cover in your shape, a wavy progress line you can
//           drag, and a play button that changes shape while it plays
//   volume  the volume or the microphone changed: a wavy level you can drag
//   ws      you switched workspace: which one
//   notif   a notification came in (the shell shows notifications): its icon in your shape, the text, and a
//           wave that runs out while it stays
//   rec     the screen is being recorded: the time, the microphone and stop
// Everything moves on springs (Material 3 spatial motion); the content fades in after the shape has grown.
// The small things (time, volume, workspace, recording) change the island in the bar itself; the player and
// notifications open as a card just beside the bar (below it, or above it for a bar at the bottom), so the bar
// is never covered.
Item {
    id: isl
    property var bar: null
    property var pal: null                     // NPal of the bar
    property var nf: null                      // Notifs.qml
    readonly property int bh: bar ? bar.barH : 38
    readonly property bool down: !bar || !bar.atBottom     // grows downwards (bar at the top)
    readonly property var cfg: bar ? bar.bc : ({})
    // Settings > Dynamic island > Dynamic island off: only the clock (it grows into nothing; a click still opens Today)
    readonly property bool dynamic: cfg.island_dynamic !== false
    function on(k) { return dynamic && cfg[k] !== false }

    Instantiator { model: isl.visible ? 1 : 0; delegate: QtObject { Component.onCompleted: Player.hold(); Component.onDestruction: Player.release() } }
    readonly property var source: Pipewire.defaultAudioSource
    PwObjectTracker { objects: [isl.source] }
    readonly property bool micMuted: source && source.audio ? source.audio.muted : false

    // ---------- what it shows ----------
    // the island in the bar: the time, or for a moment the volume, the microphone or the workspace, or the recording
    // the card beside the bar: a notification, the info card (today, system) or the player
    property string flash: ""                  // volume | mic | ws (for a moment)
    property bool hoverOpen: false
    property bool pinned: false                // the player, opened from the info card
    property var note: null                    // the notification it shows
    property bool infoOpen: false
    property string infoTab: "today"           // today | system
    readonly property string mode: flash !== "" ? flash : (bar && bar.recOn ? "rec" : "idle")
    readonly property string cardWant: note ? "notif"
        : (infoOpen ? "info" : ((pinned || hoverOpen) && Player.has && !ghost ? "media" : ""))
    readonly property bool big: cardWant !== ""
    function show(what, ms) { if (flash !== what) bump.restart(); flash = what; flashT.interval = ms; flashT.restart() }
    // something new in the island: it gives a small squeeze, like a drop landing
    property real squeeze: 0
    SequentialAnimation {
        id: bump
        NumberAnimation { target: isl; property: "squeeze"; to: 1; duration: 110; easing.type: Easing.OutQuad }
        NumberAnimation { target: isl; property: "squeeze"; to: 0; duration: 420; easing.type: Easing.OutBack; easing.overshoot: 2.2 }
    }
    Timer { id: flashT; onTriggered: isl.flash = "" }
    // no island on the bar: only the card shows (the clock or a key asked for it), in the middle of the bar
    property bool ghost: false
    // the cards you opened (not the ones that come and go by themselves): Esc, the desktop or another window close them
    readonly property bool sticky: big && (infoOpen || pinned)
    function closeCards() { infoOpen = false; pinned = false; hoverOpen = false }
    // Another window taking the focus closes the opened card. Opening the card takes the keyboard, so the window
    // that had it (Settings, a browser) lets it go: that is not "another window" and must not close the card again
    // (it did: with any window open the card closed as soon as it opened).
    property real openedAt: 0
    onStickyChanged: if (sticky) openedAt = Date.now()
    Connections {
        target: ToplevelManager
        function onActiveToplevelChanged() {
            if (!isl.sticky || ToplevelManager.activeToplevel === null || Date.now() - isl.openedAt < 600) return
            isl.closeCards()
        }
    }
    function openInfo(tab, toggle) {
        if (toggle && infoOpen && infoTab === tab) { infoOpen = false; return }
        infoTab = tab; pinned = false; hoverOpen = false; infoOpen = true; bump.restart()
        if (!hovering) pinT.restart()
    }

    property bool ready: false
    Timer { running: true; interval: 2500; onTriggered: isl.ready = true }
    readonly property real vol: bar ? bar.vol : 0
    readonly property bool muted: bar ? bar.muted : false
    onVolChanged: if (ready && !ghost && on("island_volume")) show("volume", 1600)
    onMutedChanged: if (ready && !ghost && on("island_volume")) show("volume", 1600)
    onMicMutedChanged: if (ready && !ghost && on("island_volume")) show("mic", 1600)
    // the charger: plugged in or out (UPower's "on battery", which also works when a laptop holds its battery
    // below full and so never says "charging")
    readonly property bool plugged: bar ? bar.plugged : false
    onPluggedChanged: if (ready && !ghost && bar && bar.hasBatt && on("island_charge")) show("charge", 3200)
    readonly property int wsId: bar ? bar.focusedId : 1
    onWsIdChanged: if (ready && !ghost && on("island_ws") && flash !== "volume") show("ws", 1100)
    Connections {
        target: isl.nf
        ignoreUnknownSignals: true
        function onShowed(n) {
            if (isl.ghost || !isl.on("island_notifs")) return
            isl.note = n; bump.restart()
            noteRun.stop(); isl.noteLeft = 1; noteRun.duration = isl.noteLife; noteRun.start()
        }
    }
    // rice-dashboard open / island: the info card (the dashboard is the island now)
    Trigger {
        path: isl.bar && isl.bar.extra ? "" : ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.cache/rice/island-open"
        every: 1500
        onFired: function(line) {
            const p = String(line).trim().split(/\s+/)
            if (p.length < 2) return
            isl.openInfo(p[1] === "system" || p[1] === "about" ? p[1] : "today", p[2] !== "open")
        }
    }
    // it stays while the wave runs out; pointing at it holds the wave
    readonly property int noteLife: nf ? Math.max(3, nf.timeout) * 1000 : 5000
    property real noteLeft: 1
    NumberAnimation { id: noteRun; target: isl; property: "noteLeft"; to: 0; paused: running && isl.hovering }
    onNoteLeftChanged: if (noteLeft <= 0.001 && note) note = null


    // ---------- what the info card reads ----------
    // the weather (rice-weather, at most every half hour)
    property string wCond: ""
    property string wTemp: ""
    property string wPlace: ""
    DssProc {
        id: wProc
        command: ["bash", "-c", "f=\"$HOME/.cache/rice/weather.txt\"; [ -n \"$(find \"$f\" -mmin -30 2>/dev/null)\" ] || \"$HOME/.local/bin/rice-weather\" >/dev/null 2>&1; cat \"$f\" 2>/dev/null"]
        stdout: DssCollector { onStreamFinished: {
            const p = text.trim().split("|")
            if (p.length >= 2) { isl.wCond = p[0].trim(); isl.wTemp = p[1].trim().replace(/^\+/, ""); isl.wPlace = (p[2] || "").trim() } } }
    }
    onInfoOpenChanged: if (infoOpen && !wProc.running) wProc.running = true
    Timer { interval: 1800000; repeat: true; triggeredOnStart: true; running: isl.visible && (!isl.ghost || isl.infoOpen) && isl.cfg.island_weather !== false
        onTriggered: if (!wProc.running) wProc.running = true }
    function wGlyph(c) {
        const s = String(c).toLowerCase()
        if (s.indexOf("thunder") >= 0) return ""
        if (s.indexOf("snow") >= 0 || s.indexOf("sleet") >= 0 || s.indexOf("ice") >= 0) return ""
        if (s.indexOf("rain") >= 0 || s.indexOf("drizzle") >= 0 || s.indexOf("shower") >= 0) return ""
        if (s.indexOf("fog") >= 0 || s.indexOf("mist") >= 0) return ""
        if (s.indexOf("cloud") >= 0 || s.indexOf("overcast") >= 0) return ""
        return ""
    }
    function wIcon(c) {
        const t = String(c).toLowerCase(), h = new Date().getHours(), night = h >= 20 || h < 6
        if (t.indexOf("thunder") >= 0) return "thunderstorm"
        if (t.indexOf("snow") >= 0 || t.indexOf("sleet") >= 0 || t.indexOf("ice") >= 0 || t.indexOf("blizzard") >= 0) return "ac_unit"
        if (t.indexOf("rain") >= 0 || t.indexOf("drizzle") >= 0 || t.indexOf("shower") >= 0) return "rainy"
        if (t.indexOf("fog") >= 0 || t.indexOf("mist") >= 0 || t.indexOf("haze") >= 0) return "foggy"
        if (t.indexOf("partly") >= 0) return night ? "nights_stay" : "partly_cloudy_day"
        if (t.indexOf("cloud") >= 0 || t.indexOf("overcast") >= 0) return "cloud"
        if (t.indexOf("clear") >= 0 || t.indexOf("sunny") >= 0) return night ? "clear_night" : "sunny"
        return t === "" ? "cloud" : (night ? "clear_night" : "partly_cloudy_day")
    }
    // the system numbers, only while System is open (rice-dashboard stats)
    property var st: ({})
    property var disk0: null
    DssProc {
        id: stProc
        running: isl.big && isl.cardMode === "info" && isl.infoTab === "system"
        command: ["bash", "-c", "exec \"$HOME/.local/bin/rice-dashboard\" stats"]
        stdout: DssSplit { onRead: function(line) {
            try { const j = JSON.parse(line); isl.st = j; if (j.disks && j.disks.length > 0) isl.disk0 = j.disks[0] } catch (e) { } } }
    }
    function gb(b) { return b >= 1073741824 ? (b / 1073741824).toFixed(b >= 107374182400 ? 0 : 1) + " GB" : Math.round(b / 1048576) + " MB" }
    function upText(sec) { const h = Math.floor(sec / 3600), m = Math.floor(sec / 60) % 60; return (h > 0 ? h + " h " : "") + m + " min" }


    // the About tab: the system's name, DarkShadow's Shell and its updates
    property string osName: "Linux"
    DssProc {
        running: true
        command: ["bash", "-c", ". /etc/os-release 2>/dev/null; echo \"${PRETTY_NAME:-${NAME:-Linux}}\""]
        stdout: DssCollector { onStreamFinished: if (text.trim() !== "") isl.osName = text.trim() }
    }
    property var up: ({ installed: "", latest: "", update: false, news: [], checked: 0 })
    property bool checking: false
    DssProc {
        id: upCache
        command: ["bash", "-c", "cat \"$HOME/.cache/rice/update.json\" 2>/dev/null"]
        stdout: DssCollector { onStreamFinished: { try { if (text.trim() !== "") isl.up = JSON.parse(text) } catch (e) { } } }
    }
    DssProc {
        id: upProc
        command: ["bash", "-c", "\"$HOME/.local/bin/rice-update\" check --json 2>/dev/null"]
        stdout: DssCollector { onStreamFinished: { try { if (text.trim() !== "") isl.up = JSON.parse(text) } catch (e) { } isl.checking = false } }
    }
    function checkUpdates() { if (!upProc.running) { checking = true; upProc.running = true } }
    readonly property bool aboutOpen: big && cardMode === "info" && infoTab === "about"
    onAboutOpenChanged: if (aboutOpen) { upCache.running = true; if (Date.now() / 1000 - (up.checked || 0) > 1800) checkUpdates() }
    function ago(t) {
        if (!t) return "never"
        const m = Math.floor((Date.now() / 1000 - t) / 60)
        return m < 1 ? "just now" : (m < 60 ? m + " min ago" : (m < 1440 ? Math.floor(m / 60) + " h ago" : Math.floor(m / 1440) + " d ago"))
    }

    // ---------- its size ----------
    readonly property bool showDate: cfg.island_date !== false
    readonly property real tw: {
        switch (mode) {
        case "volume": case "mic": return 300
        case "ws": return Math.max(190, wsRow.implicitWidth + 40)
        case "rec": return recRow.implicitWidth + 30
        case "charge": return 340
        }
        return idleRow.implicitWidth + 30
    }
    readonly property string pillMode: mode
    // the face that is shown in the pill. When the mode changes the old face goes first and the new one comes
    // after it, so two faces (workspace and recording timer, say) are never on top of each other, however fast
    // the modes change (scrolling through workspaces while recording)
    property string faceNow: pillMode
    onPillModeChanged: { faceNow = ""; swapT.restart() }
    Timer { id: swapT; interval: 70; onTriggered: isl.faceNow = isl.pillMode }
    property color fill: pal ? pal.surface : "#141414"
    property bool flatRest: false
    property real areaW: 1920                    // how wide the bar is (the card stays inside it)
    // its width in the bar
    readonly property real restW: ghost ? 0 : tw
    width: Math.round(restW); height: bh
    // a calm change of width: the modules beside it and the notch move with it, so no bounce here
    Behavior on width { NumberAnimation { duration: Look.lite ? 180 : 300; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }

    // the card: which one it shows (kept while it closes)
    property string cardMode: "media"
    onCardWantChanged: if (cardWant !== "") cardMode = cardWant
    readonly property real cw: cardMode === "notif" ? 440 : (cardMode === "info" ? 520 : 470)
    readonly property real ch: cardMode === "notif" ? 96 : (cardMode === "info" ? 318 : 184)
    readonly property bool cardShown: card.opacity > 0.01
    readonly property Item cardItem: card          // niri: the bar blurs behind it (NiriBlur)
    // the part of the screen it takes (the bar lets the pointer through everywhere else)
    readonly property real ux: cardShown ? Math.min(0, card.x) : 0
    readonly property real uy: cardShown ? Math.min(0, card.y) : 0
    readonly property real uw: (cardShown ? Math.max(width, card.x + card.width) : width) - ux
    readonly property real uh: (cardShown ? Math.max(height, card.y + card.height) : height) - uy

    // pointing at it: opens the player; cards close a moment after the pointer leaves (opened ones wait longer)
    readonly property bool hovering: hov.hovered || hovC.hovered
    HoverHandler { id: hov }
    onHoveringChanged: {
        if (hovering) { closeT.stop(); pinT.stop(); if (on("island_hover") && !big && !ghost) openT.restart() }
        else { openT.stop(); closeT.restart(); if (pinned || infoOpen) pinT.restart() }
    }
    Timer { id: openT; interval: 280; onTriggered: isl.hoverOpen = Player.has }
    Timer { id: closeT; interval: 450; onTriggered: isl.hoverOpen = false }
    Timer { id: pinT; interval: Math.max(2, isl.cfg.island_close || 4) * 1000; onTriggered: { isl.pinned = false; isl.infoOpen = false } }
    // the player closes by itself when nothing plays any more
    onPlayingChanged: if (!playing && !hovering) { pinT.restart(); closeT.restart() }
    readonly property bool playing: Player.playing

    // ---------- the shape ----------
    Shadow { anchors.fill: bg; radius: bg.radius; on: Look.shadows && Look.shadowBar && !isl.flatRest && !isl.ghost; size: Look.shadowSize * 0.5; strength: Look.shadowStrength }
    Rectangle {
        id: bg
        anchors.fill: parent
        visible: !isl.ghost
        transform: Scale { origin.x: bg.width / 2; origin.y: bg.height / 2; xScale: 1 + 0.035 * isl.squeeze; yScale: 1 - 0.09 * isl.squeeze }
        radius: height / 2
        // inside the notch or a pill with modules it has no color of its own (no darker band)
        color: isl.flatRest ? Qt.rgba(isl.fill.r, isl.fill.g, isl.fill.b, 0) : isl.fill
        clip: true
        // the whole island: left click today (calendar, weather), right click system, the wheel the volume;
        // it works whatever the island shows (the recording's buttons and the volume line sit on top of it)
        MouseArea {
            anchors.fill: parent; cursorShape: Qt.PointingHandCursor; acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: function(m) { isl.openInfo(m.button === Qt.RightButton ? "system" : (isl.cfg.island_tab || "today"), true) }
            onWheel: function(e) { Vol.nudge(isl.bar.sink, Look.notches(e) * 0.04) }
        }

        // ---------- idle: the time ----------
        Face {
            id: fIdle; name: "idle"; width: idleRow.implicitWidth + 30; height: isl.bh
            Row {
                id: idleRow
                // on whole pixels, so the time is sharp and sits straight in the middle
                x: Math.round((parent.width - width) / 2); y: Math.round((parent.height - height) / 2)
                spacing: 9
                ShapeImage { visible: Player.playing; width: 22; height: 22; anchors.verticalCenter: parent.verticalCenter
                    kind: Look.shape; source: Player.art; empty: isl.pal ? isl.pal.container : "#333"; fallbackColor: isl.pal ? isl.pal.inkContainer : "#ccc"
                    SlowSpin { turn: 360; period: 9000; running: Player.playing && isl.pillMode === "idle" && !Look.lite } }
                // the date (as the bar's Date module) and the time in a pill in your key color
                Text { visible: isl.showDate; anchors.verticalCenter: parent.verticalCenter
                    text: isl.bar ? Qt.locale().dayName(isl.bar.now.getDay(), Locale.ShortFormat) + ", " + isl.bar.two(isl.bar.now.getDate()) + "/" + isl.bar.two(isl.bar.now.getMonth() + 1) : ""
                    color: isl.pal ? isl.pal.fg : "white"; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.Medium }
                Rectangle { visible: isl.showDate; anchors.verticalCenter: parent.verticalCenter
                    width: tp.implicitWidth + 18; height: isl.bh - 14; radius: height / 2; color: isl.pal ? isl.pal.primary : "#8f8"
                    Text { id: tp; anchors.centerIn: parent; text: isl.bar ? isl.bar.timeText : ""; color: isl.pal ? isl.pal.inkPrimary : "black"
                        font.family: Fonts.numbers; font.pixelSize: 13; font.weight: Font.DemiBold } }
                Text { visible: !isl.showDate; anchors.verticalCenter: parent.verticalCenter; text: isl.bar ? isl.bar.timeText : ""
                    color: isl.pal ? isl.pal.fg : "white"; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold }
                Row {
                    visible: isl.cfg.island_weather !== false && isl.wTemp !== ""
                    anchors.verticalCenter: parent.verticalCenter; spacing: 4
                    // a solid weather glyph (Nerd Font), fully in your key color
                    Text { anchors.verticalCenter: parent.verticalCenter; text: isl.wGlyph(isl.wCond); font.family: Fonts.icons; font.pixelSize: 15
                        color: isl.pal ? isl.pal.primary : "white" }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: isl.wTemp.replace(/[CF]$/, ""); color: isl.pal ? isl.pal.dim : "#aaa"
                        font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.Medium }
                }
                Eq { visible: Player.playing; anchors.verticalCenter: parent.verticalCenter; h: 14; go: isl.pillMode === "idle" }
            }
        }

        // ---------- rec: the screen is being recorded ----------
        Face {
            name: "rec"; width: recRow.implicitWidth + 30; height: isl.bh
            Row {
                id: recRow
                anchors.centerIn: parent
                spacing: 12
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 12; height: 12; radius: 6; color: "#ff5a5a"
                    SequentialAnimation on opacity { running: isl.pillMode === "rec"; loops: Animation.Infinite
                        NumberAnimation { to: 0.35; duration: 700 } NumberAnimation { to: 1; duration: 700 } } }
                Text { anchors.verticalCenter: parent.verticalCenter; text: isl.bar ? isl.bar.recTime : ""
                    color: isl.pal ? isl.pal.fg : "white"; font.family: Fonts.numbers; font.pixelSize: 14; font.weight: Font.Medium }
                Item { width: 24; height: 24; anchors.verticalCenter: parent.verticalCenter
                    MIcon { anchors.centerIn: parent; size: 17; name: isl.micMuted ? "mic_off" : "mic"
                        glyph: String.fromCodePoint(isl.micMuted ? 0xF036D : 0xF036C); color: isl.micMuted ? "#ff8a8a" : (isl.pal ? isl.pal.fg : "white") }
                    MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor
                        onClicked: isl.bar.run("\"$HOME/.local/bin/rice-volume\" mic") } }
                Item { width: 24; height: 24; anchors.verticalCenter: parent.verticalCenter
                    Rectangle { anchors.centerIn: parent; width: 13; height: 13; radius: 4; color: "transparent"; border.width: 2; border.color: "#ff8a8a" }
                    MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor
                        onClicked: { isl.bar.recOn = false; isl.bar.run("\"$HOME/.local/bin/rice-hotcorner\" record stop") } } }
            }
        }

        // ---------- volume / microphone ----------
        Face {
            name: isl.mode === "mic" ? "mic" : "volume"; width: 300; height: isl.bh
            Row {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 16
                spacing: 10
                Item { width: isl.bh - 10; height: width; anchors.verticalCenter: parent.verticalCenter
                    Blob { anchors.fill: parent; kind: Look.shape; color: isl.pal ? isl.pal.primary : "white" }
                    MIcon { anchors.centerIn: parent; size: 16; filled: true; color: isl.pal ? isl.pal.inkPrimary : "black"
                        name: isl.mode === "mic" ? (isl.micMuted ? "mic_off" : "mic") : (isl.muted ? "volume_off" : (isl.vol < 0.5 ? "volume_down" : "volume_up"))
                        glyph: "" } }
                WavyLine {
                    visible: isl.mode !== "mic"
                    width: parent.width - isl.bh - 48; height: 18; anchors.verticalCenter: parent.verticalCenter
                    value: isl.muted ? 0 : Math.min(1, isl.vol); moving: !isl.muted; seekable: true
                    color: isl.pal ? isl.pal.primary : "white"; track: isl.pal ? isl.pal.track : "#444"
                    onMoved: function(v) { Vol.set(isl.bar.sink, v); isl.show("volume", 1600) }
                }
                Text { visible: isl.mode === "mic"; width: parent.width - isl.bh - 48; anchors.verticalCenter: parent.verticalCenter
                    text: isl.micMuted ? Tr.t("Microphone off") : Tr.t("Microphone on"); color: isl.pal ? isl.pal.fg : "white"; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.Medium }
                Text { width: 30; anchors.verticalCenter: parent.verticalCenter; horizontalAlignment: Text.AlignRight; visible: isl.mode !== "mic"
                    text: isl.muted ? "0" : Math.round(isl.vol * 100); color: isl.pal ? isl.pal.fg : "white"; font.family: Fonts.numbers; font.pixelSize: 13; font.weight: Font.Medium }
            }
        }

        // ---------- the charger: plugged in or out ----------
        // plugged in: the bolt pops in with two green rings spreading from it, a glow sweeps through the island,
        // the bar fills up to the battery level and the number counts up. Unplugged: the bolt drops out, the
        // battery icon falls in with a small bounce, the bar shows what is left.
        Face {
            id: chgFace
            name: "charge"; width: 340; height: isl.bh
            readonly property bool on: isl.plugged
            readonly property color green: "#9be08f"
            readonly property color tone: on ? green : (isl.bar && isl.bar.battPct <= 15 ? "#ff8a80" : (isl.pal ? isl.pal.primary : "white"))
            property real fillV: 0          // the bar, 0..1
            property real shown: 0          // the number
            property real pop: 1            // the icon's size
            property real ring: 1           // 0..1: the rings spreading
            property real sweep: 1          // 0..1: the glow going through
            onActChanged: if (act) { chgAnim.stop(); chgAnim.start() }
            readonly property real pct: isl.bar ? isl.bar.battPct : 0
            onPctChanged: if (!chgAnim.running) { fillV = pct / 100; shown = pct }
            ParallelAnimation {
                id: chgAnim
                SequentialAnimation {
                    PropertyAction { target: chgFace; property: "pop"; value: 0.2 }
                    PauseAnimation { duration: 120 }
                    NumberAnimation { target: chgFace; property: "pop"; to: 1; duration: 520; easing.type: Easing.OutBack; easing.overshoot: 3 }
                }
                SequentialAnimation {
                    PropertyAction { target: chgFace; property: "ring"; value: 0 }
                    PauseAnimation { duration: 160 }
                    NumberAnimation { target: chgFace; property: "ring"; to: 1; duration: 1100; easing.type: Easing.OutCubic }
                }
                SequentialAnimation {
                    PropertyAction { target: chgFace; property: "sweep"; value: 0 }
                    PauseAnimation { duration: 220 }
                    NumberAnimation { target: chgFace; property: "sweep"; to: 1; duration: 1300; easing.type: Easing.InOutQuad }
                }
                SequentialAnimation {
                    PropertyAction { target: chgFace; property: "fillV"; value: chgFace.on ? 0 : chgFace.pct / 100 }
                    PropertyAction { target: chgFace; property: "shown"; value: chgFace.on ? 0 : chgFace.pct }
                    PauseAnimation { duration: 200 }
                    ParallelAnimation {
                        NumberAnimation { target: chgFace; property: "fillV"; to: chgFace.pct / 100; duration: 1200; easing.type: Easing.OutCubic }
                        NumberAnimation { target: chgFace; property: "shown"; to: chgFace.pct; duration: 1200; easing.type: Easing.OutCubic }
                    }
                }
            }
            // the glow that goes through once: drawn as a round, soft-ended pill (a Rectangle with a gradient
            // ignores its radius and showed square ends), then only moved
            Canvas {
                id: glow
                visible: chgFace.on && chgFace.sweep > 0 && chgFace.sweep < 1
                width: 110; height: parent.height - 12; y: 6
                x: 8 + (parent.width - 16 - width) * chgFace.sweep
                opacity: Math.sin(Math.PI * chgFace.sweep) * 0.8
                onPaint: {
                    const c = getContext("2d"); c.reset()
                    const w = width, h = height, r = h / 2
                    c.beginPath(); c.moveTo(r, 0); c.lineTo(w - r, 0); c.arc(w - r, r, r, -Math.PI / 2, Math.PI / 2)
                    c.lineTo(r, h); c.arc(r, r, r, Math.PI / 2, Math.PI * 1.5); c.closePath(); c.clip()
                    const g = c.createLinearGradient(0, 0, w, 0)
                    g.addColorStop(0, "rgba(155,224,143,0)"); g.addColorStop(0.5, "rgba(155,224,143,0.85)"); g.addColorStop(1, "rgba(155,224,143,0)")
                    c.fillStyle = g; c.fillRect(0, 0, w, h)
                }
                Component.onCompleted: requestPaint()
            }
            Row {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 16
                spacing: 10
                Item { width: isl.bh - 10; height: width; anchors.verticalCenter: parent.verticalCenter
                    // two rings spreading out of the icon
                    Repeater {
                        model: 2
                        delegate: Rectangle {
                            required property int index
                            readonly property real r: Math.max(0, Math.min(1, chgFace.ring * 1.25 - index * 0.25))
                            visible: chgFace.on && r > 0 && r < 1
                            anchors.centerIn: parent
                            width: parent.width * (1 + 0.32 * r); height: width; radius: width / 2
                            color: "transparent"; border.width: 2; border.color: chgFace.green
                            opacity: (1 - r) * 0.9
                        }
                    }
                    Blob { anchors.fill: parent; kind: Look.shape; scale: 0.85 + 0.15 * chgFace.pop
                        color: chgFace.on ? chgFace.green : (isl.pal ? isl.pal.container : "#333")
                        Behavior on color { ColorAnimation { duration: 300 } } }
                    MIcon { anchors.centerIn: parent; size: 16; filled: true; glyph: ""
                        name: chgFace.on ? "bolt" : (chgFace.pct <= 15 ? "battery_alert" : "battery_full")
                        scale: chgFace.pop; rotation: (1 - chgFace.pop) * (chgFace.on ? -40 : 25)
                        color: chgFace.on ? "#0f2a0c" : (isl.pal ? isl.pal.inkContainer : "white") }
                }
                Column {
                    width: parent.width - isl.bh - 66; anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text { text: chgFace.on ? (chgFace.pct >= 99 ? Tr.t("Fully charged") : Tr.t("Charging")) : Tr.t("On battery")
                        color: isl.pal ? isl.pal.fg : "white"; font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.Medium }
                    Rectangle {
                        width: parent.width; height: 5; radius: 2.5
                        color: isl.pal ? isl.pal.track : "#444"
                        Rectangle {
                            width: Math.max(height, parent.width * chgFace.fillV); height: parent.height; radius: 2.5; clip: true
                            color: chgFace.tone
                            Behavior on color { ColorAnimation { duration: 300 } }
                            // a light that runs along the bar while it charges
                            Rectangle {
                                visible: chgFace.on && chgFace.act
                                width: 16; height: parent.height; radius: height / 2
                                property real k: 0
                                x: (parent.width - width) * k
                                color: "white"
                                opacity: Math.sin(Math.PI * k) * 0.55
                                NumberAnimation on k { from: 0; to: 1; duration: 1100; loops: Animation.Infinite; running: chgFace.on && chgFace.act }
                            }
                        }
                    }
                }
                Text { width: 40; anchors.verticalCenter: parent.verticalCenter; horizontalAlignment: Text.AlignRight
                    text: Math.round(chgFace.shown) + "%"; color: chgFace.on ? chgFace.green : (isl.pal ? isl.pal.fg : "white")
                    font.family: Fonts.numbers; font.pixelSize: 14; font.weight: Font.DemiBold }
            }
        }

        // ---------- workspace ----------
        Face {
            name: "ws"; width: isl.tw; height: isl.bh
            Row {
                id: wsRow
                anchors.centerIn: parent
                spacing: 7
                Text { anchors.verticalCenter: parent.verticalCenter; text: Tr.t("Workspace"); color: isl.pal ? isl.pal.dim : "#aaa"; font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.Medium; rightPadding: 4 }
                Repeater {
                    model: isl.bar ? Math.min(10, isl.bar.maxWs()) : 5
                    // follows Settings > DarkShadow bar > Workspace style: dots, Pac-Man, letters or numbers
                    delegate: Item {
                        id: wc
                        readonly property int wid: index + 1
                        readonly property bool act: isl.wsId === wid
                        readonly property string st: isl.bar ? isl.bar.wsStyle : "dots"
                        anchors.verticalCenter: parent.verticalCenter
                        width: st === "dots" ? (act ? 30 : 8) : (st === "pacman" ? 16 : 22)
                        height: 22
                        Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                        // dots: the active one a pill with its number
                        Rectangle {
                            visible: wc.st === "dots"
                            anchors.centerIn: parent
                            width: parent.width; height: wc.act ? 20 : 8; radius: height / 2
                            Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                            color: wc.act ? (isl.bar ? isl.bar.wsTint(wc.wid) : "white") : (isl.pal ? isl.pal.track : "#555")
                            Behavior on color { ColorAnimation { duration: 300 } }
                            Text { anchors.centerIn: parent; visible: wc.act; text: wc.wid; color: isl.pal ? isl.pal.inkPrimary : "black"; font.family: Fonts.main; font.pixelSize: 11; font.weight: Font.Bold }
                        }
                        // letters / numbers: the active one in a round pill of your key color
                        Rectangle {
                            visible: wc.st === "letters" || wc.st === "numbers"
                            anchors.centerIn: parent; width: 22; height: 22; radius: 11
                            color: isl.bar ? isl.bar.wsTint(wc.wid) : "white"
                            opacity: wc.act ? 1 : 0; scale: wc.act ? 1 : 0.4
                            Behavior on opacity { NumberAnimation { duration: 180 } }
                            Behavior on scale { NumberAnimation { duration: 300; easing.type: Easing.OutBack } }
                        }
                        Text {
                            visible: wc.st === "letters" || wc.st === "numbers"
                            anchors.centerIn: parent
                            text: wc.st === "numbers" ? String(wc.wid) : String.fromCharCode(64 + wc.wid)
                            color: wc.act ? (isl.pal ? isl.pal.inkPrimary : "black") : (isl.pal ? isl.pal.dim : "#aaa")
                            font.family: Fonts.main; font.pixelSize: 12; font.weight: wc.act ? Font.Bold : Font.Medium
                        }
                        // Pac-Man: dots he has eaten fade, he sits on the one you are on
                        Rectangle {
                            visible: wc.st === "pacman" && !wc.act
                            anchors.centerIn: parent; width: 6; height: 6; radius: 3
                            color: isl.pal ? isl.pal.fg : "white"
                            opacity: wc.wid < isl.wsId ? 0.25 : 0.8
                        }
                        Canvas {
                            visible: wc.st === "pacman" && wc.act
                            anchors.centerIn: parent; width: 16; height: 16
                            property real mouth: 0.22
                            SequentialAnimation on mouth {
                                running: wc.act && wc.st === "pacman" && isl.mode === "ws"; loops: Animation.Infinite
                                NumberAnimation { to: 0.02; duration: 120 } NumberAnimation { to: 0.26; duration: 120 }
                            }
                            onMouthChanged: requestPaint()
                            onVisibleChanged: if (visible) requestPaint()
                            onPaint: {
                                const c = getContext("2d"); c.reset()
                                c.fillStyle = isl.pal ? isl.pal.primary : "#ffd84a"
                                c.beginPath(); c.moveTo(8, 8); c.arc(8, 8, 8, mouth * Math.PI, (2 - mouth) * Math.PI); c.closePath(); c.fill()
                            }
                        }
                    }
                }
            }
        }

    }

    // ---------- the card: the player or a notification, just beside the bar ----------
    Shadow { anchors.fill: card; radius: card.radius; on: Look.shadows && Look.shadowBar; opacity: card.opacity; size: Look.shadowSize; strength: Look.shadowStrength }
    Rectangle {
        id: card
        // it is the island growing: at 0 it sits exactly on the island, at 1 it is the card beside the bar
        // (a container transform: size, place and corners morph together, with a little overshoot)
        property real t: isl.big ? 1 : 0
        onTChanged: if (t < 0.01 && !isl.big) isl.cardMode = ""
        // slow and soft on the way in, quicker on the way out
        Behavior on t { NumberAnimation { duration: isl.big ? (Look.lite ? 340 : 620) : 280
            easing.type: isl.big ? Easing.OutBack : Easing.InOutCubic; easing.overshoot: Look.lite ? 0 : 0.9 } }
        property real cwA: isl.cw
        property real chA: isl.ch
        Behavior on cwA { SpringAnimation { spring: 2.4; damping: 0.36; epsilon: 0.4 } }
        Behavior on chA { SpringAnimation { spring: 2.4; damping: 0.36; epsilon: 0.4 } }
        readonly property real tc: Math.max(0, t)
        width: Math.max(isl.bh, isl.width + (cwA - isl.width) * tc)
        height: Math.max(isl.bh * 0.6, isl.bh + (chA - isl.bh) * tc)
        // centred on the island, but always on the screen
        x: Math.max(-isl.x, Math.min(isl.areaW - isl.x - width, (isl.width - width) / 2))
        y: isl.down ? (isl.bh + 8) * Math.min(1, tc) : isl.bh - height - (isl.bh + 8) * Math.min(1, tc)
        radius: Math.min(height / 2, isl.bh / 2 + (30 - isl.bh / 2) * Math.min(1, tc))
        color: isl.fill
        visible: opacity > 0.01
        opacity: t < 0.02 ? 0 : Math.min(1, t * 4)
        clip: true
        HoverHandler { id: hovC }
        // the player tints the card with your key color
        Rectangle {
            anchors.fill: parent; radius: parent.radius
            opacity: isl.cardMode === "media" ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 300 } }
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: isl.pal ? Qt.rgba(isl.pal.primary.r, isl.pal.primary.g, isl.pal.primary.b, 0.2) : "transparent" }
                GradientStop { position: 0.7; color: "transparent" }
            }
        }


        // ---------- the info card: today (calendar, weather, the day) and system (CPU, GPU, RAM, disk) ----------
        Face {
            id: fInfo
            name: "info"; inCard: true; width: 520; height: 318
            readonly property var pp: isl.pal
            // tabs: a pill of your key color slides under the one that is open
            Item {
                id: tabs
                anchors.horizontalCenter: parent.horizontalCenter; y: isl.down ? 14 : 14
                width: tabRow.width + 8; height: 38
                Rectangle { anchors.fill: parent; radius: height / 2; color: fInfo.pp ? fInfo.pp.card : "#222" }
                Rectangle {
                    id: tabPill
                    readonly property var cur: isl.infoTab === "system" ? tSys : (isl.infoTab === "about" ? tAbout : tToday)
                    x: 4 + cur.x; y: 4; width: cur.width; height: parent.height - 8; radius: height / 2
                    color: fInfo.pp ? fInfo.pp.primary : "white"
                    Behavior on x { SpringAnimation { spring: 4; damping: 0.4; epsilon: 0.3 } }
                    Behavior on width { SpringAnimation { spring: 4; damping: 0.4; epsilon: 0.3 } }
                }
                Row {
                    id: tabRow
                    x: 4; y: 4; height: parent.height - 8
                    Tab { id: tToday; label: Tr.t("Today"); icon: "calendar_today"; on: isl.infoTab === "today"; onHit: isl.infoTab = "today" }
                    Tab { id: tSys; label: Tr.t("System"); icon: "memory"; on: isl.infoTab === "system"; onHit: isl.infoTab = "system" }
                    Tab { id: tAbout; label: Tr.t("About"); icon: "info"; on: isl.infoTab === "about"; onHit: isl.infoTab = "about" }
                    Tab { visible: Player.has; label: Tr.t("Music"); icon: "music_note"; on: false
                        onHit: { isl.infoOpen = false; isl.pinned = true; if (!isl.hovering) pinT.restart() } }
                }
            }

            // ---------- today ----------
            Item {
                id: today
                x: 0; y: 62; width: parent.width; height: parent.height - 62
                opacity: isl.infoTab === "today" ? 1 : 0
                visible: opacity > 0.01
                Behavior on opacity { NumberAnimation { duration: 220 } }
                transform: Translate { x: isl.infoTab === "today" ? 0 : -24; Behavior on x { NumberAnimation { duration: 320; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.decel } } }
                property var now: new Date()
                Timer { interval: 30000; running: fInfo.act; repeat: true; triggeredOnStart: true; onTriggered: today.now = new Date() }
                // left: the day and the weather
                Column {
                    x: 22; y: 4; width: 228; spacing: 2
                    Text { text: Tr.day(today.now.getDay()); color: fInfo.pp ? fInfo.pp.primary : "#aaa"; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.DemiBold }
                    Text { text: today.now.getDate() + " " + Tr.month(today.now.getMonth(), true); color: fInfo.pp ? fInfo.pp.fg : "white"; font.family: Fonts.main; font.pixelSize: 26; font.weight: Font.DemiBold }
                }
                Item {
                    x: 22; y: 74; width: 228; height: 72
                    Item { id: wIc; width: 66; height: 66; anchors.verticalCenter: parent.verticalCenter
                        Blob { anchors.fill: parent; kind: Look.shape; color: fInfo.pp ? fInfo.pp.container : "#333"
                            SlowSpin { turn: 360; period: 40000; running: fInfo.act && !Look.lite } }
                        Text { anchors.centerIn: parent; text: isl.wGlyph(isl.wCond); font.family: Fonts.icons; font.pixelSize: 30; color: fInfo.pp ? fInfo.pp.inkContainer : "white" } }
                    Column {
                        anchors.left: wIc.right; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; spacing: 1
                        width: parent.width - 80
                        Text { text: isl.wTemp !== "" ? isl.wTemp : "--"; color: fInfo.pp ? fInfo.pp.fg : "white"; font.family: Fonts.main; font.pixelSize: 26; font.weight: Font.DemiBold }
                        Text { width: parent.width; elide: Text.ElideRight; text: isl.wCond !== "" ? isl.wCond : Tr.t("No weather yet"); color: fInfo.pp ? fInfo.pp.fg : "white"; font.family: Fonts.main; font.pixelSize: 13 }
                        Text { width: parent.width; elide: Text.ElideRight; visible: text !== ""; text: isl.wPlace; color: fInfo.pp ? fInfo.pp.dim : "#aaa"; font.family: Fonts.main; font.pixelSize: 11 }
                    }
                }
                // how much of the day has gone, as a wave
                readonly property real dayFrac: (now.getHours() * 60 + now.getMinutes()) / 1440
                Text { x: 22; y: 170; text: Tr.t("Day"); color: fInfo.pp ? fInfo.pp.dim : "#aaa"; font.family: Fonts.main; font.pixelSize: 12 }
                Text { x: 250 - width; y: 170; text: Math.round(today.dayFrac * 100) + "%"; color: fInfo.pp ? fInfo.pp.dim : "#aaa"; font.family: Fonts.numbers; font.pixelSize: 12 }
                WavyLine { x: 22; y: 190; width: 228; height: 20; value: today.dayFrac; moving: fInfo.act
                    color: fInfo.pp ? fInfo.pp.primary : "white"; track: fInfo.pp ? fInfo.pp.track : "#444" }

                // right: the month
                Item {
                    id: cal
                    x: 270; y: 0; width: 230; height: parent.height - 10
                    property int off: 0
                    Connections { target: isl; function onInfoOpenChanged() { if (isl.infoOpen) cal.off = 0 } }
                    readonly property var first: new Date(today.now.getFullYear(), today.now.getMonth() + off, 1)
                    readonly property int lead: (first.getDay() + 6) % 7          // Monday first
                    readonly property int count: new Date(first.getFullYear(), first.getMonth() + 1, 0).getDate()
                    Text { x: 4; y: 4; text: Tr.month(cal.first.getMonth()) + " " + cal.first.getFullYear()
                        color: fInfo.pp ? fInfo.pp.fg : "white"; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Row {
                        anchors.right: parent.right; y: 0; spacing: 2
                        Btn { width: 28; height: 28; icon: "chevron_left"; onHit: cal.off-- }
                        Btn { width: 28; height: 28; icon: "chevron_right"; onHit: cal.off++ }
                    }
                    Grid {
                        id: grid
                        y: 34; columns: 7; columnSpacing: 0; rowSpacing: 0
                        readonly property real cw: cal.width / 7
                        Repeater {
                            model: Tr.week(true)
                            delegate: Text { width: grid.cw; height: 22; horizontalAlignment: Text.AlignHCenter; text: modelData
                                color: fInfo.pp ? fInfo.pp.dim : "#aaa"; font.family: Fonts.main; font.pixelSize: 11; font.weight: Font.Medium }
                        }
                        Repeater {
                            model: 42
                            delegate: Item {
                                readonly property int d: index - cal.lead + 1
                                readonly property bool inMonth: d >= 1 && d <= cal.count
                                readonly property bool isToday: inMonth && cal.off === 0 && d === today.now.getDate()
                                width: grid.cw; height: 27
                                visible: index < 35 || cal.lead + cal.count > 35
                                Blob { anchors.centerIn: parent; width: 26; height: 26; visible: parent.isToday; kind: Look.shape
                                    color: fInfo.pp ? fInfo.pp.primary : "white" }
                                Text { anchors.centerIn: parent; visible: parent.inMonth; text: parent.d
                                    color: parent.isToday ? (fInfo.pp ? fInfo.pp.inkPrimary : "black") : (fInfo.pp ? fInfo.pp.fg : "white")
                                    font.family: Fonts.main; font.pixelSize: 12; font.weight: parent.isToday ? Font.Bold : Font.Normal }
                            }
                        }
                    }
                }
            }


            // ---------- about: your system and DarkShadow's Shell (with updates) ----------
            Item {
                id: ab
                x: 0; y: 62; width: parent.width; height: parent.height - 62
                opacity: isl.infoTab === "about" ? 1 : 0
                visible: opacity > 0.01
                Behavior on opacity { NumberAnimation { duration: 220 } }
                transform: Translate { x: isl.infoTab === "about" ? 0 : 24; Behavior on x { NumberAnimation { duration: 320; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.decel } } }
                // the system: its logo in your shape, its name
                Item {
                    id: osIc
                    x: 28; y: 8; width: 96; height: 96
                    Blob { anchors.fill: parent; kind: Look.shape; color: fInfo.pp ? fInfo.pp.primary : "white"
                        SlowSpin { turn: 360; period: 36000; running: ab.visible && !Look.lite } }
                    Text { anchors.centerIn: parent; anchors.horizontalCenterOffset: 2
                        text: isl.bar && isl.bar.logos[isl.bar.logoKey] !== undefined ? String.fromCodePoint(isl.bar.logos[isl.bar.logoKey]) : String.fromCodePoint(0xF17C)
                        font.family: Fonts.icons; font.pixelSize: 44; color: fInfo.pp ? fInfo.pp.inkPrimary : "black" }
                }
                Column {
                    x: 142; y: 18; width: parent.width - 142 - 24; spacing: 3
                    Text { text: Tr.t("Your system"); color: fInfo.pp ? fInfo.pp.primary : "#aaa"; font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Text { width: parent.width; elide: Text.ElideRight; text: isl.osName; color: fInfo.pp ? fInfo.pp.fg : "white"; font.family: Fonts.main; font.pixelSize: 24; font.weight: Font.DemiBold }
                    Text { width: parent.width; elide: Text.ElideRight; text: Tr.t("DarkShadow's Shell") + (isl.up.installed ? "  V" + isl.up.installed : "")
                        color: fInfo.pp ? fInfo.pp.dim : "#aaa"; font.family: Fonts.main; font.pixelSize: 13 }
                }
                // updates
                Rectangle {
                    x: 24; y: 124; width: parent.width - 48; height: 104; radius: 24
                    color: isl.up.update ? (fInfo.pp ? fInfo.pp.container : "#333") : (fInfo.pp ? fInfo.pp.card : "#222")
                    Behavior on color { ColorAnimation { duration: 260 } }
                    Item { id: upIc; x: 18; anchors.verticalCenter: parent.verticalCenter; width: 48; height: 48
                        Blob { anchors.fill: parent; kind: isl.up.update ? Look.shape : "circle"; color: fInfo.pp ? (isl.up.update ? fInfo.pp.primary : fInfo.pp.cardHi) : "#444"
                            RotationAnimator on rotation { from: 0; to: 360; duration: 2400; loops: Animation.Infinite; running: isl.checking && ab.visible } }
                        MIcon { anchors.centerIn: parent; size: 22; filled: true; glyph: ""
                            name: isl.checking ? "sync" : (isl.up.update ? "system_update_alt" : "check_circle")
                            color: fInfo.pp ? (isl.up.update ? fInfo.pp.inkPrimary : fInfo.pp.fg) : "white" } }
                    Column {
                        anchors.left: upIc.right; anchors.leftMargin: 14; anchors.right: upBtn.left; anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter; spacing: 2
                        Text { width: parent.width; elide: Text.ElideRight
                            text: isl.checking && !isl.up.installed ? Tr.t("Looking for updates") : (isl.up.update ? Tr.t("Update V") + isl.up.latest + Tr.t(" is ready") : Tr.t("You are up to date"))
                            color: isl.up.update ? (fInfo.pp ? fInfo.pp.inkContainer : "white") : (fInfo.pp ? fInfo.pp.fg : "white")
                            font.family: Fonts.main; font.pixelSize: 15; font.weight: Font.DemiBold }
                        Text { width: parent.width; elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap
                            text: isl.up.update && isl.up.news && isl.up.news.length > 0 ? isl.up.news[0] : (isl.checking ? Tr.t("Asking GitHub ...") : Tr.t("Checked ") + isl.ago(isl.up.checked))
                            color: fInfo.pp ? fInfo.pp.dim : "#aaa"; font.family: Fonts.main; font.pixelSize: 12 }
                        // a wave while it looks
                        WavyLine { visible: isl.checking; width: parent.width; height: 14; value: 1; handle: false; moving: true; thick: 3
                            color: fInfo.pp ? fInfo.pp.primary : "white"; track: "transparent" }
                    }
                    Rectangle {
                        id: upBtn
                        anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter
                        width: ubt.implicitWidth + 36; height: 42; radius: ubm.pressed ? 14 : 21
                        Behavior on radius { NumberAnimation { duration: 200 } }
                        color: isl.up.update ? (fInfo.pp ? fInfo.pp.primary : "white") : (fInfo.pp ? fInfo.pp.cardHi : "#444")
                        Text { id: ubt; anchors.centerIn: parent; text: isl.up.update ? Tr.t("Update now") : Tr.t("Check")
                            color: isl.up.update ? (fInfo.pp ? fInfo.pp.inkPrimary : "black") : (fInfo.pp ? fInfo.pp.fg : "white")
                            font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.DemiBold }
                        MouseArea { id: ubm; anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (isl.up.update) { isl.infoOpen = false; DssWin.run([((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-update", "center"]) }
                                else isl.checkUpdates()
                            } }
                    }
                }
            }

            // ---------- system ----------
            Item {
                id: sys
                x: 0; y: 62; width: parent.width; height: parent.height - 62
                opacity: isl.infoTab === "system" ? 1 : 0
                visible: opacity > 0.01
                Behavior on opacity { NumberAnimation { duration: 220 } }
                transform: Translate { x: isl.infoTab === "system" ? 0 : 24; Behavior on x { NumberAnimation { duration: 320; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.decel } } }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter; y: 6
                    spacing: 26
                    Ring { label: "CPU"; value: isl.st.cpu ? isl.st.cpu.usage / 100 : 0; temp: isl.st.cpu ? isl.st.cpu.temp : null }
                    Ring { label: "GPU"; value: isl.st.gpu && isl.st.gpu.usage !== null ? isl.st.gpu.usage / 100 : 0; temp: isl.st.gpu ? isl.st.gpu.temp : null
                        none: !isl.st.gpu || isl.st.gpu.usage === null }
                    Ring { label: "RAM"; value: isl.st.mem ? isl.st.mem.pct / 100 : 0
                        sub: isl.st.mem ? isl.gb(isl.st.mem.used) + " / " + isl.gb(isl.st.mem.total) : "" }
                }
                // the disk, as a wave
                readonly property var disk: isl.disk0
                Text { x: 32; y: 166; text: Tr.t("Disk  ") + (sys.disk ? sys.disk.mount : "/"); color: fInfo.pp ? fInfo.pp.dim : "#aaa"; font.family: Fonts.main; font.pixelSize: 12 }
                Text { x: parent.width - 32 - width; y: 166; text: sys.disk ? isl.gb(sys.disk.used) + " / " + isl.gb(sys.disk.total) : ""
                    color: fInfo.pp ? fInfo.pp.dim : "#aaa"; font.family: Fonts.numbers; font.pixelSize: 12 }
                WavyLine { x: 32; y: 186; width: parent.width - 64; height: 20; value: sys.disk ? sys.disk.pct / 100 : 0; moving: false
                    color: fInfo.pp ? fInfo.pp.primary : "white"; track: fInfo.pp ? fInfo.pp.track : "#444" }
                Text { x: 32; y: 216; text: isl.st.uptime ? Tr.t("Up ") + isl.upText(isl.st.uptime) : ""; color: fInfo.pp ? fInfo.pp.dim : "#aaa"; font.family: Fonts.main; font.pixelSize: 11 }
            }
        }

        // ---------- a notification ----------
        Face {
            name: "notif"; inCard: true; width: 440; height: 96
            readonly property var n: isl.note
            Item {
                id: nIc
                x: 16; y: 16; width: 46; height: 46
                Blob { anchors.fill: parent; kind: Look.shape; color: isl.pal ? isl.pal.container : "#333"
                    SlowSpin { turn: 360; period: 24000; running: isl.big && isl.cardMode === "notif" && !Look.lite } }
                Image { id: nImg; anchors.centerIn: parent; width: 24; height: 24; sourceSize.width: 48; sourceSize.height: 48
                    source: { const n = isl.note; if (!n) return ""
                        if (n.appIcon && n.appIcon !== "") return n.appIcon.indexOf("/") === 0 ? "file:///" + n.appIcon : Icons.get(n.appIcon, true)
                        return Icons.get((n.appName || "dialog-information").toLowerCase(), true) }
                    visible: status === Image.Ready }
                MIcon { anchors.centerIn: parent; size: 20; filled: true; name: "notifications"; glyph: ""; visible: nImg.status !== Image.Ready
                    color: isl.pal ? isl.pal.inkContainer : "white" }
            }
            Column {
                x: 74; y: 14; width: parent.width - 74 - 46; spacing: 2
                Text { text: isl.note ? (isl.note.appName || "Notification") : ""; color: isl.pal ? isl.pal.dim : "#aaa"; font.family: Fonts.main; font.pixelSize: 11 }
                Text { width: parent.width; elide: Text.ElideRight; text: isl.note ? (isl.note.summary || "") : ""
                    color: isl.pal ? isl.pal.fg : "white"; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold }
                Text { width: parent.width; elide: Text.ElideRight; maximumLineCount: 1; textFormat: Text.PlainText
                    text: isl.note ? String(isl.note.body || "").replace(/<[^>]*>/g, "") : ""; color: isl.pal ? isl.pal.dim : "#aaa"; font.family: Fonts.main; font.pixelSize: 12 }
            }
            // how long it stays, as a wave that runs out
            WavyLine { x: 74; y: parent.height - 22; width: parent.width - 74 - 20; height: 14
                value: isl.noteLeft; handle: false; moving: true; thick: 3
                color: isl.pal ? isl.pal.primary : "white"; track: isl.pal ? isl.pal.track : "#444" }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                onClicked: { isl.note = null; isl.bar.run("\"$HOME/.local/bin/rice-cc\" open") } }
            Rectangle {
                anchors.right: parent.right; anchors.rightMargin: 10; y: 10; width: 28; height: 28; radius: 14
                color: xm.containsMouse ? (isl.pal ? isl.pal.cardHi : "#444") : "transparent"
                MIcon { anchors.centerIn: parent; size: 15; name: "close"; glyph: ""; color: isl.pal ? isl.pal.dim : "#aaa" }
                MouseArea { id: xm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { if (isl.note && isl.nf) isl.nf.dismiss(isl.note); isl.note = null } }
            }
        }

        // ---------- the player ----------
        Face {
            name: "media"; inCard: true; width: 470; height: 184
            ShapeImage {
                x: 16; y: 16; width: 78; height: 78
                kind: Look.shape; source: Player.art
                empty: isl.pal ? isl.pal.container : "#333"; fallbackColor: isl.pal ? isl.pal.inkContainer : "#ccc"
            }
            Column {
                x: 110; y: 18; width: parent.width - 110 - 70; spacing: 3
                Text { text: (Player.p && Player.p.identity ? Player.p.identity : Tr.t("Music")).toUpperCase(); color: isl.pal ? isl.pal.primary : "#aaa"
                    font.family: Fonts.main; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 1.2 }
                Text { width: parent.width; elide: Text.ElideRight; text: Player.title; color: isl.pal ? isl.pal.fg : "white"
                    font.family: Fonts.main; font.pixelSize: 17; font.weight: Font.DemiBold }
                Text { width: parent.width; elide: Text.ElideRight; text: Player.artist; color: isl.pal ? isl.pal.dim : "#aaa"; font.family: Fonts.main; font.pixelSize: 13 }
            }
            // a wave that moves while it plays
            Eq { x: parent.width - 58; y: 22; h: 22; bars: 5; go: isl.big && Player.playing && isl.cardMode === "media" }
            Text { x: 16; y: 110; width: 38; text: Player.clock(Player.pos); color: isl.pal ? isl.pal.dim : "#aaa"; font.family: Fonts.numbers; font.pixelSize: 11 }
            WavyLine {
                x: 56; y: 105; width: parent.width - 112; height: 22
                value: Player.frac; moving: Player.playing; seekable: true
                color: isl.pal ? isl.pal.primary : "white"; track: isl.pal ? isl.pal.track : "#444"
                onMoved: function(v) { Player.seekTo(v) }
            }
            Text { x: parent.width - 52; y: 110; width: 38; horizontalAlignment: Text.AlignRight
                text: Player.length > 0 ? "-" + Player.clock(Player.length - Player.pos) : ""; color: isl.pal ? isl.pal.dim : "#aaa"; font.family: Fonts.numbers; font.pixelSize: 11 }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter; y: 134
                spacing: 22
                Btn { icon: "skip_previous"; onHit: Player.prev() }
                // play / pause: your shape while it plays, a circle while it waits; pressing squeezes it
                Item {
                    width: 40; height: 40; anchors.verticalCenter: parent.verticalCenter
                    Blob { anchors.fill: parent; kind: Player.playing ? Look.shape : "circle"; color: isl.pal ? isl.pal.primary : "white"
                        scale: pm.pressed ? 0.86 : (pm.containsMouse ? 1.06 : 1)
                        Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                        SlowSpin { turn: 360; period: 12000; running: Player.playing && isl.big && isl.cardMode === "media" && !Look.lite } }
                    MIcon { anchors.centerIn: parent; size: 20; filled: true; name: Player.playing ? "pause" : "play_arrow"; glyph: ""
                        color: isl.pal ? isl.pal.inkPrimary : "black" }
                    MouseArea { id: pm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Player.toggle() }
                }
                Btn { icon: "skip_next"; onHit: Player.next() }
            }
            MouseArea { x: parent.width - 40; y: parent.height - 40; width: 30; height: 30; cursorShape: Qt.PointingHandCursor
                visible: isl.pinned; onClicked: isl.pinned = false
                MIcon { anchors.centerIn: parent; size: 16; name: "keyboard_arrow_up"; glyph: ""; color: isl.pal ? isl.pal.dim : "#aaa"; rotation: isl.down ? 0 : 180 } }
        }
    }

    // one face of the island: it shows when its mode is on, after the shape has begun to grow
    component Face: Item {
        id: fc
        property string name: ""
        property bool inCard: false
        readonly property bool act: inCard ? (isl.big && isl.cardMode === name) : isl.faceNow === name
        x: Math.round((parent.width - width) / 2)
        y: isl.down ? 0 : Math.round(parent.height - height)
        // it comes in once the shape has (nearly) grown to it, so it is never seen cut
        opacity: act && parent.width >= width * 0.82 && parent.height >= height * 0.82 ? 1 : 0
        scale: act ? 1 : 0.94
        visible: opacity > 0.01
        enabled: act
        // the old face leaves first, then the new one comes in (they never show on top of each other)
        Behavior on opacity { SequentialAnimation {
            PauseAnimation { duration: fc.act ? 20 : 0 }
            NumberAnimation { target: fc; property: "opacity"; duration: fc.act ? 170 : 60; easing.type: Easing.Linear } } }
        Behavior on scale { NumberAnimation { duration: 300; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.decel } }
    }
    // a small equalizer: round bars in your key color that bounce while music plays
    component Eq: Row {
        id: eq
        property int bars: 4
        property real h: 14
        property bool go: true
        spacing: 2.5
        Repeater {
            model: eq.bars
            delegate: Rectangle {
                id: bb
                width: 3.5; radius: 2; anchors.verticalCenter: parent.verticalCenter
                color: isl.pal ? isl.pal.primary : "white"
                property real lv: 0.4
                height: Math.max(4, eq.h * lv)
                SequentialAnimation on lv {
                    running: eq.go && eq.visible && !Look.lite; loops: Animation.Infinite
                    NumberAnimation { to: 1.0; duration: 260 + index * 70; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0.3 + (index % 2) * 0.15; duration: 300 + index * 50; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0.75; duration: 220 + index * 90; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0.35; duration: 280 + index * 40; easing.type: Easing.InOutSine }
                }
            }
        }
    }
    component Tab: Item {
        id: tb
        property string label: ""
        property string icon: ""
        property bool on: false
        signal hit()
        width: visible ? tr.implicitWidth + 28 : 0; height: parent ? parent.height : 30
        Row { id: tr; anchors.centerIn: parent; spacing: 6
            MIcon { anchors.verticalCenter: parent.verticalCenter; size: 16; filled: tb.on; name: tb.icon; glyph: ""
                color: tb.on ? (isl.pal ? isl.pal.inkPrimary : "black") : (isl.pal ? isl.pal.fg : "white") }
            Text { anchors.verticalCenter: parent.verticalCenter; text: tb.label; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.Medium
                color: tb.on ? (isl.pal ? isl.pal.inkPrimary : "black") : (isl.pal ? isl.pal.fg : "white")
                Behavior on color { ColorAnimation { duration: 200 } } } }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: tb.hit() }
    }
    // a wavy ring with a number in the middle (System)
    component Ring: Item {
        id: rg
        property string label: ""
        property real value: 0
        property var temp: null
        property string sub: ""
        property bool none: false
        width: 120; height: 150
        Behavior on value { NumberAnimation { duration: 600; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }
        Item {
            width: 104; height: 104; anchors.horizontalCenter: parent.horizontalCenter
            WavyArc { anchors.fill: parent; value: rg.none ? 0 : rg.value; moving: true; thick: Math.max(4, Look.waveThick)
                color: rg.value > 0.85 ? "#ffb4ab" : (isl.pal ? isl.pal.primary : "white"); track: isl.pal ? isl.pal.track : "#444" }
            Column { anchors.centerIn: parent; spacing: 0
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: rg.none ? "--" : Math.round(rg.value * 100) + "%"
                    color: isl.pal ? isl.pal.fg : "white"; font.family: Fonts.main; font.pixelSize: 20; font.weight: Font.DemiBold }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: rg.temp !== null && rg.temp !== undefined ? rg.temp + "°" : ""
                    visible: text !== ""; color: isl.pal ? isl.pal.dim : "#aaa"; font.family: Fonts.numbers; font.pixelSize: 11 } }
        }
        Column { y: 110; anchors.horizontalCenter: parent.horizontalCenter; spacing: 1
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: rg.label; color: isl.pal ? isl.pal.fg : "white"; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.Medium }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: rg.sub; visible: text !== ""; color: isl.pal ? isl.pal.dim : "#aaa"; font.family: Fonts.numbers; font.pixelSize: 10 } }
    }
    component Btn: Item {
        id: bt
        property string icon: ""
        signal hit()
        width: 34; height: 34
        Rectangle { anchors.fill: parent; radius: width / 2; color: isl.pal ? isl.pal.fg : "white"; opacity: bm.containsMouse ? 0.1 : 0
            Behavior on opacity { NumberAnimation { duration: 140 } } }
        MIcon { anchors.centerIn: parent; size: 20; filled: true; name: bt.icon; glyph: ""; color: isl.pal ? isl.pal.fg : "white" }
        MouseArea { id: bm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: bt.hit() }
    }
}
