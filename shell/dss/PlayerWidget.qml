// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// Desktop widget "Player": the album art cut as a cookie inside a ring that shows the time, the song, a seek
// line and the buttons (previous, back 10 s, play / pause, forward 10 s, next).
Item {
    id: pw
    property var pal: null
    property real bg: 1
    property bool running: true
    Instantiator { model: pw.running ? 1 : 0; delegate: QtObject { Component.onCompleted: Player.hold(); Component.onDestruction: Player.release() } }
    readonly property color ink: pal ? pal.fg : "#F6F5F4"
    Card { anchors.fill: parent; k: pw.bg; pal: pw.pal }
    // the art and the ring
    Item {
        id: artBox
        width: Math.min(parent.width - 60, parent.height * 0.5); height: width
        anchors.horizontalCenter: parent.horizontalCenter
        y: 22
        WavyArc {
            anchors.fill: parent
            value: Player.frac
            moving: Player.playing && pw.running && !Look.covered
            color: pw.pal ? pw.pal.primary : "#9ec9bd"
            track: pw.pal ? pw.pal.track : Qt.rgba(1, 1, 1, 0.14)
        }
        ShapeImage {
            anchors.centerIn: parent
            width: parent.width - 26; height: width
            kind: "cookie"
            source: Player.art
            empty: pw.pal ? pw.pal.chip : "#2a2c30"
            fallbackColor: pw.pal ? pw.pal.fg3 : "#8a8a8f"
            SlowSpin { turn: 360; period: 24000; running: (pw.running && Player.playing && !Look.lite) && !Look.covered }
        }
    }
    T {
        id: tt
        anchors.top: artBox.bottom; anchors.topMargin: 16
        x: 20; width: parent.width - 40
        elide: Text.ElideRight
        text: Player.artist !== "" ? Player.title + " – " + Player.artist : Player.title
        font.pixelSize: 14; font.weight: Font.Medium; color: pw.ink
    }
    // seek line (drag it)
    WavyLine {
        id: seek
        anchors.top: tt.bottom; anchors.topMargin: 10
        x: 20; width: parent.width - 40; height: 20
        value: Player.frac
        moving: Player.playing && pw.running && !Look.covered
        seekable: true
        color: pw.pal ? pw.pal.primary : "#9ec9bd"
        track: pw.pal ? pw.pal.track : Qt.rgba(1, 1, 1, 0.14)
        onMoved: function(v) { Player.seekTo(v) }
    }
    Row {
        anchors.top: seek.bottom; anchors.topMargin: 14
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 8
        Repeater {
            model: [
                { i: "skip_previous", g: "󰒮", a: "prev" }, { i: "fast_rewind", g: "", a: "back" },
                { i: "play", g: "", a: "toggle" },
                { i: "fast_forward", g: "", a: "fwd" }, { i: "skip_next", g: "󰒭", a: "next" }
            ]
            delegate: Rectangle {
                readonly property bool big: modelData.a === "toggle"
                width: big ? 64 : 38; height: 42
                radius: bm.pressed ? 12 : (big ? 21 : 19)
                color: big ? (pw.pal ? pw.pal.primary : "#9ec9bd") : (bm.containsMouse ? (pw.pal ? pw.pal.chipHover : Qt.rgba(1, 1, 1, 0.16)) : (pw.pal ? pw.pal.chip : Qt.rgba(1, 1, 1, 0.08)))
                Behavior on radius { NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                MIcon {
                    anchors.centerIn: parent; size: parent.big ? 22 : 17
                    name: parent.big ? (Player.playing ? "pause" : "play_arrow") : modelData.i
                    glyph: parent.big ? (Player.playing ? "" : "") : modelData.g
                    color: parent.big ? (pw.pal ? pw.pal.inkOn : "#111") : pw.ink
                    filled: true
                }
                MouseArea { id: bm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (modelData.a === "toggle") Player.toggle()
                        else if (modelData.a === "prev") Player.prev()
                        else if (modelData.a === "next") Player.next()
                        else Player.seekBy(modelData.a === "back" ? -10 : 10)
                    } }
            }
        }
    }
}
