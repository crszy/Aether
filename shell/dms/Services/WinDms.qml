pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as Win
import Caelestia.Services as CaeS
import qs.services as Cae

// Windows stand-in for DMS's backend ("dms" Go program, Linux only). DMSService.sendRequest comes here
// (dms-port.js): the requests that mean something on Windows are answered from Windows itself or from
// Caelestia's services, in the backend's own reply format; the Linux-only ones (CUPS, systemd, Wayland
// outputs, Tailscale, plugin registries...) get a clean "not on Windows" error, which DMS already handles.
//   lyrics      Caelestia's lyrics engine (the same lyrics Caelestia's dashboard shows)
//   clipboard   Windows' clipboard history (Quickshell.Windows Clipboard)
//   brightness  Caelestia's brightness (laptop panels + DDC/CI monitors)
//   wellbeing   screen time per app and daily / per-app limits, counted here, kept in wellbeing.json
//   misc        open links, notifications, drives, user info
//   dynamic theme  DMS's "Dynamic" colours (matugen's dms-colors.json on Linux) = Caelestia's wallpaper palette
Singleton {
    id: root

    // what DMS may rely on (DMSService.capabilities)
    readonly property var capabilities: ["lyrics", "clipboard", "brightness", "wellbeing"]

    property int _next: 1
    property var _cancelled: ({})

    function handle(method, params, callback, timeoutMs) {
        const id = Date.now() * 10 + (_next++ % 10);
        const reply = result => {
            if (callback && !_cancelled[id])
                Qt.callLater(() => callback({ id: id, result: result }));
        };
        const fail = msg => {
            if (callback && !_cancelled[id])
                Qt.callLater(() => callback({ id: id, error: msg }));
        };
        const m = String(method);
        try {
            if (m.startsWith("lyrics."))
                lyrics(m, params ?? {}, reply, fail, id);
            else if (m.startsWith("clipboard."))
                clipboard(m, params ?? {}, reply, fail);
            else if (m.startsWith("wellbeing."))
                wellbeing(m, params ?? {}, reply, fail);
            else if (m.startsWith("brightness."))
                brightness(m, params ?? {}, reply, fail);
            else
                misc(m, params ?? {}, reply, fail);
        } catch (e) {
            fail(`${m}: ${e}`);
        }
        return id;
    }

    function cancel(id) {
        _cancelled[id] = true;
    }

    // ---------------- lyrics: Caelestia's engine ----------------
    property var _lyricsWaiting: []

    function lyrics(m, p, reply, fail, id) {
        if (m === "lyrics.plugins") {
            reply({ plugins: [] });
            return;
        }
        if (m !== "lyrics.get") {
            fail("not on Windows");
            return;
        }
        const L = CaeS.Lyrics;
        // the same song as Caelestia's engine already has (case, spaces and punctuation aside): never reset it -
        // Caelestia's own lyrics view shares this engine, and a reset to DMS's spelling broke Caelestia's lyrics
        const norm = s => String(s ?? "").toLowerCase().replace(/[^a-z0-9À-￿]+/g, "");
        const same = norm(L.trackTitle) === norm(p.title) && (norm(L.trackArtist) === norm(p.artist) || !norm(p.artist) || !norm(L.trackArtist));
        if (!same)
            L.setTrack(p.artist ?? "", p.title ?? "", p.album ?? "", Number(p.duration) || 0);
        if (same && !L.loading) {
            reply(lyricsResult());
            return;
        }
        _lyricsWaiting = _lyricsWaiting.concat([{ id: id, reply: reply, at: Date.now() }]);
        lyricsTimeout.restart();
    }

    function lyricsResult() {
        const L = CaeS.Lyrics;
        const texts = L.lyrics ?? [];
        if (!L.hasLyrics || texts.length === 0)
            return { found: false };
        const synced = [];
        for (let i = 0; i < texts.length; i++) {
            const t = L.timeForIndex(i);
            const next = L.timeForIndex(i + 1);
            if (t < 0)
                continue;
            // word timings ("<00:35.34>I <00:36.66>just ...") become DMS's karaoke words; the line text is them joined
            const parts = String(texts[i]).split(/<(\d+):(\d+(?:\.\d+)?)>/);
            const words = [];
            for (let k = 1; k + 2 < parts.length + 1; k += 3) {
                const wt = Number(parts[k]) * 60 + Number(parts[k + 1]);
                const wx = parts[k + 2] ?? "";
                if (wx !== "")
                    words.push({ t: Math.max(t, wt), x: wx });
            }
            for (let k = 0; k < words.length; k++)
                words[k].e = k + 1 < words.length ? words[k + 1].t : (next > t ? next : words[k].t);
            const text = words.length > 0 ? words.map(w => w.x).join("") : String(texts[i]).replace(/<\d+:\d+(?:\.\d+)?>/g, "");
            synced.push(Object.assign({ t: t, e: next > t ? next : 0, x: text }, words.length > 0 && parts[0] === "" ? { w: words } : {}));
        }
        return synced.length > 0
            ? { found: true, synced: synced, attribution: { provider: "Caelestia" } }
            : { found: true, plain: texts.join("\n"), attribution: { provider: "Caelestia" } };
    }

    function flushLyrics() {
        const waiting = _lyricsWaiting;
        _lyricsWaiting = [];
        const r = lyricsResult();
        for (const w of waiting)
            w.reply(r);
    }

    Connections {
        target: CaeS.Lyrics
        function onLoadingChanged() {
            if (!CaeS.Lyrics.loading && root._lyricsWaiting.length > 0)
                root.flushLyrics();
        }
        function onLyricsChanged() {
            if (!CaeS.Lyrics.loading && root._lyricsWaiting.length > 0)
                root.flushLyrics();
        }
    }
    Timer {
        id: lyricsTimeout
        interval: 15000
        onTriggered: root.flushLyrics()
    }

    // ---------------- clipboard: Windows' clipboard history ----------------
    // Clipboard.entries lines are "id<TAB>text" or "id<TAB>[[ binary data WxH ]]" (newest first)
    function clipEntries() {
        return (Win.Clipboard.entries ?? []).map(line => {
            const tab = line.indexOf("\t");
            const id = parseInt(line.slice(0, tab));
            const text = line.slice(tab + 1);
            const img = /^\[\[ binary data (\d+)x(\d+) \]\]$/.exec(text);
            return {
                id: id,
                preview: img ? `Image ${img[1]}x${img[2]}` : text,
                isImage: !!img,
                mimeType: img ? "image/png" : "text/plain",
                width: img ? parseInt(img[1]) : 0,
                height: img ? parseInt(img[2]) : 0,
                size: text.length,
                hash: String(id),
                pinned: false
            };
        });
    }

    function clipboard(m, p, reply, fail) {
        switch (m) {
        case "clipboard.search": {
            if (p.pinned) {
                reply({ entries: [], total: 0 });
                return;
            }
            const q = String(p.query ?? "").toLowerCase();
            let all = clipEntries().filter(e => (!q || e.preview.toLowerCase().includes(q)) && (!p.type || (p.type === "image") === e.isImage));
            const offset = p.offset ?? 0;
            reply({ entries: all.slice(offset, offset + (p.limit || all.length)), total: all.length });
            return;
        }
        case "clipboard.getEntry": {
            const e = clipEntries().find(x => x.id === Number(p.id));
            if (!e) {
                reply(null);
                return;
            }
            reply(Object.assign({}, e, e.isImage ? { imagePath: Win.Clipboard.imagePath(e.id) } : {}));
            return;
        }
        case "clipboard.copyEntry":
        case "clipboard.sendPaste":
            Win.Clipboard.copy(Number(p.id));
            reply({ ok: true });
            return;
        case "clipboard.copy":
            Win.Clipboard.copyText(String(p.text ?? p.data ?? ""));
            reply({ ok: true });
            return;
        case "clipboard.deleteEntry":
            Win.Clipboard.deleteEntry(Number(p.id));
            reply({ ok: true });
            return;
        case "clipboard.clearHistory":
            Win.Clipboard.wipe();
            reply({ ok: true });
            return;
        case "clipboard.pasteSupported":
            reply({ supported: false });
            return;
        case "clipboard.getConfig":
            reply({ maxHistory: 100, disabled: false });
            return;
        default:
            fail("not on Windows");
        }
    }

    // ---------------- wellbeing: screen time (DMS's Digital wellbeing tab) ----------------
    // DMS says which app is in front (wellbeing.setState); the time is counted here while the session is
    // active, per day, and sent back as wellbeingStateUpdate {today}. Limits fire once per day.
    readonly property string wellbeingFile: (Quickshell.env("LOCALAPPDATA") || "").split(String.fromCharCode(92)).join("/") + "/caelestia/dms-wellbeing.json"
    property var wbDays: ({})
    property var wbLimits: ({ daily: 0, apps: {} })
    property var wbNotified: ({})
    property string wbApp: ""
    property bool wbActive: false
    property real wbLast: 0
    property bool wbLoaded: false

    function dayKey(d) {
        const pad = n => String(n).padStart(2, "0");
        return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
    }
    function wbToday() {
        const key = dayKey(new Date());
        if (!wbDays[key])
            wbDays[key] = { date: key, active: 0, apps: {} };
        return wbDays[key];
    }
    function wbTick() {
        const now = Date.now();
        const dt = wbLast > 0 ? Math.min(60, (now - wbLast) / 1000) : 0;
        wbLast = now;
        if (!wbActive || dt <= 0)
            return;
        const day = wbToday();
        day.active += dt;
        if (wbApp)
            day.apps[wbApp] = (day.apps[wbApp] ?? 0) + dt;
        DMSService.wellbeingStateUpdate({ today: JSON.parse(JSON.stringify(day)) });
        // limits, once a day each
        const notify = (kind, appId, used) => {
            const k = `${day.date}|${kind}|${appId}`;
            if (wbNotified[k])
                return;
            wbNotified[k] = true;
            DMSService.wellbeingStateUpdate({ limit: { kind: kind, appId: appId, used: Math.round(used) } });
        };
        if (wbLimits.daily > 0 && day.active >= wbLimits.daily)
            notify("daily", "", day.active);
        for (const appId in wbLimits.apps ?? {})
            if (wbLimits.apps[appId] > 0 && (day.apps[appId] ?? 0) >= wbLimits.apps[appId])
                notify("app", appId, day.apps[appId]);
    }
    function wbSave() {
        // keep 92 days
        const keys = Object.keys(wbDays).sort();
        for (const k of keys.slice(0, Math.max(0, keys.length - 92)))
            delete wbDays[k];
        Win.FsUtils.makePath(wellbeingFile.slice(0, wellbeingFile.lastIndexOf("/")));
        wbFile.setText(JSON.stringify({ days: wbDays }));
    }

    function wellbeing(m, p, reply, fail) {
        switch (m) {
        case "wellbeing.setState":
            wbTick();
            wbApp = String(p.appId ?? "");
            wbActive = p.active === true;
            wbLast = Date.now();
            reply({ ok: true });
            return;
        case "wellbeing.setLimits":
            wbLimits = { daily: Number(p.daily) || 0, apps: p.apps ?? {} };
            reply({ ok: true });
            return;
        case "wellbeing.summary": {
            const n = Number(p.days) || 92;
            wbToday();
            reply({ days: Object.keys(wbDays).sort().slice(-n).map(k => wbDays[k]) });
            return;
        }
        case "wellbeing.clear":
            wbDays = {};
            wbNotified = {};
            wbSave();
            DMSService.wellbeingStateUpdate({ today: wbToday() });
            reply({ ok: true });
            return;
        default:
            fail("not on Windows");
        }
    }

    FileView {
        id: wbFile
        path: root.wellbeingFile
        printErrors: false
        blockLoading: true
        onLoaded: {
            try {
                root.wbDays = JSON.parse(text()).days ?? {};
            } catch (e) {}
            root.wbLoaded = true;
        }
        onLoadFailed: root.wbLoaded = true
    }
    Timer {
        // count while something is tracked; save once a minute
        running: root.wbActive
        interval: 5000
        repeat: true
        property int n: 0
        onTriggered: {
            root.wbTick();
            if (++n % 12 === 0)
                root.wbSave();
        }
    }
    Component.onDestruction: if (Object.keys(wbDays).length) wbSave()

    // ---------------- brightness: Caelestia's ----------------
    function brightnessDevices() {
        return (Cae.Brightness.monitors ?? []).filter(mon => mon.available).map(mon => ({
            id: mon.modelData.name,
            class: mon.isDdc ? "ddc" : "backlight",
            current: Math.round(mon.brightness * 100),
            currentPercent: Math.round(mon.brightness * 100),
            max: 100,
            backend: "windows"
        }));
    }

    function brightness(m, p, reply, fail) {
        if (m === "brightness.getState" || m === "brightness.rescan") {
            reply({ devices: brightnessDevices() });
            return;
        }
        if (m === "brightness.setBrightness") {
            const mon = (Cae.Brightness.monitors ?? []).find(x => x.modelData.name === p.device) ?? Cae.Brightness.monitors[0];
            if (!mon) {
                fail("no display with brightness control");
                return;
            }
            mon.setBrightness(Math.max(0, Math.min(1, Number(p.percent) / 100)));
            reply({ ok: true });
            return;
        }
        fail("not on Windows");
    }

    // ---------------- everything else ----------------
    function misc(m, p, reply, fail) {
        switch (m) {
        case "shell.ready":
            reply({ ok: true });
            return;
        case "system.openUri":
            Qt.openUrlExternally(p.uri ?? p.url ?? "");
            reply({ ok: true });
            return;
        case "notify.send":
            // DMS shows its own toast for these
            reply({ ok: true });
            return;
        case "drives.list":
            reply({ drives: (Win.SystemMonitor.disks ?? []).reduce((a, d) => a.concat(d.volumes ?? []), []).map(v => ({ mount: v.mount, size: v.totalBytes, used: v.usedBytes, fstype: "ntfs", removable: false })) });
            return;
        case "freedesktop.getState":
            reply({ accounts: { userName: Quickshell.env("USERNAME") ?? "", realName: Quickshell.env("USERNAME") ?? "", iconFile: "" } });
            return;
        case "calendars.list":
            reply({ calendars: [] });
            return;
        case "location.getState":
        case "theme.auto.getState":
            reply({ enabled: false });
            return;
        case "matugen.status":
            reply({ available: false });
            return;
        case "wallpaper.list":
            reply({ wallpapers: [] });
            return;
        default:
            fail("not on Windows");
        }
    }

    // ---------------- dynamic theme: Caelestia's wallpaper palette as matugen's dms-colors.json ----------------
    // (Theme.stateDir is the generic cache folder: %LOCALAPPDATA%/cache/dms on Windows)
    readonly property string dynamicColorsFile: (Quickshell.env("LOCALAPPDATA") || "").split(String.fromCharCode(92)).join("/") + "/cache/dms/dms-colors.json"

    function writeDynamicColors() {
        const p = Cae.Colours.palette;
        const scheme = {};
        for (const key in p) {
            if (!/^m3[a-z]/.test(key) || key.includes("_") || typeof p[key] === "function")
                continue;
            scheme[key.slice(2).replace(/[A-Z]/g, c => "_" + c.toLowerCase())] = String(p[key]).replace(/^#ff([0-9a-f]{6})$/i, "#$1");
        }
        if (!scheme.primary)
            return;
        const text = JSON.stringify({ mode: Cae.Colours.light ? "light" : "dark", colors: { dark: scheme, light: scheme } }, null, 2);
        if (text === dynamicColorsOut._last)
            return;
        dynamicColorsOut._last = text;
        Win.FsUtils.makePath(dynamicColorsFile.slice(0, dynamicColorsFile.lastIndexOf("/")));
        dynamicColorsOut.setText(text);
    }

    FileView {
        id: dynamicColorsOut
        property string _last: ""
        path: root.dynamicColorsFile
        blockWrites: false
        atomicWrites: true
        printErrors: false
    }

    Connections {
        target: Cae.Colours.palette
        function onM3primaryChanged() { dynamicColorsTimer.restart(); }
        function onM3surfaceChanged() { dynamicColorsTimer.restart(); }
    }

    Timer {
        id: dynamicColorsTimer
        interval: 600
        running: true // once at start
        onTriggered: root.writeDynamicColors()
    }

    // ---------------- >border: the window manager's focus border colour (general:col.active_border) ----------------
    // A static colour: nothing is redrawn per frame, so it costs no CPU. Saved in hyprland.conf and sent to the
    // running window manager with hyprctl keyword.
    function hexOf(c) {
        const h = v => Math.round(v * 255).toString(16).padStart(2, "0");
        return h(c.r) + h(c.g) + h(c.b);
    }

    function borderChoices() {
        const p = Cae.Colours.palette;
        return [
            { name: "Accent", hex: hexOf(p.m3primary), note: "Caelestia's accent colour" },
            { name: "Accent soft", hex: hexOf(p.m3secondary), note: "the scheme's secondary colour" },
            { name: "Accent tertiary", hex: hexOf(p.m3tertiary), note: "the scheme's tertiary colour" },
            { name: "White", hex: "ffffff" }, { name: "Grey", hex: "9e9e9e" }, { name: "Red", hex: "f38ba8" },
            { name: "Orange", hex: "fab387" }, { name: "Yellow", hex: "f9e2af" }, { name: "Green", hex: "a6e3a1" },
            { name: "Teal", hex: "94e2d5" }, { name: "Blue", hex: "89b4fa" }, { name: "Purple", hex: "cba6f7" },
            { name: "Pink", hex: "f5c2e7" }
        ];
    }

    function borderLauncherItems(query, pluginId) {
        const q = (query || "").toString().trim().toLowerCase().replace(/^#/, "");
        const current = (Cae.HyprConf.opt("general:col.active_border", "") || "").toLowerCase();
        let list = borderChoices().filter(c => !q || c.name.toLowerCase().includes(q) || c.hex.startsWith(q));
        if (/^[0-9a-f]{6}$/.test(q))
            list = [{ name: "This colour", hex: q }].concat(list);
        return list.map(c => ({
            name: c.name + (current.indexOf(c.hex) >= 0 ? "  \u2713" : ""),
            icon: "material:circle",
            comment: "#" + c.hex + (c.note ? " - " + c.note : ""),
            action: "border:" + c.hex,
            keywords: [c.hex, "border", "colour", "color"],
            isBuiltInLauncher: true,
            builtInPluginId: pluginId
        }));
    }

    function setBorderColour(hex) {
        if (!/^[0-9a-fA-F]{6}$/.test(hex))
            return false;
        const value = "rgb(" + hex.toLowerCase() + ")";
        Cae.HyprConf.setOption("general:col.active_border", value);
        Quickshell.execDetached([(Quickshell.env("CAELESTIA_PACKAGE_DIR") || Quickshell.shellPath("..")).replace(/\\/g, "/") + "/qs/hyprctl.exe", "keyword", "general:col.active_border", value]);
        return true;
    }

    // ---------------- >wallpaper: Caelestia's wallpapers from any launcher (every shell follows Caelestia's) ----------------
    function wallpaperLauncherItems(query, pluginId) {
        const q = (query || "").toString().trim();
        const items = [{ name: "Random wallpaper", icon: "material:shuffle", comment: "a random one from your wallpaper folder", action: "wallpaper:__random__", keywords: ["random", "shuffle"], isBuiltInLauncher: true, builtInPluginId: pluginId }];
        const found = Cae.Wallpapers.query(q).slice(0, 60);
        const current = String(Cae.Wallpapers.actualCurrent).split("\\").join("/");
        for (const e of found) {
            const path = String(e.path);
            const name = path.split(/[\\/]/).pop();
            items.push({
                name: name + (path.split("\\").join("/") === current ? "  \u2713" : ""),
                icon: "material:wallpaper",
                comment: String(e.relativePath || path),
                action: "wallpaper:" + path,
                keywords: [name],
                isBuiltInLauncher: true,
                builtInPluginId: pluginId
            });
        }
        return items;
    }

    // ">border" / ">wallpaper" with nothing after them: the launcher closes and Aether's picker opens in its place
    property real _pickerAt: 0
    function openPicker(mode) {
        if (Date.now() - _pickerAt < 1000)
            return;
        _pickerAt = Date.now();
        Qt.callLater(() => {
            PopoutService.closeDankLauncherV2();
            Cae.WinSettings.ipc("aetherpicker", "open", [mode]);
        });
    }

    function setWallpaperFromLauncher(path) {
        if (path === "__random__")
            Cae.Wallpapers.setRandom();
        else if (path)
            Cae.Wallpapers.setWallpaper(path);
        return true;
    }
}
