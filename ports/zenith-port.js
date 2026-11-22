// Puts Zenith (https://github.com/zaeemali272/zenith-shell, Unlicense / public domain) inside Caelestia for
// Windows, like noctalia-port.js does for Noctalia. Zenith's imports are relative, so its tree is copied as is
// into config/zenith; shell.qml becomes ZenithShell.qml, whose parts are switched by WinSettings.mix*.
//   node zenith-port.js   (from dotfiles/)
const fs = require("fs");
const path = require("path");
const L = require("./shellport/lib.js");

const HERE = __dirname;
const SRC = path.join(HERE, "..", "ref", "zenith", "zenith-shell-main");
const OUTS = [path.join(HERE, "caelestia-win", "zenith"), path.join(HERE, "caelestia-shell-win", "zenith")];
const OVR = path.join(HERE, "zenith-overrides");
const ROOT = '(Quickshell.shellDir + "/zenith")';

function transform(rel, t) {
    t = L.common(t, { rootExpr: ROOT });
    // weather.sh needs bash + jq; curl.exe (part of Windows) gets wttr.in's own format, which Zenith reads
    // Mix & match bar pieces: other shells' widgets (WinSettings.barPieces) at the start of Zenith's right side
    if (rel === "bar/Bar.qml") {
        t = L.addImport(t, "import qs.services as Cae");
        t = t.replace("            Tray { menuRef: trayPopup }\n",
            "            Repeater { // Windows: Mix & match pieces from the other shells\n" +
            "                model: Cae.WinSettings.barPieces ?? []\n" +
            "                delegate: Loader {\n                    required property var modelData\n" +
            "                    Layout.alignment: Qt.AlignVCenter\n" +
            "                    source: Qt.resolvedUrl(\"../../modules/bar/pieces/MixPiece.qml\")\n" +
            "                    onLoaded: { item.vertical = false; item.screen = Qt.binding(() => bar.screen); item.pieceId = String(modelData); }\n" +
            "                }\n            }\n\n" +
            "            Tray { menuRef: trayPopup }\n");
        if (!t.includes("Mix & match pieces from the other shells"))
            throw new Error("zenith-port: bar/Bar.qml changed upstream - update the Mix & match bar transform");
    }
    // Mix & match inside the lock (MIXING-PLAN.md): Zenith blurs a capture of the screen, so "auto" already
    // shows the shell picked under Wallpaper; "caelestia" blurs Caelestia's wallpaper instead
    if (rel === "windows/lock/LockSurface.qml") {
        t = L.addImport(t, "import qs.services as Cae");
        t = t.replace("        ScreencopyView {\n            anchors.fill: parent\n            captureSource: root.screen\n            live: false\n        }\n",
            "        ScreencopyView {\n            anchors.fill: parent\n            captureSource: root.screen\n            live: false\n        }\n\n" +
            "        Image { // Windows: Mix & match (WinSettings.lockWallpaper)\n            anchors.fill: parent\n" +
            "            visible: Cae.WinSettings.lockWallpaper === \"caelestia\" && !!Cae.Wallpapers.current\n" +
            "            source: visible ? \"file:///\" + String(Cae.Wallpapers.current).split(\"\\\\\").join(\"/\") : \"\"\n" +
            "            fillMode: Image.PreserveAspectCrop\n            asynchronous: true\n        }\n");
        if (!t.includes("Mix & match (WinSettings.lockWallpaper)"))
            throw new Error("zenith-port: windows/lock/LockSurface.qml changed upstream - update the Mix & match lock transform");
    }
    if (rel === "services/WeatherService.qml")
        t = t.replace('command: ["bash", scriptPath]', 'command: ["curl.exe", "-s", "-m", "10", "https://wttr.in/?format=j1"] // Windows');
    // per-app volume: wpctl -> the Windows VolumeService (overrides/services/VolumeService.qml)
    if (rel === "bar/Menu/components/VolumeContent.qml")
        t = t.replace(/setAppVol\.command = \["sh", "-c", "wpctl set-volume "[^\n]*\n\s*setAppVol\.running = false;\n\s*setAppVol\.running = true;/,
            "VolumeService.setAppVolume(appId, v); // Windows");
    if (rel === "bar/Menu/components/WallpaperContent.qml") {
        t = L.addImport(t, "import qs.services as Cae");
        // no python thumbnail generator: show each picture itself, decoded at thumbnail size
        t = t.replace(/source: [^\n]*\/\.cache\/wallpaper_thumbs\/[^\n]*/, "source: fileUrl; sourceSize.width: 320 // Windows");
        t = t.replace(/(Process \{ id: thumbGen; command: )\[[^\]]*\]\]?/, '$1["cmd.exe", "/c", "exit 0"] /* Windows */');
        // awww / theme script -> Caelestia's wallpaper (which also recolours Caelestia's scheme)
        t = t.replace(/function applyWallpaper\(path\) \{[\s\S]*?\n    \}/,
            "function applyWallpaper(path) { // Windows: Caelestia sets it\n" +
            "        Cae.Wallpapers.setWallpaper(decodeURIComponent(String(path).replace(/^file:\\/+/, \"\")));\n" +
            "        CenterState.close();\n    }");
    }
    // Windows hands app icons over as absolute files (C:/.../notification-icons/x.png): use them as they are
    // instead of looking them up inside Linux icon themes
    if (rel === "services/NotificationService.qml")
        t = t.replace("let candidates = [];", "let candidates = [];\n" +
            "            for (const p of [rawImg, rawIcon]) // Windows\n" +
            "                if (/^[A-Za-z]:[\\\\/]/.test(p)) candidates.push(\"file:///\" + p.split(\"\\\\\").join(\"/\"));");
    // (notifications pass through a ListModel, which turns that list into a model - so the fetcher, which
    // every icon falls back to, needs the same rule)
    if (rel === "windows/IconsFetcher.qml")
        t = t.replace(/(function getIconCandidates\([^)]*\) \{[\s\S]*?let candidates = \[\];)/, "$1\n" +
            "        if (/^[A-Za-z]:[\\\\/]/.test(raw)) // Windows: an absolute icon file\n" +
            "            return [\"file:///\" + raw.split(\"\\\\\").join(\"/\")];");
    // the shell's parts sit in loaders on Windows (Mix & match), so their ids are the loaders' items
    if (rel === "bar/Menu/components/NotificationPopup.qml")
        t = t.replace(/\bosdPopup\.visible\b/g, "(osdLoader.item?.visible ?? false)");
    if (rel === "bar/Menu/components/OsdPopup.qml")
        t = t.replace(/- bar\.height\b/g, "- (barLoader.item?.height ?? 0)");
    // productivity (tasks, Pomodoro): saved with python3 and read with sh/cat upstream - neither is on Windows
    if (rel === "services/ProductivityService.qml") {
        t = t.replace(/^import Quickshell\.Io$/m, "import Quickshell.Io\nimport Quickshell.Windows as Win");
        t = t.replace(/saveProcess\.command = \["python3"[^\n]*\n\s*saveProcess\.running = true;/,
            "Win.FsUtils.makePath(storagePath.slice(0, storagePath.lastIndexOf(\"/\"))); // Windows: no python\n" +
            "        winStore.setText(JSON.stringify(data));");
        t = t.replace('command: ["sh", "-c", "cat \\"$1\\" 2>/dev/null || cat \\"$2\\"", "_", service.storagePath, service._legacyStoragePath]',
            'command: ["cmd.exe", "/d", "/c", "type", service.storagePath.split("/").join("\\\\")] // Windows');
        t = t.replace("    Process { id: saveProcess }",
            "    Process { id: saveProcess }\n    FileView { id: winStore; path: service.storagePath; blockWrites: false; atomicWrites: true; printErrors: false } // Windows");
        if (!t.includes("winStore.setText(") || !t.includes('"type", service.storagePath') || !t.includes("id: winStore"))
            throw new Error("zenith-port: ProductivityService.qml changed upstream - update the save/load transform");
    }
    // themes: zenith-theme.sh (bash + python3 + matugen) is not on Windows. Presets write their palette to
    // Colors' file straight from themes/presets; "dynamic" (from the wallpaper) takes Caelestia's wallpaper
    // palette instead of matugen's; the theme state is saved with a FileView instead of a sh heredoc
    if (rel === "services/ThemeService.qml") {
        t = L.addImport(t, "import qs.services as Cae");
        t = L.addImport(t, "import Quickshell.Windows as Win");
        t = t.replace("        listProc.running = true;", "        // Windows: the built-in preset list (no zenith-theme.sh --list)");
        t = t.split("        applyProc.command = cmd;\n        applyProc.running = true;").join("        root.winApply(cmd); // Windows");
        t = t.split("            applyProc.command = cmd;\n            applyProc.running = true;").join("            root.winApply(cmd); // Windows");
        t = t.replace(/(    function saveState\(\) \{\n)        saveProc\.command = \["sh", "-c", "cat <<'EOF' > " \+ root\.configPath \+ "\\n" \+ (JSON\.stringify\(\{[\s\S]*?\}, null, 2\)) \+ "\\nEOF"\];\n        saveProc\.running = true;/,
            "$1        Win.FsUtils.makePath(root.configPath.slice(0, root.configPath.lastIndexOf(\"/\"))); // Windows\n        winConfigOut.setText($2);");
        t = t.replace("    function setBorderGlow(style) {", `    // Windows: what zenith-theme.sh did - [--preset id [--accent hex]] or [--dynamic [wall] [--color hex]]
    function winApply(cmd) {
        const at = flag => { const i = cmd.indexOf(flag); return i >= 0 ? (cmd[i + 1] ?? "") : ""; };
        let palette = null;
        if (cmd.includes("--preset")) {
            winPresetFile.path = Quickshell.shellDir + "/zenith/themes/presets/" + at("--preset") + ".json";
            try {
                palette = JSON.parse(winPresetFile.text());
            } catch (e) {
                console.warn("Zenith theme: preset not found", at("--preset"));
                return;
            }
            const accent = at("--accent");
            if (accent.startsWith("#")) {
                palette.primary = accent;
                palette.accent = accent;
            }
        } else {
            palette = {};
            const p = Cae.Colours.palette;
            for (const key in p)
                if (/^m3[a-z]/.test(key) && typeof p[key] !== "function")
                    palette[key.slice(2).replace(/[A-Z]/g, c => "_" + c.toLowerCase())] = String(p[key]).replace(/^#ff([0-9a-f]{6})$/i, "#$1");
            palette.accent = at("--color").startsWith("#") ? at("--color") : palette.primary;
        }
        const palettePath = ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split(String.fromCharCode(92)).join("/")) + "/.cache/zenith/colors.json"; // = Colors.palettePath
        Win.FsUtils.makePath(palettePath.slice(0, palettePath.lastIndexOf("/")));
        winPaletteOut.path = palettePath;
        winPaletteOut.setText(JSON.stringify(palette, null, 2));
        saveState();
    }

    // dynamic mode follows Caelestia's wallpaper colours as they change
    property Connections winDynamic: Connections {
        target: Cae.Colours.palette
        enabled: root.themeMode === "dynamic"
        function onM3primaryChanged() { winDynamicTimer.restart(); }
        function onM3surfaceChanged() { winDynamicTimer.restart(); }
    }
    property Timer winDynamicTimer: Timer {
        interval: 800
        onTriggered: if (root.themeMode === "dynamic") root.winApply(["--dynamic"].concat(root.accentColor ? ["--color", root.accentColor] : []))
    }
    property FileView winPresetFile: FileView { blockLoading: true; printErrors: false }
    property FileView winPaletteOut: FileView { blockWrites: false; atomicWrites: true; printErrors: false }
    property FileView winConfigOut: FileView { path: root.configPath; blockWrites: false; atomicWrites: true; printErrors: false }

    function setBorderGlow(style) {`);
        if ((t.match(/root\.winApply\(cmd\); \/\/ Windows/g) || []).length !== 3 || !t.includes("winConfigOut.setText(") || !t.includes("function winApply("))
            throw new Error("zenith-port: ThemeService.qml changed upstream - update the theme transform");
    }
    // sleep on idle is Windows' own (Power settings), as for DMS: Zenith never suspends the PC by itself
    if (rel === "services/IdleService.qml")
        t = t.replace('Process { id: suspendProc; command: ["systemctl", "suspend"] }',
            'Process { id: suspendProc; command: ["cmd.exe", "/c", "exit", "0"] } // Windows: Windows\' own sleep settings');
    return t;
}

function host(text) {
    let t = transform("shell.qml", text);
    t = t.replace("import Quickshell.Io\n", 'import Quickshell.Io\nimport qs.services as Cae\n');
    // the Linux named-pipe reader (sh + mkfifo, forever) does not exist on Windows; the IpcHandler stays
    t = t.replace(/(Process \{\s*\n\s*id: ipcReader[\s\S]*?running: )true/, "$1false // Windows: no named pipe");
    // each part only when Mix & match picked Zenith for it
    t = L.gate(t, "Bar", 'Cae.WinSettings.mixBar === "zenith"', "barLoader");
    t = L.gate(t, "ControlCenter", 'Cae.WinSettings.mixBar === "zenith"');
    t = L.gate(t, "QuickSettingsMenu", 'Cae.WinSettings.mixBar === "zenith"');
    t = L.gate(t, "NotificationPopup", 'Cae.WinSettings.mixNotifs === "zenith"');
    t = L.gate(t, "OsdPopup", 'Cae.WinSettings.mixOsd === "zenith"', "osdLoader");
    t = L.gate(t, "LockScreen", 'Cae.WinSettings.mixLock === "zenith"');
    // the bar is inside a loader now: the focus grab takes it from there
    t = t.replace("windows: [bar, dynamicIslandOverlay]", "windows: [barLoader.item, dynamicIslandOverlay].filter(w => !!w)");
    return "// Zenith (https://github.com/zaeemali272/zenith-shell, public domain) running inside Caelestia for Windows.\n" +
        "// Generated by dotfiles/zenith-port.js from Zenith's shell.qml - edit the script, not this file.\n" + t;
}

for (const out of OUTS) {
    L.emptyDir(out);
    L.copyTree(SRC, "", out, {
        skip: new Set([".git", "flake.nix", "flake.lock", "tests", "pp.png", "launch.sh"]),
        transform,
        special: {
            "shell.qml": (src, o) => fs.writeFileSync(path.join(o, "ZenithShell.qml"), host(fs.readFileSync(src, "utf8")))
        }
    });
    L.applyOverrides(OVR, out);
    console.log("ported to", out);
}
