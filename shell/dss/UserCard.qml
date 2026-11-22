// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import QtQuick

Item {
    id: uc
    property real bg: 1   // card background opacity (widget editor)
    property var pal: null
    property date now: new Date()
    readonly property string user: Quickshell.env("USER")
    readonly property string face: "file:///" + ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.face"
    function greeting() {
        const h = now.getHours()
        return h < 5 ? "Good Night" : (h < 12 ? "Good Morning" : (h < 18 ? "Good Afternoon" : "Good Evening"))
    }
    Timer { interval: 30000; running: true; repeat: true; onTriggered: uc.now = new Date() }

    Card { anchors.fill: parent; k: uc.bg; pal: uc.pal }

    RoundImage {
        id: av
        x: 18
        anchors.verticalCenter: parent.verticalCenter
        width: 76
        height: 76
        cornerRadius: 20
        source: uc.face
    }
    Column {
        anchors.left: av.right
        anchors.leftMargin: 16
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3
        T { kind: "title"; text: Tr.t("Hi, ") + uc.user + "!"; font.pixelSize: 18; font.bold: true; width: parent.width; elide: Text.ElideRight; color: uc.pal ? uc.pal.fg : "#F6F5F4" }
        T { text: uc.greeting(); font.pixelSize: 14; color: uc.pal ? uc.pal.fg2 : "#c8c8cc" }
        T {
            text: Tr.t("Today · ") + uc.now.toLocaleDateString(Tr.loc, "dddd d MMM")
            font.pixelSize: 11
            color: uc.pal ? uc.pal.fg3 : "#8a8a8f"
        }
    }
}
