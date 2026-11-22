// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell.Io
import QtQuick

// Battery: this computer's battery as a ring, plus wireless devices (mouse, headset, ...).
// On a desktop without a battery it shows the devices, or a short note.
Item {
    id: bt
    property real bg: 1
    property var pal: null
    readonly property color cFg: pal ? pal.fg : "#F6F5F4"
    readonly property color cFg2: pal ? pal.fg2 : "#c8c8cc"
    readonly property color cFg3: pal ? pal.fg3 : "#8a8a8f"
    readonly property color cFg4: pal ? pal.fg4 : "#6f6f76"
    readonly property color cWarn: pal ? pal.warn : "#e06c75"
    readonly property color cMain: pal ? pal.primary : "#F6F5F4"
    readonly property color cTrack: pal ? pal.track : Qt.rgba(1, 1, 1, 0.12)
    property bool running: true
    property var main: null
    property var devs: []
    readonly property bool hasMain: main !== null && main !== undefined
    readonly property int pct: hasMain ? main.pct : -1
    readonly property string status: hasMain ? main.status : ""
    readonly property bool charging: status === "Charging" || status === "Full"
    readonly property bool low: hasMain && pct <= 15 && !charging

    DssProc {
        id: proc
        command: ["bash", "-c", "\"$HOME/.local/bin/rice-battery\""]
        stdout: DssCollector {
            onStreamFinished: {
                try { const j = JSON.parse(text); bt.main = j.main; bt.devs = j.devices || [] } catch (e) { }
            }
        }
    }
    Timer { interval: 20000; running: bt.running && bt.visible; repeat: true; triggeredOnStart: true; onTriggered: if (!proc.running) proc.running = true }

    function devIcon(k) {
        return k === "mouse" ? "" : k === "keyboard" ? "" : (k === "headset" || k === "headphones") ? ""
             : k === "gaming-input" ? "" : k === "phone" ? "" : k === "speakers" ? "" : ""
    }

    Card { anchors.fill: parent; k: bt.bg; pal: bt.pal }

    // ---- laptop battery
    Item {
        visible: bt.hasMain
        anchors.fill: parent
        Item {
            id: ring
            width: 72; height: 72
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            WavyArc { anchors.fill: parent; thick: 6; waves: 11; moving: bt.charging && !Look.covered
                property real v: Math.max(0, bt.pct)
                Behavior on v { NumberAnimation { duration: 700; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }
                value: Math.max(0.01, v / 100)
                color: bt.low ? bt.cWarn : bt.cMain; track: bt.cTrack }
            Blob { anchors.centerIn: parent; width: 40; height: 40; kind: Look.shape; color: bt.pal ? bt.pal.chip : Qt.rgba(1, 1, 1, 0.08)
                SlowSpin { turn: 360; period: 20000; running: (bt.charging && !Look.lite) && !Look.covered } }
            T { kind: "icon"; anchors.centerIn: parent; text: bt.charging ? "" : ""; font.pixelSize: 18; color: bt.low ? bt.cWarn : bt.cFg2 }
                }
        Column {
            anchors.left: ring.right
            anchors.leftMargin: 16
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            T { kind: "number"; text: bt.pct + "%"; font.pixelSize: 30; font.bold: true; color: bt.cFg }
            T {
                width: parent.width
                elide: Text.ElideRight
                text: bt.status === "Full" ? Tr.t("Fully charged")
                    : (bt.charging ? "Charging" : "On battery") + (bt.main && bt.main.left ? "  ·  " + bt.main.left + (bt.charging ? " to full" : " left") : "")
                font.pixelSize: 12; color: bt.cFg3
            }
            // first wireless device under it, when there is one
            T {
                visible: bt.devs.length > 0
                width: parent.width
                elide: Text.ElideRight
                text: bt.devs.length > 0 ? bt.devs[0].name + "  " + bt.devs[0].pct + "%" : ""
                font.pixelSize: 11; color: bt.cFg4
            }
        }
    }

    // ---- no battery in this computer: wireless devices, or a note
    Column {
        visible: !bt.hasMain
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7
        Row {
            visible: bt.devs.length === 0
            spacing: 14
            Rectangle {
                width: 46; height: 46; radius: 23
                color: bt.pal ? bt.pal.chip : Qt.rgba(1, 1, 1, 0.07)
                T { kind: "icon"; anchors.centerIn: parent; text: ""; font.pixelSize: 18; color: bt.cFg2 }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: bt.width - 36 - 60
                spacing: 2
                T { text: Tr.t("Plugged in"); font.pixelSize: 17; font.bold: true; color: bt.cFg }
                T { width: parent.width; elide: Text.ElideRight; text: Tr.t("No battery in this computer"); font.pixelSize: 11; color: bt.cFg3 }
                T { width: parent.width; elide: Text.ElideRight; text: Tr.t("Wireless mice and headsets show here"); font.pixelSize: 10; color: bt.cFg4 }
            }
        }
        Repeater {
            model: bt.devs.slice(0, 3)
            delegate: Item {
                width: parent.width
                height: 22
                T { id: di; kind: "icon"; text: bt.devIcon(modelData.kind); font.pixelSize: 13; color: bt.cFg2; anchors.verticalCenter: parent.verticalCenter; width: 20 }
                T { x: 26; width: parent.width - 26 - 110; elide: Text.ElideRight; text: modelData.name; font.pixelSize: 12; color: bt.cFg; anchors.verticalCenter: parent.verticalCenter }
                Rectangle {
                    anchors.right: pv.left; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 60; height: 6; radius: 3
                    color: bt.cTrack
                    Rectangle { width: parent.width * Math.max(0.03, modelData.pct / 100); height: parent.height; radius: 3; color: modelData.pct <= 15 ? bt.cWarn : bt.cMain }
                }
                T { id: pv; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; kind: "number"; text: modelData.pct + "%"; font.pixelSize: 12; font.bold: true; color: bt.cFg; width: 36; horizontalAlignment: Text.AlignRight }
            }
        }
    }
}
