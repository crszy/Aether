// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

Item {
    id: we
    property real bg: 1   // card background opacity (widget editor)
    property var pal: null
    property bool running: true
    property string cond: ""
    property string temp: "--"
    property string place: ""
    property string error: ""

    function glyph(c) {
        const s = c.toLowerCase()
        if (s.indexOf("thunder") >= 0) return ""
        if (s.indexOf("snow") >= 0 || s.indexOf("sleet") >= 0 || s.indexOf("ice") >= 0) return ""
        if (s.indexOf("rain") >= 0 || s.indexOf("drizzle") >= 0 || s.indexOf("shower") >= 0) return ""
        if (s.indexOf("fog") >= 0 || s.indexOf("mist") >= 0) return ""
        if (s.indexOf("cloud") >= 0 || s.indexOf("overcast") >= 0) return ""
        return ""
    }

    DssProc {
        id: fetch
        command: [((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-weather"]
        stdout: DssCollector {
            onStreamFinished: {
                const p = text.trim().split("|")
                if (p.length >= 3) {
                    we.cond = p[0]
                    we.temp = p[1].replace("+", "")
                    we.place = p[2]
                    we.error = ""
                } else {
                    we.error = "No weather data"
                }
            }
        }
    }
    Timer {
        interval: 1200000
        running: we.running
        repeat: true
        triggeredOnStart: true
        onTriggered: fetch.running = true
    }
    onRunningChanged: if (running) fetch.running = true

    Card { anchors.fill: parent; k: we.bg; pal: we.pal }

    // the weather's icon inside your shape, which turns slowly
    Item {
        id: ic
        x: 20
        width: 76; height: 76
        anchors.verticalCenter: parent.verticalCenter
        Blob { anchors.fill: parent; kind: Look.shape; color: we.pal ? we.pal.chip : Qt.rgba(1, 1, 1, 0.08)
            SlowSpin { turn: 360; period: 40000; running: (we.running && !Look.lite) && !Look.covered } }
        T { kind: "icon"; anchors.centerIn: parent; text: we.glyph(we.cond); font.pixelSize: 38; color: we.pal ? we.pal.primary : "#F6F5F4" }
    }
    Column {
        anchors.left: ic.right
        anchors.leftMargin: 20
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        T { kind: "number"; text: we.temp; font.pixelSize: 34; font.bold: true; color: we.pal ? we.pal.fg : "#F6F5F4" }
        T { text: we.error !== "" ? we.error : we.cond; font.pixelSize: 13; color: we.pal ? we.pal.fg2 : "#c8c8cc"; width: parent.width; elide: Text.ElideRight }
        T { text: we.place; font.pixelSize: 11; color: we.pal ? we.pal.fg3 : "#8a8a8f"; width: parent.width; elide: Text.ElideRight }
    }
}
