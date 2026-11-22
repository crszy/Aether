// Ryoku (github.com/Ryoku-dev/ryoku, GPL-3.0) on Windows: its Quickshell desktop, laid out the way its QML
// imports expect, plus its C++ plugin (Ryoku.Blobs) built for Windows.
//
//   node ryoku-port.js          -> dotfiles/ryoku-win/        (QML: shell/, stage/, inir/, Ryoku/<modules>)
//                                   dotfiles/ryoku-plugin-win/ (patched plugin sources + a Windows CMakeLists)
//
// Run it as its own Quickshell instance (Aether starts it when Mix & match picks a Ryoku part):
//   QML_IMPORT_PATH=<ryoku-win>  qsw.exe -p <ryoku-win>/shell/shell.qml
// Every edit is a transform of the upstream source (it throws when upstream changed under it) - ref/ryoku stays
// a clean checkout.
const fs = require("fs");
const path = require("path");
const L = require("./shellport/lib.js");

const HERE = __dirname;
const SRC = path.join(HERE, "..", "ref", "ryoku", "ryoku");
const OUT = path.join(HERE, "ryoku-win");
const PLUGIN_OUT = path.join(HERE, "ryoku-plugin-win");
const OVR = path.join(HERE, "ryoku-overrides");

// ---------------------------------------------------------------- QML transforms
function transform(rel, t) {
    t = t.replace(/\r\n/g, "\n");
    // the Windows answers to Ryoku's Linux commands (AetherCompat.RyokuCompat) are made at startup
    if (rel === "shell/shell.qml") {
        const before = t;
        t = L.addImport(t, "import AetherCompat");
        t = t.replace("    Timer { interval: 1500; running: true; repeat: false; onTriggered: root.warm = 1 }",
            "    Timer { interval: 1500; running: true; repeat: false; onTriggered: root.warm = 1 }\n" +
            "    readonly property var aetherCompat: RyokuCompat // Windows: Linux commands answered (Aether)");
        if (t === before || !t.includes("aetherCompat: RyokuCompat"))
            throw new Error("ryoku-port: shell.qml changed upstream - update the AetherCompat hook");
    }
    // Ryoku's apps (apps/<name>/shell.qml) answer the same commands: the hook goes on their ShellRoot
    if (/^apps\/[^/]+\/shell\.qml$/.test(rel)) {
        t = L.addImport(t, "import AetherCompat");
        const root = t.match(/^ShellRoot \{\n/m);
        if (!root) throw new Error("ryoku-port: " + rel + " has no ShellRoot - update the AetherCompat hook");
        t = t.replace(root[0], root[0] + "    readonly property var aetherCompat: RyokuCompat // Windows: Linux commands answered (Aether)\n");
    }
    // Aether: Ryoku's Kit, its keys hint and the reload cover go on the main monitor (Windows' is at 0,0), not on every one
    if (/^apps\/(keys|keys-hint|reload-cover)\/shell\.qml$/.test(rel)) {
        const before = t;
        t = t.replace("model: Quickshell.screens\n",
            "model: Quickshell.screens.filter(s => s.x === 0 && s.y === 0).length ? Quickshell.screens.filter(s => s.x === 0 && s.y === 0).slice(0, 1) : Quickshell.screens.slice(0, 1) // Aether: the main monitor\n");
        if (t === before) throw new Error("ryoku-port: " + rel + " changed upstream - update the main-monitor rule");
    }
    // tray icons on Windows are image:// URLs (overrides/shell/services/Tray.qml): only a plain path gets file:///
    t = t.replace(/"file:\/\/\/?" \+ ([A-Za-z0-9_.]+)\.iconPath\b/g,
        (m, e) => `(/^[a-z]+:/i.test(String(${e}.iconPath)) ? ${e}.iconPath : "file:///" + ${e}.iconPath)`);
    // qylock clockwork theme: its font came from an async folder listing (fallback "MS Sans Serif" until then)
    if (rel === "lockscreen/themes/clockwork/orbital/Main.qml")
        t = t.replace(`source: fontFolder.count > 0 ? "font/" + fontFolder.get(0, "fileName") : ""`,
            `source: fontFolder.count > 0 ? "font/" + fontFolder.get(0, "fileName") : "font/Outfit-Black.ttf"`);
    // the Hub waits for `ryoku-hub desktop restore` before quitting; if that cannot start, it never closed - quit
    // after 3 s regardless
    if (rel === "apps/hub/Hub.qml") {
        const before = t;
        t = t.replace("    Process { id: hyprRestore; onRunningChanged: if (!running && hub.quitting) Qt.quit() }\n",
            "    Process { id: hyprRestore; onRunningChanged: if (!running && hub.quitting) Qt.quit() }\n" +
            "    Timer { id: aetherQuitGuard; interval: 3000; onTriggered: Qt.quit() } // Aether: never stuck open\n");
        t = t.replace("            hyprRestore.running = true;\n", "            hyprRestore.running = true;\n            aetherQuitGuard.start();\n");
        if (t === before || !t.includes("aetherQuitGuard.start()"))
            throw new Error("ryoku-port: Hub.qml requestQuit changed upstream - update the quit guard");
    }
    // Aether Settings from Ryoku's launcher: "/Aether Settings" in the actions list, and ">aether" (the ">" prefix is
    // Ryoku's package search - an Aether row comes first while the query is a start of "aether")
    if (rel === "shell/modules/launcher/shared/providers/actions/catalog.js") {
        const anchor = /(\n\s*\{ id: "open-settings",[^\n]*\n)/;
        if (!anchor.test(t))
            throw new Error("ryoku-port: catalog.js open-settings entry changed upstream - update the Aether entry");
        t = t.replace(anchor, '$1    { id: "aether-settings",  name: "Aether Settings",    category: "Settings",   icon: "tune",        exec: ["ryoku-shell", "aether"] },\n');
    }
    if (rel === "shell/modules/launcher/shared/providers/packages/Packages.qml") {
        const before = t;
        t = t.replace("    function query(text) {\n",
            "    function query(text) { // Aether: \">aether\" (Aether Settings) and your commands.json commands come first\n" +
            "        var aetherRows = AetherUser.rows(text, Spawn);\n" +
            "        if (aetherRows.length > 0)\n" +
            "            return aetherRows;\n" +
            "        return packages._ryokuQuery(text);\n" +
            "    }\n\n    function _ryokuQuery(text) {\n");
        if (t === before)
            throw new Error("ryoku-port: Packages.qml query() changed upstream - update the >aether row");
        t = L.addImport(t, "import AetherCompat");
    }
    // FileView { path: Qt.resolvedUrl("x.json") } reached Quickshell as "file:C:/..." on Windows and never loaded
    // (the launcher's catalog.json among them: no launcher). A plain C:/... path instead.
    t = t.replace(/\bpath:\s*Qt\.resolvedUrl\(([^()]*(?:\([^()]*\))?[^()]*)\)/g,
        (m, arg) => `path: String(Qt.resolvedUrl(${arg})).replace(/^file:\\/*/, "")`);
    // Windows paths: trimFileProtocol("file:///C:/x") gave "/C:/x" - it now gives "C:/x"
    if (/(^|\/)FileUtils\.qml$/.test(rel) && t.includes("    function trimFileProtocol(str) {\n")) {
        t = t.replace("    function trimFileProtocol(str) {\n",
            "    function trimFileProtocol(str) { // Aether (Windows): a drive path keeps no leading slash\n" +
            "        return String(_trimFileProtocolUnix(str)).replace(/^\\/([A-Za-z]:\\/)/, \"$1\");\n" +
            "    }\n\n    function _trimFileProtocolUnix(str) {\n");
    }
    // Aether Mix & match: each part of Ryoku shows only while it is picked (AetherCompat.AetherParts)
    if (rel === "shell/shell.qml") {
        const before = t;
        const swap = (a, b) => {
            if (!t.includes(a)) throw new Error("ryoku-port: shell.qml part gate changed upstream: " + a.trim().slice(0, 60));
            t = t.replace(a, b);
        };
        // bar (the frame bars)
        swap("            Frame {\n                modelData: perScreen.modelData\n",
            "            LazyLoader { // Aether: Ryoku's bar is a Mix & match part\n                active: AetherParts.bar\n            Frame {\n                modelData: perScreen.modelData\n");
        swap("                dockLaneCenter: dockLoader.item ? dockLoader.item.bandCenter : 0\n            }\n",
            "                dockLaneCenter: dockLoader.item ? dockLoader.item.bandCenter : 0\n            }\n            }\n");
        // wallpaper & desktop (and the widgets that live on it)
        swap("                active: true\n                widgetsEnabled: Tokens.widgetsEnabledFor(perScreen.modelData.name)\n",
            "                active: AetherParts.wallpaper || AetherParts.widgets\n                aetherWallpaper: AetherParts.wallpaper\n" +
            "                widgetsEnabled: AetherParts.widgets && Tokens.widgetsEnabledFor(perScreen.modelData.name)\n");
        // dock
        swap("                surfaceVisible: !root.gokkoRunning && desktop.surfaceReady\n",
            "                surfaceVisible: AetherParts.dock && !root.gokkoRunning && desktop.surfaceReady\n");
        swap("        active: !root.gokkoRunning && root.docksMayMount\n", "        active: AetherParts.dock && !root.gokkoRunning && root.docksMayMount\n");
        // OSD and notification pop-ups
        for (const id of ["osdVolumeLoader", "osdMicLoader", "osdBrightnessLoader", "osdKeyboardLoader"])
            swap(`                id: ${id}\n                activeAsync: root.warm >= 1\n`, `                id: ${id}\n                activeAsync: AetherParts.osd && root.warm >= 1\n`);
        swap("                activeAsync: root.warm >= 1 || Notifs.popups.length > 0\n",
            "                activeAsync: AetherParts.notifs && (root.warm >= 1 || Notifs.popups.length > 0)\n");
        if (t === before) throw new Error("ryoku-port: shell.qml part gates");
    }
    // Aether: the wallpaper key pressed again while a reveal runs speeds that reveal to its end (~180 ms,
    // aetherHurry) and then reveals the next one - Ryoku cut to the end at once and only then started decoding the
    // next picture (the freeze). The next picture decodes during the hurry (aetherPreload, Qt's image cache).
    if (rel === "shell/modules/wallpaper/Backdrop.qml") {
        const before = t;
        t = t.replace("        if (revealAnim.running)\n            view.commitInstant();\n",
            "        if (revealAnim.running || aetherHurry.running) { // Aether: hurry the running reveal, then reveal this one\n" +
            "            aetherPreload.source = view.url;\n" +
            "            if (revealAnim.running) {\n" +
            "                revealAnim.stop(); // an explicit stop emits no `finished`\n" +
            "                aetherHurry.from = reveal.progress;\n" +
            "                aetherHurry.start();\n" +
            "            }\n" +
            "            return;\n" +
            "        }\n");
        t = t.split("            cache: false\n            asynchronous: true\n            sourceSize.width: view.decodeW\n")
            .join("            cache: true // Aether: shares the picture aetherPreload decoded\n            asynchronous: true\n            sourceSize.width: view.decodeW\n");
        t = t.replace("            onFinished: {\n                view.commit();\n                view.startVideo();\n                view.maybeYield();\n            }\n        }\n",
            "            onFinished: {\n                view.commit();\n                view.startVideo();\n                view.maybeYield();\n            }\n        }\n\n" +
            "        // Aether: the rest of a reveal, fast, because another wallpaper is waiting\n" +
            "        NumberAnimation {\n            id: aetherHurry\n            target: reveal\n            property: \"progress\"\n" +
            "            to: 1\n            duration: 180\n            easing.type: Easing.OutCubic\n" +
            "            onFinished: {\n                view.commit();\n" +
            "                const front = view.aFront ? imgA : imgB;\n" +
            "                if (front.source != view.url)\n                    view.beginReveal(); // the newest wallpaper\n" +
            "                else {\n                    view.startVideo();\n                    view.maybeYield();\n                }\n" +
            "            }\n        }\n");
        t = t.replace("    Timer {\n        id: renderTimer\n",
            "    // Aether: the next picture decodes here during a hurried reveal (never shown itself)\n" +
            "    Image {\n        id: aetherPreload\n        visible: false\n        cache: true\n        asynchronous: true\n" +
            "        sourceSize.width: view.decodeW\n        sourceSize.height: view.decodeH\n    }\n\n" +
            "    Timer {\n        id: renderTimer\n");
        if (t === before || !t.includes("aetherHurry.start();") || !t.includes("id: aetherHurry") || !t.includes("id: aetherPreload") ||
            (t.match(/cache: true \/\/ Aether: shares/g) || []).length !== 2)
            throw new Error("ryoku-port: wallpaper/Backdrop.qml changed upstream - update the wallpaper hurry");
    }
    if (rel === "shell/modules/desktop/Desktop.qml") {
        const before = t;
        t = t.replace("    property bool active: true\n",
            "    property bool active: true\n    // Aether: the wallpaper is a Mix & match part of its own (the widgets can show without it)\n    property bool aetherWallpaper: true\n");
        t = t.replace("        WallpaperMod.Backdrop {\n            id: backdrop\n            anchors.fill: parent\n",
            "        WallpaperMod.Backdrop {\n            id: backdrop\n            anchors.fill: parent\n            opacity: root.aetherWallpaper ? 1 : 0\n");
        if (t === before || !t.includes("opacity: root.aetherWallpaper"))
            throw new Error("ryoku-port: Desktop.qml wallpaper gate changed upstream");
        // Windows: the widget layer lifts above apps for typing only while it really has the keyboard (or 1.5 s
        // to get it). A stranded kbWanted left the desktop over every window: only the wallpaper, clicks lost
        // (2026-11-15, "edit desktop" - twice).
        const layerBefore = t;
        t = t.replace(
            `        WlrLayershell.layer: (root.stageComposing || win.kbWanted > 0)
            ? WlrLayer.Top : WlrLayer.Bottom
`,
            `        WlrLayershell.layer: (root.stageComposing || win.aetherKbLive)
            ? WlrLayer.Top : WlrLayer.Bottom
        // Aether (Windows): the lift for typing lasts only while this surface really has the keyboard (or 1.5 s
        // after a field asked for it, to get there). A stranded kbWanted (a field's "editing" left true) kept the
        // whole desktop layer over every app - only the wallpaper showed, clicks went nowhere (2026-11-15).
        readonly property bool aetherActive: !!(win.contentItem && win.contentItem.Window.window
            && win.contentItem.Window.window.active)
        property bool aetherKbGrace: false
        readonly property bool aetherKbAsked: kbWanted > 0
        onAetherKbAskedChanged: if (aetherKbAsked) { aetherKbGrace = true; aetherKbGraceT.restart(); }
        Timer { id: aetherKbGraceT; interval: 1500; onTriggered: win.aetherKbGrace = false }
        readonly property bool aetherKbLive: kbWanted > 0 && (aetherActive || aetherKbGrace)
`);
        t = t.replace("        WlrLayershell.keyboardFocus: kbWanted > 0 ? WlrKeyboardFocus.Exclusive",
            "        WlrLayershell.keyboardFocus: aetherKbLive ? WlrKeyboardFocus.Exclusive");
        if (t === layerBefore || !t.includes("aetherKbLive ? WlrKeyboardFocus.Exclusive") || !t.includes("|| win.aetherKbLive)"))
            throw new Error("ryoku-port: Desktop.qml layer lift changed upstream");
        // Windows: the lifted surface takes every click (no pass-through for its empty parts), so the typing hold
        // could never end - a click on an empty part of the desktop ends it; a stranded count goes back to 0
        const clickBefore = t;
        t = t.replace(`            : WlrKeyboardFocus.None

        anchors { top: true; left: true; right: true; bottom: true }
`, `            : WlrKeyboardFocus.None

        anchors { top: true; left: true; right: true; bottom: true }

        // Aether (Windows): while lifted for typing this surface covers the whole screen and takes every click
        // (on Linux the empty parts let clicks through to the windows below). The hold only ends when the field
        // loses focus to another window - which nobody could reach, so the desktop stayed over every app. A
        // click on an empty part of the desktop now ends the typing, and a stranded count is put back to 0.
        MouseArea {
            z: -1000
            anchors.fill: parent
            enabled: win.kbWanted > 0 && !root.stageComposing
            onPressed: mouse => {
                mouse.accepted = false;
                root.releaseHostedWidgetTyping();
                if (win.contentItem)
                    win.contentItem.forceActiveFocus();
                aetherStrandT.restart();
            }
        }
        Timer {
            id: aetherStrandT
            interval: 300
            onTriggered: if (win.kbWanted > 0 && !root.stageComposing) win.kbWanted = 0
        }
        Connections {
            target: win
            function onKbWantedChanged() { if (win.kbWanted < 0) win.kbWanted = 0; }
        }
`);
        if (t === clickBefore) throw new Error("ryoku-port: Desktop.qml typing release changed upstream");
    }
    // Aether's WM numbers each monitor's workspaces in a block of ten (11-20 on the 2nd monitor): the orpheus bar
    // padded from 1 and drew 14 slots for 3 workspaces - pad from this monitor's block, label 1, 2, 3 (2026-11-15)
    if (rel === "shell/modules/bar/barstyles/orpheus/modules/Workspaces.qml") {
        const before = t;
        t = t.replace(`        if (last === 0)
            return shown;
        const end = S.Prefs.workspaceSpare && byNumber[last].occupied ? last + 1 : last;
        const filled = [];
        for (let number = 1; number <= end; ++number) {
            const row = byNumber[number];
            filled.push(row ? Object.assign({}, row, { number: number }) : {
                key: String(number),
                label: String(number),`, `        if (last === 0)
            return shown;
        // Aether: its window manager numbers each monitor's workspaces in a block of ten (2nd monitor 11-20):
        // filling from 1 drew 14 slots for 3 workspaces. Fill from the start of this monitor's block and show
        // the numbers within it (1, 2, 3); the real number stays the key, so a click still goes there.
        let lowest = last;
        for (const n in byNumber) lowest = Math.min(lowest, Number(n));
        const base = Math.floor((lowest - 1) / 10) * 10;
        const end = S.Prefs.workspaceSpare && byNumber[last].occupied ? last + 1 : last;
        const filled = [];
        for (let number = base + 1; number <= end; ++number) {
            const row = byNumber[number];
            filled.push(row ? Object.assign({}, row, { number: number, label: String(number - base) }) : {
                key: String(number),
                label: String(number - base),`);
        if (t === before) throw new Error("ryoku-port: orpheus Workspaces.qml padding changed upstream");
    }
    // the Windows basics every port shares: file:/// URLs, layer-shell properties, IdleMonitor / PAM stand-ins (qs.shims)
    t = L.common(t, {});
    // Windows: Ryogami sends C:\x\y paths; Ryoku compares Image.source with the URL string to know when the new
    // wallpaper decoded - Qt stores "file:///C:%5Cx..." so they never matched and nothing was ever revealed (gray
    // desktop). The URLs are built in Qt's own form (forward slashes, run through Qt.resolvedUrl).
    if (rel === "shell/modules/wallpaper/Wallpaper.qml") {
        const before = t;
        t = t.replace(/\? "file:\/\/\/?" \+ frame\.path \+ "\?v=" \+ frame\.revision : ""/,
            '? String(Qt.resolvedUrl("file:///" + frame.path.replace(/\\\\/g, "/") + "?v=" + frame.revision)) : ""');
        t = t.replace(/\? "file:\/\/\/?" \+ frame\.videoPath : ""/,
            '? String(Qt.resolvedUrl("file:///" + frame.videoPath.replace(/\\\\/g, "/"))) : ""');
        if ((t.match(/Qt\.resolvedUrl\("file:\/\/\/" \+ frame\./g) || []).length !== 2 || t === before)
            throw new Error("ryoku-port: Wallpaper.qml URLs changed upstream - update the Windows path fix");
    }
    // Ryoku spreads over several roots (shell/, inir/, stage/): the stand-ins are a normal module on its import path
    t = t.replace(/^import qs.shimss*$/gm, "import AetherShims");
    // Qt.labs.platform is not shipped: its StandardPaths lives in QtCore with the same API
    t = t.replace(/^import Qt.labs.platforms*$/gm, "import QtCore");
    return t;
}

// ---------------------------------------------------------------- the QML tree
L.emptyDir(OUT);
const trees = [
    ["shell/quickshell/shell", "shell"],
    ["shell/quickshell/stage", "stage"],
    ["shell/quickshell/inir", "inir"],
    ["ui", "Ryoku/Ui"],
    ["shell/quickshell/plugins/kit", "Ryoku/PluginKit"],
    ["shell/framebars", "Ryoku/FrameBars"],
    ["wm/hyprland/qml", "Ryoku/Wm/Hyprland"],
    // Ryoku's other Quickshell apps, each its own config (qsw.exe -p apps/<name>/shell.qml), started by the
    // ryoku-* commands (AetherCompat.RyokuCompat maps them)
    ["hub/quickshell", "apps/hub"],
    ["shell/quickshell/keys", "apps/keys"],
    ["shell/quickshell/keys-hint", "apps/keys-hint"],
    ["shell/quickshell/ryoshot", "apps/ryoshot"],
    ["shell/quickshell/ryopin", "apps/ryopin"],
    ["shell/quickshell/welcome", "apps/welcome"],
    ["shell/quickshell/reload-cover", "apps/reload-cover"],
    ["apps/ryostore/quickshell", "apps/ryostore"],
    ["apps/ryovm/quickshell", "apps/ryovm"],
    // qylock, Ryoku's lock screen: lock_shell.qml + shim/SddmShim.qml are Windows versions (ryoku-overrides/lockscreen)
    ["lockscreen/qylock/quickshell-lockscreen", "lockscreen"],
    ["lockscreen/qylock/themes", "lockscreen/themes"],
];
for (const [from, to] of trees) {
    const src = path.join(SRC, from);
    if (!fs.existsSync(src)) throw new Error("ryoku-port: upstream moved " + from);
    const out = path.join(OUT, to);
    fs.mkdirSync(out, { recursive: true });
    L.copyTree(src, "", out, { skip: new Set([".git", "tests"]), transform: (rel, text) => transform(to + "/" + rel, text) });
}

// Ryogami's picker window (its QML lives in the Ryoku.Ryogami plugin): an app like the others
fs.mkdirSync(path.join(OUT, "apps", "ryogami"), { recursive: true });
fs.writeFileSync(path.join(OUT, "apps", "ryogami", "shell.qml"),
    transform("apps/ryogami/shell.qml", fs.readFileSync(path.join(SRC, "shell", "ryogami", "picker", "shell.qml"), "utf8")));

// Ryoku.Spatial3D (the Stage scene's depth mesh) needs Qt Quick3D, which Aether's Qt does not ship: an empty module
// so the one scene that imports it loads (the depth effect is simply off)
fs.mkdirSync(path.join(OUT, "Ryoku", "Spatial3D"), { recursive: true });
fs.writeFileSync(path.join(OUT, "Ryoku", "Spatial3D", "qmldir"), "module Ryoku.Spatial3D\nDepthMeshGeometry 1.0 DepthMeshGeometry.qml\n");
fs.writeFileSync(path.join(OUT, "Ryoku", "Spatial3D", "DepthMeshGeometry.qml"),
    "// Windows: Qt Quick3D is not part of Aether - the Stage depth mesh is off\nimport QtQuick\n\nQtObject {\n    property url source\n    property real depthScale\n    property int subdivisions\n    property bool ready: false\n}\n");

// Caelestia's Windows stand-ins (IdleMonitor, PamContext) where Ryoku's `qs.shims` imports look: qs = shell/
fs.cpSync(path.join(HERE, "caelestia-shell-win", "shims"), path.join(OUT, "AetherShims"), { recursive: true });
fs.writeFileSync(path.join(OUT, "AetherShims", "qmldir"),
    ["module AetherShims", "IdleMonitor 1.0 IdleMonitor.qml", "PamContext 1.0 PamContext.qml", "PamResult 1.0 PamResult.qml",
     "singleton AetherKey 1.0 AetherKey.qml", ""].join("\n"));

L.applyOverrides(OVR, OUT, transform);

// ---------------------------------------------------------------- the plugin (Ryoku.Blobs) for Windows
L.emptyDir(PLUGIN_OUT);
const PSRC = path.join(SRC, "shell", "plugin");
for (const name of fs.readdirSync(PSRC)) {
    const s = path.join(PSRC, name);
    if (fs.statSync(s).isDirectory()) {
        fs.cpSync(s, path.join(PLUGIN_OUT, name), { recursive: true });
    } else if (/\.(cpp|hpp)$/.test(name)) {
        let t = fs.readFileSync(s, "utf8");
        if (name === "systemmonitor.cpp") t = systemMonitorForWindows(t);
        fs.writeFileSync(path.join(PLUGIN_OUT, name), t);
    }
}

// The Linux-only bits of SystemMonitor: the user name (getpwuid) and the root disk (statvfs) through Qt instead.
// Its /proc and /sys reads just find nothing on Windows (those readings stay 0) - WinSystemMonitor fills them later.
function systemMonitorForWindows(t) {
    t = t.replace(/\r\n/g, "\n");
    const before = t;
    t = t.replace("#include <pwd.h>\n#include <sys/statvfs.h>\n#include <unistd.h>\n",
        "#ifndef _WIN32\n#include <pwd.h>\n#include <sys/statvfs.h>\n#include <unistd.h>\n#else\n#include <QStorageInfo>\n#endif\n");
    t = t.replace("QString currentUserName() {\n    passwd entry{};",
        "QString currentUserName() {\n#ifdef _WIN32\n    return qEnvironmentVariable(\"USERNAME\");\n#else\n    passwd entry{};");
    t = t.replace("    return qEnvironmentVariable(\"USER\");\n}",
        "    return qEnvironmentVariable(\"USER\");\n#endif\n}");
    t = t.replace("StorageReading readRootStorage() {\n    struct statvfs stats {};",
        "StorageReading readRootStorage() {\n#ifdef _WIN32\n    const QStorageInfo root(QDir::rootPath());\n    if (!root.isValid() || root.bytesTotal() <= 0)\n        return {};\n" +
        "    StorageReading win;\n    win.totalGiB = static_cast<double>(root.bytesTotal()) / BytesPerGiB;\n" +
        "    win.usedGiB = static_cast<double>(root.bytesTotal() - root.bytesAvailable()) / BytesPerGiB;\n    return win;\n#else\n    struct statvfs stats {};");
    t = t.replace("    out.usedGiB = static_cast<double>((total - std::min(total, available)) / BytesPerGiB);\n    return out;\n}",
        "    out.usedGiB = static_cast<double>((total - std::min(total, available)) / BytesPerGiB);\n    return out;\n#endif\n}");
    t = t.replace("QString currentHostName() {\n    std::array<char, 256> name{};\n    if (gethostname(name.data(), name.size() - 1) != 0)\n        return {};\n    name.back() = '\\0';\n    return QString::fromLocal8Bit(name.data());\n}",
        "QString currentHostName() {\n#ifdef _WIN32\n    return QSysInfo::machineHostName();\n#else\n    std::array<char, 256> name{};\n    if (gethostname(name.data(), name.size() - 1) != 0)\n        return {};\n    name.back() = '\\0';\n    return QString::fromLocal8Bit(name.data());\n#endif\n}");
    if (!t.includes("#include <QSysInfo>")) t = "#include <QSysInfo>\n" + t;
    if ((t.match(/_WIN32/g) || []).length < 4 || t === before)
        throw new Error("ryoku-port: systemmonitor.cpp changed upstream - update the Windows patch");
    if (!t.includes("#include <QDir>")) t = "#include <QDir>\n" + t;
    return t;
}

fs.writeFileSync(path.join(PLUGIN_OUT, "CMakeLists.txt"), `# Ryoku.Blobs for Windows (generated by dotfiles/ryoku-port.js from ryoku/shell/plugin). Ryoku.Spatial3D is left
# out: it needs Qt Quick3D, which Aether's Qt does not have (ryoku-win/Ryoku/Spatial3D is an empty stand-in).
cmake_minimum_required(VERSION 3.21)
project(ryoku-blobs LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Quick ShaderTools Multimedia)
qt_standard_project_setup(REQUIRES 6.5)
qt_add_qml_module(ryoku-blobs
    URI Ryoku.Blobs
    VERSION 1.0
    OUTPUT_DIRECTORY "\${CMAKE_BINARY_DIR}/qml/Ryoku/Blobs"
    SOURCES
        blobgroup.cpp blobgroup.hpp blobshape.cpp blobshape.hpp blobrect.cpp blobrect.hpp
        blobwave.cpp blobwave.hpp blobinvertedrect.cpp blobinvertedrect.hpp blobmaterial.cpp blobmaterial.hpp
        borderbeam.cpp borderbeam.hpp bordermaterial.cpp bordermaterial.hpp camerafeed.cpp camerafeed.hpp
        cavafeed.cpp cavafeed.hpp cavaspectrum.cpp cavaspectrum.hpp wavecapture.cpp wavecapture.hpp
        systemmonitor.cpp systemmonitor.hpp systemgraph.cpp systemgraph.hpp thinkingorb.cpp thinkingorb.hpp
)
target_compile_definitions(ryoku-blobs PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
target_link_libraries(ryoku-blobs PRIVATE Qt6::Quick Qt6::Multimedia)
set_target_properties(ryoku-blobs ryoku-blobsplugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "\${CMAKE_BINARY_DIR}/qml/Ryoku/Blobs"
    LIBRARY_OUTPUT_DIRECTORY "\${CMAKE_BINARY_DIR}/qml/Ryoku/Blobs")
qt_add_shaders(ryoku-blobs "blob_shaders" BATCHABLE GLSL "300es,330" HLSL 50 PREFIX "/"
    FILES shaders/blob.vert shaders/blob.frag shaders/beam.vert shaders/beam.frag)
`);

// ---------------------------------------------------------------- Ryogami's picker (Ryoku.Ryogami) for Windows
// The wallpaper picker is a C++ QML module (ryogami/picker). On Windows: GPUs are listed through DXGI (upstream asks
// Vulkan), the shaders also go to HLSL (Qt Quick draws with Direct3D 11 here), and the DLL lands next to the module.
const RSRC = path.join(SRC, "shell", "ryogami", "picker");
const ROUT = path.join(HERE, "ryogami-picker-win");
L.emptyDir(ROUT);
fs.cpSync(RSRC, path.join(ROUT, "picker"), { recursive: true, filter: s => !/[\\/](tests|build)([\\/]|$)/.test(path.relative(RSRC, s)) || path.relative(RSRC, s) === "" });
fs.mkdirSync(path.join(ROUT, "assets", "brand"), { recursive: true });
fs.copyFileSync(path.join(SRC, "assets", "brand", "logo-mark-v2.png"), path.join(ROUT, "assets", "brand", "logo-mark-v2.png"));
(function patchPicker() {
    const cm = path.join(ROUT, "picker", "CMakeLists.txt");
    let c = fs.readFileSync(cm, "utf8").replace(/\r\n/g, "\n");
    const before = c;
    c = c.replace("find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h REQUIRED)",
        "if(NOT WIN32) # Windows (Aether): GPUs come from DXGI, no Vulkan header needed\n    find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h REQUIRED)\nendif()");
    c = c.replace('"${CMAKE_CURRENT_SOURCE_DIR}/../../../assets/brand/logo-mark-v2.png"', '"${CMAKE_CURRENT_SOURCE_DIR}/../assets/brand/logo-mark-v2.png"');
    c = c.replace("set_target_properties(ryogami-picker ryogami-pickerplugin PROPERTIES\n    LIBRARY_OUTPUT_DIRECTORY",
        "if(WIN32)\n    target_compile_definitions(ryogami-picker PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)\n    target_link_libraries(ryogami-picker PRIVATE dxgi)\nendif()\n" +
        "set_target_properties(ryogami-picker ryogami-pickerplugin PROPERTIES\n    RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/qml/Ryoku/Ryogami\"\n    LIBRARY_OUTPUT_DIRECTORY");
    c = c.replace('    GLSL "330,300es"\n', '    GLSL "330,300es"\n    HLSL 50\n');
    if (c === before || !c.includes("HLSL 50") || !c.includes("dxgi") || !c.includes("../assets/brand"))
        throw new Error("ryoku-port: ryogami picker CMakeLists changed upstream - update the Windows patch");
    fs.writeFileSync(cm, c);

    const gp = path.join(ROUT, "picker", "src", "core", "gpudevices.cpp");
    let g = fs.readFileSync(gp, "utf8").replace(/\r\n/g, "\n");
    const gb = g;
    g = g.replace("#define VK_NO_PROTOTYPES\n#include <vulkan/vulkan.h>\n",
        "#ifdef _WIN32\n#include <windows.h>\n#include <dxgi.h>\n#else\n#define VK_NO_PROTOTYPES\n#include <vulkan/vulkan.h>\n#endif\n");
    g = g.replace("// The id is the device UUID, the same identity the renderer matches on.\n",
        "#ifdef _WIN32\n// Windows (Aether): the GPUs DXGI reports (software adapters left out), keyed by LUID\n" +
        "std::vector<Device> listDevices()\n{\n    std::vector<Device> out;\n    IDXGIFactory1 *factory = nullptr;\n" +
        "    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))))\n        return out;\n" +
        "    IDXGIAdapter1 *adapter = nullptr;\n    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {\n" +
        "        DXGI_ADAPTER_DESC1 desc{};\n        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {\n" +
        "            const QString id = QStringLiteral(\"luid:%1%2\").arg(quint32(desc.AdapterLuid.HighPart), 8, 16, QLatin1Char('0'))\n" +
        "                                   .arg(quint32(desc.AdapterLuid.LowPart), 8, 16, QLatin1Char('0'));\n" +
        "            out.push_back({id, QString::fromWCharArray(desc.Description), QString()});\n        }\n        adapter->Release();\n    }\n" +
        "    factory->Release();\n    return out;\n}\n#else\n// The id is the device UUID, the same identity the renderer matches on.\n");
    g = g.replace("\n} // namespace\n\nGpuDevices::GpuDevices", "\n#endif\n} // namespace\n\nGpuDevices::GpuDevices");
    if ((g.match(/_WIN32/g) || []).length < 2 || !g.includes("#endif\n} // namespace"))
        throw new Error("ryoku-port: gpudevices.cpp changed upstream - update the DXGI patch");
    fs.writeFileSync(gp, g);
})();

console.log("ryoku-port: QML in " + OUT + ", plugin sources in " + PLUGIN_OUT + " and " + ROUT);
