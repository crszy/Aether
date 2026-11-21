pragma Singleton
import QtQuick
import Quickshell

// Windows stand-in for Quickshell.I3 (i3/Sway IPC, not built on Windows). DMS only uses it when it runs
// on Sway (CompositorService.isSway), which never happens here.
Singleton {
    readonly property var workspaces: ({ values: [] })
    function dispatch(command) {}
}
