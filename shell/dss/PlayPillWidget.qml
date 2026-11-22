// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// Desktop widget "Play button": a big cookie that plays and pauses (it turns while music plays) and a pill
// with the song and how far it is.
Item {
    id: pp
    property var pal: null
    property real bg: 1
    property bool running: true
    Instantiator { model: pp.running ? 1 : 0; delegate: QtObject { Component.onCompleted: Player.hold(); Component.onDestruction: Player.release() } }
    Item {
        id: btn
        width: parent.height; height: parent.height
        Blob {
            anchors.fill: parent
            kind: "sun"
            color: pp.pal ? pp.pal.tertiary : "#e8a28c"
            scale: bm.pressed ? 0.92 : 1
            Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack; easing.overshoot: 2 } }
            SlowSpin { turn: 360; period: 20000; running: (pp.running && Player.playing && !Look.lite) && !Look.covered }
        }
        MIcon { anchors.centerIn: parent; size: parent.width * 0.3; filled: true
            name: Player.playing ? "pause" : "play_arrow"; glyph: Player.playing ? "" : ""
            color: pp.pal ? pp.pal.inkTertiary : "#3a1a12" }
        MouseArea { id: bm; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Player.toggle() }
    }
    Rectangle {
        x: btn.width + 10
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - x; height: parent.height * 0.8
        radius: height / 2
        color: pp.pal ? pp.pal.primary : "#9ec9bd"
        T {
            x: 26; y: parent.height * 0.18; width: parent.width - 52
            text: Player.title
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            font.pixelSize: Math.round(parent.height * 0.17); font.weight: Font.DemiBold
            color: pp.pal ? pp.pal.inkOn : "#10201c"
        }
        WavyLine {
            x: 22; width: parent.width - 44; height: 16
            anchors.bottom: parent.bottom; anchors.bottomMargin: parent.height * 0.1
            value: Player.frac
            moving: Player.playing && pp.running && !Look.covered
            seekable: true
            color: pp.pal ? pp.pal.inkOn : "#10201c"
            track: Qt.rgba(0, 0, 0, 0.16)
            onMoved: function(v) { Player.seekTo(v) }
        }
    }
}
