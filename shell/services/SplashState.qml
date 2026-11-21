pragma Singleton

// Loading screen state: shown when the shell starts or reloads, for WinSettings.splashMs.
// The cover itself is drawn by modules/splash/SplashCover.qml inside each screen's drawers window.
// qs ipc call splash show   (a preview)
import QtQuick
import Quickshell
import Quickshell.Io

Singleton {
    id: root

    property bool showing: WinSettings.splashEnabled
    property bool lingering: false      // keeps the cover while it fades out

    function show(): void {
        lingering = false;
        showing = true;
        timer.restart();
    }

    // the Aether splash ends itself when its finale is done (finish()); the timer is its safety net
    readonly property bool selfTimed: WinSettings.splashStyle === "aether"

    function finish(): void {
        if (!root.showing)
            return;
        root.lingering = true;
        root.showing = false;
        linger.restart();
    }

    Timer {
        id: timer

        running: root.showing
        interval: root.selfTimed ? Math.max(300, WinSettings.splashMs) + 9000 : Math.max(300, WinSettings.splashMs)
        onTriggered: root.finish()
    }

    Timer {
        id: linger

        interval: 500
        onTriggered: root.lingering = false
    }

    IpcHandler {
        target: "splash"

        function show(): void {
            root.show();
        }

        // `qs ipc call splash show` never reaches show(): the CLI reads "show" as its own `ipc show` command
        function preview(): void {
            root.show();
        }
    }
}
