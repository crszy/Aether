import QtQuick
import Quickshell
import Quickshell.Io
import Caelestia
import Caelestia.Config
import Caelestia.I18n
import Caelestia.State
import Quickshell.Windows as Win
import qs.services

Scope {
    id: root

    // Aether: on by itself while a game / fullscreen app has focus (WinSettings.autoGameMode). Then only the
    // costly looks go (animations, blur, shadows) - gaps and rounding stay, so other windows do not move; the
    // shells drop their own heavy parts (eg. a video wallpaper) while RuntimeState.gameMode is on.
    property bool autoOn: false
    // what it turns off follows Settings > Windows extras (WinSettings.gm*)
    readonly property var autoOptions: {
        const o = {};
        if (WinSettings.gmWmEffects) {
            o["animations:enabled"] = 0;
            o["decoration:blur:enabled"] = 0;
            o["decoration:shadow:enabled"] = 0;
        }
        if (WinSettings.gmBorders)
            o["general:border_size"] = 0;
        return o;
    }

    // the audio visualizer's sound capture (WASAPI loopback) stops while a game runs, and comes back after
    property bool visualizerWasOn: false
    readonly property bool stopVisualizer: WinSettings.gameStopVisualizer
    onStopVisualizerChanged: {
        {
            if (root.stopVisualizer) {
                root.visualizerWasOn = Win.AudioVisualizer.running;
                Win.AudioVisualizer.running = false;
            } else if (root.visualizerWasOn) {
                Win.AudioVisualizer.running = true;
                root.visualizerWasOn = false;
            }
        }
    }

    function followFullscreen(): void {
        const fullscreen = Win.WindowManager.fullscreenApp;
        if (fullscreen && WinSettings.autoGameMode && !RuntimeState.gameMode) {
            root.autoOn = true;
            RuntimeState.gameMode = true;
        } else if (!fullscreen && root.autoOn) {
            RuntimeState.gameMode = false;
            root.autoOn = false;
        }
    }

    Connections {
        function onFullscreenAppChanged(): void {
            root.followFullscreen();
        }

        target: Win.WindowManager
    }

    function applyGameMode(): void {
        const options = root.autoOn ? root.autoOptions : GlobalConfig.utilities.gameMode.options;
        if (Object.keys(options).length > 0)
            Hypr.extras.applyOptions(options);
    }

    // on/off in quick succession (a fullscreen window flickering, a screenshot overlay) collapses into the last state:
    // the window manager gets one change, so an "on" can never arrive after the "off" and leave animations off
    property bool pendingEnableDisable: false
    Timer {
        id: settle

        interval: 150
        onTriggered: root.applyState(root.pendingEnableDisable)
    }

    function syncState(enableDisable: bool): void {
        root.pendingEnableDisable = root.pendingEnableDisable || enableDisable;
        settle.restart();
    }

    property bool applied: false

    function applyState(enableDisable: bool): void {
        root.pendingEnableDisable = false;
        if (RuntimeState.gameMode === root.applied && enableDisable)
            return; // flipped and flipped back: nothing changed
        root.applied = RuntimeState.gameMode;
        if (RuntimeState.gameMode) {
            root.applyGameMode();
            if (GlobalConfig.utilities.toasts.gameModeChanged && !root.autoOn)
                Toaster.toast(Tr.tr("Game mode enabled"), Tr.tr("Disabled Hyprland animations, blur, gaps and shadows"), "gamepad");
        } else if (enableDisable) {
            Hypr.extras.message("reload");
            if (GlobalConfig.utilities.toasts.gameModeChanged && !root.autoOn)
                Toaster.toast(Tr.tr("Game mode disabled"), Tr.tr("Hyprland settings restored"), "gamepad");
        }
    }

    Component.onCompleted: {
        // Aether: game mode that "Game mode by itself" turned on is saved like a manual one; if Aether stopped while
        // it was on, it came back on with nothing left to turn it off (animations, blur and shadows stayed off for
        // good). With the automatic mode on and nothing fullscreen focused, it starts off.
        root.clearStaleAuto();
        syncState(false);
        root.followFullscreen();
    }

    function clearStaleAuto(): void {
        if (WinSettings.loaded && RuntimeState.gameMode && WinSettings.autoGameMode && !Win.WindowManager.fullscreenApp
                && !root.autoOn) {
            RuntimeState.gameMode = false;
            // the window manager may still have animations off from that earlier game mode: restore its settings
            Hypr.extras.message("reload");
        }
    }

    property bool staleChecked: false
    Connections {
        target: WinSettings
        function onLoadedChanged(): void {
            if (!root.staleChecked && WinSettings.loaded) {
                root.staleChecked = true;
                root.clearStaleAuto();
            }
        }
    }

    Connections {
        function onGameModeChanged(): void {
            root.syncState(true);
        }

        target: RuntimeState
    }

    Connections {
        function onConfigReloaded(): void {
            if (RuntimeState.gameMode)
                root.applyGameMode();
        }

        target: Hypr
    }

    IpcHandler {
        function isEnabled(): bool {
            return RuntimeState.gameMode;
        }

        function toggle(): void {
            RuntimeState.gameMode = !RuntimeState.gameMode;
        }

        function enable(): void {
            RuntimeState.gameMode = true;
        }

        function disable(): void {
            RuntimeState.gameMode = false;
        }

        target: "gameMode"
    }
}
