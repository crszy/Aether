pragma ComponentBehavior: Bound

// Your terminals in your colours: ~/.config/caelestia/user/terminals.json says which terminals Aether themes and
// lets you set any colour yourself - the text colour (foreground), background, cursor, selection, the 16 terminal
// colours - for all terminals or one of them. Written again whenever your colours (wallpaper) or that file change.
//   Alacritty         the colour tables of %APPDATA%\alacritty\alacritty.toml, between "# >>> aether colours" and
//                     "# <<< aether colours" (the rest of the file is never touched; a backup is kept the first time)
//   Windows Terminal  a scheme called "Aether" in its settings.json, made the default scheme
//   anything else     "templates": your own file with {{foreground}}, {{term4}}, {{m3primary}} ... filled in
// A colour value: "" = from your scheme, "#rrggbb" = that colour, or a scheme colour name ("primary", "term4" ...).
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as Win
import qs.services
import qs.utils

Scope {
    id: root

    readonly property string appData: (Quickshell.env("APPDATA") || "").split(String.fromCharCode(92)).join("/")
    readonly property string localAppData: (Quickshell.env("LOCALAPPDATA") || "").split(String.fromCharCode(92)).join("/")
    readonly property string alacrittyPath: `${appData}/alacritty/alacritty.toml`
    readonly property string wtPath: `${localAppData}/Packages/Microsoft.WindowsTerminal_8wekyb3d8bbwe/LocalState/settings.json`

    property var cfg: ({})

    // the colour names a terminal has, and where each comes from when you don't set it
    readonly property var defaults: ({
        foreground: "onSurface", background: "surface", cursor: "primary", cursorText: "onPrimary",
        selectionText: "onPrimaryContainer", selectionBackground: "primaryContainer",
        black: "term0", red: "term1", green: "term2", yellow: "term3", blue: "term4", magenta: "term5", cyan: "term6", white: "term7",
        brightBlack: "term8", brightRed: "term9", brightGreen: "term10", brightYellow: "term11", brightBlue: "term12",
        brightMagenta: "term13", brightCyan: "term14", brightWhite: "term15"
    })

    function hex(c: color): string {
        const h = v => Math.round(v * 255).toString(16).padStart(2, "0");
        return `#${h(c.r)}${h(c.g)}${h(c.b)}`;
    }

    // "#abc123" | "primary" | "m3primary" | "term4" -> "#rrggbb" (or "" when unknown)
    function resolve(value: var): string {
        const v = String(value ?? "").trim();
        if (/^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/.test(v))
            return v.slice(0, 7).toLowerCase();
        const p = Colours.palette;
        const name = v.startsWith("m3") || v.startsWith("term") ? v : `m3${v}`;
        return p && p[name] !== undefined ? root.hex(p[name]) : "";
    }

    // every colour for one terminal: its own overrides, then the global ones, then your scheme
    function coloursFor(term: string): var {
        const own = root.cfg[`${term}Colours`] ?? {};
        const all = root.cfg.colours ?? {};
        const out = {};
        for (const k of Object.keys(root.defaults))
            out[k] = root.resolve(own[k]) || root.resolve(all[k]) || root.resolve(root.defaults[k]);
        return out;
    }

    // ---------------- Alacritty ----------------
    function alacrittyBlock(c: var): string {
        const q = k => `"${c[k]}"`;
        return ["# >>> aether colours - written by Aether from your scheme + user/terminals.json (edit those, not this)",
            "[colors.primary]", `foreground = ${q("foreground")}`, `background = ${q("background")}`, "",
            "[colors.cursor]", `text = ${q("cursorText")}`, `cursor = ${q("cursor")}`, "",
            "[colors.selection]", `text = ${q("selectionText")}`, `background = ${q("selectionBackground")}`, "",
            "[colors.normal]", `black   = ${q("black")}`, `red     = ${q("red")}`, `green   = ${q("green")}`, `yellow  = ${q("yellow")}`,
            `blue    = ${q("blue")}`, `magenta = ${q("magenta")}`, `cyan    = ${q("cyan")}`, `white   = ${q("white")}`, "",
            "[colors.bright]", `black   = ${q("brightBlack")}`, `red     = ${q("brightRed")}`, `green   = ${q("brightGreen")}`, `yellow  = ${q("brightYellow")}`,
            `blue    = ${q("brightBlue")}`, `magenta = ${q("brightMagenta")}`, `cyan    = ${q("brightCyan")}`, `white   = ${q("brightWhite")}`,
            "# <<< aether colours"].join("\n");
    }

    function writeAlacritty(): void {
        if (root.cfg.alacritty === false || Win.FsUtils.classify(root.alacrittyPath) !== "file")
            return;
        const old = alacrittyFile.text();
        if (!old)
            return;
        const block = root.alacrittyBlock(root.coloursFor("alacritty"));
        let next;
        const a = old.indexOf("# >>> aether colours"), b = old.indexOf("# <<< aether colours");
        if (a >= 0 && b > a) {
            next = old.slice(0, a) + block + old.slice(b + "# <<< aether colours".length);
        } else {
            // first time: the file's own [colors.*] tables become the managed block (a backup is kept)
            const lines = old.split("\n");
            const out = [];
            let inColours = false, placed = false;
            let trailing = [];   // comments / blank lines after the last colour line: they belong to what follows
            for (const line of lines) {
                const table = /^\s*\[([^\]]+)\]\s*$/.exec(line);
                if (table) {
                    const wasColours = inColours;
                    inColours = table[1].trim().startsWith("colors");
                    if (inColours && !placed) {
                        out.push(block);
                        placed = true;
                    }
                    if (wasColours && !inColours)
                        out.push(...trailing);
                    trailing = [];
                }
                if (!inColours)
                    out.push(line);
                else if (!table)
                    trailing = /^\s*(#.*)?$/.test(line) ? trailing.concat([line]) : [];
            }
            if (inColours)
                out.push(...trailing);
            if (!placed)
                out.push("", block);
            next = out.join("\n");
            if (Win.FsUtils.classify(`${root.alacrittyPath}.before-aether-colours`) !== "file")
                alacrittyBackup.setText(old);
        }
        if (next !== old)
            alacrittyFile.setText(next);
    }

    // ---------------- Windows Terminal ----------------
    // settings.json may have // and /* */ comments (outside strings)
    function stripJsonc(s: string): string {
        let out = "", i = 0, inStr = false;
        while (i < s.length) {
            const ch = s[i], nx = s[i + 1];
            if (inStr) {
                out += ch;
                if (ch === "\\") {
                    out += nx ?? "";
                    i += 2;
                    continue;
                }
                if (ch === "\"")
                    inStr = false;
                i++;
            } else if (ch === "\"") {
                inStr = true;
                out += ch;
                i++;
            } else if (ch === "/" && nx === "/") {
                while (i < s.length && s[i] !== "\n")
                    i++;
            } else if (ch === "/" && nx === "*") {
                i += 2;
                while (i < s.length && !(s[i] === "*" && s[i + 1] === "/"))
                    i++;
                i += 2;
            } else {
                out += ch;
                i++;
            }
        }
        return out.replace(/,(\s*[}\]])/g, "$1");   // trailing commas
    }

    function writeWindowsTerminal(): void {
        if (root.cfg.windowsTerminal === false || Win.FsUtils.classify(root.wtPath) !== "file")
            return;
        const old = wtFile.text();
        if (!old)
            return;
        let j;
        try {
            j = JSON.parse(root.stripJsonc(old));
        } catch (e) {
            console.warn("[terminals] Windows Terminal settings.json could not be read:", e);
            return;
        }
        const c = root.coloursFor("windowsTerminal");
        const scheme = { name: "Aether", foreground: c.foreground, background: c.background, cursorColor: c.cursor,
            selectionBackground: c.selectionBackground, black: c.black, red: c.red, green: c.green, yellow: c.yellow,
            blue: c.blue, purple: c.magenta, cyan: c.cyan, white: c.white, brightBlack: c.brightBlack, brightRed: c.brightRed,
            brightGreen: c.brightGreen, brightYellow: c.brightYellow, brightBlue: c.brightBlue, brightPurple: c.brightMagenta,
            brightCyan: c.brightCyan, brightWhite: c.brightWhite };
        j.schemes = (j.schemes ?? []).filter(s => s.name !== "Aether").concat([scheme]);
        if (root.cfg.windowsTerminalDefault !== false) {
            j.profiles = j.profiles ?? {};
            if (Array.isArray(j.profiles))
                j.profiles = { defaults: {}, list: j.profiles };
            j.profiles.defaults = j.profiles.defaults ?? {};
            j.profiles.defaults.colorScheme = "Aether";
        }
        const next = JSON.stringify(j, null, 4) + "\n";
        if (next === old)
            return;
        if (Win.FsUtils.classify(`${root.wtPath}.before-aether-colours`) !== "file")
            wtBackup.setText(old);
        wtFile.setText(next);
    }

    // ---------------- Zen browser ----------------
    // "zen": true -> chrome/aether-colours.css in Zen's default profile (installs.ini), imported by the first line of
    // chrome/userChrome.css (your own userChrome / Sine mods are kept; a backup is made the first time). Zen reads it
    // when it starts: restart Zen to see a change. Needs toolkit.legacyUserProfileCustomizations.stylesheets (set
    // through user.js when missing). Colours: "zenColours" in terminals.json, names below.
    readonly property var zenDefaults: ({
        accent: "primary", accentContainer: "primaryContainer", onAccentContainer: "onPrimaryContainer", secondary: "secondaryContainer",
        background: "surface", surface: "surfaceContainer", surfaceHigh: "surfaceContainerHigh", surfaceHighest: "surfaceContainerHighest",
        text: "onSurface", border: "outlineVariant"
    })
    readonly property string zenProfile: {
        const ini = String(zenInstalls.text() ?? "");
        const m = /^Default=(.+)$/m.exec(ini);
        return m ? `${root.appData}/zen/${m[1].trim()}` : "";
    }
    function zenColours(): var {
        const own = root.cfg.zenColours ?? {};
        const out = {};
        for (const k of Object.keys(root.zenDefaults))
            out[k] = root.resolve(own[k]) || root.resolve(root.zenDefaults[k]);
        return out;
    }
    function zenCss(c: var): string {
        return ["/* Written by Aether from your colour scheme + user/terminals.json (zenColours) - edit those, not this. */",
            ":root, #main-window {",
            `  --zen-primary-color: ${c.accent} !important;`,
            `  --zen-colors-primary: ${c.accentContainer} !important;`,
            `  --zen-colors-primary-foreground: ${c.onAccentContainer} !important;`,
            `  --zen-colors-secondary: ${c.secondary} !important;`,
            `  --zen-colors-tertiary: ${c.surfaceHigh} !important;`,
            `  --zen-colors-border: ${c.border} !important;`,
            `  --zen-colors-border-contrast: ${c.border} !important;`,
            `  --zen-colors-hover-bg: ${c.surfaceHighest} !important;`,
            `  --zen-colors-input-bg: ${c.surface} !important;`,
            `  --zen-main-browser-background: ${c.background} !important;`,
            `  --zen-main-browser-background-toolbar: ${c.background} !important;`,
            `  --zen-themed-toolbar-bg-transparent: ${c.background} !important;`,
            `  --zen-dialog-background: ${c.surface} !important;`,
            `  --zen-urlbar-background: ${c.surfaceHigh} !important;`,
            `  --toolbar-bgcolor: ${c.background} !important;`,
            `  --toolbar-color: ${c.text} !important;`,
            `  --toolbar-field-background-color: ${c.surfaceHigh} !important;`,
            `  --toolbar-field-color: ${c.text} !important;`,
            `  --tab-selected-bgcolor: ${c.surfaceHighest} !important;`,
            `  --tab-selected-textcolor: ${c.text} !important;`,
            `  --arrowpanel-background: ${c.surface} !important;`,
            `  --arrowpanel-color: ${c.text} !important;`,
            `  --focus-outline-color: ${c.accent} !important;`,
            "}", ""].join("\n");
    }
    readonly property string zenImport: "@import url(\"aether-colours.css\"); /* Aether colours - remove this line to stop */"
    function writeZen(): void {
        if (!root.zenProfile || Win.FsUtils.classify(root.zenProfile) !== "dir")
            return;
        if (root.cfg.zen !== true) {   // off: take Aether's colours out again (only what Aether added)
            const cur = Win.FsUtils.classify(zenChromeFile.path) === "file" ? String(zenChromeFile.text() ?? "") : "";
            if (cur.includes(root.zenImport)) {
                const rest = cur.split(root.zenImport + "\n").join("").split(root.zenImport).join("");
                if (rest.trim())
                    zenChromeFile.setText(rest);
                else
                    Win.FsUtils.removeFile(zenChromeFile.path);
            }
            if (Win.FsUtils.classify(zenCssFile.path) === "file")
                Win.FsUtils.removeFile(zenCssFile.path);
            return;
        }
        Win.FsUtils.makePath(`${root.zenProfile}/chrome`);
        const css = root.zenCss(root.zenColours());
        if (zenCssFile.text() !== css)
            zenCssFile.setText(css);
        // userChrome.css: the import must come before any other rule
        const old = Win.FsUtils.classify(zenChromeFile.path) === "file" ? String(zenChromeFile.text() ?? "") : "";
        if (!old.includes("aether-colours.css")) {
            if (old && Win.FsUtils.classify(`${zenChromeFile.path}.before-aether-colours`) !== "file")
                zenChromeBackup.setText(old);
            zenChromeFile.setText(root.zenImport + "\n" + old);
        }
        // custom stylesheets must be allowed (Zen's own default is on; keep it on through user.js)
        const prefs = String(zenPrefs.text() ?? "");
        if (!/legacyUserProfileCustomizations\.stylesheets",\s*true/.test(prefs)) {
            const uj = Win.FsUtils.classify(zenUserJs.path) === "file" ? String(zenUserJs.text() ?? "") : "";
            if (!uj.includes("legacyUserProfileCustomizations"))
                zenUserJs.setText(uj + (uj && !uj.endsWith("\n") ? "\n" : "") + "user_pref(\"toolkit.legacyUserProfileCustomizations.stylesheets\", true); // Aether colours\n");
        }
    }

    FileView {
        id: zenInstalls

        path: `${root.appData}/zen/installs.ini`
        printErrors: false
        blockLoading: true
    }
    FileView {
        id: zenCssFile

        path: root.zenProfile ? `${root.zenProfile}/chrome/aether-colours.css` : ""
        printErrors: false
        blockLoading: true
    }
    FileView {
        id: zenChromeFile

        path: root.zenProfile ? `${root.zenProfile}/chrome/userChrome.css` : ""
        printErrors: false
        blockLoading: true
    }
    FileView {
        id: zenChromeBackup

        path: root.zenProfile ? `${root.zenProfile}/chrome/userChrome.css.before-aether-colours` : ""
        printErrors: false
    }
    FileView {
        id: zenPrefs

        path: root.zenProfile ? `${root.zenProfile}/prefs.js` : ""
        printErrors: false
        blockLoading: true
    }
    FileView {
        id: zenUserJs

        path: root.zenProfile ? `${root.zenProfile}/user.js` : ""
        printErrors: false
        blockLoading: true
    }

    // ---------------- your own templates (any other terminal or app) ----------------
    function expandPath(p: string): string {
        return String(p ?? "").replace(/^~/, Paths.home).replace(/%APPDATA%/gi, root.appData).replace(/%LOCALAPPDATA%/gi, root.localAppData)
            .split(String.fromCharCode(92)).join("/");
    }

    function fill(template: string): string {
        const c = root.coloursFor("templates");
        return template.replace(/\{\{\s*([A-Za-z0-9]+)\s*\}\}/g, (m, name) => c[name] || root.resolve(name) || m);
    }

    property int templateRev: 0

    function writeAll(): void {
        root.writeAlacritty();
        root.writeWindowsTerminal();
        root.writeZen();
        root.templateRev++;
    }

    // what the terminals depend on: rewritten only when one of these colours really changed
    readonly property string signature: {
        const p = Colours.palette;
        if (!p)
            return "";
        const names = ["m3onSurface", "m3surface", "m3primary", "m3onPrimary", "m3onPrimaryContainer", "m3primaryContainer",
            "m3secondaryContainer", "m3surfaceContainer", "m3surfaceContainerHigh", "m3surfaceContainerHighest", "m3outlineVariant"];
        for (let i = 0; i < 16; i++)
            names.push(`term${i}`);
        return names.map(n => String(p[n])).join(",") + JSON.stringify(root.cfg);
    }
    onSignatureChanged: debounce.restart()

    Timer {
        id: debounce

        interval: 400
        onTriggered: root.writeAll()
    }

    FileView {
        path: `${UserExt.dir}/terminals.json`
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            try {
                root.cfg = JSON.parse(text()) ?? {};
            } catch (e) {
                console.warn("[terminals] terminals.json:", e);
            }
        }
        onLoadFailed: root.cfg = { alacritty: false, windowsTerminal: false }   // no file: leave terminals alone
    }

    FileView {
        id: alacrittyFile

        path: root.alacrittyPath
        printErrors: false
        blockLoading: true
        watchChanges: true
        onFileChanged: reload()
    }

    FileView {
        id: alacrittyBackup

        path: `${root.alacrittyPath}.before-aether-colours`
        printErrors: false
    }

    FileView {
        id: wtFile

        path: root.wtPath
        printErrors: false
        blockLoading: true
        watchChanges: true
        onFileChanged: reload()
    }

    FileView {
        id: wtBackup

        path: `${root.wtPath}.before-aether-colours`
        printErrors: false
    }

    Variants {
        model: (root.cfg.templates ?? []).filter(t => t && t.template && t.output)

        Scope {
            id: tpl

            required property var modelData

            FileView {
                id: src

                path: root.expandPath(tpl.modelData.template)
                watchChanges: true
                printErrors: false
                onFileChanged: reload()
                onLoaded: dst.setText(root.fill(text()))
            }

            FileView {
                id: dst

                path: root.expandPath(tpl.modelData.output)
                printErrors: false
            }

            Connections {
                target: root

                function onTemplateRevChanged(): void {
                    if (src.text())
                        dst.setText(root.fill(src.text()));
                }
            }
        }
    }
}
