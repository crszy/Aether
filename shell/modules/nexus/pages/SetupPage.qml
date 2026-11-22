pragma ComponentBehavior: Bound

// Settings -> Setup: questions about how you want the shell; every answer is written straight into the
// config files (shell.json through Caelestia's config, windows.json, Waybar's config.jsonc) and applies live.
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Caelestia.Config
import Caelestia.I18n
import qs.components
import qs.services
import qs.utils
import qs.modules.waybar
import qs.modules.nexus.common
import Quickshell.Windows as Win

PageBase {
    id: root

    title: Tr.tr("Setup")

    // ---- the whole look: one shell for every part (Settings > Mix & match mixes them) ----
    readonly property var looks: [
        { id: "aether", name: "Aether", sub: Tr.tr("A piece of every shell in one colour scheme, with Aether's own lock and pop-up"),
          mix: { mixBar: "caelestia", mixLauncher: "dms", mixNotifs: "end4", mixOsd: "user:AetherPill", mixLock: "user:AetherLock", mixWallpaper: "caelestia", mixDock: "dms", mixDesktopWidgets: "off" }, also: { splashStyle: "aether", splashEnabled: true, titleBars: "themed" }, scheme: "aether" },
        { id: "caelestia", name: "Caelestia", sub: Tr.tr("Side bar, drawers that grow out of it, dashboard"),
          mix: { mixBar: "caelestia", mixLauncher: "caelestia", mixNotifs: "caelestia", mixOsd: "caelestia", mixLock: "caelestia", mixWallpaper: "caelestia", mixDock: "off", mixDesktopWidgets: "off" } },
        { id: "end4", name: "end-4 · illogical-impulse", sub: Tr.tr("Top bar, sidebars, overview"),
          mix: { mixBar: "end4", mixLauncher: "end4", mixNotifs: "end4", mixOsd: "end4", mixLock: "caelestia", mixWallpaper: "caelestia", mixDock: "off", mixDesktopWidgets: "off" } },
        { id: "dms", name: "DankMaterialShell", sub: Tr.tr("Top bar, dock, control centre, desktop widgets"),
          mix: { mixBar: "dms", mixLauncher: "dms", mixNotifs: "dms", mixOsd: "dms", mixLock: "caelestia", mixWallpaper: "dms", mixDock: "dms", mixDesktopWidgets: "dms" } },
        { id: "noctalia", name: "Noctalia", sub: Tr.tr("Top bar, dock, lots of widgets"),
          mix: { mixBar: "noctalia", mixLauncher: "noctalia", mixNotifs: "noctalia", mixOsd: "noctalia", mixLock: "caelestia", mixWallpaper: "noctalia", mixDock: "noctalia", mixDesktopWidgets: "noctalia" } },
        { id: "zenith", name: "Zenith", sub: Tr.tr("Dynamic island bar, focus tools"),
          mix: { mixBar: "zenith", mixLauncher: "zenith", mixNotifs: "zenith", mixOsd: "zenith", mixLock: "caelestia", mixWallpaper: "caelestia", mixDock: "off", mixDesktopWidgets: "off" } },
        { id: "ryoku", name: "Ryoku", sub: Tr.tr("Top bar, Stage desktop, dock, control sidebar"),
          mix: { mixBar: "ryoku", mixLauncher: "ryoku", mixNotifs: "ryoku", mixOsd: "ryoku", mixLock: "ryoku", mixWallpaper: "ryoku", mixDock: "ryoku", mixDesktopWidgets: "ryoku" } }
    ]

    function lookActive(l: var): bool {
        return Object.keys(l.mix).every(k => WinSettings[k] === l.mix[k]);
    }

    function applyLook(l: var): void {
        for (const k of Object.keys(l.mix))
            WinSettings[k] = l.mix[k];
        for (const k of Object.keys(l.also ?? {}))
            WinSettings[k] = l.also[k];
        if (l.scheme)   // every shell that follows the colour scheme (Caelestia, end-4, DMS, Zenith dynamic) matches
            Quickshell.execDetached(["caelestia", "scheme", "set", "-n", l.scheme]);
    }

    // ---- which bar ----
    readonly property string barStyle: !WinSettings.waybar ? "caelestia" : (GlobalConfig.bar.persistent ? "both" : "waybar")

    function setBarStyle(s: string): void {
        WinSettings.waybar = s !== "caelestia";
        GlobalConfig.bar.persistent = s !== "waybar";     // Waybar only: the side bar hides until you reach the edge
        GlobalConfig.bar.showOnHover = true;
    }

    // ---- Caelestia bar entries ----
    function entryIndex(id: string): int {
        const v = GlobalConfig.bar.entries.values;
        for (let i = 0; i < v.length; i++)
            if (v[i].id === id) return i;
        return -1;
    }

    function entryOn(id: string): bool {
        const i = entryIndex(id);
        return i >= 0 && GlobalConfig.bar.entries.values[i].enabled;
    }

    function setEntry(id: string, on: bool, after: string): void {
        const i = entryIndex(id);
        if (i >= 0) {
            GlobalConfig.bar.entries.at(i).enabled = on;
        } else if (on) {
            const a = entryIndex(after);
            GlobalConfig.bar.entries.insert({ id, enabled: true }, a >= 0 ? a + 1 : -1);
        }
    }

    // ---- Waybar's config.jsonc ----
    readonly property var waybar: {
        if (!wbFile.loaded) return null;
        try { return WaybarStyle.parseConfig(wbFile.text())[0]; } catch (e) { return null; }
    }

    function writeWaybar(change: var): void {
        let bars;
        try { bars = WaybarStyle.parseConfig(wbFile.text()); } catch (e) { return; }
        change(bars[0]);
        wbFile.setText("// Waybar for Windows (Caelestia) - written by Settings > Setup; edit freely, it reloads on save.\n"
                       + JSON.stringify(bars.length === 1 ? bars[0] : bars, null, 4) + "\n");
    }

    function waybarHas(mod: string): bool {
        const w = root.waybar;
        return !!w && ["modules-left", "modules-center", "modules-right"].some(k => (w[k] ?? []).includes(mod));
    }

    function setWaybarModule(mod: string, on: bool, where: string): void {
        writeWaybar(w => {
            for (const k of ["modules-left", "modules-center", "modules-right"])
                w[k] = (w[k] ?? []).filter(m => m !== mod);
            if (on) w[where] = (w[where] ?? []).concat([mod]);
        });
    }

    readonly property FileView wbFile: FileView {
        path: `${Paths.home}/.config/waybar/config.jsonc`
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: root.cappedWidth
        spacing: Tokens.spacing.extraSmall / 2

        StyledText {
            Layout.fillWidth: true
            Layout.bottomMargin: Tokens.spacing.medium
            text: Tr.tr("Answer what you like - each choice is saved to the right config file straight away.")
            color: Colours.palette.m3onSurfaceVariant
            wrapMode: Text.WordWrap
        }

        // ---------------- the whole look ----------------
        SectionHeader {
            first: true
            text: Tr.tr("Which shell should your desktop look like?")
        }

        Repeater {
            model: root.looks

            ShellRow {
                required property var modelData
                required property int index

                first: index === 0
                last: index === root.looks.length - 1
                shell: modelData.id
                text: modelData.name
                subtext: modelData.sub
                trailingIcon: root.lookActive(modelData) ? "check_circle" : "radio_button_unchecked"
                onClicked: root.applyLook(modelData)
            }
        }

        // what the picked look really looks like (the shell's own screenshot)
        ClipRect {
            readonly property var picked: root.looks.find(l => root.lookActive(l))
            readonly property string shot: picked ? [".jpg", ".png"].map(e => Quickshell.shellPath(`modules/rice/looks/${picked.id}${e}`)).find(p => Win.FsUtils.classify(p) === "file") ?? "" : ""

            visible: shot !== ""
            Layout.fillWidth: true
            Layout.topMargin: Tokens.spacing.small
            Layout.preferredHeight: width * 9 / 16
            radius: Tokens.rounding.large

            Image {
                anchors.fill: parent
                source: parent.shot ? `file:///${parent.shot}` : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 1280
            }
        }

        // ---------------- other apps' title bars ----------------
        SectionHeader {
            text: Tr.tr("Title bars on your apps")
        }

        Repeater {
            model: [
                { id: "system", icon: "web_asset", name: Tr.tr("As Windows draws them"), sub: "" },
                { id: "themed", icon: "format_color_fill", name: Tr.tr("In your colours"), sub: Tr.tr("Flat, coloured like the shell drawing your bar") },
                { id: "dms", icon: "web_asset", name: Tr.tr("DankMaterialShell's"), sub: Tr.tr("DMS's own window header (title in the middle, round buttons) on your apps") },
                { id: "discord", icon: "forum", name: Tr.tr("Discord-style"), sub: Tr.tr("Discord's dark strip with its thin square / X buttons on your apps") },
                { id: "kde", icon: "desktop_windows", name: Tr.tr("KDE Plasma (Breeze)"), sub: Tr.tr("Plasma's Breeze window header, app icon at the left, like on Arch with KDE") },
                { id: "strip", icon: "fullscreen", name: Tr.tr("Removed"), sub: Tr.tr("Like Hyprland - move windows with Super+drag; apps that draw their own keep it") }
            ]

            RowButton {
                required property var modelData
                required property int index

                first: index === 0
                last: index === 5
                icon: modelData.icon
                text: modelData.name
                subtext: modelData.sub
                trailingIcon: WinSettings.titleBars === modelData.id ? "check_circle" : "radio_button_unchecked"
                onClicked: WinSettings.titleBars = modelData.id
            }
        }

        // ---------------- the bar ----------------
        SectionHeader {
            text: Tr.tr("Which bar do you want?")
        }

        Repeater {
            model: [
                { id: "caelestia", icon: "dock_to_left", name: Tr.tr("Caelestia's side bar"), sub: Tr.tr("The vertical bar on the left (Caelestia's own)") },
                { id: "waybar", icon: "dock_to_bottom", name: Tr.tr("A Waybar top bar"), sub: Tr.tr("Waybar-style bar from ~\\.config\\waybar - the side bar only shows when you touch the left edge") },
                { id: "both", icon: "view_quilt", name: Tr.tr("Both"), sub: Tr.tr("Caelestia's side bar and a Waybar") }
            ]

            RowButton {
                required property var modelData
                required property int index

                first: index === 0
                last: index === 2
                icon: modelData.icon
                text: modelData.name
                subtext: modelData.sub
                trailingIcon: root.barStyle === modelData.id ? "check_circle" : "radio_button_unchecked"
                onClicked: root.setBarStyle(modelData.id)
            }
        }

        SectionHeader {
            visible: WinSettings.waybar
            text: Tr.tr("Where should the Waybar go?")
        }

        Repeater {
            model: WinSettings.waybar ? [
                { id: "top", icon: "border_top", name: Tr.tr("Top") },
                { id: "bottom", icon: "border_bottom", name: Tr.tr("Bottom") },
                { id: "left", icon: "border_left", name: Tr.tr("Left") },
                { id: "right", icon: "border_right", name: Tr.tr("Right") }
            ] : []

            RowButton {
                required property var modelData
                required property int index

                first: index === 0
                last: index === 3
                icon: modelData.icon
                text: modelData.name
                trailingIcon: (root.waybar?.position ?? "top") === modelData.id ? "check_circle" : "radio_button_unchecked"
                onClicked: root.writeWaybar(w => w.position = modelData.id)
            }
        }

        SectionHeader {
            text: Tr.tr("What should the bar show?")
        }

        ToggleRow {
            first: true
            text: Tr.tr("Your open apps")
            subtext: Tr.tr("App icons for every open window, like a taskbar (click to switch, middle-click to close)")
            checked: root.entryOn("apps") || root.waybarHas("wlr/taskbar")
            onToggled: {
                root.setEntry("apps", checked, "workspaces");
                if (root.waybar) root.setWaybarModule("wlr/taskbar", checked, "modules-left");
            }
        }

        ToggleRow {
            text: Tr.tr("Workspaces")
            checked: GlobalConfig.bar.workspaces.shown > 0 && root.entryOn("workspaces")
            onToggled: root.setEntry("workspaces", checked, "logo")
        }

        ToggleRow {
            text: Tr.tr("The title of the window you are in")
            checked: root.entryOn("activeWindow")
            onToggled: root.setEntry("activeWindow", checked, "apps")
        }

        ToggleRow {
            text: Tr.tr("Tray icons")
            checked: root.entryOn("tray")
            onToggled: root.setEntry("tray", checked, "activeWindow")
        }

        ToggleRow {
            text: Tr.tr("The date under the clock")
            checked: GlobalConfig.bar.clock.showDate
            onToggled: GlobalConfig.bar.clock.showDate = checked
        }

        ToggleRow {
            last: true
            text: Tr.tr("Hide the Windows taskbar")
            subtext: Tr.tr("It slides back in when the mouse touches the bottom edge")
            checked: WinSettings.hideTaskbar
            onToggled: WinSettings.hideTaskbar = checked
        }

        // ---------------- launcher ----------------
        SectionHeader {
            text: Tr.tr("Launcher")
        }

        ToggleRow {
            first: true
            text: Tr.tr("Fuzzy search for apps")
            subtext: Tr.tr("Finds apps even when you mistype (\"frfx\" finds Firefox)")
            checked: GlobalConfig.launcher.useFuzzy.apps
            onToggled: GlobalConfig.launcher.useFuzzy.apps = checked
        }

        ToggleRow {
            text: Tr.tr("Open it by touching the screen edge")
            checked: GlobalConfig.launcher.showOnHover
            onToggled: GlobalConfig.launcher.showOnHover = checked
        }

        StepperRow {
            last: true
            label: Tr.tr("Results shown")
            value: GlobalConfig.launcher.maxShown
            from: 3
            to: 20
            onMoved: v => GlobalConfig.launcher.maxShown = v
        }

        // ---------------- dashboard ----------------
        SectionHeader {
            text: Tr.tr("Dashboard")
        }

        ToggleRow {
            first: true
            text: Tr.tr("Open it by touching the top edge")
            checked: GlobalConfig.dashboard.showOnHover
            onToggled: GlobalConfig.dashboard.showOnHover = checked
        }

        ToggleRow {
            text: Tr.tr("Media player")
            checked: GlobalConfig.dashboard.showMedia
            onToggled: GlobalConfig.dashboard.showMedia = checked
        }

        ToggleRow {
            text: Tr.tr("CPU, memory and disk")
            checked: GlobalConfig.dashboard.showPerformance
            onToggled: GlobalConfig.dashboard.showPerformance = checked
        }

        ToggleRow {
            last: true
            text: Tr.tr("Weather")
            checked: GlobalConfig.dashboard.showWeather
            onToggled: GlobalConfig.dashboard.showWeather = checked
        }

        // ---------------- lock ----------------
        SectionHeader {
            text: Tr.tr("Lock screen")
        }

        ToggleRow {
            first: true
            text: Tr.tr("Use Caelestia's lock screen")
            checked: !WinSettings.useWindowsLock
            onToggled: WinSettings.lockStyle = checked ? "caelestia" : "windows"
        }

        ToggleRow {
            text: Tr.tr("Show your desktop behind it")
            checked: !GlobalConfig.lock.useWallpaper
            onToggled: GlobalConfig.lock.useWallpaper = !checked
        }

        StepperRow {
            label: Tr.tr("Background blur")
            value: Math.round(WinSettings.lockBlur * 100)
            from: 0
            to: 100
            stepSize: 5
            onMoved: v => WinSettings.lockBlur = v / 100
        }

        ToggleRow {
            last: true
            text: Tr.tr("Hide notifications on the lock screen")
            checked: GlobalConfig.lock.hideNotifs
            onToggled: GlobalConfig.lock.hideNotifs = checked
        }

        // ---------------- where it went ----------------
        SectionHeader {
            text: Tr.tr("Where your answers are saved")
        }

        InfoRow {
            first: true
            label: Tr.tr("Bar, launcher, dashboard, lock")
            value: `${Paths.config}/shell.json`.replace(/\//g, "\\")
        }

        InfoRow {
            label: Tr.tr("Windows options")
            value: WinSettings.path.replace(/\//g, "\\")
        }

        InfoRow {
            last: true
            label: Tr.tr("Waybar")
            value: `${Paths.home}/.config/waybar`.replace(/\//g, "\\")
        }
    }
}
