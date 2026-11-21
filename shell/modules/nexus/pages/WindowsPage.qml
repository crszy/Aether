pragma ComponentBehavior: Bound

// Settings -> Windows: everything that only exists on Windows, plus where every config file lives.
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import Quickshell
import Quickshell.Io
import Caelestia.Config
import Caelestia.I18n
import qs.components
import qs.components.controls
import qs.services
import qs.utils
import qs.modules.nexus.common

PageBase {
    id: root

    title: Tr.tr("Windows")

    readonly property string pkg: WinSettings.packageDir
    readonly property string launcher: pkg ? `${pkg}/Caelestia.exe` : ""
    readonly property string qsExe: pkg ? `${pkg}/qs/qs.exe` : ""
    readonly property string localAppData: (Quickshell.env("LOCALAPPDATA") || "").replace(/\\/g, "/")
    readonly property list<MenuItem> backdropItems: [
        MenuItem { text: Tr.tr("Blur (stays blurred when not focused)"); value: "blur" },
        MenuItem { text: Tr.tr("Acrylic (flat when not focused)"); value: "acrylic" },
        MenuItem { text: Tr.tr("Mica"); value: "mica" },
        MenuItem { text: Tr.tr("Tabbed"); value: "tabbed" },
        MenuItem { text: Tr.tr("None"); value: "none" }
    ]
    readonly property list<MenuItem> notifScreenItems: [
        MenuItem { text: Tr.tr("Focused monitor"); value: "active" },
        MenuItem { text: Tr.tr("Main monitor"); value: "primary" },
        MenuItem { text: Tr.tr("Every monitor"); value: "all" }
    ]
    readonly property string runKey: "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"
    property bool startsWithWindows

    function win(path: string): string {
        return path.replace(/\//g, "\\");
    }

    function openPath(path: string): void {
        Qt.openUrlExternally("file:///" + path.replace(/\\/g, "/"));
    }

    function folderOf(path: string): string {
        return path.replace(/\\/g, "/").replace(/\/[^/]*$/, "");
    }

    // (PageBase's children go into its scroll area, which only takes Items - helpers live in properties)
    // ---- start with Windows: a Run entry that starts Caelestia.exe ----
    readonly property Process runQuery: Process {
        running: true
        command: ["reg.exe", "query", root.runKey, "/v", "Caelestia"]
        onExited: code => root.startsWithWindows = code === 0
    }

    readonly property Process runSet: Process {
        onExited: root.runQuery.running = true
    }

    readonly property FolderDialog wallDialog: FolderDialog {
        title: Tr.tr("Wallpaper folder")
        currentFolder: "file:///" + Paths.wallsdir
        onAccepted: GlobalConfig.paths.wallpaperDir = decodeURIComponent(selectedFolder.toString().replace(/^file:\/{2,3}/, ""))
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: root.cappedWidth
        spacing: Tokens.spacing.extraSmall / 2

        // ---- Windows taskbar ----
        SectionHeader {
            first: true
            text: Tr.tr("Windows taskbar")
        }

        ToggleRow {
            first: true
            last: true
            text: Tr.tr("Hide the Windows taskbar")
            subtext: Tr.tr("Caelestia's bar takes over; the Windows taskbar only slides in when the mouse touches its edge. It comes back when Caelestia quits.")
            checked: WinSettings.hideTaskbar
            onToggled: WinSettings.hideTaskbar = checked
        }

        // ---- administrator ----
        SectionHeader {
            text: Tr.tr("Administrator windows")
        }

        ToggleRow {
            first: true
            last: true
            text: Tr.tr("Let Caelestia control administrator windows")
            subtext: Tr.tr("Runs Caelestia as administrator (Windows asks once when it starts) so it can tile, move and focus admin apps too. Apps you open from Caelestia still start as normal apps. Takes effect next start.")
            checked: WinSettings.runAsAdmin
            onToggled: WinSettings.runAsAdmin = checked
        }

        // ---- Waybar ----
        SectionHeader {
            text: Tr.tr("Waybar")
        }

        ToggleRow {
            first: true
            last: true
            text: Tr.tr("Show a Waybar")
            subtext: Tr.tr("A Waybar-compatible bar: config.jsonc and style.css in %1").arg(`${Paths.home}/.config/waybar`.replace(/\//g, "\\"))
            checked: WinSettings.waybar
            onToggled: WinSettings.waybar = checked
        }

        // ---- Startup ----
        SectionHeader {
            text: Tr.tr("Startup")
        }

        ToggleRow {
            first: true
            last: true
            text: Tr.tr("Start Caelestia when you sign in")
            subtext: root.launcher ? root.win(root.launcher) : Tr.tr("Start Caelestia with Caelestia.exe to use this")
            enabled: root.launcher.length > 0
            checked: root.startsWithWindows
            onToggled: root.runSet.exec(checked
                ? ["reg.exe", "add", root.runKey, "/v", "Caelestia", "/t", "REG_SZ", "/d", `"${root.win(root.launcher)}"`, "/f"]
                : ["reg.exe", "delete", root.runKey, "/v", "Caelestia", "/f"])
        }

        // ---- Lock screen ----
        SectionHeader {
            text: Tr.tr("Lock screen")
        }

        ToggleRow {
            first: true
            text: Tr.tr("Use Caelestia's lock screen")
            subtext: WinSettings.useWindowsLock
                ? Tr.tr("Off: locking shows the Windows lock screen")
                : Tr.tr("Drawn over the desktop: Ctrl+Alt+Del still reaches Windows, so for the strongest lock turn this off.")
            checked: !WinSettings.useWindowsLock
            onToggled: WinSettings.lockStyle = checked ? "caelestia" : "windows"
        }

        ToggleRow {
            text: Tr.tr("Show your desktop behind the lock")
            subtext: GlobalConfig.lock.useWallpaper
                ? Tr.tr("Off: your wallpaper is shown behind the lock")
                : Tr.tr("A blurred picture of the screen as it was when you locked")
            checked: !GlobalConfig.lock.useWallpaper
            onToggled: GlobalConfig.lock.useWallpaper = !checked
        }

        StepperRow {
            label: Tr.tr("Background blur")
            subtext: Tr.tr("0 = sharp, 100 = only colours")
            value: Math.round(WinSettings.lockBlur * 100)
            from: 0
            to: 100
            stepSize: 5
            onMoved: v => WinSettings.lockBlur = v / 100
        }

        ToggleRow {
            text: Tr.tr("Live desktop on the other monitors")
            subtext: WinSettings.lockLiveOthers
                ? Tr.tr("The lock box shows on one monitor; the others stay locked (no clicks or keys get through) but show what is going on")
                : Tr.tr("Off: every monitor shows the lock")
            checked: WinSettings.lockLiveOthers
            onToggled: WinSettings.lockLiveOthers = checked
        }

        StepperRow {
            label: Tr.tr("Live monitor dimming")
            subtext: Tr.tr("How much the live monitors are darkened while locked")
            value: Math.round(WinSettings.lockLiveDim * 100)
            from: 0
            to: 90
            stepSize: 5
            onMoved: v => WinSettings.lockLiveDim = v / 100
        }

        TextFieldRow {
            id: pinRow

            label: WinSettings.hasPin ? Tr.tr("Change lock PIN") : Tr.tr("Set a lock PIN")
            subtext: WinSettings.hasPin
                ? Tr.tr("The lock asks for your Caelestia PIN. Type a new one and press Enter to change it.")
                : Tr.tr("No PIN: the lock asks for your Windows password (with no Windows password, Enter unlocks).")
            placeholderText: WinSettings.hasPin ? "••••" : Tr.tr("new PIN")
            smallField: true
            Component.onCompleted: field.echoMode = TextInput.Password
            onEditingFinished: v => {
                if (v.length > 0) {
                    WinSettings.setPin(v);
                    pinRow.clear();
                }
            }
        }

        RowButton {
            visible: WinSettings.hasPin
            icon: "lock_open"
            text: Tr.tr("Remove the PIN")
            subtext: Tr.tr("Go back to unlocking with the Windows password")
            onClicked: WinSettings.clearPin()
        }

        RowButton {
            last: true
            icon: "lock"
            text: Tr.tr("Lock now")
            disabled: root.qsExe.length === 0
            onClicked: Quickshell.execDetached([root.qsExe, "ipc", "-p", Quickshell.shellPath("shell.qml"), "call", "lock", "lock"])
        }

        // ---- unlocking, title bars, pop-ups, bar colour, Settings style (were on the removed Mix & match page) ----
        WindowsExtras {}

        // ---- Settings window ----
        SectionHeader {
            text: Tr.tr("Settings window")
        }

        ToggleRow {
            first: true
            text: Tr.tr("Always on top")
            subtext: Tr.tr("Stays above every app; tilers (komorebi, Hyprland mode) never move or resize it")
            checked: WinSettings.nexusTopmost
            onToggled: WinSettings.nexusTopmost = checked
        }

        StepperRow {
            label: Tr.tr("Background opacity")
            subtext: Tr.tr("Lower = more see-through")
            value: Math.round(WinSettings.nexusOpacity * 100)
            from: 20
            to: 100
            stepSize: 5
            onMoved: v => WinSettings.nexusOpacity = v / 100
        }

        SelectRow {
            last: true
            label: Tr.tr("Background effect")
            subtext: Tr.tr("What Windows draws behind the see-through parts")
            menuItems: root.backdropItems
            active: root.backdropItems.find(i => i.value === WinSettings.nexusBackdrop)
            onSelected: item => WinSettings.nexusBackdrop = item.value
        }

        // ---- Notifications ----
        SectionHeader {
            text: Tr.tr("Notifications")
        }

        SelectRow {
            first: true
            last: true
            label: Tr.tr("Show pop-ups on")
            subtext: Tr.tr("Focused monitor = the one you were using when it came in")
            menuItems: root.notifScreenItems
            active: root.notifScreenItems.find(i => i.value === WinSettings.notifScreens)
            onSelected: item => WinSettings.notifScreens = item.value
        }

        // ---- You ----
        SectionHeader {
            text: Tr.tr("You")
        }

        TextFieldRow {
            first: true
            label: Tr.tr("Your name")
            subtext: Tr.tr("What every shell calls you (greetings, lock screen, fetch, Ryoku). Empty = your Windows account. Applies after Aether restarts.")
            placeholderText: Tr.tr("eg. fora")
            value: WinSettings.displayName
            onEditingFinished: v => WinSettings.displayName = v.trim()
        }

        // ---- Wallpapers ----
        SectionHeader {
            text: Tr.tr("Wallpapers")
        }

        TextFieldRow {
            first: true
            label: Tr.tr("Wallpaper folder")
            subtext: Tr.tr("Pictures in here (and its sub-folders) show up in the wallpaper picker")
            value: root.win(Paths.wallsdir)
            onEditingFinished: v => {
                if (v.trim().length) GlobalConfig.paths.wallpaperDir = v.trim().replace(/\\/g, "/");
            }
        }

        RowButton {
            icon: "folder_open"
            text: Tr.tr("Choose folder…")
            onClicked: root.wallDialog.open()
        }

        RowButton {
            last: true
            icon: "open_in_new"
            text: Tr.tr("Open wallpaper folder")
            onClicked: root.openPath(Paths.wallsdir)
        }

        // ---- Config files ----
        SectionHeader {
            text: Tr.tr("Config files")
        }

        Repeater {
            model: [
                { icon: "folder_special", name: Tr.tr("Your config folder (one file per part + README)"), path: Paths.config, folder: true },
                { icon: "tune", name: Tr.tr("Caelestia settings"), path: `${Paths.appConfig}/shell.json`, fallback: "{}\n" },
                { icon: "straighten", name: Tr.tr("Sizes and animations"), path: `${Paths.appConfig}/shell-tokens.json`, fallback: "{}\n" },
                { icon: "view_agenda", name: Tr.tr("Taskbar apps"), path: `${Paths.config}/taskbar.json`, fallback: "" },
                { icon: "desktop_windows", name: Tr.tr("Windows options (this page)"), path: WinSettings.path, fallback: "" },
                { icon: "keyboard", name: Tr.tr("Keyboard shortcuts"), path: `${root.localAppData}/caelestia/keybinds.json`, fallback: "{\n  \"extends\": \"defaults\",\n  \"binds\": [\n  ]\n}\n" },
                { icon: "keyboard_alt", name: Tr.tr("Default shortcuts (read-only reference)"), path: Quickshell.shellPath("defaults/windows/keybinds.json"), fallback: "" },
                { icon: "palette", name: Tr.tr("Current colour scheme"), path: `${Paths.state}/scheme.json`, fallback: "" },
                { icon: "wallpaper", name: Tr.tr("Current wallpaper"), path: `${Paths.state}/wallpaper/path.txt`, fallback: "" },
                { icon: "terminal", name: Tr.tr("Colour / wallpaper tool settings"), path: `${Paths.config}/cli.json`, fallback: "{}\n" },
                { icon: "monitor", name: Tr.tr("Per-monitor settings folder"), path: `${Paths.appConfig}/monitors`, folder: true },
                { icon: "dock_to_bottom", name: Tr.tr("Waybar config"), path: `${Paths.home}/.config/waybar/config.jsonc`, fallback: "" },
                { icon: "format_paint", name: Tr.tr("Waybar style"), path: `${Paths.home}/.config/waybar/style.css`, fallback: "" },
                { icon: "code", name: Tr.tr("Shell (QML) files"), path: Quickshell.shellDir, folder: true },
                { icon: "article", name: Tr.tr("Logs"), path: `${root.localAppData}/quickshell/run`, folder: true }
            ]

            RowButton {
                required property var modelData
                required property int index

                first: index === 0
                last: index === 14
                icon: modelData.icon
                text: modelData.name
                subtext: root.win(modelData.path)
                trailingIcon: "open_in_new"

                FileView {
                    id: fv

                    path: modelData.folder ? "" : modelData.path
                    printErrors: false
                    blockLoading: true
                }

                onClicked: {
                    if (modelData.folder) {
                        root.openPath(modelData.path);
                    } else if (!fv.loaded && modelData.fallback) {
                        fv.setText(modelData.fallback);   // create it so there is something to open
                        Qt.callLater(() => root.openPath(modelData.path));
                    } else if (fv.loaded) {
                        root.openPath(modelData.path);
                    } else {
                        root.openPath(root.folderOf(modelData.path));
                    }
                }
            }
        }

        // ---- Caelestia ----
        SectionHeader {
            text: Tr.tr("Caelestia")
        }

        RowButton {
            first: true
            icon: "refresh"
            text: Tr.tr("Reload the shell")
            subtext: Tr.tr("Re-reads every QML file and setting")
            onClicked: Quickshell.reload(true)
        }

        RowButton {
            icon: "restart_alt"
            text: Tr.tr("Restart Caelestia")
            disabled: root.launcher.length === 0
            onClicked: Quickshell.execDetached([root.launcher, "--restart"])
        }

        RowButton {
            last: true
            icon: "power_settings_new"
            text: Tr.tr("Quit Caelestia")
            subtext: Tr.tr("Start it again from Caelestia.exe or the Start menu")
            disabled: root.launcher.length === 0
            onClicked: Quickshell.execDetached([root.launcher, "--stop"])
        }
    }
}
