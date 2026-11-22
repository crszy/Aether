pragma Singleton
import QtQuick
import Quickshell
import "../services" as Services

QtObject {
    id: iconsFetcher

    function getIconCandidates(appName, desktopEntry, iconName) {
        let raw = (iconName || "").trim();
        let desktop = (desktopEntry || "").replace(/\.desktop$/i, "").trim();
        let app = (appName || "").trim();

        let candidates = [];
        if (/^[A-Za-z]:[\\/]/.test(raw)) // Windows: an absolute icon file
            return ["file:///" + raw.split("\\").join("/")];

        // Direct file path if specified
        if (raw.startsWith("/") || raw.startsWith("file:///")) {
            candidates.push(raw.startsWith("file:///") ? raw : "file:///" + raw);
        }

        // An already-resolved "image://icon/<name>" comes back through here
        // when a notification is re-rendered; only the name is a token, or
        // every file probe below is built on a URL and fails.
        if (raw.startsWith("image://icon/")) raw = raw.substring("image://icon/".length);

        let rawTokens = [];
        if (raw !== "") {
            rawTokens.push(raw);
            if (raw.includes("/")) {
                let parts = raw.split("/");
                rawTokens.push(parts[parts.length - 1]);
            }
        }

        if (desktop !== "") {
            rawTokens.push(desktop);
            if (desktop.includes(".")) {
                let parts = desktop.split(".");
                rawTokens.push(parts[parts.length - 1]);
            }
        }

        if (app !== "") {
            let appSlug = app.toLowerCase().replace(/\s+/g, '-');
            rawTokens.push(appSlug);
            rawTokens.push(app.toLowerCase());
        }

        let cleanTokens = [];
        for (let entry of rawTokens) {
            if (!entry || entry === "") continue;
            if (entry.startsWith("/") || entry.startsWith("file:///")) continue;
            let base = entry.replace(/\.(png|svg|xpm|jpg|jpeg)$/i, "").trim();
            if (base && base !== "") cleanTokens.push(base);
        }

        let uniqueTokens = cleanTokens.filter((v, i, a) => v && v !== "" && a.indexOf(v) === i);

        // Prioritized token expansion for battery, status, and symbolic icon formats
        let expandedTokens = [];
        for (let token of uniqueTokens) {
            // Normalize battery level tokens first (e.g. battery-level-100-symbolic -> battery-100)
            let batMatch = token.match(/^battery(?:-level)?-(\d+)(?:-(charging))?(?:-(symbolic))?$/i);
            if (batMatch) {
                let lvl = parseInt(batMatch[1]);
                let numStr = lvl.toString();
                let pad3 = numStr.padStart(3, '0');
                let isChg = !!batMatch[2];
                let chgSuffix = isChg ? "-charging" : "";

                expandedTokens.push("battery-" + numStr + chgSuffix);
                expandedTokens.push("battery-" + pad3 + chgSuffix);
                expandedTokens.push("battery-" + numStr + chgSuffix + "-symbolic");
                expandedTokens.push("battery-" + pad3 + chgSuffix + "-symbolic");
                expandedTokens.push("battery-" + numStr);
                expandedTokens.push("battery-" + pad3);
                if (isChg) {
                    expandedTokens.push("battery-good-charging-symbolic");
                    expandedTokens.push("battery-good-charging");
                    expandedTokens.push("battery-charging");
                } else {
                    expandedTokens.push("battery-good-symbolic");
                    expandedTokens.push("battery-good");
                }
                expandedTokens.push("battery-symbolic");
                expandedTokens.push("battery");
            }

            expandedTokens.push(token);
            if (token.endsWith("-symbolic")) {
                expandedTokens.push(token.replace(/-symbolic$/i, ""));
            } else {
                expandedTokens.push(token + "-symbolic");
            }
        }

        let allTokens = expandedTokens.filter((v, i, a) => v && v !== "" && a.indexOf(v) === i);

        // 1. Native Qt Theme resolution (instant C++ check)
        for (let token of allTokens) {
            if (Quickshell.hasThemeIcon(token)) {
                candidates.push("image://icon/" + token);
            }
        }

        // 2. Pre-computed, distro-verified icon base paths and theme subpaths from Services.Variables
        // Only directories that exist: probing is done by an Image per
        // candidate, so every impossible path is a failed load and a warning.
        let iconDirs = Services.Variables.iconDirs || [];
        if (iconDirs.length === 0) {
            for (let base of (Services.Variables.iconBases || []))
                for (let sub of (Services.Variables.themeSubPaths || []))
                    iconDirs.push(base + sub);
        }

        let fileTokens = allTokens.slice(0, 3);
        for (let token of fileTokens) {
            for (let dir of iconDirs) {
                candidates.push("file:///" + dir + token + ".svg");
                candidates.push("file:///" + dir + token + ".png");
            }
        }

        // Fallback theme names
        for (let token of allTokens) {
            candidates.push("image://icon/" + token);
        }

        if (Quickshell.hasThemeIcon("dialog-information")) {
            candidates.push("image://icon/dialog-information");
        }
        candidates.push("image://icon/application-x-executable");

        return candidates.filter((v, i, a) => v && v !== "" && a.indexOf(v) === i);
    }

    function getValidIcon(appName, desktopEntry, iconName) {
        let raw = (iconName || "").trim();
        if (raw !== "" && !raw.startsWith("/") && !raw.startsWith("file:///")) {
            if (Quickshell.hasThemeIcon(raw)) {
                return "image://icon/" + raw;
            }
        }

        let candidates = getIconCandidates(appName, desktopEntry, iconName);
        
        for (let cand of candidates) {
            if (cand.startsWith("image://icon/")) {
                let name = cand.replace("image://icon/", "");
                if (Quickshell.hasThemeIcon(name)) {
                    return cand;
                }
            }
        }

        if (candidates.length > 0) return candidates[0];
        return "image://icon/application-x-executable";
    }

    function isMainApp(appId, name) {
        if (!appId && !name) return false;
        let id = (appId || "").toLowerCase();
        let disp = (name || "").toLowerCase();
        
        const hideKeywords = [
            "lutris1", "pinentry", "bulk-rename", "volman", "assistant", "designer", 
            "linguist", "qdbusviewer", "qv4l2", "qvidcap", "avahi", "system-config-", 
            "hplip", "cups", "xdg-desktop-portal", "server", "backend", "helper", 
            "engine", "service", "setup", "wizard", "daemon", "url handler", "handler",
            "prompter", "viewer", "mounter", "writer", "bssh", "bvnc", "byobu", "java-java",
            "electron", "beta", "cmake-gui", "wine", "zenity",
            
            "-settings", "settings-", "qt5ct", "qt6ct", "kvantum", "lstopo", 
            "polkit", "authentication", "kiod", "ksecretd", "jshell", "jconsole", 
            "xterm", "uxterm", "xwayland", "about", "xampp", "nonplasma", "openjdk"
        ];

        for (let kw of hideKeywords) {
            if (id.includes(kw) || disp.includes(kw)) return false;
        }

        const mainApps = [
            "firefox", "chrome", "chromium", "code", "thunar", "kitty", "obsidian", 
            "discord", "spotify", "telegram", "vlc", "mpv", "steam", "zed", "element", 
            "missioncenter", "lutris", "zen", "pavucontrol", "wine", "terminal", 
            "duolingo", "beekeeper", "brave", "htop", "goverlay", "dosbox", "chess",
            "freedownloadmanager"
        ];

        for (let app of mainApps) {
            if (id.includes(app) || disp.includes(app)) return true;
        }

        return disp.length > 4;
    }
}