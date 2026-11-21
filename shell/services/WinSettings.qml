pragma Singleton

// Windows-only options of Caelestia for Windows, saved next to Caelestia's own shell.json as windows.json.
// Edit them in Settings -> Windows, or in the file itself (it reloads on save).
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Services.UPower
import Quickshell.Windows as Win
import Caelestia.Config
import qs.utils

Singleton {
    id: root

    readonly property string path: `${Paths.config}/windows.json`
    readonly property string packageDir: (Quickshell.env("CAELESTIA_PACKAGE_DIR") || "").replace(/\\/g, "/")
    // Ryoku's shell: your own copy ~/.config/quickshell/ryoku (Caelestia.exe makes and updates it), else the package's
    readonly property string ryokuUserShell: `${String(Quickshell.env("USERPROFILE") || "").replace(/\\/g, "/")}/.config/quickshell/ryoku/shell.qml`
    readonly property string ryokuShell: Win.FsUtils.classify(ryokuUserShell) === "file" ? ryokuUserShell : packageDir ? `${packageDir}/ryoku/shell/shell.qml` : ""

    property alias hideTaskbar: adapter.hideTaskbar          // hide Windows' taskbar; it peeks out on hover
    property alias lockStyle: adapter.lockStyle              // "caelestia" (this shell's lock screen) or "windows"
    property alias lockOnStart: adapter.lockOnStart          // (reserved)
    property alias lockBlur: adapter.lockBlur                // 0..1 blur of the desktop/wallpaper behind the lock
    property alias lockLiveOthers: adapter.lockLiveOthers    // lock box on one monitor; the others stay locked but show the live desktop
    property alias lockLiveDim: adapter.lockLiveDim          // 0..1 darkening over those live monitors
    property alias lockScreen: adapter.lockScreen            // monitor for the lock box ("" = the focused one)
    // Mix & match inside the lock: "auto" = any shell's lock shows the wallpaper of the shell picked under
    // Wallpaper (e.g. a DMS lock over Caelestia's wallpaper); "caelestia" = always Caelestia's wallpaper;
    // "own" = each lock keeps its own wallpaper
    property alias lockWallpaper: adapter.lockWallpaper
    // Mix & match pieces on Caelestia's lock: whose clock it shows - "caelestia" | "dms" | "end4" | "noctalia"
    property alias lockClock: adapter.lockClock
    property alias lockMedia: adapter.lockMedia              // whose media card: "caelestia" | "dms" | "end4" | "noctalia"
    // Mix & match bar pieces in the end-4, Noctalia and Zenith bars (at the start of their right side): ids like
    // "dms:CpuMonitor", "noctalia:Tray", "zenith:Resources" (Caelestia's own bar takes them in bar.json entries)
    property alias barPieces: adapter.barPieces
    // Settings window
    property alias nexusTopmost: adapter.nexusTopmost        // always above other windows (tilers leave it alone)
    property alias nexusOpacity: adapter.nexusOpacity        // 0.2..1 how solid its background is
    // Caelestia bar + screen frame colour: "" = the colour scheme's, else "#rrggbb" (keeps the scheme's transparency)
    property alias barColour: adapter.barColour
    property alias nexusBackdrop: adapter.nexusBackdrop      // "blur" | "acrylic" | "mica" | "tabbed" | "none"
    property alias configIntroShown: adapter.configIntroShown   // the first-start list of config files was closed
    property bool loaded: false                                  // windows.json was read (or written the first time)
    // Mix & match: which shell draws each part - "caelestia", "noctalia", "zenith", "dms", "end4" (built in), or "off"
    property alias mixBar: adapter.mixBar
    property alias mixNotifs: adapter.mixNotifs
    property alias mixOsd: adapter.mixOsd
    property alias mixLock: adapter.mixLock
    property alias mixWallpaper: adapter.mixWallpaper
    property alias mixLauncher: adapter.mixLauncher
    property alias mixDock: adapter.mixDock                  // "noctalia" | "dms" | "end4" | "off"
    property alias mixDesktopWidgets: adapter.mixDesktopWidgets  // "noctalia" | "dms" | "end4" | "off"
    // title bars of other apps: "system" (as Windows draws them), "themed" (kept, but flat in your Caelestia
    // colours - a Linux-style header that follows the wallpaper) or "strip" (removed, like Hyprland; apps that
    // draw their own title bar - browsers, Discord, VS Code, Terminal - are left alone). Move windows with
    // Super+drag; edges still resize. titleBarKeep: exe names that keep theirs ("notepad.exe")
    property alias titleBars: adapter.titleBars
    property alias titleBarKeep: adapter.titleBarKeep
    // app icon before the title in the shell's title bars: "auto" (as the style does it: KDE yes, DMS / Discord no) | "on" | "off"
    property alias titleBarIcon: adapter.titleBarIcon
    // header takes the app's own colour (text / buttons go light or dark to match), so it is part of the window
    property alias titleBarBlend: adapter.titleBarBlend
    // height of the shell's title bars: "style" (each style's own: DMS 34, Discord 30, KDE 30) | "compact" (as tall as
    // Windows' was) | "tall" (the style's + 8)
    property alias titleBarSize: adapter.titleBarSize
    readonly property int captionHeight: {
        const own = titleBars === "dms" ? 34 : titleBars === "discord" ? 30 : titleBars === "kde" ? 30 : 0;
        return titleBarSize === "compact" ? 0 : titleBarSize === "tall" ? own + 8 : own;
    }
    // laptops: "auto" pauses video wallpapers / video backgrounds while running on battery, "off" never does
    property alias batterySaver: adapter.batterySaver
    // set by Aether's installer on a fresh install: the first start opens the "Make it yours" setup
    property alias setupPending: adapter.setupPending
    // "themed" title bars take the colours of the shell drawing your bar (DMS sets these while its bar is on);
    // transparent = Caelestia's scheme
    property color titleBarTint: "transparent"
    property color titleBarTextTint: "transparent"
    readonly property bool saving: batterySaver === "auto" && UPower.onBattery
    property alias settingsStyle: adapter.settingsStyle      // the Settings window: "rice" (all shells) | "caelestia" | "noctalia"
    property alias riceBackground: adapter.riceBackground    // rice settings panel background (video/picture; "" = wallpaper)
    property alias riceDim: adapter.riceDim                  // 0..0.95 dimming over that background
    // Noctalia's bar/panels window is needed for its bar, launcher and control centre
    readonly property bool noctaliaPanels: mixBar === "noctalia" || mixLauncher === "noctalia"
    // Noctalia is loaded at all only when some part uses it
    readonly property bool noctaliaAny: noctaliaPanels || mixNotifs === "noctalia" || mixOsd === "noctalia" || mixLock === "noctalia"
        || mixWallpaper === "noctalia" || mixDock === "noctalia" || mixDesktopWidgets === "noctalia" || settingsStyle === "noctalia"

    // the other ported shells: loaded only while a part uses them
    function uses(shell: string): bool {
        return [mixBar, mixNotifs, mixOsd, mixLock, mixWallpaper, mixLauncher, mixDock, mixDesktopWidgets, settingsStyle].includes(shell);
    }
    readonly property bool zenithAny: uses("zenith")
    readonly property bool dmsAny: uses("dms") || titleBars === "dms"
    // title-bar styles drawn by the shell over apps (modules/captions/styles/<Name>Caption.qml)
    readonly property list<string> captionStyles: ["dms", "discord", "kde"]
    readonly property bool captionSkin: titleBars === "dms" || titleBars === "discord" || titleBars === "kde"
    readonly property bool end4Any: uses("end4")

    // how a Caelestia key reaches the shell that draws a part: [ipc target, function]
    readonly property var partIpc: ({
        launcher: { noctalia: ["launcher", "toggle"], zenith: ["zenith:menu", "launcher"], dms: ["dms:spotlight", "toggle"], end4: ["end4:search", "toggle"], ryoku: ["shell", "openSurface", ["", "launcher"], "ryoku"] },
        lock: { noctalia: ["lockScreen", "lock"], zenith: ["zenith:menu", "lock"], dms: ["dms:lock", "lock"], end4: ["end4:lock", "activate"] }
    })
    function callPart(part: string, shell: string): bool {
        if (part === "lock" && shell === "ryoku")   // Ryoku's qylock is its own Quickshell instance
            return RyokuHost.lock();
        const c = partIpc[part]?.[shell];
        if (!c)
            return false;
        ipc(c[0], c[1], c[2] ?? [], c[3] ?? "");
        return true;
    }

    // runs "qs ipc call <target> <fn> [args]" on this shell (how a Caelestia key reaches a Noctalia part)
    // instance "ryoku": Ryoku runs as its own Quickshell instance (services/RyokuHost.qml)
    // Popups (OSD, notifications, toasts, game mode...) go on the main monitor only, unless "Popups on all monitors"
    // is on. Windows' main monitor is the one at 0,0.
    function isMainScreen(screen: var): bool {
        if (!screen)
            return false;
        const main = Quickshell.screens.filter(s => s.x === 0 && s.y === 0);
        return screen === (main.length ? main[0] : Quickshell.screens[0]) || screen.name === (main.length ? main[0] : Quickshell.screens[0])?.name;
    }
    function alertsOn(screen: var): bool {
        return osdAllMonitors || isMainScreen(screen);
    }

    function ipc(target: string, fn: string, args: var, instance: string): void {
        const qs = packageDir ? `${packageDir}/qs/qs.exe` : "qs";
        const config = instance === "ryoku" && ryokuShell ? ryokuShell : Quickshell.shellPath("shell.qml");
        Quickshell.execDetached([qs, "ipc", "-p", config, "call", target, fn, ...(args ?? [])]);
    }

    // notifications
    property alias notifScreens: adapter.notifScreens        // "active" (the focused monitor) | "all" | "primary"
    property alias waybar: adapter.waybar                    // show the Waybar (config in ~\.config\waybar)
    // loading screen
    property alias splashEnabled: adapter.splashEnabled
    property alias splashStyle: adapter.splashStyle          // "aether" (the Aether splash) | "dots" | "logo" | "minimal"
    property alias aetherColours: adapter.aetherColours      // Aether style colours: "aether" (the splash purple) | "scheme" (your colour scheme)
    // your skin: ~/.config/caelestia/user/skins/<name>/ (style.json recolours Aether, settings-back/front.qml draw on
    // Aether Settings). "" = none
    property alias aetherSkin: adapter.aetherSkin
    // pieces Aether shows on Ryoku's bar (modules/userpanel/RyokuPieceHost.qml) and where
    property alias ryokuPieces: adapter.ryokuPieces
    property alias ryokuPiecesEdge: adapter.ryokuPiecesEdge
    property alias ryokuPiecesOffset: adapter.ryokuPiecesOffset
    property alias ryokuPiecesInset: adapter.ryokuPiecesInset
    // the session menu: "" = the shells' own; "user:<Name>" = your panels/<Name>.qml (example: AetherSession)
    property alias sessionMenu: adapter.sessionMenu
    // your mods (user/mods/<Name>.qml) that are switched off
    property alias modsOff: adapter.modsOff
    // the wallpaper picker: "" = Aether's strip; "user:<Name>" = your panels/<Name>.qml (example: AetherCoverflow)
    property alias wallpaperPicker: adapter.wallpaperPicker
    // which Quickshell config Caelestia.exe runs: ~/.config/quickshell/<name>/shell.qml ("aether" = Aether's own,
    // copied there on first start and yours to edit; any other folder there = another Quickshell config). Restart to apply.
    property alias shellConfig: adapter.shellConfig
    property alias splashMs: adapter.splashMs
    property alias splashText: adapter.splashText
    property alias splashTextColour: adapter.splashTextColour
    property alias splashBackground: adapter.splashBackground
    property alias splashColours: adapter.splashColours      // dot colours; empty = the scheme's
    // Settings window background
    property alias nexusFx: adapter.nexusFx                  // "aurora" | "particles" | "waves" | "none"
    property alias nexusFxStrength: adapter.nexusFxStrength  // 0..1
    property alias nexusFxSpeed: adapter.nexusFxSpeed        // 0.25..3
    // wallpaper
    property alias randomTransition: adapter.randomTransition   // a different transition every time
    // Hyprland mode: run Windows from ~/.config/hypr/hyprland.conf (tiling, borders, binds) - see HyprConf.qml
    property alias hyprland: adapter.hyprland
    // run the shell as administrator (read by Caelestia.exe at start) so it can control admin windows
    property alias runAsAdmin: adapter.runAsAdmin
    // the name every shell shows for you ($USER for the Linux shells); empty = the Windows account. Read by
    // quickshell-windows at start (launch.cpp), so it applies after Aether restarts
    property alias displayName: adapter.displayName
    // Aether tiling: the folder of komorebi.exe / komorebic.exe (the komorebi fork). Set = Caelestia starts komorebi
    // and whkd with itself and its bar shows komorebi's workspaces. Empty = off.
    property alias komorebi: adapter.komorebi
    property alias whkd: adapter.whkd
    // Hyprland (Rust): Aether tiling runs `komorebi --hyprland` - the komorebi fork reads ~/.config/hypr/hyprland.conf and
    // handles its keybinds itself (whkd is not started). Replaces the older Hyprland mode (HyprConf.qml), which turns off.
    property alias hyprlandWm: adapter.hyprlandWm
    // with it: every SUPER bind in hyprland.conf also works with ALT (SUPER keeps working; Windows keeps SUPER+L for locking)
    property alias hyprlandAlt: adapter.hyprlandAlt
    property alias osdAllMonitors: adapter.osdAllMonitors
    // game mode turns itself on while a game / fullscreen app has focus
    property alias autoGameMode: adapter.autoGameMode

    readonly property bool useWindowsLock: adapter.lockStyle === "windows"
    // lock PIN of this shell, stored only as PBKDF2-SHA256 (see Quickshell.Windows.PasswordCheck);
    // empty = the lock asks for the Windows password instead
    readonly property bool hasPin: adapter.pinHash.length > 0

    function setPin(pin: string): void {
        const salt = Win.PasswordCheck.newSalt();
        adapter.pinSalt = salt;
        adapter.pinHash = Win.PasswordCheck.hashPin(pin, salt);
        apply();
    }

    function clearPin(): void {
        adapter.pinHash = "";
        adapter.pinSalt = "";
        apply();
    }

    function apply(): void {
        Win.Taskbar.hoverOnly = adapter.hideTaskbar;
        Win.PasswordCheck.pinSalt = adapter.pinSalt;
        Win.PasswordCheck.pinHash = adapter.pinHash;
        // tiling only once windows.json is fully read: while it loads, each property arrives on its own and komorebi
        // was started before hyprlandWm had arrived (plain komorebi + whkd, then a restart that lost the race)
        if (!root.loaded)
            return;
        Win.WindowManager.whkd = adapter.whkd;
        Win.WindowManager.komorebiHyprlandAlt = adapter.hyprlandAlt;
        Win.WindowManager.komorebiHyprland = adapter.hyprlandWm; // before komorebi: it decides how komorebi starts
        Win.WindowManager.komorebi = adapter.komorebi;
    }

    // "a different wallpaper effect every time": after each change, pick the next one at random
    Connections {
        target: Wallpapers
        function onActualCurrentChanged(): void {
            if (adapter.randomTransition)
                GlobalConfig.background.wallpaperTransition = Math.floor(Math.random() * 27);
        }
    }

    // scriptable: qs ipc call windows set waybar true  |  qs ipc call windows get hideTaskbar
    IpcHandler {
        target: "windows"

        function get(key: string): string {
            const v = adapter[key];
            return v === undefined || key === "pinHash" || key === "pinSalt" ? "" : String(v);
        }

        function set(key: string, value: string): string {
            if (adapter[key] === undefined || key === "pinHash" || key === "pinSalt")
                return `unknown option: ${key}`;
            const cur = adapter[key];
            adapter[key] = typeof cur === "boolean" ? value === "true" || value === "1" : typeof cur === "number" ? Number(value)
                : typeof cur === "object" ? value.split(",").map(s => s.trim()).filter(s => s.length) : value;   // lists: a,b,c
            return `${key} = ${adapter[key]}`;
        }
    }

    FileView {
        id: file

        path: root.path
        watchChanges: true
        onFileChanged: reload()
        onLoaded: { root.loaded = true; root.apply(); } // loaded first: apply() starts tiling only with the whole file read
        onLoadFailed: err => {
            if (err === FileViewError.FileNotFound) writeAdapter();
            root.apply();
            root.loaded = true;
        }
        onAdapterUpdated: writeAdapter()

        JsonAdapter {
            id: adapter

            property bool hideTaskbar: false
            property string lockStyle: "caelestia"
            property bool lockOnStart: false
            property real lockBlur: 0.3
            property bool lockLiveOthers: true
            property real lockLiveDim: 0.2
            property string lockScreen: ""
            property string lockWallpaper: "auto"
            property string lockClock: "caelestia"
            property string lockMedia: "caelestia"
            property var barPieces: []
            property bool nexusTopmost: true
            property real nexusOpacity: 0.72
            property string nexusBackdrop: "blur"
            property string barColour: ""
            property string notifScreens: "active"
            property bool configIntroShown: false
            property string mixBar: "caelestia"
            property string mixNotifs: "caelestia"
            property string mixOsd: "caelestia"
            property string mixLock: "caelestia"
            property string mixWallpaper: "caelestia"
            property string mixLauncher: "caelestia"
            property string mixDock: "off"
            property string titleBars: "system"
            property list<string> titleBarKeep: []
            property string titleBarIcon: "auto"
            property string titleBarSize: "style"
            property bool titleBarBlend: true
            property string batterySaver: "auto"
            property bool setupPending: false
            property string mixDesktopWidgets: "off"
            property string settingsStyle: "rice"       // Aether Settings (all shells) by default
            property string riceBackground: ""
            property real riceDim: 0.45
            property bool waybar: false
            property bool splashEnabled: true
            property string splashStyle: "aether"
            property string aetherColours: "scheme"   // wallpaper-first: the splash purple only when you pick it
            property string aetherSkin: ""
            property var ryokuPieces: []
            property string ryokuPiecesEdge: "top-right"
            property int ryokuPiecesOffset: 220
            property int ryokuPiecesInset: 4
            property string sessionMenu: ""
            property var modsOff: []
            property string wallpaperPicker: ""
            property string shellConfig: "aether"
            property int splashMs: 1600
            property string splashText: "CAELESTIA"
            property string splashTextColour: ""
            property string splashBackground: ""
            property list<string> splashColours: []
            property string nexusFx: "aurora"
            property real nexusFxStrength: 0.55
            property real nexusFxSpeed: 1
            property bool randomTransition: false
            property bool hyprland: false
            onHyprlandChanged: if (hyprland) hyprlandWm = false // one Hyprland at a time
            property bool runAsAdmin: false
            property string displayName: ""
            property string komorebi: ""
            property string whkd: "C:/Program Files/whkd/bin/whkd.exe"
            onKomorebiChanged: root.apply()
            onWhkdChanged: root.apply()
            property bool hyprlandAlt: true
            // OSD / notification popups on every monitor (off: only the focused one)
            property bool osdAllMonitors: false
            property bool autoGameMode: true
            onHyprlandAltChanged: root.apply()
            property bool hyprlandWm: false
            onHyprlandWmChanged: {
                if (hyprlandWm)
                    hyprland = false; // one Hyprland at a time: the Rust WM replaces the older mode
                root.apply();
            }
            property string pinHash: ""
            property string pinSalt: ""

            onHideTaskbarChanged: root.apply()
            onPinHashChanged: root.apply()
        }
    }

    // the native side strips / restores title bars (and puts them back if the shell exits or crashes)
    Binding {
        target: Win.WindowManager
        property: "titleBars"
        restoreMode: Binding.RestoreNone // a reload must not put the old value back after the new shell set it
        // "dms" / "discord": the shell draws that header over apps' title bars (modules/captions), Windows' own
        // title bar stays underneath in the same colours
        value: root.titleBars === "system" || root.titleBars === "strip" ? root.titleBars : root.captionSkin ? "skin" : "themed"
    }
    Binding {
        target: Win.WindowManager
        property: "captionHeight"
        restoreMode: Binding.RestoreNone // a reload must not put the old value back after the new shell set it
        value: root.captionHeight
    }
    Binding {
        target: Win.WindowManager
        property: "titleBarKeep"
        restoreMode: Binding.RestoreNone // a reload must not put the old value back after the new shell set it
        value: root.titleBarKeep
    }
    Binding {
        target: Win.WindowManager
        property: "titleBarColor"
        restoreMode: Binding.RestoreNone // a reload must not put the old value back after the new shell set it
        value: root.titleBars === "discord" ? "#121214" : root.titleBars === "kde" ? "#272c31" : root.titleBarTint.a > 0 ? root.titleBarTint : Colours.palette.m3surfaceContainer
    }
    Binding {
        target: Win.WindowManager
        property: "titleBarTextColor"
        restoreMode: Binding.RestoreNone // a reload must not put the old value back after the new shell set it
        value: root.titleBars === "discord" ? "#dbdee1" : root.titleBars === "kde" ? "#fcfcfc" : root.titleBarTextTint.a > 0 ? root.titleBarTextTint : Colours.palette.m3onSurface
    }
}
