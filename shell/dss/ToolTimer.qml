// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import QtQuick

// Toolbox > Timer: focus for a while, then a break (Pomodoro). It keeps counting while the toolbox is closed and
// tells you with a notification and a sound when the time is up.
Item {
    id: tm
    property var pal
    // the opening wave (Tools.qml): 0 .. 1; each part comes a moment after the one above it
    property real enter: 1
    function e(i) { const p = Math.max(0, Math.min(1, (enter - i * 0.07) / 0.6)); return 1 - Math.pow(1 - p, 4) }
    property bool active: false
    property string mode: "focus"            // focus | break
    property int focusMin: 25
    property int breakMin: 5
    property bool going: false
    property real endAt: 0
    property int sessions: 0
    readonly property int total: (mode === "focus" ? focusMin : breakMin) * 60
    property int secs: total
    function now() { return Date.now() / 1000 }
    function two(n) { return n < 10 ? "0" + n : "" + n }
    readonly property string summary: going ? (mode === "focus" ? Tr.t("Focus") : Tr.t("Break")) + "  ·  " + two(Math.floor(secs / 60)) + ":" + two(secs % 60) : Tr.t("Focus, then a break")
    function start() { endAt = now() + secs; going = true }
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
    onTotalChanged: if (!going) secs = total
    Timer { interval: 250; running: tm.going; repeat: true
        onTriggered: { tm.secs = Math.max(0, Math.ceil(tm.endAt - tm.now())); if (tm.secs <= 0) tm.finish() } }

    Column {
        width: parent.width; spacing: 22
        // Focus | Break: one pill that slides under the chosen one
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 240; height: 44; radius: 22; color: tm.pal.card
            opacity: tm.e(0); transform: Translate { y: (1 - tm.e(0)) * 22 }
            Rectangle {
                width: 116; height: 36; radius: 18; y: 4; color: tm.pal.primary
                x: tm.mode === "focus" ? 4 : 120
                Behavior on x { NumberAnimation { duration: 320; easing.type: Easing.OutBack; easing.overshoot: 1.3 } }
            }
            Repeater {
                model: [{ k: "focus", n: "Focus" }, { k: "break", n: "Break" }]
                delegate: Item {
                    required property var modelData
                    required property int index
                    x: 4 + index * 116; y: 4; width: 116; height: 36
                    Text { anchors.centerIn: parent; text: Tr.t(parent.modelData.n); font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.DemiBold
                        color: tm.mode === parent.modelData.k ? tm.pal.inkPrimary : tm.pal.fg
                        Behavior on color { ColorAnimation { duration: 200 } } }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.play("click"); tm.pick(parent.modelData.k) } }
                }
            }
        }
        // the ring: what is left, a wave while it runs
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 250; height: 250
            opacity: tm.e(1); transform: Translate { y: (1 - tm.e(1)) * 22 }
            Rectangle { anchors.centerIn: parent; width: 196; height: 196; radius: 98; color: tm.pal.card
                scale: tm.going ? 1 : 0.96
                Behavior on scale { NumberAnimation { duration: 420; easing.type: Easing.OutBack; easing.overshoot: 1.6 } } }
            WavyArc { anchors.fill: parent; value: tm.total > 0 ? tm.secs / tm.total : 0; moving: tm.going && tm.active
                wavy: Look.wavy && tm.going; thick: 8; color: tm.mode === "focus" ? tm.pal.primary : "#8fd0a0"; track: tm.pal.track
                Behavior on value { NumberAnimation { duration: 300 } } }
            Column {
                anchors.centerIn: parent; spacing: 2
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: tm.two(Math.floor(tm.secs / 60)) + ":" + tm.two(tm.secs % 60)
                    color: tm.pal.fg; font.family: Fonts.numbers; font.pixelSize: 46; font.weight: Font.DemiBold }
                Text { anchors.horizontalCenter: parent.horizontalCenter; color: tm.pal.dim; font.family: Fonts.main; font.pixelSize: 12
                    text: tm.going ? (tm.mode === "focus" ? Tr.t("Stay with it") : Tr.t("Rest a little")) : Tr.t("Ready") }
            }
        }
        // start / pause (the big one) and reset
        Row {
            anchors.horizontalCenter: parent.horizontalCenter; spacing: 14
            opacity: tm.e(2); transform: Translate { y: (1 - tm.e(2)) * 22 }
            ToolBtn { pal: tm.pal; h: 52; anchors.verticalCenter: parent.verticalCenter; icon: "restart_alt"; glyph: String.fromCodePoint(0xF0709); onClicked: tm.reset() }
            Rectangle {
                width: tm.going ? 120 : 96; height: 72; radius: tm.going ? 24 : 36
                Behavior on width { NumberAnimation { duration: 320; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
                Behavior on radius { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                color: pm.containsMouse ? Qt.lighter(tm.pal.primary, 1.07) : tm.pal.primary
                scale: pm.pressed ? 0.94 : 1
                Behavior on scale { NumberAnimation { duration: 140 } }
                MIcon { anchors.centerIn: parent; size: 32; name: tm.going ? "pause" : "play_arrow"; glyph: String.fromCodePoint(tm.going ? 0xF03E4 : 0xF040A); color: tm.pal.inkPrimary }
                MouseArea { id: pm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { Sfx.play("select"); if (tm.going) tm.pause(); else tm.start() } }
            }
            ToolBtn { pal: tm.pal; h: 52; anchors.verticalCenter: parent.verticalCenter; icon: "skip_next"; glyph: String.fromCodePoint(0xF04AD); onClicked: tm.finish() }
        }
        // how long
        Column {
            width: parent.width; spacing: 8
            opacity: tm.e(3); transform: Translate { y: (1 - tm.e(3)) * 22 }
            Text { text: tm.mode === "focus" ? Tr.t("Focus for") : Tr.t("Break for"); color: tm.pal.dim; font.family: Fonts.main; font.pixelSize: 12 }
            Row {
                spacing: 8
                Repeater {
                    model: tm.mode === "focus" ? [15, 25, 45, 60] : [5, 10, 15, 20]
                    delegate: ToolBtn { required property int modelData; pal: tm.pal; h: 36; label: modelData + " min"
                        on: (tm.mode === "focus" ? tm.focusMin : tm.breakMin) === modelData
                        onClicked: { if (tm.mode === "focus") tm.focusMin = modelData; else tm.breakMin = modelData; if (!tm.going) tm.secs = tm.total } }
                }
            }
        }
        // sessions done today (since the shell started)
        Row {
            spacing: 6
            visible: tm.sessions > 0
            Repeater { model: Math.min(tm.sessions, 12); delegate: Rectangle { width: 10; height: 10; radius: 5; color: tm.pal.primary } }
            Text { text: "  " + tm.sessions + " " + Tr.t("sessions done"); color: tm.pal.dim; font.family: Fonts.main; font.pixelSize: 12 }
        }
    }
}
