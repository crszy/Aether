import QtQuick
import Quickshell
import Quickshell.Io
import "../Settings"

pragma Singleton

QtObject {
    id: root

    property string activeMode: "" // "", "launcher", "clipboard", "emoji"
    readonly property bool active: activeMode !== ""
    property string query: ""
    property int selectedIndex: 0
    property string selectedCategory: "All"
    property bool deleteFocused: false

    // --- LAUNCHER DATA ---
    readonly property string storagePath: PathSettings.stateDir + "/app_usage.json"
    readonly property string _legacyStoragePath: PathSettings.shellDir + "/app_usage.json"
    property var usageMap: ({})
    property var allAppsCache: []
    property var topAppsCache: []
    property var displayedApps: []

    // --- CLIPBOARD DATA ---
    property var rawClipHistory: []
    property var displayedClips: []

    // --- EMOJI DATA ---
    property var allEmojisCache: []
    property var displayedEmojis: []
    readonly property string jsonPath: (Quickshell.env("ZENITH_ROOT") || (((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/quickshell")) + "/assets/emojis.json"
    readonly property var emojiCategories: ["All", "Smileys", "People", "Animals", "Food", "Activities", "Travel", "Objects", "Symbols", "Flags"]

    // --- HELPER PROCESSES ---
    property var shellProc: Process { id: execProc }

    // Desktop entries connection
    property var appConn: Connections {
        target: DesktopEntries.applications
        function onValuesChanged() { root.rebuildAppCache(); }
        function onRowsInserted() { root.rebuildAppCache(); }
        function onModelReset() { root.rebuildAppCache(); }
    }

    // App usage loader
    property var loadUsageProc: Process {
        id: loadUsage
        // Falls back to the pre-stateDir location once, so launch counts survive the move.
        command: ["sh", "-c", "cat \"$1\" 2>/dev/null || cat \"$2\"", "_", root.storagePath, root._legacyStoragePath]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    root.usageMap = JSON.parse(text) || {};
                } catch(e) {
                    root.usageMap = {};
                }
                root.updateTopAppsCache();
                if (root.activeMode === "launcher") root.rebuildFiltered();
            }
        }
        onExited: (code) => {
            if (code !== 0 && root.activeMode === "launcher") root.rebuildFiltered();
        }
    }

    // App usage saver
    property var saveUsageProc: Process { id: saveUsage }

    // Cliphist loader
    property var loadClipProc: Process {
        id: loadClip
        command: ["sh", "-c", "cliphist list"]
        stdout: StdioCollector {
            onStreamFinished: {
                root.parseCliphistOutput(text);
            }
        }
    }

    // Cliphist deleter
    property var deleteClipProc: Process {
        id: deleteClip
        onExited: (exitCode) => {
            root.loadClipHistory();
        }
    }

    // Emoji loader
    property var loadEmojiProc: Process {
        id: loadEmoji
        command: ["cat", root.jsonPath]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    let parsed = JSON.parse(text) || [];
                    root.buildEmojiCache(parsed);
                } catch(e) {
                    root.allEmojisCache = [];
                    root.rebuildFiltered();
                }
            }
        }
    }

    property var diStartupTimer: Timer {
        id: diStartupTimer
        interval: 300
        running: true
        repeat: false
        onTriggered: loadUsage.running = true
    }

    Component.onCompleted: {
        rebuildAppCache();
    }

    // --- CONTROLS ---

    function open(mode) {
        let validMode = mode || "launcher";
        if (validMode !== "launcher" && validMode !== "clipboard" && validMode !== "emoji") {
            validMode = "launcher";
        }
        
        CenterState.close();
        QuickSettingsService.close();

        query = "";
        selectedIndex = 0;
        selectedCategory = "All";
        deleteFocused = false;
        activeMode = validMode;

        if (validMode === "launcher") {
            if (allAppsCache.length === 0) rebuildAppCache();
            else rebuildFiltered();
        } else if (validMode === "clipboard") {
            loadClipHistory();
        } else if (validMode === "emoji") {
            if (allEmojisCache.length === 0) {
                loadEmoji.running = false;
                loadEmoji.running = true;
            } else {
                rebuildFiltered();
            }
        }
    }

    function close() {
        activeMode = "";
        query = "";
        selectedIndex = 0;
        deleteFocused = false;
    }

    function toggle(mode) {
        if (activeMode === mode || (mode === "" && active)) {
            close();
        } else {
            open(mode);
        }
    }

    function setMode(mode) {
        if (activeMode === mode) return;
        query = "";
        selectedIndex = 0;
        deleteFocused = false;
        activeMode = mode;
        if (mode === "clipboard" && rawClipHistory.length === 0) {
            loadClipHistory();
        } else if (mode === "emoji" && allEmojisCache.length === 0) {
            loadEmoji.running = false;
            loadEmoji.running = true;
        } else {
            rebuildFiltered();
        }
    }

    function cycleMode() {
        if (activeMode === "launcher") setMode("clipboard");
        else if (activeMode === "clipboard") setMode("emoji");
        else setMode("launcher");
    }

    function setCategory(cat) {
        selectedCategory = cat;
        selectedIndex = 0;
        rebuildFiltered();
    }

    // --- REBUILD FILTERED RESULTS ---

    function rebuildFiltered() {
        selectedIndex = 0;
        if (activeMode === "launcher") rebuildLauncherFiltered();
        else if (activeMode === "clipboard") rebuildClipboardFiltered();
        else if (activeMode === "emoji") rebuildEmojiFiltered();
    }

    // --- LAUNCHER LOGIC ---

    function updateTopAppsCache() {
        let topApps = [];
        for (let i = 0; i < allAppsCache.length; i++) {
            let app = allAppsCache[i];
            let usage = usageMap[app.id] || 0;
            topApps.push({
                isMath: false,
                entry: app.entry,
                id: app.id,
                name: app.name,
                nameLower: app.nameLower,
                icon: app.icon,
                genericName: usage > 0 ? "Frequently Used" : (app.genericName || app.comment || "Application"),
                comment: app.comment,
                usage: usage
            });
        }
        topApps.sort((a, b) => {
            if (b.usage !== a.usage) return b.usage - a.usage;
            let an = a.nameLower;
            let bn = b.nameLower;
            return an < bn ? -1 : (an > bn ? 1 : 0);
        });
        topAppsCache = topApps.slice(0, 5);
    }

    function rebuildAppCache() {
        let rawApps = DesktopEntries.applications.values || [];
        let cache = [];

        for (let i = 0; i < rawApps.length; i++) {
            let entry = rawApps[i];
            if (!entry || !entry.name || entry.noDisplay) continue;

            let name = entry.name || "";
            let appId = entry.id || "";
            let genericName = entry.genericName || "";
            let comment = entry.comment || "";
            let categories = entry.categories || "";

            cache.push({
                entry: entry,
                id: appId,
                idLower: appId.toLowerCase(),
                name: name,
                nameLower: name.toLowerCase(),
                icon: entry.icon || "",
                genericName: genericName,
                comment: comment,
                searchKey: (name + " " + appId + " " + genericName + " " + comment + " " + categories).toLowerCase()
            });
        }

        allAppsCache = cache;
        updateTopAppsCache();
        if (activeMode === "launcher") rebuildLauncherFiltered();
    }

    function evalMath(expr) {
        if (!expr) return null;
        let clean = expr.trim();
        if (!/^[\d\s\+\-\*\/\%\(\)\.\^]+$/.test(clean)) return null;
        if (!/[\+\-\*\/\%\^]/.test(clean)) return null;
        if (/[\+\-\*\/\%\^\(]$/.test(clean)) return null;

        try {
            clean = clean.replace(/\^/g, '**');
            let openParen = (clean.match(/\(/g) || []).length;
            let closeParen = (clean.match(/\)/g) || []).length;
            if (openParen !== closeParen) return null;
            if (/[\+\-\*\/\%]{3,}/.test(clean)) return null;

            let val = Function('"use strict"; return (' + clean + ')')();
            if (typeof val === "number" && !isNaN(val) && isFinite(val)) {
                let formatted = Number.isInteger(val) ? val.toString() : val.toFixed(4).replace(/\.?0+$/, '');
                return formatted;
            }
        } catch(e) {}
        return null;
    }

    function rebuildLauncherFiltered() {
        let q = query.toLowerCase().trim();

        if (q === "") {
            displayedApps = topAppsCache;
            return;
        }

        let results = [];

        // Math calculation - only evaluate if query starts with a number or math operator
        if (/^[\d\(\.\-\+]/.test(q)) {
            let mathRes = evalMath(query);
            if (mathRes !== null) {
                results.push({
                    isMath: true,
                    result: mathRes,
                    name: query.trim() + " = " + mathRes,
                    genericName: "Calculation Result (Press Enter to copy)",
                    icon: "",
                    id: "math_calc",
                    score: 100000
                });
            }
        }

        // Filter pre-indexed apps
        for (let i = 0; i < allAppsCache.length; i++) {
            let app = allAppsCache[i];
            let nameLower = app.nameLower;
            let idLower = app.idLower;

            if (app.searchKey.includes(q)) {
                let usage = usageMap[app.id] || 0;
                let score = usage * 1000;

                if (nameLower === q) score += 5000;
                else if (nameLower.startsWith(q)) score += 2000;
                else if (nameLower.includes(q)) score += 1000;
                else if (idLower.includes(q)) score += 500;

                results.push({
                    isMath: false,
                    entry: app.entry,
                    id: app.id,
                    name: app.name,
                    nameLower: nameLower,
                    icon: app.icon,
                    genericName: app.genericName,
                    comment: app.comment,
                    score: score
                });
            }
        }

        results.sort((a, b) => {
            if (b.score !== a.score) return b.score - a.score;
            let an = a.nameLower;
            let bn = b.nameLower;
            return an < bn ? -1 : (an > bn ? 1 : 0);
        });

        // Terminal command fallback if no application matches
        if (results.length === 0) {
            let trimmed = query.trim();
            if (trimmed !== "") {
                results.push({
                    isTerminal: true,
                    queryText: trimmed,
                    name: "Run \"" + trimmed + "\" in terminal",
                    genericName: "Execute command in Kitty (Enter or click)",
                    icon: "utilities-terminal",
                    id: "terminal_run",
                    score: 90
                });
            }
        }

        displayedApps = results.slice(0, 7);
    }

    function runInTerminal(cmdText) {
        let cleanCmd = (cmdText || "").trim();
        if (!cleanCmd) return;
        execProc.running = false;
        execProc.command = ["sh", "-c", "kitty sh -c 'eval \"$1\"; exec ${SHELL:-fish}' _ \"$1\" &", "_", cleanCmd];
        execProc.running = true;
    }

    function saveAppUsage() {
        saveUsage.command = [
            "python3", "-c",
            "import json, os, sys; p=sys.argv[1]; d=sys.argv[2]; os.makedirs(os.path.dirname(p), exist_ok=True); f=open(p, 'w'); f.write(d); f.close()",
            storagePath,
            JSON.stringify(usageMap)
        ];
        saveUsage.running = true;
    }

    function recordAppUsage(appId) {
        if (!appId) return;
        let count = usageMap[appId] || 0;
        usageMap[appId] = count + 1;
        updateTopAppsCache();
        saveAppUsage();
    }

    function launchApp(appItem) {
        if (!appItem) return;
        
        if (appItem.isMath) {
            execProc.command = ["sh", "-c", "echo -n '" + appItem.result + "' | wl-copy 2>/dev/null || true"];
            execProc.running = true;
            close();
            return;
        }

        if (appItem.isTerminal) {
            runInTerminal(appItem.queryText || query);
            close();
            return;
        }

        if (appItem.entry) {
            recordAppUsage(appItem.id);
            appItem.entry.execute();
            close();
        }
    }

    // --- CLIPBOARD LOGIC ---

    function loadClipHistory() {
        loadClip.running = false;
        loadClip.running = true;
    }

    function parseCliphistOutput(text) {
        if (!text) {
            rawClipHistory = [];
            rebuildClipboardFiltered();
            return;
        }

        let lines = text.split("\n");
        let items = [];

        for (let i = 0; i < lines.length; i++) {
            let line = lines[i];
            if (!line.trim()) continue;

            let tabIdx = line.indexOf("\t");
            if (tabIdx !== -1) {
                let clipId = line.substring(0, tabIdx).trim();
                let clipText = line.substring(tabIdx + 1);

                items.push({
                    id: clipId,
                    preview: clipText,
                    rawLine: line,
                    searchKey: clipText.toLowerCase()
                });
            }
        }

        rawClipHistory = items;
        rebuildClipboardFiltered();
    }

    function rebuildClipboardFiltered() {
        let q = query.toLowerCase().trim();

        if (q === "") {
            displayedClips = rawClipHistory.slice(0, 15);
            return;
        }

        let results = [];
        for (let i = 0; i < rawClipHistory.length; i++) {
            let item = rawClipHistory[i];
            if (item.searchKey.includes(q)) {
                results.push(item);
            }
        }

        displayedClips = results.slice(0, 15);
    }

    function copyClipItem(clipItem) {
        if (!clipItem || !clipItem.id) return;
        execProc.command = ["sh", "-c", "cliphist decode " + clipItem.id + " | wl-copy"];
        execProc.running = true;
        close();
    }

    function deleteClipIndex(idx) {
        if (typeof idx !== "number" || idx < 0 || idx >= displayedClips.length) return;
        deleteClipItem(displayedClips[idx]);
    }

    function deleteClipItem(clipItem) {
        if (typeof clipItem === "number") {
            if (clipItem >= 0 && clipItem < displayedClips.length) {
                clipItem = displayedClips[clipItem];
            } else {
                return;
            }
        }
        if (!clipItem || !clipItem.id) return;

        let targetId = clipItem.id;
        let targetLine = clipItem.rawLine || (clipItem.id + "\t" + clipItem.preview);

        // 1. Instantly remove from in-memory arrays so the UI updates immediately
        rawClipHistory = rawClipHistory.filter(item => item && item.id !== targetId);
        rebuildClipboardFiltered();

        if (selectedIndex >= displayedClips.length) {
            selectedIndex = Math.max(0, displayedClips.length - 1);
        }
        deleteFocused = false;

        // 2. Safely execute cliphist delete on disk
        deleteClip.running = false;
        deleteClip.command = ["sh", "-c", "printf '%s\n' \"$1\" | cliphist delete", "_", targetLine];
        deleteClip.running = true;
    }

    // --- EMOJI LOGIC ---

    function buildEmojiCache(rawList) {
        let cache = [];
        for (let i = 0; i < rawList.length; i++) {
            let item = rawList[i];
            if (!item) continue;
            let emojiChar = item.char || item.emoji || "";
            if (!emojiChar) continue;
            let name = item.name || "";
            let category = item.cat || item.category || "All";
            let keywords = Array.isArray(item.tags || item.keywords) ? (item.tags || item.keywords).join(" ") : (item.tags || item.keywords || "");

            cache.push({
                emoji: emojiChar,
                name: name,
                category: category,
                keywords: keywords,
                searchKey: (name + " " + category + " " + keywords).toLowerCase()
            });
        }
        allEmojisCache = cache;
        rebuildEmojiFiltered();
    }

    function rebuildEmojiFiltered() {
        let q = query.toLowerCase().trim();
        let cat = selectedCategory;

        let results = [];
        for (let i = 0; i < allEmojisCache.length; i++) {
            let item = allEmojisCache[i];
            
            if (cat !== "All" && item.category !== cat) continue;

            if (q === "" || item.searchKey.includes(q)) {
                results.push(item);
            }
        }

        displayedEmojis = results.slice(0, 120);
    }

    function copyEmoji(emojiChar) {
        if (!emojiChar) return;
        let safeEmoji = emojiChar.replace(/'/g, "'\\''");
        execProc.command = ["sh", "-c", "printf '%s' '" + safeEmoji + "' | wl-copy"];
        execProc.running = true;
        close();
    }
}

