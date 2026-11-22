// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// Desktop widget "Now playing": a round label with the song in big letters, a scalloped ring that turns
// while it plays, and a dot that walks around it with the time. Click it to play or pause.
Item {
    id: nr
    property var pal: null
    property real bg: 1
    property bool running: true
    Instantiator { model: nr.running ? 1 : 0; delegate: QtObject { Component.onCompleted: Player.hold(); Component.onDestruction: Player.release() } }
    readonly property color ink: pal ? pal.fg : "#F6F5F4"
    Blob {
        id: disc
        anchors.fill: parent
        kind: "circle"
        color: nr.pal ? (nr.bg > 0.02 ? nr.pal.shapeFill : "transparent") : "#22262a"
    }
    // the scalloped inner ring
    Blob {
        anchors.centerIn: parent
        width: parent.width * 0.86; height: width
        kind: "sun"
        color: "transparent"
        line: nr.pal ? nr.pal.track : Qt.rgba(1, 1, 1, 0.14)
        lineWidth: 2
        SlowSpin { turn: 360; period: 30000; running: (nr.running && Player.playing && !Look.lite) && !Look.covered }
    }
    // the time: a dot on the outer edge
    Item {
        anchors.fill: parent
        rotation: Player.frac * 360
        Behavior on rotation { enabled: Player.playing; NumberAnimation { duration: 900 } }
        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 4; width: 16; height: 6; radius: 3; color: nr.pal ? nr.pal.primary : "#9ec9bd" }
    }
    T {
        anchors.centerIn: parent
        width: parent.width * 0.62
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        maximumLineCount: 4
        elide: Text.ElideRight
        text: Player.title
        font.pixelSize: Math.round(nr.width * 0.075)
        font.weight: Font.DemiBold
        color: nr.ink
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Player.toggle() }
}
