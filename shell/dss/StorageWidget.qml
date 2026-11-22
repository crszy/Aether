// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// Desktop widget "Storage": used and free space of your home disk, with a ring around the disk icon.
Item {
    id: sw
    property var pal: null
    property real bg: 1
    property bool running: true
    property real used: 0
    property real free: 0
    readonly property real frac: used + free > 0 ? used / (used + free) : 0
    function gb(k) { const g = k / 1048576; return g >= 1000 ? (g / 1024).toFixed(1) + " TB" : Math.round(g) + " GB" }
    DssProc {
        id: df
        command: ["bash", "-c", "df -k --output=used,avail \"$HOME\" 2>/dev/null | tail -1"]
        stdout: DssCollector { onStreamFinished: { const p = text.trim().split(/\s+/); sw.used = parseFloat(p[0]) || 0; sw.free = parseFloat(p[1]) || 0 } }
    }
    Timer { interval: 60000; running: sw.running; repeat: true; triggeredOnStart: true; onTriggered: df.running = true }
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: sw.pal ? (sw.bg > 0.02 ? sw.pal.shapeFill : "transparent") : "#22262a"
        border.width: sw.pal ? sw.pal.edgeWidth : 0; border.color: sw.pal ? sw.pal.edge : "transparent"
    }
    Column {
        x: 28; anchors.verticalCenter: parent.verticalCenter
        T { text: Tr.t("Used"); font.pixelSize: 11; color: sw.pal ? sw.pal.fg3 : "#8a8a8f" }
        T { kind: "number"; text: sw.gb(sw.used); font.pixelSize: 18; font.weight: Font.DemiBold; color: sw.pal ? sw.pal.fg : "#F6F5F4" }
    }
    Column {
        anchors.right: parent.right; anchors.rightMargin: 28; anchors.verticalCenter: parent.verticalCenter
        T { anchors.right: parent.right; text: Tr.t("Free"); font.pixelSize: 11; color: sw.pal ? sw.pal.fg3 : "#8a8a8f" }
        T { kind: "number"; anchors.right: parent.right; text: sw.gb(sw.free); font.pixelSize: 18; font.weight: Font.DemiBold; color: sw.pal ? sw.pal.fg : "#F6F5F4" }
    }
    Item {
        anchors.centerIn: parent
        width: parent.height - 12; height: width
        WavyArc { anchors.fill: parent; thick: 4; waves: 10; moving: false
            property real f: sw.frac
            Behavior on f { NumberAnimation { duration: 700; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }
            value: Math.max(0.01, f)
            color: sw.frac > 0.9 ? "#ff8a80" : (sw.pal ? sw.pal.primary : "#9ec9bd"); track: sw.pal ? sw.pal.track : Qt.rgba(1, 1, 1, 0.14) }
        Blob { anchors.centerIn: parent; width: parent.width - 16; height: width; kind: Look.shape; color: sw.pal ? sw.pal.chip : Qt.rgba(1, 1, 1, 0.08) }
        MIcon { anchors.centerIn: parent; size: parent.width * 0.32; name: "storage"; glyph: ""; color: sw.pal ? sw.pal.fg : "#F6F5F4" }
    }
}
