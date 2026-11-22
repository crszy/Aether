// Puts Noctalia v4 (the last Quickshell/QML version, MIT - see noctalia/LICENSE) inside Caelestia for Windows, so
// both shells run in the same process and every part can be picked from either (Settings > Mix & match).
//
//   node noctalia-port.js            (from dotfiles/)  ->  caelestia-win/noctalia, caelestia-shell-win/noctalia
//
// What it changes in Noctalia's files (everything else is left as it is, so a newer v4 can be ported again):
//  - imports qs.Commons / qs.Modules / qs.Services / qs.Widgets / qs.Helpers -> qs.noctalia.*
//    (Windows file names ignore case: Noctalia's "Services" and Caelestia's "services" would be the same folder)
//  - Quickshell.shellDir -> the noctalia folder, so its Assets / Shaders / Translations are found
//  - Quickshell.env("HOME") -> HOME or USERPROFILE (Windows has no HOME)
//  - the services that call home (telemetry ping, GitHub release/contributor lookups, supporters list) do nothing
//  - shell.qml becomes NoctaliaShell.qml: a Scope (not a ShellRoot) whose parts are switched by WinSettings.mix*
const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const SRC = path.join(HERE, "..", "ref", "noctalia-v4", "noctalia-4.7.7");
const OUTS = [path.join(HERE, "caelestia-win", "noctalia"), path.join(HERE, "caelestia-shell-win", "noctalia")];
const SKIP = new Set(["nix", ".github", "flake.nix", "flake.lock", "shell.nix", "lefthook.yml", ".gitignore"]);

const ROOT_EXPR = '(Quickshell.shellDir + "/noctalia")';
const HOME_EXPR = '((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\\\").join("/"))';

function addImport(t, line) {
    if (t.includes(line)) return t;
    return t.replace(/^(import QtQuick\s*)$/m, `$1\n${line}`);
}

function transform(rel, text) {
    let t = text;
    // one wallpaper for every shell: a wallpaper Noctalia sets is set in Caelestia too, and Noctalia shows the
    // wallpaper Caelestia has (whichever shell's picker set it)
    if (rel.split("\\").join("/") === "Services/UI/WallpaperService.qml") {
        t = addImport(t, "import qs.services as CaeWall");
        t = t.replace("  function changeWallpaper(path, screenName, appearanceSlot) {\n",
            "  function changeWallpaper(path, screenName, appearanceSlot) {\n" +
            "    if (typeof path === \"string\" && path && String(CaeWall.Wallpapers.actualCurrent).split(String.fromCharCode(92)).join(\"/\") !== path.split(String.fromCharCode(92)).join(\"/\"))\n" +
            "      CaeWall.Wallpapers.setWallpaper(path); // Windows: shared wallpaper\n");
        t = t.replace(/\n\}\s*$/, "\n\n" +
            "  // Windows: follow Caelestia's wallpaper (set from any shell's picker)\n" +
            "  function followCaelestiaWallpaper() {\n" +
            "    const p = String(CaeWall.Wallpapers.actualCurrent || \"\").split(String.fromCharCode(92)).join(\"/\");\n" +
            "    if (p && Quickshell.screens.some(s => root.getWallpaper(s.name) !== p))\n" +
            "      root.changeWallpaper(p, undefined);\n" +
            "  }\n" +
            "  Connections {\n    target: CaeWall.Wallpapers\n    function onActualCurrentChanged() {\n      root.followCaelestiaWallpaper();\n    }\n  }\n" +
            "  Timer {\n    running: true\n    interval: 4000\n    onTriggered: root.followCaelestiaWallpaper()\n  }\n}\n");
    }
    // Mix & match bar pieces (MIXING-PLAN.md): a Noctalia bar widget placed in Caelestia's vertical bar
    // ("noctalia:<Widget>" in bar.json) is told it sits in a left bar, whatever Noctalia's own bar uses
    if (/^Modules\/Bar\/Widgets\/[^/]+\.qml$/.test(rel.split("\\").join("/")))
        t = t.replace("  readonly property string barPosition: Settings.getBarPositionForScreen(screenName)",
            "  property bool aetherVertical: false // Windows: Mix & match - set when the widget sits in Caelestia's bar\n" +
            "  readonly property string barPosition: aetherVertical ? \"left\" : Settings.getBarPositionForScreen(screenName)");
    // Mix & match bar pieces: other shells' widgets (WinSettings.barPieces) after Noctalia's own right-side
    // widgets, in its vertical and horizontal bar layouts (through Caelestia's modules/bar/pieces/MixPiece.qml)
    if (rel.split("\\").join("/") === "Modules/Bar/Bar.qml") {
        t = t.replace(/^(import QtQuick\s*)$/m, "$1\nimport qs.services as Cae");
        const mix = (vertical, align) =>
            "\n        Repeater { // Windows: Mix & match pieces from the other shells\n" +
            "          model: Cae.WinSettings.barPieces ?? []\n" +
            "          delegate: Loader {\n            required property var modelData\n" +
            `            Layout.alignment: ${align}\n` +
            "            source: Qt.resolvedUrl(\"../../../modules/bar/pieces/MixPiece.qml\")\n" +
            `            onLoaded: { item.vertical = ${vertical}; item.screen = Qt.binding(() => root.screen); item.pieceId = String(modelData); }\n` +
            "          }\n        }\n";
        let n = 0;
        t = t.replace(/("sectionWidgetsCount": root\.rightWidgetsModel\.count\n\s*\}\)\n\s*Layout\.alignment: Qt\.(AlignHCenter|AlignVCenter)\n\s*\}\n\s*\}\n)/g,
            (m, all, align) => { n++; return m + mix(align === "AlignHCenter", "Qt." + align); });
        if (n !== 2)
            throw new Error("noctalia-port: Modules/Bar/Bar.qml changed upstream - update the Mix & match bar transform");
    }
    // Mix & match inside the lock (WinSettings.lockWallpaper, see MIXING-PLAN.md): Noctalia's lock can show
    // Caelestia's wallpaper ("caelestia", or "auto" while another shell draws the wallpaper)
    if (rel.split("\\").join("/") === "Modules/LockScreen/LockScreenBackground.qml") {
        t = t.replace(/^(import QtQuick\s*)$/m, "$1\nimport qs.services as Cae");
        t = t.replace("  property string resolvedWallpaperPath: \"\"",
            "  property string resolvedWallpaperPath: \"\"\n" +
            "  readonly property string aetherWallpaper: ((Cae.WinSettings.lockWallpaper === \"caelestia\" || (Cae.WinSettings.lockWallpaper === \"auto\" && Cae.WinSettings.mixWallpaper !== \"noctalia\")) && Cae.Wallpapers.current) ? String(Cae.Wallpapers.current).split(\"\\\\\").join(\"/\") : \"\" // Windows: Mix & match\n" +
            "  onAetherWallpaperChanged: Qt.callLater(requestCachedWallpaper)");
        t = t.replace("    // Check for solid color mode first\n",
            "    if (aetherWallpaper !== \"\") { // Windows: Mix & match\n      resolvedWallpaperPath = aetherWallpaper; // same form as Noctalia's own paths\n      return;\n    }\n\n    // Check for solid color mode first\n");
        t = t.replace("visible: source !== \"\" && Settings.data.wallpaper.enabled && !Settings.data.wallpaper.useSolidColor",
            "visible: source !== \"\" && (root.aetherWallpaper !== \"\" || (Settings.data.wallpaper.enabled && !Settings.data.wallpaper.useSolidColor))");
        if (!t.includes("onAetherWallpaperChanged") || !t.includes("resolvedWallpaperPath = aetherWallpaper;") || !t.includes("root.aetherWallpaper !== \"\" ||"))
            throw new Error("noctalia-port: LockScreenBackground.qml changed upstream - update the Mix & match lock transform");
    }
    // Aether: the wallpaper key pressed again while a transition plays speeds that transition to its end (~180 ms,
    // aetherHurry) and then plays the next one - Noctalia used to cut to the end at once and restart. The next
    // picture decodes while the hurry runs (aetherPreload), and the change debounce is 50 ms instead of 333.
    if (rel.split("\\").join("/") === "Modules/Background/Background.qml") {
        const before = t;
        t = t.replace("      readonly property bool transitioning: transitionAnimation.running\n",
            "      readonly property bool transitioning: transitionAnimation.running || aetherHurry.running\n" +
            "      property var aetherPending: null   // Aether: waits for the running transition to finish\n");
        t = t.replace("        id: debounceTimer\n        interval: 333\n", "        id: debounceTimer\n        interval: 50 // Aether: 333 swallowed quick key presses\n");
        t = t.replace("        if (transitioning) {\n          // We are interrupting a transition - handle cleanup properly\n",
            "        if (transitioning) { // Aether: hurry the running one, then play this one\n" +
            "          aetherPending = source;\n" +
            "          aetherPreload.source = isSolidSource ? \"\" : source;\n" +
            "          if (transitionAnimation.running) {\n" +
            "            transitionAnimation.stop();\n" +
            "            aetherHurry.from = transitionProgress;\n" +
            "            aetherHurry.start();\n" +
            "          }\n" +
            "          return;\n" +
            "        }\n" +
            "        if (false) {\n          // We are interrupting a transition - handle cleanup properly\n");
        t = t.replace("                         Qt.callLater(() => {\n                                        currentWallpaper.asynchronous = true;\n                                      });\n                       });\n        }\n      }\n",
            "                         Qt.callLater(() => {\n                                        currentWallpaper.asynchronous = true;\n                                      });\n                       });\n" +
            "          // Aether: a wallpaper that arrived during this transition plays now\n" +
            "          if (aetherPending !== null) {\n" +
            "            const aetherNext = aetherPending;\n" +
            "            aetherPending = null;\n" +
            "            Qt.callLater(() => setWallpaperWithTransition(aetherNext));\n" +
            "          }\n" +
            "        }\n      }\n\n" +
            "      // Aether: the rest of a transition, fast, because another wallpaper is waiting\n" +
            "      NumberAnimation {\n        id: aetherHurry\n        target: root\n        property: \"transitionProgress\"\n" +
            "        to: 1.0\n        duration: 180\n        easing.type: Easing.OutCubic\n" +
            "        onFinished: transitionAnimation.finished()\n      }\n\n" +
            "      // Aether: the next picture decodes here during the hurry (never shown itself)\n" +
            "      Image {\n        id: aetherPreload\n        visible: false\n        asynchronous: true\n        cache: false\n      }\n");
        t = t.replace("        cache: false // Not cached - temporary during transitions\n",
            "        cache: true // Aether: cached, so the picture aetherPreload decoded is ready at once\n");
        t = t.replace("        id: aetherPreload\n        visible: false\n        asynchronous: true\n        cache: false\n",
            "        id: aetherPreload\n        visible: false\n        asynchronous: true\n        cache: true\n");
        if (t === before || !t.includes("aetherHurry.start();") || !t.includes("id: aetherHurry") || !t.includes("interval: 50 // Aether") ||
            !t.includes("cache: true // Aether: cached") ||
            !t.includes("setWallpaperWithTransition(aetherNext)") || !t.includes("transitionAnimation.running || aetherHurry.running"))
            throw new Error("noctalia-port: Background.qml changed upstream - update the wallpaper hurry");
    }
    t = t.replace(/\bimport qs\.(Commons|Modules|Services|Widgets|Helpers)\b/g, "import qs.noctalia.$1");
    t = t.replace(/\bQuickshell\.shellDir\b/g, ROOT_EXPR);
    t = t.replace(/\bQuickshell\.env\("HOME"\)/g, HOME_EXPR);
    // "mkdir -p" does not exist on Windows: Quickshell.Windows' FsUtils.makePath does the same
    if (/Quickshell\.execDetached\(\["mkdir", "-p", /.test(t)) {
        t = t.replace(/Quickshell\.execDetached\(\["mkdir", "-p", ([^\]]+)\]\)/g, "WinFs.FsUtils.makePath($1)");
        t = t.replace(/^(import Quickshell\s*)$/m, "$1\nimport Quickshell.Windows as WinFs");
        if (!t.includes("import Quickshell.Windows as WinFs"))
            t = t.replace(/^(import QtQuick\s*)$/m, "$1\nimport Quickshell.Windows as WinFs");
    }

    const base = rel.replace(/\\/g, "/");
    // no network calls home
    if (/Services\/Noctalia\/(TelemetryService|GitHubService|SupporterService)\.qml$/.test(base))
        t = t.replace(/(function init\(\)\s*\{)/, "$1\n    return; // Caelestia for Windows: no calls home\n");
    // Windows' PanelWindow has these itself (no layer-shell attached object for them)
    t = t.replace(/\bWlrLayershell\.exclusionMode:/g, "exclusionMode:");
    t = t.replace(/\bWlrLayershell\.anchors\s*\{/g, "anchors {");
    // compositor blur requests (Wayland ext-background-effect) have no Windows counterpart here
    t = t.replace(/^(\s*)(BackgroundEffect\.blurRegion:.*)$/gm, "$1// Windows: no compositor blur - $2");
    // rounded-corner states on Regions (only used for that blur) are newer than this Quickshell
    if (/(MainScreen|LauncherOverlayWindow)\.qml$/.test(base))
        t = t.replace(/^(\s*)((topLeft|topRight|bottomLeft|bottomRight)Corner:.*)$/gm, "$1// Windows: $2");
    // the dock waits for Noctalia's bar on its screen - there is none unless Mix & match picked Noctalia's bar
    if (/Modules\/Dock\/Dock\.qml$/.test(base)) {
        t = t.replace("readonly property bool hasBar: modelData && modelData.name ?", 'readonly property bool hasBar: Cae.WinSettings.mixBar === "noctalia" && modelData && modelData.name ?');
        t = addImport(t, "import qs.services as Cae");
    }
    // system stats: the Linux readers stay off; a Windows feeder fills the same properties
    if (/Services\/System\/SystemStatService\.qml$/.test(base)) {
        t = t.replace(/running: root\.shouldRun( && root\.gpuAvailable)?\n/g, "running: false // Windows: see _winTick\n");
        const part = fs.readFileSync(path.join(HERE, "noctalia-overrides", "Services", "System", "SystemStatService.windows.part"), "utf8");
        t = t.replace(/\}\s*$/, part + "}\n");
        t = t.replace(/^(import Quickshell\s*)$/m, "$1\nimport Quickshell.Windows as Win");
    }
    // "file://" + "C:/..." is not a valid URL; Windows paths need "file:///C:/..."
    t = t.replace(/(["'`])file:\/\/(?!\/)/g, "$1file:///");
    // colour schemes: `find` -> FsUtils.findFiles (same result handler)
    if (/Services\/Theming\/ColorSchemeService\.qml$/.test(base)) {
        t = t.replace(/findProcess\.command = \["find"[^\n]*\n(\s*)findProcess\.running = true;/,
            'root._winFound = WinFs.FsUtils.findFiles([schemesDirectory, downloadedSchemesDirectory], ["*.json"], 2, 8);\n$1findProcess.exited(0, 0);   // Windows: no `find`; same handler');
        t = t.replace("var output = stdout.text.trim();", 'var output = root._winFound !== null ? root._winFound.join("\\n") : stdout.text.trim();');
        t = t.replace(/(\n  id: root\n)/, '$1  property var _winFound: null   // Windows: files found by FsUtils.findFiles\n');
    }
    // old-cache cleanup runs `find -delete` (Windows' find.exe is a different program) - off
    if (/Services\/UI\/ImageCacheService\.qml$/.test(base))
        t = t.replace(/^(\s*)(Quickshell\.execDetached\(\["find", [^\n]*)$/m, "$1// Windows: no `find -delete` - $2");
    // the plugin list is fetched with git through sh: no network calls from here
    if (/Services\/Noctalia\/PluginService\.qml$/.test(base))
        t = t.replace(/(function refreshAvailablePlugins\(\)\s*\{)/, "$1\n    return; // Caelestia for Windows: no plugin downloads\n");
    // Noctalia's settings open as a panel on its bar by default; without Noctalia's bar/panels (e.g. only
    // Settings style = Noctalia) that panel does not exist, so it opens as its own window then
    if (/Services\/UI\/SettingsPanelService\.qml$/.test(base)) {
        t = t.split('Settings.data.ui.settingsPanelMode === "window"').join('(Settings.data.ui.settingsPanelMode === "window" || Cae.WinSettings.settingsStyle === "noctalia" || !Cae.WinSettings.noctaliaPanels)');
        t = addImport(t, "import qs.services as Cae");
    }
    // Noctalia's settings window behaves like Caelestia's: on top (WinSettings.nexusTopmost) and in front when opened
    if (/Modules\/Panels\/Settings\/SettingsPanelWindow\.qml$/.test(base)) {
        t = addImport(t, "import qs.services as Cae\nimport Quickshell.Windows as WinFrame");
        t = t.replace(/(\n  visible: false\n)/, `$1
  // Caelestia for Windows: same as Caelestia's Settings window - on top, in front when it opens
  Connections {
    target: root
    function onVisibleChanged() {
      if (root.visible)
        Qt.callLater(() => {
          const w = root.contentItem?.Window.window;
          if (!w) return;
          WinFrame.WindowFrame.setTopmost(w, Cae.WinSettings.nexusTopmost);
          WinFrame.WindowFrame.bringToFront(w);
        });
    }
  }
`);
    }
    // a "Caelestia" tab first in Noctalia's settings (Mix & match + the way back to Caelestia's settings)
    if (/Modules\/Panels\/Settings\/SettingsPanel\.qml$/.test(base))
        t = t.replace(/(\n\s*)Wallpaper(\s*\n\s*\})/, "$1Wallpaper,$1Caelestia$2");
    if (/Modules\/Panels\/Settings\/SettingsContent\.qml$/.test(base)) {
        t = addImport(t, "import qs.noctalia.Modules.Panels.Settings.Tabs.Caelestia");
        t = t.replace(/(\n  Component \{\n    id: aboutTab\n)/, `\n  Component {\n    id: caelestiaTab\n    CaelestiaTab {}\n  }$1`);
        t = t.replace(/(let newTabs = \[\n)/, `$1          {
            "id": SettingsPanel.Tab.Caelestia,
            "label": "panels.caelestia.title",
            "icon": "layers-intersect",
            "source": caelestiaTab
          },
`);
    }
    // no PAM on Windows (LockContext is replaced in noctalia-overrides)
    t = t.replace(/^import Quickshell\.Services\.Pam\s*\n/m, "");
    // Windows: always the Hyprland backend - Caelestia's Quickshell.Hyprland emulation answers it
    if (/Services\/Compositor\/CompositorService\.qml$/.test(base))
        t = t.replace('const hyprlandSignature = Quickshell.env("HYPRLAND_INSTANCE_SIGNATURE");', 'const hyprlandSignature = "windows";');
    if (/Services\/Noctalia\/UpdateService\.qml$/.test(base))
        t = t.replace(/(function shouldShowTelemetryWizard\(\)\s*\{)/, "$1\n    return false; // Caelestia for Windows: no telemetry\n");
    // clipboard history: Windows keeps it (cliphist list/decode/delete answer from it, services/LinuxCompat.qml);
    // the wl-paste watchers would end at once and be restarted every second
    if (/Services\/Keyboard\/ClipboardService\.qml$/.test(base)) {
        t = t.replace("  function startWatchers() {\n", "  function startWatchers() {\n    return; // Windows: Windows keeps the clipboard history itself\n");
        if (!t.includes("return; // Windows: Windows keeps the clipboard history"))
            throw new Error("noctalia-port: ClipboardService.qml changed upstream - update the watcher transform");
    }
    return t;
}

function host(shellText) {
    // shell.qml -> NoctaliaShell.qml
    let t = transform("shell.qml", shellText);
    t = t.replace(/ShellRoot\s*\{\s*\n\s*id: shellRoot/, "Scope {\n  id: shellRoot");
    t = t.replace("import qs.noctalia.Commons\n", "import qs.noctalia.Commons\nimport qs.services as Cae\n");
    // each part only when Mix & match picked Noctalia for it
    const gate = (comp, cond) => {
        t = t.replace(new RegExp(`\\n(\\s+)${comp} \\{\\}`), `\n$1LazyLoader {\n$1  active: ${cond}\n$1  ${comp} {}\n$1}`);
    };
    gate("Overview", 'Cae.WinSettings.mixWallpaper === "noctalia"');
    gate("Background", 'Cae.WinSettings.mixWallpaper === "noctalia"');
    gate("DesktopWidgets", 'Cae.WinSettings.mixDesktopWidgets === "noctalia"');
    gate("AllScreens", 'Cae.WinSettings.noctaliaPanels');
    gate("Dock", 'Cae.WinSettings.mixDock === "noctalia"');
    gate("Notification", 'Cae.WinSettings.mixNotifs === "noctalia"');
    gate("ToastOverlay", 'Cae.WinSettings.noctaliaPanels');
    gate("OSD", 'Cae.WinSettings.mixOsd === "noctalia"');
    gate("LockScreen", 'Cae.WinSettings.mixLock === "noctalia"');
    gate("SettingsPanelWindow", 'Cae.WinSettings.settingsStyle === "noctalia"');
    // Caelestia has its own first-start window; Noctalia's setup wizard / changelog / telemetry prompt stay off
    t = t.replace(/(\n\s*)showWizardOrChangelog\(\);\n(\s*\}\n\s*\}\n\n\s*\/\/ Retry timer)/, "$1// Windows: no setup wizard (Caelestia shows its own first-start window)\n$2");
    // Wayland-only env pragmas are meaningless here
    t = t.replace(/\/\/@ pragma Env QT_FFMPEG[^\n]*\n/g, "");
    return "// Noctalia (https://github.com/noctalia-dev/noctalia, MIT) running inside Caelestia for Windows.\n" +
        "// Generated by dotfiles/noctalia-port.js from Noctalia's shell.qml - edit the script, not this file.\n" + t;
}

function copy(dir, rel, outRoot) {
    for (const name of fs.readdirSync(path.join(dir, rel))) {
        if (SKIP.has(name)) continue;
        const r = path.join(rel, name);
        const src = path.join(dir, r);
        const dst = path.join(outRoot, r);
        if (fs.statSync(src).isDirectory()) {
            if (r.replace(/\\/g, "/") === "Scripts/dev") continue;
            fs.mkdirSync(dst, { recursive: true });
            copy(dir, r, outRoot);
        } else if (r === "shell.qml") {
            fs.writeFileSync(path.join(outRoot, "NoctaliaShell.qml"), host(fs.readFileSync(src, "utf8")));
        } else if (r.replace(/\\/g, "/") === "Assets/Translations/en.json") {
            // the Caelestia tab's title (every language falls back to English)
            const j = JSON.parse(fs.readFileSync(src, "utf8"));
            j.panels = j.panels || {};
            j.panels.caelestia = { title: "Caelestia" };
            fs.writeFileSync(dst, JSON.stringify(j, null, 2));
        } else if (/\.(qml|js)$/.test(name)) {
            fs.writeFileSync(dst, transform(r, fs.readFileSync(src, "utf8")));
        } else {
            fs.copyFileSync(src, dst);
        }
    }
}

for (const out of OUTS) {
    // empty it (not remove: the folder itself may be open somewhere)
    fs.mkdirSync(out, { recursive: true });
    for (const name of fs.readdirSync(out))
        fs.rmSync(path.join(out, name), { recursive: true, force: true });
    copy(SRC, "", out);
    // Windows versions of files that cannot work as they are (noctalia-overrides/, same layout)
    const OVR = path.join(HERE, "noctalia-overrides");
    const over = rel => {
        for (const name of fs.readdirSync(path.join(OVR, rel))) {
            const r = path.join(rel, name);
            if (fs.statSync(path.join(OVR, r)).isDirectory()) {
                fs.mkdirSync(path.join(out, r), { recursive: true });
                over(r);
            } else if (!name.endsWith(".part")) {   // .part files are spliced in by transform()
                fs.copyFileSync(path.join(OVR, r), path.join(out, r));
            }
        }
    };
    if (fs.existsSync(OVR)) over("");
    console.log("ported to", out);
}
