.pragma library
.import "../rice/pieceCatalog.js" as Pieces
.import "../../distros/catalog.js" as Distros
// What the merged Aether Settings panel shows. The shells, parts, looks and config sources are the same as the
// classic panel (modules/rice/RiceWindow.qml): keep the two in step when a shell or a part is added.
// groups = the rail (DarkShadowShell's navigation), each with its tabs; a tab with `piece` is drawn from `pieces`.

var shells = [
    { id: "aether", name: "Aether", colour: "#d0bcff", ported: true, blurb: "Ryoku-style bar, island dock, its own dashboard" },
    { id: "caelestia", name: "Caelestia", colour: "#9ecbff", ported: true, blurb: "Material 3, blobs and drawers" },
    { id: "noctalia", name: "Noctalia", colour: "#f7d774", ported: true, blurb: "Warm, minimal, lots of widgets" },
    { id: "dms", name: "DankMaterialShell", colour: "#c39bff", ported: true, blurb: "Dank bar, dash, islands" },
    { id: "zenith", name: "Zenith", colour: "#7ee0c3", ported: true, blurb: "Dynamic island, focus tools" },
    { id: "end4", name: "end-4 illogical-impulse", colour: "#ffb4ab", ported: true, blurb: "Material bar, sidebars, overview" },
    { id: "ryoku", name: "Ryoku", colour: "#e86a6a", ported: true, blurb: "A whole desktop: top bar, Stage, dock" },
    { id: "debian", name: "Debian (GNOME)", colour: "#d70a53", ported: true, blurb: "Debian 13 GNOME: black top bar, calendar, quick settings" },
    { id: "serp", name: "serpantinum", colour: "#b8a4ff", ported: true, blurb: "Widget bar, floating panels, guide (AGPL-3.0)" },
    { id: "dss", name: "DarkShadowShell", colour: "#9fb4ff", ported: true, blurb: "Dynamic island bar, desktop widgets, dock (MIT)" }
];

var pieces = {
    bar: { name: "Bar", icon: "toolbar", desc: "The bar along the screen edge", mix: "mixBar", from: ["aether", "caelestia", "noctalia", "zenith", "dms", "end4", "ryoku", "debian", "serp", "dss"],
        options: [{ shell: "caelestia", file: "windows.json", include: "^barPieces$", title: "Widgets from the other shells", multiChoices: { barPieces: Pieces.barWidgets } },
            { shell: "caelestia", file: "bar.json" }, { shell: "caelestia", file: "taskbar.json", title: "Caelestia · running apps" }, { shell: "noctalia", section: "bar" }] },
    titlebars: { name: "Title bars", icon: "web_asset", desc: "The title bar Windows draws on apps: theme it Linux-style or strip it", mix: "titleBars",
        choices: [{ id: "system", name: "Windows", colour: "#8a8f98", ported: true, blurb: "As Windows draws them" }, { id: "themed", name: "Themed", colour: "#9ecbff", ported: true, blurb: "Linux-style, in your colours" }, { id: "strip", name: "Stripped", colour: "#ffb4ab", ported: true, blurb: "Gone; Super+drag moves windows" }],
        options: [{ shell: "caelestia", file: "windows.json", include: "^titleBar", title: "Title bars and apps that keep theirs" }] },
    wallpaper: { name: "Wallpaper", icon: "wallpaper", desc: "Wallpaper and desktop clock", mix: "mixWallpaper", from: ["caelestia", "noctalia", "dms", "end4", "ryoku", "serp"],
        options: [{ shell: "caelestia", file: "background.json" }, { shell: "noctalia", section: "wallpaper" }] },
    dock: { name: "Dock", icon: "dock_to_bottom", desc: "A dock of your apps", mix: "mixDock", from: ["aether", "noctalia", "dms", "end4", "ryoku", "serp", "dss"],
        options: [{ shell: "noctalia", section: "dock" }] },
    widgets: { name: "Widgets", icon: "widgets", desc: "Clocks, media and more on the desktop", mix: "mixDesktopWidgets", from: ["noctalia", "dms", "ryoku", "serp", "dss"],
        options: [{ shell: "noctalia", section: "desktopWidgets" }] },
    notifs: { name: "Notifications", icon: "notifications", desc: "Pop-ups and history", mix: "mixNotifs", from: ["caelestia", "noctalia", "zenith", "dms", "end4", "ryoku", "serp", "dss"],
        options: [{ shell: "caelestia", file: "notifs.json" }, { shell: "caelestia", file: "windows.json", include: "^notif", title: "Caelestia · which monitor" }, { shell: "noctalia", section: "notifications" }] },
    osd: { name: "OSD", icon: "tune", desc: "The volume and brightness pop-up", mix: "mixOsd", user: "osd", from: ["caelestia", "noctalia", "zenith", "dms", "end4", "ryoku", "serp", "dss"],
        options: [{ shell: "caelestia", file: "osd.json" }, { shell: "noctalia", section: "osd" }] },
    lock: { name: "Lock screen", icon: "lock", desc: "What locking shows, and its features", mix: "mixLock", user: "lock", from: ["caelestia", "noctalia", "zenith", "dms", "end4", "ryoku"],
        options: [{ shell: "caelestia", file: "lock.json" }, { shell: "caelestia", file: "windows.json", include: "^lock", title: "Pieces from other shells, live monitors, blur, style", choices: { lockClock: Pieces.lockPieces, lockMedia: Pieces.lockPieces, lockWallpaper: Pieces.lockWallpaper } }, { shell: "noctalia", section: "general", include: "lock", title: "Noctalia · lock screen" }] },
    launcher: { name: "Launcher", icon: "apps", desc: "What the launcher key opens", mix: "mixLauncher", from: ["caelestia", "noctalia", "zenith", "dms", "end4", "ryoku", "serp", "dss"],
        options: [{ shell: "caelestia", file: "launcher.json" }, { shell: "noctalia", section: "appLauncher" }] },
    control: { name: "Control centre", icon: "toggle_on", desc: "Quick toggles and sliders",
        options: [{ shell: "noctalia", section: "controlCenter" }, { shell: "caelestia", file: "utilities.json", title: "Caelestia · utilities" }] },
    dashboard: { name: "Dashboard", icon: "space_dashboard", desc: "Media, weather, performance",
        options: [{ shell: "caelestia", file: "dashboard.json" }, { shell: "noctalia", section: "systemMonitor" }] },
    sidebar: { name: "Sidebar", icon: "view_sidebar", desc: "Notification centre and side panels",
        options: [{ shell: "caelestia", file: "sidebar.json" }] },
    session: { name: "Session", icon: "power_settings_new", desc: "Lock, log out, restart",
        options: [{ shell: "caelestia", file: "session.json" }, { shell: "noctalia", section: "sessionMenu" }] },
    look: { name: "Look & colours", icon: "palette", desc: "Fonts, roundness, transparency, colours",
        options: [{ shell: "caelestia", file: "appearance.json" }, { shell: "caelestia", file: "border.json" }, { shell: "noctalia", section: "ui" }, { shell: "noctalia", section: "colorSchemes" }] },
    audio: { name: "Audio", icon: "volume_up", desc: "Volume steps, visualiser",
        options: [{ shell: "noctalia", section: "audio" }, { shell: "caelestia", file: "services.json" }] },
    general: { name: "General", icon: "settings", desc: "Everything else",
        options: [{ shell: "caelestia", file: "general.json" }, { shell: "caelestia", file: "paths.json" }, { shell: "noctalia", section: "general", exclude: "lock|pinHash|pinSalt" }] },
    windows: { name: "Windows", icon: "desktop_windows", desc: "Taskbar, startup, the Settings window",
        options: [{ shell: "caelestia", file: "windows.json", exclude: "^(lock|notif|mix|pin|settingsStyle|rice|shellConfig|ryokuPieces|wallpaperPicker|titleBar|barPieces)" }] }
};

// the rail. page: a page of its own (HomePage, LooksPage ...), piece: drawn by PiecePage
var groups = [
    { id: "home", name: "Home", icon: "home", tabs: [{ id: "home", name: "Home", page: "home" }] },
    { id: "shells", name: "Shells", icon: "auto_awesome", tabs: [{ id: "looks", name: "Looks", page: "looks" }, { id: "mix", name: "Mix & match", page: "mix" }] },
    { id: "bar", name: "Bar", icon: "toolbar", tabs: [{ id: "bar", name: "Bar", piece: "bar" }, { id: "titlebars", name: "Title bars", piece: "titlebars" }] },
    { id: "desktop", name: "Desktop", icon: "wallpaper", tabs: [{ id: "wallpaper", name: "Wallpaper", piece: "wallpaper" }, { id: "dock", name: "Dock", piece: "dock" }, { id: "widgets", name: "Widgets", piece: "widgets" }] },
    { id: "alerts", name: "Alerts", icon: "notifications", tabs: [{ id: "notifs", name: "Notifications", piece: "notifs" }, { id: "osd", name: "OSD", piece: "osd" }, { id: "lock", name: "Lock screen", piece: "lock" }] },
    { id: "panels", name: "Panels", icon: "space_dashboard", tabs: [{ id: "launcher", name: "Launcher", piece: "launcher" }, { id: "control", name: "Control centre", piece: "control" }, { id: "dashboard", name: "Dashboard", piece: "dashboard" }, { id: "sidebar", name: "Sidebar", piece: "sidebar" }, { id: "session", name: "Session", piece: "session" }] },
    { id: "look", name: "Look", icon: "palette", tabs: [{ id: "look", name: "Colours & fonts", piece: "look" }, { id: "panel", name: "This panel", page: "panel" }] },
    { id: "system", name: "System", icon: "settings", tabs: [{ id: "general", name: "General", piece: "general" }, { id: "windows", name: "Windows", piece: "windows" }, { id: "audio", name: "Audio", piece: "audio" }] },
    // the Aether shell (AGPL-3.0): its bar, island dock and dashboard (~/.config/caelestia/aether-bar.json)
    { id: "aether", name: "Aether shell", icon: "auto_awesome", tabs: [{ id: "aetherbar", name: "Bar", page: "aetherbar" }, { id: "aetherdock", name: "Dock", page: "aetherdock" }, { id: "aetherdash", name: "Dashboard", page: "aetherdash" }] },
    // serpantinum (AGPL-3.0): its own settings window, whole, inside this one
    { id: "serp", name: "serpantinum", icon: "widgets", tabs: [{ id: "serp", name: "serpantinum", page: "serp" }] },
    // DarkShadowShell (MIT): its bar, desktop widgets and dock settings (~/.config/rice/bar.json, desktop.json)
    { id: "dss", name: "DarkShadowShell", icon: "dark_mode", tabs: [{ id: "dss", name: "Bar & island", page: "dss" }, { id: "dsswidgets", name: "Desktop widgets", page: "dsswidgets" }, { id: "dssdock", name: "Dock", page: "dssdock" }, { id: "dsswm", name: "Windows & tiling", page: "dsswm" }, { id: "dsskeys", name: "Keybinds", page: "dsskeys" }, { id: "dssmon", name: "Monitors", page: "dssmon" }] },
    { id: "more", name: "More", icon: "apps", tabs: [{ id: "more", name: "More", page: "more" }] },
    { id: "about", name: "About", icon: "info", tabs: [{ id: "about", name: "About", page: "about" }] }
];

var presets = [
    { id: "aether", name: "Aether", colour: "#d0bcff", icon: "auto_awesome", blurb: "A piece of every shell, coloured by your wallpaper, Aether's own lock and pop-up",
      mix: { mixBar: "aether", mixLauncher: "dms", mixNotifs: "end4", mixOsd: "user:AetherPill", mixLock: "user:AetherLock", mixWallpaper: "caelestia", mixDock: "aether", mixDesktopWidgets: "off" }, also: { aetherColours: "scheme", splashStyle: "aether", splashEnabled: true, titleBars: "themed" }, scheme: "dynamic" },
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
      mix: { mixBar: "ryoku", mixLauncher: "ryoku", mixNotifs: "ryoku", mixOsd: "ryoku", mixLock: "ryoku", mixWallpaper: "ryoku", mixDock: "ryoku", mixDesktopWidgets: "ryoku" } },
    { id: "serp", name: "serpantinum", colour: "#b8a4ff", icon: "widgets", blurb: "A widget bar with floating panels and serp's guide",
      mix: { mixBar: "serp", mixLauncher: "serp", mixNotifs: "serp", mixOsd: "serp", mixLock: "caelestia", mixWallpaper: "caelestia", mixDock: "off", mixDesktopWidgets: "off" } },
    { id: "dss", name: "DarkShadowShell", colour: "#9fb4ff", icon: "dark_mode", blurb: "A dynamic island bar with DarkShadowShell's menus, desktop widgets and dock",
      mix: { mixBar: "dss", mixLauncher: "dss", mixNotifs: "dss", mixOsd: "dss", mixLock: "caelestia", mixWallpaper: "caelestia", mixDock: "dss", mixDesktopWidgets: "dss" } }
];

// every look, the Linux distros last: a distro look takes the whole desktop (WinSettings.distro), the others are mixes
var looks = presets.concat(Distros.distros.map(function (d) {
    return { id: d.id, name: d.name, colour: d.accent, icon: "computer", blurb: d.blurb, distro: d.id, mix: {} };
}));

// s = WinSettings. A distro look is on while that distro runs; a mix look only while no distro covers it.
function lookActive(p, s) {
    if (p.distro)
        return s.distro === p.distro;
    return !s.distroMode && Object.keys(p.mix).every(function (k) { return s[k] === p.mix[k]; });
}

function activeLook(s) {
    for (var i = 0; i < looks.length; i++)
        if (lookActive(looks[i], s))
            return looks[i];
    return null;
}

// the parts Mix & match lists, in rail order
var mixParts = ["bar", "launcher", "notifs", "osd", "lock", "wallpaper", "dock", "widgets", "titlebars"];

function shell(id) {
    id = String(id);
    if (id.indexOf("user:") === 0)
        return { id: id, name: id.slice(5) + " (yours)", colour: "#D0BCFF", ported: true, blurb: "Your own file" };
    if (id === "off")
        return { id: "off", name: "Off", colour: "#8a8f98", ported: true, blurb: "Not shown" };
    for (var i = 0; i < shells.length; i++)
        if (shells[i].id === id)
            return shells[i];
    for (var k in pieces) {
        var ch = pieces[k].choices || [];
        for (var j = 0; j < ch.length; j++)
            if (ch[j].id === id)
                return ch[j];
    }
    return { id: id, name: id, colour: "#8a8f98", ported: false, blurb: "" };
}

// the choices for one part: its own list, or every shell + Off + your files
function pickerModel(piece, userNames) {
    if (!piece || !piece.mix)
        return [];
    if (piece.choices)
        return piece.choices.map(function (c) { return { id: c.id, name: c.name, colour: c.colour, blurb: c.blurb, offered: true }; });
    var out = shells.map(function (s) {
        return { id: s.id, name: s.name, colour: s.colour, blurb: s.blurb, offered: s.ported && piece.from.indexOf(s.id) >= 0 };
    });
    out.push({ id: "off", name: "Off", colour: "#8a8f98", blurb: "Not shown", offered: true });
    (userNames || []).forEach(function (n) {
        out.push({ id: "user:" + n, name: n, colour: "#D0BCFF", blurb: "Yours · user/" + piece.user + "/" + n + ".qml", offered: true });
    });
    return out;
}

// every tab as one flat list, for search and IPC: { group, groupIdx, tab, tabIdx, name, words }
function flatTabs() {
    var out = [];
    groups.forEach(function (g, gi) {
        g.tabs.forEach(function (t, ti) {
            var p = t.piece ? pieces[t.piece] : null;
            out.push({ group: g.id, groupIdx: gi, tab: t.id, tabIdx: ti, name: t.name, groupName: g.name,
                       words: (g.name + " " + t.name + " " + (p ? p.desc : "")).toLowerCase() });
        });
    });
    return out;
}

// "lockScreenBlur" -> "Lock screen blur"
function human(key) {
    var s = String(key).replace(/[_-]+/g, " ").replace(/([a-z0-9])([A-Z])/g, "$1 $2").toLowerCase();
    return s.charAt(0).toUpperCase() + s.slice(1);
}

// shell and look logos in logos/ (each project's own mark; Debian's from Papirus, GPL-3.0). "aether" is drawn live
// by AetherLogo; ids with no file (zenith, dss, off, your files) fall back to their Material icon.
var logos = { caelestia: "caelestia.svg", noctalia: "noctalia.svg", dms: "dms.svg", end4: "end4.svg", ryoku: "ryoku.png",
    debian: "debian.svg", serp: "serp.svg" };

function logoFile(id) {
    return logos[String(id)] || "";
}
// logos drawn in black, shown on a light tile
var darkLogos = { caelestia: true, serp: true };
