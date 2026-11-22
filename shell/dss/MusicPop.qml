// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// The music card the bar's Music module opens (inside BarPop.qml): the cover in a turning halo of your shape,
// the song, its lyrics running with it (rice-lyrics, LRCLIB; fetched only while the card is open), a wavy
// progress line you can drag, the buttons and the volume. Everything comes in one after another as the card opens.
Item {
    id: mp
    property var bp: null                       // the card it lives in (colors, open, t)
    property var sink: null                     // the speakers (the volume buttons)
    readonly property bool on: !!bp && bp.open && bp.kind === "music"
    readonly property color fg: bp ? bp.fg : "white"
    readonly property color dim: bp ? bp.dim : "#aaa"
    readonly property color acc: bp ? bp.acc : "#9ec9bd"
    readonly property color chip: bp ? bp.chip : "#2a2c30"
    readonly property color ink: bp && bp.pal ? bp.pal.inkPrimary : "#10201c"
    width: 380
    implicitHeight: 360

    Instantiator { model: mp.on ? 1 : 0; delegate: QtObject { Component.onCompleted: Player.hold(); Component.onDestruction: Player.release() } }

    // one after another: each part gets its own moment while the card opens
    property real e: 0
    onOnChanged: { eA.stop(); if (on) { e = 0; eA.start() } }
    NumberAnimation { id: eA; target: mp; property: "e"; from: 0; to: 1; duration: 760; easing.type: Easing.Linear }
    function st(i) { const p = Math.max(0, Math.min(1, (e - i * 0.09) / 0.55)); return 1 - Math.pow(1 - p, 3) }

    // ---------- the lyrics ----------
    property var lines: []
    property string plain: ""
    // rice-lyrics answered that lookups are off (Settings > Language & region > Online lookups)
    property bool lyOff: false
    property bool loading: false
    property string songKey: ""
    readonly property string want: Player.has && on ? Player.artist + "|" + Player.title : ""
    // nothing plays (the player closed): the last song's lyrics go too
    readonly property bool hasSong: Player.has
    onHasSongChanged: if (!hasSong) { lines = []; plain = ""; songKey = ""; loading = false }
    onWantChanged: lookT.restart()
    Timer { id: lookT; interval: 400; onTriggered: mp.look() }
    function look() {
        if (want === "" || want === songKey) return
        songKey = want; lines = []; plain = ""
        loading = true
        fetch.command = [((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-lyrics", Player.artist, Player.title, Player.album, String(Math.round(Player.length))]
        fetch.running = true
    }
    DssProc {
        id: fetch
        stdout: DssCollector { onStreamFinished: { mp.loading = false; try { const j = JSON.parse(text); mp.lines = j.synced || []; mp.plain = j.plain || ""; mp.lyOff = j.off === true } catch (e) { } } }
    }
    readonly property int cur: {
        const l = lines, t = Player.pos + 0.3
        let k = -1
        for (let i = 0; i < l.length; i++) { if (l[i][0] <= t) k = i; else break }
        return k
    }

    // ---------- the cover and the song ----------
    Item {
        id: cov
        x: 10; y: 4; width: 118; height: 118
        opacity: mp.st(0)
        scale: 0.55 + 0.45 * mp.st(0)
        rotation: -24 * (1 - mp.st(0))
        // a halo of your shape behind it, turning while the song plays
        Blob {
            anchors.centerIn: parent; width: parent.width + 16; height: width
            kind: Look.shape; color: Qt.rgba(mp.acc.r, mp.acc.g, mp.acc.b, 0.22)
            SlowSpin { turn: 360; period: 22000; running: mp.on && Player.playing && !Look.lite }
        }
        ShapeImage { anchors.fill: parent; anchors.margins: 6; kind: "square"; source: Player.art; empty: mp.chip; fallbackColor: mp.dim }
    }
    Column {
        x: cov.x + cov.width + 16; y: 10
        width: mp.width - x - 4
        spacing: 4
        opacity: mp.st(1)
        transform: Translate { x: (1 - mp.st(1)) * 26 }
        Rectangle {
            visible: Player.has
            height: 22; radius: 11; width: idt.implicitWidth + 30
            color: Qt.rgba(mp.acc.r, mp.acc.g, mp.acc.b, 0.18)
            Row { anchors.centerIn: parent; spacing: 5
                Item { width: 12; height: 12; anchors.verticalCenter: parent.verticalCenter
                    // three small bars that dance while it plays
                    Row { anchors.centerIn: parent; spacing: 1.5
                        Repeater { model: 3
                            Rectangle { width: 2.5; radius: 1.25; anchors.bottom: parent.bottom; color: mp.acc
                                property real h: 4
                                height: h
                                SequentialAnimation on h { running: mp.on && Player.playing && !Look.lite; loops: Animation.Infinite
                                    NumberAnimation { to: 12; duration: 260 + index * 90; easing.type: Easing.InOutSine }
                                    NumberAnimation { to: 3; duration: 300 + index * 70; easing.type: Easing.InOutSine } } } } } }
                Text { id: idt; anchors.verticalCenter: parent.verticalCenter; text: Player.p ? (Player.p.identity || "Music") : ""; color: mp.acc
                    font.family: Fonts.main; font.pixelSize: 11; font.weight: Font.DemiBold } }
        }
        Text { width: parent.width; text: Player.title; color: mp.fg; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            font.family: Fonts.main; font.pixelSize: 19; font.weight: Font.Bold; lineHeight: 0.95 }
        Text { width: parent.width; text: Player.artist + (Player.album !== "" ? "  ·  " + Player.album : ""); color: mp.dim; elide: Text.ElideRight
            font.family: Fonts.main; font.pixelSize: 12 }
    }

    // ---------- the lyrics ----------
    Rectangle {
        id: words
        x: 0; y: cov.y + cov.height + 12; width: mp.width; height: 112
        radius: 22; color: mp.chip; clip: true
        opacity: mp.st(2)
        transform: Translate { y: (1 - mp.st(2)) * 22 }
        ListView {
            id: lyr
            anchors.fill: parent; anchors.margins: 6
            visible: mp.hasSong && mp.lines.length > 0
            model: mp.lines
            interactive: false
            currentIndex: Math.max(0, mp.cur)
            highlightRangeMode: ListView.StrictlyEnforceRange
            preferredHighlightBegin: height / 2 - 13
            preferredHighlightEnd: height / 2 + 13
            highlightMoveDuration: 420
            delegate: Text {
                width: lyr.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: modelData[1] === "" ? "♪" : modelData[1]
                readonly property bool now: index === mp.cur
                font.family: Fonts.main
                font.pixelSize: now ? 16 : 12
                font.weight: now ? Font.Bold : Font.Normal
                color: now ? mp.acc : mp.fg
                opacity: now ? 1 : Math.max(0.15, 0.6 - Math.abs(index - mp.cur) * 0.2)
                Behavior on opacity { NumberAnimation { duration: 300 } }
                Behavior on font.pixelSize { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
            }
        }
        Flickable {
            anchors.fill: parent; anchors.margins: 12
            visible: mp.hasSong && mp.lines.length === 0 && mp.plain !== ""
            contentHeight: ptx.implicitHeight
            contentY: Math.max(0, (ptx.implicitHeight - height) * Player.frac)
            interactive: false
            Text { id: ptx; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: mp.plain
                color: mp.dim; font.family: Fonts.main; font.pixelSize: 12; lineHeight: 1.3 }
        }
        Row {
            anchors.centerIn: parent
            visible: mp.lines.length === 0 && mp.plain === ""
            spacing: 8
            Blob { width: 16; height: 16; kind: Look.shape; color: mp.dim; anchors.verticalCenter: parent.verticalCenter
                RotationAnimator on rotation { from: 0; to: 360; duration: 1600; loops: Animation.Infinite; running: mp.loading && mp.on } }
            Text { anchors.verticalCenter: parent.verticalCenter; color: mp.dim; font.family: Fonts.main; font.pixelSize: 12
                text: !Player.has ? Tr.t("Nothing playing") : (mp.loading ? Tr.t("Looking for the lyrics...") : (mp.lyOff ? Tr.t("Lyrics are off: Settings > Language & region") : Tr.t("No lyrics for this song"))) }
        }
        // soft edges, the lines fade in and out
        Rectangle { anchors.top: parent.top; width: parent.width; height: 30; radius: words.radius; gradient: Gradient {
            GradientStop { position: 0; color: mp.chip } GradientStop { position: 1; color: Qt.rgba(mp.chip.r, mp.chip.g, mp.chip.b, 0) } } }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 30; radius: words.radius; gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(mp.chip.r, mp.chip.g, mp.chip.b, 0) } GradientStop { position: 1; color: mp.chip } } }
    }

    // ---------- where the song is ----------
    Item {
        id: prog
        x: 0; y: words.y + words.height + 12; width: mp.width; height: 20
        opacity: mp.st(3)
        transform: Translate { y: (1 - mp.st(3)) * 18 }
        Text { id: tA; anchors.left: parent.left; anchors.leftMargin: 4; anchors.verticalCenter: parent.verticalCenter; text: Player.clock(Player.pos)
            color: mp.dim; font.family: Fonts.numbers; font.pixelSize: 11 }
        WavyLine {
            anchors.left: tA.right; anchors.leftMargin: 10; anchors.right: tB.left; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter; height: 18
            value: Player.frac; moving: Player.playing && mp.on; seekable: true
            color: mp.acc; track: Qt.rgba(mp.acc.r, mp.acc.g, mp.acc.b, 0.2)
            onMoved: function(v) { Player.seekTo(v) }
        }
        Text { id: tB; anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter; text: Player.clock(Player.length)
            color: mp.dim; font.family: Fonts.numbers; font.pixelSize: 11 }
    }

    // ---------- the buttons ----------
    Row {
        id: btns
        anchors.horizontalCenter: parent.horizontalCenter
        y: prog.y + prog.height + 14
        spacing: 8
        opacity: mp.st(4)
        transform: Translate { y: (1 - mp.st(4)) * 18 }
        readonly property real vol: Vol.of(mp.sink)
        function setVol(d) { Vol.nudge(mp.sink, d) }
        Btn { w: 46; icon: "volume_down"; g: 0xF075E; onHit: btns.setVol(-0.05) }
        Btn { w: 56; icon: "skip_previous"; g: 0xF04AE; onHit: Player.prev() }
        Btn { w: 84; icon: Player.playing ? "pause" : "play_arrow"; g: Player.playing ? 0xF03E4 : 0xF040A; strong: true; onHit: Player.toggle() }
        Btn { w: 56; icon: "skip_next"; g: 0xF04AD; onHit: Player.next() }
        Btn { w: 46; icon: "volume_up"; g: 0xF057E; onHit: btns.setVol(0.05) }
    }
    // the volume, as a thin line under the buttons
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: btns.y + 56; width: 150; height: 4; radius: 2
        color: Qt.rgba(mp.acc.r, mp.acc.g, mp.acc.b, 0.18)
        opacity: mp.st(5)
        Rectangle { width: Math.max(4, parent.width * Math.min(1, btns.vol)); height: 4; radius: 2; color: mp.acc
            Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } } }
    }

    component Btn: Item {
        id: b
        property real w: 50
        property string icon: ""
        property int g: 0
        property bool strong: false
        signal hit()
        width: w; height: 46
        Rectangle {
            anchors.fill: parent
            // pressed: the corners tighten; the play button is a pill that turns into a rounder square while it plays
            radius: bm.pressed ? 12 : (b.strong && Player.playing ? 16 : height / 2)
            Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
            color: b.strong ? mp.acc : (bm.containsMouse ? Qt.rgba(mp.acc.r, mp.acc.g, mp.acc.b, 0.3) : Qt.rgba(mp.acc.r, mp.acc.g, mp.acc.b, 0.16))
            Behavior on color { ColorAnimation { duration: 140 } }
            scale: bm.pressed ? 0.94 : 1
            Behavior on scale { NumberAnimation { duration: 140 } }
        }
        MIcon { anchors.centerIn: parent; size: b.strong ? 24 : 20; name: b.icon; glyph: String.fromCodePoint(b.g); color: b.strong ? mp.ink : mp.fg }
        MouseArea { id: bm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); b.hit() } }
    }
}
