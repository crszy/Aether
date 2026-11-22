// Puts DankMaterialShell / Dux Shell 2.1.1 (ref/dms/dux-shell-2.1.1, MIT) inside Caelestia for Windows, like
// zenith-port.js does for Zenith. Its modules (qs.Common, qs.Services, ...) would clash with Caelestia's
// (qs.services, qs.modules - Windows paths ignore case), so they become qs.dms.*; its shared library
// (dux-qml-common/DCommon, a symlink in the original tree) is copied into dms/DCommon. Its IPC targets get a
// "dms:" prefix (lock, audio, wallpaper, ... are Caelestia's too).
//   node dms-port.js   (from dotfiles/)
const fs = require("fs");
const path = require("path");
const L = require("./shellport/lib.js");

const HERE = __dirname;
const REPO = path.join(HERE, "..", "ref", "dms", "dux-shell-2.1.1");
const SRC = path.join(REPO, "quickshell");
const DCOMMON = path.join(REPO, "dux-qml-common", "DCommon");
const OUTS = [path.join(HERE, "caelestia-win", "dms"), path.join(HERE, "caelestia-shell-win", "dms")];
const OVR = path.join(HERE, "dms-overrides");
const ROOT = '(Quickshell.shellDir + "/dms")';
const MODS = "Common|Modules|Services|DCommon|Widgets|Modals";

function transform(rel, t) {
    t = L.common(t, { rootExpr: ROOT });
    // the "dms" helper binary (Go, Linux) is not on Windows: those calls end at once with "not found" (127)
    // instead of a "failed to start" warning each time
    // (copy to clipboard: Windows' own)
    t = t.replace(/\["dms", "cl", "copy", (\w+)\]/g, '["powershell.exe", "-NoProfile", "-Command", "Set-Clipboard -Value $args[0]", $1]');
    t = t.replace(/(^|[^:\w])\[\s*"dms",/gm, (m, pre) => /tags:\s*$/.test(pre) ? m : pre + '["cmd.exe", "/c", "exit", "127", "dms",');
    t = t.replace(/(tags: )\["cmd\.exe", "\/c", "exit", "127", /g, "$1[");
    // (".import" in .js files too)
    t = t.replace(new RegExp(`^(\\.?)import qs\\.(${MODS})\\b`, "gm"), "$1import qs.dms.$2");
    // IpcHandler targets
    t = t.replace(/(\btarget: )"([a-z][a-z0-9-]*)"/g, '$1"dms:$2"');
    // the i3/Sway module is not part of Quickshell for Windows: stand-ins in dms-overrides/Shims
    t = t.replace(/^import Quickshell\.I3\s*$/gm, "import qs.dms.Shims // Windows: I3 stand-ins");
    // Caelestia (and Windows) own idle lock and sleep: DMS never locks or suspends on idle by itself
    if (rel === "Services/IdleService.qml") {
        t = t.replace("lockMonitor.enabled = base && lockTimeout > 0;", "lockMonitor.enabled = false; // Windows: Caelestia handles idle");
        t = t.replace("suspendMonitor.enabled = base && suspendTimeout > 0;", "suspendMonitor.enabled = false; // Windows");
    }
    // Mix & match bar pieces (MIXING-PLAN.md): WinSettings.barPieces join the end of DMS's right section as
    // "aethermix:<piece>" widgets, so DMS's own overflow/segment logic handles them like its own widgets;
    // the widget host turns them into Caelestia's modules/bar/pieces/MixPiece.qml
    if (rel.split("\\").join("/") === "Modules/DBar/DBar.qml") {
        t = L.addImport(t, "import qs.services as Cae");
        t = t.replace("JSON.stringify(WidgetModel.normalize(root.barConfig?.rightWidgets))",
            "JSON.stringify(WidgetModel.normalize((root.barConfig?.rightWidgets ?? []).concat((Cae.WinSettings.barPieces ?? []).map(p => \"aethermix:\" + p)))) // Windows: Mix & match");
        if (!t.includes("// Windows: Mix & match"))
            throw new Error("dms-port: Modules/DBar/DBar.qml changed upstream - update the Mix & match bar transform");
    }
    if (rel.split("\\").join("/") === "Modules/SurfaceWidgets/SurfaceWidgetHost.qml") {
        t = t.replace("        return components?.[widgetId] || components?.[widgetId.split(\":\")[0]] || null;",
            "        if (widgetId.startsWith(\"aethermix:\")) // Windows: Mix & match - another shell's widget\n" +
            "            return aetherMixComponent;\n" +
            "        return components?.[widgetId] || components?.[widgetId.split(\":\")[0]] || null;");
        t = t.replace(/\n    function getWidgetComponent\(/,
            "\n    Component { // Windows: Mix & match - a bar piece from another shell (WinSettings.barPieces)\n" +
            "        id: aetherMixComponent\n\n        Loader {\n" +
            "            source: Qt.resolvedUrl(\"../../../modules/bar/pieces/MixPiece.qml\")\n" +
            "            onLoaded: {\n                item.vertical = root.isInColumn;\n" +
            "                item.screen = Qt.binding(() => root.parentScreen);\n" +
            "                item.pieceId = root.widgetId.slice(10);\n            }\n        }\n    }\n\n" +
            "    function getWidgetComponent(");
        if (!t.includes("return aetherMixComponent;") || !t.includes("id: aetherMixComponent"))
            throw new Error("dms-port: SurfaceWidgetHost.qml changed upstream - update the Mix & match bar transform");
    }
    // Mix & match inside the lock (WinSettings.lockWallpaper "auto"): when another shell draws the wallpaper,
    // DMS's lock shows that wallpaper (Caelestia's current one) instead of DMS's own. A lock wallpaper set in
    // DMS's own settings still wins.
    if (rel.split("\\").join("/") === "Modules/Lock/LockScreenBackground.qml") {
        t = L.addImport(t, "import qs.services as Cae");
        t = t.replace('readonly property bool hasCustomWallpaper: SettingsData.lockScreenWallpaperPath !== ""',
            'readonly property string aetherWallpaper: ((Cae.WinSettings.lockWallpaper === "caelestia" || (Cae.WinSettings.lockWallpaper === "auto" && Cae.WinSettings.mixWallpaper !== "dms")) && Cae.Wallpapers.current) ? String(Cae.Wallpapers.current).split("\\\\").join("/") : "" // Windows: Mix & match\n' +
            '    readonly property bool hasCustomWallpaper: SettingsData.lockScreenWallpaperPath !== "" || aetherWallpaper !== ""');
        t = t.replace("return root.encodeFileUrl(SettingsData.lockScreenWallpaperPath);",
            'return SettingsData.lockScreenWallpaperPath !== "" ? root.encodeFileUrl(SettingsData.lockScreenWallpaperPath) : "file:///" + root.aetherWallpaper;');
        if (!t.includes("aetherWallpaper !== \"\"") || !t.includes("root.aetherWallpaper;"))
            throw new Error("dms-port: LockScreenBackground.qml changed upstream - update the Mix & match lock transform");
    }
    // no polkit on Windows (UAC asks itself)
    if (rel === "Services/PolkitService.qml")
        t = t.replace("readonly property bool polkitAvailable: !disablePolkitIntegration", "readonly property bool polkitAvailable: false // Windows");
    // no compositor blur on Windows (the attached BackgroundEffect does not exist)
    t = t.replace(/^(\s*)((?:targetWindow|win)\.BackgroundEffect\.blurRegion = [^;\n]*;?)$/gm, "$1void 0; // Windows: no compositor blur");
    t = t.replace("if (!targetWindow?.BackgroundEffect)", "if (true) // Windows: no compositor blur");
    if (/^\s*ShortcutInhibitor \{/m.test(t)) t = L.addImport(t, "import qs.dms.Shims");
    // Linux tools and DMS's Go backend ("dms") do not exist on Windows (a Git Bash PATH would still find sh/cat
    // and run DMS's Linux scripts): such commands end at once as "not found", which DMS already handles
    // DMS's settings: a way back to Aether's settings (switch shells, every part) at the bottom of its sidebar
    if (rel.split("\\").join("/").endsWith("Modals/Settings/SettingsSidebar.qml")) {
        t = L.addImport(t, "import qs.modules.nexus as AetherNexus");
        t = t.replace("        anchors.top: searchField.bottom\n        anchors.bottom: parent.bottom\n        anchors.topMargin: SettingsMetrics.searchBarGap",
            "        anchors.top: searchField.bottom\n        anchors.bottom: aetherButton.top // Windows: room for the Aether button\n        anchors.bottomMargin: Theme.spacingS\n        anchors.topMargin: SettingsMetrics.searchBarGap");
        t = t.replace(/\n    DFlickable \{\n        id: sidebarFlickable/,
            "\n    // Windows (Aether): back to Aether's settings - pick other shells for every part\n" +
            "    DButton {\n        id: aetherButton\n\n        anchors.left: parent.left\n        anchors.right: parent.right\n        anchors.bottom: parent.bottom\n" +
            "        anchors.margins: SettingsMetrics.paneMargin\n        text: I18n.tr(\"Switch shells (Mix & match)\")\n        iconName: \"swap_horiz\"\n        variant: \"tonal\"\n" +
            "        onClicked: AetherNexus.WindowFactory.create(null, { caelestia: true, startPage: \"Mix\" })\n    }\n\n    DFlickable {\n        id: sidebarFlickable");
    }
    // file browser: an absolute path also starts with a drive ("C:/...") on Windows
    if (rel.split("\\").join("/").includes("FileBrowser/") && !rel.endsWith("FilePaths.qml"))
        t = t.replace(/([\w.?]+)\.startsWith\("\/"\)/g, "/^(\\/|[A-Za-z]:)/.test($1)");
    // DMS's windows (Settings, Files...) draw DMS's own title bar on Windows too, not Windows' one
    if (rel.split("\\").join("/").endsWith("Widgets/FloatingWindowControls.qml")) {
        t = t.replace('readonly property bool frameless: ownDecorations && Compositor.desktop === "kde"',
            "readonly property bool frameless: ownDecorations // Windows: DMS draws its own title bar");
        // the frameless flag does not take on Windows once the window exists: hide the caption natively
        // (Quickshell.Windows WindowFrame, as Caelestia's Settings does - resize edges stay)
        t = L.addImport(t, "import Quickshell.Windows as WinFrame");
        t = t.replace(/(function applyDecorationHint\(\) \{\n\s*if \(!backingWindow\)\n\s*return;)/,
            "$1\n        if (frameless) WinFrame.WindowFrame.hideTitleBar(backingWindow); else WinFrame.WindowFrame.showTitleBar(backingWindow); // Windows\n        return;");
        t = t.replace("onBackingWindowChanged: applyDecorationHint()",
            "onBackingWindowChanged: applyDecorationHint()\n    Component.onCompleted: Qt.callLater(applyDecorationHint) // Windows\n    Connections {\n        target: root.backingWindow\n        ignoreUnknownSignals: true\n        function onVisibleChanged() {\n            if (root.backingWindow?.visible)\n                Qt.callLater(root.applyDecorationHint);\n        }\n    }");
        t = t.replace("readonly property bool canMinimize: targetWindow.minimized !== undefined && Compositor.supportsMinimize",
            "readonly property bool canMinimize: targetWindow.minimized !== undefined // Windows: minimizing works");
    }
    // one wallpaper for every shell: a wallpaper DMS sets is set in Caelestia too (Caelestia's picker, transitions
    // and colours follow); DmsHost makes DMS follow Caelestia's (see host())
    if (rel.split("\\").join("/").endsWith("Common/SessionData.qml")) {
        t = L.addImport(t, "import qs.services as CaeWall");
        const push = "        if (typeof imagePath === \"string\" && imagePath && !imagePath.startsWith(\"#\") && String(CaeWall.Wallpapers.actualCurrent).replace(/\\\\/g, \"/\") !== imagePath.replace(/\\\\/g, \"/\"))\n" +
                     "            CaeWall.Wallpapers.setWallpaper(imagePath); // Windows: shared wallpaper\n";
        t = t.replace("    function setWallpaper(imagePath) {\n", "    function setWallpaper(imagePath) {\n" + push);
        t = t.replace("    function setMonitorWallpaper(screenName, path) {\n", "    function setMonitorWallpaper(screenName, path) {\n" + push.split("imagePath").join("path"));
    }
    // DMS's backend on Windows is WinDms (dms-overrides/Services/WinDms.qml): "connected", with only the
    // capabilities it really answers, and every request goes to it (the Linux "dms" socket is never opened)
    if (rel.split("\\").join("/").endsWith("Services/DMSService.qml")) {
        t = t.replace("property bool isConnected: false", "property bool isConnected: true // Windows: WinDms answers");
        t = t.replace("property var capabilities: []", "property var capabilities: WinDms.capabilities // Windows");
        // the API level of what WinDms answers (clipboard settings + settings search need 23); without it DMS
        // tells you to "update the dms package with your package manager", which does not exist on Windows
        t = t.replace("property int apiVersion: 0", "property int apiVersion: 23 // Windows: WinDms");
        t = t.replace("property string cliVersion: \"\"", "property string cliVersion: \"Windows (Aether)\"");
        t = t.replace("function sendRequest(method, params, callback, timeoutMs) {",
            "function sendRequest(method, params, callback, timeoutMs) {\n        return WinDms.handle(method, params, callback, timeoutMs); // Windows");
        t = t.replace("function cancelRequest(id) {", "function cancelRequest(id) {\n        WinDms.cancel(id); // Windows\n        return;");
    }
    // desktop widgets: their stock position is asked for before the edit layer is ready (27 errors per start)
    if (rel.split("\\").join("/").endsWith("Modules/Plugins/WidgetEditItem.qml"))
        t = t.replace("readonly property var stock: hostLayer.stockRect(widgetType, root)",
            'readonly property var stock: (typeof hostLayer?.stockRect === "function") ? hostLayer.stockRect(widgetType, root) : null // Windows');
    const isProc =rel.split("\\").join("/").endsWith("Common/Proc.qml") && t.includes("property int defaultDebounceMs");
    if (isProc)
        t = t.replace(/(function runCommand\(id, command, callback, debounceMs, timeoutMs, owner\) \{)/, "$1\n" +
            "        if (root.windowsSkipped.includes(String(command?.[0] ?? \"\").replace(/^.*[\\\\/]/, \"\"))) { // Windows\n" +
            "            if (callback) Qt.callLater(() => callback(\"\", 127));\n" +
            "            return;\n        }");
    if (isProc)
        t = t.replace("property int defaultDebounceMs: 50", "readonly property var windowsSkipped: [dmsBin, \"sh\", \"bash\", \"cat\", \"cp\", \"mv\", \"rm\", \"test\", \"pgrep\", \"systemctl\", \"loginctl\", \"busctl\", \"gdbus\", \"nmcli\", \"awk\", \"find\", \"which\", \"gsettings\", \"pkexec\", \"sudo\"]\n    property int defaultDebounceMs: 50");
    // Caelestia emulates Hyprland on Windows (Quickshell.Hyprland, hyprctl): DMS uses its Hyprland code
    if (rel === "Services/CompositorService.qml")
        t = t.replace("function detectCompositor() {", "function detectCompositor() {\n" +
            "        _applyCompositor(\"hyprland\"); // Windows: Caelestia's Hyprland emulation\n        return;");
    // system monitor: dgop requests are answered from Windows' counters (overrides/Services/WinDgop.qml)
    if (rel === "Services/DgopService.qml") {
        t = t.replace(/readonly property bool dgopAvailable: [^\n]*/, "readonly property bool dgopAvailable: true // Windows: WinDgop");
        t = t.replace(/\bDMSService\.sendRequest\(/g, "WinDgop.handle(");
    }
    // weather: Open-Meteo through curl.exe (part of Windows) instead of DMS's backend; place names the same way
    if (rel === "Services/WeatherService.qml") {
        t = t.replace(/readonly property var lowPriorityCmd: [^\n]*/, "readonly property var lowPriorityCmd: [] // Windows");
        t = t.replace(/readonly property var fetchCmd: [^\n]*/, 'readonly property var fetchCmd: ["curl.exe", "-s", "-L", "--connect-timeout", "8", "-m", "20"] // Windows');
        t = t.replace(/readonly property bool _backendReady: [^\n]*/, "readonly property bool _backendReady: true // Windows: curl.exe");
        t = t.replace(/root\._weatherRequestId = DMSService\.sendRequest\("weather\.get", \{[\s\S]*?root\._requestTimeout\) \?\? null;/,
            "root._weatherRequestId = root.winFetchWeather(lat, lon, token); // Windows");
        t = t.replace("function cancelFetch() {", "function cancelFetch() {\n        winWeatherFetcher.running = false; // Windows");
        t = t.replace(/(\n    Process \{\n        id: nominatimFetcher)/, `
    // Windows: the forecast straight from Open-Meteo (what DMS's backend fetches), in its own format
    function winFetchWeather(lat, lon, token) {
        const cur = "temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,surface_pressure,wind_speed_10m,wind_direction_10m";
        const hourly = "temperature_2m,relative_humidity_2m,apparent_temperature,precipitation_probability,weather_code,surface_pressure,visibility,wind_speed_10m,is_day";
        const daily = "weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_probability_max";
        const url = "https://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon + "&current=" + cur + "&hourly=" + hourly + "&daily=" + daily + "&timezone=auto&forecast_days=7";
        winWeatherFetcher.token = token;
        winWeatherFetcher.command = fetchCmd.concat([url]);
        winWeatherFetcher.running = false;
        winWeatherFetcher.running = true;
        return -token;
    }

    Process {
        id: winWeatherFetcher
        property int token: 0
        running: false
        stdout: StdioCollector {
            onStreamFinished: {
                let forecast = null;
                try {
                    forecast = JSON.parse(text);
                } catch (e) {}
                root.receiveWeather(winWeatherFetcher.token, forecast?.current ? { result: { forecast: forecast, source: root.primaryProvider } } : { error: forecast?.reason || "no answer from Open-Meteo" });
            }
        }
    }
$1`);
    }
    // network: the Windows backend (overrides/Services/WinNetworkBackend.qml, no periodic Wi-Fi scans)
    if (rel === "Services/NetworkService.qml")
        t = t.replace(/(Component\.onCompleted: \{\n)/, "$1        activeService = WinNetworkBackend; // Windows\n        connectSignals();\n        return;\n");
    // pop-ups, OSDs, toasts and modals wait for their window's first frame (frameSwapped) before they show;
    // with Windows' proxied panel windows that signal does not always arrive, so each such wait also ends by
    // itself a moment later
    t = t.replace(/\n([ \t]*)Connections \{\n\s*target: [\w.]+\.Window\.window\n\s*enabled: ([^\n]+)\n(?:\s*\n)?\s*function onFrameSwapped\(\) \{\n\s*([^\n;]*(?:_surfaceFrameReady|surfaceReady|present\(\)|mapped) ?[^\n;]*;)\n\s*\}\n\s*\}/g,
        (m, ind, cond, stmt) => `${m}\n${ind}Timer { // Windows: the first frame's signal may not come\n${ind}    running: ${cond}\n${ind}    interval: 80\n${ind}    onTriggered: { ${stmt} }\n${ind}}`);
    // the dock only loads when Mix & match picked DMS's dock, so it starts switched on (DMS can still turn it off)
    if (rel === "Common/settings/DockConfig.js")
        t = t.replace(/^(\s*)enabled: false,/m, "$1enabled: true, // Windows: Mix & match decides");
    // launching apps: a Windows entry's command is a Start-menu shortcut or a Store app id, which only the
    // Windows shell can start (execDetached cannot) - go through the entry's own execute(), as Caelestia does
    if (rel === "Services/SessionService.qml") {
        t = t.replace("    function launchDesktopEntry(desktopEntry, useNvidia, args) {\n        if (!desktopEntry || !desktopEntry.command)\n            return;\n",
            "    function launchDesktopEntry(desktopEntry, useNvidia, args) {\n        if (!desktopEntry || !desktopEntry.command)\n            return;\n" +
            "        if (typeof desktopEntry.execute === \"function\") { // Windows\n" +
            "            CompositorService.closeNiriOverviewOnWindowFocus();\n" +
            "            if (args?.length) // open files with it: let Windows pass them to the shortcut/app\n" +
            "                Quickshell.execDetached({ command: [\"cmd.exe\", \"/c\", \"start\", \"\", desktopEntry.command[0]].concat(args) });\n" +
            "            else\n                desktopEntry.execute();\n            return;\n        }\n");
        t = t.replace("    function launchDesktopAction(desktopEntry, action, useNvidia) {\n        if (!desktopEntry || !action || !action.command)\n            return;\n",
            "    function launchDesktopAction(desktopEntry, action, useNvidia) {\n        if (!desktopEntry || !action || !action.command)\n            return;\n" +
            "        if (typeof action.execute === \"function\") { // Windows\n" +
            "            CompositorService.closeNiriOverviewOnWindowFocus();\n            action.execute();\n            return;\n        }\n");
        if ((t.match(/\/\/ Windows\n/g) || []).length < 2 || !t.includes("desktopEntry.execute();"))
            throw new Error("dms-port: SessionService.qml changed upstream - update the app launch transform");
    }
    // screen time: Aether's own windows (Settings, launcher...) are qsw/qs on Windows - not an app you used
    if (rel === "Services/WellbeingService.qml")
        t = t.replace('readonly property var ownAppIds: ["dev.duxlabs.shell", "com.danklinux.dms", "org.quickshell", "quickshell"',
            'readonly property var ownAppIds: ["qsw", "qs", "caelestia", "dev.duxlabs.shell", "com.danklinux.dms", "org.quickshell", "quickshell"');
    // who is signed in: Windows' environment instead of getent/id
    if (rel === "Services/UserInfoService.qml")
        t = t.replace("function getUserInfo() {", "function getUserInfo() {\n" +
            "        root.username = Quickshell.env(\"USERNAME\") || \"User\"; // Windows\n" +
            "        root.fullName = root.username;\n" +
            "        root.hostname = Quickshell.env(\"COMPUTERNAME\") || \"\";\n        return;");
    // the dock's apps follow the workspace like the bar's (Aether: only what is on screen; the WM's canvas included)
    if (rel === "Modules/DBar/Widgets/AppsDock.qml") {
        const before = t;
        t = t.replace('currentWorkspace: appOption("runningAppsCurrentWorkspace", false)', 'currentWorkspace: appOption("runningAppsCurrentWorkspace", true)');
        if (t === before)
            throw new Error("dms-port: AppsDock.qml changed upstream - update the current-workspace default");
    }
    // Aether: the dock / bar apps pop in when they appear (switching workspace or to the canvas swaps the list).
    // A one-off animation per new icon: nothing runs while the list stays the same.
    if (rel === "Modules/SurfaceWidgets/ApplicationStrip.qml") {
        const before = t;
        t = t.replace("                delegate: ApplicationItem {\n                    strip: root\n                }",
            "                delegate: ApplicationItem {\n                    strip: root\n" +
            "                    // Aether: pop in\n" +
            "                    id: popItem\n" +
            "                    transform: Scale {\n                        id: popScale\n" +
            "                        origin.x: popItem.width / 2\n                        origin.y: popItem.height / 2\n                        xScale: 0.4\n                        yScale: 0.4\n                    }\n" +
            "                    Component.onCompleted: popIn.start()\n" +
            "                    NumberAnimation {\n                        id: popIn\n                        target: popScale\n" +
            "                        properties: \"xScale,yScale\"\n                        to: 1\n                        duration: 220\n" +
            "                        easing.type: Easing.OutBack\n                    }\n" +
            "                    // icons kept across a workspace switch (same app on both) pop too\n" +
            "                    Connections {\n                        target: root\n" +
            "                        function onAetherShownWsChanged() {\n" +
            "                            popScale.xScale = 0.4;\n                            popScale.yScale = 0.4;\n                            popIn.restart();\n" +
            "                        }\n                    }\n                }");
        t = t.replace("import QtQuick\nimport Quickshell\n", "import QtQuick\nimport Quickshell\nimport Quickshell.Hyprland\n");
        t = t.replace("    readonly property var visibleWindows:",
            "    // Aether: the workspace this strip's screen shows (a switch pops the icons)\n" +
            "    readonly property int aetherShownWs: (Hyprland.monitors?.values ?? []).find(m => m.name === dockScreen?.name)?.activeWorkspace?.id ?? -1\n" +
            "    readonly property var visibleWindows:");
        if (t === before || !t.includes("aetherShownWs: ") || !t.includes("import Quickshell.Hyprland"))
            throw new Error("dms-port: ApplicationStrip.qml changed upstream - update the pop-in");
    }
    // Aether: DMS's settings window gets a way back to Aether's own settings (Mix & match, where the settings style
    // is picked) - with the style set to DMS, the Settings key opens this window instead
    if (rel === "Modals/Settings/SettingsModal.qml") {
        const before = t;
        t = t.replace("import qs.dms.Common\n", "import qs.dms.Common\nimport qs.modules.nexus as CaeNexus\n");
        t = t.replace("                title: I18n.tr(\"Settings\")\n                onCloseRequested: settingsModal.hide()\n",
            "                title: I18n.tr(\"Settings\")\n                onCloseRequested: settingsModal.hide()\n\n" +
            "                DActionButton { // Aether: back to Aether's settings\n" +
            "                    iconName: \"tune\"\n" +
            "                    tooltipText: \"Aether settings (Mix & match, settings style)\"\n" +
            "                    onClicked: {\n" +
            "                        settingsModal.hide();\n" +
            "                        CaeNexus.WindowFactory.create(null, { caelestia: true, startPage: \"Mix\" });\n" +
            "                    }\n" +
            "                }\n");
        if (t === before || !t.includes("CaeNexus.WindowFactory.create"))
            throw new Error("dms-port: SettingsModal.qml header changed upstream - update the Aether settings button");
    }
    // Aether: popups (volume / mic / brightness OSD, notifications, toasts) on the main monitor only, unless
    // Settings > Mix & match "Popups on all monitors" (WinSettings.osdAllMonitors). DMS's "follow the focused
    // monitor" stays at its default (off) - the main monitor is the one.
    if (rel === "Common/SettingsData.qml") {
        const before = t;
        t = t.replace("import Quickshell.Io\n", "import Quickshell.Io\nimport Quickshell.Hyprland\nimport qs.services as Cae\n");
        t = t.replace("    function getFilteredScreens(componentId) {\n",
            "    function getFilteredScreens(componentId) {\n" +
            "        if ([\"osd\", \"notifications\", \"toast\"].includes(componentId) && !Cae.WinSettings.osdAllMonitors) { // Aether: the main monitor only\n" +
            "            const one = ShellLayout.realScreens.filter(s => Cae.WinSettings.isMainScreen(s));\n" +
            "            return one.length ? one : ShellLayout.realScreens.slice(0, 1);\n        }\n");
        if (t === before || !t.includes("Cae.WinSettings.osdAllMonitors"))
            throw new Error("dms-port: SettingsData.qml changed upstream - update the OSD screen rule");
    }
    // Aether: rapid wallpaper changes - a new one while a transition runs hurries that transition to its end
    // (~180 ms, eased) and then plays the next, instead of waiting for the whole second (felt frozen)
    if (rel === "Modules/WallpaperBackground.qml") {
        const before = t;
        t = t.replace("readonly property bool transitioning: transitionAnimation.running",
            "readonly property bool transitioning: transitionAnimation.running || hurryAnimation.running");
        t = t.replace("                if (root.transitioning || root.effectActive) {\n                    root.pendingWallpaper = newPath;\n                    return;\n                }",
            "                if (root.transitioning || root.effectActive) {\n                    root.pendingWallpaper = newPath;\n" +
            "                    if (transitionAnimation.running) { // Aether: hurry the running one\n" +
            "                        transitionAnimation.stop();\n                        hurryAnimation.start();\n                    }\n" +
            "                    return;\n                }");
        t = t.replace("                id: transitionAnimation\n",
            "                id: transitionAnimation\n");
        t = t.replace(/(\n            NumberAnimation \{\n                id: transitionAnimation\n[\s\S]*?\n            \}\n)/,
            "$1\n            // Aether: the rest of a transition, fast, when another wallpaper is already waiting\n" +
            "            NumberAnimation {\n                id: hurryAnimation\n                target: root\n                property: \"transitionProgress\"\n" +
            "                to: 1.0\n                duration: 180\n                easing.type: Easing.OutCubic\n" +
            "                onFinished: {\n                    if (!nextWallpaper.source || nextWallpaper.status !== Image.Ready) {\n" +
            "                        root.finishTransition();\n                        return;\n                    }\n" +
            "                    currentWallpaper.source = nextWallpaper.source;\n" +
            "                    if (root._freezeWaitFrames === 0 && currentWallpaper.status !== Image.Loading)\n" +
            "                        root.finishTransition();\n                }\n            }\n");
        // Aether: a change starts at once (ease-out, 700 ms) and changes in quick succession play fast (380 ms) -
        // pressing the wallpaper key repeatedly flows through the wallpapers instead of waiting a second each
        t = t.replace("            function changeWallpaper(newPath) {\n",
            "            property real aetherLastChange: 0\n            property bool aetherRapid: false\n" +
            "            function changeWallpaper(newPath) {\n" +
            "                const aetherNow = Date.now(); // Aether: rapid changes go fast\n" +
            "                root.aetherRapid = aetherNow - root.aetherLastChange < 1500;\n                root.aetherLastChange = aetherNow;\n");
        t = t.replace("                duration: root.actualTransitionType === \"none\" ? 0 : 1000\n                easing.type: Easing.InOutCubic",
            "                duration: root.actualTransitionType === \"none\" ? 0 : (root.aetherRapid ? 380 : 700) // Aether\n                easing.type: Easing.OutCubic");
        if (!t.includes("root.aetherRapid ? 380 : 700"))
            throw new Error("dms-port: WallpaperBackground.qml changed upstream - update the rapid transition");
        // Aether: pressing the wallpaper key again while a transition runs speeds that transition to its end
        // (hurryAnimation, ~180 ms) and then plays the next one. The next picture starts loading the moment the key
        // is pressed (preloadWallpaper), so it is decoded by the time the hurried transition ends - the old freeze
        // was the next transition waiting for its picture to load after the first one finished.
        const tPreload = t;
        t = t.replace("                    if (transitionAnimation.running) { // Aether: hurry the running one\n",
            "                    preloadWallpaper.source = newPath; // Aether: decode it now, so the next transition starts at once\n" +
            "                    if (transitionAnimation.running) { // Aether: hurry the running one\n");
        t = t.replace("            Loader {\n                id: videoLoader\n",
            "            // Aether: the next wallpaper decodes here while a transition still runs (never shown itself);\n" +
            "            // nextWallpaper then gets the same picture from Qt's image cache without waiting\n" +
            "            Image {\n                id: preloadWallpaper\n                visible: false\n                asynchronous: true\n" +
            "                cache: true\n                smooth: true\n" +
            "                sourceSize: Qt.size(root.textureWidth, root.textureHeight)\n" +
            "                fillMode: Theme.getFillMode(SessionData.getMonitorWallpaperFillMode(modelData.name))\n" +
            "            }\n\n" +
            "            Loader {\n                id: videoLoader\n");
        if (t === tPreload || !t.includes("preloadWallpaper.source = newPath;") || !t.includes("id: preloadWallpaper"))
            throw new Error("dms-port: WallpaperBackground.qml changed upstream - update the wallpaper preload");
        // Aether game mode: the video wallpaper is unloaded (its still frame stays) - frees its VRAM and decoder
        t = L.addImport(t, "import Caelestia.State as CaeGame");
        t = t.replace("                active: root.isVideoSource || (item?.opacity ?? 0) > 0",
            "                active: !CaeGame.RuntimeState.gameMode && (root.isVideoSource || (item?.opacity ?? 0) > 0) // Aether: off in game mode");
        if (t === before || !t.includes("id: hurryAnimation") || !t.includes("hurryAnimation.start()") || !t.includes("CaeGame.RuntimeState.gameMode"))
            throw new Error("dms-port: WallpaperBackground.qml changed upstream - update the wallpaper hurry");
    }
    // Windows: DMS's file search needs dsearch (Linux only) - the launcher never reopens in Files mode, and Tab
    // cycles all / apps / plugins only (a remembered "files" made the Windows key open an empty file search)
    if (rel === "Common/SessionData.qml") {
        const before = t;
        t = t.replace("        return launcherLastMode || \"all\";", "        return (launcherLastMode === \"files\" ? \"all\" : launcherLastMode) || \"all\"; // Windows: no dsearch");
        if (t === before)
            throw new Error("dms-port: SessionData.qml changed upstream - update the launcher restore mode");
    }
    if (rel === "Modals/DLauncherV2/Controller.qml") {
        const before = t;
        t = t.replace("        var modes = [\"all\", \"apps\", \"files\", \"plugins\"];", "        var modes = [\"all\", \"apps\", \"plugins\"]; // Windows: no dsearch for files");
        // Aether: Enter on ">border" / ">wallpaper" (nothing after them) opens Aether's picker - and only that. It used
        // to open while typing, and the Enter that followed still ran the list's first item ("Random wallpaper"):
        // two animations. Typing only shows the list now; with something after the trigger Enter picks that item.
        t = t.replace("    function executeSelected(newInstance = false) {\n        if (_searchPending) {\n            _searchPending = false;\n            searchDebounce.stop();\n            performSearch();\n        }\n",
            "    function executeSelected(newInstance = false) {\n        if (_searchPending) {\n            _searchPending = false;\n            searchDebounce.stop();\n            performSearch();\n        }\n" +
            "        if (searchMode !== \"files\" && searchMode !== \"dmenu\") { // Aether: the picker, on Enter only\n" +
            "            const aetherTrigger = detectTrigger(String(searchQuery || \"\").trim());\n" +
            "            if ((aetherTrigger.pluginId === \"dms_border\" || aetherTrigger.pluginId === \"dms_wallpaper\") && !String(aetherTrigger.query || \"\").trim()) {\n" +
            "                WinDms.openPicker(aetherTrigger.pluginId === \"dms_border\" ? \"border\" : \"wallpaper\");\n" +
            "                return;\n" +
            "            }\n" +
            "        }\n");
        if (!t.includes("WinDms.openPicker("))
            throw new Error("dms-port: Controller.qml changed upstream - update the picker hook");
        if (t === before)
            throw new Error("dms-port: Controller.qml changed upstream - update the launcher modes");
    }
    // Windows: the "dms" CLI that writes compositor window rules does not exist (exit 127 warnings); Aether's
    // window manager has its own rules (hyprland.conf windowrule)
    if (rel === "Services/CompositorService.qml") {
        const before = t;
        t = t.replace("    function setDmsRule(id, rule, onDone) {\n        if (!supportsWindowRules)\n            return;",
            "    function setDmsRule(id, rule, onDone) {\n        if (!supportsWindowRules || Qt.platform.os === \"windows\") // Windows: no dms CLI\n            return;");
        if (t === before)
            throw new Error("dms-port: CompositorService.qml changed upstream - update setDmsRule");
    }
    // Aether: ">border" in DMS's launcher - the window manager's border colour (WinDms does the work)
    if (rel === "Services/AppSearchService.qml") {
        const before = t;
        t = t.replace('            "dms_clipboard_search": {', '            "dms_border": {\n' +
            '                id: "dms_border",\n                name: "Border colour",\n                cornerIcon: "border_style",\n' +
            '                comment: "Aether",\n                defaultTrigger: ">border",\n                isLauncher: true,\n' +
            '                viewMode: "list",\n                viewModeEnforced: true\n            },\n' +
            '            "dms_wallpaper": {\n                id: "dms_wallpaper",\n                name: "Wallpaper",\n                cornerIcon: "wallpaper",\n' +
            '                comment: "Aether",\n                defaultTrigger: ">wallpaper",\n                isLauncher: true,\n' +
            '                viewMode: "list",\n                viewModeEnforced: true\n            },\n' +
            '            "dms_aether_settings": {\n                id: "dms_aether_settings",\n                name: "Aether settings",\n' +
            '                icon: "material:tune",\n                cornerIcon: "tune",\n                comment: "Setup, Hyprland, game mode",\n' +
            '                action: "ipc:aether-settings",\n                categories: ["Settings", "System"],\n' +
            '                defaultTrigger: "",\n                isLauncher: false\n            },\n            "dms_clipboard_search": {');
        t = t.replace('        case "qr-generator":\n            PopoutService.showQRGeneratorModal();\n            return true;',
            '        case "qr-generator":\n            PopoutService.showQRGeneratorModal();\n            return true;\n' +
            '        case "aether-settings": // Aether: Caelestia\'s settings (Mix & match, Hyprland ...)\n' +
            '            Cae.WinSettings.ipc("aether", "settings", []);\n            return true;');
        t = L.addImport(t, "import qs.services as Cae");
        t = t.replace("    function getBuiltInLauncherItems(pluginId, query, allowEmptyQuery) {\n",
            "    function getBuiltInLauncherItems(pluginId, query, allowEmptyQuery) {\n" +
            "        if (pluginId === \"dms_border\") // Aether\n            return WinDms.borderLauncherItems(query, pluginId);\n" +
            "        if (pluginId === \"dms_wallpaper\") // Aether\n            return WinDms.wallpaperLauncherItems(query, pluginId);\n");
        t = t.replace('        case "power":\n            return executePowerLauncherAction(parts.slice(1).join(":"));',
            '        case "power":\n            return executePowerLauncherAction(parts.slice(1).join(":"));\n' +
            '        case "border": // Aether\n            return WinDms.setBorderColour(parts[1]);\n' +
            '        case "wallpaper": // Aether\n            return WinDms.setWallpaperFromLauncher(parts.slice(1).join(":"));');
        if ((t.match(/Aether/g) || []).length - (before.match(/Aether/g) || []).length < 3)
            throw new Error("dms-port: AppSearchService.qml changed upstream - update the >border plugin");
    }
    // its own folder: Qt.resolvedUrl does not give a file path for qs modules on Windows
    if (rel === "Common/Theme.qml")
        t = t.replace(/readonly property string shellDir: [^\n]*/,"readonly property string shellDir: Quickshell.shellDir + \"/dms\" // Windows");
    return t;
}

function shellCore(text) {
    let t = transform("ShellCore.qml", text);
    t = L.addImport(t, "import qs.services as Cae");
    // bars, island and frame only when Mix & match picked DMS's bar
    t = t.replace("values: JSON.parse(root._barLayoutStateJson)",
        'values: Cae.WinSettings.mixBar === "dms" ? JSON.parse(root._barLayoutStateJson) : [] // Windows: Mix & match');
    t = t.replace("active: SettingsData.dankIslandEnabled", 'active: SettingsData.dankIslandEnabled && Cae.WinSettings.mixBar === "dms"');
    t = t.replace("active: root.frameSurfacesLoaded", 'active: root.frameSurfacesLoaded && Cae.WinSettings.mixBar === "dms"');
    return t;
}

function dmsShell(text) {
    let t = transform("DMSShell.qml", text);
    t = L.addImport(t, "import qs.services as Cae");
    t = L.gate(t, "DesktopWidgetLayer", 'Cae.WinSettings.mixDesktopWidgets === "dms"');
    t = L.gate(t, "DesktopWidgetEditor", 'Cae.WinSettings.mixDesktopWidgets === "dms"');
    t = L.gate(t, "DesktopContextMenu", 'Cae.WinSettings.mixDesktopWidgets === "dms"');
    t = L.gate(t, "Lock", 'Cae.WinSettings.mixLock === "dms"', "lockLoader");
    t = t.replace(/\block\.activate\(\)/g, "lockLoader.item?.activate()");
    t = t.replace("active: root.dockEnabled", 'active: root.dockEnabled && Cae.WinSettings.mixDock === "dms"');
    t = t.replace("model: root.notificationPopupScreens", 'model: Cae.WinSettings.mixNotifs === "dms" ? root.notificationPopupScreens : []');
    t = t.replace('model: SettingsData.getFilteredScreens("toast")', 'model: Cae.WinSettings.mixNotifs === "dms" ? SettingsData.getFilteredScreens("toast") : []');
    t = t.replace("active: root.osdSurfacesLoaded", 'active: root.osdSurfacesLoaded && Cae.WinSettings.mixOsd === "dms"');
    // no first-run greeter / changelog pop-ups inside Caelestia
    t = t.replace(/if \(FirstLaunchService\.shouldShowGreeter\)\s*\n\s*active = true;/, "// Windows: no greeter");
    t = t.replace(/if \(ChangelogService\.shouldShowChangelog\)\s*\n\s*active = true;/, "// Windows: no changelog");
    return t;
}

function host(text) {
    let t = transform("shell.qml", text);
    t = t.replace(/^\/\/@ pragma[^\n]*\n/gm, "");
    t = L.addImport(t, "import qs.services as Cae");
    t = t.replace(/^ShellRoot \{/m, "Scope {");
    // Windows has no compositor decorations in DMS's style: DMS always draws its own window header (title,
    // close / minimise / maximise), and Windows' title bar is hidden on its windows (FloatingWindowControls)
    t = t.replace("DC.Host.ownWindowDecorations = Qt.binding(() => SettingsData.floatingWindowTitleBars);",
        "DC.Host.ownWindowDecorations = true; // Windows: always DMS's own header");
    // Caelestia decides about hot reload
    t = t.replace(/\n    Binding \{\n        target: Quickshell\n        property: "watchFiles"[\s\S]*?\n    \}\n/, "\n");
    // DMS's wallpaper only when Mix & match picked it
    t = t.replace(/(id: wallpaperLoader\n\s*)asynchronous: false/, '$1active: Cae.WinSettings.mixWallpaper === "dms" // Windows: Mix & match\n        asynchronous: false');
    // "themed" app title bars wear DMS's bar colours while DMS draws the bar
    t = t.replace(/\n\}\s*$/, "\n\n" +
        "    // Windows: apps' title bars in DMS's colours (Settings > Title bars > Themed) while DMS draws the bar\n" +
        "    Binding {\n        target: Cae.WinSettings\n        property: \"titleBarTint\"\n        value: Theme.surfaceContainer\n        when: (Cae.WinSettings.mixBar === \"dms\" && Cae.WinSettings.titleBars === \"themed\") || Cae.WinSettings.titleBars === \"dms\"\n        restoreMode: Binding.RestoreValue\n    }\n" +
        "    Binding {\n        target: Cae.WinSettings\n        property: \"titleBarTextTint\"\n        value: Theme.surfaceText\n        when: (Cae.WinSettings.mixBar === \"dms\" && Cae.WinSettings.titleBars === \"themed\") || Cae.WinSettings.titleBars === \"dms\"\n        restoreMode: Binding.RestoreValue\n    }\n}\n");
    // one wallpaper for every shell: DMS shows the wallpaper Caelestia has (whichever picker set it)
    t = t.replace(/\n\}\s*$/, "\n\n" +
        "    // Windows: one wallpaper for every shell - DMS follows Caelestia's (set from any shell's picker)\n" +
        "    function followCaelestiaWallpaper() {\n" +
        "        const p = String(Cae.Wallpapers.actualCurrent || \"\").split(String.fromCharCode(92)).join(\"/\");\n" +
        "        if (p && SessionData.wallpaperPath !== p)\n" +
        "            SessionData.wallpaperPath = p;\n" +
        "    }\n" +
        "    Connections {\n        target: Cae.Wallpapers\n        function onActualCurrentChanged() {\n            entrypoint.followCaelestiaWallpaper();\n        }\n    }\n" +
        "    Timer {\n        running: true\n        interval: 3000\n        onTriggered: entrypoint.followCaelestiaWallpaper()\n    }\n}\n");
    return "// DankMaterialShell / Dux Shell (MIT) running inside Caelestia for Windows.\n" +
        "// Generated by dotfiles/dms-port.js from its shell.qml - edit the script, not this file.\n" + t;
}

for (const out of OUTS) {
    L.emptyDir(out);
    L.copyTree(SRC, "", out, {
        skip: new Set([".git", "tests", "qmlformat-all.sh", "LICENSE_CHANGE_12_11_2025.md"]),
        transform,
        special: {
            "shell.qml": (src, o) => fs.writeFileSync(path.join(o, "DmsHost.qml"), host(fs.readFileSync(src, "utf8"))),
            "ShellCore.qml": (src, o) => fs.writeFileSync(path.join(o, "ShellCore.qml"), shellCore(fs.readFileSync(src, "utf8"))),
            "DMSShell.qml": (src, o) => fs.writeFileSync(path.join(o, "DMSShell.qml"), dmsShell(fs.readFileSync(src, "utf8")))
        }
    });
    fs.mkdirSync(path.join(out, "DCommon"), { recursive: true });
    L.copyTree(DCOMMON, "", path.join(out, "DCommon"), { skip: new Set(["tests"]), transform });
    fs.copyFileSync(path.join(REPO, "LICENSE"), path.join(out, "LICENSE"));
    L.applyOverrides(OVR, out);
    console.log("ported to", out);
}
