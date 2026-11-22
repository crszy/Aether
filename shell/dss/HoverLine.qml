// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// A short line under a module of the bar while you point at it (it grows out from the middle).
Rectangle {
    property bool on: false
    property real full: 16
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    y: parent ? Math.round(parent.height / 2 + 13) : 0
    height: 2; radius: 1
    width: on ? full : 4
    color: "#F6F5F4"
    opacity: on ? 0.9 : 0
    Behavior on width { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
    Behavior on opacity { NumberAnimation { duration: 160 } }
}
