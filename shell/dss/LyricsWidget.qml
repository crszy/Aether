// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// Desktop widget "Lyrics": the song with its cover, and its words line by line in time with the music
// (synced lyrics from LRCLIB through rice-lyrics). The line being sung is big and bright, the next ones fade.
Item {
    id: ly
    property var pal: null
    property real bg: 1
    property bool running: true
    Instantiator { model: ly.running ? 1 : 0; delegate: QtObject { Component.onCompleted: Player.hold(); Component.onDestruction: Player.release() } }
    readonly property color ink: pal ? pal.fg : "#F6F5F4"
    property var lines: []          // [[seconds, text], ...]
    property string plain: ""
    // rice-lyrics answered that lookups are off (Settings > Language & region > Online lookups)
    property bool lyOff: false
    property bool loading: false
    property string songKey: ""
    readonly property string want: Player.has ? Player.artist + "|" + Player.title : ""
    onWantChanged: lookT.restart()
    Timer { id: lookT; interval: 600; onTriggered: ly.look() }
    function look() {
        if (want === songKey) return
        songKey = want; lines = []; plain = ""
        if (!Player.has || !running) return
        loading = true
        fetch.command = [((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-lyrics", Player.artist, Player.title, Player.album, String(Math.round(Player.length))]
        fetch.running = true
    }
    onRunningChanged: if (running) look()
    DssProc {
        id: fetch
        stdout: DssCollector {
            onStreamFinished: {
                ly.loading = false
                try { const j = JSON.parse(text); ly.lines = j.synced || []; ly.plain = j.plain || ""; ly.lyOff = j.off === true } catch (e) { }
            }
        }
    }
    // the line being sung
    readonly property int cur: {
        const l = lines, t = Player.pos + 0.3
        let k = -1
        for (let i = 0; i < l.length; i++) { if (l[i][0] <= t) k = i; else break }
        return k
    }

    Card { anchors.fill: parent; k: ly.bg; pal: ly.pal }
    // the song
    Item {
        id: head
        x: 16; y: 16; width: parent.width - 32; height: 52
        ShapeImage { id: cov; width: 52; height: 52; kind: "cookie"; source: Player.art; empty: ly.pal ? ly.pal.chip : "#2a2c30"; fallbackColor: ly.pal ? ly.pal.fg3 : "#8a8a8f" }
        Column {
            x: 64; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 64
            T { width: parent.width; elide: Text.ElideRight; text: Player.title; font.pixelSize: 14; font.weight: Font.DemiBold; color: ly.ink }
            T { width: parent.width; elide: Text.ElideRight; text: Player.artist; font.pixelSize: 11; color: ly.pal ? ly.pal.fg3 : "#8a8a8f" }
        }
    }
    // the words
    Item {
        id: words
        anchors.top: head.bottom; anchors.topMargin: 10
        anchors.bottom: bar.top; anchors.bottomMargin: 10
        x: 16; width: parent.width - 32
        clip: true
        // synced: the current line in the middle, the others around it
        ListView {
            id: lv
            anchors.fill: parent
            visible: ly.lines.length > 0
            model: ly.lines
            interactive: false
            spacing: 6
            currentIndex: Math.max(0, ly.cur)
            highlightRangeMode: ListView.ApplyRange
            preferredHighlightBegin: height * 0.35
            preferredHighlightEnd: height * 0.35 + 30
            highlightMoveDuration: 420
            delegate: T {
                width: lv.width
                wrapMode: Text.WordWrap
                text: modelData[1] === "" ? "♪" : modelData[1]
                readonly property bool now: index === ly.cur
                font.pixelSize: now ? 17 : 14
                font.weight: now ? Font.DemiBold : Font.Normal
                color: now ? ly.ink : (ly.pal ? ly.pal.fg3 : "#8a8a8f")
                opacity: now ? 1 : Math.max(0.25, 0.8 - Math.abs(index - ly.cur) * 0.15)
                Behavior on opacity { NumberAnimation { duration: 300 } }
            }
        }
        // plain words only: they scroll with the song
        Flickable {
            anchors.fill: parent
            visible: ly.lines.length === 0 && ly.plain !== ""
            contentHeight: pt.implicitHeight
            contentY: Math.max(0, (pt.implicitHeight - height) * Player.frac)
            interactive: false
            T { id: pt; width: parent.width; wrapMode: Text.WordWrap; text: ly.plain; font.pixelSize: 13; lineHeight: 1.3; color: ly.pal ? ly.pal.fg2 : "#c8c8cc" }
        }
        Row {
            anchors.centerIn: parent
            visible: ly.lines.length === 0 && ly.plain === ""
            spacing: 8
            Blob { width: 16; height: 16; kind: "cookie"; color: ly.pal ? ly.pal.fg3 : "#8a8a8f"; anchors.verticalCenter: parent.verticalCenter
                RotationAnimator on rotation { from: 0; to: 360; duration: 1600; loops: Animation.Infinite; running: ly.loading } }
            T { text: !Player.has ? Tr.t("Nothing playing") : (ly.loading ? Tr.t("Looking for the lyrics...") : (ly.lyOff ? Tr.t("Lyrics are off: Settings > Language & region") : Tr.t("No lyrics for this song"))); font.pixelSize: 12; color: ly.pal ? ly.pal.fg3 : "#8a8a8f"; anchors.verticalCenter: parent.verticalCenter }
        }
    }
    // how far the song is
    WavyLine {
        id: bar
        anchors.bottom: parent.bottom; anchors.bottomMargin: 12
        x: 16; width: parent.width - 32; height: 16
        value: Player.frac
        moving: Player.playing && ly.running && !Look.covered
        seekable: true
        color: ly.pal ? ly.pal.primary : "#9ec9bd"
        track: ly.pal ? ly.pal.track : Qt.rgba(1, 1, 1, 0.14)
        onMoved: function(v) { Player.seekTo(v) }
    }
}
