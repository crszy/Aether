// Windows (Aether): which parts of Ryoku Aether's Mix & match picked, so Ryoku can be mixed with other shells part by
// part (e.g. DMS's bar with Ryoku's wallpaper and dock). Aether's RyokuHost writes
// <XDG_RUNTIME_DIR>/aether-parts.json; Ryoku's shell.qml shows only the parts set here (ryoku-port.js wires them).
// Without the file (Ryoku run on its own) every part is on.
pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

Singleton {
    id: root

    readonly property string path: `${String(Quickshell.env("XDG_RUNTIME_DIR") || "").replace(/\\/g, "/")}/aether-parts.json`
    property bool known: false

    readonly property bool bar: !known || adapter.bar
    readonly property bool wallpaper: !known || adapter.wallpaper
    readonly property bool widgets: !known || adapter.widgets
    readonly property bool dock: !known || adapter.dock
    readonly property bool notifs: !known || adapter.notifs
    readonly property bool osd: !known || adapter.osd
    readonly property bool launcher: !known || adapter.launcher

    FileView {
        path: Quickshell.env("XDG_RUNTIME_DIR") ? root.path : ""
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.known = true
        onLoadFailed: root.known = false

        JsonAdapter {
            id: adapter

            property bool bar: true
            property bool wallpaper: true
            property bool widgets: true
            property bool dock: true
            property bool notifs: true
            property bool osd: true
            property bool launcher: true
        }
    }
}
