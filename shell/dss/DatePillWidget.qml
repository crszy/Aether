// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import QtQuick

// Desktop widget "Date & time": a slim pill with the day and the date, the time in a chip of your theme color.
Item {
    id: dp
    property var pal: null
    property real bg: 1
    property bool running: true
    property var now: new Date()
    property bool h12: false
    Timer { interval: 1000; running: dp.running; repeat: true; triggeredOnStart: true; onTriggered: dp.now = new Date() }
    Trigger { path: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/rice/region.json"; every: 10000; onContent: function(t) { dp.h12 = /"clock": *"12"/.test(t) } }
    function two(n) { return n < 10 ? "0" + n : "" + n }
    readonly property string day: Qt.locale().dayName(now.getDay(), Locale.ShortFormat)
    readonly property string date: two(now.getDate()) + "/" + two(now.getMonth() + 1)
    readonly property string time: h12 ? ((now.getHours() + 11) % 12 + 1) + ":" + two(now.getMinutes()) + (now.getHours() < 12 ? " AM" : " PM")
                                       : two(now.getHours()) + ":" + two(now.getMinutes())
    readonly property color acc: pal ? pal.primary : "#9ec9bd"

    Card { anchors.fill: parent; k: dp.bg; pal: dp.pal; radius: height / 2; property real blurRadius: height / 2 }
    // as wide as what it shows (shell.qml sizes the widget by this)
    readonly property real wantW: dpRow.width + 40
    Row {
        id: dpRow
        anchors.verticalCenter: parent.verticalCenter
        x: 20; spacing: 12
        T { anchors.verticalCenter: parent.verticalCenter; text: dp.day + ", " + dp.date; font.pixelSize: 16; font.weight: Font.DemiBold; color: dp.pal ? dp.pal.fg : "#F6F5F4" }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: tt.implicitWidth + 26; height: dp.height - 16; radius: height / 2
            color: dp.acc
            T { id: tt; anchors.centerIn: parent; kind: "number"; text: dp.time; font.pixelSize: 15; font.weight: Font.DemiBold; color: dp.pal ? dp.pal.inkOn : "#10201c" }
        }
        // the seconds, as a dot that breathes
        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 6; height: 6; radius: 3; color: dp.acc
            opacity: dp.now.getSeconds() % 2 ? 0.35 : 1; Behavior on opacity { NumberAnimation { duration: 600 } } }
    }
}
