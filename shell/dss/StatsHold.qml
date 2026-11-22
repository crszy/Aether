// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// Put this in a widget that shows the numbers of Stats.qml: rice-stats runs while at least one of them is out.
//   StatsHold { on: me.running }
Instantiator {
    property bool on: false
    model: on ? 1 : 0
    delegate: QtObject { Component.onCompleted: Stats.hold(); Component.onDestruction: Stats.release() }
}
