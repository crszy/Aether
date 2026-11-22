// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// Desktop widget "Forecast": where you are, the temperature now in big numbers, the sky in your shape, how it
// feels, today's high and low, and the next hours in small chips (rice-weather json, wttr.in, every half hour).
Item {
    id: fw
    property var pal: null
    property real bg: 1
    property bool running: true
    property var w: null
    readonly property color fg: pal ? pal.fg : "#F6F5F4"
    readonly property color fg2: pal ? pal.fg2 : "#c8c8cc"
    readonly property color fg3: pal ? pal.fg3 : "#8a8a8f"
    readonly property color acc: pal ? pal.primary : "#9ec9bd"
    readonly property bool night: { const h = new Date().getHours(); return h < 6 || h >= 19 }

    DssProc {
        id: fetch
        command: [((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.local/bin/rice-weather", "json"]
        stdout: DssCollector { onStreamFinished: { try { const j = JSON.parse(text.trim().split("\n").pop()); if (j && j.temp !== undefined) fw.w = j } catch (e) { } } }
    }
    Timer { interval: 1800000; running: fw.running; repeat: true; triggeredOnStart: true; onTriggered: if (!fetch.running) fetch.running = true }

    Card { anchors.fill: parent; k: fw.bg; pal: fw.pal }

    // where
    Row {
        x: 20; y: 16; spacing: 4
        MIcon { anchors.verticalCenter: parent.verticalCenter; size: 15; name: "location_on"; glyph: String.fromCodePoint(0xF034E); color: fw.acc }
        T { anchors.verticalCenter: parent.verticalCenter; text: fw.w ? fw.w.place : Tr.t("Looking outside..."); font.pixelSize: 13; font.weight: Font.Medium; color: fw.fg2
            width: Math.min(implicitWidth, fw.width - 140); elide: Text.ElideRight }
    }
    // now
    Row {
        x: 18; y: 38; spacing: 2
        T { kind: "number"; text: fw.w ? String(fw.w.temp) : "--"; font.pixelSize: 58; font.weight: Font.Medium; color: fw.fg }
        T { kind: "number"; text: "°" + (fw.w ? fw.w.unit : ""); font.pixelSize: 22; color: fw.fg2; y: 10 }
    }
    Column {
        x: 20; y: 110; width: fw.width - 140; spacing: 1
        T { width: parent.width; elide: Text.ElideRight; text: fw.w ? fw.w.cond : ""; font.pixelSize: 13; font.weight: Font.DemiBold; color: fw.fg }
        T { width: parent.width; elide: Text.ElideRight; font.pixelSize: 11; color: fw.fg3
            text: fw.w ? Tr.t("Feels") + " " + fw.w.feels + "° · ↑" + fw.w.hi + "° ↓" + fw.w.lo + "°" : "" }
    }
    // the sky, in your shape, turning slowly
    Item {
        width: 92; height: 92
        x: fw.width - width - 18; y: 26
        Blob { anchors.fill: parent; kind: Look.shape; color: fw.pal ? fw.pal.chip : Qt.rgba(1, 1, 1, 0.08)
            SlowSpin { turn: 360; period: 46000; running: fw.running && !Look.lite && !Look.covered } }
        WxIcon { anchors.centerIn: parent; size: 44; cond: fw.w ? fw.w.cond : ""; night: fw.night; color: fw.acc }
    }
    // the next hours
    Row {
        id: hrs
        x: 14; y: fw.height - height - 14
        spacing: 6
        readonly property var list: fw.w && fw.w.hours ? fw.w.hours : []
        readonly property real cw: list.length ? (fw.width - 28 - spacing * (list.length - 1)) / list.length : 0
        Repeater {
            model: hrs.list
            delegate: Rectangle {
                width: hrs.cw; height: 70; radius: 18
                color: index === 0 ? Qt.rgba(fw.acc.r, fw.acc.g, fw.acc.b, 0.22) : (fw.pal ? fw.pal.chip : Qt.rgba(1, 1, 1, 0.08))
                Column {
                    anchors.centerIn: parent; spacing: 2
                    T { anchors.horizontalCenter: parent.horizontalCenter; text: (modelData.h < 10 ? "0" : "") + modelData.h + ":00"; font.pixelSize: 10; color: fw.fg3 }
                    WxIcon { anchors.horizontalCenter: parent.horizontalCenter; size: 20; cond: modelData.c; night: modelData.h < 6 || modelData.h >= 19; color: fw.acc }
                    T { anchors.horizontalCenter: parent.horizontalCenter; kind: "number"; text: modelData.t + "°"; font.pixelSize: 13; font.weight: Font.DemiBold; color: fw.fg }
                }
            }
        }
    }
}
