// services/CaffeineService.qml
//
// Keeps the session awake. IdleService checks this flag directly, which is
// the only thing that works reliably: Hyprland honours idle-inhibit only on
// toplevel windows, so an invisible layer-shell inhibitor (what caelestia
// uses) is silently ignored, and the previous `systemd-inhibit sleep
// infinity` was invisible to the compositor altogether.
import QtQuick
import Quickshell
import Quickshell.Io

pragma Singleton

Singleton {
    id: service

    property alias active: props.active

    function toggle() { active = !active; }
    function enable() { active = true; }
    function disable() { active = false; }

    // Survives a shell reload, not a restart -- a stuck inhibitor after a
    // crash is worse than having to press the button again.
    PersistentProperties {
        id: props
        reloadableId: "zenithCaffeine"
        property bool active: false
    }

    onActiveChanged: {
        notifyProc.command = ["notify-send", "-a", "Caffeine", "-i", "preferences-desktop-screensaver",
            active ? "Caffeine Enabled" : "Caffeine Disabled",
            active ? "Idle lock, screen off and sleep are held off." : "Idle handling is back to normal."];
        notifyProc.running = true;
    }

    Process { id: notifyProc }
}
