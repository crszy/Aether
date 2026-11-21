// Puts end-4's illogical-impulse shell (ref/end4 = github.com/end-4/dots-hyprland, GPL-3.0) inside Caelestia for
// Windows, like dms-port.js does for DMS. Its modules (qs, qs.modules.*, qs.services) have the same names as
// Caelestia's own, so they become qs.end4.*; its IPC targets get an "end4:" prefix. Its shell.qml is replaced by
// End4Host.qml (written below), whose panels are switched by WinSettings.mix*. Only the "ii" panel family is
// used ("waffle", its Windows-11 look-alike, is copied but never loaded).
//   node end4-port.js   (from dotfiles/)
const fs = require("fs");
const path = require("path");
const L = require("./shellport/lib.js");

const HERE = __dirname;
const SRC = path.join(HERE, "..", "ref", "end4", "dots", ".config", "quickshell", "ii");
const OUTS = [path.join(HERE, "caelestia-win", "end4"), path.join(HERE, "caelestia-shell-win", "end4")];
const OVR = path.join(HERE, "end4-overrides");
const ROOT = '(Quickshell.shellDir + "/end4")';

function transform(rel, t) {
    t = t.replace(/\r\n/g, "\n"); // a Windows checkout has CRLF line ends
    t = L.common(t, { rootExpr: ROOT });
    // its shellPath("x") means its own folder
    t = t.replace(/\bQuickshell\.shellPath\(/g, 'Quickshell.shellPath("end4/" + ');
    // namespacing (".import" in .js files too)
    t = t.replace(/^(\.?)import qs\s*$/gm, "$1import qs.end4");
    t = t.replace(/^(\.?)import qs\.(modules|services)\b/gm, "$1import qs.end4.$2");
    // IpcHandler targets
    t = t.replace(/(\btarget: )"([a-zA-Z][a-zA-Z0-9-]*)"/g, '$1"end4:$2"');
    // "file:///C:/x".slice(7) leaves "/C:/x"
    if (rel === "modules/common/functions/FileUtils.qml")
        t = t.replace('return s.startsWith("file:///") ? s.slice(7) : s;',
            'return s.replace(/^file:\\/\\/\\/?(?=[A-Za-z]:)/, "").replace(/^file:\\/\\//, ""); // Windows: keep C:/');
    // weather: curl.exe (part of Windows) and the same three parts jq picked out
    if (rel === "services/Weather.qml") {
        t = t.replace('command: ["bash", "-c", ""]', 'command: ["curl.exe", "-s", "-m", "15", "https://wttr.in/?format=j1"] // Windows');
        t = t.replace(/(function getData\(\) \{)[\s\S]*?fetcher\.running = true;\n    \}/,
            "$1 // Windows: curl.exe, no jq\n" +
            "        const where = (root.gpsActive && root.location.valid) ? `${root.location.lat},${root.location.long}` : formatCityName(root.city);\n" +
            "        fetcher.command = [\"curl.exe\", \"-s\", \"-m\", \"15\", `https://wttr.in/${where}?format=j1`];\n" +
            "        fetcher.running = true;\n    }");
        t = t.replace("const parsedData = JSON.parse(text);",
            "const raw = JSON.parse(text); // Windows: what jq picked out\n" +
            "                    const parsedData = raw.current_condition ? { current: raw.current_condition[0], location: raw.nearest_area[0], astronomy: raw.weather[0].astronomy[0] } : raw;");
    }
    // night light: hyprsunset -> Windows' night light (Quickshell.Windows NightLight); no automatic schedule
    // (Caelestia has its own), only the quick toggle
    if (rel === "services/Hyprsunset.qml") {
        t = L.addImport(t, "import Quickshell.Windows as Win");
        t = t.replace(/property bool automatic: [^\n]*/, "property bool automatic: false // Windows: only the toggle");
        t = t.replace(/(function startHyprsunset\(\) \{)\n[^\n]*\n(\s*\})/, "$1 // Windows: nothing to start\n$2");
        t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", `hyprctl hyprsunset temperature \$\{root\.colorTemperature\}`\]\);/,
            "Win.NightLight.enable(root.colorTemperature); // Windows");
        t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", `hyprctl hyprsunset temperature \$\{root\.defaultColorTemperature\}`\]\);/,
            "Win.NightLight.disable(); // Windows");
        t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", `hyprctl hyprsunset gamma \$\{root\.gamma\}`\]\);/, "// Windows: no gamma");
        t = t.replace(/(id: fetchProc\n\s*running: )true/, "$1false // Windows");
        t = t.replace(/Quickshell\.execDetached\(\["hyprctl", "hyprsunset", "temperature", `\$\{Config\.options\.light\.night\.colorTemperature\}`\]\);/,
            "Win.NightLight.enable(Config.options.light.night.colorTemperature); // Windows");
    }
    // wallpaper picks go to Caelestia, which also recolours everything (end-4 follows Caelestia's scheme)
    // Aether: the wallpaper never blinks out on a change - end-4 faded its wallpaper to 0 while the next picture
    // loaded (status Loading), although the image keeps showing the old picture meanwhile (retainWhileLoading)
    if (rel === "modules/ii/background/Background.qml") {
        const before = t;
        t = t.replace("                opacity: (status === Image.Ready && !bgRoot.wallpaperIsVideo) ? 1 : 0\n",
            "                opacity: ((status === Image.Ready || status === Image.Loading) && !bgRoot.wallpaperIsVideo) ? 1 : 0 // Aether: no blink while the next one loads\n");
        if (t === before)
            throw new Error("end4-port: background/Background.qml changed upstream - update the wallpaper blink fix");
    }
    // Aether: the media controls' visualizer reads Quickshell.Windows' AudioVisualizer (WASAPI loopback, shared with
    // Caelestia / DMS / Noctalia) instead of running cava with a Linux raw-output config - grouped into end-4's
    // 50 points, 0..1000 like cava's ascii output; the shared bar count is never changed
    if (rel === "modules/ii/mediaControls/MediaControls.qml") {
        const before = t;
        t = t.replace(/    Process \{\n        id: cavaProc\n[\s\S]*?\n        \}\n    \}\n/,
            "    // Aether (Windows): the shared AudioVisualizer instead of cava\n" +
            "    Timer {\n        id: cavaProc\n        interval: 16\n        repeat: true\n        running: mediaControlsLoader.active\n" +
            "        onRunningChanged: {\n            if (running)\n                AetherWin.AudioVisualizer.running = true; // on only - other shells may use it\n" +
            "            else\n                root.visualizerPoints = [];\n        }\n" +
            "        onTriggered: {\n            const src = AetherWin.AudioVisualizer.values ?? [];\n" +
            "            const max = Math.max(1, AetherWin.AudioVisualizer.maxValue);\n            if (!src.length)\n                return;\n" +
            "            const out = [];\n            for (let g = 0; g < 50; g++) {\n" +
            "                const i = Math.min(src.length - 1, Math.floor(g * src.length / 50));\n" +
            "                out.push(Math.max(0, Math.min(1, src[i] / max)) * 1000);\n            }\n" +
            "            root.visualizerPoints = out;\n        }\n    }\n");
        t = t.replace(/^(import Quickshell\s*)$/m, "$1\nimport Quickshell.Windows as AetherWin");
        if (t === before || !t.includes("AetherWin.AudioVisualizer.values") || !t.includes("import Quickshell.Windows as AetherWin"))
            throw new Error("end4-port: mediaControls/MediaControls.qml changed upstream - update the visualizer");
    }
    // Mix & match bar pieces (MIXING-PLAN.md): other shells' widgets in end-4's bar, after its tray
    // (WinSettings.barPieces, e.g. ["dms:CpuMonitor", "zenith:Resources"]; loaded through
    // Caelestia's modules/bar/pieces/MixPiece.qml)
    if (rel === "modules/ii/bar/BarContent.qml") {
        t = L.addImport(t, "import qs.services as Cae");
        const tray = "            SysTray {\n                visible: root.useShortenedForm === 0\n                Layout.fillWidth: false\n                Layout.fillHeight: true\n                invertSide: Config?.options.bar.bottom\n            }\n";
        t = t.replace(tray, tray +
            "\n            Repeater { // Windows: Mix & match pieces from the other shells\n" +
            "                model: Cae.WinSettings.barPieces ?? []\n\n" +
            "                Loader {\n                    required property var modelData\n\n" +
            "                    Layout.alignment: Qt.AlignVCenter\n" +
            "                    source: Qt.resolvedUrl(\"../../../../modules/bar/pieces/MixPiece.qml\")\n" +
            "                    onLoaded: {\n                        item.screen = Qt.binding(() => root.QsWindow.window?.screen ?? null);\n" +
            "                        item.pieceId = String(modelData);\n                    }\n                }\n            }\n");
        if (!t.includes("Mix & match pieces from the other shells"))
            throw new Error("end4-port: ii/bar/BarContent.qml changed upstream - update the Mix & match bar transform");
    }
    // Mix & match inside the lock (MIXING-PLAN.md): end-4's lock draws over the desktop, so "auto" already
    // shows the wallpaper of the shell picked under Wallpaper; "caelestia" puts Caelestia's wallpaper behind it
    if (rel === "modules/ii/lock/LockSurface.qml") {
        t = L.addImport(t, "import qs.services as Cae");
        t = t.replace("    hoverEnabled: true\n    acceptedButtons: Qt.LeftButton\n",
            "    Image { // Windows: Mix & match (WinSettings.lockWallpaper)\n" +
            "        anchors.fill: parent\n        z: -1\n" +
            "        visible: Cae.WinSettings.lockWallpaper === \"caelestia\" && !!Cae.Wallpapers.current\n" +
            "        source: visible ? \"file:///\" + String(Cae.Wallpapers.current).split(\"\\\\\").join(\"/\") : \"\"\n" +
            "        fillMode: Image.PreserveAspectCrop\n        asynchronous: true\n    }\n" +
            "    hoverEnabled: true\n    acceptedButtons: Qt.LeftButton\n");
        if (!t.includes("Mix & match (WinSettings.lockWallpaper)"))
            throw new Error("end4-port: ii/lock/LockSurface.qml changed upstream - update the Mix & match lock transform");
    }
    if (rel === "services/Wallpapers.qml") {
        t = L.addImport(t, "import qs.services as Cae");
        t = t.replace(/(function apply\(path, darkMode = [^)]*\) \{)/,
            "$1\n        if (path) { Cae.Wallpapers.setWallpaper(FileUtils.trimFileProtocol(path)); root.changed(); return; } // Windows");
        // "is it a folder?" without coreutils' test
        t = t.replace('selectProc.exec(["test", "-d", FileUtils.trimFileProtocol(filePath)])',
            'selectProc.exec(["cmd.exe", "/c", `if exist "${FileUtils.trimFileProtocol(filePath).split("/").join("\\\\")}\\\\*" (exit 0) else (exit 1)`]) // Windows');
        // start in Caelestia's wallpaper folder (the one its own picker uses)
        t = L.addImport(t, "import qs.utils as CaeU");
        t = t.replace(/property url defaultFolder: [^\n]*/, 'property url defaultFolder: "file:///" + String(CaeU.Paths.wallsdir).replace(/^file:\\/+/, "") // Windows');
    }
    // Linux /tmp -> end-4's cache folder
    if (["modules/common/Directories.qml", "services/Ai.qml", "services/Brightness.qml"].includes(rel))
        t = t.replace(/: "\/tmp\/quickshell\//g, ': Directories.winTmp + "/');
    if (rel === "modules/common/Directories.qml") {
        t = t.replace("property string assetsPath:", 'property string winTmp: FileUtils.trimFileProtocol(`${Directories.cache}/tmp`) // Windows: for /tmp/quickshell\n    property string assetsPath:');
        // its start-up clean-up deletes cache folders with rm -rf through bash: on Windows it only makes sure
        // its folders exist, and never deletes anything
        t = t.replace(/(Component\.onCompleted: \{)[\s\S]*?\n    \}/,
            "$1 // Windows: create the folders, delete nothing\n" +
            "        for (const p of [shellConfig, favicons, coverArt, booruPreviews, latexOutput, cliphistDecode, aiChats, userActions, tempImages])\n" +
            "            WinFs.FsUtils.makePath(p);\n    }");
        t = L.addImport(t, "import Quickshell.Windows as WinFs");
    }
    // uptime from Windows instead of /proc/uptime
    if (rel === "services/DateTime.qml") {
        t = L.addImport(t, "import Quickshell.Windows as WinUp");
        t = t.replace(/fileUptime\.reload\(\);\n\s*const textUptime = fileUptime\.text\(\);\n\s*const uptimeSeconds = [^\n]*/,
            "const uptimeSeconds = Number(WinUp.SystemMonitor.uptimeSeconds) || 0; // Windows");
        t = t.replace('path: "/proc/uptime"', 'path: "" // Windows');
    }
    // "which distro": Windows (assets/os-release is written by end4-port.js)
    if (rel === "services/SystemInfo.qml") {
        t = t.replace("switch (distroId) {", 'switch (distroId) {\n                case "windows": distroIcon = "windows-symbolic"; break; // Windows');
        t = t.replace('path: "/etc/os-release"', 'path: Quickshell.shellPath("end4/assets/os-release") // Windows');
        t = t.replace('command: ["bash", "-c", "echo $XDG_CURRENT_DESKTOP,$WAYLAND_DISPLAY"]', 'command: ["cmd.exe", "/c", "echo Caelestia,windows"] // Windows');
    }
    // Caelestia's hyprctl does not answer every query: an empty answer must not break the panels
    if (rel === "services/HyprlandData.qml") {
        t = t.replace("root.windowList = JSON.parse(clientsCollector.text)\n",
            "try { root.windowList = JSON.parse(clientsCollector.text || \"[]\"); } catch (e) { return; } // Windows\n");
        t = t.replace("var rawWorkspaces = JSON.parse(workspacesCollector.text);",
            "var rawWorkspaces; try { rawWorkspaces = JSON.parse(workspacesCollector.text); } catch (e) { return; } // Windows");
    }
    if (rel === "services/HyprlandData.qml")
        t = t.replace(/root\.(\w+) = JSON\.parse\((\w+)\.text\);/g, "try { root.$1 = JSON.parse($2.text); } catch (e) {} // Windows");
    if (rel === "services/HyprlandXkb.qml")
        t = t.replace("const parsedOutput = JSON.parse(devicesCollector.text);",
            "let parsedOutput; try { parsedOutput = JSON.parse(devicesCollector.text); } catch (e) { return; } // Windows\n" +
            "                if (!parsedOutput?.keyboards?.length) return;");
    // Linux commands behind end-4's buttons -> WinActions (end4-overrides/services/WinActions.qml)
    const before = t;
    const qsIpc = 'Quickshell.execDetached\\(\\["qs", "-p", Quickshell\\.shellPath\\("end4/" \\+ ""\\), "ipc", "call", ';
    t = t.replace(new RegExp(qsIpc + '"region", "(screenshot|search)"\\]\\)', "g"), "E4Win.screenshot()");
    t = t.replace(new RegExp(qsIpc + '"region", "recordWithSound"\\]\\)', "g"), "E4Win.record(true)");
    t = t.replace(new RegExp(qsIpc + '"(\\w+)", "(\\w+)"\\]\\)', "g"), 'E4Win.end4Ipc("$1", "$2")');
    t = t.replace(/Quickshell\.execDetached\(\["qs", "-p", (?:root\.settingsQmlPath|Quickshell\.shellPath\("end4\/" \+ "settings\.qml"\))\]\)/g, "E4Win.openSettings()");
    t = t.replace(/Quickshell\.execDetached\(\[Directories\.recordScriptPath\]\)/g, "E4Win.record(false)");
    t = t.replace(/Quickshell\.execDetached\(\["hyprpicker", "-a"\]\)/g, "E4Win.colorPicker()");
    t = t.replace(/Quickshell\.execDetached\(\["wpctl", "set-mute", "@DEFAULT_SOURCE@", "toggle"\]\)/g, "E4Win.toggleMic()");
    t = t.replace(/GlobalStates\.oskOpen = !GlobalStates\.oskOpen/g, "E4Win.toggleOsk()");
    t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", `\$\{Directories\.wallpaperSwitchScriptPath\} --mode (light|dark) --noswitch`\]\)/g,
        (_, m) => `E4Win.setDark(${m === "dark"})`);
    t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", `\$\{Directories\.wallpaperSwitchScriptPath\} --mode \$\{dark \? "dark" : "light"\} --noswitch`\]\)/g, "E4Win.setDark(dark)");
    t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", `\$\{Directories\.wallpaperSwitchScriptPath\} --noswitch`\]\)/g, "MaterialThemeLoader.reapplyTheme()");
    t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", (?:`\$\{(Config\.options\.apps\.\w+)\}`|(Config\.options\.apps\.\w+))\]\)/g, (_, a, b) => `E4Win.open(${a || b})`);
    t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", `\$\{Network\.ethernet \? Config\.options\.apps\.networkEthernet : Config\.options\.apps\.network\}`\]\)/g,
        "E4Win.open(Network.ethernet ? Config.options.apps.networkEthernet : Config.options.apps.network)");
    t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", "flatpak run com\.github\.wwmm\.easyeffects \|\| easyeffects"\]\)/g, 'E4Win.open("ms-settings:sound")');
    t = t.replace(/Quickshell\.execDetached\(\["hyprctl", "reload"\]\)/g, "/* Windows: no hyprctl */");
    t = t.replace(/Quickshell\.execDetached\(\["notify-send",/g, "E4Win.notifySend([");
    t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", "grim - \| wl-copy"\]\)/g, "E4Win.screenshotClip()");
    if (rel.endsWith("quickToggles/classicStyle/GameMode.qml")) {
        t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", `hyprctl --batch [^`]*`\]\)/, "E4Win.gameMode(true)");
        t = t.replace("/* Windows: no hyprctl */", "E4Win.gameMode(false)");
    }
    // end-4 deletes its decoded clipboard image with rm -f: ported shells never delete files
    if (rel === "modules/common/widgets/CliphistImage.qml")
        t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", `\[ -f [^`]*rm -f [^`]*`\]\);/, "// Windows: never delete (left in end-4's cache folder)");
    // lock: hyprctl workspace shuffling does nothing on Windows
    if (rel === "modules/ii/lock/Lock.qml")
        t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", batch\]\)/g, "void batch /* Windows: no hyprctl */");
    if (rel === "modules/common/panels/lock/LockScreen.qml")
        t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", "pidof hyprlock \|\| hyprlock"\]\);/, "// Windows: no hyprlock");
    if (rel === "modules/ii/sidebarLeft/aiChat/MessageCodeBlock.qml")
        t = t.replace(/Quickshell\.execDetached\(\["bash", "-c",\s*\n\s*`echo '\$\{StringUtils\.shellSingleQuoteEscape\(segmentContent\)\}' > '\$\{downloadPath\}\/code\.\$\{segmentLang \|\| "txt"\}'`\s*\n\s*\]\)/,
            'E4Win.saveText(`${downloadPath}/code.${segmentLang || "txt"}`, segmentContent)');
    if (rel === "modules/ii/sidebarLeft/anime/BooruImage.qml")
        t = t.replace(/Quickshell\.execDetached\(\["bash", "-c",\s*\n\s*`mkdir -p [^\n]*\n\s*\]\)/,
            "E4Win.download(root.imageData.file_url, targetPath, root.fileName, userAgent)");
    // Linux settings apps -> Windows settings pages
    if (rel === "modules/common/Config.qml") {
        const apps = {
            bluetooth: "ms-settings:bluetooth", changePassword: "ms-settings:signinoptions", network: "ms-settings:network-wifi",
            manageUser: "ms-settings:yourinfo", networkEthernet: "ms-settings:network-ethernet", taskManager: "taskmgr.exe",
            terminal: "wt.exe", update: "ms-settings:windowsupdate", volumeMixer: "ms-settings:apps-volume"
        };
        for (const [k, v] of Object.entries(apps))
            t = t.replace(new RegExp(`(property string ${k}: )[^\\n]*`), `$1"${v}" // Windows`);
    }
    // dark / light (quick toggle, launcher actions)
    t = t.replace(/Quickshell\.execDetached\(\[Directories\.wallpaperSwitchScriptPath, "--mode", "(light|dark)", "--noswitch"\]\)/g,
        (_, m) => `E4Win.setDark(${m === "dark"})`);
    t = t.replace(/Quickshell\.execDetached\(\[Directories\.recordScriptPath, "--fullscreen", "--sound"\]\)/g, "E4Win.record(true)");
    // launcher: terminal entries and ">" commands through Windows Terminal / cmd
    t = t.replace(/Quickshell\.execDetached\(\["bash", '-c', `\$\{Config\.options\.apps\.terminal\} -e '\$\{StringUtils\.shellSingleQuoteEscape\((\w+)\.command\.join\(' '\)\)\}'`\]\)/g,
        "E4Win.runInTerminal($1.command)");
    t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", root\.query\.startsWith\('sudo'\) \? [^\]]*\]\)/,
        "E4Win.runCommand(cleanedCommand)");
    // battery: Windows does its own low-battery sleep
    t = t.replace(/Quickshell\.execDetached\(\["bash", "-c", `systemctl suspend \|\| loginctl suspend`\]\);/g, "// Windows: power plan handles it");
    t = t.replace(/Quickshell\.execDetached\(\[\s*\n\s*"notify-send",/g, "E4Win.notifySend([");
    // system sounds: no /usr/share/sounds or ffplay
    if (rel === "services/Audio.qml")
        t = t.replace(/Quickshell\.execDetached\(command\);/g, "void command; // Windows: no ffplay");
    // never delete: the brightness screenshot clean-up (that grim pipeline does not run on Windows anyway)
    if (rel === "services/Brightness.qml")
        t = t.replace(/Quickshell\.execDetached\(\["rm", [^\n]*/, "// Windows: never delete");
    // clipboard history: Windows' (Quickshell.Windows Clipboard keeps the same "id<TAB>text" lines as cliphist)
    if (rel === "services/Cliphist.qml") {
        t = L.addImport(t, "import Quickshell.Windows as WinClip");
        t = t.replace(/property list<string> entries: \[\]/, "property list<string> entries: WinClip.Clipboard.entries // Windows");
        t = t.replace(/(function refresh\(\) \{)[\s\S]*?\n    \}/, "$1} // Windows: entries follow the clipboard");
        t = t.replace(/(function copy\(entry\) \{)[\s\S]*?\n    \}/, "$1 WinClip.Clipboard.copy(parseInt(entry.split(\"\\t\")[0])); }");
        t = t.replace(/(function paste\(entry\) \{)[\s\S]*?\n    \}/, "$1 WinClip.Clipboard.copy(parseInt(entry.split(\"\\t\")[0])); }");
        t = t.replace(/(function superpaste\(count, isImage = false\) \{)[\s\S]*?\n    \}/, "$1} // Windows: not supported");
        t = t.replace(/^(    function deleteEntry\(entry\) \{)[\s\S]*?\n    \}/m,"$1 WinClip.Clipboard.deleteEntry(parseInt(entry.split(\"\\t\")[0])); }");
        t = t.replace(/(function wipe\(\) \{)[\s\S]*?\n    \}/, "$1 WinClip.Clipboard.wipe(); }");
        t = t.replace(/(id: readProc[\s\S]*?command: )\[root\.cliphistBinary, "list"\]/, '$1["cmd.exe", "/c", "exit 0"] // Windows: unused');
        t = t.replace(/(id: wipeProc\n\s*command: )\[root\.cliphistBinary, "wipe"\]/, '$1["cmd.exe", "/c", "exit 0"] // Windows: unused');
        t = t.replace(/(id: deleteProc[\s\S]*?command: )\["bash"[^\n]*/, '$1["cmd.exe", "/c", "exit 0"] // Windows: unused');
    }
    if (rel === "modules/ii/sidebarLeft/AiChat.qml") {
        t = L.addImport(t, "import Quickshell.Windows as WinClip");
        t = t.replace(/decodeImageAndAttachProc\.exec\(\["bash", "-c", [^\n]*/,
            'decodeImageAndAttachProc.exec(["cmd.exe", "/c", "copy", "/y", WinClip.Clipboard.imagePath(parseInt(entry)).split("/").join("\\\\"), imageDecodeFilePath.split("/").join("\\\\")]); // Windows');
    }
    // wallpaper script / accent colour / random konachan wallpaper: Caelestia makes colours from the wallpaper
    if (["services/Wallpapers.qml", "services/LauncherSearch.qml"].includes(rel))
        t = L.addImport(t, "import qs.end4 as E4Root");
    if (rel === "services/Wallpapers.qml")
        t = t.replace(/Quickshell\.execDetached\(\[Directories\.wallpaperSwitchScriptPath, "--mode", darkMode \? "dark" : "light"\]\);/, "E4Root.GlobalStates.wallpaperSelectorOpen = true; // Windows");
    if (rel === "services/LauncherSearch.qml") {
        t = t.replace(/Quickshell\.execDetached\(\[Directories\.wallpaperSwitchScriptPath, "--noswitch", "--color"[^\n]*/, "void args; // Windows: colours come from the wallpaper");
        t = t.replace(/Quickshell\.execDetached\(\[Quickshell\.shellPath\("end4\/" \+ "scripts\/colors\/random\/random_konachan_wall\.sh"\)\]\);/, "E4Root.GlobalStates.wallpaperSelectorOpen = true; // Windows");
    }
    if (rel === "modules/common/widgets/CliphistImage.qml") {
        t = L.addImport(t, "import Quickshell.Windows as WinClip");
        t = t.replace(/decodeImageProcess\.running = true;/, "root.source = WinClip.Clipboard.imagePath(root.entryNumber); // Windows: Windows' clipboard keeps the image file");
        t = t.replace(/(id: decodeImageProcess\n\s*command: )\["bash"[^\n]*/, '$1["cmd.exe", "/c", "exit 0"] // Windows: unused');
    }
    // AI chat: curl.exe straight away instead of a bash script; the key stays in the environment
    // (curl's --variable / --expand-*), the request body goes through a file
    if (rel === "services/Ai.qml") {
        t = t.replace(/(\n\s*)\/\* Get authorization header from strategy \*\/[\s\S]*?requester\.running = true\n/,
            "$1// Windows: curl.exe, no bash script" +
            "$1const authHeader = requester.currentStrategy.buildAuthorizationHeader(root.apiKeyEnvVarName);" +
            "$1const expand = s => s.split(\"${\" + root.apiKeyEnvVarName + \"}\").join(\"{{\" + root.apiKeyEnvVarName + \"}}\");" +
            "$1root.pendingFilePath = \"\"; // Windows: attachments are not sent" +
            "$1const bodyPath = CF.FileUtils.trimFileProtocol(root.requestScriptFilePath) + \".json\";" +
            "$1WinAiFs.FsUtils.makePath(bodyPath.replace(/\\/[^\\/]*$/, \"\"));" +
            "$1requesterScriptFile.path = Qt.resolvedUrl(bodyPath);" +
            "$1requesterScriptFile.setText(JSON.stringify(data));" +
            "$1const cmd = [\"curl.exe\", \"--no-buffer\", \"-s\", \"--variable\", \"%\" + root.apiKeyEnvVarName + \"=\", \"--expand-url\", expand(endpoint)];" +
            "$1for (const [k, v] of Object.entries(requestHeaders)) if (v) cmd.push(\"-H\", `${k}: ${v}`);" +
            "$1const auth = /-H \"([^\"]*)\"/.exec(authHeader);" +
            "$1if (auth) cmd.push(\"--expand-header\", expand(auth[1]));" +
            "$1cmd.push(\"--data-binary\", \"@\" + bodyPath);" +
            "$1requester.command = cmd;" +
            "$1requester.running = true\n");
        t = L.addImport(t, "import Quickshell.Windows as WinAiFs");
        t = t.replace(/(id: requesterScriptFile)/, "$1\n        blockWrites: true // Windows: the body must be written before curl reads it");
        t = t.replace('command: ["bash", "-c", shellCommand]', 'command: ["cmd.exe", "/c", shellCommand] // Windows');
        t = t.replace(/command: \["ls", "-1", ([\w.]+)\]/g, 'command: ["cmd.exe", "/c", "dir", "/b", String($1).split("/").join("\\\\")] // Windows');
        t = t.replace(/text\.split\("\\n"\)/g, 'text.split(/\\r?\\n/)');
        // Ollama: ask it directly (its /api/tags) instead of the shell script
        t = t.replace(/command: \["bash", "-c", `\$\{Directories\.scriptPath\}\/ai\/show-installed-ollama-models\.sh`[^\n]*/,
            'command: ["curl.exe", "-s", "-m", "3", "http://127.0.0.1:11434/api/tags"] // Windows');
        t = t.replace("const dataJson = JSON.parse(data);",
            "const parsed = JSON.parse(data); const dataJson = Array.isArray(parsed) ? parsed : (parsed.models ?? []).map(m => m.name); // Windows");
        t = t.replace(/http:\/\/localhost:11434/g, "http://127.0.0.1:11434");
    }
    // translator: Google's free endpoint through curl.exe (upstream: the translate-shell "trans" script)
    if (rel === "modules/ii/sidebarLeft/Translator.qml") {
        t = t.replace(/command: \["bash", "-c", `trans -brief -no-bidi`[\s\S]*?shellSingleQuoteEscape\(root\.inputField\.text\.trim\(\)\)\}'`\]/,
            'command: ["curl.exe", "-s", "-m", "10", "https://translate.googleapis.com/translate_a/single?client=gtx&dt=t"\n' +
            '            + "&sl=" + encodeURIComponent(root.sourceLanguage || "auto")\n' +
            '            + "&tl=" + encodeURIComponent(!root.targetLanguage || root.targetLanguage === "auto" ? Qt.locale().name.split("_")[0] : root.targetLanguage)\n' +
            '            + "&q=" + encodeURIComponent(root.inputField.text.trim())] // Windows');
        t = t.replace("root.translatedText = translateProc.buffer.trim();",
            "try { root.translatedText = JSON.parse(translateProc.buffer)[0].map(s => s[0]).join(\"\"); } catch (e) { root.translatedText = \"\"; } // Windows");
        t = t.replace('command: ["trans", "-list-languages", "-no-bidi"]',
            'command: ["cmd.exe", "/c", "for %l in (en es fr de it pt ru ja ko zh-CN zh-TW ar hi bn nl pl tr sv uk vi id th he el cs ro hu fi da no ms fa) do @echo %l"] // Windows: Google language codes');
    }
    // downloads (album art, favicons, Booru/wallpaper images): curl.exe
    if (rel === "modules/ii/mediaControls/PlayerControl.qml")
        t = t.replace(/command: \[ "bash", "-c", `\[ -f \$\{artFilePath\} \] \|\| curl -4 -sSL '\$\{targetFile\}' -o '\$\{artFilePath\}'` \]/,
            'command: ["curl.exe", "-4", "-sSL", "--create-dirs", targetFile, "-o", artFilePath] // Windows');
    if (rel === "modules/common/widgets/Favicon.qml")
        t = t.replace(/command: \["bash", "-c", `\[ -f \$\{faviconFilePath\} \] \|\| curl [^\n]*/,
            'command: ["curl.exe", "-sL", "--create-dirs", "-A", downloadUserAgent || "Mozilla/5.0", root.faviconUrl, "-o", faviconFilePath] // Windows');
    if (rel === "modules/common/utils/ImageDownloaderProcess.qml") {
        t = L.addImport(t, "import Quickshell.Windows as WinImg");
        t = t.replace(/command: \["bash", "-c",\s*\n\s*`mkdir -p [^\n]*\n\s*\]/,
            'command: ["curl.exe", "-sSL", "--create-dirs", "-A", downloadUserAgent || "Mozilla/5.0", sourceUrl, "-o", FileUtils.trimFileProtocol(filePath)] // Windows\n' +
            '    onExited: { const s = WinImg.ImageTools.imageSize(FileUtils.trimFileProtocol(root.filePath)); if (s.width > 0) root.done(root.filePath, s.width, s.height); } // Windows');
    }
    // "is it an image?" without the file command: by extension
    if (rel === "modules/common/widgets/DirectoryIcon.qml")
        t = t.replace(/command: \["file", "--mime", "-b", fileModelData\.filePath\]/,
            'command: ["cmd.exe", "/c", "echo image/" + (String(fileModelData.filePath).split(".").pop().toLowerCase().replace("jpg", "jpeg")) + ";"] // Windows');
    if (rel.endsWith("CloudflareWarp.qml") || rel.endsWith("CloudflareWarpToggle.qml"))
        t = t.replace('command: ["bash", "-c", "warp-cli status"]', 'command: ["warp-cli", "status"] // Windows');
    // translations: no GNU find; the "generated" folder does not exist on Windows (read the shipped one twice)
    if (rel === "services/Translation.qml") {
        t = t.replace(/property string generatedTranslationsDir: [^\n]*/, "property string generatedTranslationsDir: root.translationsDir // Windows");
        t = t.replace(/command: \["find", translationScanner\.translationsDir, [^\n]*/,
            'command: ["cmd.exe", "/c", `for %f in ("${translationScanner.translationsDir.split("/").join("\\\\")}\\\\*.json") do @echo %~nf`] // Windows');
    }
    // upstream bugs that only warn: Workspaces has no widgetPadding; the tab bar asks for a child that is not there yet
    // Windows trays hold far more icons than Linux ones: shorten the bar sooner the more tray icons there are
    // (laptops at 125-150% scale are only 1280-1536 wide), so the clock never runs into the tray
    if (rel === "modules/ii/bar/BarContent.qml") {
        t = L.addImport(t, "import Quickshell.Services.SystemTray as WinTray");
        t = t.replace("(Appearance.sizes.barShortenScreenWidthThreshold >= screen?.width)",
            "(Appearance.sizes.barShortenScreenWidthThreshold + WinTray.SystemTray.items.values.length * 32 >= screen?.width)");
    }
    if (rel === "modules/ii/bar/BarContent.qml")
        t = t.replace("padding: workspacesWidget.widgetPadding", "padding: workspacesWidget.widgetPadding ?? 0");
    if (rel === "modules/common/widgets/ToolbarTabBar.qml")
        t = t.replace("property Item targetItem: contentItem.children[root.currentIndex]", "property Item targetItem: contentItem.children[root.currentIndex] ?? null")
            .replace("index: activeIndicator.targetItem.x + activeIndicator.targetItem.width", "index: (activeIndicator.targetItem?.x ?? 0) + (activeIndicator.targetItem?.width ?? 0)")
            .replace("index: activeIndicator.targetItem.x\n", "index: activeIndicator.targetItem?.x ?? 0\n");
    if (t !== before && t.includes("E4Win."))
        t = L.addImport(t, "import qs.end4.services as E4Win_");
    t = t.replace(/\bE4Win\./g, "E4Win_.WinActions.");
    // KDE's syntax highlighting (AI chat code blocks) is not on Windows: plain code text
    if (rel === "modules/ii/sidebarLeft/aiChat/MessageCodeBlock.qml") {
        t = t.replace(/^import org\.kde\.syntaxhighlighting\s*\n/m, "");
        t = t.replace(/\n(\s*)SyntaxHighlighter \{[\s\S]*?\n\1\}/, "\n$1// Windows: no KDE syntax highlighting");
    }
    return t;
}

// Which Caelestia Mix & match choice switches each end-4 panel on
const PANELS = [
    ["Bar", 'bar && !Config.options.bar.vertical'],
    ["VerticalBar", 'bar && Config.options.bar.vertical'],
    ["SidebarLeft", "bar"],
    ["SidebarRight", "bar"],
    ["Cheatsheet", "bar"],
    ["MediaControls", "bar"],
    ["ScreenCorners", "bar"],
    ["SessionScreen", "bar"],
    ["WallpaperSelector", "bar"],
    ["Overview", "bar || launcher"],
    ["NotificationPopup", "notifs"],
    ["OnScreenDisplay", "osd"],
    ["Lock", "lock"],
    ["Background", "wallpaper"],
    ["Dock", 'dock'],
];

function host() {
    const imports = ["background", "bar", "cheatsheet", "dock", "lock", "mediaControls", "notificationPopup",
        "onScreenDisplay", "overview", "screenCorners", "sessionScreen", "sidebarLeft", "sidebarRight",
        "verticalBar", "wallpaperSelector"].map(m => `import qs.end4.modules.ii.${m}`).join("\n");
    const loaders = PANELS.map(([name, cond]) => `    LazyLoader { active: Config.ready && (${cond}); component: ${name} {} }`).join("\n");
    return `// end-4's illogical-impulse (github.com/end-4/dots-hyprland, GPL-3.0) running inside Caelestia for Windows.
// Generated by dotfiles/end4-port.js - edit the script, not this file. Replaces its shell.qml: only the "ii"
// panels, each switched by Caelestia's Mix & match.
import QtQuick
import Quickshell
import qs.services as Cae
import qs.end4
import qs.end4.modules.common
import qs.end4.services
${imports}

Scope {
    id: root

    readonly property bool bar: Cae.WinSettings.mixBar === "end4"
    readonly property bool launcher: Cae.WinSettings.mixLauncher === "end4"
    readonly property bool notifs: Cae.WinSettings.mixNotifs === "end4"
    readonly property bool osd: Cae.WinSettings.mixOsd === "end4"
    readonly property bool lock: Cae.WinSettings.mixLock === "end4"
    readonly property bool wallpaper: Cae.WinSettings.mixWallpaper === "end4"
    readonly property bool dock: Cae.WinSettings.mixDock === "end4"

    Component.onCompleted: {
        MaterialThemeLoader.reapplyTheme();
        Cliphist.refresh();
        void WinActions.open; // create it: its IPC (end4:actions) only exists once it is
        Qt.callLater(root.followCaelestiaWallpaper);
    }

    // one wallpaper for every shell: end-4 shows the wallpaper Caelestia has (its own picker already sets
    // Caelestia's - services/Wallpapers.qml)
    function followCaelestiaWallpaper() {
        const p = String(Cae.Wallpapers.actualCurrent || "").split(String.fromCharCode(92)).join("/");
        if (p && Config.ready && Config.options.background.wallpaperPath !== p)
            Config.options.background.wallpaperPath = p;
    }
    Connections {
        target: Cae.Wallpapers
        function onActualCurrentChanged() {
            root.followCaelestiaWallpaper();
        }
    }
    Connections {
        target: Config
        function onReadyChanged() {
            root.followCaelestiaWallpaper();
        }
    }

${loaders}
}
`;
}

for (const out of OUTS) {
    L.emptyDir(out);
    L.copyTree(SRC, "", out, {
        skip: new Set([".qmlformat.ini", "welcome.qml", "killDialog.qml", "shell.qml"]),
        transform
    });
    fs.writeFileSync(path.join(out, "End4Host.qml"), host());
    fs.writeFileSync(path.join(out, "assets", "os-release"),
        'NAME="Windows"\nPRETTY_NAME="Windows 11 + Caelestia"\nID=windows\nHOME_URL="https://www.microsoft.com/windows"\nLOGO=\n');
    // end-4 is GPL-3.0: its license travels with it
    fs.copyFileSync(path.join(HERE, "..", "ref", "end4", "LICENSE"), path.join(out, "LICENSE"));
    L.applyOverrides(OVR, out);
    console.log("ported to", out);
}
