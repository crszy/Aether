pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Hyprland
import Quickshell.Io
import Quickshell.Wayland
import qs.components.misc
import qs.services

Scope {
    id: root

    property alias lock: lock

    // Mix & match: the lock key/IPC locks with Noctalia's lock screen instead
    function lockNow(): void {
        if (WinSettings.mixLock === "caelestia" || !WinSettings.callPart("lock", WinSettings.mixLock))
            lock.locked = true;
    }

    WlSessionLock {
        id: lock

        useWindowsLock: WinSettings.useWindowsLock
        // lock box on one monitor (the chosen one, else the focused one); the others show the live desktop
        liveOthers: WinSettings.lockLiveOthers
        mainScreen: WinSettings.lockScreen || (Quickshell.screens.find(s => Hyprland.monitorFor(s) === Hypr.focusedMonitor)?.name ?? "")

        LockSurface {
            lock: lock
            pam: pam
        }
    }

    Pam {
        id: pam

        lock: lock
    }

    Loader {
        asynchronous: true
        active: true
        onLoaded: active = false

        // Force a load of a screencopy so the one in the lock works
        // My guess is the ICC backend loads async on first request, which if the lock is
        // the first request it fails to capture (because it's async and the compositor
        // refuses capture when locked)
        sourceComponent: ScreencopyView {
            captureSource: Quickshell.screens[0]
        }
    }

    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        name: "lock"
        description: "Lock the current session"
        onPressed: root.lockNow()
    }

    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        name: "unlock"
        description: "Unlock the current session"
        onPressed: lock.unlock()
    }

    IpcHandler {
        function lock(): void {
            root.lockNow();
        }

        function unlock(): void {
            lock.unlock();
        }

        function isLocked(): bool {
            return lock.locked;
        }

        target: "lock"
    }
}
