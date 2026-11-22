// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import QtQuick

// Focus timer (Pomodoro): a work session, then a short break. Click the play button; you get a notification when time is up.
Item {
    id: fo
    property real bg: 1       // card background opacity (widget editor)
    property var pal: null
    property bool running: true
    property int focusMin: 25            // set from the editor / desktop-set (wset focus focus N)
    signal setFocus(int minutes)

    property string mode: "focus"        // focus | break
    property bool going: false           // the clock is running
    property real endAt: 0               // when it ends (seconds)
    property int sessions: 0             // finished focus sessions
    readonly property int breakMin: Math.max(5, Math.round(focusMin / 5))
    readonly property int total: (mode === "focus" ? focusMin : breakMin) * 60
    property int secs: total
    readonly property real frac: total > 0 ? secs / total : 0
    readonly property color cFg: pal ? pal.fg : "#F6F5F4"
    readonly property color cFg2: pal ? pal.fg2 : "#c8c8cc"
    readonly property color cFg3: pal ? pal.fg3 : "#8a8a8f"
    readonly property color cMain: pal ? pal.primary : "#F6F5F4"

    function pad(n) { return n < 10 ? "0" + n : "" + n }
    function clock(s) { return pad(Math.floor(s / 60)) + ":" + pad(s % 60) }
    function now() { return Date.now() / 1000 }
    function start() { if (secs <= 0) secs = total; endAt = now() + secs; going = true }
    function pause() { secs = Math.max(0, Math.ceil(endAt - now())); going = false }
    function reset() { going = false; secs = total }
    function pick(m) { if (mode === m) return; going = false; mode = m; secs = total }
    function finish() {
        going = false
        const wasFocus = mode === "focus"
        if (wasFocus) sessions++
        DssWin.run(["notify-send", "-a", "DarkShadow's Shell", "-i", "alarm",
                                 wasFocus ? "Focus session done" : "Break is over",
                                 wasFocus ? "Time for a " + breakMin + " minute break." : "Ready for the next session?"])
        Sfx.play("done")
        mode = wasFocus ? "break" : "focus"
        secs = total
    }
    onFocusMinChanged: if (!going) secs = total
    Timer {
        interval: 250
        running: fo.going
        repeat: true
        onTriggered: { fo.secs = Math.max(0, Math.ceil(fo.endAt - fo.now())); if (fo.secs <= 0) fo.finish() }
    }

    Card { anchors.fill: parent; k: fo.bg; pal: fo.pal }

    // Focus | Break
    Row {
        id: tabs
        anchors.horizontalCenter: parent.horizontalCenter
        y: 18
        spacing: 6
        Repeater {
            model: [{ k: "focus", n: "Focus" }, { k: "break", n: "Break" }]
            delegate: Rectangle {
                width: 76; height: 28; radius: 14
                color: fo.mode === modelData.k ? fo.cMain : (fo.pal ? fo.pal.chip : Qt.rgba(1, 1, 1, 0.08))
                Behavior on color { ColorAnimation { duration: 160 } }
                T { anchors.centerIn: parent; text: modelData.n; font.pixelSize: 12; font.bold: fo.mode === modelData.k
                    color: fo.mode === modelData.k ? (fo.pal ? fo.pal.inkOn : "#0d0d0d") : fo.cFg2 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: fo.pick(modelData.k) }
            }
        }
    }

    // the ring
    Item {
        id: ring
        width: 150; height: 150
        anchors.horizontalCenter: parent.horizontalCenter
        y: 52
        // the time left, as a wavy ring that moves while it runs
        WavyArc { anchors.fill: parent; thick: 9; waves: 16; moving: fo.going && fo.running && !Look.covered
            property real v: fo.frac
            Behavior on v { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
            value: Math.max(0.004, v)
            color: fo.cMain; track: fo.pal ? fo.pal.track : Qt.rgba(1, 1, 1, 0.13) }
        Column {
            anchors.centerIn: parent
            spacing: 0
            T { anchors.horizontalCenter: parent.horizontalCenter; kind: "number"; text: fo.clock(fo.secs); font.pixelSize: 34; font.bold: true; color: fo.cFg }
            T { anchors.horizontalCenter: parent.horizontalCenter; text: fo.mode === "focus" ? Tr.t("stay with it") : Tr.t("rest a little"); font.pixelSize: 11; color: fo.cFg3 }
        }
        }

    // buttons
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 208
        spacing: 14
        Rectangle {
            width: 36; height: 36; radius: 18
            anchors.verticalCenter: parent.verticalCenter
            color: rm.containsMouse ? (fo.pal ? fo.pal.chipHover : Qt.rgba(1, 1, 1, 0.16)) : (fo.pal ? fo.pal.chip : Qt.rgba(1, 1, 1, 0.08))
            T { kind: "icon"; anchors.centerIn: parent; text: ""; font.pixelSize: 13; color: fo.cFg }
            MouseArea { id: rm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: fo.reset() }
        }
        Rectangle {
            width: 54; height: 54; radius: 27
            color: fo.cMain
            scale: pm.pressed ? 0.94 : 1
            Behavior on scale { NumberAnimation { duration: 90 } }
            T { kind: "icon"; anchors.centerIn: parent; anchors.horizontalCenterOffset: fo.going ? 0 : 2; text: fo.going ? "" : ""; font.pixelSize: 18; color: fo.pal ? fo.pal.inkOn : "#0d0d0d" }
            MouseArea { id: pm; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; property bool sfxOwn: true; onClicked: { Sfx.play(fo.going ? "switch-off" : "switch-on"); if (fo.going) fo.pause(); else fo.start() } }
        }
        Rectangle {
            width: 36; height: 36; radius: 18
            anchors.verticalCenter: parent.verticalCenter
            color: sm.containsMouse ? (fo.pal ? fo.pal.chipHover : Qt.rgba(1, 1, 1, 0.16)) : (fo.pal ? fo.pal.chip : Qt.rgba(1, 1, 1, 0.08))
            T { kind: "icon"; anchors.centerIn: parent; text: ""; font.pixelSize: 13; color: fo.cFg }
            MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { fo.going = false; fo.pick(fo.mode === "focus" ? "break" : "focus") } }
        }
    }

    // sessions done today (four dots) and the length of a session
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - 26
        spacing: 14
        Row {
            spacing: 5
            anchors.verticalCenter: parent.verticalCenter
            Repeater {
                model: 4
                delegate: Rectangle { width: 8; height: 8; radius: 4; color: index < fo.sessions % 4 || (fo.sessions > 0 && fo.sessions % 4 === 0) ? fo.cMain : (fo.pal ? fo.pal.track : Qt.rgba(1, 1, 1, 0.13)) }
            }
        }
        Row {
            spacing: 6
            anchors.verticalCenter: parent.verticalCenter
            opacity: fo.going ? 0.4 : 1
            T { text: "−"; font.pixelSize: 16; color: fo.cFg2; MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; enabled: !fo.going; onClicked: fo.setFocus(Math.max(5, fo.focusMin - 5)) } }
            T { text: fo.focusMin + Tr.t(" min"); font.pixelSize: 12; color: fo.cFg3; anchors.verticalCenter: parent.verticalCenter }
            T { text: "+"; font.pixelSize: 16; color: fo.cFg2; MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; enabled: !fo.going; onClicked: fo.setFocus(Math.min(120, fo.focusMin + 5)) } }
        }
    }
}
