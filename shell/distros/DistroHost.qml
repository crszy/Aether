import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as Win
import qs.services as Cae
import "catalog.js" as Catalog
import "gnome"

// Linux distro looks (distros/catalog.js). windows.json "distro": "debian" = the whole desktop is that distro
// (Caelestia's frame, bar, launcher, notifications, OSD and wallpaper are not loaded); "mixBar": "debian" alone =
// only its bar. Commands: qs ipc call distro overview | apps | monitor | power
Scope {
    id: root

    readonly property bool full: Cae.WinSettings.distroMode
    readonly property var distro: Catalog.get(full ? Cae.WinSettings.distro : Cae.WinSettings.mixBar)
    readonly property bool gnome: distro?.family === "gnome"

    property string overview: ""   // "" | "windows" | "apps"
    property bool monitor
    property string power: ""   // "" = closed, "choose" = the four-button card, "restart" / "poweroff" / "logout"
    property string settings: ""   // "" = closed, else the page to open
    // Digital Wellbeing counts screen time while the whole GNOME desktop is on (referencing it starts it)
    readonly property bool wellbeing: full && gnome && GnomeWellbeing.ready
    // Night Light (Settings > Displays), only while the GNOME desktop is on; turning it off here hands the screen back
    readonly property bool nightLight: full && gnome && Cae.WinSettings.loaded && Cae.WinSettings.distroNightLight
    onNightLightChanged: nightLight ? Win.NightLight.enable(Cae.WinSettings.distroNightTemp) : Win.NightLight.disable()

    function command(cmd: string): void {
        if (cmd === "overview")
            overview = overview ? "" : "windows";
        else if (cmd === "apps")
            overview = overview === "apps" ? "" : "apps";
        else if (cmd === "monitor")
            monitor = true;
        else if (cmd.startsWith("settings"))
            settings = cmd.split(":")[1] || "wifi";
        else if (cmd.startsWith("power"))
            power = cmd.split(":")[1] || "choose";
    }

    Component.onCompleted: if (distro) {
        Adw.accent = Cae.WinSettings.distroAccent || distro.accent;
        Adw.distroWallpaper = distro.wallpaper ? Quickshell.shellPath("distros/" + distro.wallpaper) : "";
    }

    Connections {
        target: Cae.WinSettings
        function onDistroCommand(cmd: string): void {
            root.command(cmd);
        }
        function onDistroNightTempChanged(): void {
            if (root.nightLight)
                Win.NightLight.enable(Cae.WinSettings.distroNightTemp);
        }
    }

    IpcHandler {
        target: "distro"
        function overview(): void { root.command("overview"); }
        function apps(): void { root.command("apps"); }
        function monitor(): void { root.command("monitor"); }
        function power(): void { root.command("power"); }
        function settings(page: string): void { root.command("settings:" + page); }
    }

    Variants {
        model: root.full && root.gnome ? Quickshell.screens : []
        GnomeWallpaper {
            required property var modelData
            screen: modelData
            distro: root.distro
            onSettingsRequested: page => root.command("settings:" + page)
        }
    }

    Variants {
        model: root.gnome ? Quickshell.screens : []
        GnomeTopBar {
            required property var modelData
            screen: modelData
            distro: root.distro
            overviewOpen: root.overview !== ""
            onOverviewRequested: root.command("overview")
            onPowerRequested: action => root.command("power:" + action)
            onMonitorRequested: root.command("monitor")
            onSettingsRequested: page => root.command("settings:" + page)
        }
    }

    LazyLoader {
        active: root.gnome && root.overview !== ""
        GnomeOverview {
            distro: root.distro
            startApps: root.overview === "apps"
            onDismissed: root.overview = ""
        }
    }

    LazyLoader {
        active: root.full && root.gnome
        GnomeBanners {}
    }

    LazyLoader {
        active: root.full && root.gnome
        GnomeOsd {}
    }

    LazyLoader {
        active: root.gnome && root.power !== ""
        GnomePowerDialog {
            action: root.power === "choose" ? "" : root.power
            onDismissed: root.power = ""
        }
    }

    Variants {   // the taskbar (Dash to Dock) on the main monitor
        model: root.full && root.gnome ? Quickshell.screens.filter(s => Cae.WinSettings.isMainScreen(s)) : []
        GnomeDock {
            required property var modelData
            screen: modelData
            distro: root.distro
            onAppsRequested: root.command("apps")
        }
    }

    LazyLoader {
        active: root.gnome && root.settings !== ""
        GnomeSettings {
            page: root.settings
            onDismissed: root.settings = ""
        }
    }

    LazyLoader {
        active: root.monitor
        GnomeSystemMonitor {
            onDismissed: root.monitor = false
        }
    }
}
