// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell.Services.Mpris
import QtQuick

Item {
    id: me
    property real bg: 1   // card background opacity (widget editor)
    property var pal: null
    function pick() {
        const l = Mpris.players.values
        for (let i = 0; i < l.length; i++) if (l[i].isPlaying) return l[i]
        return l.length > 0 ? l[0] : null
    }
    readonly property var player: pick()
    property bool running: true
    // where the song is (asked for once a second while it plays)
    Instantiator { model: me.running && me.visible ? 1 : 0; delegate: QtObject { Component.onCompleted: Player.hold(); Component.onDestruction: Player.release() } }

    Card { anchors.fill: parent; k: me.bg; pal: me.pal }

    RoundImage {
        id: art
        x: 0
        y: 0
        width: 120
        height: parent.height
        cornerRadius: 26
        fallback: ""
        source: me.player && me.player.trackArtUrl ? me.player.trackArtUrl : ""
    }

    Column {
        anchors.left: art.right
        anchors.leftMargin: 16
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.top: parent.top
        anchors.topMargin: 22
        spacing: 3
        T {
            width: parent.width
            elide: Text.ElideRight
            font.pixelSize: 16
            font.bold: true
            color: me.pal ? me.pal.fg : "#F6F5F4"
            text: me.player ? (me.player.trackTitle || "Unknown") : Tr.t("Nothing playing")
        }
        T {
            width: parent.width
            elide: Text.ElideRight
            font.pixelSize: 12
            color: me.pal ? me.pal.fg2 : "#a8a8ad"
            text: me.player ? (me.player.trackArtist || "") : Tr.t("Start some music")
        }
    }

    // how far the song is, as a wave you can drag
    WavyLine {
        anchors.left: art.right; anchors.leftMargin: 14
        anchors.right: ctrls.left; anchors.rightMargin: 10
        anchors.verticalCenter: ctrls.verticalCenter; height: 18
        visible: Player.has && Player.length > 0
        value: Player.frac; moving: Player.playing && !Look.covered; seekable: true
        color: me.pal ? me.pal.primary : "#9ec9bd"; track: me.pal ? me.pal.track : Qt.rgba(1, 1, 1, 0.14)
        onMoved: function(v) { Player.seekTo(v) }
    }

    Row {
        id: ctrls
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        spacing: 10
        Rectangle {
            width: 34
            height: 34
            radius: 17
            color: "transparent"
            opacity: me.player ? 1 : 0.35
            T { anchors.centerIn: parent; text: ""; font.pixelSize: 14; color: me.pal ? me.pal.fg : "#F6F5F4" }
            MouseArea {
                anchors.fill: parent
                enabled: me.player !== null
                cursorShape: Qt.PointingHandCursor
                onClicked: me.player.previous()
            }
        }
        // play / pause in your shape (a circle while it waits), it turns while music plays
        Item {
            width: 44
            height: 44
            opacity: me.player ? 1 : 0.35
            Blob { anchors.fill: parent; kind: me.player && me.player.isPlaying ? Look.shape : "circle"; color: me.pal ? me.pal.primary : "#F6F5F4"
                scale: pm.pressed ? 0.86 : 1
                Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                SlowSpin { turn: 360; period: 12000; running: (me.player && me.player.isPlaying && !Look.lite) && !Look.covered } }
            T {
                anchors.centerIn: parent
                text: me.player && me.player.isPlaying ? "" : ""
                font.pixelSize: 16
                color: me.pal ? me.pal.inkOn : "#0d0d0d"
            }
            MouseArea {
                id: pm
                anchors.fill: parent
                enabled: me.player !== null
                cursorShape: Qt.PointingHandCursor
                onClicked: me.player.togglePlaying()
            }
        }
        Rectangle {
            width: 34
            height: 34
            radius: 17
            color: "transparent"
            opacity: me.player ? 1 : 0.35
            T { anchors.centerIn: parent; text: ""; font.pixelSize: 14; color: me.pal ? me.pal.fg : "#F6F5F4" }
            MouseArea {
                anchors.fill: parent
                enabled: me.player !== null
                cursorShape: Qt.PointingHandCursor
                onClicked: me.player.next()
            }
        }
    }
}
