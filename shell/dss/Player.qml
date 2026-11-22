// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
pragma Singleton
import Quickshell
import Quickshell.Services.Mpris
import QtQuick

// The music player the widgets follow: the one that plays, else the first one. Its position is asked for
// once a second while a widget that shows it is out (hold() on the way in, release() on the way out).
Singleton {
    id: pl
    // how many places show the player right now (0: nothing ticks)
    property int users: 0
    function hold() { users++ }
    function release() { users = Math.max(0, users - 1) }
    readonly property bool running: users > 0
    function pick() {
        const l = Mpris.players.values
        for (let i = 0; i < l.length; i++) if (l[i].isPlaying) return l[i]
        return l.length > 0 ? l[0] : null
    }
    readonly property var p: pick()
    readonly property bool has: p !== null
    readonly property bool playing: p ? p.isPlaying : false
    readonly property string title: p ? (p.trackTitle || "Unknown") : Tr.t("Nothing playing")
    readonly property string artist: p ? (p.trackArtist || "") : ""
    readonly property string album: p ? (p.trackAlbum || "") : ""
    readonly property string art: p && p.trackArtUrl ? p.trackArtUrl : ""
    readonly property real length: p && p.length > 0 ? p.length : 0
    property real pos: 0
    readonly property real frac: length > 0 ? Math.max(0, Math.min(1, pos / length)) : 0
    property Timer tick: Timer {
        interval: 1000; repeat: true; triggeredOnStart: true
        running: pl.running && pl.playing
        onTriggered: { if (pl.p) { pl.p.positionChanged(); pl.pos = pl.p.position } }
    }
    onPChanged: Qt.callLater(function() { pl.pos = pl.p ? pl.p.position : 0 })
    function toggle() { if (p && p.canTogglePlaying) p.togglePlaying() }
    function next() { if (p && p.canGoNext) p.next() }
    function prev() { if (p && p.canGoPrevious) p.previous() }
    function seekBy(s) { if (p && p.canSeek) { p.position = Math.max(0, Math.min(length, p.position + s)); pos = p.position } }
    function seekTo(f) { if (p && p.canSeek && length > 0) { p.position = f * length; pos = p.position } }
    function clock(sec) { const v = Math.max(0, Math.floor(sec)); const m = Math.floor(v / 60), s = v % 60; return m + ":" + (s < 10 ? "0" + s : s) }
}
