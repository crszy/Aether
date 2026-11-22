// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Services.Pipewire
import Quickshell.Widgets
import QtQuick

// The game bar (SUPER + G): slides in from the left over whatever you play. CPU and GPU in soft shapes with a
// wavy ring, RAM, ping and heat, screenshot / record / microphone, power mode, gaming mode, Do Not Disturb,
// volume and brightness. It reads anything only while it is open.
PanelWindow {
    screen: Mon.screen
    id: gp
    property bool open: false
    property real t: 0
    // t runs evenly in time; the panel and each part ease on their own (quick start, long soft landing, like
    // Material 3's emphasized-decelerate), one after another from the top. Closing is a short accelerating fade.
    Behavior on t { NumberAnimation { duration: gp.open ? 760 : 260; easing.type: Easing.Linear } }
    function ease(p) { return 1 - Math.pow(1 - p, 4) }
    readonly property real et: ease(t)
    function stg(i) { return ease(Math.max(0, Math.min(1, (gp.t - 0.08 - i * 0.04) / 0.6))) }   // i up to 8
    onOpenChanged: { t = open ? 1 : 0; if (open) refresh() }

    // ---------- colors: dark tonal surfaces with the theme color ----------
    readonly property real hue: Look.accent.hslHue < 0 ? 0.55 : Look.accent.hslHue
    readonly property bool mono: Look.accent.hslHue < 0 || Look.accent.hslSaturation < 0.1
    readonly property real sat: mono ? 0 : Math.max(0.35, Math.min(0.85, Look.accent.hslSaturation))
    readonly property color primary: Qt.hsla(hue, sat, 0.78, 1)
    readonly property color inkPrimary: Qt.hsla(hue, Math.min(sat, 0.5), 0.13, 1)
    readonly property color container: Qt.hsla(hue, sat * 0.35, 0.24, 1)
    readonly property color inkContainer: Qt.hsla(hue, sat * 0.6, 0.88, 1)
    // Settings > Gaming > Game bar: how see-through it is and the blur behind it (desktop.json "gamebar")
    property var gb: ({})
    readonly property real alpha: Look.flat ? 1 : Math.max(0.2, Math.min(1, gb.opacity !== undefined ? Number(gb.opacity) : 0.9))
    readonly property bool blurOn: !Look.flat && gb.blur !== false
    readonly property color surface: Qt.hsla(hue, 0.08, 0.085, alpha)
    readonly property color card: Qt.hsla(hue, 0.1, 0.14, Math.min(1, alpha + 0.1))
    readonly property color cardHi: Qt.hsla(hue, 0.12, 0.19, Math.min(1, alpha + 0.16))
    readonly property color fg: "#ECEDEE"
    readonly property color dim: "#A9ADB2"
    readonly property color track: Qt.rgba(1, 1, 1, 0.1)

    // ---------- data ----------
    // one rice-stats for the whole shell (Stats.qml)
    readonly property var st: Stats.st
    readonly property bool haveGpu: Stats.haveGpu
    StatsHold { on: gp.open }
    property int ping: -1
    property int fps: -1
    property bool fpsOn: false
    property string profile: ""
    property bool havePP: false
    property var fx: ({ gaming: false, opts: {} })
    property bool dnd: false
    property bool recording: false
    property real recStart: 0
    property bool micMuted: false
    property int bright: -1
    property int notifCount: 0
    property var now: new Date()
    readonly property var sink: Pipewire.defaultAudioSink
    PwObjectTracker { objects: [gp.sink] }
    readonly property var win: Wm.focusedWindow
    readonly property string game: win && win.title ? win.title : Tr.t("Desktop")
    function two(n) { return n < 10 ? "0" + n : "" + n }
    readonly property string recTime: {
        const t = Math.max(0, Math.floor(now.getTime() / 1000 - recStart))
        return two(Math.floor(t / 3600)) + ":" + two(Math.floor(t / 60) % 60) + ":" + two(t % 60)
    }

    DssProc {
        id: pingProc
        command: ["bash", "-c", "ping -c1 -W1 1.1.1.1 2>/dev/null | sed -n 's/.*time=\\([0-9.]*\\).*/\\1/p'"]
        stdout: DssCollector { onStreamFinished: { const v = parseFloat(text); gp.ping = isNaN(v) ? -1 : Math.round(v) } }
    }
    DssProc {
        id: fpsProc
        command: ["bash", "-c", "\"$HOME/.local/bin/rice-fps\" now"]
        stdout: DssCollector { onStreamFinished: { const v = parseInt(text); gp.fps = isNaN(v) ? -1 : v } }
    }
    DssProc {
        id: infoProc
        command: ["bash", "-c", [
            "command -v powerprofilesctl >/dev/null && echo \"pp $(powerprofilesctl get 2>/dev/null)\"",
            "echo \"fx $(\"$HOME/.local/bin/rice-effects\" state 2>/dev/null | tr -d '\\n')\"",
            "echo \"mic $(wpctl get-volume @DEFAULT_AUDIO_SOURCE@ 2>/dev/null)\"",
            "echo \"dnd $(\"$HOME/.local/bin/rice-cc\" dnd state 2>/dev/null)\"",
            "grep -q '\"engine\": *\"shell\"' \"$HOME/.config/rice/cc.json\" 2>/dev/null || { command -v swaync-client >/dev/null && echo \"nc $(timeout 2 swaync-client -c -sw 2>/dev/null)\"; }",
            "echo \"fpson $(\"$HOME/.local/bin/rice-fps\" state 2>/dev/null)\"",
            "command -v brightnessctl >/dev/null && echo \"br $(brightnessctl -m 2>/dev/null | cut -d, -f4 | tr -d %)\""
        ].join("; ")]
        stdout: DssCollector {
            onStreamFinished: {
                gp.havePP = false
                text.split("\n").forEach(function(l) {
                    const i = l.indexOf(" "); if (i < 0) return
                    const k = l.substring(0, i), v = l.substring(i + 1).trim()
                    if (k === "pp" && v) { gp.havePP = true; gp.profile = v }
                    else if (k === "fx") { try { gp.fx = JSON.parse(v) } catch (e) { } }
                    else if (k === "mic") gp.micMuted = v.indexOf("MUTED") >= 0
                    else if (k === "dnd") gp.dnd = v === "on"
                    else if (k === "nc") { const n = parseInt(v); gp.notifCount = isNaN(n) ? 0 : n }
                    else if (k === "fpson") gp.fpsOn = v === "on"
                    else if (k === "br") { const n = parseInt(v); gp.bright = isNaN(n) ? -1 : n }
                })
            }
        }
    }
    // recording: the same file the bar watches (rice-hotcorner writes it while it records)
    Trigger {
        path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.cache/rice/record.json"
        every: 2000
        onContent: function(t) {
            let j = null
            try { j = t.trim() !== "" ? JSON.parse(t) : null } catch (e) { j = null }
            gp.recording = j !== null
            if (j) gp.recStart = j.start || Date.now() / 1000
        }
    }
    function refresh() {
        if (!infoProc.running) infoProc.running = true
        if (!pingProc.running) pingProc.running = true
        now = new Date()
    }
    Timer { interval: 3000; running: gp.open; repeat: true; onTriggered: gp.refresh() }
    Timer { interval: 1000; running: gp.open; repeat: true; onTriggered: { gp.now = new Date(); if (!fpsProc.running) fpsProc.running = true } }
    function sh(cmd) { DssWin.run(["bash", "-c", cmd]) }
    function bin(a) { DssWin.run(["bash", "-c", "\"$HOME/.local/bin/" + a[0] + "\" \"${@}\"", "_"].concat(a.slice(1))) }
    function later() { again.restart() }
    Timer { id: again; interval: 700; onTriggered: gp.refresh() }

    Trigger {
        name: "gamebar-open"; fresh: true
        onFired: function(o) {
            const w = o.split(" ")[1] || "toggle"
            gp.open = w === "open" ? true : (w === "close" ? false : !gp.open)
        }
    }

    // ---------- the window ----------
    visible: open || (t > 0.01 && !closeT.done)
    // if the closing animation ever stalls (the compositor sent no frames for a moment), it still goes away
    Timer { id: closeT; property bool done: false; interval: 600; running: !open; onRunningChanged: if (running) done = false; onTriggered: done = true }
    // niri: blur behind it (Hyprland blurs this layer with its own rule)
    NiriBlur { win: gp; items: [panel]; radius: panel.radius; on: gp.open && gp.et > 0.94 && gp.blurOn }
    color: "transparent"
    anchors { top: true; left: true; right: true; bottom: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-gamebar"
    WlrLayershell.keyboardFocus: open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    // a light dim over the game, click it to close
    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.28 * gp.et) }
    MouseArea { anchors.fill: parent; onClicked: gp.open = false }
    Item { focus: gp.open; Keys.onEscapePressed: gp.open = false }

    // ---------- pieces ----------
    // a ring gauge: the used part is a wave, the rest a calm track; the heat sits in a chip above
    component Ring: Item {
        id: g
        property string name: "CPU"
        property real v: 0
        property int heat: 0
        property bool na: false
        width: 150; height: 150
        Canvas {
            id: rc
            anchors.fill: parent
            property real vv: g.na ? 0 : g.v
            property real ph: 0
            Behavior on vv { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
            Timer { interval: 33; repeat: true; running: gp.open && !Look.lite && !g.na; onTriggered: rc.ph = (Date.now() % 2000) / 2000 * Math.PI * 2 }  // ~30 a second (V4.84)
            onVvChanged: requestPaint(); onPhChanged: requestPaint()
            renderStrategy: Canvas.Cooperative
            onPaint: {
                const c = getContext("2d"); c.reset()
                const cx = width / 2, cy = height / 2 + 6, r = width / 2 - 14
                const len = Math.PI * 2 * Math.max(0.005, Math.min(1, vv / 100)), s = -Math.PI / 2
                c.lineCap = "round"; c.lineWidth = 7
                c.strokeStyle = gp.track
                if (vv < 99) { c.beginPath(); c.arc(cx, cy, r, s + len + 0.32, s + Math.PI * 2 - 0.32); c.stroke() }
                if (g.na) return
                c.strokeStyle = gp.primary; c.beginPath()
                const n = Math.max(10, Math.round(len * 18)), amp = (Look.wavy ? 2.6 : 0) * Math.sin(Math.PI * Math.min(1, vv / 100))
                for (let i = 0; i <= n; i++) { const a = s + len * i / n, rr = r + amp * Math.sin(a * 10 - ph); const x = cx + rr * Math.cos(a), y = cy + rr * Math.sin(a); if (i === 0) c.moveTo(x, y); else c.lineTo(x, y) }
                c.stroke()
            }
        }
        // the heat chip
        Rectangle {
            x: 6; y: 0
            height: 22; width: hr.implicitWidth + 16; radius: 11
            color: g.heat >= 85 ? Qt.rgba(1, 0.4, 0.4, 0.25) : gp.card
            Row { id: hr; anchors.centerIn: parent; spacing: 3
                MIcon { size: 12; name: "thermostat"; glyph: ""; color: gp.dim; anchors.verticalCenter: parent.verticalCenter }
                Text { text: g.heat > 0 ? g.heat + "°C" : "–°C"; color: gp.fg; font.family: Fonts.main; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter } }
        }
        Column {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 6
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: g.name; color: gp.dim; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.Medium }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: g.na ? Tr.t("N/A") : Math.round(g.v) + "%"; color: gp.fg; font.family: Fonts.numbers; font.pixelSize: 28; font.weight: Font.DemiBold }
        }
    }
    // a small round button
    component Pip: Rectangle {
        id: pb
        property string icon: ""
        property string glyph: ""
        property bool on: false
        property color onColor: gp.primary
        signal clicked()
        width: 46; height: 46
        radius: pm.pressed ? 14 : (on ? 16 : 23)
        color: on ? onColor : (pm.containsMouse ? gp.cardHi : gp.card)
        scale: pm.pressed ? 0.92 : 1
        Behavior on radius { NumberAnimation { duration: 260; easing.type: Easing.OutBack; easing.overshoot: 2 } }
        Behavior on color { ColorAnimation { duration: 160 } }
        Behavior on scale { NumberAnimation { duration: 120 } }
        MIcon { anchors.centerIn: parent; size: 19; name: pb.icon; glyph: pb.glyph; color: pb.on ? gp.inkPrimary : gp.fg }
        MouseArea { id: pm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: pb.clicked() }
    }
    // a stat card: icon, value, small chip
    component Stat: Rectangle {
        property string icon: ""
        property string glyph: ""
        property string label: ""
        property string value: ""
        property string chip: ""
        width: (body.width - 10) / 2; height: 62; radius: 20; color: gp.card
        Rectangle { x: 12; anchors.verticalCenter: parent.verticalCenter; width: 32; height: 32; radius: 11; color: gp.container
            MIcon { anchors.centerIn: parent; size: 16; name: parent.parent.icon; glyph: parent.parent.glyph; color: gp.inkContainer } }
        Column { x: 52; anchors.verticalCenter: parent.verticalCenter
            Text { text: parent.parent.label; color: gp.dim; font.family: Fonts.main; font.pixelSize: 10 }
            Text { text: parent.parent.value; color: gp.fg; font.family: Fonts.numbers; font.pixelSize: 16; font.weight: Font.DemiBold } }
        Rectangle { visible: parent.chip !== ""; anchors.right: parent.right; anchors.rightMargin: 10; y: 10
            width: ct.implicitWidth + 12; height: 20; radius: 10; color: gp.cardHi
            Text { id: ct; anchors.centerIn: parent; text: parent.parent.chip; color: gp.dim; font.family: Fonts.main; font.pixelSize: 10 } }
    }
    // a thick slider with the icon inside, like the volume keys of a phone
    component Line: Item {
        id: ln
        property string icon: ""
        property string glyph: ""
        property real v: 0
        signal moved(real v)
        width: body.width; height: 44
        Rectangle {
            anchors.fill: parent; radius: 22; color: gp.card
            clip: true
            Rectangle { width: Math.max(44, parent.width * ln.v); height: parent.height; radius: 22; color: gp.primary
                Behavior on width { enabled: !lm.pressed; NumberAnimation { duration: 160 } } }
            MIcon { x: 14; anchors.verticalCenter: parent.verticalCenter; size: 18; name: ln.icon; glyph: ln.glyph; color: gp.inkPrimary }
            Text { anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter
                text: Math.round(ln.v * 100) + "%"; color: ln.v > 0.85 ? gp.inkPrimary : gp.fg; font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.Medium }
        }
        MouseArea {
            id: lm; anchors.fill: parent; cursorShape: Qt.PointingHandCursor
            function at(x) { return Math.max(0, Math.min(1, x / width)) }
            onPressed: function(m) { ln.moved(at(m.x)) }
            onPositionChanged: function(m) { if (pressed) ln.moved(at(m.x)) }
            onWheel: function(e) { ln.moved(Math.max(0, Math.min(1, ln.v + Look.notches(e) * 0.05))) }
        }
    }

    // ---------- the panel: a slim column on the left ----------
    Shadow { x: panel.x; y: panel.y; width: panel.width; height: panel.height; radius: panel.radius; opacity: panel.opacity; scale: panel.scale; transformOrigin: Item.Left
        on: Look.shadows && Look.shadowPanels }
    Rectangle {
        id: panel
        width: 372
        x: 12 - 72 * (1 - gp.et)
        scale: 0.94 + 0.06 * gp.et
        transformOrigin: Item.Left
        height: Math.min(parent.height - 24, body.implicitHeight + 32)
        anchors.verticalCenter: parent.verticalCenter
        radius: 30
        color: gp.surface
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.06)
        opacity: Math.min(1, gp.et * 1.5)
        clip: true
        MouseArea { anchors.fill: parent }       // clicks on the panel do not close it

        Flickable {
            anchors.fill: parent
            anchors.margins: 16
            contentHeight: body.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Column {
                id: body
                width: parent.width
                spacing: 12

                // ---- header: time, status, close
                Item {
                    opacity: gp.stg(0); transform: Translate { x: (1 - gp.stg(0)) * -26 }
                    width: parent.width; height: 40
                    Rectangle { width: 40; height: 40; radius: 14; color: gp.container
                        MIcon { anchors.centerIn: parent; size: 20; name: "sports_esports"; glyph: ""; color: gp.inkContainer; filled: true } }
                    Text { x: 50; anchors.verticalCenter: parent.verticalCenter; text: gp.now.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                        color: gp.fg; font.family: Fonts.numbers; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Row {
                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Row { spacing: 3; anchors.verticalCenter: parent.verticalCenter
                            MIcon { size: 16; name: gp.dnd ? "notifications_off" : "notifications"; glyph: ""; color: gp.dim }
                            Text { text: gp.notifCount; color: gp.dim; font.family: Fonts.main; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter } }
                        Rectangle {
                            width: 34; height: 34; radius: 17; color: xm.containsMouse ? gp.cardHi : gp.card
                            MIcon { anchors.centerIn: parent; size: 18; name: "close"; glyph: ""; color: gp.fg }
                            MouseArea { id: xm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: gp.open = false }
                        }
                    }
                }
                Text { width: parent.width; elide: Text.ElideRight; text: gp.game; color: gp.dim; font.family: Fonts.main; font.pixelSize: 12 }

                // ---- CPU and GPU
                Row {
                    opacity: gp.stg(1); transform: Translate { x: (1 - gp.stg(1)) * -26 }
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 18
                    Ring { name: "CPU"; v: gp.st.cpu; heat: gp.st.temp }
                    Ring { name: "GPU"; v: gp.st.gpu; heat: gp.st.gtemp; na: !gp.haveGpu }
                }

                // ---- FPS
                Rectangle {
                    opacity: gp.stg(2); transform: Translate { x: (1 - gp.stg(2)) * -26 }
                    width: parent.width; height: 74; radius: 22; color: gp.card
                    Rectangle { x: 14; anchors.verticalCenter: parent.verticalCenter; width: 40; height: 40; radius: 14; color: gp.container
                        MIcon { anchors.centerIn: parent; size: 20; name: "speed"; glyph: "󰓅"; color: gp.inkContainer } }
                    Column {
                        x: 66; anchors.verticalCenter: parent.verticalCenter
                        Text { text: "FPS"; color: gp.dim; font.family: Fonts.main; font.pixelSize: 11 }
                        Text { text: gp.fps >= 0 ? gp.fps : Tr.t("No data"); color: gp.fg; font.family: Fonts.numbers; font.pixelSize: gp.fps >= 0 ? 26 : 17; font.weight: Font.DemiBold }
                    }
                    // turn MangoHud's FPS log on (games started with mangohud)
                    Rectangle {
                        anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
                        width: ft.implicitWidth + 20; height: 30; radius: 15
                        color: gp.fpsOn ? gp.container : (fm.containsMouse ? gp.cardHi : "transparent")
                        border.width: gp.fpsOn ? 0 : 1; border.color: Qt.rgba(1, 1, 1, 0.14)
                        Text { id: ft; anchors.centerIn: parent; text: gp.fpsOn ? Tr.t("MangoHud on") : Tr.t("Use MangoHud"); color: gp.fpsOn ? gp.inkContainer : gp.dim; font.family: Fonts.main; font.pixelSize: 11 }
                        MouseArea { id: fm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: { gp.bin(["rice-fps", gp.fpsOn ? "off" : "on"]); gp.fpsOn = !gp.fpsOn } }
                    }
                }

                // ---- capture: record (with the time), screenshot, microphone, Do Not Disturb
                Row {
                    opacity: gp.stg(3); transform: Translate { x: (1 - gp.stg(3)) * -26 }
                    spacing: 8
                    Rectangle {
                        width: body.width - 3 * 46 - 3 * 8; height: 46
                        radius: rm.pressed ? 14 : 23
                        color: gp.recording ? Qt.rgba(1, 0.35, 0.35, 0.22) : (rm.containsMouse ? gp.cardHi : gp.card)
                        Behavior on color { ColorAnimation { duration: 160 } }
                        Behavior on radius { NumberAnimation { duration: 200 } }
                        Row {
                            anchors.centerIn: parent; spacing: 10
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14; height: 14; radius: gp.recording ? 3 : 7; color: "#ff5c5c"
                                Behavior on radius { NumberAnimation { duration: 200 } }
                            }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: gp.recording ? gp.recTime : Tr.t("Record")
                                color: gp.fg; font.family: Fonts.numbers; font.pixelSize: 14; font.weight: Font.Medium }
                        }
                        MouseArea { id: rm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (gp.recording) { gp.bin(["rice-hotcorner", "record", "stop"]); gp.recording = false }
                                else { gp.bin(["rice-hotcorner", "record", "full"]); gp.recStart = Date.now() / 1000 }
                            } }
                    }
                    Pip { icon: "photo_camera"; glyph: ""; onClicked: { gp.open = false; shotT.restart() } }
                    Pip { icon: gp.micMuted ? "mic_off" : "mic"; glyph: gp.micMuted ? "󰍭" : "󰍬"
                        on: gp.micMuted; onColor: "#FFB4AB"
                        onClicked: { gp.bin(["rice-volume", "mic"]); gp.micMuted = !gp.micMuted; gp.later() } }
                    Pip { icon: gp.dnd ? "notifications_off" : "notifications"; glyph: ""; on: gp.dnd
                        onClicked: { gp.bin(["rice-cc", "dnd", gp.dnd ? "off" : "on"]); gp.dnd = !gp.dnd; gp.later() } }
                }
                Timer { id: shotT; interval: 450; onTriggered: gp.bin(["rice-hotcorner", "shot", "region"]) }

                // ---- RAM and ping
                Row {
                    opacity: gp.stg(4); transform: Translate { x: (1 - gp.stg(4)) * -26 }
                    spacing: 10
                    Stat { icon: "memory"; glyph: "󰍛"; label: "RAM"; value: gp.st.ram + "%"; chip: gp.st.ramu + " GB" }
                    Stat { icon: "network_ping"; glyph: "󰄥"; label: Tr.t("Ping"); value: gp.ping >= 0 ? gp.ping + " ms" : "–"; chip: "1.1.1.1" }
                }

                // ---- performance profile
                Rectangle {
                    opacity: gp.stg(5); transform: Translate { x: (1 - gp.stg(5)) * -26 }
                    width: parent.width; height: perf.implicitHeight + 28; radius: 22; color: gp.card
                    Column {
                        id: perf
                        x: 14; y: 14; width: parent.width - 28; spacing: 10
                        Row { spacing: 8
                            MIcon { size: 16; name: "bolt"; glyph: "󰓅"; color: gp.primary; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: gp.havePP ? Tr.t("Performance profile") : Tr.t("Performance profile (power-profiles-daemon not running)"); color: gp.fg; font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.Medium } }
                        Row {
                            visible: gp.havePP
                            spacing: 4
                            Repeater {
                                model: [{ k: "power-saver", n: "Low", i: "eco", g: "󰌪" }, { k: "balanced", n: "Medium", i: "balance", g: "" }, { k: "performance", n: "High", i: "bolt", g: "󰓅" }]
                                delegate: Rectangle {
                                    readonly property bool on: gp.profile === modelData.k
                                    width: (perf.width - 8) / 3; height: 38
                                    radius: on ? 19 : 10
                                    color: on ? gp.primary : (pm2.containsMouse ? gp.cardHi : Qt.rgba(1, 1, 1, 0.04))
                                    Behavior on radius { NumberAnimation { duration: 240; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
                                    Behavior on color { ColorAnimation { duration: 160 } }
                                    Row {
                                        anchors.centerIn: parent; spacing: 5
                                        MIcon { size: 15; name: modelData.i; glyph: modelData.g; color: parent.parent.on ? gp.inkPrimary : gp.fg; anchors.verticalCenter: parent.verticalCenter }
                                        Text { text: modelData.n; color: parent.parent.on ? gp.inkPrimary : gp.fg; font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.Medium; anchors.verticalCenter: parent.verticalCenter }
                                    }
                                    MouseArea { id: pm2; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: { gp.profile = modelData.k; gp.bin(["rice-services", "profile", modelData.k]); gp.later() } }
                                }
                            }
                        }
                    }
                }

                // ---- gaming mode
                Rectangle {
                    opacity: gp.stg(6); transform: Translate { x: (1 - gp.stg(6)) * -26 }
                    readonly property bool on: gp.fx.gaming === true
                    width: parent.width; height: 56; radius: 22
                    color: on ? gp.container : (gm.containsMouse ? gp.cardHi : gp.card)
                    Behavior on color { ColorAnimation { duration: 160 } }
                    MIcon { x: 16; anchors.verticalCenter: parent.verticalCenter; size: 20; name: "sports_esports"; glyph: ""; color: parent.on ? gp.primary : gp.fg; filled: parent.on }
                    Column { x: 48; anchors.verticalCenter: parent.verticalCenter
                        Text { text: Tr.t("Gaming mode"); color: gp.fg; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.Medium }
                        Text { text: parent.parent.on ? Tr.t("On: effects are off") : Tr.t("Turns off blur, animations and shadows"); color: parent.parent.on ? gp.inkContainer : gp.dim; font.family: Fonts.main; font.pixelSize: 11 } }
                    MouseArea { id: gm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { const n = JSON.parse(JSON.stringify(gp.fx)); n.gaming = !parent.on; gp.fx = n; gp.bin(["rice-effects", "gaming", n.gaming ? "on" : "off"]); gp.later() } }
                }

                // ---- volume and brightness
                Line {
                    opacity: gp.stg(7); transform: Translate { x: (1 - gp.stg(7)) * -26 }
                    icon: gp.sink && gp.sink.audio && gp.sink.audio.muted ? "volume_off" : "volume_up"; glyph: "󰕾"
                    v: Vol.of(gp.sink)
                    onMoved: function(x) { Vol.set(gp.sink, x) }
                }
                Line {
                    opacity: gp.stg(8); transform: Translate { x: (1 - gp.stg(8)) * -26 }
                    visible: gp.bright >= 0
                    icon: "brightness_6"; glyph: "󰃟"
                    v: gp.bright / 100
                    onMoved: function(x) { const p = Math.max(1, Math.round(x * 100)); gp.bright = p; gp.sh("brightnessctl -q set " + p + "%") }
                }
                Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; topPadding: 4; text: Tr.t("SUPER + G or Esc to close"); color: Qt.rgba(1, 1, 1, 0.35); font.family: Fonts.main; font.pixelSize: 11 }
            }
        }
    }
}
