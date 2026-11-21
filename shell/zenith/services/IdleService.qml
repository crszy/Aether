// services/IdleService.qml
//
// Idle timeouts driven by the compositor's idle-notify protocol (what
// hypridle uses) with no extra daemon: lock, then screen off, then suspend,
// each gated on IdleSettings. Application idle inhibitors (a video playing
// fullscreen, a call) are respected by the compositor; Caffeine is checked
// here directly because Hyprland ignores inhibitors on layer-shell surfaces.
import QtQuick
import qs.shims
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Hyprland
import "../Settings"

pragma Singleton

Item {
    id: service

    signal lockRequested()

    readonly property bool inhibited:
        CaffeineService.active
        || (IdleSettings.inhibitWhenAudio && MediaPlayerService.isActuallyPlaying)
        || (IdleSettings.inhibitWhenCharging && BatteryService.acOnline)

    // Hyprland with a Lua config only accepts Lua dispatchers, a classic
    // config only classic ones; the wrong one is a harmless error.
    function dpms(on) {
        dpmsProc.command = ["sh", "-c",
            "hyprctl dispatch 'hl.dsp.dpms({ action = \"" + (on ? "enable" : "disable") + "\" })' >/dev/null 2>&1; " +
            "hyprctl dispatch dpms " + (on ? "on" : "off") + " >/dev/null 2>&1"];
        dpmsProc.running = true;
    }

    Process { id: dpmsProc }
    Process { id: suspendProc; command: ["cmd.exe", "/c", "exit", "0"] } // Windows: Windows' own sleep settings

    IdleMonitor {
        enabled: !service.inhibited && IdleSettings.lockTimeout > 0
        timeout: IdleSettings.lockTimeout
        respectInhibitors: true
        onIsIdleChanged: if (isIdle) service.lockRequested()
    }

    IdleMonitor {
        enabled: !service.inhibited && IdleSettings.screenOffTimeout > 0
        timeout: IdleSettings.screenOffTimeout
        respectInhibitors: true
        onIsIdleChanged: service.dpms(!isIdle)
    }

    IdleMonitor {
        enabled: !service.inhibited && IdleSettings.suspendTimeout > 0
        timeout: IdleSettings.suspendTimeout
        respectInhibitors: true
        onIsIdleChanged: if (isIdle) suspendProc.running = true
    }
}
