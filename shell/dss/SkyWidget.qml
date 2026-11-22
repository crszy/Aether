// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// Desktop widget "Sky": the weather as a tilted pill: a big sun, moon or cloud and the temperature.
// Follows Settings > Language & region for °C or °F (rice-weather).
Item {
    id: sk
    property var pal: null
    property real bg: 1
    property bool running: true
    property string cond: ""
    property string temp: "--"
    property var now: new Date()
    readonly property bool night: now.getHours() >= 19 || now.getHours() < 6
    function icon(c) {
        const s = c.toLowerCase()
        if (s.indexOf("thunder") >= 0) return ["thunderstorm", ""]
        if (s.indexOf("snow") >= 0 || s.indexOf("sleet") >= 0 || s.indexOf("ice") >= 0) return ["weather_snowy", ""]
        if (s.indexOf("rain") >= 0 || s.indexOf("drizzle") >= 0 || s.indexOf("shower") >= 0) return ["rainy", ""]
        if (s.indexOf("fog") >= 0 || s.indexOf("mist") >= 0) return ["foggy", ""]
        if (s.indexOf("cloud") >= 0 || s.indexOf("overcast") >= 0) return [night ? "nights_stay" : "partly_cloudy_day", ""]
        return night ? ["bedtime", ""] : ["sunny", ""]
    }
    DssProc {
        id: fetch
        command: [((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-weather"]
        stdout: DssCollector {
            onStreamFinished: {
                const p = text.trim().split("|")
                if (p.length >= 2) { sk.cond = p[0]; sk.temp = p[1].replace("+", "").replace(/°[CF]/, "°") }
            }
        }
    }
    Timer { interval: 1200000; running: sk.running; repeat: true; triggeredOnStart: true; onTriggered: { sk.now = new Date(); fetch.running = true } }
    Rectangle {
        id: pill
        anchors.centerIn: parent
        width: parent.width * 0.92; height: parent.height * 0.56
        radius: height / 2
        rotation: -38
        color: sk.pal ? (sk.bg > 0.02 ? sk.pal.shapeFill : "transparent") : "#22262a"
        border.width: sk.pal ? sk.pal.edgeWidth : 0; border.color: sk.pal ? sk.pal.edge : "transparent"
    }
    MIcon {
        x: parent.width * 0.16; y: parent.height * 0.42
        size: parent.width * 0.26
        filled: true
        name: sk.icon(sk.cond)[0]; glyph: sk.icon(sk.cond)[1]
        color: sk.pal ? sk.pal.fg : "#F6F5F4"
    }
    T {
        kind: "number"
        x: parent.width * 0.52; y: parent.height * 0.2
        text: sk.temp
        font.pixelSize: Math.round(parent.width * 0.17); font.weight: Font.DemiBold
        color: sk.pal ? sk.pal.fg : "#F6F5F4"
    }
}
