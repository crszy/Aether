import QtQuick
import Quickshell
import Quickshell.Io
import Caelestia
import Caelestia.Config
import qs.components.misc
import qs.services
import qs.modules.aetherbar as AB
import qs.modules.nexus
import qs.modules.rice
import qs.modules.aethersettings as AS

Scope {
    id: root

    property bool launcherInterrupted
    readonly property bool hasFullscreen: Hypr.focusedWorkspace?.toplevels.values.some(t => t.lastIpcObject.fullscreen > 1) ?? false

    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        name: "nexus"
        description: "Open nexus"
        onPressed: WindowFactory.create()
    }

    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        // always Aether's own settings (opens on Setup), whichever shell's settings window the style picks
        name: "nexusAether"
        description: "Open Aether's own settings (Setup)"
        onPressed: WindowFactory.create(null, { caelestia: true, startPage: "Setup" })
    }

    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        name: "showall"
        description: "Toggle launcher, dashboard and osd"
        onPressed: {
            if (root.hasFullscreen)
                return;
            const v = ShellState.forActive();
            v.launcher = v.dashboard = v.osd = v.utilities = !(v.launcher || v.dashboard || v.osd || v.utilities);
        }
    }

    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        name: "dashboard"
        description: "Toggle dashboard"
        onPressed: {
            if (root.hasFullscreen)
                return;
            const screenState = ShellState.forActive();
            screenState.dashboard = !screenState.dashboard;
        }
    }

    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        name: "session"
        description: "Toggle session menu"
        onPressed: {
            if (root.hasFullscreen)
                return;
            if (WinSettings.distroMode) {   // a whole distro: its own power dialog
                WinSettings.distroCommand("power");
                return;
            }
            // your own session menu (windows.json "sessionMenu": "user:<Name>") replaces Caelestia's
            const mine = UserExt.userName(WinSettings.sessionMenu);
            if (mine) {
                UserExt.togglePanel(mine);
                return;
            }
            const screenState = ShellState.forActive();
            screenState.session = !screenState.session;
        }
    }

    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        name: "launcher"
        description: "Toggle launcher"
        onPressed: root.launcherInterrupted = false
        onReleased: {
            if (!root.launcherInterrupted && !root.hasFullscreen) {
                if (WinSettings.distroMode) {   // a whole distro: its overview (GNOME Activities ...)
                    WinSettings.distroCommand("overview");
                    root.launcherInterrupted = false;
                    return;
                }
                const screenState = ShellState.forActive();
                screenState.launcher = !screenState.launcher;
            }
            root.launcherInterrupted = false;
        }
    }

    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        name: "wallpicker"
        description: "Wallpaper picker (press again for the next one, hold to freeze)"
        onPressed: AB.AetherBarConfig.pickerPress()
        onReleased: AB.AetherBarConfig.pickerRelease()
    }

    // the old name, so a keybinds.json that still says caelestia:wallpaper opens the picker too
    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        name: "wallpaper"
        description: "Wallpaper picker"
        onPressed: AB.AetherBarConfig.pickerPress()
        onReleased: AB.AetherBarConfig.pickerRelease()
    }

    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        name: "launcherInterrupt"
        description: "Interrupt launcher keybind"
        onPressed: root.launcherInterrupted = true
    }

    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        name: "sidebar"
        description: "Toggle sidebar"
        onPressed: {
            if (root.hasFullscreen || !Config.sidebar.enabled)
                return;
            const screenState = ShellState.forActive();
            screenState.sidebar = !screenState.sidebar;
        }
    }

    // qmllint disable unresolved-type
    CustomShortcut {
        // qmllint enable unresolved-type
        name: "utilities"
        description: "Toggle utilities"
        onPressed: {
            if (root.hasFullscreen)
                return;
            const screenState = ShellState.forActive();
            screenState.utilities = !screenState.utilities;
        }
    }

    IpcHandler {
        function toggle(drawer: string): void {
            if (WinSettings.distroMode && (drawer === "launcher" || drawer === "session")) {
                WinSettings.distroCommand(drawer === "launcher" ? "overview" : "power");
                return;
            }
            if (list().split("\n").includes(drawer)) {
                if (root.hasFullscreen && ["launcher", "session", "dashboard"].includes(drawer))
                    return;
                const screenState = ShellState.forActive();
                screenState[drawer] = !screenState[drawer];
            } else {
                console.warn(lc, `Drawer "${drawer}" does not exist`);
            }
        }

        function list(): string {
            const screenState = ShellState.forActive();
            return Object.keys(screenState).filter(k => typeof screenState[k] === "boolean").join("\n");
        }

        function isOpen(drawer: string): string {
            const screenState = ShellState.forActive();
            if (typeof screenState[drawer] !== "boolean")
                return "unknown";
            return screenState[drawer] ? "1" : "0";
        }

        target: "drawers"
    }

    IpcHandler {
        function open(): void {
            WindowFactory.create();
        }

        function openPage(page: string): void {
            WindowFactory.create(null, { startPage: page });
        }

        // always Caelestia's Settings, whatever the Settings style is (the way back from Noctalia's)
        function openCaelestia(): void {
            WindowFactory.create(null, { caelestia: true, startPage: "Setup" });
        }

        target: "nexus"
    }

    // Aether Settings (all shells in one window): `qs ipc call aether settings`, `>aether` in the launchers.
    // Opens the new window (modules/aethersettings) unless Settings style is "rice" (Aether classic).
    IpcHandler {
        function settings(): void {
            if (WinSettings.settingsStyle === "rice")
                RiceFactory.open("");
            else
                AS.AetherSettings.open("");
        }

        // the store, your shells, configs and setup still live in the classic window
        function open(tab: string): void {
            if (WinSettings.settingsStyle === "rice" || ["store", "yours", "configs", "setup", "overview"].includes(tab))
                RiceFactory.open(tab);
            else
                AS.AetherSettings.open(tab);
        }

        function close(): void {
            if (AS.AetherSettings.current)
                AS.AetherSettings.current.close();
            if (RiceFactory.current)
                RiceFactory.current.visible = false;
        }

        // the Aether dashboard: home | music | lyrics | walls | system (same tab again closes it)
        function walls(): void { AB.AetherBarConfig.pickerTap(); }
        function dash(tab: string): void {
            AB.AetherBarConfig.toggleDash(tab || "home");
        }

        // AetherStore: open it and look inside a rice (link, or a folder / .zip on this PC)
        function store(url: string): void {
            RiceFactory.open("store");
            if (RiceFactory.current)
                RiceFactory.current.storeLook(url);
        }

        target: "aether"
    }

    IpcHandler {
        function info(title: string, message: string, icon: string): void {
            Toaster.toast(title, message, icon, Toast.Info);
        }

        function success(title: string, message: string, icon: string): void {
            Toaster.toast(title, message, icon, Toast.Success);
        }

        function warn(title: string, message: string, icon: string): void {
            Toaster.toast(title, message, icon, Toast.Warning);
        }

        function error(title: string, message: string, icon: string): void {
            Toaster.toast(title, message, icon, Toast.Error);
        }

        target: "toaster"
    }

    LoggingCategory {
        id: lc

        name: "caelestia.qml.shortcuts"
        defaultLogLevel: LoggingCategory.Info
    }
}
