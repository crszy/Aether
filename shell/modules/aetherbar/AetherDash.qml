// Aether shell (AGPL-3.0-or-later). The Aether dashboard: drops down under the Aether bar (the logo, the clock,
// the song or the system monitor open it). Tabs: Home (clock, calendar, weather, now playing and quick toggles,
// after Caelestia's and serpantinum's dashboards), Music (big cover, seek, volume), Lyrics (the song as a text
// conversation: each line a chat bubble that pops in, zooms while it is sung and shrinks away after, with a typing
// bubble in the gaps and a sparkle when a line lands), Wallpapers (your wallpaper folder) and System.
// Lyrics come from Caelestia's Lyrics service, wallpapers from the Wallpapers service; the code is Aether's own.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import Quickshell
import Quickshell.Wayland
import Quickshell.Widgets
import Quickshell.Windows as Win
import Caelestia.Config
import Caelestia.Services
import Caelestia.State
import qs.services
import qs.utils

Scope {
    id: dash

    readonly property var cfg: AetherBarConfig
    readonly property bool open: cfg.dashOpen
    readonly property var player: Players.active
    readonly property string font: Tokens.font.body.small.family
    readonly property var tabs: [
        { id: "home", name: "Home", icon: "home" },
        { id: "music", name: "Music", icon: "album" },
        { id: "lyrics", name: "Lyrics", icon: "lyrics" },
        { id: "walls", name: "Wallpapers", icon: "wallpaper" },
        { id: "system", name: "System", icon: "monitoring" }
    ]
    readonly property int tabIdx: Math.max(0, tabs.findIndex(x => x.id === cfg.dashTab))

    property real t: 0
    onOpenChanged: t = open ? 1 : 0
    Behavior on t {
        NumberAnimation {
            duration: dash.open ? 460 : 200
            easing.type: dash.open ? Easing.OutBack : Easing.InCubic
            easing.overshoot: 1.1
        }
    }

    // keep the song's position fresh while the dashboard is open (Windows' media session only reports on change)
    Timer {
        running: dash.open && !!dash.player && dash.player.isPlaying
        interval: dash.cfg.dashTab === "lyrics" ? 100 : 200
        repeat: true
        triggeredOnStart: true
        onTriggered: dash.player && dash.player.positionChanged()
    }
    // lyrics follow the song
    readonly property var _lyr: {
        const p = Players.active;
        if (p)
            Lyrics.setTrack(p.trackArtist, p.trackTitle, p.trackAlbum, p.length);
        else
            Lyrics.clearTrack();
        return 0;
    }

    // CPU / memory while open
    property int cpu: 0
    property int ram: 0
    property real ramUsedGb: 0
    property real ramTotalGb: 0
    property var lastStat: null
    Timer {
        running: dash.open
        interval: 1500
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            const s = Win.FsUtils.systemStats();
            if (!s || !s.total)
                return;
            if (dash.lastStat) {
                const busy = (s.kernel - dash.lastStat.kernel) + (s.user - dash.lastStat.user);
                const idle = s.idle - dash.lastStat.idle;
                if (busy > 0)
                    dash.cpu = Math.max(0, Math.min(100, Math.round((1 - idle / busy) * 100)));
            }
            dash.lastStat = { idle: s.idle, kernel: s.kernel, user: s.user };
            dash.ram = Math.round((1 - s.available / s.total) * 100);
            dash.ramTotalGb = s.total / 1073741824;
            dash.ramUsedGb = (s.total - s.available) / 1073741824;
        }
    }

    function normPath(p) {
        return Wallpapers.norm(String(p || "").replace(/^file:\/+/, ""));
    }

    function fmt(sec) {
        if (!isFinite(sec) || sec < 0)
            sec = 0;
        const m = Math.floor(sec / 60);
        const s = Math.floor(sec % 60);
        return m + ":" + (s < 10 ? "0" : "") + s;
    }

    component Sym: Text {
        property real size: 20
        font.family: "Material Symbols Rounded"
        font.pixelSize: size
        color: AetherStyle.textMain
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
    }
    component Txt: Text {
        font.family: dash.font
        font.pixelSize: 13
        color: AetherStyle.textMain
        elide: Text.ElideRight
    }
    component Card: Rectangle {
        radius: 22
        color: Qt.alpha(AetherStyle.palette.m3surfaceContainerHigh, 0.75)
        border.width: 1
        border.color: Qt.alpha(AetherStyle.outlineVar, 0.35)
    }
    // a round icon button that squashes when pressed
    component RoundBtn: Rectangle {
        id: rb
        property string icon: ""
        property real size: 40
        property bool on: false
        property bool big: false
        signal clicked()
        width: size
        height: size
        radius: size / 2
        color: on || big ? AetherStyle.primary : rbm.containsMouse ? Qt.alpha(AetherStyle.textMain, 0.12) : Qt.alpha(AetherStyle.textMain, 0.06)
        Behavior on color { ColorAnimation { duration: 150 } }
        scale: rbm.pressed ? 0.88 : rbm.containsMouse ? 1.06 : 1
        Behavior on scale { NumberAnimation { duration: 240; easing.type: Easing.OutBack; easing.overshoot: 2.2 } }
        Sym {
            anchors.centerIn: parent
            text: rb.icon
            size: rb.size * 0.5
            color: rb.on || rb.big ? AetherStyle.palette.m3onPrimary : AetherStyle.textMain
        }
        MouseArea {
            id: rbm
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: rb.clicked()
        }
    }
    // a thin bar you can drag (seek, volume)
    component Track: Item {
        id: tr
        property real value: 0      // 0..1
        property bool live: true
        signal moved(real v)
        height: 18
        readonly property real shown: ma.pressed ? ma.v : value
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: ma.containsMouse || ma.pressed ? 8 : 5
            radius: height / 2
            color: Qt.alpha(AetherStyle.textMain, 0.14)
            Behavior on height { NumberAnimation { duration: 160 } }
            Rectangle {
                width: Math.max(parent.height, parent.width * Math.max(0, Math.min(1, tr.shown)))
                height: parent.height
                radius: height / 2
                color: AetherStyle.primary
                Behavior on width { enabled: !ma.pressed; NumberAnimation { duration: 200 } }
            }
        }
        MouseArea {
            id: ma
            property real v: 0
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onPressed: m => v = Math.max(0, Math.min(1, m.x / width))
            onPositionChanged: m => {
                if (pressed)
                    v = Math.max(0, Math.min(1, m.x / width));
            }
            onReleased: tr.moved(v)
        }
    }
    // the cover art, round-cornered, with a music note when there is none
    component Cover: ClippingRectangle {
        id: cov
        property real size: 64
        width: size
        height: size
        radius: size * 0.22
        color: AetherStyle.primaryContainer
        Sym {
            anchors.centerIn: parent
            text: "music_note"
            size: cov.size * 0.45
            color: AetherStyle.textOnPrimaryContainer
            visible: art.status !== Image.Ready
        }
        Image {
            id: art
            anchors.fill: parent
            source: dash.player ? (dash.player.trackArtUrl || "") : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            sourceSize.width: 512
        }
    }

    PanelWindow {
        id: win
        screen: dash.cfg.dashScreen || Quickshell.screens[0]
        visible: dash.open || dash.t > 0.01
        color: "transparent"
        anchors { top: true; left: true; right: true; bottom: true }
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.namespace: "aether-dash"
        WlrLayershell.keyboardFocus: dash.open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None

        // a light dim; clicking it closes
        Rectangle {
            anchors.fill: parent
            color: "#000000"
            opacity: 0.28 * Math.max(0, Math.min(1, dash.t))
            MouseArea {
                anchors.fill: parent
                onClicked: dash.cfg.dashOpen = false
            }
        }

        Item {
            anchors.fill: parent
            focus: true
            Keys.onEscapePressed: dash.cfg.dashOpen = false
            Keys.onLeftPressed: dash.cfg.dashTab = dash.tabs[(dash.tabIdx + dash.tabs.length - 1) % dash.tabs.length].id
            Keys.onRightPressed: dash.cfg.dashTab = dash.tabs[(dash.tabIdx + 1) % dash.tabs.length].id
            Keys.onSpacePressed: dash.player && dash.player.togglePlaying()
        }

        Rectangle {
            id: card
            width: Math.min(940, win.width - 40)
            height: Math.min(620, win.height - y - 30)
            x: (win.width - width) / 2
            y: dash.cfg.barHeight + dash.cfg.gap + 14 - (1 - dash.t) * 40
            radius: 30
            color: Qt.alpha(AetherStyle.surface, 0.94)
            border.width: 1
            border.color: Qt.alpha(AetherStyle.outlineVar, 0.5)
            opacity: Math.max(0, Math.min(1, dash.t * 1.6))
            scale: 0.92 + 0.08 * dash.t
            transformOrigin: Item.Top
            clip: true
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#000000"
                shadowOpacity: 0.45
                shadowBlur: 1
                shadowVerticalOffset: 8
            }
            MouseArea { anchors.fill: parent }   // clicks inside never close it

            // ---- tab strip: a pill slides to the tab you pick ----
            Item {
                id: strip
                x: 18
                y: 16
                width: parent.width - 36
                height: 44
                readonly property real tw: Math.min(150, (width - 50) / dash.tabs.length)
                Rectangle {
                    id: pill
                    width: strip.tw
                    height: strip.height
                    radius: height / 2
                    color: AetherStyle.primaryContainer
                    x: dash.tabIdx * strip.tw
                    Behavior on x { NumberAnimation { duration: 420; easing.type: Easing.OutBack; easing.overshoot: 1.4 } }
                }
                Row {
                    Repeater {
                        model: dash.tabs
                        Item {
                            id: tab
                            required property var modelData
                            required property int index
                            readonly property bool sel: index === dash.tabIdx
                            width: strip.tw
                            height: strip.height
                            Row {
                                anchors.centerIn: parent
                                spacing: 7
                                scale: tm.pressed ? 0.92 : 1
                                Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
                                Sym {
                                    text: tab.modelData.icon
                                    size: 19
                                    color: tab.sel ? AetherStyle.textOnPrimaryContainer : AetherStyle.textSub
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Txt {
                                    text: tab.modelData.name
                                    font.pixelSize: 13
                                    font.weight: tab.sel ? Font.DemiBold : Font.Medium
                                    color: tab.sel ? AetherStyle.textOnPrimaryContainer : AetherStyle.textSub
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            MouseArea {
                                id: tm
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: dash.cfg.dashTab = tab.modelData.id
                            }
                        }
                    }
                }
                RoundBtn {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "close"
                    size: 36
                    onClicked: dash.cfg.dashOpen = false
                }
            }

            // ---- the page: slides in from the side of the tab you came from ----
            Item {
                id: host
                x: 18
                y: strip.y + strip.height + 14
                width: parent.width - 36
                height: parent.height - y - 18
                property int lastIdx: 0
                property real dir: 1
                Connections {
                    target: dash
                    function onTabIdxChanged() {
                        host.dir = dash.tabIdx >= host.lastIdx ? 1 : -1;
                        host.lastIdx = dash.tabIdx;
                        enter.restart();
                    }
                }
                ParallelAnimation {
                    id: enter
                    NumberAnimation { target: pageLoader; property: "opacity"; from: 0; to: 1; duration: 260; easing.type: Easing.OutCubic }
                    NumberAnimation { target: slideT; property: "x"; from: 40 * host.dir; to: 0; duration: 420; easing.type: Easing.OutCubic }
                    NumberAnimation { target: pageLoader; property: "scale"; from: 0.97; to: 1; duration: 420; easing.type: Easing.OutBack }
                }
                Loader {
                    id: pageLoader
                    anchors.fill: parent
                    active: win.visible
                    transform: Translate { id: slideT }
                    sourceComponent: dash.cfg.dashTab === "music" ? musicC : dash.cfg.dashTab === "lyrics" ? lyricsC : dash.cfg.dashTab === "walls" ? wallsC : dash.cfg.dashTab === "system" ? systemC : homeC
                }
            }
        }
    }

    // ===================================== Home =====================================
    Component {
        id: homeC
        Item {
            id: home
            // left: clock, date, weather, calendar
            Column {
                id: left
                width: Math.round(parent.width * 0.42)
                height: parent.height
                spacing: 12
                Card {
                    width: parent.width
                    height: 150
                    Column {
                        x: 22
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Txt {
                            text: Time.format(dash.cfg.clock24h ? "hh:mm" : "h:mm")
                            font.pixelSize: 58
                            font.weight: Font.Bold
                            color: AetherStyle.primary
                        }
                        Txt {
                            text: Time.format("dddd, d MMMM")
                            font.pixelSize: 15
                            color: AetherStyle.textSub
                        }
                    }
                    Column {
                        anchors.right: parent.right
                        anchors.rightMargin: 22
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !!Weather.cc
                        spacing: 2
                        Sym { anchors.horizontalCenter: parent.horizontalCenter; text: Weather.icon; size: 40; color: AetherStyle.primary }
                        Txt { anchors.horizontalCenter: parent.horizontalCenter; text: Weather.temp; font.pixelSize: 18; font.weight: Font.DemiBold }
                        Txt { anchors.horizontalCenter: parent.horizontalCenter; text: Weather.description; font.pixelSize: 11; color: AetherStyle.textSub; width: Math.min(implicitWidth, 120) }
                    }
                }
                // calendar (this month; today is filled)
                Card {
                    id: cal
                    width: parent.width
                    height: parent.height - 162
                    property int offset: 0
                    readonly property date base: { const d = new Date(Time.date.getFullYear(), Time.date.getMonth() + offset, 1); return d; }
                    readonly property int first: (base.getDay() + 6) % 7
                    readonly property int days: new Date(base.getFullYear(), base.getMonth() + 1, 0).getDate()
                    Row {
                        x: 18
                        y: 14
                        width: parent.width - 36
                        Txt {
                            width: parent.width - 72
                            text: Qt.formatDate(cal.base, "MMMM yyyy")
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        RoundBtn { icon: "chevron_left"; size: 30; onClicked: cal.offset-- }
                        RoundBtn { icon: "chevron_right"; size: 30; onClicked: cal.offset++ }
                    }
                    Grid {
                        id: cg
                        x: 14
                        y: 54
                        columns: 7
                        readonly property real cw: (cal.width - 28) / 7
                        readonly property real ch: Math.min(cw, (cal.height - 66) / 7)
                        Repeater {
                            model: ["M", "T", "W", "T", "F", "S", "S"]
                            Txt {
                                required property string modelData
                                width: cg.cw
                                height: cg.ch
                                text: modelData
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: AetherStyle.textSub
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                        }
                        Repeater {
                            model: 42
                            Item {
                                id: dc
                                required property int index
                                readonly property int day: index - cal.first + 1
                                readonly property bool inMonth: day >= 1 && day <= cal.days
                                readonly property bool today: cal.offset === 0 && day === Time.date.getDate()
                                width: cg.cw
                                height: cg.ch
                                visible: index < Math.ceil((cal.first + cal.days) / 7) * 7
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: Math.min(parent.width, parent.height) - 4
                                    height: width
                                    radius: width / 2
                                    color: dc.today ? AetherStyle.primary : "transparent"
                                }
                                Txt {
                                    anchors.centerIn: parent
                                    text: dc.inMonth ? dc.day : ""
                                    font.pixelSize: 12
                                    font.weight: dc.today ? Font.Bold : Font.Normal
                                    color: dc.today ? AetherStyle.palette.m3onPrimary : AetherStyle.textMain
                                }
                            }
                        }
                    }
                }
            }
            // right: now playing, quick toggles, system
            Column {
                x: left.width + 12
                width: parent.width - x
                height: parent.height
                spacing: 12
                Card {
                    width: parent.width
                    height: 170
                    clip: true
                    // the cover, blurred, behind
                    Image {
                        anchors.fill: parent
                        source: dash.player ? (dash.player.trackArtUrl || "") : ""
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: 256
                        opacity: 0.35
                        visible: status === Image.Ready
                        layer.enabled: true
                        layer.effect: MultiEffect { blurEnabled: true; blur: 1; blurMax: 48 }
                    }
                    Cover {
                        id: hc
                        x: 18
                        anchors.verticalCenter: parent.verticalCenter
                        size: 128
                    }
                    Column {
                        x: hc.x + hc.width + 18
                        width: parent.width - x - 18
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Txt { width: parent.width; text: dash.player ? (dash.player.trackTitle || "Unknown song") : "Nothing playing"; font.pixelSize: 18; font.weight: Font.DemiBold }
                        Txt { width: parent.width; text: dash.player ? (dash.player.trackArtist || "") : "Play something and it shows here"; color: AetherStyle.textSub }
                        Item { width: 1; height: 8 }
                        Row {
                            spacing: 10
                            visible: !!dash.player
                            RoundBtn { icon: "skip_previous"; onClicked: dash.player.previous() }
                            RoundBtn { icon: dash.player && dash.player.isPlaying ? "pause" : "play_arrow"; big: true; size: 46; onClicked: dash.player.togglePlaying() }
                            RoundBtn { icon: "skip_next"; onClicked: dash.player.next() }
                            RoundBtn { icon: "lyrics"; onClicked: dash.cfg.dashTab = "lyrics" }
                        }
                    }
                }
                // quick toggles
                Grid {
                    id: qt
                    width: parent.width
                    columns: 4
                    spacing: 10
                    readonly property real cw: (width - 30) / 4
                    Repeater {
                        model: [
                            { icon: PersistentState.doNotDisturb ? "notifications_off" : "notifications", name: "Do not disturb", on: PersistentState.doNotDisturb, run: () => PersistentState.doNotDisturb = !PersistentState.doNotDisturb },
                            { icon: Audio.muted ? "volume_off" : "volume_up", name: Audio.muted ? "Muted" : Math.round(Audio.volume * 100) + "%", on: Audio.muted, run: () => { if (Audio.sink && Audio.sink.audio) Audio.sink.audio.muted = !Audio.muted; } },
                            { icon: "shuffle", name: "Random wallpaper", on: false, run: () => Wallpapers.setRandom() },
                            { icon: "settings", name: "Aether settings", on: false, run: () => { dash.cfg.dashOpen = false; WinSettings.ipc("aether", "open", ["aetherbar"]); } }
                        ]
                        Rectangle {
                            id: qtile
                            required property var modelData
                            width: qt.cw
                            height: 78
                            radius: 20
                            color: modelData.on ? AetherStyle.primary : qm.containsMouse ? Qt.alpha(AetherStyle.palette.m3surfaceContainerHigh, 1) : Qt.alpha(AetherStyle.palette.m3surfaceContainerHigh, 0.75)
                            Behavior on color { ColorAnimation { duration: 180 } }
                            scale: qm.pressed ? 0.93 : 1
                            Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                            Column {
                                anchors.centerIn: parent
                                spacing: 4
                                Sym { anchors.horizontalCenter: parent.horizontalCenter; text: qtile.modelData.icon; size: 24; color: qtile.modelData.on ? AetherStyle.palette.m3onPrimary : AetherStyle.textMain }
                                Txt { anchors.horizontalCenter: parent.horizontalCenter; text: qtile.modelData.name; font.pixelSize: 11; width: Math.min(implicitWidth, qtile.width - 12); color: qtile.modelData.on ? AetherStyle.palette.m3onPrimary : AetherStyle.textSub }
                            }
                            MouseArea {
                                id: qm
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: qtile.modelData.run()
                            }
                        }
                    }
                }
                // system at a glance
                Card {
                    width: parent.width
                    height: parent.height - 170 - 78 - 24
                    Column {
                        x: 20
                        width: parent.width - 40
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 14
                        Repeater {
                            model: [
                                { icon: "memory", name: "CPU", v: dash.cpu / 100, label: dash.cpu + "%" },
                                { icon: "memory_alt", name: "Memory", v: dash.ram / 100, label: dash.ramUsedGb.toFixed(1) + " / " + dash.ramTotalGb.toFixed(0) + " GB" },
                                { icon: Icons.getVolumeIcon(Audio.volume, Audio.muted), name: "Volume", v: Audio.volume, label: Math.round(Audio.volume * 100) + "%", vol: true }
                            ]
                            Row {
                                id: sr
                                required property var modelData
                                width: parent.width
                                spacing: 12
                                Sym { text: sr.modelData.icon; size: 20; anchors.verticalCenter: parent.verticalCenter }
                                Txt { text: sr.modelData.name; width: 64; anchors.verticalCenter: parent.verticalCenter }
                                Track {
                                    width: parent.width - 32 - 64 - 120 - 36
                                    anchors.verticalCenter: parent.verticalCenter
                                    value: sr.modelData.v
                                    enabled: !!sr.modelData.vol
                                    onMoved: v => { if (Audio.sink && Audio.sink.audio) Audio.sink.audio.volume = v; }
                                }
                                Txt { text: sr.modelData.label; width: 120; horizontalAlignment: Text.AlignRight; color: AetherStyle.textSub; anchors.verticalCenter: parent.verticalCenter }
                            }
                        }
                    }
                }
            }
        }
    }

    // ===================================== Music =====================================
    Component {
        id: musicC
        Item {
            id: mus
            clip: true
            readonly property real pos: dash.player ? dash.player.position : 0
            readonly property real len: dash.player ? dash.player.length : 0
            // the cover, huge and blurred, slowly breathing behind everything
            Image {
                id: bg
                anchors.centerIn: parent
                width: parent.width
                height: parent.height
                source: dash.player ? (dash.player.trackArtUrl || "") : ""
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 400
                opacity: status === Image.Ready ? 0.5 : 0
                Behavior on opacity { NumberAnimation { duration: 600 } }
                layer.enabled: true
                layer.effect: MultiEffect { blurEnabled: true; blur: 1; blurMax: 64; saturation: 0.3 }
                SequentialAnimation on scale {
                    running: dash.open && !!dash.player && dash.player.isPlaying
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.1; to: 1.25; duration: 9000; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 1.25; to: 1.1; duration: 9000; easing.type: Easing.InOutSine }
                }
            }
            Rectangle { anchors.fill: parent; radius: 22; color: Qt.alpha(AetherStyle.surface, 0.35) }

            Cover {
                id: big
                x: 34
                anchors.verticalCenter: parent.verticalCenter
                size: Math.min(parent.height - 60, 360)
                // a little pulse with the music
                scale: dash.player && dash.player.isPlaying ? beat : 0.94
                property real beat: 1
                Behavior on scale { NumberAnimation { duration: 500; easing.type: Easing.OutBack } }
                SequentialAnimation on beat {
                    running: dash.open && !!dash.player && dash.player.isPlaying
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 1.025; duration: 900; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 1.025; to: 1; duration: 900; easing.type: Easing.InOutSine }
                }
                layer.enabled: true
                layer.effect: MultiEffect { shadowEnabled: true; shadowColor: "#000000"; shadowOpacity: 0.5; shadowBlur: 1; shadowVerticalOffset: 10 }
            }
            Column {
                x: big.x + big.size + 40
                width: parent.width - x - 34
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Txt { width: parent.width; text: dash.player ? (dash.player.trackTitle || "Unknown song") : "Nothing playing"; font.pixelSize: 28; font.weight: Font.Bold; wrapMode: Text.WordWrap; maximumLineCount: 2 }
                Txt { width: parent.width; text: dash.player ? (dash.player.trackArtist || "") : "Start a song in any player"; font.pixelSize: 17; color: AetherStyle.textSub }
                Txt { width: parent.width; text: dash.player ? (dash.player.trackAlbum || "") : ""; font.pixelSize: 13; color: AetherStyle.textSub; opacity: 0.7; visible: text !== "" }
                Item { width: 1; height: 14 }
                Track {
                    width: parent.width
                    visible: !!dash.player
                    value: mus.len > 0 ? mus.pos / mus.len : 0
                    onMoved: v => { if (dash.player && dash.player.canSeek) dash.player.position = v * mus.len; }
                }
                Item {
                    width: parent.width
                    height: 16
                    visible: !!dash.player
                    Txt { text: dash.fmt(mus.pos); font.pixelSize: 11; color: AetherStyle.textSub }
                    Txt { anchors.right: parent.right; text: dash.fmt(mus.len); font.pixelSize: 11; color: AetherStyle.textSub }
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 18
                    visible: !!dash.player
                    RoundBtn { icon: "skip_previous"; size: 50; anchors.verticalCenter: parent.verticalCenter; onClicked: dash.player.previous() }
                    RoundBtn { icon: dash.player && dash.player.isPlaying ? "pause" : "play_arrow"; big: true; size: 68; anchors.verticalCenter: parent.verticalCenter; onClicked: dash.player.togglePlaying() }
                    RoundBtn { icon: "skip_next"; size: 50; anchors.verticalCenter: parent.verticalCenter; onClicked: dash.player.next() }
                }
                Item { width: 1; height: 6 }
                Row {
                    width: parent.width
                    spacing: 12
                    Sym { text: Icons.getVolumeIcon(Audio.volume, Audio.muted); anchors.verticalCenter: parent.verticalCenter }
                    Track {
                        width: parent.width - 120
                        anchors.verticalCenter: parent.verticalCenter
                        value: Audio.volume
                        onMoved: v => { if (Audio.sink && Audio.sink.audio) Audio.sink.audio.volume = v; }
                    }
                    RoundBtn { icon: "lyrics"; size: 36; anchors.verticalCenter: parent.verticalCenter; onClicked: dash.cfg.dashTab = "lyrics" }
                    RoundBtn { icon: "swap_horiz"; size: 36; anchors.verticalCenter: parent.verticalCenter; visible: Players.list.length > 1
                        onClicked: { const l = Players.list; Players.manualActive = l[(l.indexOf(dash.player) + 1) % l.length]; } }
                }
            }
        }
    }

    // ===================================== Lyrics: the song as a text conversation =====================================
    Component {
        id: lyricsC
        Item {
            id: ly
            clip: true
            readonly property bool chat: dash.cfg.lyricsBubbles
            readonly property bool fx: dash.cfg.lyricsFx
            readonly property real now: dash.player ? dash.player.position : 0
            // a little ahead, so the bubble pops as the line starts rather than just after
            readonly property int cur: Lyrics.hasLyrics ? Lyrics.indexForTime(now + 0.15) : -1

            // belting: a long held line ("byeeeeee") makes its bubble go wild and the whole chat zooms into it
            property real belt: 0                  // 0..1, set by the line being belted
            property real zx: lv.width / 2         // where to zoom to (the belted bubble)
            property real zy: lv.height / 2
            property real clock: 0
            property real zoomS: 1 + 0.45 * belt
            Behavior on zoomS { SpringAnimation { spring: 2.6; damping: 0.22; epsilon: 0.002 } }
            FrameAnimation {
                running: dash.open && (ly.belt > 0.001 || ly.zoomS > 1.002)
                onTriggered: ly.clock += frameTime
            }

            // backdrop: the cover, blurred and slowly zooming
            Image {
                anchors.fill: parent
                source: dash.player ? (dash.player.trackArtUrl || "") : ""
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 300
                opacity: status === Image.Ready ? 0.28 : 0
                layer.enabled: true
                layer.effect: MultiEffect { blurEnabled: true; blur: 1; blurMax: 64 }
                SequentialAnimation on scale {
                    running: dash.open && ly.fx
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.05; to: 1.3; duration: 12000; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 1.3; to: 1.05; duration: 12000; easing.type: Easing.InOutSine }
                }
            }

            // header: who you're "talking" to
            Rectangle {
                id: head
                width: parent.width
                height: 64
                radius: 22
                color: Qt.alpha(AetherStyle.palette.m3surfaceContainerHigh, 0.85)
                z: 2
                Cover {
                    id: av
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    size: 42
                    radius: 21
                }
                Column {
                    x: av.x + av.width + 12
                    width: parent.width - x - 120
                    anchors.verticalCenter: parent.verticalCenter
                    Txt { width: parent.width; text: dash.player ? (dash.player.trackArtist || "Unknown artist") : "Nobody's singing"; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Txt { width: parent.width; text: dash.player ? ((dash.player.isPlaying ? "singing now · " : "paused · ") + (dash.player.trackTitle || "")) : "Play a song to start the chat"; font.pixelSize: 11; color: dash.player && dash.player.isPlaying ? AetherStyle.primary : AetherStyle.textSub }
                }
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    RoundBtn { icon: dash.player && dash.player.isPlaying ? "pause" : "play_arrow"; size: 36; visible: !!dash.player; onClicked: dash.player.togglePlaying() }
                    RoundBtn { icon: "forum"; size: 36; on: ly.chat; onClicked: dash.cfg.lyricsBubbles = !dash.cfg.lyricsBubbles }
                }
            }

            // nothing to show: a typing bubble and a line why
            Column {
                anchors.centerIn: parent
                visible: !Lyrics.hasLyrics
                spacing: 14
                Typing { anchors.horizontalCenter: parent.horizontalCenter }
                Txt {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: !dash.player ? "Play a song to see its lyrics here" : Lyrics.loading ? "Looking for the lyrics…" : "No lyrics found for this song"
                    color: AetherStyle.textSub
                    font.pixelSize: 14
                }
            }

            ListView {
                id: lv
                anchors.fill: parent
                anchors.topMargin: head.height + 6
                visible: Lyrics.hasLyrics
                model: Lyrics.lyrics
                spacing: ly.chat ? 10 : 14
                clip: true
                currentIndex: ly.cur
                highlightRangeMode: ListView.ApplyRange
                preferredHighlightBegin: height * 0.42
                preferredHighlightEnd: height * 0.58
                highlightMoveDuration: 650
                highlightMoveVelocity: -1
                topMargin: height * 0.4
                bottomMargin: height * 0.45
                boundsBehavior: Flickable.StopAtBounds
                transform: Scale {
                    origin.x: ly.zx
                    origin.y: ly.zy
                    xScale: ly.zoomS
                    yScale: ly.zoomS
                }
                onModelChanged: Qt.callLater(() => lv.positionViewAtIndex(Math.max(0, ly.cur), ListView.Center))
                Component.onCompleted: Qt.callLater(() => lv.positionViewAtIndex(Math.max(0, ly.cur), ListView.Center))

                // fade the top and bottom of the conversation
                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: fadeMask
                    maskThresholdMin: 0
                    maskSpreadAtMin: 1
                }

                delegate: Item {
                    id: line
                    required property string modelData
                    required property int index
                    readonly property bool isCur: index === ly.cur
                    readonly property bool past: ly.cur >= 0 && index < ly.cur
                    readonly property bool mine: index % 2 === 1        // alternate sides, like two people texting
                    readonly property bool gap: modelData.trim() === ""   // an instrumental gap: someone is typing
                    readonly property real maxW: lv.width * 0.7
                    // belting: the line is held long, or its letters are stretched already ("noooo", "byeee")
                    readonly property real t0: Lyrics.timeForIndex(index)
                    readonly property real t1: {
                        const n = Lyrics.timeForIndex(index + 1);
                        return n > t0 ? n : t0 + 4;
                    }
                    readonly property bool stretched: /([a-zA-Z])\1{2,}/.test(modelData)
                    readonly property bool longHeld: !gap && (t1 - t0) > 5 && modelData.trim().split(/\s+/).length <= 5
                    readonly property bool isBelt: !gap && (stretched || longHeld)
                    readonly property real beltK: isCur && isBelt && ly.fx ? Math.max(0, Math.min(1, (ly.now - t0) / Math.max(0.8, (t1 - t0) * 0.4))) : 0
                    // the letter to stretch: the last doubled one, else the last vowel of the line
                    readonly property int runAt: {
                        const m = modelData.match(/([a-zA-Z])\1+(?!.*([a-zA-Z])\2)/);
                        if (m)
                            return m.index + m[0].length - 1;
                        const v = modelData.search(/[aeiouyAEIOUY](?=[^aeiouyAEIOUY]*$)/);
                        return v >= 0 ? v : modelData.length - 1;
                    }
                    readonly property int extra: Math.floor(beltK * 26)
                    readonly property string beltText: beltK <= 0 ? modelData : modelData.slice(0, runAt + 1) + Array(extra + 1).join(modelData.charAt(runAt)) + modelData.slice(runAt + 1)
                    onBeltKChanged: if (isCur) {
                        ly.belt = beltK;
                        ly.zx = bubble.x + bubble.width / 2;
                        ly.zy = line.y - lv.contentY + bubble.height / 2;
                    }
                    width: lv.width
                    height: (gap ? typing.height : bubble.height) + (isCur && mine && ly.chat ? 16 : 0)
                    Behavior on height { NumberAnimation { duration: 220 } }

                    // zoom in when sung, shrink away once it's passed
                    property real zoom: isCur ? 1.07 : past ? 0.92 : 1
                    Behavior on zoom { NumberAnimation { duration: line.isCur ? 520 : 400; easing.type: line.isCur ? Easing.OutBack : Easing.OutCubic; easing.overshoot: 2.4 } }
                    opacity: isCur ? 1 : past ? 0.5 : 0.75
                    Behavior on opacity { NumberAnimation { duration: 400 } }

                    onIsCurChanged: {
                        if (isCur && ly.fx) { pop.restart(); burst.restart(); }
                        if (!isCur && isBelt) ly.belt = 0;
                    }

                    TextMetrics {
                        id: tmx
                        font.family: dash.font
                        font.pixelSize: ly.chat ? 16 : 22
                        text: line.modelData
                    }

                    Typing {
                        id: typing
                        visible: line.gap
                        x: line.mine && ly.chat ? line.width - width - 18 : ly.chat ? 18 : (line.width - width) / 2
                        active: line.isCur
                    }

                    Rectangle {
                        id: bubble
                        visible: !line.gap
                        readonly property real padX: ly.chat ? 16 : 6
                        width: line.beltK > 0 ? Math.min(wild.implicitWidth * wild.scale + padX * 2 + 4, lv.width * 0.92) : Math.min(tmx.width + padX * 2 + 2, line.maxW)
                        height: line.beltK > 0 ? wild.implicitHeight * wild.scale + (ly.chat ? 26 : 12) : txt.implicitHeight + (ly.chat ? 20 : 8)
                        rotation: line.beltK > 0 ? Math.sin(ly.clock * 21) * 2.6 * line.beltK : 0
                        x: !ly.chat ? (line.width - width) / 2 : line.mine ? line.width - width - 18 : 18
                        radius: ly.chat ? Math.min(20, height / 2) : 0
                        color: !ly.chat ? "transparent" : line.mine ? (line.isCur ? AetherStyle.primary : AetherStyle.primaryContainer) : (line.isCur ? AetherStyle.palette.m3surfaceContainerHighest : AetherStyle.palette.m3surfaceContainerHigh)
                        Behavior on color { ColorAnimation { duration: 300 } }
                        scale: line.zoom * popS * (1 + (line.beltK > 0 ? 0.06 * line.beltK * Math.sin(ly.clock * 8.5) : 0))
                        property real popS: 1
                        transformOrigin: !ly.chat ? Item.Center : line.mine ? Item.BottomRight : Item.BottomLeft
                        layer.enabled: line.isCur && ly.fx
                        layer.effect: MultiEffect {
                            shadowEnabled: true
                            shadowColor: AetherStyle.primary
                            shadowOpacity: 0.7
                            shadowBlur: 1
                        }

                        // the tail: the corner nearest the sender stays sharp, like a text message
                        Rectangle {
                            visible: ly.chat
                            width: Math.min(18, bubble.height / 2)
                            height: width
                            radius: 4
                            color: bubble.color
                            y: bubble.height - height
                            x: line.mine ? bubble.width - width : 0
                        }
                        // the belted line: each letter wobbles on its own, the stretched ones swell
                        Row {
                            id: wild
                            visible: line.beltK > 0
                            anchors.centerIn: parent
                            scale: Math.min(1.25, (lv.width * 0.92 - bubble.padX * 2 - 4) / Math.max(1, implicitWidth))
                            Repeater {
                                model: line.beltK > 0 ? line.beltText.split("") : []
                                Text {
                                    required property string modelData
                                    required property int index
                                    readonly property bool held: index >= line.runAt && index <= line.runAt + line.extra
                                    text: modelData === " " ? "\u00a0" : modelData
                                    font.family: dash.font
                                    font.pixelSize: (ly.chat ? 18 : 24) * (held ? 1 + 0.3 * line.beltK : 1)
                                    font.weight: Font.Bold
                                    color: txt.color
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.verticalCenterOffset: Math.sin(ly.clock * (held ? 16 : 10) + index * 0.75) * (held ? 6 : 2.5) * line.beltK
                                    rotation: Math.sin(ly.clock * 13 + index * 1.3) * (held ? 12 : 4) * line.beltK
                                }
                            }
                        }
                        Text {
                            id: txt
                            visible: line.beltK <= 0
                            x: bubble.padX
                            anchors.verticalCenter: parent.verticalCenter
                            width: bubble.width - bubble.padX * 2
                            text: line.modelData
                            wrapMode: Text.WordWrap
                            horizontalAlignment: ly.chat ? Text.AlignLeft : Text.AlignHCenter
                            font.family: dash.font
                            font.pixelSize: ly.chat ? 16 : 22
                            font.weight: line.isCur ? Font.DemiBold : Font.Medium
                            color: !ly.chat ? (line.isCur ? AetherStyle.primary : AetherStyle.textMain) : line.mine ? (line.isCur ? AetherStyle.palette.m3onPrimary : AetherStyle.textOnPrimaryContainer) : AetherStyle.textMain
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: if (dash.player) dash.player.position = Lyrics.timeForIndex(line.index)
                        }
                    }
                    // "Delivered", under the line being sung on your side
                    Txt {
                        visible: line.isCur && line.mine && ly.chat && !line.gap
                        anchors.right: parent.right
                        anchors.rightMargin: 22
                        y: bubble.height + 2
                        text: "Delivered"
                        font.pixelSize: 10
                        color: AetherStyle.textSub
                    }

                    // the "send" pop: squash, overshoot, settle
                    SequentialAnimation {
                        id: pop
                        NumberAnimation { target: bubble; property: "popS"; from: 0.7; to: 1.12; duration: 220; easing.type: Easing.OutQuad }
                        NumberAnimation { target: bubble; property: "popS"; to: 1; duration: 380; easing.type: Easing.OutElastic; easing.amplitude: 1.2; easing.period: 0.5 }
                    }
                    // sparkles flying off the bubble
                    Item {
                        id: sparks
                        property real k: 1
                        visible: k < 1 && !line.gap
                        x: line.mine ? bubble.x : bubble.x + bubble.width
                        y: bubble.height / 2
                        Repeater {
                            model: 10
                            Rectangle {
                                required property int index
                                readonly property real a: (line.mine ? Math.PI * 0.5 : -Math.PI * 0.5) + (index / 9 - 0.5) * Math.PI * 1.3
                                readonly property real dist: 24 + (index % 3) * 14
                                width: 4 + (index % 2) * 3
                                height: width
                                radius: width / 2
                                color: index % 3 === 0 ? "#ffffff" : AetherStyle.primary
                                x: Math.sin(a) * dist * sparks.k - width / 2
                                y: -Math.cos(a) * dist * sparks.k * 0.7 - height / 2
                                opacity: 1 - sparks.k
                                scale: 1.4 - sparks.k
                            }
                        }
                        NumberAnimation on k { id: burst; running: false; from: 0; to: 1; duration: 700; easing.type: Easing.OutCubic }
                    }
                }
            }
            Rectangle {
                id: fadeMask
                anchors.fill: lv
                visible: false
                layer.enabled: true
                gradient: Gradient {
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 0.12; color: "black" }
                    GradientStop { position: 0.88; color: "black" }
                    GradientStop { position: 1; color: "transparent" }
                }
            }
        }
    }

    // the three dots that bounce while someone is typing
    component Typing: Rectangle {
        id: ty
        property bool active: true
        width: 64
        height: 36
        radius: 18
        color: AetherStyle.palette.m3surfaceContainerHigh
        Row {
            anchors.centerIn: parent
            spacing: 6
            Repeater {
                model: 3
                Rectangle {
                    id: dot
                    required property int index
                    width: 8
                    height: 8
                    radius: 4
                    color: AetherStyle.textSub
                    property real b: 0
                    transform: Translate { y: -dot.b * 5 }
                    opacity: 0.5 + dot.b * 0.5
                    SequentialAnimation on b {
                        running: ty.active && ty.visible && dash.open
                        loops: Animation.Infinite
                        PauseAnimation { duration: dot.index * 140 }
                        NumberAnimation { from: 0; to: 1; duration: 260; easing.type: Easing.OutQuad }
                        NumberAnimation { from: 1; to: 0; duration: 260; easing.type: Easing.InQuad }
                        PauseAnimation { duration: (2 - dot.index) * 140 + 200 }
                    }
                }
            }
        }
    }

    // ===================================== Wallpapers =====================================
    Component {
        id: wallsC
        Item {
            id: wp
            Txt {
                id: wpHead
                text: Wallpapers.list.length + " wallpapers in " + Paths.shortenHome(Paths.wallsdir) + "  ·  click one to use it"
                color: AetherStyle.textSub
                width: parent.width - 150
            }
            RoundBtn {
                anchors.right: parent.right
                anchors.verticalCenter: wpHead.verticalCenter
                icon: "shuffle"
                size: 34
                onClicked: Wallpapers.setRandom()
            }
            Column {
                anchors.centerIn: parent
                visible: Wallpapers.list.length === 0
                spacing: 8
                Sym { anchors.horizontalCenter: parent.horizontalCenter; text: "hide_image"; size: 44; color: AetherStyle.textSub }
                Txt { anchors.horizontalCenter: parent.horizontalCenter; text: "No pictures in " + Paths.shortenHome(Paths.wallsdir); color: AetherStyle.textSub }
                Txt { anchors.horizontalCenter: parent.horizontalCenter; text: "Put some there, or pick another folder in Settings > System > General (paths)."; color: AetherStyle.textSub; font.pixelSize: 11 }
            }
            GridView {
                id: gv
                y: wpHead.height + 14
                width: parent.width
                height: parent.height - y
                clip: true
                model: Wallpapers.list
                readonly property int cols: Math.max(2, Math.floor(width / 210))
                cellWidth: width / cols
                cellHeight: cellWidth * 0.62
                boundsBehavior: Flickable.StopAtBounds
                delegate: Item {
                    id: wc
                    required property var modelData
                    required property int index
                    readonly property bool cur: dash.normPath(modelData.path) === dash.normPath(Wallpapers.actualCurrent)
                    width: gv.cellWidth
                    height: gv.cellHeight
                    // tiles drop in one after another
                    opacity: 0
                    scale: 0.85
                    Component.onCompleted: inAnim.start()
                    ParallelAnimation {
                        id: inAnim
                        PauseAnimation { duration: Math.min(wc.index, 16) * 28 }
                        SequentialAnimation {
                            PauseAnimation { duration: Math.min(wc.index, 16) * 28 }
                            ParallelAnimation {
                                NumberAnimation { target: wc; property: "opacity"; to: 1; duration: 260 }
                                NumberAnimation { target: wc; property: "scale"; to: 1; duration: 420; easing.type: Easing.OutBack }
                            }
                        }
                    }
                    ClippingRectangle {
                        anchors.fill: parent
                        anchors.margins: 6
                        radius: 16
                        color: AetherStyle.palette.m3surfaceContainerHigh
                        border.width: wc.cur ? 3 : 0
                        border.color: AetherStyle.primary
                        scale: wm.containsMouse ? 1.04 : 1
                        Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                        Image {
                            anchors.fill: parent
                            source: wc.modelData.path
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 360
                            cache: true
                        }
                        Rectangle {
                            visible: wc.cur
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 8
                            width: 26
                            height: 26
                            radius: 13
                            color: AetherStyle.primary
                            Sym { anchors.centerIn: parent; text: "check"; size: 16; color: AetherStyle.palette.m3onPrimary }
                        }
                        MouseArea {
                            id: wm
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Wallpapers.setWallpaper(wc.modelData.path)
                        }
                    }
                }
            }
        }
    }

    // ===================================== System =====================================
    Component {
        id: systemC
        Item {
            id: sys
            component Ring: Item {
                id: ring
                property real value: 0
                property string label: ""
                property string sub: ""
                property string icon: ""
                property bool warn: true
                width: 200
                height: 200
                property real shown: value
                Behavior on shown { NumberAnimation { duration: 700; easing.type: Easing.OutCubic } }
                onShownChanged: cv.requestPaint()
                Canvas {
                    id: cv
                    anchors.fill: parent
                    onPaint: {
                        const c = getContext("2d");
                        c.reset();
                        const r = width / 2 - 12;
                        c.lineWidth = 14;
                        c.lineCap = "round";
                        c.strokeStyle = Qt.alpha(AetherStyle.textMain, 0.1);
                        c.beginPath();
                        c.arc(width / 2, height / 2, r, Math.PI * 0.75, Math.PI * 2.25);
                        c.stroke();
                        c.strokeStyle = ring.warn && ring.shown > 0.85 ? AetherStyle.errorAccent : AetherStyle.primary;
                        c.beginPath();
                        c.arc(width / 2, height / 2, r, Math.PI * 0.75, Math.PI * (0.75 + 1.5 * Math.max(0.005, Math.min(1, ring.shown))));
                        c.stroke();
                    }
                }
                Column {
                    anchors.centerIn: parent
                    spacing: 2
                    Sym { anchors.horizontalCenter: parent.horizontalCenter; text: ring.icon; size: 26; color: AetherStyle.textSub }
                    Txt { anchors.horizontalCenter: parent.horizontalCenter; text: ring.label; font.pixelSize: 30; font.weight: Font.Bold }
                    Txt { anchors.horizontalCenter: parent.horizontalCenter; text: ring.sub; font.pixelSize: 12; color: AetherStyle.textSub }
                }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 20
                spacing: 50
                Ring { value: dash.cpu / 100; label: dash.cpu + "%"; sub: "Processor"; icon: "memory" }
                Ring { value: dash.ram / 100; label: dash.ram + "%"; sub: dash.ramUsedGb.toFixed(1) + " of " + dash.ramTotalGb.toFixed(0) + " GB"; icon: "memory_alt" }
                Ring { value: Audio.volume; label: Math.round(Audio.volume * 100) + "%"; sub: Audio.muted ? "Muted" : "Volume"; warn: false; icon: Icons.getVolumeIcon(Audio.volume, Audio.muted) }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 30
                spacing: 18
                Repeater {
                    model: [
                        { icon: "lock", name: "Lock", run: () => WinSettings.ipc("lock", "lock", []) },
                        { icon: "bedtime", name: "Sleep", run: () => SessionManager.suspend() },
                        { icon: "logout", name: "Sign out", confirm: true, run: () => SessionManager.logout() },
                        { icon: "restart_alt", name: "Restart", confirm: true, run: () => SessionManager.reboot() },
                        { icon: "power_settings_new", name: "Shut down", confirm: true, run: () => SessionManager.poweroff() }
                    ]
                    Column {
                        id: act
                        required property var modelData
                        property bool armed: false
                        spacing: 8
                        Timer { id: disarm; interval: 3000; onTriggered: act.armed = false }
                        RoundBtn {
                            anchors.horizontalCenter: parent.horizontalCenter
                            icon: act.modelData.icon
                            size: 64
                            on: act.armed
                            onClicked: {
                                if (act.modelData.confirm && !act.armed) {
                                    act.armed = true;
                                    disarm.restart();
                                    return;
                                }
                                dash.cfg.dashOpen = false;
                                act.modelData.run();
                            }
                        }
                        Txt {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: act.armed ? "Click again" : act.modelData.name
                            color: act.armed ? AetherStyle.primary : AetherStyle.textSub
                        }
                    }
                }
            }
        }
    }
}
