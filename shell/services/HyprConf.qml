pragma Singleton
pragma ComponentBehavior: Bound

// Hyprland for Windows: reads ~/.config/hypr/hyprland.conf (Hyprland's own syntax) and runs Windows like Hyprland.
//   general / decoration / dwindle / master / scrolling / binds / cursor  -> the port's tiler and window decorations
//   bind, binde, bindr, bindl, bindd ...                                   -> real global keys (written into keybinds.json,
//                                                                            tagged "from": "hyprland", nothing else touched)
//   exec-once (once per session) and exec (every reload), windowrule float -> run / never tile
//   source = other.conf, $variables, nested { } sections, key:sub = value  -> like Hyprland
// Lines that make no sense on Windows (monitor, env, blur, shadows, input devices ...) are kept as "notes" for Settings.
// Nothing happens to your windows until "Hyprland mode" is on (WinSettings.hyprland). layout = canvas runs InfiniteDesk.
// IPC: qs ipc call hyprctl dispatch|reload|keyword|getoption|clients|workspaces|activewindow|monitors|binds|notes|version
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import Quickshell.Windows as Win
import "niri.js" as Niri

Singleton {
    id: root

    readonly property string home: (Quickshell.env("USERPROFILE") || "").replace(/\\/g, "/")
    readonly property string dir: `${home}/.config/hypr`
    readonly property string path: `${dir}/hyprland.conf`
    readonly property string luaPath: `${dir}/hyprland.lua`
    readonly property string niriPath: `${home}/.config/niri/config.kdl`
    property bool usingNiri: false
    property string dragMod: ""       // bindm modifier: "alt", "super" or "" (no mod + drag)
    readonly property string keybindsPath: `${(Quickshell.env("LOCALAPPDATA") || "").replace(/\\/g, "/")}/caelestia/keybinds.json`
    readonly property string canvasExe: (Quickshell.env("CAELESTIA_PACKAGE_DIR") || Quickshell.shellPath("..")).replace(/\\/g, "/") + "/infinitedesk/InfiniteDesk.exe"

    readonly property bool enabled: WinSettings.hyprland
    property var options: ({})        // "general:gaps_in" -> "5"
    property var runtime: ({})        // hyprctl keyword overrides (until the next reload)
    property var binds: []            // { mods, key, keys, dispatcher, args, flags, description, line, file }
    property var execOnce: []
    property var execAlways: []
    property var rules: []            // raw windowrule lines
    property var floating: []         // patterns for Win.Tiling.excluded
    property var ruleList: []         // { match: {class, title, ...}, effects: {float, size, move, workspace ...} }
    property var notes: []            // { line, file, text, why }
    property var variables: ({})
    property var files: []
    property bool loaded: false
    property bool ranOnce: false
    property int reloads: 0
    property string error: ""

    readonly property string layout: (opt("general:layout", "dwindle") || "dwindle").toLowerCase()

    signal reloaded

    function opt(key: string, fallback: var): var {
        if (runtime[key] !== undefined) return runtime[key];
        const v = options[key];
        return v === undefined ? fallback : v;
    }
    function optInt(key: string, fallback: int): int {
        const n = parseInt(String(opt(key, fallback)).trim().split(/[\s,]+/)[0]);
        return isNaN(n) ? fallback : n;
    }
    function optReal(key: string, fallback: real): real {
        const n = parseFloat(String(opt(key, fallback)));
        return isNaN(n) ? fallback : n;
    }
    function optBool(key: string, fallback: bool): bool {
        const v = String(opt(key, fallback)).trim().toLowerCase();
        if (["1", "true", "yes", "on"].includes(v)) return true;
        if (["0", "false", "no", "off"].includes(v)) return false;
        return fallback;
    }

    // rgba(33ccffee) rgb(33ccff) 0xee33ccff, gradients "a b 45deg" -> first colour
    function colour(value: var, fallback: color): color {
        const s = String(value ?? "").trim();
        if (!s) return fallback;
        let m = s.match(/rgba\(\s*([0-9a-fA-F]{8})\s*\)/);
        if (m) return Qt.color(`#${m[1].slice(6, 8)}${m[1].slice(0, 6)}`);
        m = s.match(/rgba\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([\d.]+)\s*\)/);
        if (m) return Qt.rgba(m[1] / 255, m[2] / 255, m[3] / 255, parseFloat(m[4]));
        m = s.match(/rgb\(\s*([0-9a-fA-F]{6})\s*\)/);
        if (m) return Qt.color(`#${m[1]}`);
        m = s.match(/rgb\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)/);
        if (m) return Qt.rgba(m[1] / 255, m[2] / 255, m[3] / 255, 1);
        m = s.match(/0x([0-9a-fA-F]{8})/);
        if (m) return Qt.color(`#${m[1]}`);
        m = s.match(/#([0-9a-fA-F]{6,8})/);
        if (m) return Qt.color(`#${m[1]}`);
        return fallback;
    }

    // ------------------------------------------------------------ parsing
    readonly property Component reader: Component {
        FileView {
            blockLoading: true
            printErrors: false
        }
    }

    // null when the file does not exist (untyped return: a ": string" function would turn null into "null")
    function readFile(p: string): var {
        // the two files we also watch are read through their own FileView (a second view on the same path reads nothing)
        const own = p === path ? confFile : p === keybindsPath ? keybindsFile : p === luaPath ? luaFile : null;
        if (own) {
            own.reload();
            let t = null;
            try { t = own.text(); if (!own.loaded) t = null; } catch (e) { t = null; }
            return t;
        }
        const f = reader.createObject(null, { path: p });
        let t = null;
        // text() is what makes a blocking FileView load, so ask for it first, then see whether it loaded
        try { t = f.text(); if (!f.loaded) t = null; } catch (e) { t = null; }
        f.destroy();
        return t;
    }

    function expandPath(p: string, base: string): string {
        p = p.trim().replace(/^["']|["']$/g, "").replace(/\\/g, "/");
        if (p.startsWith("~")) p = home + p.slice(1);
        else if (p.startsWith("$HOME")) p = home + p.slice(5);
        else if (!/^[A-Za-z]:\//.test(p) && !p.startsWith("/")) p = `${base}/${p}`;
        return p;
    }

    function substitute(v: string, vars: var): string {
        const names = Object.keys(vars).sort((a, b) => b.length - a.length);
        for (const n of names)
            v = v.split(`$${n}`).join(vars[n]);
        return v;
    }

    function stripComment(line: string): string {
        let out = "";
        for (let i = 0; i < line.length; i++) {
            if (line[i] === "#") {
                if (line[i + 1] === "#") { out += "#"; i++; continue; }
                break;
            }
            out += line[i];
        }
        return out;
    }

    // ------------------------------------------------------------ hyprland.lua (Hyprland's current config format)
    // hyprlua.exe (Lua 5.5.1 + a stand-in `hl` table) runs the config and prints what it set as JSON.
    readonly property string hyprluaExe: (Quickshell.env("CAELESTIA_PACKAGE_DIR") || Quickshell.shellPath("..")).replace(/\\/g, "/") + "/qs/hyprlua.exe"
    property bool usingLua: false

    readonly property Process luaProc: Process {
        stdout: StdioCollector {
            id: luaOut
        }
        stderr: StdioCollector {
            id: luaErr
        }
        onExited: (code, status) => {
            let d = null;
            try { d = JSON.parse(luaOut.text); } catch (e) { d = null; }
            if (!d) {
                root.error = `hyprland.lua could not be read: ${(luaErr.text || luaOut.text || "hyprlua exited " + code).trim().slice(0, 300)}`;
                root.loaded = false;
            } else {
                root.fromLua(d);
            }
            root.apply();
            root.reloaded();
        }
    }

    function luaBind(b: var): var {
        const parts = String(b.keys).split("+").map(x => x.trim()).filter(x => x.length);
        const key = parts.pop() ?? "";
        const opts = b.opts ?? {};
        const flags = (opts.repeating ? "e" : "") + (opts.release ? "r" : "") + (opts.mouse ? "m" : "");
        if (b.luaFunction) return { problem: "this bind runs a Lua function - not in the Windows port yet" };
        if (b.dsp === "hl.dsp.exit") return { problem: "exit would sign you out of Windows - not bound" };
        const r = parseBind(flags, `${parts.join(" ")}, ${key}, lua, x`, b.submap ?? "");
        if (r.problem) return r;
        r.description = opts.description ?? "";
        if (b.dsp === "hl.dsp.exec_cmd" || b.dsp === "hl.dsp.exec_raw") {
            r.dispatcher = "exec";
            r.args = String((b.argv ?? [])[0] ?? "");
        } else {
            r.dispatcher = "lua";
            r.args = b.call ?? "";
        }
        return r;
    }

    function luaFloatPatterns(list: var): var {
        const out = [];
        for (const r of list) {
            if (r.enabled === false || !(r.float === true || r.float === "on" || r.float === 1)) continue;
            const m = r.match ?? {};
            for (const kind of ["class", "title", "initial_class", "initial_title"]) {
                if (typeof m[kind] !== "string" || !m[kind] || m[kind] === "^$") continue;
                const rule = kind.includes("title") ? `title ${m[kind]}` : `class ${m[kind]}`;
                for (const p of floatPatterns([`float, ${rule}`])) out.push(p);
            }
        }
        return out;
    }

    function fromLua(d: var): void {
        const where = { line: 0, file: luaPath };
        const st = { options: {}, binds: [], execOnce: d.execOnce ?? [], execAlways: [], rules: [], notes: [], vars: {}, files: [luaPath] };
        for (const k of Object.keys(d.options ?? {})) {
            st.options[k] = d.options[k];
            if (!known.includes(k)) st.notes.push(Object.assign({ text: `${k} = ${d.options[k]}`, why: "read, but Windows has no use for it yet" }, where));
        }
        let luaDrag = "";
        for (const b of d.binds ?? []) {
            if (b.opts?.mouse) {
                const ks = String(b.keys).split("+").map(x => x.trim()).filter(x => x);
                const k = ks.pop() ?? "";
                if (/^mouse:27[23]$/.test(k)) luaDrag = dragModOf(ks.join(" "));
                continue;
            }
            if (b.enabled === false) continue;
            const r = luaBind(b);
            if (r.problem) st.notes.push(Object.assign({ text: `hl.bind("${b.keys}", ${b.call ?? "function"})`, why: r.problem }, where));
            else st.binds.push(Object.assign(r, where));
        }
        for (const n of d.notes ?? []) st.notes.push(Object.assign({}, n, where));
        // hl.curve / hl.animation -> the same keys hyprland.conf's bezier / animation lines give
        for (const c of d.curves ?? []) {
            const pts = c.def?.points;
            if (c.def?.type === "bezier" && pts && pts.length >= 2)
                st.options[`animations:bezier:${c.name}`] = `${c.name}, ${pts[0][0]}, ${pts[0][1]}, ${pts[1][0]}, ${pts[1][1]}`;
            else if (c.def?.type === "spring")
                st.options[`animations:bezier:${c.name}`] = `${c.name}, 0.23, 1, 0.32, 1`;   // a spring, drawn as its bezier look-alike
        }
        for (const a of d.animations ?? []) {
            if (!a.leaf) continue;
            st.options[`animations:animation:${a.leaf}`] = `${a.leaf}, ${a.enabled === false ? 0 : 1}, ${a.speed ?? 5}, ${a.bezier ?? a.spring ?? "default"}${a.style ? ", " + a.style : ""}`;
        }
        for (const m of d.monitors ?? []) st.notes.push(Object.assign({ text: `hl.monitor(${JSON.stringify(m)})`, why: "Windows handles this itself - ignored" }, where));
        for (const e of d.env ?? []) st.notes.push(Object.assign({ text: `hl.env("${e.k}", "${e.v}")`, why: "Windows handles this itself - ignored" }, where));
        for (const g of d.gestures ?? []) st.notes.push(Object.assign({ text: `hl.gesture(${JSON.stringify(g)})`, why: "touchpad gestures are not in the Windows port yet" }, where));
        for (const v of d.devices ?? []) st.notes.push(Object.assign({ text: `hl.device(${JSON.stringify(v)})`, why: "Windows handles this itself - ignored" }, where));
        if (d.error) st.notes.push(Object.assign({ text: "hyprland.lua", why: `stopped with an error: ${d.error}` }, where));
        options = st.options;
        binds = st.binds;
        execOnce = st.execOnce;
        execAlways = [];
        rules = (d.rules ?? []).map(r => JSON.stringify(r));
        ruleList = (d.rules ?? []).filter(r => r.enabled !== false).map(r => luaRule(r));
        notes = st.notes;
        variables = {};
        files = st.files;
        runtime = {};
        floating = luaFloatPatterns(d.rules ?? []);
        error = d.error ? `hyprland.lua stopped early: ${d.error}` : "";
        loaded = true;
        usingLua = true;
        usingNiri = false;
        dragMod = luaDrag;
        reloads++;
    }

    // niri's config.kdl, read when there is no hyprland.conf / hyprland.lua (see niri.js)
    function fromNiri(text: string): void {
        const where = { line: 0, file: niriPath };
        const st = { notes: [], binds: [] };
        let out;
        try {
            out = Niri.convert(Niri.parseKdl(text));
        } catch (e) {
            out = { options: {}, binds: [], execOnce: [], rules: [], floatLines: [], notes: [{ text: "config.kdl", why: `could not be read: ${e}` }] };
        }
        for (const b of out.binds) {
            const parts = b.keys.split("+").map(x => x.trim()).filter(x => x);
            const key = parts.pop() ?? "";
            // niri repeats held keys unless repeat=false; Hyprland's "e" flag is the same thing
            const r = parseBind(b.repeat ? "e" : "", `${parts.join(" ")}, ${key}, ${b.dispatcher}, ${b.args}`);
            if (r.problem) st.notes.push(Object.assign({ text: `${b.keys} { ${b.dispatcher} ${b.args} }`, why: r.problem }, where));
            else st.binds.push(Object.assign(r, { description: b.description }, where));
        }
        options = out.options;
        binds = st.binds;
        execOnce = out.execOnce;
        execAlways = [];
        rules = out.rules.map(r => r.text);
        ruleList = out.rules;
        notes = out.notes.map(n => Object.assign({}, n, where)).concat(st.notes);
        variables = {};
        files = [niriPath];
        runtime = {};
        floating = floatPatterns(out.floatLines);
        dragMod = out.dragMod ?? "";
        error = "";
        loaded = true;
        usingNiri = true;
        reloads++;
    }

    // returns false while hyprland.lua is still being read (apply() then runs when hyprlua.exe is done)
    function parse(): bool {
        if (readFile(luaPath) !== null) {
            luaProc.running = false;
            luaProc.command = [hyprluaExe.replace(/\//g, "\\"), luaPath.replace(/\//g, "\\")];
            luaProc.running = true;
            return false;
        }
        usingLua = false;
        usingNiri = false;
        const st = { options: {}, binds: [], execOnce: [], execAlways: [], rules: [], notes: [], vars: {}, files: [] };
        const main = readFile(path);
        if (main === null) {
            // no Hyprland config, but a niri one: run that (niri's scrolling layout, its binds and rules)
            const kdl = readFile(niriPath);
            if (kdl !== null) {
                fromNiri(kdl);
                return true;
            }
            options = {};
            binds = [];
            execOnce = [];
            execAlways = [];
            rules = [];
            ruleList = [];
            notes = [];
            files = [];
            loaded = false;
            error = "no hyprland.conf or hyprland.lua yet";
            return true;
        }
        parseText(main, path, st, 0);
        dragMod = st.dragMod ?? "";
        options = st.options;
        binds = st.binds;
        execOnce = st.execOnce;
        execAlways = st.execAlways;
        rules = st.rules;
        ruleList = st.rules.map(r => confRule(r));
        notes = st.notes;
        variables = st.vars;
        files = st.files;
        runtime = {};
        floating = floatPatterns(st.rules);
        error = "";
        loaded = true;
        reloads++;
        return true;
    }

    function parseText(text: string, file: string, st: var, depth: int): void {
        if (depth > 8) return;
        st.files.push(file);
        const base = file.slice(0, file.lastIndexOf("/"));
        const stack = [];
        const lines = text.split(/\r?\n/);
        for (let n = 0; n < lines.length; n++) {
            let line = stripComment(lines[n]).trim();
            if (!line) continue;
            const where = { line: n + 1, file };

            // "section {" / "section:sub {" / "}" (several on one line are rare; handle the common forms)
            while (line.startsWith("}")) {
                stack.pop();
                line = line.slice(1).trim();
            }
            if (!line) continue;
            let m = line.match(/^([A-Za-z0-9_.:\-]+)\s*\{\s*(.*)$/);
            if (m) {
                stack.push(m[1]);
                line = m[2].trim();
                if (!line) continue;
            }
            let closes = 0;
            while (line.endsWith("}")) {
                closes++;
                line = line.slice(0, -1).trim();
            }

            if (line) {
                const eq = line.indexOf("=");
                if (eq < 0) {
                    st.notes.push(Object.assign({ text: line, why: "not a key = value line" }, where));
                } else {
                    const rawKey = line.slice(0, eq).trim();
                    let value = line.slice(eq + 1).trim();
                    if (rawKey.startsWith("$")) {
                        st.vars[rawKey.slice(1)] = substitute(value, st.vars);
                    } else {
                        value = substitute(value, st.vars);
                        const key = stack.concat([rawKey]).join(":");
                        handle(key, rawKey, value, where, st, depth, base);
                    }
                }
            }
            for (let i = 0; i < closes; i++)
                stack.pop();
        }
    }

    readonly property var known: ["general:layout", "input:follow_mouse", "general:gaps_in", "general:gaps_out", "general:border_size",
        "general:col.active_border", "general:col.inactive_border", "decoration:rounding", "dwindle:preserve_split",
        "dwindle:force_split", "dwindle:smart_split", "dwindle:default_split_ratio", "master:mfact", "master:new_status",
        "master:new_on_top", "master:orientation", "scrolling:column_width", "scrolling:explicit_column_widths",
        "scrolling:focus_fit_method", "binds:workspace_back_and_forth", "cursor:warp_on_change_workspace",
        "animations:enabled", "animations:resize_during_slide", "windows:canvas"]

    // ------------------------------------------------------------ animations
    // bezier = name, x1, y1, x2, y2      animation = leaf, on, speed, curve[, style]   (speed 1 = 100 ms, like Hyprland)
    // The tiler slides windows with the "windowsMove" leaf, falling back to "windows" then "global".
    readonly property var builtinCurves: ({ "default": [0, 0.75, 0.15, 1], "linear": [0, 0, 1, 1],
        "easeOutQuint": [0.23, 1, 0.32, 1], "easeInOutCubic": [0.65, 0.05, 0.36, 1], "almostLinear": [0.5, 0.5, 0.75, 1],
        "quick": [0.15, 0, 0.1, 1], "spring": [0.23, 1, 0.32, 1] })

    function curveNamed(name: string): var {
        const line = opt(`animations:bezier:${name}`, "");
        if (line) {
            const n = String(line).split(",").slice(1).map(x => parseFloat(x));
            if (n.length >= 4 && n.every(x => !isNaN(x))) return n.slice(0, 4);
        }
        return builtinCurves[name] ?? builtinCurves["default"];
    }

    function leaf(name: string): var {
        for (const l of { windowsMove: ["windowsMove", "windows", "global"], windows: ["windows", "global"] }[name] ?? [name, "global"]) {
            const line = opt(`animations:animation:${l}`, "");
            if (!line) continue;
            const p = String(line).split(",").map(x => x.trim());
            return { on: p[1] !== "0" && p[1] !== "false", speed: parseFloat(p[2]) || 5, curve: p[3] || "default", style: p[4] || "" };
        }
        return { on: true, speed: 8, curve: "default", style: "" };   // Hyprland's "global" node: 8 ds = 800 ms
    }

    function applyAnimation(): void {
        const l = leaf("windowsMove");
        const on = enabled && optBool("animations:enabled", true) && l.on && layout !== "canvas";
        // speed is in deciseconds (1 = 100 ms). The size changes once at the end of a slide (see tiling.cpp).
        Win.Tiling.setAnimation(on, Math.round(l.speed * 100), curveNamed(l.curve), false);
    }

    function handle(key: string, rawKey: string, value: string, where: var, st: var, depth: int, base: string): void {
        if (rawKey === "source") {
            const p = expandPath(value, base);
            const t = readFile(p);
            if (t === null) st.notes.push(Object.assign({ text: `source = ${value}`, why: "file not found" }, where));
            else parseText(t, p, st, depth + 1);
            return;
        }
        // submap = NAME ... submap = reset: binds in between only work while that submap is active
        if (rawKey === "submap") {
            st.submap = value.trim() === "reset" ? "" : value.trim();
            return;
        }
        // bindm = MODS, mouse:272, movewindow / mouse:273, resizewindow: hold MODS and drag with the left button to
        // move a window (drop it on another tile to swap), with the right button to resize it
        if (/^bind[a-z]*$/.test(rawKey) && rawKey.slice(4).includes("m")) {
            const p = value.split(",").map(x => x.trim());
            const m = dragModOf(p[0] ?? "");
            if (m && /^mouse:27[23]$/.test(p[1] ?? "") && /^(movewindow|resizewindow)$/.test(p[2] ?? "")) st.dragMod = m;
            else st.notes.push(Object.assign({ text: `${rawKey} = ${value}`, why: "mouse binds: only MOD + mouse:272 movewindow / mouse:273 resizewindow with one of SUPER or ALT" }, where));
            return;
        }
        if (/^bind[a-z]*$/.test(rawKey)) {
            const b = parseBind(rawKey.slice(4), value, st.submap ?? "");
            if (b.problem) st.notes.push(Object.assign({ text: `${rawKey} = ${value}`, why: b.problem }, where));
            else st.binds.push(Object.assign(b, where));
            return;
        }
        // unbind = MODS, key: drops every bind on that key made before this line
        if (rawKey === "unbind") {
            const u = parseBind("", `${value}, no_op`, st.submap || "x");
            const sub = st.submap ?? "";
            if (!u.problem) st.binds = st.binds.filter(b => b.keys.toLowerCase() !== u.keys.toLowerCase() || (b.submap ?? "") !== sub);
            return;
        }
        if (rawKey === "exec-once" || rawKey === "execr-once") { st.execOnce.push(value); return; }
        if (rawKey === "exec" || rawKey === "execr") { st.execAlways.push(value); return; }
        if (rawKey === "windowrule" || rawKey === "windowrulev2") { st.rules.push(value); return; }
        if (["monitor", "env", "workspace", "plugin", "submap", "unbind", "layerrule", "exec-shutdown", "permission", "gesture", "device"].includes(rawKey)
            || key.startsWith("device")) {
            st.notes.push(Object.assign({ text: `${rawKey} = ${value}`, why: "Windows handles this itself - ignored" }, where));
            return;
        }
        if (rawKey === "bezier" || rawKey === "animation") {
            st.options[`${key}:${value.split(",")[0].trim()}`] = value;
            return;
        }
        st.options[key] = value;
        if (!known.includes(key))
            st.notes.push(Object.assign({ text: `${key} = ${value}`, why: "read, but Windows has no use for it yet" }, where));
    }

    // Hyprland key names -> the port's ("Super+Shift+Return")
    function keyName(k: string): string {
        const map = { "return": "Return", "enter": "Return", "space": "Space", "tab": "Tab", "escape": "Escape",
            "backspace": "Backspace", "delete": "Delete", "insert": "Insert", "home": "Home", "end": "End",
            "prior": "PageUp", "next": "PageDown", "page_up": "PageUp", "page_down": "PageDown",
            "left": "Left", "right": "Right", "up": "Up", "down": "Down", "print": "Print", "pause": "Pause",
            "comma": ",", "period": ".", "minus": "-", "equal": "=", "slash": "/", "backslash": "\\",
            "semicolon": ";", "apostrophe": "'", "grave": "`", "bracketleft": "[", "bracketright": "]" };
        const low = k.toLowerCase();
        if (map[low]) return map[low];
        if (/^f([1-9]|1[0-9]|2[0-4])$/i.test(k)) return k.toUpperCase();
        if (/^xf86/i.test(k)) return k;
        if (k.length === 1) return k.toUpperCase();
        return k;
    }

    // the drag modifier of a bindm: "alt" or "super", or "" when it is something the drag code can't do
    function dragModOf(mods: string): string {
        const M = mods.toUpperCase();
        const alt = /ALT|MOD1/.test(M);
        const sup = /SUPER|WIN|LOGO|MOD4|META/.test(M);
        if (/CTRL|CONTROL|SHIFT/.test(M) || alt === sup) return "";
        return alt ? "alt" : "super";
    }

    function parseBind(flags: string, value: string, submap: var): var {
        const parts = value.split(",");
        if (parts.length < 3) return { problem: "a bind needs MODS, key, dispatcher" };
        const desc = flags.includes("d") ? parts.splice(2, 1)[0].trim() : "";
        const modsRaw = parts[0].trim();
        const key = parts[1].trim();
        const dispatcher = parts[2].trim().toLowerCase();
        const args = parts.slice(3).join(",").trim();
        if (flags.includes("m") || /^mouse/i.test(key)) return { problem: "mouse binds are done by Windows (drag to move, edges to resize)" };
        if (/^code:/i.test(key)) return { problem: "key codes are Linux-only - use the key's name" };
        if (!key) return { problem: "no key" };
        // like Hyprland's stringToModMask: it looks for each name anywhere, so "SUPER SHIFT", "SUPER_SHIFT" and
        // "SUPERSHIFT" are all the same
        const mods = [];
        const M = modsRaw.toUpperCase();
        if (/SUPER|WIN|LOGO|MOD4|META/.test(M)) mods.push("Super");
        if (/CTRL|CONTROL/.test(M)) mods.push("Ctrl");
        if (/ALT|MOD1/.test(M)) mods.push("Alt");
        if (/SHIFT/.test(M)) mods.push("Shift");
        const odd = M.match(/CAPS|MOD2|MOD3|MOD5/);
        if (odd) return { problem: `modifier ${odd[0]} does not exist on Windows` };
        const order = ["Super", "Ctrl", "Alt", "Shift"];
        const uniq = order.filter(o => mods.includes(o));
        const keys = uniq.concat([keyName(key)]).join("+");
        if (dispatcher === "exit") return { problem: "exit would sign you out of Windows - not bound" };
        // inside a submap a plain key is fine: it only works while you are in that submap (like Hyprland)
        if (!submap && !uniq.filter(x => x !== "Shift").length && !/^(F\d+|XF86.*|Print|Pause)$/i.test(keyName(key)))
            return { problem: "a key with no Super/Ctrl/Alt would stop that key typing everywhere" };
        return { mods: uniq, key, keys, dispatcher, args, flags, description: desc, submap: submap ?? "" };
    }

    function floatPatterns(list: var): var {
        const out = [];
        for (const r of list) {
            const parts = r.split(",").map(x => x.trim());
            const isFloat = parts.some(p => /^float(\s+(on|1|true|yes))?$/i.test(p));
            if (!isFloat) continue;
            for (const p of parts) {
                const m = p.match(/^(?:match:)?(class|title|initialclass|initialtitle)\s*[: ]\s*(.+)$/i);
                if (!m) continue;
                const byTitle = m[1].toLowerCase().includes("title");
                let rx = m[2].trim().replace(/^\^/, "").replace(/\$$/, "").replace(/^\((.*)\)$/, "$1");
                for (let alt of rx.split("|")) {
                    alt = alt.replace(/\.\*/g, "*").replace(/\\\./g, ".").replace(/[()^$]/g, "").trim();
                    if (alt) out.push(byTitle ? `title:${alt}` : alt);
                }
            }
        }
        return out;
    }

    // ------------------------------------------------------------ window rules
    // Applied when a window opens (Hyprland's "static" rules). Both spellings of hyprland.conf work:
    //   windowrule = float, class:^(kitty)$, title:foo          (rule first, then props)
    //   windowrule = match:class ^(kitty)$, float on, size 800 600
    // and hyprland.lua's hl.window_rule({ match = { class = "kitty" }, float = true, size = "800 600" }).
    readonly property var ruleEffects: ["float", "tile", "size", "move", "center", "workspace", "pin", "fullscreen", "maximize"]
    readonly property var matchKeys: ({ class: "class", title: "title", initialclass: "initial_class", initial_class: "initial_class",
                                         initialtitle: "initial_title", initial_title: "initial_title", xwayland: "xwayland" })

    function ruleValue(v: var): var {
        const s = String(v).trim();
        if (s === "" || /^(on|1|true|yes)$/i.test(s)) return true;
        if (/^(off|0|false|no)$/i.test(s)) return false;
        return s;
    }

    function confRule(line: string): var {
        const out = { match: {}, effects: {}, text: line };
        const parts = line.split(",").map(x => x.trim()).filter(x => x);
        for (const p of parts) {
            let m = p.match(/^match:([a-z_]+)\s+(.*)$/i) || p.match(/^([a-z_]+):(.*)$/i);
            if (m && matchKeys[m[1].toLowerCase()]) {
                out.match[matchKeys[m[1].toLowerCase()]] = m[2].trim();
                continue;
            }
            m = p.match(/^([a-z_]+)\s*(.*)$/i);
            if (m) out.effects[m[1].toLowerCase()] = ruleValue(m[2]);
        }
        return out;
    }

    function luaRule(r: var): var {
        const out = { match: {}, effects: {}, text: r.name ?? JSON.stringify(r.match ?? {}) };
        for (const k of Object.keys(r.match ?? {})) out.match[matchKeys[k] ?? k] = r.match[k];
        for (const k of Object.keys(r)) {
            if (k === "match" || k === "name" || k === "enabled") continue;
            const v = r[k];
            out.effects[k] = Array.isArray(v) ? v.join(" ") : (typeof v === "boolean" ? v : ruleValue(v));
        }
        return out;
    }

    // Hyprland matches with RE2 FullMatch
    function fullMatch(pattern: var, value: string): bool {
        try {
            return new RegExp(`^(?:${pattern})$`).test(value ?? "");
        } catch (e) {
            return false;
        }
    }

    function ruleMatches(rule: var, w: var): bool {
        const m = rule.match;
        const keys = Object.keys(m);
        if (!keys.length) return false;
        for (const k of keys) {
            if (k === "class" || k === "initial_class") { if (!fullMatch(m[k], w.class)) return false; }
            else if (k === "title" || k === "initial_title") { if (!fullMatch(m[k], w.title)) return false; }
            else if (k === "xwayland") { if (ruleValue(m[k]) !== false) return false; }   // nothing is XWayland here
            else return false;                                                          // a match we cannot check yet
        }
        return true;
    }

    // "800 600", "50% 50%", "monitor_w*0.5 monitor_h-120", "(monitor_w-window_w)/2 20"
    function ruleExpr(s: string, mw: real, mh: real, ww: real, wh: real, horizontal: bool): var {
        let e = String(s).trim();
        if (/%$/.test(e)) return (horizontal ? mw : mh) * parseFloat(e) / 100;
        e = e.replace(/monitor_w/g, mw).replace(/monitor_h/g, mh).replace(/window_w/g, ww).replace(/window_h/g, wh);
        if (!/^[0-9.+\-*/() ]+$/.test(e)) return null;
        try {
            const v = Function(`return (${e});`)();
            return isFinite(v) ? Math.round(v) : null;
        } catch (err) {
            return null;
        }
    }

    function pair(v: var): var {
        const s = String(v).trim();
        // split on the space between the two expressions, not spaces inside one
        const m = s.match(/^(\S+)\s+(\S+)$/) ?? s.match(/^(.+?)\s+(\S+)$/);
        return m ? [m[1], m[2]] : null;
    }

    // windows already open when the shell starts are "opened" again by its first scan - Hyprland's rules are
    // for windows that open, so those are left alone
    property bool rulesArmed: false
    readonly property Timer rulesArmTimer: Timer {
        running: true
        interval: 5000
        onTriggered: root.rulesArmed = true
    }

    function onWindowOpened(address: string): void {
        if (!enabled || !rulesArmed || !ruleList.length) return;
        const addr = address.replace(/^0x/, "");
        const t = Hyprland.toplevels.values.find(x => String(x.address).replace(/^0x/, "") === addr);
        if (!t) return;
        const info = t.lastIpcObject ?? {};
        const w = { class: t.wayland?.appId ?? info.class ?? "", title: t.title ?? info.title ?? "" };
        const eff = {};
        for (const r of ruleList)
            if (ruleMatches(r, w)) Object.assign(eff, r.effects);
        if (!Object.keys(eff).length) return;

        const sel = `address:0x${addr}`;
        const d = request => Hyprland.dispatch(request);
        if (eff.workspace !== undefined && eff.workspace !== false) {
            const ws = String(eff.workspace).trim().split(/\s+/);
            d(`${ws[1] === "silent" ? "movetoworkspacesilent" : "movetoworkspace"} ${ws[0]},${sel}`);
        }
        if (eff.float === true) d(`setfloating ${sel}`);
        if (eff.float === false || eff.tile === true) d(`settiled ${sel}`);
        if (eff.pin === true && !info.pinned) d(`pin ${sel}`);
        if (eff.fullscreen === true) d(`hl.dsp.window.fullscreen({ mode = "fullscreen", action = "set", window = "${sel}" })`);
        if (eff.maximize === true) d(`hl.dsp.window.fullscreen({ mode = "maximized", action = "set", window = "${sel}" })`);

        // size / move / center once the window is floating where it opened
        if (eff.size !== undefined || eff.move !== undefined || eff.center === true) {
            ruleTimer.queue = ruleTimer.queue.concat([{ addr, eff }]);
            ruleTimer.restart();
        }
    }

    function applyPlacement(addr: string, eff: var): void {
        const t = Hyprland.toplevels.values.find(x => String(x.address).replace(/^0x/, "") === addr);
        if (!t) return;
        const info = t.lastIpcObject ?? {};
        const mon = t.monitor ?? Hyprland.monitors.values.find(m => m.id === info.monitor);
        const mw = mon?.width ?? 1920;
        const mh = mon?.height ?? 1080;
        let ww = info.size?.[0] ?? 800;
        let wh = info.size?.[1] ?? 600;
        const sel = `address:0x${addr}`;
        if (eff.size !== undefined) {
            const p = pair(eff.size);
            const x = p ? ruleExpr(p[0], mw, mh, ww, wh, true) : null;
            const y = p ? ruleExpr(p[1], mw, mh, ww, wh, false) : null;
            if (x !== null && y !== null) {
                Hyprland.dispatch(`hl.dsp.window.resize({ x = ${x}, y = ${y}, window = "${sel}" })`);
                ww = x;
                wh = y;
            }
        }
        if (eff.move !== undefined) {
            const p = pair(eff.move);
            const x = p ? ruleExpr(p[0], mw, mh, ww, wh, true) : null;
            const y = p ? ruleExpr(p[1], mw, mh, ww, wh, false) : null;
            if (x !== null && y !== null) Hyprland.dispatch(`hl.dsp.window.move({ x = ${x}, y = ${y}, window = "${sel}" })`);
        } else if (eff.center === true) {
            Hyprland.dispatch(`hl.dsp.window.center({ window = "${sel}" })`);
        }
    }

    readonly property Timer ruleTimer: Timer {
        property var queue: []
        interval: 250
        onTriggered: {
            const q = queue;
            queue = [];
            for (const item of q) root.applyPlacement(item.addr, item.eff);
        }
    }

    readonly property Connections hyprEvents: Connections {
        target: Hyprland
        function onRawEvent(event: var): void {
            if (event.name === "openwindow") root.onWindowOpened(String(event.data).split(",")[0]);
            else if (event.name === "configreloaded" && String(event.data) === "") root.reloadTimer.restart();
        }
    }

    // ------------------------------------------------------------ applying
    function bindJson(b: var): var {
        const o = { keys: b.keys, from: "hyprland", description: b.description || `${b.dispatcher} ${b.args}`.trim() };
        if (b.submap) o.submap = b.submap;
        if (b.flags.includes("e")) o.repeat = true;
        if (b.flags.includes("r")) o.onRelease = true;
        if (b.dispatcher === "exec") {
            o.action = "exec";
            o.command = b.args;
        } else if (b.dispatcher === "global") {
            o.action = "global";
            o.name = b.args;
        } else if (b.dispatcher === "lua") {          // from hyprland.lua: the port's dispatcher reads hl.dsp.* calls
            o.action = "dispatch";
            o.dispatch = b.args;
            if (!b.description) o.description = b.args;
        } else {
            o.action = "dispatch";
            o.dispatch = `${b.dispatcher} ${b.args}`.trim();
        }
        return o;
    }

    function writeBinds(): void {
        let cur = { extends: "defaults", binds: [] };
        const t = readFile(keybindsPath);
        if (t !== null) {
            try { cur = JSON.parse(t); } catch (e) { cur = null; }
        }
        if (!cur || typeof cur !== "object") {           // never overwrite a file we cannot read
            console.warn(`HyprConf: could not read ${keybindsPath} (${t === null ? "missing" : t.length + " chars: " + JSON.stringify(t) + " " + typeof t}) - binds not written`);
            return;
        }
        const keep = (cur.binds ?? []).filter(b => b.from !== "hyprland");
        if (!enabled && keep.length === (cur.binds ?? []).length) return;   // nothing of ours in there
        const mine = enabled ? binds.map(b => bindJson(b)) : [];
        // your own shortcuts win over hyprland.conf ones on the same keys
        const taken = keep.filter(b => b.keys && !b.submap).map(b => b.keys.toLowerCase());
        const next = Object.assign({}, cur, { extends: cur.extends ?? "defaults", binds: keep.concat(mine.filter(b => b.submap || !taken.includes(b.keys.toLowerCase()))) });
        const out = JSON.stringify(next, null, 2) + "\n";
        if (out !== (t ?? "")) keybindsFile.setText(out);
    }

    function apply(): void {
        const T = Win.Tiling;
        const W = Win.WindowManager;
        const lay = layout;
        canvas.wanted = enabled && lay === "canvas";
        if (!enabled) {
            T.enabled = false;
            W.followMouse = 0;
            Win.SuperDrag.enabled = false;
            writeBinds();
            return;
        }
        T.gapsIn = optInt("general:gaps_in", 5);
        T.gapsOut = optInt("general:gaps_out", 20);
        T.preserveSplit = optBool("dwindle:preserve_split", false);
        T.forceSplit = optInt("dwindle:force_split", 0);
        T.smartSplit = optBool("dwindle:smart_split", false);
        T.defaultSplitRatio = optReal("dwindle:default_split_ratio", 1.0);
        T.masterFactor = optReal("master:mfact", 0.55);
        T.masterNewStatus = String(opt("master:new_status", "slave"));
        T.masterNewOnTop = optBool("master:new_on_top", false);
        T.masterOrientation = String(opt("master:orientation", "left"));
        T.scrollingColumnWidth = optReal("scrolling:column_width", 0.5);
        const presets = String(opt("scrolling:explicit_column_widths", "")).split(",").map(x => parseFloat(x)).filter(x => !isNaN(x) && x > 0);
        if (presets.length) T.scrollingPresets = presets;
        const fit = String(opt("scrolling:focus_fit_method", "")).trim();
        T.scrollingCenter = fit === "0" || fit === "center" ? "always" : fit === "1" || fit === "fit" ? "overflow" : "overflow";
        T.excluded = floating;
        if (["dwindle", "master", "scrolling"].includes(lay)) T.layout = lay;
        T.enabled = ["dwindle", "master", "scrolling"].includes(lay);

        if (W.decorationsSupported) {
            const size = optInt("general:border_size", 1);
            // Hyprland's defaults: border 1, active white, inactive #444444, rounding 0
            W.borders = size <= 0 ? "none" : "colors";
            W.activeBorderColor = colour(opt("general:col.active_border", ""), "#ffffff");
            W.inactiveBorderColor = colour(opt("general:col.inactive_border", ""), "#444444");
            const r = optInt("decoration:rounding", 0);
            W.corners = r <= 0 ? "square" : r <= 6 ? "roundSmall" : "round";
        }
        W.workspaceBackAndForth = optBool("binds:workspace_back_and_forth", false);
        W.warpOnChangeWorkspace = optInt("cursor:warp_on_change_workspace", 0);
        W.followMouse = optInt("input:follow_mouse", 0);   // click to focus, like Windows (Hyprland itself defaults to 1)
        // mod + drag (bindm): left button moves (drop on another tile to swap), right button resizes
        Win.SuperDrag.modifier = dragMod || "super";
        Win.SuperDrag.enabled = dragMod !== "";
        applyAnimation();

        writeBinds();

        if (!ranOnce) {
            ranOnce = true;
            for (const c of execOnce) run(c);
        }
        for (const c of execAlways) run(c);
    }

    function run(cmd: string): void {
        if (cmd.trim()) Quickshell.execDetached(["cmd.exe", "/c", cmd]);
    }

    function reload(): void {
        if (parse()) {
            apply();
            reloaded();
        }
    }

    // Settings changes for a hyprland.lua go in one block at its end; a later hl.config() wins in Hyprland too.
    function luaValue(v: string): string {
        const s = String(v).trim();
        if (/^(true|false)$/i.test(s)) return s.toLowerCase();
        if (/^-?\d+(\.\d+)?$/.test(s) || /^0x[0-9a-f]+$/i.test(s)) return s;
        return JSON.stringify(s);
    }

    function setLuaOption(full: string, value: string): void {
        let text = readFile(luaPath) ?? "";
        const start = "-- caelestia-settings (Settings > Hyprland writes here; later hl.config calls win)";
        const end = "-- end caelestia-settings";
        let lines = [];
        const a = text.indexOf(start);
        const b = text.indexOf(end);
        if (a >= 0 && b > a) {
            lines = text.slice(a + start.length, b).split(/\r?\n/).filter(l => l.trim().startsWith("hl.config"));
            text = text.slice(0, a).replace(/\s*$/, "\n") + text.slice(b + end.length).replace(/^\s*/, "");
        }
        // general:col.active_border -> { general = { col = { active_border = ... } } }
        const path = full.replace(/\.(?=[a-z_]+$)/, ":").split(":");
        let lua = luaValue(value);
        for (let i = path.length - 1; i >= 0; i--) lua = `{ ${path[i]} = ${lua} }`;
        const line = `hl.config(${lua})  -- ${full}`;
        lines = lines.filter(l => !l.endsWith(`-- ${full}`)).concat([line]);
        const out = text.replace(/\s*$/, "\n") + `\n${start}\n${lines.join("\n")}\n${end}\n`;
        luaFile.setText(out);
    }

    // ------------------------------------------------------------ editing (Settings writes back into hyprland.conf)
    // Replaces "key = ..." inside its section (or the "section:key" form), or adds it to the section / a new section.
    function setOption(full: string, value: string): void {
        if (usingNiri) {
            // Settings does not rewrite config.kdl; the change lasts until the next reload, like hyprctl keyword
            const r = Object.assign({}, runtime);
            r[full] = value;
            runtime = r;
            apply();
            return;
        }
        if (usingLua) {
            setLuaOption(full, value);
            return;
        }
        let text = readFile(path);
        if (text === null) text = defaultConf;
        const parts = full.split(":");
        const leaf = parts.pop();
        const lines = text.split(/\r?\n/);
        const stack = [];
        let sectionEnd = -1;
        let done = false;
        for (let i = 0; i < lines.length && !done; i++) {
            const raw = lines[i];
            const s = stripComment(raw).trim();
            const open = s.match(/^([A-Za-z0-9_.:\-]+)\s*\{/);
            if (open) { stack.push(open[1]); continue; }
            if (s.startsWith("}")) {
                if (stack.join(":") === parts.join(":")) sectionEnd = i;
                stack.pop();
                continue;
            }
            const m = s.match(/^([A-Za-z0-9_.:\-]+)\s*=/);
            if (!m) continue;
            const k = stack.concat([m[1]]).join(":");
            if (k === full) {
                const indent = raw.match(/^\s*/)[0];
                const comment = raw.indexOf("#") > 0 && raw[raw.indexOf("#") + 1] !== "#" ? "   " + raw.slice(raw.indexOf("#")) : "";
                lines[i] = `${indent}${m[1]} = ${value}${comment}`;
                done = true;
            }
        }
        if (!done && sectionEnd >= 0 && parts.length) {
            lines.splice(sectionEnd, 0, `${"    ".repeat(parts.length)}${leaf} = ${value}`);
            done = true;
        }
        if (!done) {
            if (parts.length) lines.push("", `${parts.join(":")} {`, `    ${leaf} = ${value}`, "}");
            else lines.push(`${leaf} = ${value}`);
        }
        confFile.setText(lines.join("\n").replace(/\n*$/, "\n"));
    }

    function ensureConf(): void {
        // a niri user's config.kdl is used as it is - no starter hyprland.conf next to it
        if (readFile(path) === null && readFile(luaPath) === null && readFile(niriPath) === null) {
            Quickshell.execDetached(["cmd.exe", "/c", "mkdir", dir.replace(/\//g, "\\")]);
            confFile.setText(defaultConf);
        }
    }

    function openConf(): void {
        if (!usingLua && !usingNiri) ensureConf();
        const p = usingLua ? luaPath : usingNiri ? niriPath : path;
        Qt.callLater(() => Qt.openUrlExternally("file:///" + p));
    }

    readonly property string defaultConf: `# Hyprland for Windows - your hyprland.conf (Hyprland's own syntax).
# Caelestia reads this when "Hyprland mode" is on (Settings > Hyprland) and reloads it when you save.
# Lines Windows cannot use are listed in Settings > Hyprland > Notes instead of failing.
# Wiki for every option: https://wiki.hypr.land/Configuring/

$mainMod = ALT
$terminal = wt.exe
$fileManager = explorer.exe
$browser = explorer.exe https://

# exec-once = runs once when Caelestia starts       exec = runs on every reload
# exec-once = $terminal

general {
    layout = dwindle          # dwindle | master | scrolling (niri-like columns) | floating | canvas (InfiniteDesk)
    gaps_in = 5
    gaps_out = 12
    border_size = 2
    col.active_border = rgba(33ccffee) rgba(00ff99ee) 45deg
    col.inactive_border = rgba(595959aa)
}

decoration {
    rounding = 10             # Windows 11 draws round (> 6), small round (1-6) or square (0) corners
}

dwindle {
    preserve_split = true
}

master {
    new_status = master
    mfact = 0.55
}

scrolling {
    column_width = 0.5
    explicit_column_widths = 0.333, 0.5, 0.667, 1.0
}

binds {
    workspace_back_and_forth = false
}

input {
    follow_mouse = 0          # 1: focus follows the mouse (Hyprland's default) | 0: click to focus, like Windows
}

# Tiling = Alt + key, moving windows = Alt+Shift + key, desktops = Alt + number.
# Alt+Tab and Alt+F4 stay Windows'.
bind = $mainMod, Return, exec, $terminal
bind = $mainMod, Q, killactive,
bind = $mainMod, V, togglefloating,
bind = $mainMod, J, togglesplit,
bind = $mainMod, F, fullscreen, 1
bind = $mainMod SHIFT, F, fullscreen, 0

bind = $mainMod, left, movefocus, l
bind = $mainMod, right, movefocus, r
bind = $mainMod, up, movefocus, u
bind = $mainMod, down, movefocus, d
bind = $mainMod SHIFT, left, swapwindow, l
bind = $mainMod SHIFT, right, swapwindow, r
bind = $mainMod SHIFT, up, swapwindow, u
bind = $mainMod SHIFT, down, swapwindow, d

binde = $mainMod CTRL, left, resizeactive, -40 0
binde = $mainMod CTRL, right, resizeactive, 40 0
binde = $mainMod CTRL, up, resizeactive, 0 -40
binde = $mainMod CTRL, down, resizeactive, 0 40

# hold the mod and drag: left button moves a window (drop it on another one to swap them), right button resizes
bindm = $mainMod, mouse:272, movewindow
bindm = $mainMod, mouse:273, resizewindow

bind = $mainMod, 1, workspace, 1
bind = $mainMod, 2, workspace, 2
bind = $mainMod, 3, workspace, 3
bind = $mainMod, 4, workspace, 4
bind = $mainMod, 5, workspace, 5
bind = $mainMod SHIFT, 1, movetoworkspace, 1
bind = $mainMod SHIFT, 2, movetoworkspace, 2
bind = $mainMod SHIFT, 3, movetoworkspace, 3
bind = $mainMod SHIFT, 4, movetoworkspace, 4
bind = $mainMod SHIFT, 5, movetoworkspace, 5

bind = $mainMod, S, togglespecialworkspace, magic
bind = $mainMod SHIFT, S, movetoworkspace, special:magic

# windows that should never tile
windowrule = float, class:^(Calculator|SnippingTool|Taskmgr)$
# Spotify (CEF) breaks when another program resizes it - it floats. Discord tiles (Alt+T floats it)
windowrule = float, class:^(spotify)$
# Aether: Alt+T (togglefloating) resizes apps for real instead of stretching their picture while they tile
aether {
    toggle_live_resize = true
}
`

    // ------------------------------------------------------------ files
    readonly property FileView confFile: FileView {
        path: root.path
        blockLoading: true
        blockAllReads: true   // a reload after a save must read the new text, not the old one
        watchChanges: true
        printErrors: false
        onFileChanged: reloadTimer.restart()
        onSaved: reloadTimer.restart()
    }

    readonly property FileView keybindsFile: FileView {
        path: root.keybindsPath
        blockLoading: true
        blockAllReads: true
        printErrors: false
    }

    readonly property FileView luaFile: FileView {
        path: root.luaPath
        blockLoading: true
        blockAllReads: true
        watchChanges: true
        printErrors: false
        onFileChanged: reloadTimer.restart()
        onSaved: reloadTimer.restart()
    }

    // only watched: its text is read through readFile's reader
    readonly property FileView niriWatch: FileView {
        path: root.niriPath
        watchChanges: true
        printErrors: false
        onFileChanged: reloadTimer.restart()
    }

    readonly property Timer reloadTimer: Timer {
        interval: 250
        onTriggered: root.reload()
    }

    Connections {
        target: WinSettings
        function onHyprlandChanged(): void {
            if (WinSettings.hyprland) root.ensureConf();
            root.reload();
        }
    }

    Component.onCompleted: Qt.callLater(() => root.reload())

    // ------------------------------------------------------------ InfiniteDesk (layout = canvas)
    QtObject {
        id: canvas

        property bool wanted: false
        onWantedChanged: {
            if (wanted) Quickshell.execDetached([root.canvasExe]);
            else Quickshell.execDetached(["taskkill.exe", "/im", "InfiniteDesk.exe"]);
        }
    }

    // ------------------------------------------------------------ hyprctl
    function clientsJson(): var {
        return Hyprland.toplevels.values.map(t => {
            const o = t.lastIpcObject && Object.keys(t.lastIpcObject).length ? t.lastIpcObject : {};
            return Object.assign({ address: t.address, title: t.title, workspace: { id: t.workspace?.id ?? -1, name: t.workspace?.name ?? "" } }, o);
        });
    }

    IpcHandler {
        target: "hyprctl"

        function dispatch(request: string): string {
            if (/^exit\b/.test(request.trim())) return "refused: exit would sign you out of Windows";
            Hyprland.dispatch(request);
            return "ok";
        }
        // Windows extra: write the starter hyprland.conf if there is none (qs ipc call hyprctl init)
        function init(): string {
            root.ensureConf();
            return root.path;
        }
        function reload(): string {
            root.reload();
            return root.error || "ok";
        }
        function keyword(key: string, value: string): string {
            const r = Object.assign({}, root.runtime);
            r[key] = value;
            root.runtime = r;
            root.apply();
            return "ok";
        }
        function getoption(key: string): string {
            return JSON.stringify({ option: key, value: root.opt(key, null), set: root.options[key] !== undefined || root.runtime[key] !== undefined });
        }
        function clients(): string { return JSON.stringify(root.clientsJson(), null, 2); }
        function workspaces(): string {
            return JSON.stringify(Hyprland.workspaces.values.map(w => Object.assign({ id: w.id, name: w.name }, w.lastIpcObject ?? {})), null, 2);
        }
        function activeworkspace(): string {
            const w = Hyprland.focusedWorkspace;
            return JSON.stringify(w ? Object.assign({ id: w.id, name: w.name }, w.lastIpcObject ?? {}) : {}, null, 2);
        }
        function activewindow(): string {
            const t = Hyprland.activeToplevel;
            return JSON.stringify(t ? Object.assign({ address: t.address, title: t.title }, t.lastIpcObject ?? {}) : {}, null, 2);
        }
        function monitors(): string {
            return JSON.stringify(Hyprland.monitors.values.map(m => Object.assign({ id: m.id, name: m.name }, m.lastIpcObject ?? {})), null, 2);
        }
        function binds(): string { return JSON.stringify(root.binds.map(b => root.bindJson(b)), null, 2); }
        function notes(): string { return JSON.stringify(root.notes, null, 2); }
        function version(): string {
            return JSON.stringify({ branch: "windows", tag: "caelestia-windows", config: root.usingLua ? root.luaPath : root.usingNiri ? root.niriPath : root.path, lua: root.usingLua, niri: root.usingNiri, drag: root.dragMod, dragOn: Win.SuperDrag.enabled, enabled: root.enabled, layout: root.layout, binds: root.binds.length, notes: root.notes.length, error: root.error });
        }
    }
}
