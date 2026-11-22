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

// The DarkShadow bar on the LEFT or RIGHT edge (Settings > DarkShadow bar > Position). The same modules as the
// top bar, stacked: the layout's left zone goes on top, center in the middle, right at the bottom. The clock
// stands as hours over minutes (a click opens the island's Today card at the top of the screen, Bar.qml keeps the
// island there), the workspaces as a column of dots, the visualizer grows sideways. The styles of the top bar
// (flat, island, islands, pills, dynamic) and the launcher's logo work here too; the menus open beside the bar,
// level with their icon, and the bar slides in and out when it moves.
// (The top / bottom bar is Bar.qml; only one of the two shows.)
PanelWindow {
    id: vb
    screen: Mon.screen

    // ---------- settings (~/.config/rice/bar.json, the same file as the top bar) ----------
    property var bc: ({})
    property string cfgText: ""
    readonly property string home: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/"))
    Trigger {
        path: vb.home + "/.config/rice/bar.json"
        every: 2000
        onContent: function(t) {
            const x = t.trim()
            if (x === vb.cfgText) return
            vb.cfgText = x
            try { vb.bc = x === "" ? ({}) : JSON.parse(x) } catch (e) { }
        }
    }
    // the edge it sits on now. A move to the other edge (or a new style) plays out: it slides away, changes, slides back
    readonly property bool wantRight: bc.position === "right"
    property bool atRight: false
    readonly property bool on: bc.enabled !== false && (bc.position === "left" || bc.position === "right")
    // styles, like the top bar: flat (the whole edge), island (one pill), islands (a pill for each zone: top, middle,
    // bottom), pills (a pill for each module), dynamic (islands in the Material colors of your theme). notch has no
    // edge to grow out of standing up: islands
    readonly property string want: bc.style === "flat" || bc.style === "island" || bc.style === "pills" ? bc.style
        : (bc.style === "islands" || bc.style === "notch" ? "islands" : "dynamic")
    property string sty: "dynamic"
    readonly property bool goo: sty === "dynamic"
    readonly property bool flat: sty === "flat"
    readonly property bool zoned: sty === "islands" || sty === "dynamic"
    readonly property bool pills: sty === "pills"
    readonly property int barW: bc.height ? bc.height + 4 : 42
    readonly property real gap: flat ? 0 : 8
    readonly property real alpha: Look.flat ? 1 : (bc.opacity !== undefined ? bc.opacity : 0.72)
    NPal { id: gp; theme: "shell"; alpha: Math.min(1, vb.alpha + 0.14) }
    readonly property color fg: goo ? gp.fg : "#F6F5F4"
    readonly property color dim: goo ? gp.dim : Qt.rgba(0.965, 0.961, 0.957, 0.66)
    readonly property color bg: goo ? gp.surface : Qt.rgba(0.05, 0.05, 0.05, alpha)
    readonly property color line: goo ? "transparent" : Qt.rgba(1, 1, 1, 0.07)
    readonly property int nws: bc.workspaces ? bc.workspaces : 5
    function noClock(l) { return (l || []).map(function(m) { return m === "clock" ? "island" : m }) }
    readonly property var lay: bc.layout ? ({ top: noClock(bc.layout.left), mid: noClock(bc.layout.center), bottom: noClock(bc.layout.right) })
        : ({ top: ["launcher", "workspaces"], mid: ["island"], bottom: ["music", "tray", "volume", "bluetooth", "network", "battery", "notifications", "power"] })
    function run(cmd) { DssWin.run(["bash", "-c", cmd]) }
    // the pop-up menus open beside the bar, level with the icon that opened them: "X Y" for rice-wifi and the others
    function menuX() { return atRight ? (vb.screen ? vb.screen.width - 260 : 1660) : 260 }
    function menuAt(it) { const p = it.mapToItem(null, 0, it.height / 2); return menuX() + " " + Math.round(p.y) }

    // ---------- coming and going ----------
    // t: 0 gone .. 1 in place. It slides in from its edge when it comes (the bar moved here from the top, or was
    // turned on), and out when it goes
    property real t: 0
    readonly property real et: 1 - Math.pow(1 - t, 3)
    property bool ready: false
    Behavior on t { enabled: vb.ready && !moveAnim.running; NumberAnimation { duration: vb.on ? 460 : 220; easing.type: Easing.Linear } }
    onOnChanged: t = on ? 1 : 0
    Timer { running: true; interval: 1200; onTriggered: { vb.atRight = vb.wantRight; vb.sty = vb.want; vb.ready = true; vb.t = vb.on ? 1 : 0 } }
    onWantRightChanged: if (ready && wantRight !== atRight) moveAnim.restart()
    onWantChanged: if (ready && want !== sty) moveAnim.restart()
    SequentialAnimation {
        id: moveAnim
        NumberAnimation { target: vb; property: "t"; to: 0; duration: 200; easing.type: Easing.Linear }
        ScriptAction { script: { vb.atRight = vb.wantRight; vb.sty = vb.want } }
        PauseAnimation { duration: 40 }
        NumberAnimation { target: vb; property: "t"; to: vb.on ? 1 : 0; duration: 460; easing.type: Easing.Linear }
    }

    // ---------- the launcher logo: your distro (Nerd Font), the app grid, the black hole or the moon ----------
    readonly property var logos: ({
        tux: 0xF17C, arch: 0xF303, gentoo: 0xF30D, fedora: 0xF30A, debian: 0xF306, ubuntu: 0xF31B, nixos: 0xF313,
        cachyos: 0xF385, endeavouros: 0xF322, manjaro: 0xF312, linuxmint: 0xF30E, opensuse: 0xF314, void: 0xF32E,
        alpine: 0xF300, pop: 0xF32A, kali: 0xF327, centos: 0xF304, raspbian: 0xF315, elementary: 0xF309, zorin: 0xF32F,
        nobara: 0xF380, artix: 0xF31F, slackware: 0xF318, garuda: 0xF303, mageia: 0xF310, rocky: 0xF32B
    })
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
        running: true
        command: ["bash", "-c", ". /etc/os-release 2>/dev/null; echo \"$ID $ID_LIKE\""]
        stdout: DssCollector { onStreamFinished: vb.osIds = text.trim().toLowerCase().split(/\s+/) }
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

    // ---------- the window ----------
    visible: on || t > 0.01
    color: "transparent"
    anchors { top: true; bottom: true; left: !atRight; right: atRight }
    implicitWidth: barW + gap * 2
    exclusionMode: ExclusionMode.Normal
    exclusiveZone: on ? barW + gap * 2 : 0
    WlrLayershell.layer: WlrLayer.Top
    WlrLayershell.namespace: "rice-bar"
    // niri: blur behind every background of the bar (Hyprland: a layer rule). Only once it has slid into place
    property var pillBgs: []
    readonly property var blurItems: flat || sty === "island" ? [panel] : (pills ? pillBgs.slice(0, 8) : [zTop.bgItem, zMid.bgItem, zBot.bgItem])
    NiriBlur { win: vb; items: vb.blurItems; radius: vb.flat ? 0 : vb.barW / 2; on: vb.on && vb.t > 0.98 && !moveAnim.running }

    // the tray, without the network and Bluetooth applets when the bar shows Wi-Fi and Bluetooth itself (niri and
    // some distros start nm-applet and blueman on their own: a second, odd Wi-Fi icon)
    function hasMod(n) { const l = vb.lay || {}; return [].concat(l.top || [], l.mid || [], l.bottom || []).indexOf(n) >= 0 }
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

    // ---------- what it shows ----------
    readonly property var player: { const l = Mpris.players.values; for (let i = 0; i < l.length; i++) if (l[i].isPlaying) return l[i]; return l.length ? l[0] : null }
    readonly property var sink: Pipewire.defaultAudioSink
    PwObjectTracker { objects: [vb.sink] }
    readonly property real vol: Vol.of(sink)
    readonly property bool muted: sink && sink.audio ? sink.audio.muted : false
    readonly property var batt: UPower.displayDevice
    readonly property bool hasBatt: batt && batt.isPresent && batt.isLaptopBattery
    readonly property real battPct: batt ? Math.round(batt.percentage * 100) : 0
    readonly property bool charging: batt && (batt.state === UPowerDeviceState.Charging || batt.state === UPowerDeviceState.FullyCharged)
    readonly property bool plugged: hasBatt && !UPower.onBattery
    readonly property var svc: BarSvc.state
    Binding { target: BarSvc; property: "sideShown"; value: vb.on }
    property var now: new Date()
    Timer { interval: 15000; running: vb.on; repeat: true; triggeredOnStart: true; onTriggered: vb.now = new Date() }
    property bool h12: false
    Trigger { path: vb.home + "/.config/rice/region.json"; every: 5000; onContent: function(t) { vb.h12 = /"clock": *"12"/.test(t) } }
    function two(n) { return n < 10 ? "0" + n : "" + n }

    // (declared before it is used: an older Qt cannot see a component further down the file)
    // a zone of the bar (top, middle, bottom): its modules in a column; islands / dynamic: in a pill of its own,
    // pills: every module in its own pill
    component Zone: Item {
        id: z
        property var mods: []
        readonly property alias bgItem: zbg
        width: vb.barW
        height: mods.length === 0 ? 0 : zc.implicitHeight + (vb.zoned ? 16 : 0)
        Rectangle {
            id: zbg
            anchors.fill: parent
            visible: vb.zoned && z.mods.length > 0
            radius: vb.barW / 2
            color: vb.bg
            border.width: vb.goo ? 0 : 1
            border.color: vb.line
        }
        Column {
            id: zc
            anchors.horizontalCenter: parent.horizontalCenter
            y: vb.zoned ? 8 : 0
            spacing: vb.pills ? 8 : 6
            Repeater {
                model: z.mods
                delegate: Item {
                    id: slot
                    readonly property bool shows: ld.item !== null && vb.modShows(modelData)
                    visible: shows
                    width: vb.barW
                    height: !shows ? 0 : (vb.pills ? Math.max(vb.barW, ld.item.height + 12) : ld.item.height)
                    Rectangle {
                        id: pbg
                        anchors.fill: parent
                        visible: vb.pills
                        radius: vb.barW / 2
                        color: vb.bg
                        border.width: 1
                        border.color: vb.line
                        Component.onCompleted: vb.pillBgs = vb.pillBgs.concat([pbg])
                        Component.onDestruction: vb.pillBgs = vb.pillBgs.filter(function(p) { return p !== pbg })
                    }
                    Loader { id: ld; anchors.centerIn: parent; sourceComponent: vb.compFor(modelData) }
                }
            }
        }
    }

    // ---------- the panel ----------
    Item {
        id: content
        x: vb.flat ? 0 : vb.gap; y: vb.flat ? 0 : vb.gap
        width: vb.barW; height: parent.height - (vb.flat ? 0 : vb.gap * 2)
        opacity: Math.min(1, vb.et * 1.4)
        transform: Translate { x: (1 - vb.et) * (vb.atRight ? 1 : -1) * (vb.barW + vb.gap * 2 + 12) }

        // island / flat: one background behind everything
        Rectangle {
            id: panel
            anchors.fill: parent
            visible: vb.flat || vb.sty === "island"
            radius: vb.flat ? 0 : vb.barW / 2
            color: vb.bg
            border.width: vb.flat ? 0 : 1
            border.color: vb.line
            Rectangle { visible: vb.flat; width: 1; height: parent.height; color: Qt.rgba(1, 1, 1, 0.09)
                x: vb.atRight ? 0 : parent.width - 1 }
        }
        Zone { id: zTop; mods: vb.lay.top || []; y: vb.flat ? 10 : (vb.zoned || vb.pills ? 0 : 10) }
        Zone { id: zMid; mods: vb.lay.mid || []; y: (parent.height - height) / 2 }
        Zone { id: zBot; mods: vb.lay.bottom || []; y: parent.height - height - (vb.zoned || vb.pills ? 0 : 10) }
    }

    // a module with nothing to show right now takes no room (music without a player, a battery on a desktop)
    function modShows(n) { return n === "media" ? player !== null : (n === "battery" ? hasBatt : true) }
    function compFor(n) {
        switch (n) {
        case "launcher": return cLauncher
        case "workspaces": return cWs
        case "island": case "clock": return cClock
        case "media": return cMedia
        case "tray": return cTray
        case "volume": return cVolume
        case "network": return cNet
        case "bluetooth": return cBt
        case "battery": return cBatt
        case "notifications": return cNotif
        case "power": return cPower
        case "visualizer": return cViz
        case "weather": return cWeather
        case "sysmon": return cSysmon
        case "date": return cDate
        case "music": return cMusic
        }
        return null                     // the window title has no room standing up
    }

    // one round icon button
    component Btn: Item {
        id: b
        property string icon: ""
        property string glyph: ""
        property color tint: vb.fg
        property real isz: 19
        signal hit(var mouse)
        signal wheel(var e)
        width: vb.barW - 10; height: width
        Rectangle { anchors.fill: parent; radius: width / 2; color: vb.fg; opacity: bm.containsMouse ? 0.1 : 0
            Behavior on opacity { NumberAnimation { duration: 140 } } }
        MIcon { anchors.centerIn: parent; size: b.isz; name: b.icon; glyph: b.glyph; color: b.tint }
        MouseArea { id: bm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: function(m) { Sfx.play("click"); b.hit(m) }
            onWheel: function(e) { b.wheel(e) } }
    }

    Component { id: cLauncher
        Item { width: vb.barW - 6; height: width
            readonly property bool ink: vb.goo
            Rectangle { anchors.fill: parent; radius: lm.pressed ? width * 0.3 : width / 2; visible: vb.goo
                color: gp.mono ? gp.container : gp.primary; scale: lm.containsMouse ? 1.06 : 1
                Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } } }
            BlackHoleMark { visible: vb.logoKey === "blackhole"; anchors.centerIn: parent; size: parent.width; extras: false
                readonly property var pl: vb.logoPal("blackhole"); hot: pl[0]; warm: pl[1]; cool: pl[2]; glowColor: pl[3]
                running: visible && !Look.lite && (bc.logo_move === true) }
            MoonMark { visible: vb.logoKey === "moon"; anchors.centerIn: parent; size: parent.width - 8; extras: false
                readonly property var pl: vb.logoPal("moon"); moonTop: pl[0]; moonBottom: pl[1]; glowColor: pl[3]
                running: visible && !Look.lite && (bc.logo_move === true) }
            MIcon { visible: vb.logoKey === "apps"; anchors.centerIn: parent; name: "apps"; glyph: String.fromCodePoint(0xF003B); size: 18
                color: vb.goo ? (gp.mono ? gp.inkContainer : gp.inkPrimary) : vb.fg }
            // your distro's logo (Settings > DarkShadow bar > Launcher icon: auto)
            Text { visible: vb.logoKey !== "apps" && vb.logoKey !== "blackhole" && vb.logoKey !== "moon"; anchors.centerIn: parent; anchors.horizontalCenterOffset: 1
                text: vb.logos[vb.logoKey] !== undefined ? String.fromCodePoint(vb.logos[vb.logoKey]) : ""
                font.family: Fonts.icons; font.pixelSize: 18; color: vb.goo ? (gp.mono ? gp.inkContainer : gp.inkPrimary) : vb.fg
                scale: lm.containsMouse ? 1.12 : 1; Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } } }
            MouseArea { id: lm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: { Sfx.play("click"); vb.run("\"$HOME/.local/bin/rice-launch\"") } }
        }
    }

    Component { id: cWs
        Column {
            spacing: 4
            Repeater {
                model: Wm.maxWs(vb.nws)
                delegate: Item {
                    readonly property int wid: index + 1
                    readonly property bool act: Wm.focusedId === wid
                    readonly property bool occ: Wm.occupied(wid)
                    width: vb.barW - 10; height: act ? 34 : 16
                    Behavior on height { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                    Rectangle {
                        anchors.centerIn: parent
                        width: act ? 10 : (occ ? 9 : 6); height: act ? parent.height - 6 : width; radius: width / 2
                        color: act ? (vb.goo ? gp.primary : vb.fg) : (occ ? vb.fg : vb.dim)
                        opacity: act ? 1 : (occ ? 0.75 : 0.35)
                        Behavior on height { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Wm.goWs(wid) }
                }
            }
            // the wheel over the dots: one workspace per notch
            property real sum: 0
            WheelHandler { onWheel: function(e) { parent.sum += e.angleDelta.y; while (Math.abs(parent.sum) >= 120) { Wm.goWs(parent.sum > 0 ? "e-1" : "e+1"); parent.sum -= (parent.sum > 0 ? 120 : -120) } } }
        }
    }

    Component { id: cClock
        Rectangle {
            width: vb.barW - 8; height: cc.implicitHeight + 16; radius: width / 2
            color: vb.goo ? gp.container : "transparent"
            Column { id: cc; anchors.centerIn: parent; spacing: -2
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: vb.h12 ? String((vb.now.getHours() + 11) % 12 + 1) : vb.two(vb.now.getHours())
                    color: vb.goo ? gp.inkContainer : vb.fg; font.family: Fonts.main; font.pixelSize: 16; font.weight: Font.Bold }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: vb.two(vb.now.getMinutes())
                    color: vb.goo ? gp.inkContainer : vb.fg; opacity: 0.8; font.family: Fonts.main; font.pixelSize: 16; font.weight: Font.Medium }
                Text { anchors.horizontalCenter: parent.horizontalCenter; topPadding: 4; text: vb.now.getDate() + "." + vb.two(vb.now.getMonth() + 1)
                    color: vb.goo ? gp.inkContainer : vb.dim; opacity: 0.7; font.family: Fonts.main; font.pixelSize: 10 }
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); vb.run("\"$HOME/.local/bin/rice-dashboard\" open") } }
        }
    }

    Component { id: cMedia
        Btn { visible: vb.player !== null; icon: vb.player && vb.player.isPlaying ? "pause" : "music_note"; glyph: String.fromCodePoint(vb.player && vb.player.isPlaying ? 0xF03E4 : 0xF075A)
            onHit: function(m) { if (vb.player) vb.player.togglePlaying() } }
    }

    Component { id: cTray
        Column { spacing: 6
            Repeater {
                model: vb.trayItems
                delegate: Item {
                    id: ti
                    width: vb.barW - 10; height: 24
                    IconImage { anchors.centerIn: parent; implicitSize: Math.min(20, vb.bc.tray_size ? vb.bc.tray_size : 18); source: Icons.src(modelData.icon)
                        scale: tm.containsMouse ? 1.15 : 1; Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } } }
                    MouseArea {
                        id: tm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                        onClicked: function(m) {
                            if ((m.button === Qt.RightButton || modelData.onlyMenu) && modelData.hasMenu) {
                                const p = ti.mapToItem(null, vb.atRight ? -4 : ti.width + 4, 0)
                                modelData.display(vb, p.x, p.y)
                            } else if (m.button === Qt.MiddleButton) modelData.secondaryActivate()
                            else modelData.activate()
                        }
                    }
                }
            }
        }
    }

    Component { id: cVolume
        Btn { id: vbtn; icon: vb.muted ? "volume_off" : (vb.vol < 0.34 ? "volume_mute" : (vb.vol < 0.67 ? "volume_down" : "volume_up"))
            glyph: String.fromCodePoint(vb.muted ? 0xF0581 : (vb.vol < 0.34 ? 0xF057F : (vb.vol < 0.67 ? 0xF0580 : 0xF057E)))
            tint: vb.muted ? vb.dim : vb.fg; isz: 20
            onHit: function(m) {
                if (m.button === Qt.LeftButton) vb.run("\"$HOME/.local/bin/rice-volume\" menu " + vb.menuAt(vbtn))
                else if (vb.sink && vb.sink.audio) vb.sink.audio.muted = !vb.sink.audio.muted
            }
            onWheel: function(e) { Vol.nudge(vb.sink, Look.notches(e) * 0.04) } }
    }
    Component { id: cNet
        Btn { id: nbtn; icon: vb.svc.network ? "wifi" : "wifi_off"; glyph: String.fromCodePoint(vb.svc.network ? 0xF05A9 : 0xF05AA); tint: vb.svc.network ? vb.fg : vb.dim
            onHit: function(m) { vb.run("\"$HOME/.local/bin/rice-wifi\" " + vb.menuAt(nbtn)) } }
    }
    Component { id: cBt
        Btn { id: bbtn; icon: vb.svc.bluetooth ? "bluetooth" : "bluetooth_disabled"; glyph: String.fromCodePoint(vb.svc.bluetooth ? 0xF00AF : 0xF00B2); tint: vb.svc.bluetooth ? vb.fg : vb.dim
            onHit: function(m) { vb.run("\"$HOME/.local/bin/rice-bluetooth\" " + vb.menuAt(bbtn)) } }
    }
    Component { id: cBatt
        Column { visible: vb.hasBatt; spacing: 1
            MIcon { anchors.horizontalCenter: parent.horizontalCenter; size: 17; color: !vb.charging && vb.battPct <= 15 ? "#ff6b6b" : vb.fg
                name: vb.charging ? "battery_charging_full" : "battery_full"; glyph: String.fromCodePoint(vb.charging ? 0xF0084 : 0xF0079) }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: vb.battPct; color: vb.dim; font.family: Fonts.main; font.pixelSize: 10 }
        }
    }
    Component { id: cNotif
        Btn { icon: vb.svc.dnd ? "notifications_off" : "notifications"; glyph: String.fromCodePoint(vb.svc.dnd ? 0xF009B : 0xF009A); tint: vb.svc.dnd ? vb.dim : vb.fg
            onHit: function(m) { vb.run(m.button === Qt.RightButton ? "\"$HOME/.local/bin/rice-cc\" dnd toggle" : "\"$HOME/.local/bin/rice-cc\" toggle"); BarSvc.refreshSoon() } }
    }
    Component { id: cPower
        Btn { id: pbtn; icon: "power_settings_new"; glyph: String.fromCodePoint(0xF0425); tint: "#ff8a80"
            onHit: function(m) { vb.run("\"$HOME/.local/bin/rice-power\" " + vb.menuAt(pbtn)) } }
    }
    // Weather, System, Date: their cards open at the top of the screen, beside this bar (Bar.qml shows them)
    function pop(k) { run("mkdir -p \"$HOME/.cache/rice\"; printf '%s %s %s\\n' \"$(date +%s.%N)\" " + k + " " + (atRight ? (vb.screen ? vb.screen.width - 420 : 1500) : 230) + " > \"$HOME/.cache/rice/barpop-open\"") }
    property var wx: null
    Trigger { path: vb.home + "/.cache/rice/weather.json"; every: 60000; onContent: function(t) { try { vb.wx = JSON.parse(t) } catch (e) { } } }
    Component { id: cWeather
        Item { width: vb.barW - 10; height: wc.implicitHeight + 6
            Column { id: wc; anchors.centerIn: parent; spacing: 0
                WxIcon { anchors.horizontalCenter: parent.horizontalCenter; size: 18; cond: vb.wx ? vb.wx.cond : ""; color: vb.goo ? gp.primary : vb.fg }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: vb.wx ? vb.wx.temp + "°" : "--"; color: vb.fg; font.family: Fonts.main; font.pixelSize: 11 } }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); vb.pop("weather") } }
        }
    }
    Component { id: cSysmon
        Btn { icon: "monitoring"; glyph: String.fromCodePoint(0xF0128); onHit: function(m) { vb.pop("system") } }
    }
    Component { id: cMusic
        Btn { icon: "music_note"; glyph: String.fromCodePoint(0xF075A); onHit: function(m) { vb.pop("music") } }
    }
    Component { id: cDate
        Btn { icon: "calendar_month"; glyph: String.fromCodePoint(0xF00F0); onHit: function(m) { vb.pop("calendar") } }
    }
    Component { id: cViz
        BarViz { vertical: true; count: 10; thick: vb.barW - 18; active: vb.on; color: vb.goo ? gp.primary : vb.fg }
    }
}
