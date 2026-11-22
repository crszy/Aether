// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import QtQuick

// A panel that is only made when it is asked for (its command file in ~/.cache/rice changes), and let go again
// after it has been closed for a while: nothing of it sits in memory from the start (Settings > Desktop >
// Performance). The panel's own Trigger has `fresh: true`, so it still gets the line that made it.
//   LazyPanel { name: "cc-open"; ControlCenter { nf: notifs } }
Scope {
    id: lp
    property string name: ""
    property int keep: 120000                  // closed this long (ms): it is let go
    default property Component panel
    readonly property var item: ld.item
    Trigger { name: lp.name; onFired: function(o) { ld.active = true; idle.restart() } }
    LazyLoader { id: ld; active: false; component: lp.panel }
    Timer {
        id: idle; interval: lp.keep; repeat: true; running: ld.active
        onTriggered: { const it = ld.item; if (it && it.open !== true && it.visible !== true) ld.active = false }
    }
}
