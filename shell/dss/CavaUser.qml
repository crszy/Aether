// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// Put one in anything that shows the music: while active is true, the shared cava (CavaFeed) runs for it.
QtObject {
    property bool active: false
    property bool held: false
    function sync() { if (active !== held) { held = active; CavaFeed.users += active ? 1 : -1 } }
    onActiveChanged: sync()
    Component.onCompleted: sync()
    Component.onDestruction: if (held) CavaFeed.users = Math.max(0, CavaFeed.users - 1)
}
