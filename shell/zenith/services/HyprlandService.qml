import QtQuick
import Quickshell
import Quickshell.Hyprland

pragma Singleton

Item {
    id: root

    property int _trigger: 0
    function trigger() { _trigger++; }

    Connections {
        target: Hyprland
        function onFocusedWorkspaceChanged() { root.trigger(); }
        function onRawEvent(event) {
            if (event.name === "activewindow" || event.name === "fullscreen") {
                root.trigger();
            }
        }
    }

    // Robust fullscreen detection
    readonly property bool isFullscreen: {
        _trigger; // Force re-evaluation on compositor events
        
        // 1. Track active window's direct fullscreen state
        const win = Hyprland.activeWindow;
        if (win && win.fullscreen) return true;

        // 2. Check workspaces for fullscreen flag
        // We look at all workspaces and see if the one that is currently active 
        // on its monitor has a fullscreen window.
        const workspaces = Hyprland.workspaces.values;
        for (let i = 0; i < workspaces.length; i++) {
            const ws = workspaces[i];
            if (ws && ws.hasFullscreen) {
                // Check if this workspace is the active one on its monitor
                const mon = ws.monitor;
                if (mon && mon.activeWorkspace === ws) {
                    return true;
                }
            }
        }
        
        // 3. Size-based fallback for apps that don't report fullscreen flags
        if (win) {
            const mon = Hyprland.monitorFor(win.monitor);
            if (mon && mon.width > 0 && mon.height > 0) {
                // Check if window fills the monitor (with a small 15px margin)
                const isFullSize = Math.abs(win.width - mon.width) < 15 && 
                                   Math.abs(win.height - mon.height) < 15;
                if (isFullSize) return true;
            }
        }

        return false;
    }

    // Optional debug log
    /*
    onIsFullscreenChanged: {
    }
    */
}
