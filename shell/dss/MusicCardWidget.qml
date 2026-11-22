// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// Desktop widget "Music card": the cover, the song, a wavy line of how far it is and the buttons. The words button
// (or a click on the cover) opens it downwards: the lyrics run with the song, the line being sung bright in the
// middle (synced lyrics from LRCLIB through rice-lyrics, fetched only while it is open).
Item {
    id: mc
    property var pal: null
    property real bg: 1
    property bool running: true
    Instantiator { model: mc.running ? 1 : 0; delegate: QtObject { Component.onCompleted: Player.hold(); Component.onDestruction: Player.release() } }
    property bool open: false
    property real ex: open ? 1 : 0
    Behavior on ex { NumberAnimation { duration: 460; easing.type: Easing.BezierSpline; easing.bezierCurve: mc.open ? Look.emph : Look.accel } }
    readonly property real topH: 132
    readonly property real wordsH: 200
    readonly property real wantH: topH + wordsH * ex
    readonly property color fg: pal ? pal.fg : "#F6F5F4"
    readonly property color fg3: pal ? pal.fg3 : "#8a8a8f"
    readonly property color acc: pal ? pal.primary : "#9ec9bd"

    // ---- the lyrics (only while open)
    property var lines: []
    property string plain: ""
    // rice-lyrics answered that lookups are off (Settings > Language & region > Online lookups)
    property bool lyOff: false
    property bool loading: false
    property string songKey: ""
    readonly property string want: Player.has && open ? Player.artist + "|" + Player.title : ""
    onWantChanged: lookT.restart()
    Timer { id: lookT; interval: 500; onTriggered: mc.look() }
    function look() {
        if (want === "" || want === songKey) return
        songKey = want; lines = []; plain = ""
        loading = true
        fetch.command = [((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-lyrics", Player.artist, Player.title, Player.album, String(Math.round(Player.length))]
        fetch.running = true
    }
    DssProc {
        id: fetch
        stdout: DssCollector { onStreamFinished: { mc.loading = false; try { const j = JSON.parse(text); mc.lines = j.synced || []; mc.plain = j.plain || ""; mc.lyOff = j.off === true } catch (e) { } } }
    }
    readonly property int cur: {
        const l = lines, t = Player.pos + 0.3
        let k = -1
        for (let i = 0; i < l.length; i++) { if (l[i][0] <= t) k = i; else break }
        return k
    }

    Card { id: card; anchors.fill: parent; k: mc.bg; pal: mc.pal }

    // ---- the cover
    Item {
        id: cov
        x: 14; y: 14; width: mc.topH - 28; height: width
        ShapeImage { anchors.fill: parent; kind: "square"; source: Player.art; empty: mc.pal ? mc.pal.chip : "#2a2c30"; fallbackColor: mc.fg3 }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: mc.open = !mc.open }
    }
    // ---- the song
    Column {
        x: cov.x + cov.width + 16; y: 18
        width: mc.width - x - 18
        spacing: 2
        T { width: parent.width; elide: Text.ElideRight; text: Player.title; font.pixelSize: 16; font.weight: Font.DemiBold; color: mc.fg }
        T { width: parent.width; elide: Text.ElideRight; text: Player.artist; font.pixelSize: 12; color: mc.fg3 }
    }
    WavyLine {
        x: cov.x + cov.width + 14; y: 62
        width: mc.width - x - 18; height: 16
        value: Player.frac
        moving: Player.playing && mc.running && !Look.covered
        seekable: true
        color: mc.acc
        track: mc.pal ? mc.pal.track : Qt.rgba(1, 1, 1, 0.14)
        onMoved: function(v) { Player.seekTo(v) }
    }
    // ---- the buttons: words, back, play (a cookie that turns while it plays), next
    Row {
        id: btns
        anchors.right: parent.right; anchors.rightMargin: 14
        y: mc.topH - height - 14
        spacing: 6
        component Round: Item {
            id: rb
            property string icon: ""
            property int g: 0
            property bool on: false
            signal hit()
            width: 36; height: 36; anchors.verticalCenter: parent.verticalCenter
            Rectangle { anchors.fill: parent; radius: rb.on ? 12 : width / 2; color: rb.on ? Qt.rgba(mc.acc.r, mc.acc.g, mc.acc.b, 0.28) : (mc.pal ? mc.pal.chip : Qt.rgba(1, 1, 1, 0.08))
                scale: rm.pressed ? 0.9 : (rm.containsMouse ? 1.06 : 1)
                Behavior on radius { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } } }
            MIcon { anchors.centerIn: parent; size: 18; name: rb.icon; glyph: String.fromCodePoint(rb.g); color: rb.on ? mc.acc : mc.fg }
            MouseArea { id: rm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: rb.hit() }
        }
        Round { icon: "lyrics"; g: 0xF0D3C; on: mc.open; onHit: mc.open = !mc.open }
        Round { icon: "skip_previous"; g: 0xF04AE; onHit: Player.prev() }
        Item {
            width: 46; height: 46
            Blob {
                anchors.fill: parent; kind: "cookie"; color: mc.acc
                scale: pm.pressed ? 0.9 : 1
                Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                SlowSpin { turn: 360; period: 14000; running: mc.running && Player.playing && !Look.lite && !Look.covered }
            }
            MIcon { anchors.centerIn: parent; size: 22; name: Player.playing ? "pause" : "play_arrow"; glyph: String.fromCodePoint(Player.playing ? 0xF03E4 : 0xF040A); color: mc.pal ? mc.pal.inkOn : "#10201c" }
            MouseArea { id: pm; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Player.toggle() }
        }
        Round { icon: "skip_next"; g: 0xF04AD; onHit: Player.next() }
    }

    // ---- the words
    Item {
        id: words
        y: mc.topH; x: 22
        width: mc.width - 44; height: mc.wordsH * mc.ex - 14
        clip: true
        visible: mc.ex > 0.02
        opacity: Math.max(0, (mc.ex - 0.3) / 0.7)
        Rectangle { width: parent.width; height: 1; color: mc.pal ? mc.pal.track : Qt.rgba(1, 1, 1, 0.12); opacity: 0.6 }
        ListView {
            id: lv
            y: 8; width: parent.width; height: mc.wordsH - 30
            visible: mc.lines.length > 0
            model: mc.lines
            interactive: false
            spacing: 8
            currentIndex: Math.max(0, mc.cur)
            highlightRangeMode: ListView.StrictlyEnforceRange
            preferredHighlightBegin: height / 2 - 14
            preferredHighlightEnd: height / 2 + 14
            highlightMoveDuration: 460
            delegate: T {
                width: lv.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: modelData[1] === "" ? "♪" : modelData[1]
                readonly property bool now: index === mc.cur
                font.pixelSize: now ? 16 : 13
                font.weight: now ? Font.DemiBold : Font.Normal
                color: now ? mc.fg : mc.fg3
                opacity: now ? 1 : Math.max(0.2, 0.75 - Math.abs(index - mc.cur) * 0.18)
                Behavior on opacity { NumberAnimation { duration: 300 } }
                Behavior on font.pixelSize { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
            }
        }
        Flickable {
            y: 8; width: parent.width; height: mc.wordsH - 30
            visible: mc.lines.length === 0 && mc.plain !== ""
            contentHeight: ptx.implicitHeight
            contentY: Math.max(0, (ptx.implicitHeight - height) * Player.frac)
            interactive: false
            T { id: ptx; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: mc.plain; font.pixelSize: 13; lineHeight: 1.3; color: mc.fg3 }
        }
        Row {
            y: (mc.wordsH - 30) / 2; anchors.horizontalCenter: parent.horizontalCenter
            visible: mc.lines.length === 0 && mc.plain === ""
            spacing: 8
            Blob { width: 16; height: 16; kind: "cookie"; color: mc.fg3; anchors.verticalCenter: parent.verticalCenter
                RotationAnimator on rotation { from: 0; to: 360; duration: 1600; loops: Animation.Infinite; running: mc.loading } }
            T { anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12; color: mc.fg3
                text: !Player.has ? Tr.t("Nothing playing") : (mc.loading ? Tr.t("Looking for the lyrics...") : (mc.lyOff ? Tr.t("Lyrics are off: Settings > Language & region") : Tr.t("No lyrics for this song"))) }
        }
    }
}
