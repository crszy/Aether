// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import QtQuick

Item {
    id: rs
    property real bg: 1   // card background opacity (widget editor)
    property var pal: null
    property bool running: true
    // one rice-stats for the whole shell (Stats.qml)
    StatsHold { on: rs.running }
    readonly property int cpu: Math.round(Stats.st.cpu || 0)
    readonly property int ram: Math.round(Stats.st.ram || 0)
    readonly property int gpu: Math.round(Stats.st.gpu || 0)

    Column {
        anchors.fill: parent
        spacing: 12
        Repeater {
            model: [
                { name: "CPU", icon: "", key: "cpu" },
                { name: "RAM", icon: "󰍛", key: "ram" },
                { name: "GPU", icon: "", key: "gpu" }
            ]
            delegate: Item {
                id: cell
                width: rs.width
                height: (rs.height - 24) / 3
                readonly property int val: modelData.key === "cpu" ? rs.cpu : (modelData.key === "ram" ? rs.ram : rs.gpu)
                Card { anchors.fill: parent; k: rs.bg; pal: rs.pal }

                Item {
                    id: ring
                    width: 52; height: 52
                    anchors.right: parent.right; anchors.rightMargin: 16
                    anchors.top: parent.top; anchors.topMargin: 14
                    // how busy, as a wavy ring around a small shape of yours
                    WavyArc { anchors.fill: parent; thick: 4; waves: 9; moving: rs.running && !Look.lite && !Look.covered
                        property real v: cell.val
                        Behavior on v { NumberAnimation { duration: 600; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }
                        value: Math.max(0.01, v / 100)
                        color: cell.val > 85 ? "#ff8a80" : (rs.pal ? rs.pal.primary : "#F6F5F4"); track: rs.pal ? rs.pal.track : Qt.rgba(1, 1, 1, 0.12) }
                    Blob { anchors.centerIn: parent; width: 30; height: 30; kind: Look.shape; color: rs.pal ? rs.pal.chip : Qt.rgba(1, 1, 1, 0.08) }
                    T { kind: "icon"; anchors.centerIn: parent; text: modelData.icon; font.pixelSize: 15; color: rs.pal ? rs.pal.fg2 : "#c8c8cc" }
                                }
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 14
                    spacing: 0
                    T { kind: "number"; text: cell.val + "%"; font.pixelSize: 26; font.bold: true; color: rs.pal ? rs.pal.fg : "#F6F5F4" }
                    T { text: modelData.name; font.pixelSize: 12; color: rs.pal ? rs.pal.fg3 : "#8a8a8f" }
                }
            }
        }
    }
}
