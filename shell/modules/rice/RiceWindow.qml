pragma ComponentBehavior: Bound

// Aether settings (the "rice" panel): one place for every shell, in end-4's Material 3 style over your video.
// It is also the first-run "Make it yours" setup (startTab "setup").
// Every tab is one piece of the desktop. Pick which shell draws it (the cards), then every option every shell
// has for that piece is right below - read from and written to that shell's own config file, so nothing is
// copied and everything stays live. The background is your video/picture (WinSettings.riceBackground, else the
// wallpaper), dimmed by WinSettings.riceDim.
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import Quickshell
import Quickshell.Io
import Quickshell.Widgets
import Quickshell.Windows as Win
import Caelestia.Config
import Caelestia.Multimedia
import qs.components
import qs.services
import qs.utils
import qs.modules.userpanel
import "pieceCatalog.js" as Pieces

FloatingWindow {
    id: win

    property string startTab: "overview"
    property string tab: startTab === "setup" ? "overview" : startTab
    property string query: ""

    readonly property string cae: Paths.config
    readonly property string noc: `${Paths.home}/.config/noctalia/settings.json`
    readonly property color accent: AetherStyle.palette.m3primary

    // the shells; ported = selectable in the cards
    readonly property var shells: [
        { id: "caelestia", name: "Caelestia", colour: "#9ecbff", ported: true, blurb: "Material 3, blobs and drawers" },
        { id: "noctalia", name: "Noctalia", colour: "#f7d774", ported: true, blurb: "Warm, minimal, lots of widgets" },
        { id: "dms", name: "DankMaterialShell", colour: "#c39bff", ported: true, blurb: "Dank bar, dash, islands" },
        { id: "zenith", name: "Zenith", colour: "#7ee0c3", ported: true, blurb: "Dynamic island, focus tools" },
        { id: "end4", name: "end-4 illogical-impulse", colour: "#ffb4ab", ported: true, blurb: "Material bar, sidebars, overview" },
        { id: "ryoku", name: "Ryoku", colour: "#e86a6a", ported: true, blurb: "A whole desktop: top bar, Stage, dock" }
    ]

    // the pieces. mix = the WinSettings choice; from = which shells can draw it; options = config sources
    readonly property var tabs: [
        { id: "overview", name: "Overview", icon: "dashboard", desc: "Your rice at a glance" },
        { id: "bar", name: "Bar", icon: "toolbar", desc: "The bar along the screen edge", mix: "mixBar", from: ["caelestia", "noctalia", "zenith", "dms", "end4", "ryoku"],
          options: [{ shell: "caelestia", file: "windows.json", include: "^barPieces$", title: "Widgets from the other shells (in the end-4, Noctalia, DMS and Zenith bars)", multiChoices: { barPieces: Pieces.barWidgets } },
              { shell: "caelestia", file: "bar.json" }, { shell: "caelestia", file: "taskbar.json", title: "Caelestia · running apps" }, { shell: "noctalia", section: "bar" }] },
        { id: "notifs", name: "Notifications", icon: "notifications", desc: "Pop-ups and history", mix: "mixNotifs", from: ["caelestia", "noctalia", "zenith", "dms", "end4", "ryoku"],
          options: [{ shell: "caelestia", file: "notifs.json" }, { shell: "caelestia", file: "windows.json", include: "^notif", title: "Caelestia · which monitor" }, { shell: "noctalia", section: "notifications" }] },
        { id: "osd", name: "On-screen display", icon: "tune", desc: "Volume and brightness pop-up", mix: "mixOsd", user: "osd", from: ["caelestia", "noctalia", "zenith", "dms", "end4", "ryoku"],
          options: [{ shell: "caelestia", file: "osd.json" }, { shell: "noctalia", section: "osd" }] },
        { id: "launcher", name: "Launcher", icon: "apps", desc: "What the launcher key opens", mix: "mixLauncher", from: ["caelestia", "noctalia", "zenith", "dms", "end4", "ryoku"],
          options: [{ shell: "caelestia", file: "launcher.json" }, { shell: "noctalia", section: "appLauncher" }] },
        { id: "lock", name: "Lock screen", icon: "lock", desc: "What locking shows - and its features", mix: "mixLock", user: "lock", from: ["caelestia", "noctalia", "zenith", "dms", "end4", "ryoku"],
          options: [{ shell: "caelestia", file: "lock.json" }, { shell: "caelestia", file: "windows.json", include: "^lock", title: "Caelestia · pieces from other shells, live monitors, blur, style", choices: { lockClock: Pieces.lockPieces, lockMedia: Pieces.lockPieces, lockWallpaper: Pieces.lockWallpaper } }, { shell: "noctalia", section: "general", include: "lock", title: "Noctalia · lock screen" }] },
        { id: "wallpaper", name: "Wallpaper & desktop", icon: "wallpaper", desc: "Wallpaper, desktop clock", mix: "mixWallpaper", from: ["caelestia", "noctalia", "dms", "end4", "ryoku"],
          options: [{ shell: "caelestia", file: "background.json" }, { shell: "noctalia", section: "wallpaper" }] },
        { id: "dock", name: "Dock", icon: "dock_to_bottom", desc: "A dock of your apps", mix: "mixDock", from: ["noctalia", "dms", "end4", "ryoku"],
          options: [{ shell: "noctalia", section: "dock" }] },
        { id: "widgets", name: "Desktop widgets", icon: "widgets", desc: "Clocks, media and more on the desktop", mix: "mixDesktopWidgets", from: ["noctalia", "dms", "ryoku"],
          options: [{ shell: "noctalia", section: "desktopWidgets" }] },
        { id: "titlebars", name: "Title bars", icon: "web_asset", desc: "The title bar Windows draws on apps - theme it Linux-style or strip it for a Hyprland look", mix: "titleBars",
          choices: [{ id: "system", name: "Windows", colour: "#8a8f98", ported: true, blurb: "As Windows draws them" }, { id: "themed", name: "Themed", colour: "#9ecbff", ported: true, blurb: "Linux-style, in your colours" }, { id: "strip", name: "Stripped", colour: "#ffb4ab", ported: true, blurb: "Gone; Super+drag moves windows" }],
          options: [{ shell: "caelestia", file: "windows.json", include: "^titleBar", title: "Caelestia · title bars and apps that keep theirs" }] },
        { id: "control", name: "Control centre", icon: "toggle_on", desc: "Quick toggles and sliders",
          options: [{ shell: "noctalia", section: "controlCenter" }, { shell: "caelestia", file: "utilities.json", title: "Caelestia · utilities" }] },
        { id: "dashboard", name: "Dashboard", icon: "space_dashboard", desc: "Media, weather, performance",
          options: [{ shell: "caelestia", file: "dashboard.json" }, { shell: "noctalia", section: "systemMonitor" }] },
        { id: "sidebar", name: "Sidebar", icon: "view_sidebar", desc: "Notification centre and side panels",
          options: [{ shell: "caelestia", file: "sidebar.json" }] },
        { id: "session", name: "Session menu", icon: "power_settings_new", desc: "Lock, log out, restart",
          options: [{ shell: "caelestia", file: "session.json" }, { shell: "noctalia", section: "sessionMenu" }] },
        { id: "look", name: "Look & colours", icon: "palette", desc: "Fonts, roundness, transparency, colours",
          options: [{ shell: "caelestia", file: "appearance.json" }, { shell: "caelestia", file: "border.json" }, { shell: "noctalia", section: "ui" }, { shell: "noctalia", section: "colorSchemes" }] },
        { id: "audio", name: "Audio", icon: "volume_up", desc: "Volume steps, visualiser",
          options: [{ shell: "noctalia", section: "audio" }, { shell: "caelestia", file: "services.json" }] },
        { id: "general", name: "General", icon: "settings", desc: "Everything else",
          options: [{ shell: "caelestia", file: "general.json" }, { shell: "caelestia", file: "paths.json" }, { shell: "noctalia", section: "general", exclude: "lock|pinHash|pinSalt" }] },
        { id: "windows", name: "Windows", icon: "desktop_windows", desc: "Taskbar, startup, Settings window",
          options: [{ shell: "caelestia", file: "windows.json", exclude: "^(lock|notif|mix|pin|settingsStyle|rice|shellConfig|ryokuPieces)" }] },
        { id: "yours", name: "Your pieces", icon: "extension", desc: "Everything in your user folder (~/.config/caelestia/user): put a widget on your bar, switch desktop widgets, mods and dashboard tabs on or off, use a lock screen, volume pop-up or skin, open a panel" },
        { id: "store", name: "AetherStore", icon: "storefront", desc: "Rices from anywhere - Linux dotfiles, Hyprland configs for AetherWM, terminals, fastfetch, browsers, shells, lock screens. Everything it replaces is backed up" },
        { id: "configs", name: "Quickshell configs", icon: "code", desc: "Every shell here is a Quickshell config in your own folder - change how it looks and works, or run another config" },
        { id: "panel", name: "This panel", icon: "settings_applications", desc: "Background, dimming, which settings opens" }
    ]

    // ---- Your pieces: every file in ~/.config/caelestia/user by kind, "_Name" = switched off (Aether skips those)
    property int piecesTick: 0
    readonly property var pieceKinds: [
        { kind: "widgets", title: "Bar widgets", icon: "toolbar", how: "bar" },
        { kind: "desktop", title: "Desktop widgets", icon: "widgets", how: "toggle" },
        { kind: "dashboard", title: "Dashboard tabs", icon: "space_dashboard", how: "toggle" },
        { kind: "mods", title: "Mods", icon: "extension", how: "toggle" },
        { kind: "panels", title: "Panels", icon: "web_asset", how: "open" },
        { kind: "lock", title: "Lock screens", icon: "lock", how: "use", key: "mixLock" },
        { kind: "osd", title: "Volume / brightness pop-ups", icon: "tune", how: "use", key: "mixOsd" },
        { kind: "skins", title: "Skins (themes for this window)", icon: "palette", how: "skin" }
    ]
    function userPieces(kind: string): var {
        piecesTick;
        if (tab !== "yours")
            return [];
        const skins = kind === "skins";
        return (Win.FsUtils.listDetailed(`${UserExt.dir}/${kind}`, false) ?? [])
            .filter(e => skins ? e.isDir : (!e.isDir && /\.qml$/i.test(e.name)))
            .map(e => {
                const file = skins ? e.name : e.name.replace(/\.qml$/i, "");
                const off = file.startsWith("_");
                return { file: e.name, name: off ? file.slice(1) : file, off: off };
            })
            .filter(p => /^[A-Za-z0-9][\w-]*$/.test(p.name))
            .sort((a, b) => a.name.localeCompare(b.name));
    }
    // on / off = the "_" in front of the file name (mods also leave WinSettings.modsOff when switched on)
    function setPieceOn(kind: string, p: var, on: bool): void {
        if (p.off === !on)
            return;
        const target = kind === "skins" ? (on ? p.name : `_${p.name}`) : (on ? `${p.name}.qml` : `_${p.name}.qml`);
        if (!Win.FsUtils.renamePath(`${UserExt.dir}/${kind}/${p.file}`, target))
            console.warn("[pieces] could not rename", p.file, "to", target);
        if (kind === "mods" && on)
            WinSettings.modsOff = (WinSettings.modsOff ?? []).filter(n => n !== p.name);
        piecesTick++;
    }
    // which bar takes your widgets: Caelestia's (bar.json entries), Ryoku's (the strip Aether lays over it) or the
    // end-4 / Noctalia / DMS / Zenith bars (barPieces)
    readonly property string pieceBar: WinSettings.mixBar === "caelestia" ? "caelestia" : WinSettings.mixBar === "ryoku" ? "ryoku" : "other"
    function barEntryIndex(id: string): int {
        const v = GlobalConfig.bar.entries.values;
        for (let i = 0; i < v.length; i++)
            if (v[i].id === id)
                return i;
        return -1;
    }
    function onBar(name: string): bool {
        piecesTick;
        const id = `user:${name}`;
        if (pieceBar === "caelestia") {
            const i = barEntryIndex(id);
            return i >= 0 && GlobalConfig.bar.entries.values[i].enabled;
        }
        return ((pieceBar === "ryoku" ? WinSettings.ryokuPieces : WinSettings.barPieces) ?? []).includes(id);
    }
    function setOnBar(name: string, on: bool): void {
        const id = `user:${name}`;
        if (pieceBar === "caelestia") {
            const i = barEntryIndex(id);
            if (i >= 0)
                GlobalConfig.bar.entries.at(i).enabled = on;
            else if (on)
                GlobalConfig.bar.entries.insert({ id: id, enabled: true }, -1);
        } else {
            const key = pieceBar === "ryoku" ? "ryokuPieces" : "barPieces";
            const list = (WinSettings[key] ?? []).filter(x => x !== id);
            WinSettings[key] = on ? list.concat([id]) : list;
        }
        piecesTick++;
    }
    readonly property var ryokuEdges: ["top-left", "top", "top-right", "right", "bottom-right", "bottom", "bottom-left", "left"]
    // ---- Quickshell configs: ~/.config/quickshell/<name>/shell.qml, like Linux (Caelestia.exe makes aether + ryoku)
    readonly property string qsConfigDir: `${Paths.home}/.config/quickshell`
    property int configsTick: 0   // bump to list the folder again
    readonly property var qsConfigs: {
        configsTick;
        if (tab !== "configs")
            return [];
        const order = ["aether", "ryoku"];
        return (Win.FsUtils.listDetailed(qsConfigDir, false) ?? []).filter(e => e.isDir).map(e => e.name).filter(n => !/\.backup-\d/.test(n) && Win.FsUtils.classify(`${qsConfigDir}/${n}/shell.qml`) === "file")
            .sort((a, b) => ((order.indexOf(a) + 1 || 99) - (order.indexOf(b) + 1 || 99)) || a.localeCompare(b));
    }
    // the config this shell runs from (its folder's name), "" = the install's own copy
    readonly property string runningConfig: {
        const d = String(Quickshell.shellDir).replace(/\\/g, "/").replace(/\/$/, "");
        return d.toLowerCase().startsWith(qsConfigDir.toLowerCase() + "/") ? d.slice(qsConfigDir.length + 1) : "";
    }
    function configBlurb(name: string): string {
        if (name === "aether")
            return "Aether: Caelestia's code plus the ported DMS (dms/), Noctalia (noctalia/), end-4 (end4/) and Zenith (zenith/)";
        if (name === "ryoku")
            return "Ryoku: runs next to Aether while a Ryoku piece is picked - edits show the next time it starts";
        return "A Quickshell config - running it replaces Aether until you switch back";
    }
    // ---- AetherStore (assets/store.ps1): scan a rice -> pick its parts -> install with backups -> uninstall
    property var storeScan: null        // { id, name, url, parts: [{ type, path, name, label }] }
    property var storePicked: []        // part types to install
    property var storeInstalled: []     // [{ id, name, url, parts, installed }]
    property string storeStatus: ""
    property string storeConfirm: ""    // id waiting for a second click on Uninstall
    property var storeNext: null        // asked for while busy: runs right after
    property bool storeGotLock: false   // the last install brought a Hyprlock style -> offer "Use it"
    function storeKey(p: var): string {  // several parts of one type (hyprlock styles) have keys "type:name"
        return p?.key || p?.type || "";
    }
    function storeRun(args: var): void {
        if (storeProc.running) {
            storeNext = args;
            return;
        }
        storeProc.last = "";
        storeProc.command = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", Quickshell.shellPath("assets/store.ps1"), ...args];
        storeProc.running = true;
    }
    // the browse list: store/index.json in Aether's repo, else the copy Aether ships (assets/store-index.json)
    readonly property string storeIndexUrl: "https://raw.githubusercontent.com/crszy/aether/main/store/index.json"
    property var storeIndex: null
    property bool storeIndexRemote: false   // read from GitHub (previews from there too) or the shipped copy
    property string storeCategory: ""
    function storePreview(p: string): string {
        if (!p)
            return "";
        if (/^https?:/.test(p))
            return p;
        return storeIndexRemote ? storeIndexUrl.replace(/index\.json$/, p) : "file:///" + Quickshell.shellPath(`assets/store-${p}`);
    }
    function loadStoreIndex(): void {
        const xhr = new XMLHttpRequest();
        xhr.onreadystatechange = () => {
            if (xhr.readyState !== XMLHttpRequest.DONE)
                return;
            try {
                if (xhr.status === 200) {
                    const j = JSON.parse(xhr.responseText);
                    if (j && Array.isArray(j.items)) {
                        storeIndexRemote = true;
                        storeIndex = j;
                        return;
                    }
                }
            } catch (e) {}
            try {
                storeIndex = JSON.parse(bundledIndex.text());
            } catch (e) {
                storeStatus = `The store list could not be read: ${e}`;
            }
        };
        xhr.open("GET", storeIndexUrl);
        xhr.send();
    }
    FileView {
        id: bundledIndex

        path: Quickshell.shellPath("assets/store-index.json")
        blockLoading: true
        printErrors: false
    }
    // the id store.ps1 gives a repo (owner-repo), so a card knows it is installed
    function storeIdFor(url: string): string {
        const m = /github\.com\/([^\/]+)\/([^\/#?]+)/.exec(url ?? "");
        return m ? `${m[1]}-${m[2].replace(/\.git$/, "")}`.replace(/[^\w.-]/g, "-").toLowerCase() : "";
    }
    // a whole shell (category "shell") is a Quickshell config: install-config.ps1 puts it in ~/.config/quickshell/<config>
    function storeIsShell(item: var): bool {
        return item?.category === "shell";
    }
    function storeShellInstalled(item: var): bool {
        configsTick;
        return !!item?.config && Win.FsUtils.classify(`${qsConfigDir}/${item.config}`) === "dir";
    }
    property bool installFromStore: false   // the running install-config.ps1 was started by a store card
    property string storeGetting: ""        // the card whose Get is running (its button shows it; other Gets wait)
    readonly property bool storeGetBusy: storeGetting !== "" && (storeProc.running || (installProc.running && installFromStore))
    function storeGet(item: var): void {
        if (storeGetBusy || storeProc.running || installProc.running)
            return;
        storeGetting = item.id;
        if (storeIsShell(item)) {
            if (installProc.running)
                return;
            installFromStore = true;
            storeStatus = `Getting ${item.name} as a Quickshell config…`;
            installConfig(item.url, item.config ?? "");
            return;
        }
        storeStatus = `Getting ${item.name}…`;
        const args = ["scan", "-Url", item.url, "-Name", item.name];
        if (item.parts)
            args.push("-Hint", Qt.btoa(JSON.stringify(item.parts)));
        storeRun(args);
    }
    function storeLook(url: string): void {   // `qs ipc call aether store <link>`
        tab = "store";
        if (String(url ?? "").trim()) {
            storeStatus = "Getting it…";
            storeRun(["scan", "-Url", String(url).trim()]);
        }
    }
    onTabChanged: if (tab === "yours") piecesTick++; else if (tab === "store") {
        if (!storeIndex)
            loadStoreIndex();
        storeRun(["list"]);
    }
    Process {
        id: storeProc

        property string last: ""
        stdout: SplitParser {
            onRead: line => {
                if (line.startsWith("PARTS ")) {
                    try {
                        const s = JSON.parse(line.slice(6));
                        s.parts = [].concat(s.parts ?? []);
                        win.storeScan = s;
                        // everything on, but of the hyprlock styles only the first (they replace each other)
                        win.storePicked = s.parts.filter((p, i) => p.type !== "hyprlock" || s.parts.findIndex(q => q.type === "hyprlock") === i).map(p => win.storeKey(p));
                        win.storeStatus = `Found ${s.parts.length} part(s) in ${s.name} - pick what you want, then Install`;
                    } catch (e) {
                        win.storeStatus = `Could not read the scan: ${e}`;
                    }
                } else if (line.startsWith("INSTALLED ")) {
                    try {
                        win.storeInstalled = [].concat(JSON.parse(line.slice(10)) ?? []);
                    } catch (e) {}
                } else if (line.startsWith("ERROR ")) {
                    win.storeStatus = "Didn't work: " + line.slice(6);
                } else if (line.startsWith("OK ")) {
                    storeProc.last = line;
                } else if (line.trim()) {
                    win.storeStatus = line.trim();
                }
            }
        }
        onExited: {
            if (last.startsWith("OK ")) {
                win.storeGotLock = !!win.storeScan && last.slice(3) === win.storeScan.id && win.storePicked.some(k => k.startsWith("hyprlock"));
                win.storeStatus = win.storeScan && last.slice(3) === win.storeScan.id ? `Installed ${win.storeScan.name}. Uninstall below puts everything back` : "Done - your files are back as they were";
                win.storeScan = null;
                win.configsTick++;
                UserExt.rescan();
                if (!win.storeNext)
                    win.storeNext = ["list"];
            }
            if (win.storeNext) {
                const next = win.storeNext;
                win.storeNext = null;
                Qt.callLater(() => win.storeRun(next));
            }
        }
    }

    // install from a link (assets/install-config.ps1: git clone, or GitHub / .zip download with Windows' curl + tar)
    property string installStatus: ""
    function installConfig(url: string, name: string): void {
        url = String(url ?? "").trim();
        if (!url || installProc.running)
            return;
        installStatus = "Getting it…";
        const args = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", Quickshell.shellPath("assets/install-config.ps1"), "-Launcher", launcher, "-Url", url];
        if (String(name ?? "").trim())
            args.push("-Name", String(name).trim());
        installProc.updating = "";
        installProc.command = args;
        installProc.running = true;
    }
    // installed from a link: <config>/.aether/source.json; git clones pull, downloads fetch again + merge (edits kept)
    function hasSource(name: string): bool {
        configsTick;
        return Win.FsUtils.classify(`${qsConfigDir}/${name}/.aether/source.json`) === "file";
    }
    function updateConfig(name: string): void {
        if (installProc.running)
            return;
        installStatus = `Updating ${name}…`;
        installProc.updating = name;
        installProc.command = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", Quickshell.shellPath("assets/install-config.ps1"), "-Launcher", launcher, "-Update", "-Name", name];
        installProc.running = true;
    }
    Process {
        id: installProc

        property string last: ""
        property string updating: ""   // the config being updated ("" = an install)
        stdout: SplitParser {
            onRead: line => {
                installProc.last = line;
                if (!/^(OK|ERROR) /.test(line)) {
                    win.installStatus = line;
                    if (win.installFromStore)
                        win.storeStatus = line;
                }
            }
        }
        onExited: {
            const ok = /^OK (.+)$/.exec(last);
            win.installStatus = ok ? (updating ? `Updated "${ok[1]}" - your edits were kept (see its report if both of you changed a file)` : `Installed "${ok[1]}" - press Run on it below to try it`)
                : (last.replace(/^ERROR /, "Didn't work: ") || "Didn't work");
            if (win.installFromStore)
                win.storeStatus = ok ? `Installed "${ok[1]}" - it is under Quickshell configs: Run it there (made for Linux: parts may not work)` : win.installStatus;
            win.installFromStore = false;
            win.configsTick++;
            UserExt.scanConfigs();
        }
    }
    readonly property var current: tabs.find(t => t.id === tab) ?? tabs[0]

    // ---- looks: one click sets every piece (the "what should Aether look like" cards)
    readonly property var presets: [
        { id: "aether", name: "Aether", colour: "#d0bcff", icon: "auto_awesome", blurb: "A piece of every shell, coloured by your wallpaper, Aether's own lock and pop-up",
          mix: { mixBar: "caelestia", mixLauncher: "dms", mixNotifs: "end4", mixOsd: "user:AetherPill", mixLock: "user:AetherLock", mixWallpaper: "caelestia", mixDock: "dms", mixDesktopWidgets: "off" }, also: { aetherColours: "scheme", splashStyle: "aether", splashEnabled: true, titleBars: "themed" }, scheme: "dynamic" },
        { id: "caelestia", name: "Caelestia", colour: "#9ecbff", icon: "bubble_chart", blurb: "Blobs, drawers and a slim side bar",
          mix: { mixBar: "caelestia", mixLauncher: "caelestia", mixNotifs: "caelestia", mixOsd: "caelestia", mixLock: "caelestia", mixWallpaper: "caelestia", mixDock: "off", mixDesktopWidgets: "off" } },
        { id: "end4", name: "end-4", colour: "#ffb4ab", icon: "view_quilt", blurb: "Material top bar, sidebars, overview",
          mix: { mixBar: "end4", mixLauncher: "end4", mixNotifs: "end4", mixOsd: "end4", mixLock: "caelestia", mixWallpaper: "caelestia", mixDock: "off", mixDesktopWidgets: "off" } },
        { id: "dms", name: "DankMaterial", colour: "#c39bff", icon: "space_dashboard", blurb: "Dank bar, dock, desktop widgets",
          mix: { mixBar: "dms", mixLauncher: "dms", mixNotifs: "dms", mixOsd: "dms", mixLock: "caelestia", mixWallpaper: "dms", mixDock: "dms", mixDesktopWidgets: "dms" } },
        { id: "noctalia", name: "Noctalia", colour: "#f7d774", icon: "nights_stay", blurb: "Warm and minimal, lots of widgets",
          mix: { mixBar: "noctalia", mixLauncher: "noctalia", mixNotifs: "noctalia", mixOsd: "noctalia", mixLock: "caelestia", mixWallpaper: "noctalia", mixDock: "noctalia", mixDesktopWidgets: "noctalia" } },
        { id: "zenith", name: "Zenith", colour: "#7ee0c3", icon: "blur_circular", blurb: "A dynamic island and focus tools",
          mix: { mixBar: "zenith", mixLauncher: "zenith", mixNotifs: "zenith", mixOsd: "zenith", mixLock: "caelestia", mixWallpaper: "caelestia", mixDock: "off", mixDesktopWidgets: "off" } },
        { id: "ryoku", name: "Ryoku", colour: "#e86a6a", icon: "landscape", blurb: "A whole desktop: top bar, Stage, dock, sidebar",
          mix: { mixBar: "ryoku", mixLauncher: "ryoku", mixNotifs: "ryoku", mixOsd: "ryoku", mixLock: "ryoku", mixWallpaper: "ryoku", mixDock: "ryoku", mixDesktopWidgets: "ryoku" } }
    ]
    function presetActive(p: var): bool {
        return Object.keys(p.mix).every(k => WinSettings[k] === p.mix[k]);
    }
    function applyPreset(p: var): void {
        for (const k of Object.keys(p.mix))
            WinSettings[k] = p.mix[k];
        for (const k of Object.keys(p.also ?? {}))
            WinSettings[k] = p.also[k];
        if (p.scheme)   // every shell that follows the colour scheme (Caelestia, end-4, DMS, Zenith dynamic) matches
            Quickshell.execDetached(["caelestia", "scheme", "set", "-n", p.scheme]);
    }

    // ---- first-run setup ("Make it yours"), also reachable from the rail
    property bool setup: startTab === "setup"
    property int step: 0
    readonly property var steps: ["Welcome", "Your look", "Top bars", "Extras", "Done"]
    function finishSetup(): void {
        WinSettings.setupPending = false;
        win.setup = false;
        win.tab = "overview";
    }

    // ---- start with Windows (the same Run entry as Settings > Windows)
    readonly property string launcher: WinSettings.packageDir ? `${WinSettings.packageDir}/Caelestia.exe` : ""
    readonly property string runKey: "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"
    property bool startsWithWindows: false
    Process {
        id: runQuery
        running: true
        command: ["reg.exe", "query", win.runKey, "/v", "Caelestia"]
        onExited: code => win.startsWithWindows = code === 0
    }
    Process {
        id: runSet
        onExited: runQuery.running = true
    }
    function setStartup(on: bool): void {
        if (!win.launcher)
            return;
        runSet.exec(on ? ["reg.exe", "add", win.runKey, "/v", "Caelestia", "/t", "REG_SZ", "/d", `"${win.launcher.replace(/\//g, "\\")}"`, "/f"]
                       : ["reg.exe", "delete", win.runKey, "/v", "Caelestia", "/f"]);
    }

    // ---- Aether tiling (the komorebi fork). Komorebi's license only allows sharing its source, so the installer
    // carries the source and it is built here, on this PC, after the user agrees to the license (personal use).
    readonly property string tilingDir: WinSettings.packageDir ? `${WinSettings.packageDir}/extras/aether-tiling` : ""
    readonly property bool tilingShipped: !!tilingDir && Win.FsUtils.classify(`${tilingDir}/LICENSE.md`) === "file"
    property bool tilingAgreed: false
    function buildTiling(): void {
        Quickshell.execDetached(["cmd.exe", "/c", "start", "Aether tiling", "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
            `${win.tilingDir}/build-aether-tiling.ps1`.replace(/\//g, "\\")]);
    }

    // ---- skins: user/skins/<name>/ folders; a part is drawn only when its file exists
    property int skinRev: 0
    function hasSkinPart(part: string): bool {
        return skinRev >= 0 && !!AetherStyle.skin && Win.FsUtils.classify(`${UserExt.dir}/skins/${AetherStyle.skin}/${part}.qml`) === "file";
    }
    readonly property var skinNames: {
        skinRev;
        return UserExt.skinNames;
    }

    // ---- wallpaper folder: any path the user picks (the default ~/Pictures/Wallpapers is created on demand)
    property int wallsRev: 0
    readonly property bool wallsExist: wallsRev >= 0 && !!Paths.wallsdir && Win.FsUtils.classify(Paths.wallsdir) === "dir"
    function setWallsDir(v: string): void {
        v = String(v).trim().replace(/\\/g, "/").replace(/\/+$/, "");
        if (!v.length)
            return;
        GlobalConfig.paths.wallpaperDir = v;
        win.wallsRev++;
    }
    function createWallsDir(): void {
        Win.FsUtils.makePath(Paths.wallsdir);
        win.wallsRev++;
    }
    readonly property FolderDialog wallDialog: FolderDialog {
        title: "Wallpaper folder"
        currentFolder: "file:///" + (win.wallsExist ? Paths.wallsdir : Paths.pictures)
        onAccepted: win.setWallsDir(decodeURIComponent(selectedFolder.toString().replace(/^file:\/{2,3}/, "")))
    }

    function shell(id: string): var {
        if (String(id).startsWith("user:"))   // one of your own files (~/.config/caelestia/user)
            return { id: id, name: `${id.slice(5)} (yours)`, colour: "#D0BCFF", ported: true, blurb: "Your own file" };
        return shells.find(s => s.id === id) ?? tabs.reduce((a, t) => a.concat(t.choices ?? []), []).find(s => s.id === id);
    }
    function sourcePath(o: var): string {
        return o.shell === "noctalia" ? noc : `${cae}/${o.file}`;
    }
    function close(): void {
        win.visible = false;
    }

    title: "Aether Settings"
    color: "transparent"
    implicitWidth: Math.min(1280, (screen?.width ?? 1280) - 80)
    implicitHeight: Math.min(820, (screen?.height ?? 820) - 80)
    minimumSize.width: 860
    minimumSize.height: 560

    function applyFrame(): void {
        const w = root.Window.window;
        if (!w)
            return;
        Win.WindowFrame.setTopmost(w, WinSettings.nexusTopmost);
        Win.WindowFrame.bringToFront(w);
    }
    onVisibleChanged: if (visible) {
        Qt.callLater(applyFrame);
        skinRev++;   // skin files added while closed show up on the next open
    }

    Timer {
        running: true
        interval: 300
        onTriggered: win.applyFrame()
    }

    // colours: your scheme, see-through over the video
    readonly property color surface: AetherStyle.palette.m3surface
    readonly property color fgMain: AetherStyle.palette.m3onSurface
    readonly property color fgSub: AetherStyle.palette.m3onSurfaceVariant
    function layer(c: color, a: real): color {
        return Qt.alpha(c, a);
    }

    ClippingRectangle {
        id: root

        anchors.fill: parent
        radius: AetherStyle.cornerXL
        color: win.surface
        border.width: 1
        border.color: win.layer(AetherStyle.palette.m3outlineVariant, 0.6)

        // ---- background: your video / picture (or the wallpaper), dimmed and tinted with your scheme
        Visual {
            anchors.fill: parent
            source: WinSettings.riceBackground || Wallpapers.current
            playing: win.visible && !WinSettings.saving
        }
        Rectangle {
            anchors.fill: parent
            color: "black"
            opacity: WinSettings.riceDim
            Behavior on opacity {
                NumberAnimation {
                    duration: 200
                }
            }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: win.layer(win.surface, 0.72) }
                GradientStop { position: 0.35; color: win.layer(win.surface, 0.28) }
                GradientStop { position: 1; color: win.layer(win.surface, 0.42) }
            }
        }

        // ---- your skin, behind everything else: user/skins/<skin>/settings-back.qml (Aether Settings > Look,
        // or windows.json "aetherSkin"). It gets `surface` (this window) and `active` (false while hidden: pause
        // your animations then) if it declares them.
        UserPiece {
            anchors.fill: parent
            fill: true
            kind: AetherStyle.skin ? `skins/${AetherStyle.skin}` : ""
            name: AetherStyle.skin && win.hasSkinPart("settings-back") ? "settings-back" : ""
            context: ({ surface: win, active: win.visible })
        }

        // move from any empty spot; resize from the bottom-right corner
        DragHandler {
            target: null
            onActiveChanged: if (active) root.Window.window.startSystemMove()
        }
        MouseArea {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 22
            height: 22
            z: 10
            cursorShape: Qt.SizeFDiagCursor
            onPressed: root.Window.window.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
        }

        // ================= navigation rail =================
        Item {
            id: rail

            visible: !win.setup
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 12
            width: root.width < 1000 ? 76 : 248
            readonly property bool slim: width < 100

            Behavior on width {
                NumberAnimation {
                    duration: 220
                    easing.type: Easing.OutCubic
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 8
                spacing: 6

                // brand
                RowLayout {
                    Layout.leftMargin: rail.slim ? 8 : 10
                    Layout.bottomMargin: 8
                    spacing: 12

                    Rectangle {
                        implicitWidth: 44
                        implicitHeight: 44
                        radius: 15
                        color: AetherStyle.palette.m3primaryContainer
                        MaterialIcon {
                            anchors.centerIn: parent
                            text: "settings"
                            color: AetherStyle.palette.m3onPrimaryContainer
                            fill: 1
                            fontStyle: Tokens.font.icon.medium
                        }
                    }
                    ColumnLayout {
                        visible: !rail.slim
                        spacing: -2
                        StyledText {
                            text: "Aether"
                            color: win.fgMain
                            font: Tokens.font.title.large
                        }
                        StyledText {
                            text: "settings for every shell"
                            color: win.fgSub
                            font: Tokens.font.label.medium
                        }
                    }
                }

                // search (end-4's "FAB" spot)
                GlassField {
                    visible: !rail.slim
                    Layout.fillWidth: true
                    Layout.bottomMargin: 6
                    placeholder: "Search every option…"
                    onTextChanged: win.query = text
                }

                ListView {
                    id: navList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: win.tabs
                    boundsBehavior: Flickable.StopAtBounds
                    currentIndex: Math.max(0, win.tabs.findIndex(t => t.id === win.tab))
                    highlightMoveDuration: 220
                    highlightResizeDuration: 0
                    highlight: Rectangle {
                        radius: 20
                        color: AetherStyle.palette.m3secondaryContainer
                    }

                    delegate: Item {
                        id: navItem

                        required property var modelData
                        required property int index

                        readonly property bool selected: win.tab === modelData.id
                        readonly property string using: modelData.mix ? (WinSettings[modelData.mix] ?? "") : ""

                        width: ListView.view.width
                        height: 42

                        Rectangle {
                            anchors.fill: parent
                            radius: 20
                            color: win.fgMain
                            opacity: navArea.containsMouse && !navItem.selected ? 0.08 : 0
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: rail.slim ? 0 : 16
                            anchors.rightMargin: 12
                            spacing: 12

                            MaterialIcon {
                                Layout.alignment: rail.slim ? Qt.AlignHCenter : Qt.AlignLeft
                                Layout.fillWidth: rail.slim
                                horizontalAlignment: Text.AlignHCenter
                                text: navItem.modelData.icon
                                color: navItem.selected ? AetherStyle.palette.m3onSecondaryContainer : win.fgSub
                                fill: navItem.selected ? 1 : 0
                                fontStyle: Tokens.font.icon.small
                            }
                            StyledText {
                                visible: !rail.slim
                                Layout.fillWidth: true
                                text: navItem.modelData.name
                                color: navItem.selected ? AetherStyle.palette.m3onSecondaryContainer : win.fgMain
                                font: Tokens.font.label.large
                                elide: Text.ElideRight
                            }
                            // which shell draws it
                            Rectangle {
                                visible: !rail.slim && navItem.using.length > 0
                                implicitWidth: 10
                                implicitHeight: 10
                                radius: 5
                                color: navItem.using === "off" ? win.layer(win.fgSub, 0.4) : (win.shell(navItem.using)?.colour ?? "white")
                            }
                        }

                        MouseArea {
                            id: navArea

                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.tab = navItem.modelData.id
                        }
                    }
                }

                GlassButton {
                    Layout.fillWidth: !rail.slim
                    Layout.alignment: Qt.AlignHCenter
                    compact: rail.slim
                    icon: "tune"
                    text: rail.slim ? "" : "Make it yours"
                    onClicked: {
                        win.step = 0;
                        win.setup = true;
                    }
                }
            }
        }

        // ---- close
        GlassButton {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 16
            z: 20
            icon: "close"
            compact: true
            onClicked: win.close()
        }

        // ================= pages =================
        Rectangle {
            id: page

            visible: !win.setup
            anchors.left: rail.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 12
            anchors.leftMargin: 10
            radius: 24
            color: win.layer(AetherStyle.palette.m3surfaceContainerLow, 0.55)

            Flickable {
                id: content

                anchors.fill: parent
                anchors.leftMargin: 26
                anchors.rightMargin: 26
                anchors.topMargin: 22
                contentHeight: body.implicitHeight + 40
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Connections {
                    target: win
                    function onTabChanged(): void {
                        content.contentY = 0;
                    }
                    // the store found a rice's parts: bring them into view (they sit below the cards)
                    function onStoreScanChanged(): void {
                        if (win.storeScan)
                            Qt.callLater(() => {
                                scrollTo.to = Math.max(0, Math.min(storeLinkRow.y - 60, content.contentHeight - content.height));
                                scrollTo.restart();
                            });
                    }
                }
                NumberAnimation {   // one-off glide (a Behavior would also drag on your own scrolling)
                    id: scrollTo

                    target: content
                    property: "contentY"
                    duration: 450
                    easing.type: Easing.OutCubic
                }

                ColumnLayout {
                    id: body

                    width: content.width
                    spacing: 14

                    // header
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.rightMargin: 50
                        spacing: 16

                        Rectangle {
                            implicitWidth: 52
                            implicitHeight: 52
                            radius: 18
                            color: AetherStyle.palette.m3tertiaryContainer
                            MaterialIcon {
                                anchors.centerIn: parent
                                text: win.current.icon
                                color: AetherStyle.palette.m3onTertiaryContainer
                                fill: 1
                                fontStyle: Tokens.font.icon.medium
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            StyledText {
                                text: win.current.name
                                color: win.fgMain
                                font: Tokens.font.title.large
                            }
                            StyledText {
                                Layout.fillWidth: true
                                text: win.current.desc
                                color: win.fgSub
                                font: Tokens.font.body.medium
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // ---- Overview: the look + every piece
                    SectionTitle {
                        visible: win.tab === "overview"
                        text: "What should Aether look like?"
                        icon: "palette"
                    }
                    LookPicker {
                        visible: win.tab === "overview"
                        Layout.fillWidth: true
                    }

                    SectionTitle {
                        visible: win.tab === "overview"
                        text: "Every piece, and who draws it"
                        icon: "dashboard_customize"
                    }
                    GridLayout {
                        visible: win.tab === "overview"
                        Layout.fillWidth: true
                        columns: Math.max(2, Math.floor(body.width / 230))
                        columnSpacing: 10
                        rowSpacing: 10

                        Repeater {
                            model: win.tabs.filter(t => t.mix)

                            Rectangle {
                                id: card

                                required property var modelData
                                readonly property var by: win.shell(WinSettings[modelData.mix])

                                Layout.fillWidth: true
                                implicitHeight: 100
                                radius: 22
                                color: win.layer(cardArea.containsMouse ? AetherStyle.palette.m3surfaceContainerHigh : AetherStyle.palette.m3surfaceContainer, 0.85)

                                Behavior on color {
                                    ColorAnimation {
                                        duration: 140
                                    }
                                }

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 2
                                    MaterialIcon {
                                        text: card.modelData.icon
                                        color: card.by?.colour ?? win.fgSub
                                        fill: 1
                                        fontStyle: Tokens.font.icon.medium
                                    }
                                    Item {
                                        Layout.fillHeight: true
                                    }
                                    StyledText {
                                        text: card.modelData.name
                                        color: win.fgMain
                                        font: Tokens.font.title.small
                                    }
                                    StyledText {
                                        text: card.by ? card.by.name : "Off"
                                        color: card.by?.colour ?? win.fgSub
                                        font: Tokens.font.label.medium
                                    }
                                }
                                MouseArea {
                                    id: cardArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: win.tab = card.modelData.id
                                }
                            }
                        }
                    }

                    SectionTitle {
                        visible: win.tab === "overview"
                        text: "Quick switches"
                        icon: "toggle_on"
                    }
                    QuickSwitches {
                        visible: win.tab === "overview"
                        Layout.fillWidth: true
                    }

                    // ---- which shell draws this piece
                    SectionTitle {
                        visible: !!win.current.mix
                        text: win.current.choices ? "Mode" : "Drawn by"
                        icon: win.current.choices ? "tune" : "brush"
                    }
                    ShellPicker {
                        visible: !!win.current.mix
                        Layout.fillWidth: true
                        tabData: win.current
                    }

                    // ---- Wallpaper page: the folder every wallpaper picker / effect reads (any user, any path)
                    SectionTitle {
                        visible: win.tab === "wallpaper"
                        text: "Wallpaper folder"
                        icon: "folder"
                    }
                    ColumnLayout {
                        visible: win.tab === "wallpaper"
                        Layout.fillWidth: true
                        spacing: 3

                        PanelRow {
                            first: true
                            label: "Wallpaper folder"
                            hint: win.wallsExist ? "Pictures in here (and its sub-folders) show up in every wallpaper picker" : "This folder doesn't exist yet - pick one, or press Create"
                            GlassField {
                                implicitWidth: Math.min(420, page.width * 0.4)
                                text: Paths.wallsdir
                                placeholder: "C:/Users/you/Pictures/Wallpapers"
                                onCommitted: v => win.setWallsDir(v)
                            }
                        }
                        PanelRow {
                            label: "Wallpaper picker"
                            hint: "What >wallpaper opens: Aether's strip, or one of your panels (example: AetherCoverflow)"
                            Flow {
                                Layout.maximumWidth: Math.min(520, page.width * 0.45)
                                spacing: 6
                                Repeater {
                                    model: [""].concat(UserExt.panelNames.filter(n => /wall|cover|flow|picker/i.test(n)).map(n => `user:${n}`))
                                    GlassButton {
                                        required property string modelData
                                        text: modelData ? modelData.slice(5) : "Aether's strip"
                                        highlighted: WinSettings.wallpaperPicker === modelData
                                        onClicked: WinSettings.wallpaperPicker = modelData
                                    }
                                }
                            }
                        }
                        PanelRow {
                            last: true
                            label: "Folder"
                            hint: "Pick another folder, open this one, or create it"
                            RowLayout {
                                spacing: 6
                                GlassButton {
                                    icon: "folder_open"
                                    text: "Choose…"
                                    onClicked: win.wallDialog.open()
                                }
                                GlassButton {
                                    visible: win.wallsExist
                                    icon: "open_in_new"
                                    text: "Open"
                                    onClicked: Qt.openUrlExternally("file:///" + Paths.wallsdir)
                                }
                                GlassButton {
                                    visible: !win.wallsExist
                                    highlighted: true
                                    icon: "create_new_folder"
                                    text: "Create"
                                    onClicked: win.createWallsDir()
                                }
                            }
                        }
                    }

                    // ---- every option every shell has for this piece
                    Repeater {
                        model: win.current.options ?? []

                        JsonOptions {
                            required property var modelData

                            Layout.fillWidth: true
                            title: modelData.title ?? `${win.shell(modelData.shell).name} · ${win.current.name.toLowerCase()}`
                            path: win.sourcePath(modelData)
                            section: modelData.section ?? ""
                            include: modelData.include ?? ""
                            exclude: modelData.exclude ?? "pinHash|pinSalt"
                            choices: modelData.choices ?? ({})
                            multiChoices: modelData.multiChoices ?? ({})
                            query: win.query
                            accent: win.shell(modelData.shell).colour
                            active: !!win.current.mix && WinSettings[win.current.mix] === modelData.shell
                        }
                    }

                    // ---- AetherStore: any rice (Linux dotfiles, Windows rices, Aether rices), part by part, with backups
                    SectionTitle {
                        visible: win.tab === "store" && !!win.storeIndex
                        text: "Browse"
                        icon: "storefront"
                    }
                    Flow {   // categories (only the ones that have something)
                        visible: win.tab === "store" && !!win.storeIndex
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: win.storeIndex ? [{ id: "", name: "Everything", icon: "apps" }].concat((win.storeIndex.categories ?? []).filter(c => win.storeIndex.items.some(i => i.category === c.id))) : []
                            GlassButton {
                                required property var modelData
                                icon: modelData.icon ?? ""
                                text: modelData.name
                                highlighted: win.storeCategory === modelData.id
                                onClicked: win.storeCategory = modelData.id
                            }
                        }
                    }
                    Flow {   // the cards
                        id: storeCards

                        readonly property int columns: Math.max(1, Math.floor((width + spacing) / 300))
                        readonly property real cardWidth: (width - spacing * (columns - 1)) / columns

                        visible: win.tab === "store" && !!win.storeIndex
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: win.storeIndex ? win.storeIndex.items.filter(i => !win.storeCategory || i.category === win.storeCategory) : []

                            Rectangle {
                                id: card

                                required property var modelData
                                readonly property bool isInstalled: win.storeIsShell(card.modelData) ? win.storeShellInstalled(card.modelData) : win.storeInstalled.some(r => r.id === win.storeIdFor(card.modelData.url))

                                width: storeCards.cardWidth
                                height: cardCol.implicitHeight + 24
                                radius: 22
                                color: win.layer(AetherStyle.palette.m3surfaceContainer, 0.86)

                                ColumnLayout {
                                    id: cardCol

                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 12
                                    spacing: 6

                                    ClippingRectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: width / 2
                                        radius: 14
                                        color: win.layer(AetherStyle.palette.m3surfaceContainerHigh, 0.9)
                                        MaterialIcon {   // no picture (or it didn't load): the category's icon
                                            anchors.centerIn: parent
                                            visible: preview.status !== Image.Ready
                                            text: ((win.storeIndex?.categories ?? []).find(c => c.id === card.modelData.category) ?? {}).icon ?? "image"
                                            color: AetherStyle.palette.m3primary
                                            fill: 1
                                            fontStyle: Tokens.font.icon.large
                                        }
                                        Image {
                                            id: preview

                                            anchors.fill: parent
                                            source: win.storePreview(card.modelData.preview ?? "")
                                            // banners (much wider than the card) are shown whole, screenshots fill it;
                                            // measured once on load (a binding on implicitWidth loops with fillMode)
                                            fillMode: Image.PreserveAspectCrop
                                            onStatusChanged: if (status === Image.Ready && implicitWidth / Math.max(1, implicitHeight) > 2.6)
                                                fillMode = Image.PreserveAspectFit
                                            asynchronous: true
                                            cache: true
                                            sourceSize.width: 600
                                        }
                                    }
                                    StyledText {
                                        Layout.fillWidth: true
                                        text: card.modelData.name
                                        color: win.fgMain
                                        font: Tokens.font.title.small
                                        elide: Text.ElideRight
                                    }
                                    StyledText {
                                        Layout.fillWidth: true
                                        text: `by ${card.modelData.author}  ·  ${card.modelData.license ?? "?"}`
                                        color: win.fgSub
                                        font: Tokens.font.label.small
                                        elide: Text.ElideRight
                                    }
                                    StyledText {
                                        Layout.fillWidth: true
                                        text: card.modelData.about ?? ""
                                        color: win.fgSub
                                        font: Tokens.font.label.medium
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 3
                                        elide: Text.ElideRight
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Item {
                                            Layout.fillWidth: true
                                        }
                                        GlassButton {
                                            icon: "open_in_new"
                                            text: "Page"
                                            onClicked: Qt.openUrlExternally(card.modelData.url)
                                        }
                                        GlassButton {
                                            readonly property bool getting: win.storeGetBusy && win.storeGetting === card.modelData.id
                                            highlighted: !card.isInstalled && !getting
                                            enabled: card.isInstalled || getting || !win.storeGetBusy
                                            icon: getting ? "hourglass_top" : card.isInstalled ? "check" : "download"
                                            text: getting ? "Getting…" : card.isInstalled ? "Installed" : "Get"
                                            onClicked: if (getting) return; else if (!card.isInstalled) win.storeGet(card.modelData); else if (win.storeIsShell(card.modelData)) win.tab = "configs"
                                        }
                                    }
                                }
                            }
                        }
                    }

                    SectionTitle {
                        visible: win.tab === "store"
                        text: "Get a rice"
                        icon: "download"
                    }
                    ColumnLayout {
                        id: storeLinkRow

                        visible: win.tab === "store"
                        Layout.fillWidth: true
                        spacing: 3

                        PanelRow {
                            first: true
                            last: true
                            label: "Link or file"
                            hint: win.storeStatus || "A GitHub repo or git link of someone's dotfiles, a .zip link, or a folder / .zip on this PC. Aether finds the parts it knows (Hyprland, shells' settings, terminals, fastfetch, browser themes, Quickshell configs, wallpapers...) and shows them first"
                            RowLayout {
                                spacing: 6
                                GlassField {
                                    id: storeField
                                    implicitWidth: Math.min(380, page.width * 0.32)
                                    placeholder: "https://github.com/someone/dotfiles"
                                    onCommitted: v => { if (v.trim()) { win.storeStatus = "Getting it…"; win.storeRun(["scan", "-Url", v.trim()]); } }
                                }
                                GlassButton {
                                    highlighted: true
                                    icon: storeProc.running ? "hourglass_top" : "travel_explore"
                                    text: storeProc.running ? "Working…" : "Look inside"
                                    onClicked: if (storeField.text.trim()) {
                                        win.storeStatus = "Getting it…";
                                        win.storeRun(["scan", "-Url", storeField.text.trim()]);
                                    }
                                }
                            }
                        }
                        PanelRow {   // after installing a Hyprlock style
                            visible: win.storeGotLock
                            first: true
                            last: true
                            label: "Use it as your lock screen"
                            hint: WinSettings.mixLock === "user:Hyprlock" ? "Your lock screen is Hyprlock - it shows the style you just installed" : "Lock screen > Hyprlock: draws ~/.config/hypr/hyprlock.conf, the style you just installed"
                            GlassButton {
                                highlighted: WinSettings.mixLock !== "user:Hyprlock"
                                icon: WinSettings.mixLock === "user:Hyprlock" ? "check" : "lock"
                                text: WinSettings.mixLock === "user:Hyprlock" ? "In use" : "Use it"
                                onClicked: WinSettings.mixLock = "user:Hyprlock"
                            }
                        }
                    }

                    SectionTitle {
                        visible: win.tab === "store" && !!win.storeScan
                        text: win.storeScan ? `What's in ${win.storeScan.name}` : ""
                        icon: "checklist"
                    }
                    ColumnLayout {
                        visible: win.tab === "store" && !!win.storeScan
                        Layout.fillWidth: true
                        spacing: 3

                        Repeater {
                            model: win.storeScan ? win.storeScan.parts : []

                            PanelRow {
                                id: partRow

                                required property var modelData
                                required property int index

                                first: index === 0
                                label: modelData.label || modelData.type || ""
                                hint: (modelData.path || "the whole thing") + (modelData.type === "quickshell" ? " - a whole shell made for Linux: may need porting to run on Windows" : modelData.type === "hypr" ? " - keys, looks and rules for AetherWM; programs it starts are Linux ones, so those lines are turned off" : modelData.type === "hyprlock" ? " - a lock screen (one at a time); its fonts are installed for you" : "")
                                GlassSwitch {
                                    readonly property string key: win.storeKey(partRow.modelData)
                                    checked: win.storePicked.includes(key)
                                    onToggled: v => {
                                        // hyprlock styles all write ~/.config/hypr/hyprlock.conf: switching one on switches the others off
                                        const others = partRow.modelData.type === "hyprlock" ? win.storeScan.parts.filter(p => p.type === "hyprlock").map(p => win.storeKey(p)) : [];
                                        win.storePicked = v ? win.storePicked.filter(k => !others.includes(k)).concat([key]) : win.storePicked.filter(k => k !== key);
                                    }
                                }
                            }
                        }
                        PanelRow {
                            last: true
                            label: "Install what's switched on"
                            hint: "Everything it replaces is backed up first - Uninstall puts your setup back exactly"
                            GlassButton {
                                highlighted: true
                                enabled: win.storePicked.length > 0 && !storeProc.running
                                icon: "download_done"
                                text: storeProc.running ? "Working…" : "Install"
                                onClicked: {
                                    win.storeStatus = "Installing…";
                                    win.storeRun(["install", "-Id", win.storeScan.id, "-Parts", win.storePicked.join(",")]);
                                }
                            }
                        }
                    }

                    SectionTitle {
                        visible: win.tab === "store" && win.storeInstalled.length > 0
                        text: "Installed"
                        icon: "inventory_2"
                    }
                    ColumnLayout {
                        visible: win.tab === "store" && win.storeInstalled.length > 0
                        Layout.fillWidth: true
                        spacing: 3

                        Repeater {
                            model: win.storeInstalled

                            PanelRow {
                                id: riceRow

                                required property var modelData
                                required property int index

                                first: index === 0
                                last: index === win.storeInstalled.length - 1
                                label: modelData.name
                                hint: `${[].concat(modelData.parts ?? []).join(", ")}  ·  ${String(modelData.installed ?? "").replace("T", " ")}`
                                GlassButton {
                                    icon: win.storeConfirm === riceRow.modelData.id ? "warning" : "delete"
                                    text: win.storeConfirm === riceRow.modelData.id ? "Sure? Puts your files back" : "Uninstall"
                                    onClicked: {
                                        if (win.storeConfirm !== riceRow.modelData.id) {
                                            win.storeConfirm = riceRow.modelData.id;
                                            return;
                                        }
                                        win.storeConfirm = "";
                                        win.storeStatus = "Putting your files back…";
                                        win.storeRun(["uninstall", "-Id", riceRow.modelData.id]);
                                    }
                                }
                            }
                        }
                    }

                    // ---- Quickshell configs: the shells' code is the user's (live reload), plus any other config
                    SectionTitle {
                        visible: win.tab === "configs"
                        text: "Your configs"
                        icon: "folder_code"
                    }
                    ColumnLayout {
                        visible: win.tab === "configs"
                        Layout.fillWidth: true
                        spacing: 3

                        Repeater {
                            model: win.qsConfigs

                            PanelRow {
                                id: cfgRow

                                required property string modelData
                                required property int index
                                readonly property bool isRunning: win.runningConfig === modelData || (modelData === "ryoku" && RyokuHost.wanted)
                                readonly property bool ours: modelData === "aether" || modelData === "ryoku"
                                property string reportText: ""
                                readonly property int toMerge: {
                                    const m = /both changed (\d+)/.exec(reportText);
                                    return m ? parseInt(m[1]) : 0;
                                }
                                property bool confirmReset: false

                                FileView {   // the last update's report (Caelestia.exe merge)
                                    // git clones have none (git pull is their update); configsTick re-checks
                                    path: { win.configsTick; const p = `${win.qsConfigDir}/${cfgRow.modelData}/.aether/last-update.txt`; return Win.FsUtils.classify(p) === "file" ? p : ""; }
                                    watchChanges: true
                                    onFileChanged: reload()
                                    onLoaded: cfgRow.reportText = text()
                                    onLoadFailed: cfgRow.reportText = ""
                                }

                                first: index === 0
                                label: modelData + (isRunning ? "  ·  running" : "")
                                hint: win.configBlurb(modelData) + (toMerge ? `\n${toMerge} file(s) changed by both you and an update - see the report (*.aether-new files)` : "")

                                RowLayout {
                                    spacing: 6
                                    GlassButton {
                                        icon: "folder_open"
                                        text: "Edit"
                                        onClicked: Qt.openUrlExternally("file:///" + `${win.qsConfigDir}/${cfgRow.modelData}`)
                                    }
                                    GlassButton {
                                        visible: cfgRow.toMerge > 0
                                        highlighted: true
                                        icon: "merge"
                                        text: "Report"
                                        onClicked: Qt.openUrlExternally("file:///" + `${win.qsConfigDir}/${cfgRow.modelData}/.aether/last-update.txt`)
                                    }
                                    GlassButton {
                                        visible: !cfgRow.ours && win.hasSource(cfgRow.modelData) && !!win.launcher
                                        icon: installProc.running && installProc.updating === cfgRow.modelData ? "hourglass_top" : "update"
                                        text: "Update"
                                        onClicked: win.updateConfig(cfgRow.modelData)
                                    }
                                    GlassButton {
                                        visible: cfgRow.modelData !== "ryoku" && !cfgRow.isRunning && !!win.launcher
                                        icon: "play_arrow"
                                        text: "Run"
                                        onClicked: Quickshell.execDetached([win.launcher, "--config", cfgRow.modelData])
                                    }
                                    GlassButton {
                                        visible: cfgRow.ours && !!win.launcher
                                        icon: cfgRow.confirmReset ? "warning" : "restart_alt"
                                        text: cfgRow.confirmReset ? "Sure? Yours is kept as a backup" : "Reset"
                                        onClicked: {
                                            if (!cfgRow.confirmReset) {
                                                cfgRow.confirmReset = true;
                                                return;
                                            }
                                            cfgRow.confirmReset = false;
                                            Quickshell.execDetached([win.launcher, "--reset-config", cfgRow.modelData]);
                                        }
                                    }
                                }
                            }
                        }
                        PanelRow {
                            first: win.qsConfigs.length === 0
                            label: "Get a config from a link"
                            hint: win.installStatus || "A GitHub repo, a git link or a .zip of a Quickshell config (people share rices this way). A config is code - only install ones you trust. Configs made for Linux may need porting to work on Windows"
                            RowLayout {
                                spacing: 6
                                GlassField {
                                    id: linkField
                                    implicitWidth: Math.min(360, page.width * 0.3)
                                    placeholder: "https://github.com/someone/their-rice"
                                    onCommitted: v => win.installConfig(v, nameField.text)
                                }
                                GlassField {
                                    id: nameField
                                    implicitWidth: 120
                                    placeholder: "name (optional)"
                                    onCommitted: v => win.installConfig(linkField.text, v)
                                }
                                GlassButton {
                                    highlighted: true
                                    icon: installProc.running ? "hourglass_top" : "download"
                                    text: installProc.running ? "Getting it…" : "Install"
                                    onClicked: if (!installProc.running) win.installConfig(linkField.text, nameField.text)
                                }
                            }
                        }
                        PanelRow {
                            last: true
                            label: "Add a config"
                            hint: "Any Quickshell config works here, like `qs -c <name>` on Linux: put its folder (with a shell.qml) in ~/.config/quickshell, then press Run. Copy aether to a new name to make your own Aether"
                            RowLayout {
                                spacing: 6
                                GlassButton {
                                    icon: "folder_open"
                                    text: "Open"
                                    onClicked: Qt.openUrlExternally("file:///" + win.qsConfigDir)
                                }
                                GlassButton {
                                    icon: "refresh"
                                    text: "Look again"
                                    onClicked: win.configsTick++
                                }
                            }
                        }
                    }
                    StyledText {
                        visible: win.tab === "configs"
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        text: "Edits reload live. Updates never overwrite them: files you didn't touch get the new version, files you changed stay yours, and when both changed, the new version lands next to yours as <file>.aether-new. Running another config stops Aether; run Caelestia.exe to come back."
                        color: win.fgSub
                        font: Tokens.font.label.medium
                        wrapMode: Text.WordWrap
                    }

                    // ---- Your pieces (pieceKinds / userPieces above)
                    StyledText {
                        visible: win.tab === "yours"
                        Layout.fillWidth: true
                        text: `Your own QML files in ${UserExt.dir} - edit one and it reloads live. Off = the file's name starts with "_" (Aether skips it). Bar widgets go on the bar you use now: ${win.pieceBar === "caelestia" ? "Caelestia's" : win.pieceBar === "ryoku" ? "Ryoku's (Aether shows them in a strip on it)" : "the " + WinSettings.mixBar + " bar"}.`
                        color: win.fgSub
                        font: Tokens.font.label.medium
                        wrapMode: Text.WordWrap
                    }
                    Repeater {
                        model: win.tab === "yours" ? win.pieceKinds : []

                        ColumnLayout {
                            id: kindBlock

                            required property var modelData
                            readonly property var files: win.userPieces(modelData.kind)

                            Layout.fillWidth: true
                            spacing: 3

                            SectionTitle {
                                text: kindBlock.modelData.title
                                icon: kindBlock.modelData.icon
                            }
                            Repeater {
                                model: kindBlock.files

                                PanelRow {
                                    id: pieceRow

                                    required property var modelData
                                    required property int index
                                    readonly property string how: kindBlock.modelData.how
                                    readonly property bool inUse: how === "use" ? WinSettings[kindBlock.modelData.key] === `user:${modelData.name}`
                                        : how === "skin" ? WinSettings.aetherSkin === modelData.name
                                        : how === "bar" ? win.onBar(modelData.name) : false

                                    first: index === 0
                                    last: index === kindBlock.files.length - 1
                                    label: modelData.name + (modelData.off ? "  ·  off" : inUse ? "  ·  in use" : "")
                                    hint: `user/${kindBlock.modelData.kind}/${modelData.file}`

                                    RowLayout {
                                        spacing: 6
                                        GlassButton {
                                            icon: "edit"
                                            text: "Edit"
                                            onClicked: Qt.openUrlExternally("file:///" + `${UserExt.dir}/${kindBlock.modelData.kind}/${pieceRow.modelData.file}`)
                                        }
                                        GlassButton {   // switched-off files first need switching on
                                            visible: pieceRow.modelData.off || pieceRow.how === "toggle"
                                            highlighted: pieceRow.modelData.off
                                            icon: pieceRow.modelData.off ? "toggle_off" : "toggle_on"
                                            text: pieceRow.modelData.off ? "Turn on" : "On"
                                            onClicked: win.setPieceOn(kindBlock.modelData.kind, pieceRow.modelData, pieceRow.modelData.off)
                                        }
                                        GlassButton {
                                            visible: !pieceRow.modelData.off && pieceRow.how === "bar"
                                            highlighted: !pieceRow.inUse
                                            icon: pieceRow.inUse ? "remove" : "add"
                                            text: pieceRow.inUse ? "Take off the bar" : "Put on the bar"
                                            onClicked: win.setOnBar(pieceRow.modelData.name, !pieceRow.inUse)
                                        }
                                        GlassButton {
                                            visible: !pieceRow.modelData.off && pieceRow.how === "open"
                                            icon: "open_in_new"
                                            text: "Open"
                                            onClicked: UserExt.togglePanel(pieceRow.modelData.name)
                                        }
                                        GlassButton {
                                            visible: !pieceRow.modelData.off && (pieceRow.how === "use" || pieceRow.how === "skin")
                                            highlighted: !pieceRow.inUse
                                            icon: pieceRow.inUse ? "check" : "done"
                                            text: pieceRow.inUse ? "Stop using" : "Use"
                                            onClicked: {
                                                if (pieceRow.how === "skin")
                                                    WinSettings.aetherSkin = pieceRow.inUse ? "" : pieceRow.modelData.name;
                                                else
                                                    WinSettings[kindBlock.modelData.key] = pieceRow.inUse ? "caelestia" : `user:${pieceRow.modelData.name}`;
                                            }
                                        }
                                    }
                                }
                            }
                            PanelRow {
                                visible: kindBlock.files.length === 0
                                first: true
                                last: true
                                label: "None yet"
                                hint: `Put a .qml file in user/${kindBlock.modelData.kind} (the examples in it show how)`
                                GlassButton {
                                    icon: "folder_open"
                                    text: "Open folder"
                                    onClicked: Qt.openUrlExternally("file:///" + `${UserExt.dir}/${kindBlock.modelData.kind}`)
                                }
                            }
                        }
                    }
                    SectionTitle {
                        visible: win.tab === "yours" && win.pieceBar === "ryoku"
                        text: "Where your widgets sit on Ryoku's bar"
                        icon: "open_with"
                    }
                    ColumnLayout {
                        visible: win.tab === "yours" && win.pieceBar === "ryoku"
                        Layout.fillWidth: true
                        spacing: 3

                        PanelRow {
                            first: true
                            label: "Edge"
                            hint: "The screen edge / corner the strip sits at - move it so it doesn't cover Ryoku's own widgets"
                            GlassButton {
                                icon: "rotate_right"
                                text: WinSettings.ryokuPiecesEdge || "top-right"
                                onClicked: {
                                    const i = win.ryokuEdges.indexOf(WinSettings.ryokuPiecesEdge || "top-right");
                                    WinSettings.ryokuPiecesEdge = win.ryokuEdges[(i + 1) % win.ryokuEdges.length];
                                }
                            }
                        }
                        PanelRow {
                            label: "Distance along the edge"
                            hint: "Pixels from that end of the edge"
                            GlassField {
                                implicitWidth: 90
                                text: String(WinSettings.ryokuPiecesOffset ?? 220)
                                onCommitted: v => { const n = parseInt(v); if (!isNaN(n)) WinSettings.ryokuPiecesOffset = Math.max(0, n); }
                            }
                        }
                        PanelRow {
                            last: true
                            label: "Distance from the edge"
                            hint: "Pixels in from the screen edge (match Ryoku's bar)"
                            GlassField {
                                implicitWidth: 90
                                text: String(WinSettings.ryokuPiecesInset ?? 4)
                                onCommitted: v => { const n = parseInt(v); if (!isNaN(n)) WinSettings.ryokuPiecesInset = Math.max(0, n); }
                            }
                        }
                    }
                    // ---- This panel
                    ColumnLayout {
                        visible: win.tab === "panel"
                        Layout.fillWidth: true
                        spacing: 3

                        PanelRow {
                            first: true
                            label: "Background video or picture"
                            hint: "An .mp4 / .webm / picture - empty = your wallpaper"
                            GlassField {
                                implicitWidth: Math.min(420, page.width * 0.4)
                                text: WinSettings.riceBackground
                                placeholder: "C:/Users/you/Videos/star.mp4"
                                onCommitted: v => WinSettings.riceBackground = v.trim().replace(/\\/g, "/")
                            }
                        }
                        PanelRow {
                            label: "Skin"
                            hint: win.skinNames.length ? "A whole look from user/skins: colours, background, borders" : "Make one in user/skins/<name>/ (example: TokyoRGB)"
                            Flow {
                                Layout.maximumWidth: Math.min(520, page.width * 0.45)
                                spacing: 6
                                Repeater {
                                    model: [""].concat(win.skinNames)
                                    GlassButton {
                                        required property string modelData
                                        text: modelData || "None"
                                        highlighted: WinSettings.aetherSkin === modelData
                                        onClicked: WinSettings.aetherSkin = modelData
                                    }
                                }
                            }
                        }
                        PanelRow {
                            label: "Session menu"
                            hint: "What the power key / button opens: the shell's own, or one of your panels"
                            Flow {
                                Layout.maximumWidth: Math.min(520, page.width * 0.45)
                                spacing: 6
                                Repeater {
                                    model: [""].concat(UserExt.panelNames.filter(n => /session|power/i.test(n)).map(n => `user:${n}`))
                                    GlassButton {
                                        required property string modelData
                                        text: modelData ? modelData.slice(5) : "Shell's own"
                                        highlighted: WinSettings.sessionMenu === modelData
                                        onClicked: WinSettings.sessionMenu = modelData
                                    }
                                }
                            }
                        }
                        PanelRow {
                            label: "Dim the background"
                            hint: "0 = full brightness, 0.95 = almost black"
                            GlassNumber {
                                value: WinSettings.riceDim
                                onCommitted: v => WinSettings.riceDim = Math.max(0, Math.min(0.95, v))
                            }
                        }
                        PanelRow {
                            label: "Settings opens"
                            hint: "What the settings key / button opens"
                            RowLayout {
                                spacing: 6
                                Repeater {
                                    model: [["rice", "This panel"], ["caelestia", "Caelestia's"], ["noctalia", "Noctalia's"]]
                                    GlassButton {
                                        required property var modelData
                                        text: modelData[1]
                                        highlighted: WinSettings.settingsStyle === modelData[0]
                                        onClicked: WinSettings.settingsStyle = modelData[0]
                                    }
                                }
                            }
                        }
                        PanelRow {
                            label: "Always on top"
                            hint: "Stays above every app; tilers leave it alone"
                            GlassSwitch {
                                checked: WinSettings.nexusTopmost
                                onToggled: v => WinSettings.nexusTopmost = v
                            }
                        }
                        PanelRow {
                            last: true
                            label: "Each shell's own settings"
                            hint: "Their full settings windows, with every page"
                            RowLayout {
                                spacing: 6
                                GlassButton {
                                    text: "Caelestia"
                                    onClicked: WinSettings.ipc("nexus", "openCaelestia", [])
                                }
                                GlassButton {
                                    visible: WinSettings.noctaliaAny
                                    text: "Noctalia"
                                    onClicked: WinSettings.ipc("settings", "open", [])
                                }
                                GlassButton {
                                    visible: WinSettings.dmsAny
                                    text: "DMS"
                                    onClicked: WinSettings.ipc("dms:settings", "focusOrToggle", [])
                                }
                            }
                        }
                    }
                }
            }
        }

        // ================= "Make it yours" setup =================
        Item {
            id: wizard

            visible: win.setup
            anchors.fill: parent
            anchors.margins: 28

            // steps
            RowLayout {
                id: stepper

                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8

                Repeater {
                    model: win.steps

                    Rectangle {
                        id: stepPill

                        required property string modelData
                        required property int index
                        readonly property bool done: index < win.step
                        readonly property bool now: index === win.step

                        implicitHeight: 34
                        implicitWidth: stepRow.implicitWidth + 24
                        radius: 17
                        color: now ? AetherStyle.palette.m3primary : done ? win.layer(AetherStyle.palette.m3primaryContainer, 0.9) : win.layer(AetherStyle.palette.m3surfaceContainerHigh, 0.8)

                        Behavior on color {
                            ColorAnimation {
                                duration: 200
                            }
                        }

                        RowLayout {
                            id: stepRow
                            anchors.centerIn: parent
                            spacing: 6
                            MaterialIcon {
                                visible: stepPill.done
                                text: "check"
                                color: AetherStyle.palette.m3onPrimaryContainer
                                fontStyle: Tokens.font.icon.small
                            }
                            StyledText {
                                text: stepPill.modelData
                                color: stepPill.now ? AetherStyle.palette.m3onPrimary : stepPill.done ? AetherStyle.palette.m3onPrimaryContainer : win.fgSub
                                font: Tokens.font.label.large
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: stepPill.done
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.step = stepPill.index
                        }
                    }
                }
            }

            // the card for the current step
            Rectangle {
                id: stepCard

                anchors.top: stepper.bottom
                anchors.topMargin: 18
                anchors.bottom: nav.top
                anchors.bottomMargin: 18
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width, 1000)
                radius: 28
                color: win.layer(AetherStyle.palette.m3surfaceContainerLow, 0.72)
                clip: true

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 30
                    contentHeight: stepBody.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: stepBody

                        width: parent.width
                        spacing: 16

                        // 0 welcome
                        ColumnLayout {
                            visible: win.step === 0
                            Layout.fillWidth: true
                            spacing: 14

                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.topMargin: 20
                                implicitWidth: 96
                                implicitHeight: 96
                                radius: 32
                                color: AetherStyle.palette.m3primaryContainer
                                MaterialIcon {
                                    anchors.centerIn: parent
                                    text: "desktop_windows"
                                    color: AetherStyle.palette.m3onPrimaryContainer
                                    fill: 1
                                    fontStyle: Tokens.font.icon.extraLarge
                                }
                            }
                            StyledText {
                                Layout.alignment: Qt.AlignHCenter
                                text: "Welcome to Aether"
                                color: win.fgMain
                                font: Tokens.font.title.large
                            }
                            StyledText {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: "Linux desktop shells - Caelestia, end-4, DankMaterial, Noctalia and Zenith - running on Windows, mixed however you like.\nPick a look, choose your top bars and title bars, and you're done. Everything stays changeable later in this panel (Super+Comma)."
                                color: win.fgSub
                                font: Tokens.font.body.large
                            }
                        }

                        // 1 look
                        ColumnLayout {
                            visible: win.step === 1
                            Layout.fillWidth: true
                            spacing: 14

                            SectionTitle {
                                text: "What do you want your Aether to look like?"
                                icon: "palette"
                            }
                            StyledText {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "Click one - it switches live, behind this window. You can mix pieces from different shells afterwards."
                                color: win.fgSub
                                font: Tokens.font.body.medium
                            }
                            LookPicker {
                                Layout.fillWidth: true
                            }
                        }

                        // 2 top bars
                        ColumnLayout {
                            visible: win.step === 2
                            Layout.fillWidth: true
                            spacing: 14

                            SectionTitle {
                                text: "Your bar"
                                icon: "toolbar"
                            }
                            ShellPicker {
                                Layout.fillWidth: true
                                tabData: win.tabs.find(t => t.id === "bar")
                            }
                            SectionTitle {
                                text: "App title bars"
                                icon: "web_asset"
                            }
                            StyledText {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "The bar Windows draws at the top of every app. Themed = flat, in your colours, like a Linux desktop. Stripped = gone, like Hyprland (move windows with Super+drag). Apps that draw their own - browsers, Discord, VS Code - stay as they are."
                                color: win.fgSub
                                font: Tokens.font.body.medium
                            }
                            ShellPicker {
                                Layout.fillWidth: true
                                tabData: win.tabs.find(t => t.id === "titlebars")
                            }
                            PanelRow {
                                first: true
                                last: true
                                label: "Hide the Windows taskbar"
                                hint: "Your bar takes over; the Windows one slides in when the mouse touches its edge"
                                GlassSwitch {
                                    checked: WinSettings.hideTaskbar
                                    onToggled: v => WinSettings.hideTaskbar = v
                                }
                            }
                        }

                        // 3 extras
                        ColumnLayout {
                            visible: win.step === 3
                            Layout.fillWidth: true
                            spacing: 3

                            SectionTitle {
                                Layout.bottomMargin: 10
                                text: "Extras"
                                icon: "extension"
                            }
                            PanelRow {
                                first: true
                                label: "Start with Windows"
                                hint: win.launcher ? "Aether starts when you sign in" : "Start Aether from Caelestia.exe to use this"
                                GlassSwitch {
                                    checked: win.startsWithWindows
                                    onToggled: v => win.setStartup(v)
                                }
                            }
                            PanelRow {
                                label: "Battery saver"
                                hint: "Laptops: pause video wallpapers and this panel's video while on battery"
                                GlassSwitch {
                                    checked: WinSettings.batterySaver === "auto"
                                    onToggled: v => WinSettings.batterySaver = v ? "auto" : "off"
                                }
                            }
                            PanelRow {
                                last: true
                                label: "Settings background"
                                hint: "The video or picture behind this panel - empty = your wallpaper"
                                GlassField {
                                    implicitWidth: Math.min(360, stepCard.width * 0.38)
                                    text: WinSettings.riceBackground
                                    placeholder: "C:/Users/you/Videos/star.mp4"
                                    onCommitted: v => WinSettings.riceBackground = v.trim().replace(/\\/g, "/")
                                }
                            }

                            // AetherWM (based on komorebi) - opt-in, personal use only
                            Rectangle {
                                visible: win.tilingShipped
                                Layout.fillWidth: true
                                Layout.topMargin: 16
                                implicitHeight: tilingCol.implicitHeight + 36
                                radius: 22
                                color: win.layer(AetherStyle.palette.m3surfaceContainer, 0.9)
                                border.width: 1
                                border.color: win.tilingAgreed ? AetherStyle.palette.m3primary : "transparent"

                                ColumnLayout {
                                    id: tilingCol

                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 18
                                    spacing: 10

                                    RowLayout {
                                        spacing: 12
                                        MaterialIcon {
                                            text: "grid_view"
                                            color: AetherStyle.palette.m3primary
                                            fill: 1
                                            fontStyle: Tokens.font.icon.medium
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 0
                                            StyledText {
                                                text: "AetherWM - Aether's window manager (optional)"
                                                color: win.fgMain
                                                font: Tokens.font.title.medium
                                            }
                                            StyledText {
                                                Layout.fillWidth: true
                                                wrapMode: Text.WordWrap
                                                text: "Tiling with Hyprland's config, keybinds and animations. AetherWM is based on komorebi by LGUG2Z, whose license allows personal use only and sharing as source - so it is built here on your PC. No Visual Studio needed: the build window gets Rust for you if it's missing (a few minutes, once)."
                                                color: win.fgSub
                                                font: Tokens.font.body.small
                                            }
                                        }
                                    }

                                    // the agreement: a real checkbox
                                    RowLayout {
                                        spacing: 12
                                        Rectangle {
                                            implicitWidth: 24
                                            implicitHeight: 24
                                            radius: 6
                                            color: win.tilingAgreed ? AetherStyle.palette.m3primary : "transparent"
                                            border.width: 2
                                            border.color: win.tilingAgreed ? AetherStyle.palette.m3primary : AetherStyle.palette.m3outline
                                            MaterialIcon {
                                                anchors.centerIn: parent
                                                visible: win.tilingAgreed
                                                text: "check"
                                                color: AetherStyle.palette.m3onPrimary
                                                fontStyle: Tokens.font.icon.small
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: win.tilingAgreed = !win.tilingAgreed
                                            }
                                        }
                                        StyledText {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            text: "I have read the license (Komorebi License 2.0, AetherWM is based on komorebi) and will use AetherWM for personal use only (not at work, not commercially)."
                                            color: win.fgMain
                                            font: Tokens.font.body.medium
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: win.tilingAgreed = !win.tilingAgreed
                                            }
                                        }
                                    }

                                    RowLayout {
                                        spacing: 8
                                        GlassButton {
                                            icon: "description"
                                            text: "Read the license"
                                            onClicked: Qt.openUrlExternally("file:///" + win.tilingDir + "/LICENSE.md")
                                        }
                                        GlassButton {
                                            icon: "build"
                                            text: WinSettings.komorebi ? "Built - rebuild" : "Build and turn on"
                                            highlighted: win.tilingAgreed
                                            opacity: win.tilingAgreed ? 1 : 0.4
                                            onClicked: if (win.tilingAgreed) win.buildTiling()
                                        }
                                        GlassButton {
                                            visible: !!WinSettings.komorebi
                                            icon: "power_settings_new"
                                            text: "Turn off"
                                            onClicked: WinSettings.komorebi = ""
                                        }
                                    }
                                }
                            }
                        }

                        // 4 done
                        ColumnLayout {
                            visible: win.step === 4
                            Layout.fillWidth: true
                            spacing: 14

                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.topMargin: 20
                                implicitWidth: 96
                                implicitHeight: 96
                                radius: 48
                                color: AetherStyle.palette.m3tertiaryContainer
                                MaterialIcon {
                                    anchors.centerIn: parent
                                    text: "check"
                                    color: AetherStyle.palette.m3onTertiaryContainer
                                    fill: 1
                                    fontStyle: Tokens.font.icon.extraLarge
                                }
                            }
                            StyledText {
                                Layout.alignment: Qt.AlignHCenter
                                text: "You're set"
                                color: win.fgMain
                                font: Tokens.font.title.large
                            }
                            StyledText {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: "Super - launcher    ·    Super+Comma - this panel    ·    Super+A - dashboard    ·    Super+Escape - power menu\nEvery piece can be swapped any time from the rail on the left."
                                color: win.fgSub
                                font: Tokens.font.body.large
                            }
                        }
                    }
                }
            }

            // back / next
            RowLayout {
                id: nav

                anchors.bottom: parent.bottom
                anchors.left: stepCard.left
                anchors.right: stepCard.right
                spacing: 10

                GlassButton {
                    text: "Skip setup"
                    visible: win.step < win.steps.length - 1
                    onClicked: win.finishSetup()
                }
                Item {
                    Layout.fillWidth: true
                }
                GlassButton {
                    visible: win.step > 0
                    icon: "arrow_back"
                    text: "Back"
                    onClicked: win.step--
                }
                GlassButton {
                    highlighted: true
                    icon: win.step === win.steps.length - 1 ? "check" : "arrow_forward"
                    text: win.step === 0 ? "Let's go" : win.step === win.steps.length - 1 ? "Finish" : "Next"
                    onClicked: win.step === win.steps.length - 1 ? win.finishSetup() : win.step++
                }
            }
        }

        // ---- your skin, on top of everything: user/skins/<skin>/settings-front.qml (borders, glows, scanlines...).
        // Clicks go through it unless the file declares `property bool interactive: true`.
        UserPiece {
            id: skinFront

            anchors.fill: parent
            z: 1000
            fill: true
            enabled: !!item && item.interactive === true
            kind: AetherStyle.skin ? `skins/${AetherStyle.skin}` : ""
            name: AetherStyle.skin && win.hasSkinPart("settings-front") ? "settings-front" : ""
            context: ({ surface: win, active: win.visible })
        }
    }

    // ================= pieces =================
    component SectionTitle: RowLayout {
        id: st

        property string text
        property string icon

        Layout.fillWidth: true
        Layout.topMargin: 8
        spacing: 10

        MaterialIcon {
            text: st.icon
            color: AetherStyle.palette.m3primary
            fill: 1
            fontStyle: Tokens.font.icon.small
        }
        StyledText {
            Layout.fillWidth: true
            text: st.text
            color: win.fgMain
            font: Tokens.font.title.medium
        }
    }

    // the looks, as cards with a little drawing of where its bar sits
    component LookPicker: GridLayout {
        id: lp

        columns: Math.max(2, Math.min(5, Math.floor(width / 175)))
        columnSpacing: 10
        rowSpacing: 10

        Repeater {
            model: win.presets

            Rectangle {
                id: look

                required property var modelData
                readonly property bool chosen: win.presetActive(modelData)

                Layout.fillWidth: true
                implicitHeight: 196
                radius: 24
                color: chosen ? win.layer(AetherStyle.palette.m3primaryContainer, 0.95) : win.layer(lookArea.containsMouse ? AetherStyle.palette.m3surfaceContainerHigh : AetherStyle.palette.m3surfaceContainer, 0.88)
                border.width: chosen ? 2 : 0
                border.color: AetherStyle.palette.m3primary
                scale: lookArea.pressed ? 0.97 : 1

                Behavior on color {
                    ColorAnimation {
                        duration: 160
                    }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: 120
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    // a real screenshot of the shell (modules/rice/looks/<id>.png), else a small drawing of it
                    ClippingRectangle {
                        visible: shot.status === Image.Ready
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 96 : 0
                        radius: 14
                        color: "black"

                        Image {
                            id: shot

                            anchors.fill: parent
                            readonly property string file: [".png", ".jpg"].map(e => Quickshell.shellPath(`modules/rice/looks/${look.modelData.id}${e}`)).find(p => Win.FsUtils.classify(p) === "file") ?? ""
                            source: file ? `file:///${file}` : ""
                            sourceSize.width: 480
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            smooth: true
                        }
                    }
                    Rectangle {
                        visible: shot.status !== Image.Ready
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 74 : 0
                        radius: 14
                        color: win.layer("black", 0.35)
                        clip: true

                        // a side bar (Caelestia) or a top bar (the others)
                        Rectangle {
                            visible: look.modelData.id === "caelestia"
                            x: 5
                            y: 5
                            width: 10
                            height: parent.height - 10
                            radius: 5
                            color: look.modelData.colour
                        }
                        Rectangle {
                            visible: look.modelData.id !== "caelestia"
                            x: 5
                            y: 5
                            width: parent.width - 10
                            height: 10
                            radius: 5
                            color: look.modelData.colour
                        }
                        // island (Zenith)
                        Rectangle {
                            visible: look.modelData.id === "zenith"
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 3
                            width: 38
                            height: 14
                            radius: 7
                            color: "#0b0d14"
                            border.width: 1
                            border.color: look.modelData.colour
                        }
                        // dock (DMS / Noctalia)
                        Rectangle {
                            visible: look.modelData.id === "dms" || look.modelData.id === "noctalia"
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 5
                            width: parent.width * 0.4
                            height: 9
                            radius: 4.5
                            color: Qt.alpha(look.modelData.colour, 0.7)
                        }
                        // sidebar (end-4)
                        Rectangle {
                            visible: look.modelData.id === "end4"
                            anchors.right: parent.right
                            anchors.rightMargin: 5
                            y: 19
                            width: parent.width * 0.26
                            height: parent.height - 24
                            radius: 6
                            color: Qt.alpha(look.modelData.colour, 0.45)
                        }
                        // a window
                        Rectangle {
                            x: look.modelData.id === "caelestia" ? 22 : 10
                            y: look.modelData.id === "caelestia" ? 10 : 20
                            width: parent.width * 0.42
                            height: parent.height * 0.5
                            radius: 5
                            color: win.layer("white", 0.14)
                        }
                    }

                    RowLayout {
                        spacing: 6
                        MaterialIcon {
                            text: look.modelData.icon
                            color: look.chosen ? AetherStyle.palette.m3onPrimaryContainer : look.modelData.colour
                            fill: 1
                            fontStyle: Tokens.font.icon.small
                        }
                        StyledText {
                            Layout.fillWidth: true
                            text: look.modelData.name
                            color: look.chosen ? AetherStyle.palette.m3onPrimaryContainer : win.fgMain
                            font: Tokens.font.title.small
                            elide: Text.ElideRight
                        }
                        MaterialIcon {
                            visible: look.chosen
                            text: "check_circle"
                            color: AetherStyle.palette.m3primary
                            fill: 1
                            fontStyle: Tokens.font.icon.small
                        }
                    }
                    StyledText {
                        Layout.fillWidth: true
                        text: look.modelData.blurb
                        color: look.chosen ? AetherStyle.palette.m3onPrimaryContainer : win.fgSub
                        font: Tokens.font.label.medium
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: lookArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: win.applyPreset(look.modelData)
                }
            }
        }
    }

    // which shell draws one piece (or the modes of the title bars)
    component ShellPicker: GridLayout {
        id: sp

        property var tabData

        columns: Math.max(2, Math.min(6, Math.floor(width / 150)))
        columnSpacing: 8
        rowSpacing: 8

        Repeater {
            // the shells, Off, then your own files for this piece (user/lock, user/osd)
            model: sp.tabData?.mix ? (sp.tabData.choices ?? win.shells.concat([{ id: "off", name: "Off", colour: "#8a8f98", ported: true, blurb: "Not shown" }])
                .concat((sp.tabData.user === "lock" ? UserExt.lockNames : sp.tabData.user === "osd" ? UserExt.osdNames : [])
                    .map(n => ({ id: `user:${n}`, name: n, colour: "#D0BCFF", ported: true, yours: true, blurb: `Yours · user/${sp.tabData.user}/${n}.qml` })))) : []

            Rectangle {
                id: pick

                required property var modelData
                readonly property bool offered: !!sp.tabData.choices || modelData.id === "off" || !!modelData.yours || (modelData.ported && sp.tabData.from.includes(modelData.id))
                readonly property bool chosen: WinSettings[sp.tabData.mix] === modelData.id

                Layout.fillWidth: true
                implicitHeight: 78
                radius: 20
                opacity: offered ? 1 : 0.4
                color: chosen ? win.layer(AetherStyle.palette.m3secondaryContainer, 0.95) : win.layer(pickArea.containsMouse && offered ? AetherStyle.palette.m3surfaceContainerHigh : AetherStyle.palette.m3surfaceContainer, 0.85)
                border.width: chosen ? 2 : 0
                border.color: pick.modelData.colour
                scale: pickArea.pressed && offered ? 0.97 : 1

                Behavior on color {
                    ColorAnimation {
                        duration: 160
                    }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: 120
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 2
                    RowLayout {
                        Rectangle {
                            implicitWidth: 10
                            implicitHeight: 10
                            radius: 5
                            color: pick.modelData.colour
                        }
                        StyledText {
                            Layout.fillWidth: true
                            text: pick.modelData.name
                            color: pick.chosen ? AetherStyle.palette.m3onSecondaryContainer : win.fgMain
                            font: Tokens.font.label.large
                            elide: Text.ElideRight
                        }
                        MaterialIcon {
                            visible: pick.chosen
                            text: "check_circle"
                            color: AetherStyle.palette.m3primary
                            fill: 1
                            fontStyle: Tokens.font.icon.small
                        }
                    }
                    StyledText {
                        Layout.fillWidth: true
                        text: !pick.modelData.ported ? "porting…" : pick.offered ? pick.modelData.blurb : "doesn't have one"
                        color: win.fgSub
                        font: Tokens.font.label.small
                        elide: Text.ElideRight
                    }
                }
                MouseArea {
                    id: pickArea
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: pick.offered
                    cursorShape: Qt.PointingHandCursor
                    onClicked: WinSettings[sp.tabData.mix] = pick.modelData.id
                }
            }
        }
    }

    // the switches people look for first
    component QuickSwitches: ColumnLayout {
        spacing: 3

        PanelRow {
            first: true
            label: "Hide the Windows taskbar"
            hint: "Your bar takes over; Windows' slides in at the screen edge"
            GlassSwitch {
                checked: WinSettings.hideTaskbar
                onToggled: v => WinSettings.hideTaskbar = v
            }
        }
        PanelRow {
            label: "Aether colours"
            hint: "The colours of Aether's own parts (this window, your panels, the splash): the Aether purple, or your colour scheme"
            RowLayout {
                spacing: 6
                Repeater {
                    model: [["aether", "Aether purple"], ["scheme", "My colour scheme"]]
                    GlassButton {
                        required property var modelData
                        text: modelData[1]
                        highlighted: WinSettings.aetherColours === modelData[0]
                        onClicked: WinSettings.aetherColours = modelData[0]
                    }
                }
            }
        }
        PanelRow {
            label: "Start with Windows"
            hint: win.launcher ? "Aether starts when you sign in" : "Start Aether from Caelestia.exe to use this"
            GlassSwitch {
                checked: win.startsWithWindows
                onToggled: v => win.setStartup(v)
            }
        }
        PanelRow {
            last: true
            label: "Battery saver"
            hint: "Laptops: pause video wallpapers while on battery"
            GlassSwitch {
                checked: WinSettings.batterySaver === "auto"
                onToggled: v => WinSettings.batterySaver = v ? "auto" : "off"
            }
        }
    }

    component PanelRow: Rectangle {
        id: pr

        property string label
        property string hint
        property bool first: false
        property bool last: false
        default property alias control: holder.data

        Layout.fillWidth: true
        implicitHeight: Math.max(68, labels.implicitHeight + 24)
        radius: 6
        topLeftRadius: first ? 22 : 6
        topRightRadius: first ? 22 : 6
        bottomLeftRadius: last ? 22 : 6
        bottomRightRadius: last ? 22 : 6
        color: win.layer(AetherStyle.palette.m3surfaceContainer, 0.86)

        ColumnLayout {
            id: labels

            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.right: holder.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            StyledText {
                Layout.fillWidth: true
                text: pr.label
                color: win.fgMain
                font: Tokens.font.body.large
                elide: Text.ElideRight
            }
            StyledText {
                Layout.fillWidth: true
                text: pr.hint
                color: win.fgSub
                font: Tokens.font.label.medium
                wrapMode: Text.WordWrap
            }
        }
        Item {
            id: holder
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
            width: childrenRect.width
            height: childrenRect.height
        }
    }
}
