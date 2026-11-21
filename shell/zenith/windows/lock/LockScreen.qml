// Session lock: one surface per screen, state and PAM in LockService.
// Driven by IdleService, the `zenith:lock` global shortcut and `launch.sh lock`.
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "../../services"

Scope {
    WlSessionLock {
        locked: LockService.locked
        LockSurface {}
    }

    Connections {
        target: IdleService
        function onLockRequested() { LockService.lock(); }
    }

    // The first screencopy in a session can come back empty if it is
    // requested while the screen is already locked, so one is warmed up here.
    Loader {
        asynchronous: true
        active: true
        onLoaded: active = false
        sourceComponent: ScreencopyView { captureSource: Quickshell.screens[0] }
    }

    IpcHandler {
        target: "zenith:lock"
        function lock(): void { LockService.lock(); }
        function unlock(): void { LockService.unlock(); }
        function isLocked(): bool { return LockService.locked; }
    }
}
