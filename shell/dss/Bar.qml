// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Services.Mpris
import Quickshell.Services.Pipewire
import Quickshell.Services.UPower
import Quickshell.Services.SystemTray
import Quickshell.Widgets
import QtQuick

// The DarkShadow bar: the bar of this shell, made in Quickshell (Settings > DarkShadow bar and Bar layout).
// Styles: flat, island (one pill), islands (a pill for each side), pills (a pill for each module), notch (the middle
// grows out of the screen edge, the sides are islands), edge (like dynamic, but every part is one piece with the
// screen edge, HugShape.qml), dynamic (Material 3: tonal pills in your theme color, the
// launcher and the active workspace in your key color, soft state layers on hover, and in the middle an island that
// grows into the player, the volume, the workspace, notifications and recording: DynIsland.qml). The old "google"
// style is dynamic now. Auto-hide: it slides away and a thin strip at the edge brings it back.
// What sits where comes from ~/.config/rice/bar.json (layout: left, center, right).
PanelWindow {
    id: bar
    // the main bar lives on the main screen; Bar on every screen (Mon.everywhere) makes a copy for each other screen.
    // A copy shows the same, but commands from scripts (open the island, the calendar ...) go to the main bar only
    property var scr: null
    readonly property bool extra: scr !== null
    screen: scr ? scr : Mon.screen
    // one rice-services and one rice-weather for every bar (BarSvc.qml): a copy for another screen only reads them
    onOnChanged: if (!bar.extra) BarSvc.shown = bar.on || bar.ghostOnly
    onGhostOnlyChanged: if (!bar.extra) BarSvc.shown = bar.on || bar.ghostOnly
    Component.onCompleted: if (!bar.extra) BarSvc.shown = bar.on || bar.ghostOnly

    // ---------- settings (~/.config/rice/bar.json) ----------
    property var bc: ({})
    // no bar.json yet (a fresh install or an update) = on. On the left or right edge VBar.qml is the bar instead
    readonly property bool vertical: bc.position === "left" || bc.position === "right"
    readonly property bool on: bc.enabled !== false && !vertical
    // the bar on the left or right edge (VBar.qml): this window stays for the island's cards only (the clock on the
    // side bar opens the island's Today card here, at the top), without the bar itself
    readonly property bool ghostOnly: bc.enabled !== false && vertical
    // the bar itself: 1 shown .. 0 gone (it fades when the bar moves to a side edge, and back)
    property real barK: on ? 1 : 0
    Behavior on barK { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
    // the style asked for; the bar fades and settles into it (sty follows want once the old look has gone)
    readonly property string want: bc.style === "google" || bc.style === undefined ? "dynamic"
        : (["flat", "islands", "pills", "notch", "island", "edge"].indexOf(bc.style) >= 0 ? bc.style : "dynamic")
    property string sty: "island"
    property real swap: 0                       // 0 shown .. 1 gone (while the style changes)
    property bool styReady: false
    onWantChanged: {
        if (!styReady) { sty = want; return }
        if (want !== sty) styAnim.restart()
    }
    // top or bottom: a move to the other edge plays out the same way (it goes, moves, comes back)
    readonly property bool wantBottom: bc.position === "bottom"
    onWantBottomChanged: {
        if (!styReady) { atBottom = wantBottom; return }
        if (wantBottom !== atBottom) styAnim.restart()
    }
    Timer { running: true; interval: 1200; onTriggered: { bar.sty = bar.want; bar.atBottom = bar.wantBottom; bar.styReady = true } }
    SequentialAnimation {
        id: styAnim
        NumberAnimation { target: bar; property: "swap"; to: 1; duration: 170; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.accel }
        ScriptAction { script: { bar.sty = bar.want; bar.atBottom = bar.wantBottom } }
        PauseAnimation { duration: 60 }
        NumberAnimation { target: bar; property: "swap"; to: 0; duration: 520; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.decel }
    }
    readonly property bool notch: sty === "notch"
    // edge: the Dynamic style's look and island, but every side of the bar is one piece with the screen edge
    // (HugShape.qml): the left part reaches the screen's left side, the right part its right side, the middle hangs
    // from the edge with soft curves; the modules sit on tonal pills inside them
    // the black hole and the moon logo move while the pointer is on them (Settings > Bar > Logo moves all the time:
    // on, as before; it redraws the bar every frame of the screen)
    readonly property bool logoMove: bc.logo_move === true
    readonly property bool hug: sty === "edge"
    // edge: the dark frame along the edge can be turned off (Settings > Bar > Frame along the edge): then the pills
    // keep their places, without anything behind them
    readonly property bool hugFrame: hug && bc.edge_frame !== false
    readonly property bool dyn: sty === "dynamic" || hug
    readonly property bool goo: dyn                              // the Material 3 look of the Dynamic style
    property var nf: null                                         // Notifs.qml, for the island
    readonly property bool islandNotifs: hasIsland && bc.island_dynamic !== false && bc.island_notifs !== false
    readonly property bool islandVolume: hasIsland && bc.island_dynamic !== false && bc.island_volume !== false
    // the Google style's colors: tonal, made from your theme color (the same as the control center)
    // (the bar's Transparency is its own now: before V4.83 it was made 0.14 more solid here, so with the default the
    // bar was nearly solid and its blur could hardly be seen)
    NPal { id: gp; theme: "shell"; alpha: Math.min(1, bar.alpha + 0.04) }
    readonly property bool hideOn: bc.autohide === true
    readonly property bool shadowOn: bc.shadow !== false && Look.shadows && Look.shadowBar
    // auto-hide: shown while the pointer is on it (and a moment after it leaves)
    property bool peek: false
    // Infinite Desktop: it gets out of the way while you move the canvas (SUPER + ALT + mouse)
    readonly property bool shownBar: (!hideOn || peek || !on) && !(on && Infinite.hideUi)
    Timer { id: leaveT; interval: 700; onTriggered: bar.peek = false }
    readonly property bool island: sty === "island"
    readonly property bool floating: sty !== "flat"
    // V3.36: the island is the clock (an old "clock" in the layout is the island now)
    function noClock(l) { return (l || []).map(function(m) { return m === "clock" ? "island" : m }) }
    readonly property var layRaw: bc.layout ? ({ left: noClock(bc.layout.left), center: noClock(bc.layout.center), right: noClock(bc.layout.right) })
        : ({ left: ["launcher", "workspaces", "title"], center: ["island"], right: ["music", "tray", "volume", "bluetooth", "network", "battery", "notifications", "power"] })
    function laid(l, k) { return (l.left || []).indexOf(k) >= 0 || (l.center || []).indexOf(k) >= 0 || (l.right || []).indexOf(k) >= 0 }
    // the island is a module of the layout ("island", any zone, any style, with modules beside it). Taken out in
    // Bar layout (island_off) the bar has no island, only its cards (clock card, volume, notifications) stay.
    // A layout from before that (no island in it, no island_off) still gets the island in the middle of the
    // middle zone, and the modules that were in the middle stay (they were lost: the zone became just the island)
    function withIsland(c) { const a = (c || []).slice(); a.splice(Math.floor(a.length / 2), 0, "island"); return a }
    readonly property var lay: dyn && bc.island_off !== true && !laid(layRaw, "island")
        ? ({ left: layRaw.left || [], center: withIsland(layRaw.center), right: layRaw.right || [] }) : layRaw
    readonly property bool hasIsland: on && laid(lay, "island")
    property Item islSlot: null                 // where the island sits in its zone
    readonly property bool islAlone: ["left", "center", "right"].some(function(z) { const m = lay[z] || []; return m.length === 1 && m[0] === "island" })
    property bool atBottom: false
    readonly property int barH: bc.height ? bc.height : 38
    // tray icons: Settings > DarkShadow bar > Tray icon size (never taller than the bar)
    readonly property int traySize: Math.min(barH - 8, bc.tray_size ? bc.tray_size : 18)
    readonly property int nws: bc.workspaces ? bc.workspaces : 5
    readonly property string wsStyle: bc.ws_style === "pacman" || bc.ws_style === "letters" || bc.ws_style === "numbers" ? bc.ws_style : "dots"
    // the launcher button: a distro logo (Nerd Font) or the app grid (Settings > DarkShadow bar > Launcher icon)
    readonly property var logos: ({
        tux: 0xF17C, arch: 0xF303, gentoo: 0xF30D, fedora: 0xF30A, debian: 0xF306, ubuntu: 0xF31B, nixos: 0xF313,
        cachyos: 0xF385, endeavouros: 0xF322, manjaro: 0xF312, linuxmint: 0xF30E, opensuse: 0xF314, void: 0xF32E,
        alpine: 0xF300, pop: 0xF32A, kali: 0xF327, centos: 0xF304, raspbian: 0xF315, elementary: 0xF309, zorin: 0xF32F,
        nobara: 0xF380, artix: 0xF31F, slackware: 0xF318, garuda: 0xF303, mageia: 0xF310, rocky: 0xF32B
    })
    // the colors of the black hole / moon on the launcher button (Settings > DarkShadow bar > Launcher icon)
    readonly property string logoColor: bc.logo_color || "default"
    function logoPal(kind) {
        const k = logoColor, a = gp.primary
        const P = {
            white: ["#ffffff", "#e6e6ec", "#9c9caa", "#d8d8e0"],
            gold: ["#fff6e4", "#ffb35c", "#ff8a3d", "#ff9d4d"],
            ice: ["#f2fbff", "#7fd0ff", "#3d6bff", "#6fbfff"],
            rose: ["#fff0f5", "#ff7aa8", "#8a3dff", "#ff6f9f"],
            violet: ["#f6f0ff", "#b58cff", "#5b3dff", "#9d7bff"]
        }
        if (k === "theme") return [Qt.lighter(a, 1.5), a, Qt.darker(a, 1.7), a]
        if (P[k]) return P[k]
        return kind === "moon" ? ["#ffffff", "#b9bfd8", "#8a93d8", "#cfd6ff"] : ["#fff6e4", "#ffb35c", "#a46bff", "#ff9d4d"]
    }
    property var osIds: []
    DssProc {
        id: osProc
        running: true
        command: ["bash", "-c", ". /etc/os-release 2>/dev/null; echo \"$ID $ID_LIKE\""]
        stdout: DssCollector { onStreamFinished: bar.osIds = text.trim().toLowerCase().split(/\s+/) }
    }
    readonly property string logoKey: {
        const k = bc.logo || "auto"
        if (k === "apps" || k === "blackhole" || k === "moon") return k
        if (k !== "auto") return logos[k] !== undefined ? k : "apps"
        for (let i = 0; i < osIds.length; i++) {
            const id = osIds[i] === "opensuse-tumbleweed" || osIds[i] === "opensuse-leap" ? "opensuse" : osIds[i]
            if (logos[id] !== undefined) return id
        }
        return "tux"
    }
    readonly property color fg: goo ? gp.fg : "#F6F5F4"
    readonly property color dim: goo ? gp.dim : Qt.rgba(0.965, 0.961, 0.957, 0.66)
    readonly property color line: goo ? "transparent" : Qt.rgba(1, 1, 1, 0.09)
    readonly property real alpha: Look.flat ? 1 : (bc.opacity !== undefined ? bc.opacity : 0.72)
    readonly property color bg: goo ? gp.surface : Qt.rgba(0.05, 0.05, 0.05, alpha)
    readonly property real gap: hug ? 3 : (floating ? 8 : 0)
    property string cfgText: ""
    readonly property string home: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/"))

    Trigger {
        path: bar.home + "/.config/rice/bar.json"
        every: 2000
        onContent: function(t) {
            const x = t.trim()
            if (x === bar.cfgText) return
            bar.cfgText = x
            try { bar.bc = x === "" ? ({}) : JSON.parse(x) } catch (e) { }
        }
    }

    // ---------- the window ----------
    visible: on || ghostOnly || barK > 0.01
    color: "transparent"
    anchors { top: !atBottom; bottom: atBottom; left: true; right: true }
    // room below for the recording island; windows only keep away from the bar itself
    implicitHeight: barH + gap * 2 + 460      // room for the island's cards (the bar lets the pointer through there)
    exclusionMode: hideOn || !on ? ExclusionMode.Ignore : ExclusionMode.Normal
    exclusiveZone: hideOn || !on ? 0 : barH + gap * 2
    // only the bar (and the recording island) take the pointer; auto-hide: only a thin strip at the edge
    mask: Region { item: !bar.on ? hitNone : (bar.shownBar ? hitBar : hitStrip); Region { item: hitRec } Region { item: hitIsl } Region { item: hitMenu } Region { item: hitPop } }
    Item { id: hitMenu; x: bar.gap + trayMenu.x; y: body.y + trayMenu.y; width: trayMenu.visible ? trayMenu.width : 0; height: trayMenu.height }
    Item { id: hitPop; x: bar.gap + barPop.x; y: body.y + barPop.y; width: barPop.visible ? barPop.width : 0; height: barPop.height }
    // a card of the island or the tray menu is open: Esc, a click on the desktop (shell.qml) or another window closes it
    readonly property bool sticky: isl.sticky || trayMenu.open || barPop.open
    function closeCards() { isl.closeCards(); trayMenu.close(); barPop.close() }
    onStickyChanged: if (sticky) keyCatch.forceActiveFocus()
    Item { id: keyCatch; focus: true; Keys.onEscapePressed: bar.closeCards() }
    WlrLayershell.keyboardFocus: sticky ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    // the island when it has grown out of the bar
    Item { id: hitIsl; x: bar.gap + isl.x + isl.ux; y: body.y + isl.y + isl.uy; width: isl.visible && bar.shownBar ? isl.uw : 0; height: isl.uh }
    Item { id: hitBar; width: parent.width; height: bar.barH + bar.gap * 2; y: bar.atBottom ? parent.height - height : 0 }
    Item { id: hitRec; x: recIsland.x + bar.gap; width: recIsland.width; height: bar.recOn && bar.shownBar ? 54 : 0; y: bar.atBottom ? 0 : bar.barH + bar.gap * 2 }
    Item { id: hitAll; anchors.fill: parent }
    Item { id: hitNone; width: 0; height: 0 }
    Item { id: hitStrip; width: parent.width; height: 3; y: bar.atBottom ? parent.height - 3 : 0 }
    // niri: blur behind the bar's backgrounds, the island, its card and the tray menu (Hyprland: a layer rule).
    // These follow the island and its card in window coordinates, so the blur moves with them.
    Item { id: blurIsl; x: bar.gap + isl.x; y: body.y + isl.y; width: isl.ghost || !isl.visible ? 0 : isl.width; height: isl.height }
    Item { id: blurCard; x: bar.gap + isl.x + isl.cardItem.x; y: body.y + isl.y + isl.cardItem.y
        width: isl.cardShown ? isl.cardItem.width : 0; height: isl.cardItem.height }
    NiriBlur {
        win: bar
        on: (bar.on || bar.ghostOnly) && bar.shownBar && bar.swap < 0.05
        readonly property var parts: {
            const l = [], r = []
            const pill = bar.barH / 2
            if (!bar.on) { }
            else if (bar.sty === "island" || bar.sty === "flat") { l.push(bodyBg); r.push(bar.sty === "island" ? pill : 0) }
            else if (bar.notch) { l.push(notchBody); r.push(notchBg.corner) }
            else if (bar.hugFrame) { for (const z of [hl, hc, hr]) if (z.visible) { l.push(z); r.push(18) } }
            else if (bar.hug) { for (const z of [zl, zc, zr]) if (z.any && !z.onlyIsland) { l.push(z); r.push(pill) } }
            else if (body.soft) {
                for (const z of [zl, zc, zr]) if (z.any && !z.onlyIsland) { l.push(z); r.push(pill) }
            }
            // fixed radii, and the card only once it is fully open: the list stays the same while things move,
            // so niri is not sent a new blur shape on every frame of an animation
            if (!isl.ghost && isl.visible) { l.push(blurIsl); r.push(pill) }
            if (isl.cardItem.opacity > 0.98) { l.push(blurCard); r.push(28) }
            if (trayMenu.visible) { l.push(hitMenu); r.push(24) }
            if (barPop.t > 0.98) { l.push(hitPop); r.push(24) }
            return { items: l, radii: r }
        }
        items: parts.items
        radii: parts.radii
    }
    HoverHandler {
        enabled: bar.hideOn
        onHoveredChanged: { if (hovered) { leaveT.stop(); bar.peek = true } else leaveT.restart() }
    }
    WlrLayershell.layer: WlrLayer.Top
    WlrLayershell.namespace: "rice-bar"

    // ---------- screen recording (rice-hotcorner record): the island under the bar ----------
    property bool recOn: false
    property real recStart: 0
    property bool micMuted: false
    property real nowS: Date.now() / 1000
    readonly property string recTime: {
        const t = Math.max(0, Math.floor(nowS - recStart))
        return two(Math.floor(t / 3600)) + ":" + two(Math.floor(t / 60) % 60) + ":" + two(t % 60)
    }
    Trigger {
        path: bar.home + "/.cache/rice/record.json"
        every: 2500
        onContent: function(t) {
            let j = null
            try { j = t.trim() !== "" ? JSON.parse(t) : null } catch (e) { j = null }
            bar.recOn = j !== null
            if (j) { bar.recStart = j.start || Date.now() / 1000; recCheck.running = true; micProc.running = true }
        }
    }
    Timer { interval: 1000; running: bar.recOn; repeat: true; onTriggered: bar.nowS = Date.now() / 1000 }
    // a recorder that quit on its own leaves its files behind: check now and then that it still runs
    Timer { interval: 5000; running: bar.recOn; repeat: true; onTriggered: { if (!recCheck.running) recCheck.running = true; if (!micProc.running) micProc.running = true } }
    DssProc {
        id: recCheck
        command: ["bash", "-c", "p=$(cat \"$HOME/.cache/rice/record.pid\" 2>/dev/null); [ -n \"$p\" ] && kill -0 \"$p\" 2>/dev/null && echo 1 || { rm -f \"$HOME/.cache/rice/record.pid\" \"$HOME/.cache/rice/record.json\"; echo 0; }"]
        stdout: DssCollector { onStreamFinished: if (text.trim() === "0") bar.recOn = false }
    }
    DssProc {
        id: micProc
        command: ["bash", "-c", "wpctl get-volume @DEFAULT_AUDIO_SOURCE@ 2>/dev/null"]
        stdout: DssCollector { onStreamFinished: bar.micMuted = text.indexOf("MUTED") >= 0 }
    }

    // ---------- the things the bar shows ----------
    // the tray, without the network and Bluetooth applets when the bar shows Wi-Fi and Bluetooth itself (niri and
    // some distros start nm-applet and blueman on their own: a second Wi-Fi icon next to the battery)
    function hasMod(n) { const l = bar.lay || {}; return [].concat(l.left || [], l.center || [], l.right || []).indexOf(n) >= 0 }
    readonly property var trayItems: {
        const all = SystemTray.items.values || []
        const net = hasMod("network"), bt = hasMod("bluetooth")
        return all.filter(function(it) {
            const k = ((it.id || "") + " " + (it.title || "")).toLowerCase()
            if (net && /nm-applet|networkmanager|network manager|nm-tray|connman/.test(k)) return false
            if (bt && /blueman|bluetooth/.test(k)) return false
            return true
        })
    }
    // workspaces: Hyprland or niri (Wm.qml)
    readonly property int focusedId: Wm.focusedId
    function occupied(id) { return Wm.occupied(id) }
    function maxWs() { return Wm.maxWs(bar.nws) }
    // ws-go: Hyprland (its Lua dispatchers) or niri (focus-workspace)
    function goWs(id) { Wm.goWs(id) }
    function run(cmd) { DssWin.run(["bash", "-c", cmd]) }

    function pickPlayer() {
        const l = Mpris.players.values
        for (let i = 0; i < l.length; i++) if (l[i].isPlaying) return l[i]
        return null
    }
    readonly property var player: pickPlayer()
    readonly property string track: player ? ((player.trackArtist ? player.trackArtist + " - " : "") + (player.trackTitle || "")) : ""

    readonly property var sink: Pipewire.defaultAudioSink
    PwObjectTracker { objects: [bar.sink] }
    readonly property real vol: Vol.of(sink)
    readonly property bool muted: sink && sink.audio ? sink.audio.muted : false

    readonly property var batt: UPower.displayDevice
    readonly property bool hasBatt: batt && batt.isPresent && batt.isLaptopBattery
    readonly property real battPct: batt ? Math.round(batt.percentage * 100) : 0
    readonly property bool charging: batt && (batt.state === UPowerDeviceState.Charging || batt.state === UPowerDeviceState.FullyCharged)
    readonly property bool plugged: hasBatt && !UPower.onBattery

    readonly property var svc: BarSvc.state

    property var now: new Date()
    Timer { interval: bc.seconds === true ? 1000 : 15000; running: bar.on || bar.ghostOnly; repeat: true; triggeredOnStart: true; onTriggered: bar.now = new Date() }
    function two(n) { return n < 10 ? "0" + n : "" + n }
    readonly property string days: "SunMonTueWedThuFriSat"
    readonly property string months: "JanFebMarAprMayJunJulAugSepOctNovDec"
    // 24 or 12 hour clock: Settings > Sound & language > Language & region (rice-region)
    property bool h12: false
    Trigger {
        path: bar.home + "/.config/rice/region.json"
        every: 5000
        onContent: function(t) { bar.h12 = /"clock": *"12"/.test(t) }
    }
    readonly property string timeText: (h12 ? ((now.getHours() + 11) % 12 + 1) : two(now.getHours())) + ":" + two(now.getMinutes())
                                       + (bc.seconds === true ? ":" + two(now.getSeconds()) : "") + (h12 ? (now.getHours() < 12 ? " AM" : " PM") : "")
    readonly property string dateText: days.substr(now.getDay() * 3, 3) + " " + now.getDate() + " " + months.substr(now.getMonth() * 3, 3)

    // ---------- the modules ----------
    function compFor(n) {
        switch (n) {
        case "launcher": return mLauncher
        case "workspaces": return mWorkspaces
        case "title": return mTitle
        case "media": return mMedia
        case "clock": return mClock
        case "tray": return mTray
        case "volume": return mVolume
        case "network": return mNetwork
        case "bluetooth": return mBluetooth
        case "battery": return mBattery
        case "notifications": return mNotify
        case "power": return mPower
        case "island": return mIsland
        case "visualizer": return mViz
        case "weather": return mWeather
        case "sysmon": return mSysmon
        case "music": return mMusic
        }
        return null
    }

    Component { id: mLauncher
        Item { property bool shown: true; width: bar.goo ? bar.barH - 8 : (bar.logoKey === "blackhole" ? 34 : bar.logoKey === "moon" ? 28 : 22); height: bar.barH
            // Google: the launcher sits in a circle of your key color
            Rectangle { visible: bar.goo; anchors.centerIn: parent; width: parent.width; height: width; radius: lm.pressed ? width * 0.3 : width / 2
                // grey colors (a black or white wallpaper): a dark circle with a light icon, it fits the bar
                color: gp.mono ? gp.container : gp.primary; scale: lm.containsMouse ? 1.06 : 1
                Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } } }
            MIcon { visible: bar.logoKey === "apps"; anchors.centerIn: parent; name: "apps"; glyph: String.fromCodePoint(0xF003B); size: 18; color: bar.goo ? (gp.mono ? gp.inkContainer : gp.inkPrimary) : (lm.containsMouse ? bar.fg : bar.dim)
                scale: lm.containsMouse ? 1.12 : 1; Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } } }
            // DarkShadow's own logos: the black hole (its disk turns) or the moon
            BlackHoleMark { visible: bar.logoKey === "blackhole"; anchors.centerIn: parent; size: bar.goo ? bar.barH - 2 : 36; extras: false
                readonly property var pl: bar.logoPal("blackhole"); hot: pl[0]; warm: pl[1]; cool: pl[2]; glowColor: pl[3]
                running: visible && !Look.lite && (lm.containsMouse || bar.logoMove); scale: lm.containsMouse ? 1.15 : 1; Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } } }
            MoonMark { visible: bar.logoKey === "moon"; anchors.centerIn: parent; size: bar.goo ? bar.barH - 8 : 28; extras: false
                readonly property var pl: bar.logoPal("moon"); moonTop: pl[0]; moonBottom: pl[1]; glowColor: pl[3]
                running: visible && !Look.lite && (lm.containsMouse || bar.logoMove); scale: lm.containsMouse ? 1.15 : 1; Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } } }
            Text { visible: bar.logoKey !== "apps" && bar.logoKey !== "blackhole" && bar.logoKey !== "moon"; anchors.centerIn: parent; anchors.horizontalCenterOffset: 1
                text: bar.logos[bar.logoKey] !== undefined ? String.fromCodePoint(bar.logos[bar.logoKey]) : ""
                font.family: Fonts.icons; font.pixelSize: bar.goo ? 17 : 18; color: bar.goo ? (gp.mono ? gp.inkContainer : gp.inkPrimary) : bar.fg
                scale: lm.containsMouse ? 1.15 : 1; Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } } }
            MouseArea { id: lm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); bar.run("\"$HOME/.local/bin/rice-launch\"") } }
        }
    }

    Component { id: mWorkspaces
        Item {
            id: wsm
            property bool shown: true
            readonly property string st: bar.wsStyle
            readonly property bool pac: st === "pacman"
            readonly property bool dots: st === "dots"
            // every workspace has the same width, so nothing in the row moves; what moves is one marker that slides.
            // The sliding and the popping run as Animators, on the render thread: they stay smooth even while the
            // shell is busy (the desktop widgets coming back when you switch to an empty workspace)
            readonly property real cw: dots ? 16 : (pac ? 20 : 18)
            readonly property int count: bar.maxWs()
            readonly property int fi: bar.focusedId - 1
            readonly property bool fiOk: fi >= 0 && fi < count
            width: wsRow.width; height: bar.barH

            Row {
                id: wsRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: wsm.dots ? 7 : 0
                Repeater {
                    model: wsm.count
                    delegate: Item {
                        id: cell
                        readonly property int wid: index + 1
                        readonly property bool act: bar.focusedId === wid
                        readonly property bool occ: bar.occupied(wid)
                        width: wsm.dots && act ? 40 : wsm.cw; height: bar.barH
                        Behavior on width { enabled: wsm.dots; NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.4 } }

                        // dots: a dot, the active one stretched into a pill
                        Rectangle {
                            visible: wsm.dots
                            anchors.centerIn: parent
                            width: parent.width; height: bar.goo && act ? 22 : (bar.goo ? 10 : 14); radius: height / 2
                            Behavior on height { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }
                            color: bar.goo ? (act ? bar.wsTint(wid) : (occ ? gp.inkContainer : gp.track))
                                           : (act ? bar.wsTint(wid) : (occ ? Qt.rgba(0.965, 0.961, 0.957, 0.62) : Qt.rgba(0.965, 0.961, 0.957, 0.24)))
                            // Google: the active workspace shows its number
                            Text { anchors.centerIn: parent; visible: bar.goo; opacity: act ? 1 : 0; text: String(wid)
                                color: gp.inkPrimary; font.family: Fonts.main; font.pixelSize: 12; font.weight: Font.Bold
                                Behavior on opacity { NumberAnimation { duration: 200 } } }
                            scale: wm.containsMouse && !act ? 1.18 : 1
                            Behavior on color { ColorAnimation { duration: 200 } }
                            Behavior on scale { ScaleAnimator { duration: 120 } }
                        }
                        // pacman: pac-dots; the one Pac-Man sits on is gone, the ones he has passed are eaten
                        Rectangle {
                            visible: wsm.pac
                            anchors.centerIn: parent
                            width: occ ? 10 : 7; height: width; radius: width / 2
                            color: Qt.rgba(0.965, 0.961, 0.957, occ ? 0.9 : 0.45)
                            opacity: act ? 0 : (wid < bar.focusedId ? 0.25 : 1)
                            scale: wm.containsMouse ? 1.35 : 1
                            Behavior on opacity { OpacityAnimator { duration: 260 } }
                            Behavior on scale { ScaleAnimator { duration: 120 } }
                        }
                        // letters (A B C) or numbers (1 2 3); Dynamic: the active one sits in a pill of your key color
                        Rectangle {
                            visible: bar.goo && !wsm.dots && !wsm.pac
                            anchors.centerIn: parent
                            width: 22; height: 22; radius: 11
                            color: bar.wsTint(wid)
                            opacity: act ? 1 : 0
                            scale: act ? 1 : 0.5
                            Behavior on opacity { NumberAnimation { duration: 200 } }
                            Behavior on scale { NumberAnimation { duration: 280; easing.type: Easing.OutBack } }
                        }
                        Text {
                            visible: !wsm.dots && !wsm.pac
                            anchors.centerIn: parent
                            text: wsm.st === "numbers" ? String(wid) : String.fromCharCode(64 + wid)
                            font.family: Fonts.main; font.pixelSize: 14; font.weight: act ? Font.Bold : Font.Medium
                            color: bar.goo ? (act ? gp.inkPrimary : (occ ? gp.fg : gp.dim)) : (act ? bar.fg : (occ ? Qt.rgba(0.965, 0.961, 0.957, 0.9) : Qt.rgba(0.965, 0.961, 0.957, 0.32)))
                            scale: act ? (bar.goo ? 1 : 1.18) : (wm.containsMouse ? 1.12 : 1)
                            Behavior on scale { ScaleAnimator { duration: 220; easing.type: Easing.OutBack } }
                        }
                        MouseArea { id: wm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: bar.goWs(wid) }
                    }
                }
            }

            // Pac-Man: still and white; he only chomps while he runs to the workspace you switch to
            Item {
                id: pacBox
                visible: wsm.pac && wsm.fiOk
                width: 18; height: 18
                anchors.verticalCenter: parent.verticalCenter
                readonly property real tx: Math.max(0, wsm.fi) * wsm.cw + (wsm.cw - width) / 2
                property bool facingLeft: false
                x: tx
                Behavior on x { XAnimator { duration: 380; easing.type: Easing.InOutCubic } }
                onTxChanged: { facingLeft = tx < x; chomp.restart() }
                Canvas {
                    id: pac
                    anchors.fill: parent
                    property real mouth: 0.2
                    renderStrategy: Canvas.Threaded
                    SequentialAnimation {
                        id: chomp
                        SequentialAnimation {
                            loops: 3
                            NumberAnimation { target: pac; property: "mouth"; to: 0.02; duration: 65 }
                            NumberAnimation { target: pac; property: "mouth"; to: 0.26; duration: 65 }
                        }
                        NumberAnimation { target: pac; property: "mouth"; to: 0.2; duration: 80 }
                    }
                    transform: Scale { origin.x: 9; xScale: pacBox.facingLeft ? -1 : 1 }
                    onMouthChanged: requestPaint()
                    onVisibleChanged: if (visible) requestPaint()
                    onPaint: {
                        const c = getContext("2d")
                        c.reset()
                        c.fillStyle = "#F6F5F4"
                        c.beginPath()
                        c.moveTo(width / 2, height / 2)
                        c.arc(width / 2, height / 2, width / 2, mouth * Math.PI, (2 - mouth) * Math.PI)
                        c.closePath()
                        c.fill()
                    }
                }
            }
            // the wheel over the workspaces: one workspace per notch; a touchpad swipe: one workspace per swipe
            // (it sends dozens of small events: each of them went one workspace further)
            WheelHandler { id: wsWheel; acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                property real sum: 0
                property bool done: false
                property Timer rest: Timer { interval: 260; onTriggered: { wsWheel.sum = 0; wsWheel.done = false } }
                onWheel: function(e) {
                    // a mouse wheel: one step per full notch (high-resolution wheels send small parts of one)
                    if (e.pixelDelta.y === 0) { rest.restart(); sum += e.angleDelta.y; if (Math.abs(sum) >= 120) { bar.goWs(sum > 0 ? "e-1" : "e+1"); sum = 0 } return }
                    rest.restart()
                    if (done) return
                    sum += e.pixelDelta.y
                    if (Math.abs(sum) >= 40) { bar.goWs(sum > 0 ? "e-1" : "e+1"); done = true }
                } }
        }
    }

    // the window's title, then its app's icon at the end.
    // The icon is found as a file (Icons.qml) and drawn by a plain Image that loads in the background.
    // V4.02 and before drew it through the icon theme (Quickshell.iconPath + IconImage) right when the window
    // changed: Qt 6.12's SVG icon engine can crash there, and it took the bar and the widgets down with it.
    Component { id: mTitle
        Item {
            id: tt
            readonly property var tl: ToplevelManager.activeToplevel
            readonly property string title: tl ? (tl.title || "") : ""
            // the app's icon name, worked out a moment after the window changes (never inside that change)
            property string appId: ""
            Timer { id: idT; interval: 120; onTriggered: tt.appId = tt.tl && tt.tl.appId ? tt.tl.appId : "" }
            onTlChanged: idT.restart()
            Connections { target: tt.tl; ignoreUnknownSignals: true; function onAppIdChanged() { idT.restart() } }
            Component.onCompleted: idT.restart()
            readonly property string iconName: {
                if (appId === "") return ""
                const e = DesktopEntries.heuristicLookup(appId)
                return e && e.icon ? e.icon : appId.toLowerCase()
            }
            readonly property string appIcon: Icons.get(iconName)
            readonly property bool shown: title !== ""
            // Settings > DarkShadow bar > App icon by the title: left, right (default) or off
            readonly property string side: bar.bc.title_icon === "left" || bar.bc.title_icon === "off" ? bar.bc.title_icon : "right"
            height: bar.barH
            width: Math.min(tTxt.implicitWidth, Math.max(120, bar.width * 0.2)) + (ic.visible ? 24 : 0)
            Text { id: tTxt; anchors.verticalCenter: parent.verticalCenter
                x: ic.visible && tt.side === "left" ? 24 : 0
                width: Math.min(implicitWidth, Math.max(120, bar.width * 0.2)); elide: Text.ElideRight
                text: tt.title; color: bar.dim; font.family: Fonts.main; font.pixelSize: 12 }
            Image { id: ic; visible: tt.appIcon !== "" && tt.side !== "off" && status === Image.Ready
                anchors.verticalCenter: parent.verticalCenter
                x: tt.side === "left" ? 0 : tTxt.width + 8; width: 16; height: 16
                sourceSize.width: 32; sourceSize.height: 32
                asynchronous: true; cache: true; smooth: true; mipmap: true; fillMode: Image.PreserveAspectFit
                source: tt.appIcon
                opacity: visible ? 1 : 0; Behavior on opacity { NumberAnimation { duration: 200 } } }
        }
    }

    Component { id: mMedia
        Text { id: mt; readonly property bool shown: bar.track !== ""; height: bar.barH; verticalAlignment: Text.AlignVCenter
            width: Math.min(implicitWidth, 220); elide: Text.ElideRight
            // the last song stays written while the module folds away (paused, stopped)
            property string keep: ""
            Component.onCompleted: keep = bar.track
            Connections { target: bar; function onTrackChanged() { if (bar.track !== "") mt.keep = bar.track } }
            text: "\u266a  " + keep
            color: bar.dim; font.family: Fonts.main; font.pixelSize: 12 }
    }

    Component { id: mClock
        Item { property bool shown: true; height: bar.barH
            width: bar.goo ? gRow.width + 8 : Math.max(ct.implicitWidth, cd.implicitWidth) + 12
            // Google: the time, then the date, side by side
            Row { id: gRow; visible: bar.goo; anchors.centerIn: parent; spacing: 8
                Text { anchors.verticalCenter: parent.verticalCenter; text: bar.timeText; color: gp.inkContainer
                    font.family: Fonts.main; font.pixelSize: 15; font.weight: Font.DemiBold }
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 4; height: 4; radius: 2; color: gp.inkContainer; opacity: 0.5 }
                Text { anchors.verticalCenter: parent.verticalCenter; text: bar.dateText; color: gp.inkContainer; opacity: 0.8
                    font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.Medium } }
            Text { id: ct; visible: !bar.goo; anchors.centerIn: parent; text: bar.timeText; color: bar.fg; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.Medium
                opacity: cm.containsMouse ? 0 : 1; scale: cm.containsMouse ? 0.9 : 1
                Behavior on opacity { NumberAnimation { duration: 160 } }
                Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } } }
            Text { id: cd; visible: !bar.goo; anchors.centerIn: parent; text: bar.dateText; color: bar.fg; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.Medium
                opacity: cm.containsMouse ? 1 : 0; scale: cm.containsMouse ? 1 : 1.1
                Behavior on opacity { NumberAnimation { duration: 160 } }
                Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } } }
            MouseArea { id: cm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: { Sfx.play("click"); bar.run("\"$HOME/.local/bin/rice-dashboard\" open") } }
        }
    }

    Component { id: mTray
        Row { readonly property bool shown: bar.trayItems.length > 0; height: bar.barH; spacing: 10
            Repeater {
                model: bar.trayItems
                delegate: Item {
                    id: ti
                    width: bar.traySize; height: bar.barH
                    IconImage { anchors.centerIn: parent; implicitSize: bar.traySize; source: Icons.src(modelData.icon)
                        scale: tm.containsMouse ? 1.15 : 1; Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } } }
                    MouseArea {
                        id: tm; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                        cursorShape: Qt.PointingHandCursor
                        // left: the app, right: its menu in the shell's look (with Close app), middle: its second action
                        onClicked: function(m) {
                            if (m.button === Qt.RightButton || modelData.onlyMenu) {
                                const p = ti.mapToItem(body, ti.width / 2, 0)
                                if (trayMenu.open && trayMenu.item === modelData) trayMenu.close()
                                else { barPop.close(); trayMenu.show(modelData, p.x) }
                            } else if (m.button === Qt.MiddleButton) modelData.secondaryActivate()
                            else modelData.activate()
                        }
                    }
                }
            }
        }
    }

    Component { id: mVolume
        Item { property bool shown: true; width: 24; height: bar.barH
            MIcon { anchors.centerIn: parent; size: 22; color: bar.muted ? bar.dim : bar.fg
                name: bar.muted ? "volume_off" : (bar.vol < 0.34 ? "volume_mute" : (bar.vol < 0.67 ? "volume_down" : "volume_up"))
                glyph: String.fromCodePoint(bar.muted ? 0xF0581 : (bar.vol < 0.34 ? 0xF057F : (bar.vol < 0.67 ? 0xF0580 : 0xF057E))) }
            MouseArea { id: vm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(m) {
                    // left: the sound menu, right: mute
                    if (m.button === Qt.LeftButton) { const p = mapToItem(null, width / 2, 0); bar.run("\"$HOME/.local/bin/rice-volume\" menu " + Math.round(p.x)); return }
                    if (bar.sink && bar.sink.audio) bar.sink.audio.muted = !bar.sink.audio.muted
                }
                onWheel: function(e) { Vol.nudge(bar.sink, Look.notches(e) * 0.04) } }
            HoverLine { on: vm.containsMouse; full: 16 * Math.min(1, bar.vol); color: bar.fg }   // how loud, as a line under the icon
        }
    }

    Component { id: mNetwork
        Item { property bool shown: true; width: 22; height: bar.barH
            MIcon { anchors.centerIn: parent; size: 18; color: bar.svc.network ? bar.fg : bar.dim
                name: bar.svc.network ? "wifi" : "wifi_off"
                glyph: String.fromCodePoint(bar.svc.network ? 0xF05A9 : 0xF05AA) }
            MouseArea { id: nm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(m) {
                    const p = mapToItem(null, width / 2, 0)
                    bar.run("\"$HOME/.local/bin/rice-wifi\" " + Math.round(p.x))
                } }
            HoverLine { on: nm.containsMouse; color: bar.fg }
        }
    }

    Component { id: mBluetooth
        Item { property bool shown: true; width: 22; height: bar.barH
            MIcon { anchors.centerIn: parent; size: 18; color: bar.svc.bluetooth ? bar.fg : bar.dim
                name: bar.svc.bluetooth ? "bluetooth" : "bluetooth_disabled"
                glyph: String.fromCodePoint(bar.svc.bluetooth ? 0xF00AF : 0xF00B2) }
            MouseArea { id: bm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(m) {
                    const p = mapToItem(null, width / 2, 0)
                    bar.run("\"$HOME/.local/bin/rice-bluetooth\" " + Math.round(p.x))
                } }
            HoverLine { on: bm.containsMouse; color: bar.fg }
        }
    }

    Component { id: mBattery
        Item { readonly property bool shown: bar.hasBatt; width: bt.implicitWidth + 4; height: bar.barH
            Row { id: bt; anchors.centerIn: parent; spacing: 5
                MIcon { size: 16; anchors.verticalCenter: parent.verticalCenter
                    color: !bar.charging && bar.battPct <= 15 ? "#ff6b6b" : bar.fg
                    name: bar.charging ? "battery_charging_full" : "battery_full"
                    glyph: String.fromCodePoint(bar.charging ? 0xF0084 : 0xF0079) }
                Text { anchors.verticalCenter: parent.verticalCenter; text: bar.battPct + "%"; color: bar.dim; font.family: Fonts.main; font.pixelSize: 12 }
            }
        }
    }

    Component { id: mNotify
        Item { property bool shown: true; width: 22; height: bar.barH
            MIcon { anchors.centerIn: parent; size: 18; color: bar.svc.dnd ? bar.dim : bar.fg
                name: bar.svc.dnd ? "notifications_off" : "notifications"
                glyph: String.fromCodePoint(bar.svc.dnd ? 0xF009B : 0xF009A) }
            MouseArea { id: dm; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.LeftButton | Qt.RightButton; cursorShape: Qt.PointingHandCursor
                onClicked: function(m) { bar.run(m.button === Qt.RightButton ? "\"$HOME/.local/bin/rice-cc\" dnd toggle" : "\"$HOME/.local/bin/rice-cc\" toggle"); BarSvc.refreshSoon() } }
            HoverLine { on: dm.containsMouse; color: bar.fg }
        }
    }

    // the island's place in its zone: as wide as the island at rest; the island itself floats on top (it grows
    // out of the bar), centred on this place
    Component { id: mIsland
        Item {
            id: ip
            property bool shown: true
            width: Math.round(isl.restW); height: bar.barH
            Behavior on width { NumberAnimation { duration: Look.lite ? 180 : 300; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }
            // where it is in the bar (follows the slot, the row and the zone as they move)
            readonly property real bx: parent && parent.parent && parent.parent.parent && parent.parent.parent.parent
                ? x + parent.x + parent.parent.x + parent.parent.parent.x + parent.parent.parent.parent.x : 0
            // where it is inside its zone (without the zone's own place, so the notch can centre on it)
            readonly property real inZone: parent && parent.parent && parent.parent.parent
                ? x + parent.x + parent.parent.x + parent.parent.parent.x : 0
            Component.onCompleted: bar.islSlot = ip
            Component.onDestruction: if (bar.islSlot === ip) bar.islSlot = null
        }
    }

    // the music, as a few moving bars (Bar layout > Visualizer)
    Component { id: mViz
        Item { property bool shown: true; width: vz.implicitWidth + 8; height: bar.barH
            BarViz { id: vz; anchors.centerIn: parent; active: bar.on; thick: Math.round(bar.barH * 0.5); color: bar.goo ? gp.primary : bar.fg }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: if (bar.player) bar.player.togglePlaying() }
        }
    }

    // ---------- Weather, System, Date: small modules that open a card under them (BarPop.qml) ----------
    readonly property var wx: BarSvc.wx
    // every 30 minutes, as before; the process itself is shared (BarSvc.qml) and refuses to run twice
    Timer { interval: 1800000; running: (bar.on || bar.ghostOnly) && (bar.hasMod("weather") || barPop.open); repeat: true; triggeredOnStart: true; onTriggered: BarSvc.refreshWx() }
    // one rice-stats for the whole shell (Stats.qml)
    readonly property var st: Stats.st
    StatsHold { on: (bar.on || bar.ghostOnly) && (bar.hasMod("sysmon") || (barPop.open && barPop.kind === "system")) }
    // a side bar (VBar.qml) opens these cards too: "KIND" in ~/.cache/rice/barpop-open
    Trigger { name: bar.extra ? "" : "barpop-open"; onFired: function(o) { const p = o.trim().split(" "); barPop.toggle(p[1] || "calendar", p[2] !== undefined ? parseFloat(p[2]) : 200) } }
    function popAt(it, k) { const p = it.mapToItem(body, it.width / 2, 0); barPop.fromW = it.width + 16; barPop.toggle(k, p.x) }

    Component { id: mWeather
        Item { property bool shown: true; width: wr.implicitWidth + 4; height: bar.barH
            Row { id: wr; anchors.centerIn: parent; spacing: 5
                WxIcon { anchors.verticalCenter: parent.verticalCenter; size: 17; cond: bar.wx ? bar.wx.cond : ""; color: bar.goo ? gp.primary : bar.fg
                    night: { const h = bar.now.getHours(); return h < 6 || h >= 19 } }
                Text { anchors.verticalCenter: parent.verticalCenter; text: bar.wx ? bar.wx.temp + "°" : "--"; color: bar.fg; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.Medium }
            }
            MouseArea { id: wm2; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); bar.popAt(parent, "weather") } }
            HoverLine { on: wm2.containsMouse; color: bar.fg }
        }
    }
    Component { id: mSysmon
        Item { property bool shown: true; width: sr.implicitWidth + 4; height: bar.barH
            Row { id: sr; anchors.centerIn: parent; spacing: 6
                Repeater {
                    // a fixed model: the rings stay and only their value moves (a model built from the numbers made
                    // all three again at every reading, so they blinked out every few seconds)
                    model: [["memory", 0xF035B, "cpu"], ["memory_alt", 0xF061A, "ram"], ["hard_drive", 0xF02CA, "disk"]]
                    delegate: Item {
                        width: 24; height: 24; anchors.verticalCenter: parent.verticalCenter
                        // the use, gliding to each new reading
                        property real use: Math.max(0, Math.min(100, bar.st[modelData[2]] || 0)) / 100
                        Behavior on use { NumberAnimation { duration: 900; easing.type: Easing.OutCubic } }
                        // a ring that fills with the use
                        Canvas {
                            anchors.fill: parent
                            readonly property real v: parent.use
                            readonly property color c: bar.goo ? gp.primary : bar.fg
                            onVChanged: requestPaint(); onCChanged: requestPaint()
                            onPaint: {
                                const g = getContext("2d"); g.reset(); g.lineWidth = 2.4; g.lineCap = "round"
                                g.strokeStyle = Qt.rgba(c.r, c.g, c.b, 0.22); g.beginPath(); g.arc(12, 12, 10, 0, Math.PI * 2); g.stroke()
                                if (v > 0.005) { g.strokeStyle = c; g.beginPath(); g.arc(12, 12, 10, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * Math.min(1, v)); g.stroke() }
                            }
                        }
                        MIcon { anchors.centerIn: parent; size: 11; name: modelData[0]; glyph: String.fromCodePoint(modelData[1]); color: bar.fg }
                    }
                }
            }
            MouseArea { id: sm2; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); bar.popAt(parent, "system") } }
            HoverLine { on: sm2.containsMouse; color: bar.fg }
        }
    }
    // Music: a note (three small bars dancing while a song plays); a click opens the music card
    Component { id: mMusic
        Item { property bool shown: true; width: mu.width; height: bar.barH
            Rectangle {
                id: mu
                anchors.centerIn: parent
                readonly property bool playing: bar.player !== null
                // nothing plays: just the note, as narrow as the other icons; a song: a pill with the bars
                width: playing ? 46 : 24; height: bar.barH - 12; radius: height / 2
                Behavior on width { NumberAnimation { duration: 320; easing.type: Easing.OutBack } }
                color: playing ? (bar.goo ? gp.primary : Qt.rgba(1, 1, 1, mum.containsMouse ? 0.16 : 0.08))
                               : (mum.containsMouse ? Qt.rgba(1, 1, 1, 0.1) : "transparent")
                Behavior on color { ColorAnimation { duration: 200 } }
                scale: mum.pressed ? 0.92 : (mum.containsMouse ? 1.05 : 1)
                Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } }
                readonly property color ic: bar.goo && playing ? gp.inkPrimary : bar.fg
                Row { anchors.centerIn: parent; spacing: 3
                    MIcon { anchors.verticalCenter: parent.verticalCenter; size: 16; name: "music_note"; glyph: String.fromCodePoint(0xF075A); color: mu.ic }
                    Row { visible: mu.playing; anchors.verticalCenter: parent.verticalCenter; spacing: 2; height: 12
                        Repeater { model: 3
                            Rectangle { width: 3; radius: 1.5; anchors.bottom: parent.bottom; color: mu.ic
                                property real h: 4
                                height: h
                                SequentialAnimation on h { running: mu.playing && !Look.lite; loops: Animation.Infinite
                                    NumberAnimation { to: 12; duration: 280 + index * 80; easing.type: Easing.InOutSine }
                                    NumberAnimation { to: 3; duration: 320 + index * 60; easing.type: Easing.InOutSine } } } } }
                }
            }
            MouseArea { id: mum; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); bar.popAt(parent, "music") } }
        }
    }
    Component { id: mPower
        Item { property bool shown: true; width: 22; height: bar.barH
            MIcon { anchors.centerIn: parent; name: "power_settings_new"; glyph: String.fromCodePoint(0xF0425); size: 18; color: pm.containsMouse ? "#ff6b6b" : bar.dim
                Behavior on color { ColorAnimation { duration: 150 } } }
            MouseArea { id: pm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(m) {
                    const p = mapToItem(null, width / 2, 0)
                    bar.run("\"$HOME/.local/bin/rice-power\" " + Math.round(p.x))
                } }
            HoverLine { on: pm.containsMouse; color: "#ff6b6b" }
        }
    }

    // each workspace has its own color: your key color turned around the color wheel
    // the workspace you are on, in the bar and the island: the wallpaper's color (it used to turn a little around the
    // color wheel for every workspace, so each one had its own color)
    function wsTint(id) { return gp.primary }

    // ---------- one side of the bar: a row of modules ----------
    component Zone: Rectangle {
        id: zone
        property var mods: []
        property bool soft: false       // a pill of its own (islands), else no background
        readonly property bool any: row.width > 0
        visible: any
        // Google: the zone with the clock gets a tinted pill of its own
        readonly property bool tint: bar.goo && mods.length === 1 && mods[0] === "clock"
        readonly property bool onlyIsland: mods.length === 1 && mods[0] === "island"
        // the gap between modules lives inside each module, so a module that comes or goes slides the others
        readonly property real sp: bar.sty === "pills" ? 8 : (bar.goo ? 2 : 14)
        width: Math.max(0, row.width - sp) + (soft && any && !onlyIsland ? (bar.goo ? 12 : 28) : 0)
        // edge: a tonal pill a little inside the edge's shape
        height: bar.hug ? bar.barH - 8 : bar.barH
        y: bar.hug ? 4 : 0
        radius: height / 2
        color: soft && !onlyIsland ? (tint ? gp.container : (bar.hug ? body.hpill : bar.bg)) : "transparent"
        Behavior on color { ColorAnimation { duration: 300 } }
        border.width: soft && !onlyIsland ? 1 : 0
        border.color: bar.line
        Row {
            id: row
            x: Math.round((parent.width - width) / 2); y: Math.round((parent.height - height) / 2)
            spacing: 0
            // edge: the pill is a little lower than the bar; the modules keep the bar's own height
            Repeater {
                model: zone.mods
                delegate: Item {
                    id: slot
                    readonly property bool shown: ld.status === Loader.Ready && ld.item && ld.item.shown !== false
                    readonly property bool pill: bar.sty === "pills"
                    // Google: every module has room around it and a soft state layer when you point at it
                    readonly property bool lay: bar.goo && modelData !== "launcher" && modelData !== "island" && !zone.tint
                    // it grows in when it appears (a new module, music that starts) and folds away when it goes
                    property bool born: false
                    Timer { running: true; interval: 40 + index * 45; onTriggered: slot.born = true }
                    property real k: born && shown ? 1 : 0
                    Behavior on k { NumberAnimation { duration: slot.k < 0.5 ? 320 : 220; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }
                    readonly property real full: ld.width + (pill ? 24 : 0) + (lay ? 16 : (bar.goo ? 4 : 0))
                    visible: k > 0.01
                    width: (full + zone.sp) * k
                    height: bar.barH
                    // a wide module (the window title) is centered in a slot that is still narrow while it grows in
                    // or folds away: it showed over the buttons beside it. Clipped while the slot is not full.
                    clip: k < 0.995
                    opacity: Math.min(1, k * 1.4)
                    scale: 0.7 + 0.3 * k
                    HoverHandler { id: sh; enabled: slot.lay }
                    Rectangle { visible: slot.lay; anchors.centerIn: parent; width: slot.full; height: bar.barH - 8; radius: height / 2
                        color: bar.fg; opacity: sh.hovered ? 0.1 : 0
                        Behavior on opacity { NumberAnimation { duration: 160 } } }
                    Rectangle { visible: slot.pill; anchors.centerIn: parent; width: slot.full; height: bar.barH
                        radius: height / 2; color: bar.bg; border.width: 1; border.color: bar.line }
                    Loader { id: ld; anchors.centerIn: parent; sourceComponent: bar.compFor(modelData) }
                }
            }
        }
    }

    // ---------- the bar ----------
    Item {
        id: body
        x: bar.gap; y: bar.atBottom ? bar.height - bar.barH - bar.gap : bar.gap
        width: parent.width - 2 * bar.gap
        height: bar.barH
        opacity: (bar.shownBar ? 1 : 0) * (1 - bar.swap)
        Behavior on opacity { enabled: !styAnim.running; NumberAnimation { duration: 240 } }
        transform: [Scale { origin.x: body.width / 2; origin.y: bar.atBottom ? body.height : 0; xScale: 1 - 0.035 * bar.swap; yScale: 1 - 0.2 * bar.swap },
                    Translate { y: (bar.atBottom ? 8 : -8) * bar.swap }, Translate {
            y: !bar.on && !bar.ghostOnly ? (bar.atBottom ? 12 : -12) : (bar.shownBar ? 0 : (bar.atBottom ? 1 : -1) * (bar.barH + bar.gap * 2 + 4))
            Behavior on y { NumberAnimation { duration: 360; easing.type: bar.shownBar ? Easing.OutBack : Easing.InCubic; easing.overshoot: 0.8 } }
        }]

        // island / flat: one background behind everything
        Rectangle {
            id: bodyBg
            anchors.fill: parent
            visible: (bar.sty === "island" || bar.sty === "flat") && bar.barK > 0.01
            opacity: bar.barK
            radius: bar.sty === "island" ? height / 2 : 0
            color: bar.bg
            border.width: bar.sty === "island" ? 1 : 0
            border.color: bar.line
            Rectangle { visible: bar.sty === "flat"; width: parent.width; height: 1; color: bar.line
                anchors.bottom: bar.atBottom ? undefined : parent.bottom; anchors.top: bar.atBottom ? parent.top : undefined }
        }

        readonly property bool soft: bar.sty === "islands" || bar.goo
        readonly property real edge: bar.sty === "island" ? 16 : (bar.sty === "flat" ? 14 : 0)
        // notch: every zone sits inside one notch in the middle of the edge
        readonly property real ngap: 22
        readonly property real nW: (zl.any ? zl.width : 0) + (zc.any ? zc.width : 0) + (zr.any ? zr.width : 0)
                                   + ngap * Math.max(0, (zl.any ? 1 : 0) + (zc.any ? 1 : 0) + (zr.any ? 1 : 0) - 1)
        // with the island in the notch, the island sits exactly in the middle of the screen and the notch
        // reaches as far to both sides (the shorter side gets a little more room)
        readonly property string islZone: !bar.notch || !bar.islSlot ? ""
            : ((bar.lay.left || []).indexOf("island") >= 0 ? "left" : ((bar.lay.center || []).indexOf("island") >= 0 ? "center"
            : ((bar.lay.right || []).indexOf("island") >= 0 ? "right" : "")))
        readonly property real islRel: islZone === "" ? -1
            : (islZone === "left" ? 0 : (islZone === "center" ? (zl.any ? zl.width + ngap : 0) : nW - zr.width)) + bar.islSlot.inZone + bar.islSlot.width / 2
        readonly property bool islMid: islRel >= 0
        readonly property real nX: islMid ? Math.round(width / 2 - islRel) : Math.round((width - nW) / 2)
        readonly property real nHalf: islMid ? Math.max(islRel, nW - islRel) : nW / 2

        // a soft shadow under every background (the bar floats above the wallpaper)
        Repeater {
            model: bar.shadowOn && !bar.hug ? (bar.sty === "island" ? [body] : (bar.sty === "islands" || bar.goo ? [zl, zc, zr] : [])) : []
            delegate: Item {
                readonly property var src: modelData
                visible: src.visible !== false && src.width > 0 && src.onlyIsland !== true && bar.barK > 0.01
                opacity: bar.barK
                x: src === body ? 0 : src.x; y: (src === body ? 0 : src.y) + 3
                width: src.width; height: bar.barH
                Rectangle { anchors.fill: parent; anchors.margins: -1; radius: height / 2; color: Qt.rgba(0, 0, 0, 0.18) }
                Rectangle { anchors.fill: parent; anchors.margins: -3; radius: height / 2; color: Qt.rgba(0, 0, 0, 0.08) }
            }
        }

        // the notch's body without its ears and the room for its shadow (niri blurs exactly this)
        Item { id: notchBody; visible: bar.notch; x: notchBg.x + notchBg.ear; y: notchBg.y
            width: Math.max(0, notchBg.width - 2 * notchBg.ear); height: notchBg.height }
        // ---------- notch background (and its shadow) ----------
        NotchShape {
            id: notchBg
            visible: bar.notch && bar.barK > 0.01
            opacity: bar.barK
            flip: bar.atBottom
            width: Math.round(2 * body.nHalf) + 2 * ear + 40
            height: bar.barH + bar.gap
            x: Math.round(body.width / 2 - body.nHalf) - ear - 20
            y: bar.atBottom ? 0 : -bar.gap
            fill: Qt.rgba(0.02, 0.02, 0.025, Look.flat ? 1 : bar.alpha)
            shadow: bar.shadowOn
        }

        // ---------- edge: the parts of the bar that are one piece with the screen edge ----------
        readonly property real hpad: 7
        // the edge's own color is the darkest layer (like the screen's frame), the pills on it one step lighter.
        // Both follow the bar's Transparency, so the blur behind them shows
        readonly property color hbase: Qt.darker(gp.surface, 1.35)
        readonly property color hfill: Qt.rgba(hbase.r, hbase.g, hbase.b, Look.flat ? 1 : Math.min(1, bar.alpha + 0.1))
        readonly property color hcard: Qt.lighter(gp.card, 1.12)
        readonly property color hpill: Qt.rgba(hcard.r, hcard.g, hcard.b, Look.flat ? 1 : Math.min(1, bar.alpha + (bar.hugFrame ? 0.16 : 0.1)))
        // where a part is: its zone, and the island too when it sits there (the island grows on its own, for the
        // player, a notification, a recording; the part grows with it in the same frame). No animation of its own:
        // V4.82 eased these after the zones and the island, so the parts lagged and swung when they changed
        function spanOf(z) {
            let a = z.x, b = z.x + z.width
            if (bar.islSlot && isl.visible && !isl.ghost && z.mods.indexOf("island") >= 0) {
                a = Math.min(a, isl.x); b = Math.max(b, isl.x + isl.width)
            }
            return [a, b]
        }
        component Hug: HugShape {
            property Item zn
            readonly property var sp: body.spanOf(zn)
            visible: bar.hugFrame && zn.any && bar.barK > 0.01
            opacity: bar.barK
            flip: bar.atBottom
            fill: body.hfill
            shadow: bar.shadowOn
            height: bar.barH + bar.gap
            y: bar.atBottom ? 0 : -bar.gap
        }
        Hug { id: hl; zn: zl; openL: true; x: -bar.gap; width: Math.round(sp[1] + body.hpad + ear + bar.gap) }
        Hug { id: hc; zn: zc; x: Math.round(sp[0] - body.hpad - ear); width: Math.round(sp[1] - sp[0] + 2 * (body.hpad + ear)) }
        Hug { id: hr; zn: zr; openR: true; x: Math.round(sp[0] - body.hpad - ear); width: body.width - x + bar.gap }

        Zone { id: zl; mods: bar.lay.left || []; soft: body.soft; opacity: bar.barK; visible: bar.barK > 0.01
            x: bar.notch ? body.nX : (bar.hug ? 8 : body.edge) }
        Zone { id: zc; mods: bar.lay.center || []; soft: body.soft; opacity: bar.barK; visible: bar.barK > 0.01
            x: Math.round(bar.notch ? body.nX + (zl.any ? zl.width + body.ngap : 0) : (body.width - width) / 2) }
        Zone { id: zr; mods: bar.lay.right || []; soft: body.soft; opacity: bar.barK; visible: bar.barK > 0.01
            x: bar.notch ? body.nX + body.nW - width : body.width - width - (bar.hug ? 8 : body.edge) }

        // ---------- dynamic: the island in the middle ----------
        DynIsland {
            id: isl
            // without the island module only its cards show (the clock, SUPER + ALT + D, notifications stay pop-ups)
            visible: bar.on || bar.ghostOnly
            ghost: !bar.on || !(bar.hasIsland && bar.islSlot !== null)
            onInfoOpenChanged: if (infoOpen) barPop.close()
            onPinnedChanged: if (pinned) barPop.close()
            bar: bar
            pal: gp
            nf: bar.nf
            // in the notch it is the notch's own color, so at rest it is part of it
            areaW: body.width
            fill: bar.notch ? notchBg.fill : (bar.hug ? body.hpill : (bar.goo ? gp.surface : Qt.rgba(bar.bg.r, bar.bg.g, bar.bg.b, Math.min(1, bar.alpha + 0.14))))
            // at rest it only casts a shadow when it stands alone (not inside the notch or a pill with modules)
            flatRest: bar.notch || !bar.islAlone
            readonly property real cx: bar.islSlot ? bar.islSlot.bx + bar.islSlot.width / 2 : body.width / 2
            x: Math.round(Math.max(0, Math.min(body.width - width, cx - width / 2)))
            y: bar.atBottom ? bar.barH - height : 0
        }

        // ---------- the tray menu ----------
        // the cards of the Weather, System and Date modules
        BarPop {
            id: barPop
            pal: gp
            down: !bar.atBottom
            areaW: body.width
            wx: bar.wx
            st: bar.st
            sink: bar.sink
            barH: bar.barH
            y: down ? bar.barH + 8 : -height - 8
            z: 41
            // one card at a time: the island's card folds away when one of these opens, and the other way round
            onOpenChanged: if (open) { trayMenu.close(); isl.closeCards() }
        }
        TrayMenu {
            id: trayMenu
            pal: gp
            down: !bar.atBottom
            areaW: body.width
            maxH: Math.max(200, bar.implicitHeight - bar.barH - bar.gap * 2 - 16)
            y: down ? bar.barH + 8 : -height - 8
            z: 40
        }

        // ---------- the recording island: hangs under the bar while the screen is recorded ----------
        Item {
            id: recIsland
            readonly property bool on: bar.recOn && !bar.hasIsland
            width: recRow.width + 40 + (bar.notch ? 2 * 14 : 0)
            height: 46
            x: bar.notch ? body.nX + body.nW + 14 - width : body.width - width - body.edge
            y: bar.atBottom ? -height - (bar.notch ? 0 : 6) : bar.barH + (bar.notch ? bar.gap - 1 : 6)
            opacity: on ? 1 : 0
            scale: on ? 1 : 0.85
            visible: opacity > 0.01
            transformOrigin: bar.atBottom ? Item.Bottom : Item.Top
            Behavior on opacity { NumberAnimation { duration: 220 } }
            Behavior on scale { NumberAnimation { duration: 380; easing.type: Easing.OutBack; easing.overshoot: 1.4 } }
            NotchShape {
                anchors.fill: parent
                visible: bar.notch
                flip: bar.atBottom
                fill: notchBg.fill
                shadow: bar.shadowOn
            }
            Rectangle {
                anchors.fill: parent
                visible: !bar.notch
                radius: height / 2
                color: bar.bg
                border.width: 1; border.color: bar.line
            }
            Row {
                id: recRow
                anchors.centerIn: parent
                anchors.verticalCenterOffset: bar.notch ? (bar.atBottom ? 3 : -3) : 0
                spacing: 16
                // the red dot breathes while it records
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14; height: 14; radius: 7; color: "#ff4d4d"
                    SequentialAnimation on opacity { running: recIsland.on; loops: Animation.Infinite
                        NumberAnimation { to: 0.35; duration: 700 } NumberAnimation { to: 1; duration: 700 } }
                }
                Text { anchors.verticalCenter: parent.verticalCenter; text: bar.recTime; color: bar.fg
                    font.family: Fonts.numbers; font.pixelSize: 17; font.weight: Font.Medium }
                Item { width: 2; height: 1 }
                Item {
                    width: 26; height: 26; anchors.verticalCenter: parent.verticalCenter
                    MIcon { anchors.centerIn: parent; size: 19; name: bar.micMuted ? "mic_off" : "mic"
                        glyph: String.fromCodePoint(bar.micMuted ? 0xF036D : 0xF036C); color: bar.micMuted ? "#ff8a8a" : bar.fg }
                    MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor
                        onClicked: { bar.run("\"$HOME/.local/bin/rice-volume\" mic"); bar.micMuted = !bar.micMuted } }
                }
                Item {
                    width: 26; height: 26; anchors.verticalCenter: parent.verticalCenter
                    Rectangle { anchors.centerIn: parent; width: 15; height: 15; radius: 4; color: "transparent"; border.width: 2; border.color: "#ff8a8a" }
                    MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor
                        onClicked: { bar.recOn = false; bar.run("\"$HOME/.local/bin/rice-hotcorner\" record stop") } }
                }
            }
        }
    }
}
