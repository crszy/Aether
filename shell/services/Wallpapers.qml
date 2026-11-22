pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import Quickshell.Windows as Win
import Caelestia.Config
import Caelestia.Models
import qs.services
import qs.utils

Searcher {
    id: root

    readonly property string currentNamePath: `${Paths.state}/wallpaper/path.txt`
    readonly property list<string> smartArg: GlobalConfig.services.smartScheme ? [] : ["--no-smart"]
    readonly property string fallback: Quickshell.shellPath("assets/wallpaper.webp")

    function ensureWalls(dir: string): string {
        const def = `${Paths.pictures}/Wallpapers`;
        if (dir && dir.toLowerCase() === def.toLowerCase() && Win.FsUtils.classify(dir) !== "dir")
            Win.FsUtils.makePath(dir);
        return dir;
    }

    property bool showPreview: false
    readonly property string current: showPreview ? previewPath : actualCurrent
    property string previewPath
    property string actualCurrent
    property bool previewColourLock
    property bool pendingPreviewClear

    function getCategoryFor(w: FileSystemEntry): string {
        let category = w.parentDir.slice(Paths.wallsdir.length + 1);
        if (category.includes("/"))
            category = category.slice(0, category.indexOf("/"));
        return category;
    }

    function nextWallpaper(): void {
        const paths = root.list.map(w => String(w.path));
        if (!paths.length)
            return;
        const norm = x => String(x).split(String.fromCharCode(92)).join("/").toLowerCase(); // paths come with / or \
        const i = paths.map(norm).indexOf(norm(root.actualCurrent));
        root.setWallpaper(paths[(i + 1) % paths.length]);
    }

    function setRandom(): void {
        // Aether: picked here (instant) instead of the caelestia CLI - and chosen ahead (nextRandom), so its
        // colours are already worked out when the key is pressed
        const pick = root.pickRandom();
        root.nextRandom = "";
        if (pick)
            root.setWallpaper(pick);
    }

    property real lastSetAt: 0

    function setWallpaper(path: string): void {
        lastSetAt = Date.now();
        // Aether: colours that change WITH the wallpaper transition. When this wallpaper's colours were worked out
        // ahead (schemeCache), they are written now - every shell that follows Caelestia's colours recolours while the
        // transition plays, instead of ~400 ms later when the CLI is done. The CLI still runs and writes the same.
        const ahead = Colours.scheme === "dynamic" ? root.schemeCache[root.norm(path)] : undefined;
        if (ahead)
            schemeWriter.setText(ahead);
        actualCurrent = path;
        Quickshell.execDetached(["caelestia", "wallpaper", "-f", path, ...smartArg]);
    }

    // ---------------- Aether: colours worked out ahead (for Alt+W next / Alt+Shift+W random) ----------------
    property var schemeCache: ({})        // normalised wallpaper path -> the scheme.json text the CLI would write
    property string nextRandom: ""        // the next random pick, chosen early so its colours can be ready
    property var warmQueue: []
    property string schemeKey: ""         // your variant / mode / flavour choice: a change makes every cached scheme stale

    function norm(x: var): string {
        return String(x ?? "").split(String.fromCharCode(92)).join("/").toLowerCase();
    }

    function nextPath(ahead: int): string {   // the wallpaper `ahead` steps after the current one (Alt+W)
        const paths = root.list.map(w => String(w.path));
        if (!paths.length)
            return "";
        const i = paths.map(root.norm).indexOf(root.norm(root.actualCurrent));
        return paths[(i + Math.max(1, ahead)) % paths.length];
    }

    function pickRandom(): string {
        const cur = root.norm(root.actualCurrent);
        if (root.nextRandom && root.norm(root.nextRandom) !== cur && root.list.some(w => root.norm(w.path) === root.norm(root.nextRandom)))
            return root.nextRandom;
        const paths = root.list.map(w => String(w.path)).filter(p => root.norm(p) !== cur);
        return paths.length ? paths[Math.floor(Math.random() * paths.length)] : "";
    }

    // work out the colours of what the keys will show next (one CLI preview each, ~250 ms, one at a time)
    function warm(): void {
        if (Colours.scheme !== "dynamic")
            return;
        if (!root.nextRandom || root.norm(root.nextRandom) === root.norm(root.actualCurrent))
            root.nextRandom = root.pickRandom();
        root.warmQueue = [root.nextPath(1), root.nextRandom, root.nextPath(2), root.nextPath(3)].filter((p, i, a) => p && a.indexOf(p) === i && !(root.norm(p) in root.schemeCache));
        root.runWarm();
    }

    function runWarm(): void {
        if (warmProc.running || !root.warmQueue.length)
            return;
        const p = root.warmQueue[0];
        root.warmQueue = root.warmQueue.slice(1);
        warmProc.path = p;
        warmProc.command = ["caelestia", "wallpaper", "-p", p, ...root.smartArg];
        warmProc.running = true;
    }

    onActualCurrentChanged: warmTimer.restart()
    onListChanged: warmTimer.restart()

    Timer {
        id: warmTimer

        interval: 700     // after the CLI of the wallpaper just set has started
        onTriggered: root.warm()
    }

    Process {
        id: warmProc

        property string path

        stdout: StdioCollector {
            onStreamFinished: {
                const t = text.trim();
                if (!t.startsWith("{"))
                    return;
                const c = Object.assign({}, root.schemeCache);
                c[root.norm(warmProc.path)] = t;
                root.schemeCache = c;
            }
        }
        onExited: Qt.callLater(root.runWarm)
    }

    // writes a worked-out scheme straight away (the CLI writes the same file a moment later)
    FileView {
        id: schemeWriter

        path: `${Paths.state}/scheme.json`
        printErrors: false
        watchChanges: true
        onFileChanged: reload()
        onLoaded: {
            try {
                const s = JSON.parse(text());
                // smart colours pick variant + mode per wallpaper (part of each cached result) - only your own choice counts
                const key = GlobalConfig.services.smartScheme ? `smart|${s.flavour}` : `${s.variant}|${s.mode}|${s.flavour}`;
                if (root.schemeKey && key !== root.schemeKey) {
                    root.schemeCache = {};   // variant / mode changed: work everything out again
                    warmTimer.restart();
                }
                root.schemeKey = key;
            } catch (e) {}
        }
    }

    function preview(path: string): void {
        previewPath = path;
        showPreview = true;

        if (Colours.scheme === "dynamic")
            getPreviewColoursProc.running = true;
    }

    function stopPreview(): void {
        showPreview = false;
        if (previewColourLock)
            pendingPreviewClear = true;
        else
            Colours.showPreview = false;
    }

    onPreviewColourLockChanged: {
        if (!previewColourLock && pendingPreviewClear)
            Colours.showPreview = false;
    }

    list: wallpapers.entries
    key: "relativePath"
    useFuzzy: GlobalConfig.launcher.useFuzzy.wallpapers
    extraOpts: useFuzzy ? ({}) : ({
            forward: false
        })

    IpcHandler {
        function get(): string {
            return root.actualCurrent;
        }

        function set(path: string): void {
            root.setWallpaper(path);
        }

        function list(): string {
            return root.list.map(w => w.path).join("\n");
        }

        // Aether: for a key (hyprland.conf exec): a random wallpaper, or the next one in the folder
        function random(): void {
            root.setRandom();
        }

        function next(): void {
            root.nextWallpaper();
        }

        target: "wallpaper"
    }

    // Aether: the window manager's "aether, wallpaper next|random" binds arrive as an event - instant, no process
    // started per key press, so holding / hammering the key flows from one wallpaper into the next
    Connections {
        target: Hyprland

        function onRawEvent(event: var): void {
            if (event.name !== "aether")
                return;
            const what = String(event.data).trim();
            if (what === "wallpaper next")
                root.nextWallpaper();
            else if (what === "wallpaper random")
                root.setRandom();
        }
    }

    FileView {
        path: root.currentNamePath
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            let wall = text().trim();
            if (!wall) {
                wall = root.fallback;
                Quickshell.execDetached(["caelestia", "wallpaper", "-f", root.fallback, ...root.smartArg]);
            }
            // Aether: a file written by an earlier (slower) set must not undo a newer pick (rapid key presses)
            if (Date.now() - root.lastSetAt < 3000 && wall.split(String.fromCharCode(92)).join("/").toLowerCase() !== String(root.actualCurrent).split(String.fromCharCode(92)).join("/").toLowerCase()) {
                root.previewColourLock = false;
                return;
            }
            root.actualCurrent = wall;
            root.previewColourLock = false;
        }
        onLoadFailed: {
            root.actualCurrent = root.fallback;
            root.previewColourLock = false;
            Quickshell.execDetached(["caelestia", "wallpaper", "-f", root.fallback, ...root.smartArg]);
        }
    }

    FileSystemModel {
        id: wallpapers

        recursive: true
        // Windows: a fresh install points at ~/Pictures/Wallpapers, which usually does not exist yet - make it
        // (only that default; a folder the user typed is never created behind their back)
        path: root.ensureWalls(Paths.wallsdir)
        filter: FileSystemModel.Images | FileSystemModel.Videos
    }

    Process {
        id: getPreviewColoursProc

        command: ["caelestia", "wallpaper", "-p", root.previewPath, ...root.smartArg]
        stdout: StdioCollector {
            onStreamFinished: {
                Colours.load(text, true);
                Colours.showPreview = true;
            }
        }
    }
}
