// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

// Desktop widget "CPU · GPU · RAM": three small shapes with how busy each one is. The GPU shows a dash
// when the computer does not tell its load.
Item {
    id: tw
    property var pal: null
    property real bg: 1
    property bool running: true
    // one rice-stats for the whole shell (Stats.qml)
    readonly property var st: Stats.st
    StatsHold { on: tw.running }
    readonly property bool haveGpu: Stats.haveGpu
    Row {
        anchors.centerIn: parent
        spacing: Math.round(tw.width * 0.025)
        Repeater {
            model: [
                { k: "cpu", n: "CPU", s: "cookie" },
                { k: "gpu", n: "GPU", s: "circle" },
                { k: "ram", n: "RAM", s: "cookie" }
            ]
            delegate: Item {
                readonly property bool mid: modelData.k === "gpu"
                readonly property bool na: mid && !tw.haveGpu
                width: Math.min(tw.height, (tw.width - 2 * Math.round(tw.width * 0.025)) / 3); height: width
                Blob {
                    anchors.fill: parent
                    kind: modelData.s
                    color: parent.mid ? (tw.pal ? tw.pal.tertiary : "#e8a28c") : (tw.pal ? (tw.bg > 0.02 ? tw.pal.shapeFill : "transparent") : "#22262a")
                    SlowSpin { turn: 360; period: 60000; running: (tw.running && !Look.lite && !parent.mid) && !Look.covered }
                }
                Column {
                    anchors.centerIn: parent
                    readonly property color ink: parent.mid ? (tw.pal ? tw.pal.inkTertiary : "#3a1a12") : (tw.pal ? tw.pal.fg : "#F6F5F4")
                    T { kind: "number"; anchors.horizontalCenter: parent.horizontalCenter; text: parent.parent.na ? "—" : tw.st[modelData.k] + "%"
                        font.pixelSize: Math.round(parent.parent.width * 0.24); font.weight: Font.DemiBold; color: parent.ink }
                    T { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.n; font.pixelSize: Math.round(parent.parent.width * 0.12); color: parent.ink; opacity: 0.8 }
                }
            }
        }
    }
}
