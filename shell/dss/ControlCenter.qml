// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Services.Pipewire
import QtQuick

// The control center (the bell on the bar, SUPER + N): slides in from the right.
// Control: brightness, volume and microphone, Wi-Fi and Bluetooth, quick switches, the music and your notifications.
// Overview: the time, a calendar, the weather and how busy the computer is.
// It reads anything only while it is open.
PanelWindow {
    screen: Mon.screen
    id: cc
    property var nf: null                      // Notifs.qml
    property bool open: false
    property string tab: "control"
    property real t: 0
    // t runs evenly in time; every part eases on its own (quick start, long soft landing, like Material 3's
    // emphasized-decelerate), so the panel opens in one smooth wave and closes with a quick, accelerating fade
    // Light mode (slower computers): a quicker opening (fewer frames to draw)
    Behavior on t { NumberAnimation { duration: cc.open ? (Look.lite ? 280 : 720) : (Look.lite ? 160 : 260); easing.type: Easing.Linear } }
    function ease(p) { return 1 - Math.pow(1 - p, 4) }
    readonly property real et: ease(t)
    // each part of the panel comes in a little after the one above it (i = its place, from the top)
    function stg(i) { return ease(Math.max(0, Math.min(1, (cc.t - i * 0.035) / 0.615))) }   // i up to 11
    // Control <-> Overview: Material 3 shared axis (the old one slides out and fades, the new one comes in)
    property real tabT: 0
    onTabChanged: { fl.contentY = 0; tabAnim.to = tab === "overview" ? 1 : 0; tabAnim.restart() }
    NumberAnimation { id: tabAnim; target: cc; property: "tabT"; duration: 520; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph }
    onOpenChanged: { t = open ? 1 : 0; if (open) refresh() }

    // ---------- colors: Settings > Notifications > Theme and Transparency ----------
    NPal { id: pal; theme: cc.nf ? cc.nf.theme : "shell"; alpha: cc.nf ? Math.min(1, cc.nf.ccAlpha + 0.08) : 0.9 }
    readonly property color primary: pal.primary
    readonly property color inkPrimary: pal.inkPrimary
    readonly property color container: pal.container
    readonly property color inkContainer: pal.inkContainer
    readonly property color surface: pal.surface
    readonly property color card: pal.card
    readonly property color cardHi: pal.cardHi
    readonly property color fg: pal.fg
    readonly property color dim: pal.dim

    // ---------- data ----------
    property var sv: ({})
    property string ssid: ""
    property string btDev: ""
    property int bright: -1
    property bool recording: false
    property real recStart: 0
    property var now: new Date()
    // one rice-stats for the whole shell (Stats.qml)
    readonly property var st: Stats.st
    StatsHold { on: cc.open && cc.tab === "overview" }
    readonly property var sink: Pipewire.defaultAudioSink
    readonly property var source: Pipewire.defaultAudioSource
    PwObjectTracker { objects: [cc.sink, cc.source] }
    function two(n) { return n < 10 ? "0" + n : "" + n }
    readonly property string recTime: {
        const s = Math.max(0, Math.floor(now.getTime() / 1000 - recStart))
        return two(Math.floor(s / 3600)) + ":" + two(Math.floor(s / 60) % 60) + ":" + two(s % 60)
    }
    DssProc {
        id: svProc
        command: ["bash", "-c", "\"$HOME/.local/bin/rice-services\" state 2>/dev/null"]
        stdout: DssCollector { onStreamFinished: { try { cc.sv = JSON.parse(text) } catch (e) { } } }
    }
    DssProc {
        id: netProc
        command: ["bash", "-c", [
            "echo \"ssid=$(nmcli -t -f active,ssid dev wifi 2>/dev/null | sed -n 's/^yes://p' | head -1)\"",
            "echo \"bt=$(bluetoothctl devices Connected 2>/dev/null | head -1 | cut -d' ' -f3-)\"",
            "command -v brightnessctl >/dev/null && echo \"br=$(brightnessctl -m 2>/dev/null | cut -d, -f4 | tr -d %)\""
        ].join("; ")]
        stdout: DssCollector {
            onStreamFinished: {
                text.split("\n").forEach(function(l) {
                    const i = l.indexOf("="); if (i < 0) return
                    const k = l.substring(0, i), v = l.substring(i + 1).trim()
                    if (k === "ssid") cc.ssid = v
                    else if (k === "bt") cc.btDev = v
                    else if (k === "br") { const n = parseInt(v); cc.bright = isNaN(n) ? -1 : n }
                })
            }
        }
    }
    Trigger {
        path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.cache/rice/record.json"
        every: 2000
        onContent: function(t) {
            let j = null
            try { j = t.trim() !== "" ? JSON.parse(t) : null } catch (e) { j = null }
            cc.recording = j !== null
            if (j) cc.recStart = j.start || Date.now() / 1000
        }
    }
    function refresh() { if (!svProc.running) svProc.running = true; if (!netProc.running) netProc.running = true; now = new Date() }
    Timer { interval: 4000; running: cc.open; repeat: true; onTriggered: cc.refresh() }
    Timer { interval: 1000; running: cc.open; repeat: true; onTriggered: cc.now = new Date() }
    Timer { id: again; interval: 900; onTriggered: cc.refresh() }
    function bin(a) { DssWin.run(["bash", "-c", "\"$HOME/.local/bin/" + a[0] + "\" \"${@}\"", "_"].concat(a.slice(1))) }
    function sh(c) { DssWin.run(["bash", "-c", c]) }
    function flip(key) {
        const n = Object.assign({}, sv); n[key] = !sv[key]; sv = n
        if (key === "dnd" && nf && nf.shellMode) nf.setDnd(n.dnd)
        else bin(["rice-services", "set", key, n[key] ? "on" : "off"])
        again.restart()
    }

    Trigger {
        name: "cc-open"; fresh: true
        onFired: function(o) {
            const w = o.split(" ")[1] || "toggle"
            cc.open = w === "open" ? true : (w === "close" ? false : !cc.open)
        }
    }

    // ---------- the window ----------
    visible: open || (t > 0.01 && !closeT.done)
    // if the closing animation ever stalls (the compositor sent no frames for a moment), it still goes away
    Timer { id: closeT; property bool done: false; interval: 600; running: !open; onRunningChanged: if (running) done = false; onTriggered: done = true }
    // niri: blur behind it (Hyprland blurs this layer with its own rule)
    NiriBlur { win: cc; items: [panel]; radius: panel.radius; on: cc.open && cc.et > 0.94 }
    color: "transparent"
    anchors { top: true; left: true; right: true; bottom: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-cc"
    WlrLayershell.keyboardFocus: open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    MouseArea { anchors.fill: parent; onClicked: cc.open = false }
    Item { focus: cc.open; Keys.onEscapePressed: cc.open = false }

    // ---------- pieces ----------
    // a long slider with its icon inside (brightness, volume, microphone)
    component Line: Item {
        id: ln
        property string icon: ""
        property string glyph: ""
        property real v: 0
        property bool off: false
        signal moved(real v)
        signal iconClicked()
        height: 46
        Rectangle {
            anchors.fill: parent; radius: 23; color: cc.card
            clip: true
            Rectangle { width: Math.max(46, parent.width * ln.v); height: parent.height; radius: 23; color: ln.off ? cc.cardHi : cc.primary
                Behavior on width { enabled: !lm.pressed; NumberAnimation { duration: 160 } } }
        }
        MouseArea {
            id: lm; anchors.fill: parent; anchors.leftMargin: 46; cursorShape: Qt.PointingHandCursor
            function at(x) { return Math.max(0, Math.min(1, (x + 46) / ln.width)) }
            onPressed: function(m) { ln.moved(at(m.x)) }
            onPositionChanged: function(m) { if (pressed) ln.moved(at(m.x)) }
            onWheel: function(e) { ln.moved(Math.max(0, Math.min(1, ln.v + Look.notches(e) * 0.05))) }
        }
        Item {
            width: 46; height: 46
            MIcon { anchors.centerIn: parent; size: 19; name: ln.icon; glyph: ln.glyph; color: ln.off ? cc.dim : cc.inkPrimary }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ln.iconClicked() }
        }
    }
    // a quick switch: icon over a short name, the shape closes in when it is on
    component Tile: Rectangle {
        id: tl
        property string icon: ""
        property string glyph: ""
        property string label: ""
        property bool on: false
        signal clicked()
        height: 70
        radius: tm.pressed ? 14 : (on ? 22 : 35)
        color: on ? cc.primary : (tm.containsMouse ? cc.cardHi : cc.card)
        scale: tm.pressed ? 0.95 : 1
        Behavior on radius { NumberAnimation { duration: 280; easing.type: Easing.OutBack; easing.overshoot: 1.8 } }
        Behavior on color { ColorAnimation { duration: 160 } }
        Behavior on scale { NumberAnimation { duration: 120 } }
        Column {
            anchors.centerIn: parent
            spacing: 4
            MIcon { anchors.horizontalCenter: parent.horizontalCenter; size: 20; name: tl.icon; glyph: tl.glyph; filled: tl.on; color: tl.on ? cc.inkPrimary : cc.fg }
            Text { anchors.horizontalCenter: parent.horizontalCenter; width: tl.width - 10; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                text: tl.label; color: tl.on ? cc.inkPrimary : cc.fg; font.family: Fonts.main; font.pixelSize: 11; font.weight: Font.Medium }
        }
        MouseArea { id: tm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: tl.clicked() }
    }
    // a wide switch with a name and a line under it (Wi-Fi, Bluetooth); the arrow opens its menu
    component Wide: Rectangle {
        id: wd
        property string icon: ""
        property string glyph: ""
        property string label: ""
        property string sub: ""
        property bool on: false
        signal clicked()
        signal more()
        height: 64
        radius: wm.pressed ? 16 : 24
        color: on ? cc.primary : (wm.containsMouse ? cc.cardHi : cc.card)
        Behavior on color { ColorAnimation { duration: 160 } }
        Behavior on radius { NumberAnimation { duration: 200 } }
        MIcon { x: 18; anchors.verticalCenter: parent.verticalCenter; size: 21; name: wd.icon; glyph: wd.glyph; filled: wd.on; color: wd.on ? cc.inkPrimary : cc.fg }
        Column {
            x: 52; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 52 - 40
            Text { width: parent.width; elide: Text.ElideRight; text: wd.label; color: wd.on ? cc.inkPrimary : cc.fg; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.DemiBold }
            Text { width: parent.width; elide: Text.ElideRight; text: wd.sub; color: wd.on ? Qt.darker(cc.inkPrimary, 0.7) : cc.dim; font.family: Fonts.main; font.pixelSize: 11; visible: text !== "" }
        }
        MouseArea { id: wm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: wd.clicked() }
        Item {
            anchors.right: parent.right; width: 40; height: parent.height
            MIcon { anchors.centerIn: parent; size: 18; name: "chevron_right"; glyph: ""; color: wd.on ? cc.inkPrimary : cc.dim }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: wd.more() }
        }
    }

    // ---------- the panel ----------
    Shadow { x: panel.x; y: panel.y; width: panel.width; height: panel.height; radius: panel.radius; opacity: panel.opacity; scale: panel.scale; transformOrigin: Item.TopRight
        on: Look.shadows && Look.shadowPanels }
    Rectangle {
        id: panel
        width: 440
        x: parent.width - width - 12 + 64 * (1 - cc.et)
        scale: 0.94 + 0.06 * cc.et
        transformOrigin: Item.TopRight
        anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.topMargin: 58; anchors.bottomMargin: 12
        radius: 32
        color: cc.surface
        border.width: 1; border.color: pal.line
        opacity: Math.min(1, cc.et * 1.5)
        clip: true
        MouseArea { anchors.fill: parent }

        // header: close, the two tabs, Settings
        Item {
            id: head
            x: 16; y: 14; width: parent.width - 32; height: 44
            opacity: cc.stg(0); transform: Translate { y: (1 - cc.stg(0)) * -10 }
            Rectangle {
                width: 40; height: 40; radius: 20; anchors.verticalCenter: parent.verticalCenter
                color: xm.containsMouse ? cc.cardHi : "transparent"
                MIcon { anchors.centerIn: parent; size: 20; name: "close"; glyph: ""; color: cc.fg }
                MouseArea { id: xm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: cc.open = false }
            }
            Item {
                id: tabs
                property real w0: 0
                property real w1: 0
                anchors.centerIn: parent
                width: tabRow.width; height: 40
                // the selected pill slides and stretches to the tab you pick
                Rectangle {
                    x: cc.tab === "control" ? 0 : tabs.w0 + 4
                    width: cc.tab === "control" ? tabs.w0 : tabs.w1
                    height: 40; radius: 20; color: cc.primary
                    Behavior on x { NumberAnimation { duration: 480; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }
                    Behavior on width { NumberAnimation { duration: 480; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }
                }
                Row {
                    id: tabRow
                    spacing: 4
                    Repeater {
                        model: [{ k: "control", n: "Control", i: "tune", g: "\uf1de" }, { k: "overview", n: "Overview", i: "dashboard", g: "\uf009" }]
                        delegate: Item {
                            readonly property bool on: cc.tab === modelData.k
                            width: tr.implicitWidth + 30; height: 40
                            onWidthChanged: if (index === 0) tabs.w0 = width; else tabs.w1 = width
                            Component.onCompleted: if (index === 0) tabs.w0 = width; else tabs.w1 = width
                            Rectangle { anchors.fill: parent; radius: 20; color: Qt.rgba(1, 1, 1, tbm.containsMouse && !parent.on ? 0.06 : 0) }
                            Row { id: tr; anchors.centerIn: parent; spacing: 6
                                MIcon { size: 17; name: modelData.i; glyph: modelData.g; filled: parent.parent.on; color: parent.parent.on ? cc.inkPrimary : cc.fg; anchors.verticalCenter: parent.verticalCenter
                                    Behavior on color { ColorAnimation { duration: 300 } } }
                                Text { text: modelData.n; color: parent.parent.on ? cc.inkPrimary : cc.fg; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.Medium; anchors.verticalCenter: parent.verticalCenter
                                    Behavior on color { ColorAnimation { duration: 300 } } } }
                            MouseArea { id: tbm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: cc.tab = modelData.k }
                        }
                    }
                }
            }
            Rectangle {
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                width: 40; height: 40; radius: 20; color: gm.containsMouse ? cc.cardHi : cc.card
                MIcon { anchors.centerIn: parent; size: 18; name: "settings"; glyph: ""; color: cc.fg }
                MouseArea { id: gm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { cc.open = false; cc.bin(["rice-settings", "10"]) } }
            }
        }

        Flickable {
            id: fl
            x: 16; y: head.y + head.height + 12
            width: parent.width - 32; height: parent.height - y - 16
            contentHeight: cc.tabT < 0.5 ? ctl.implicitHeight : ov.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            // ================= Control =================
            Column {
                id: ctl
                visible: cc.tabT < 0.999
                opacity: Math.max(0, 1 - cc.tabT * 2.4)
                transform: Translate { x: -cc.tabT * 56 }
                width: fl.width
                spacing: 10

                Line {
                    visible: cc.bright >= 0
                    width: parent.width
                    opacity: cc.stg(1); transform: Translate { y: (1 - cc.stg(1)) * 22 }
                    icon: "brightness_6"; glyph: "󰃟"
                    v: cc.bright / 100
                    onMoved: function(x) { const p = Math.max(1, Math.round(x * 100)); cc.bright = p; cc.sh("brightnessctl -q set " + p + "%") }
                }
                Row {
                    width: parent.width
                    spacing: 8
                    opacity: cc.stg(2); transform: Translate { y: (1 - cc.stg(2)) * 22 }
                    Line {
                        width: (parent.width - 8) * 0.62
                        readonly property bool mt: cc.sink && cc.sink.audio ? cc.sink.audio.muted : false
                        icon: mt ? "volume_off" : "volume_up"; glyph: "󰕾"; off: mt
                        v: Vol.of(cc.sink)
                        onMoved: function(x) { Vol.set(cc.sink, x) }
                        onIconClicked: if (cc.sink && cc.sink.audio) cc.sink.audio.muted = !cc.sink.audio.muted
                    }
                    Line {
                        width: (parent.width - 8) * 0.38
                        readonly property bool mt: cc.source && cc.source.audio ? cc.source.audio.muted : false
                        icon: mt ? "mic_off" : "mic"; glyph: "󰍬"; off: mt
                        v: cc.source && cc.source.audio ? cc.source.audio.volume : 0
                        onMoved: function(x) { if (cc.source && cc.source.audio) { cc.source.audio.muted = false; cc.source.audio.volume = x } }
                        onIconClicked: cc.bin(["rice-volume", "mic"])
                    }
                }
                Row {
                    width: parent.width
                    spacing: 8
                    opacity: cc.stg(3); transform: Translate { y: (1 - cc.stg(3)) * 22 }
                    Wide { width: (parent.width - 8) / 2; icon: "wifi"; glyph: ""; on: cc.sv.wifi === true
                        label: cc.sv.wifi === true ? (cc.ssid !== "" ? cc.ssid : Tr.t("Wi-Fi")) : Tr.t("Wi-Fi"); sub: cc.sv.wifi === true ? (cc.ssid !== "" ? Tr.t("Connected") : Tr.t("On")) : Tr.t("Off")
                        onClicked: cc.flip("wifi"); onMore: { cc.open = false; cc.bin(["rice-wifi", String(Math.round(cc.width - 300))]) } }
                    Wide { width: (parent.width - 8) / 2; icon: "bluetooth"; glyph: ""; on: cc.sv.bluetooth === true
                        label: cc.btDev !== "" ? cc.btDev : Tr.t("Bluetooth"); sub: cc.sv.bluetooth === true ? (cc.btDev !== "" ? Tr.t("Connected") : Tr.t("On")) : Tr.t("Off")
                        onClicked: cc.flip("bluetooth"); onMore: { cc.open = false; cc.bin(["rice-bluetooth", String(Math.round(cc.width - 300))]) } }
                }
                Grid {
                    width: parent.width
                    columns: 4
                    spacing: 8
                    opacity: cc.stg(4); transform: Translate { y: (1 - cc.stg(4)) * 22 }
                    readonly property real tw: (width - 3 * 8) / 4
                    Tile { width: parent.tw; icon: "do_not_disturb_on"; glyph: ""; label: Tr.t("Do Not Disturb"); on: cc.nf && cc.nf.shellMode ? cc.nf.dnd : cc.sv.dnd === true; onClicked: cc.flip("dnd") }
                    Tile { width: parent.tw; icon: "nightlight"; glyph: ""; label: Tr.t("Night light"); on: cc.sv.nightlight === true; onClicked: cc.flip("nightlight") }
                    Tile { width: parent.tw; icon: "coffee"; glyph: ""; label: Tr.t("Keep awake"); on: cc.sv.awake === true; onClicked: cc.flip("awake") }
                    Tile { width: parent.tw; icon: "airplanemode_active"; glyph: ""; label: Tr.t("Airplane"); on: cc.sv.airplane === true; onClicked: cc.flip("airplane") }
                    Tile { width: parent.tw; icon: "screenshot_region"; glyph: ""; label: Tr.t("Screenshot"); onClicked: { cc.open = false; shotT.restart() } }
                    Tile { width: parent.tw; icon: cc.recording ? "stop_circle" : "radio_button_checked"; glyph: ""; label: cc.recording ? cc.recTime : Tr.t("Record"); on: cc.recording
                        onClicked: { cc.bin(["rice-hotcorner", "record", cc.recording ? "stop" : "full"]); if (cc.recording) cc.recording = false; else cc.open = false } }
                    Tile { width: parent.tw
                        readonly property string p: cc.sv.profile || ""
                        visible: cc.sv.have && cc.sv.have.powerprofilesctl === true
                        icon: p === "performance" ? "bolt" : (p === "power-saver" ? "eco" : "balance"); glyph: ""
                        label: p === "performance" ? Tr.t("High") : (p === "power-saver" ? Tr.t("Low") : Tr.t("Medium")); on: p === "performance"
                        onClicked: {
                            const next = p === "power-saver" ? "balanced" : (p === "balanced" ? "performance" : "power-saver")
                            const n = Object.assign({}, cc.sv); n.profile = next; cc.sv = n
                            cc.bin(["rice-services", "profile", next]); again.restart()
                        } }
                    Tile { width: parent.tw; icon: "terminal"; glyph: ""; label: Tr.t("Terminal"); onClicked: { cc.open = false; cc.sh("kitty >/dev/null 2>&1 &") } }
                }
                Timer { id: shotT; interval: 450; onTriggered: cc.bin(["rice-hotcorner", "shot", "region"]) }

                // ---- the music: the cover as a cookie in a wavy ring, a wavy seek line, five buttons
                Rectangle {
                    width: parent.width; height: 176; radius: 28; color: cc.card
                    visible: Player.has
                    opacity: cc.stg(5); transform: Translate { y: (1 - cc.stg(5)) * 22 }
                    // the one Player ticks while this card is out
                    Instantiator { model: cc.open && cc.tab === "control" ? 1 : 0
                                   delegate: QtObject { Component.onCompleted: Player.hold(); Component.onDestruction: Player.release() } }
                    Item {
                        id: art
                        x: 12; anchors.verticalCenter: parent.verticalCenter
                        width: 152; height: 152
                        WavyArc {
                            anchors.fill: parent
                            value: Player.frac
                            moving: Player.playing && cc.open
                            color: cc.primary
                            track: pal.track
                        }
                        ShapeImage { anchors.centerIn: parent; width: 118; height: 118; kind: "cookie"; source: Player.art; empty: cc.cardHi
                            SlowSpin { turn: 360; period: 24000; running: cc.open && Player.playing && !Look.lite } }
                    }
                    Column {
                        x: art.x + art.width + 12; anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - x - 14
                        spacing: 4
                        Text { width: parent.width; elide: Text.ElideRight; text: Player.title; color: cc.fg; font.family: Fonts.main; font.pixelSize: 15; font.weight: Font.DemiBold }
                        Text { width: parent.width; elide: Text.ElideRight; text: Player.artist; color: cc.dim; font.family: Fonts.main; font.pixelSize: 12; visible: text !== "" }
                        WavyLine {
                            width: parent.width; height: 22
                            value: Player.frac
                            moving: Player.playing && cc.open
                            seekable: true
                            color: cc.primary
                            track: pal.track
                            onMoved: function(v) { Player.seekTo(v) }
                        }
                        Text { text: Player.clock(Player.pos) + "  /  " + Player.clock(Player.length); color: cc.dim; font.family: Fonts.numbers; font.pixelSize: 11; visible: Player.length > 0 }
                        Row {
                            spacing: 4
                            topPadding: 2
                            Repeater {
                                model: [{ i: "skip_previous", g: "\uf048", a: "prev" }, { i: "fast_rewind", g: "\uf04a", a: "back" },
                                        { i: "play", g: "", a: "toggle" },
                                        { i: "fast_forward", g: "\uf04e", a: "fwd" }, { i: "skip_next", g: "\uf051", a: "next" }]
                                delegate: Rectangle {
                                    readonly property bool big: modelData.a === "toggle"
                                    width: big ? 54 : 34; height: 38; radius: pm.pressed ? 12 : 19
                                    color: big ? cc.primary : (pm.containsMouse ? cc.cardHi : Qt.rgba(1, 1, 1, 0.06))
                                    Behavior on radius { NumberAnimation { duration: 260; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
                                    MIcon { anchors.centerIn: parent; size: parent.big ? 21 : 16; filled: true
                                        name: parent.big ? (Player.playing ? "pause" : "play_arrow") : modelData.i
                                        glyph: parent.big ? (Player.playing ? "\uf04c" : "\uf04b") : modelData.g
                                        color: parent.big ? cc.inkPrimary : cc.fg }
                                    MouseArea { id: pm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (modelData.a === "toggle") Player.toggle()
                                            else if (modelData.a === "next") Player.next()
                                            else if (modelData.a === "prev") Player.prev()
                                            else Player.seekBy(modelData.a === "back" ? -10 : 10)
                                        } }
                                }
                            }
                        }
                    }
                }

                // ---- notifications
                Item {
                    width: parent.width; height: 40
                    opacity: cc.stg(6); transform: Translate { y: (1 - cc.stg(6)) * 22 }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter; spacing: 8
                        MIcon { size: 17; name: "notifications"; glyph: ""; color: cc.fg; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: Tr.t("Notifications"); color: cc.fg; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: cc.nf && cc.nf.shellMode; text: cc.nf ? cc.nf.count + (cc.nf.count === 1 ? Tr.t(" notification") : Tr.t(" notifications")) : ""; color: cc.dim; font.family: Fonts.main; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Rectangle {
                        visible: cc.nf && cc.nf.shellMode && cc.nf.count > 0
                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        width: ca.implicitWidth + 26; height: 32; radius: 16; color: cam.containsMouse ? cc.cardHi : cc.card
                        Row { id: ca; anchors.centerIn: parent; spacing: 5
                            MIcon { size: 15; name: "clear_all"; glyph: ""; color: cc.fg; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: Tr.t("Clear all"); color: cc.fg; font.family: Fonts.main; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter } }
                        MouseArea { id: cam; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: cc.nf.clearAll() }
                    }
                }
                Repeater {
                    model: cc.nf && cc.nf.shellMode ? cc.nf.list.slice().reverse() : []
                    delegate: NotifCard { width: ctl.width; n: modelData; nf: cc.nf; onClosed: cc.nf.dismiss(modelData)
                        opacity: cc.stg(7 + Math.min(index, 4)); transform: Translate { y: (1 - cc.stg(7 + Math.min(index, 4))) * 26 } }
                }
                // nothing new
                Column {
                    visible: cc.nf && cc.nf.shellMode && cc.nf.count === 0
                    width: parent.width
                    topPadding: 18; bottomPadding: 18
                    spacing: 8
                    Blob { anchors.horizontalCenter: parent.horizontalCenter; width: 64; height: 64; kind: Look.shape; color: cc.card
                        MIcon { anchors.centerIn: parent; size: 24; name: "notifications_paused"; glyph: ""; color: cc.dim } }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: Tr.t("All caught up"); color: cc.dim; font.family: Fonts.main; font.pixelSize: 13 }
                }
                // swaync shows them
                Rectangle {
                    visible: !(cc.nf && cc.nf.shellMode)
                    width: parent.width; height: 120; radius: 22; color: cc.card
                    Column {
                        x: 16; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 32; spacing: 8
                        Text { width: parent.width; wrapMode: Text.WordWrap; text: Tr.t("swaync shows your notifications now. Let the shell show them here, with its own pop-ups?"); color: cc.dim; font.family: Fonts.main; font.pixelSize: 12 }
                        Row { spacing: 8
                            Rectangle { width: u1.implicitWidth + 28; height: 36; radius: 18; color: cc.primary
                                Text { id: u1; anchors.centerIn: parent; text: Tr.t("Use the shell"); color: cc.inkPrimary; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.Medium }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: cc.bin(["rice-cc", "engine", "shell"]) } }
                            Rectangle { width: u2.implicitWidth + 28; height: 36; radius: 18; color: cc.cardHi
                                Text { id: u2; anchors.centerIn: parent; text: Tr.t("Open swaync"); color: cc.fg; font.family: Fonts.main; font.pixelSize: 13 }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { cc.open = false; cc.sh("swaync-client -t -sw") } } } }
                    }
                }
            }

            // ================= Overview =================
            Column {
                id: ov
                visible: cc.tabT > 0.001
                opacity: Math.max(0, (cc.tabT - 0.3) / 0.7)
                transform: Translate { x: (1 - cc.tabT) * 56 }
                width: fl.width
                spacing: 12
                // the time
                Rectangle {
                    width: parent.width; height: 140; radius: 28; color: cc.container
                    opacity: cc.stg(1); transform: Translate { y: (1 - cc.stg(1)) * 22 }
                    clip: true
                    Blob { width: 150; height: 150; x: parent.width - 120; y: -30; kind: Look.shape; color: Qt.rgba(1, 1, 1, 0.06)
                        SlowSpin { turn: 360; period: 40000; running: cc.open && cc.tab === "overview" } }
                    Column {
                        x: 22; anchors.verticalCenter: parent.verticalCenter
                        Text { text: cc.now.toLocaleTimeString(Qt.locale(), Locale.ShortFormat); color: cc.inkContainer; font.family: Fonts.numbers; font.pixelSize: 48; font.weight: Font.Medium }
                        Text { text: cc.now.toLocaleDateString(Tr.loc, "dddd, d MMMM"); color: cc.inkContainer; opacity: 0.85; font.family: Fonts.main; font.pixelSize: 14 }
                    }
                }
                // a calendar of this month
                Rectangle {
                    width: parent.width; height: cal.implicitHeight + 28; radius: 26; color: cc.card
                    Column {
                        id: cal
                        x: 14; y: 14; width: parent.width - 28; spacing: 6
                        readonly property var first: new Date(cc.now.getFullYear(), cc.now.getMonth(), 1)
                        readonly property int lead: (first.getDay() + 6) % 7
                        readonly property int days: new Date(cc.now.getFullYear(), cc.now.getMonth() + 1, 0).getDate()
                        Text { text: cc.now.toLocaleDateString(Tr.loc, "MMMM yyyy"); color: cc.fg; font.family: Fonts.title; font.pixelSize: 15; font.weight: Font.DemiBold }
                        Grid {
                            columns: 7
                            width: parent.width
                            readonly property real cw: width / 7
                            Repeater {
                                model: Tr.week(true)
                                delegate: Text { width: parent.cw; horizontalAlignment: Text.AlignHCenter; text: modelData; color: cc.dim; font.family: Fonts.main; font.pixelSize: 11 }
                            }
                            Repeater {
                                model: cal.lead + cal.days
                                delegate: Item {
                                    width: parent.cw; height: 34
                                    readonly property int d: index - cal.lead + 1
                                    readonly property bool today: d === cc.now.getDate()
                                    Blob { visible: parent.today; anchors.centerIn: parent; width: 32; height: 32; kind: Look.shape; color: cc.primary }
                                    Text { anchors.centerIn: parent; visible: parent.d > 0; text: parent.d; color: parent.today ? cc.inkPrimary : cc.fg
                                        font.family: Fonts.numbers; font.pixelSize: 13; font.weight: parent.today ? Font.Bold : Font.Normal }
                                }
                            }
                        }
                    }
                }
                // how busy the computer is
                Row {
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: [{ k: "cpu", n: "CPU", s: "pentagon" }, { k: "ram", n: "RAM", s: "cookie" }, { k: "gpu", n: "GPU", s: "circle" }]
                        delegate: Rectangle {
                            width: (ov.width - 16) / 3; height: 120; radius: 24; color: cc.card
                            Item {
                                anchors.horizontalCenter: parent.horizontalCenter; y: 12
                                width: 72; height: 72
                                Blob { anchors.fill: parent; kind: modelData.s; color: cc.container }
                                Text { anchors.centerIn: parent; text: (cc.st[modelData.k] || 0) + "%"; color: cc.inkContainer; font.family: Fonts.numbers; font.pixelSize: 18; font.weight: Font.DemiBold }
                            }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 10; text: modelData.n; color: cc.dim; font.family: Fonts.main; font.pixelSize: 12 }
                        }
                    }
                }
            }
        }
    }
}
