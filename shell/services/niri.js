.pragma library

// niri's config.kdl for Caelestia on Windows: a small KDL reader plus niri -> Hyprland-port translations.
// HyprConf.qml runs the result through the same engine as hyprland.conf, with layout = scrolling (niri's own layout).

// ------------------------------------------------------------ KDL
// node: { name, args: [], props: {}, children: [] }. Handles // and /* */ comments, /- (slashdash), "strings" with
// escapes, r#"raw"# strings, bare words, numbers, #true/#false/#null and true/false/null, ; and newlines.
function parseKdl(text) {
    let i = 0;
    const s = String(text ?? "");
    const n = s.length;

    function skipSpace(newlines) {
        for (;;) {
            const c = s[i];
            if (c === " " || c === "\t" || c === "\r" || c === "﻿") { i++; continue; }
            if (c === "\\") {                                      // line continuation
                let j = i + 1;
                while (s[j] === " " || s[j] === "\t") j++;
                if (s[j] === "\n" || s[j] === "\r") { i = j; while (s[i] === "\r" || s[i] === "\n") i++; continue; }
            }
            if (newlines && c === "\n") { i++; continue; }
            if (c === "/" && s[i + 1] === "/") { while (i < n && s[i] !== "\n") i++; continue; }
            if (c === "/" && s[i + 1] === "*") {
                let depth = 1;
                i += 2;
                while (i < n && depth > 0) {
                    if (s[i] === "/" && s[i + 1] === "*") { depth++; i += 2; }
                    else if (s[i] === "*" && s[i + 1] === "/") { depth--; i += 2; }
                    else i++;
                }
                continue;
            }
            return;
        }
    }

    function readString() {
        // at the opening quote
        i++;
        let out = "";
        while (i < n && s[i] !== "\"") {
            if (s[i] === "\\") {
                const e = s[i + 1];
                const map = { n: "\n", t: "\t", r: "\r", "\"": "\"", "\\": "\\", "/": "/", b: "\b", f: "\f" };
                if (e === "u" && s[i + 2] === "{") {
                    const end = s.indexOf("}", i);
                    out += String.fromCodePoint(parseInt(s.slice(i + 3, end), 16));
                    i = end + 1;
                    continue;
                }
                out += map[e] ?? e;
                i += 2;
                continue;
            }
            out += s[i++];
        }
        i++;
        return out;
    }

    function readRaw() {
        // r#"..."# or #"..."# (KDL 2)
        if (s[i] === "r") i++;
        let hashes = 0;
        while (s[i] === "#") { hashes++; i++; }
        i++;                                                       // the quote
        const close = "\"" + "#".repeat(hashes);
        const end = s.indexOf(close, i);
        const out = s.slice(i, end < 0 ? n : end);
        i = end < 0 ? n : end + close.length;
        return out;
    }

    function readBare() {
        const start = i;
        while (i < n && !/[\s{}();=\"]/.test(s[i]) && !(s[i] === "/" && (s[i + 1] === "/" || s[i + 1] === "*"))) i++;
        return s.slice(start, i);
    }

    function value(word) {
        if (word === "#true" || word === "true") return true;
        if (word === "#false" || word === "false") return false;
        if (word === "#null" || word === "null") return null;
        if (/^[+-]?(\d[\d_]*)(\.\d[\d_]*)?([eE][+-]?\d+)?$/.test(word)) return parseFloat(word.replace(/_/g, ""));
        if (/^0x[0-9a-fA-F_]+$/.test(word)) return parseInt(word.slice(2).replace(/_/g, ""), 16);
        return word;
    }

    function readValue() {
        if (s[i] === "\"") return readString();
        if ((s[i] === "r" && (s[i + 1] === "#" || s[i + 1] === "\"")) || (s[i] === "#" && (s[i + 1] === "#" || s[i + 1] === "\""))) return readRaw();
        if (s[i] === "(") {                                        // (type)value annotations: ignore the type
            i = s.indexOf(")", i) + 1;
        }
        return value(readBare());
    }

    function readNodes(untilBrace) {
        const nodes = [];
        for (;;) {
            skipSpace(true);
            while (s[i] === ";") { i++; skipSpace(true); }
            if (i >= n) return nodes;
            if (s[i] === "}") {
                if (untilBrace) i++;
                return nodes;
            }
            let skip = false;
            if (s[i] === "/" && s[i + 1] === "-") { skip = true; i += 2; skipSpace(true); }
            const node = readNode();
            if (!skip && node) nodes.push(node);
        }
    }

    function readNode() {
        if (s[i] === "(") i = s.indexOf(")", i) + 1;
        const name = s[i] === "\"" ? readString() : (s[i] === "r" && s[i + 1] === "#") ? readRaw() : readBare();
        const node = { name, args: [], props: {}, children: [] };
        if (!name) { i++; return null; }
        for (;;) {
            skipSpace(false);
            const c = s[i];
            if (i >= n || c === "\n" || c === ";") { if (c === ";") i++; return node; }
            if (c === "}") return node;
            let skip = false;
            if (c === "/" && s[i + 1] === "-") { skip = true; i += 2; skipSpace(false); }
            if (s[i] === "{") {
                i++;
                const kids = readNodes(true);
                if (!skip) node.children = node.children.concat(kids);
                continue;
            }
            // prop=value or a plain value
            const save = i;
            let word = null;
            if (s[i] !== "\"" && !(s[i] === "r" && s[i + 1] === "#")) {
                word = readBare();
                if (s[i] === "=") {
                    i++;
                    const v = readValue();
                    if (!skip) node.props[word] = v;
                    continue;
                }
                i = save;
            } else if (s[i] === "\"") {
                const str = readString();
                if (s[i] === "=") {
                    i++;
                    const v = readValue();
                    if (!skip) node.props[str] = v;
                    continue;
                }
                if (!skip) node.args.push(str);
                continue;
            }
            const v = readValue();
            if (i === save) { i++; continue; }                     // nothing readable: step over it
            if (!skip) node.args.push(v);
        }
    }

    return readNodes(false);
}

function child(node, name) { return (node?.children ?? []).find(c => c.name === name); }
function children(node, name) { return (node?.children ?? []).filter(c => c.name === name); }
function has(node, name) { return !!child(node, name); }
function arg(node, name, fallback) { const c = child(node, name); return c && c.args.length ? c.args[0] : fallback; }

// ------------------------------------------------------------ niri actions -> the port's Hyprland dispatchers
function quote(a) { const t = String(a); return /[\s"]/.test(t) ? `"${t.replace(/"/g, '\\"')}"` : t; }

function percent(v) {
    // "+10%" -> "+0.1", "50%" -> "0.5", "-100" (pixels) -> null
    const m = String(v).trim().match(/^([+-]?)(\d+(?:\.\d+)?)%$/);
    return m ? `${m[1]}${parseFloat(m[2]) / 100}` : null;
}

// returns { dispatcher, args } or { problem }
function action(node) {
    const a = node.args ?? [];
    const ws = w => (typeof w === "number" ? String(w) : `name:${w}`);
    const table = {
        "spawn": () => ({ dispatcher: "exec", args: a.map(quote).join(" ") }),
        "spawn-sh": () => ({ dispatcher: "exec", args: String(a[0] ?? "") }),
        "close-window": () => ({ dispatcher: "killactive", args: "" }),
        "fullscreen-window": () => ({ dispatcher: "fullscreen", args: "0" }),
        "maximize-column": () => ({ dispatcher: "fullscreen", args: "1" }),
        "maximize-window-to-edges": () => ({ dispatcher: "fullscreen", args: "1" }),
        "toggle-window-floating": () => ({ dispatcher: "togglefloating", args: "" }),
        "focus-column-left": () => ({ dispatcher: "movefocus", args: "l" }),
        "focus-column-right": () => ({ dispatcher: "movefocus", args: "r" }),
        "focus-window-up": () => ({ dispatcher: "movefocus", args: "u" }),
        "focus-window-down": () => ({ dispatcher: "movefocus", args: "d" }),
        "focus-column-or-monitor-left": () => ({ dispatcher: "movefocus", args: "l" }),
        "focus-column-or-monitor-right": () => ({ dispatcher: "movefocus", args: "r" }),
        "focus-window-or-monitor-up": () => ({ dispatcher: "movefocus", args: "u" }),
        "focus-window-or-monitor-down": () => ({ dispatcher: "movefocus", args: "d" }),
        "focus-window-or-workspace-up": () => ({ dispatcher: "movefocus", args: "u" }),
        "focus-window-or-workspace-down": () => ({ dispatcher: "movefocus", args: "d" }),
        "move-column-left": () => ({ dispatcher: "layoutmsg", args: "swapcol l" }),
        "move-column-right": () => ({ dispatcher: "layoutmsg", args: "swapcol r" }),
        "move-column-left-or-to-monitor-left": () => ({ dispatcher: "layoutmsg", args: "swapcol l" }),
        "move-column-right-or-to-monitor-right": () => ({ dispatcher: "layoutmsg", args: "swapcol r" }),
        "move-window-up": () => ({ dispatcher: "movewindow", args: "u" }),
        "move-window-down": () => ({ dispatcher: "movewindow", args: "d" }),
        "move-window-up-or-to-workspace-up": () => ({ dispatcher: "movewindow", args: "u" }),
        "move-window-down-or-to-workspace-down": () => ({ dispatcher: "movewindow", args: "d" }),
        "consume-or-expel-window-left": () => ({ dispatcher: "layoutmsg", args: "movewindowto l" }),
        "consume-or-expel-window-right": () => ({ dispatcher: "layoutmsg", args: "movewindowto r" }),
        "consume-window-into-column": () => ({ dispatcher: "layoutmsg", args: "movewindowto l" }),
        "expel-window-from-column": () => ({ dispatcher: "layoutmsg", args: "movewindowto r" }),
        "center-column": () => ({ dispatcher: "layoutmsg", args: "centercolumn" }),
        "center-window": () => ({ dispatcher: "centerwindow", args: "" }),
        "switch-preset-column-width": () => ({ dispatcher: "layoutmsg", args: "colresize +conf" }),
        "switch-preset-column-width-back": () => ({ dispatcher: "layoutmsg", args: "colresize -conf" }),
        "set-column-width": () => {
            const p = percent(a[0]);
            return p === null ? { problem: "set-column-width in pixels is not in the port yet - use a percentage" }
                              : { dispatcher: "layoutmsg", args: `colresize ${p}` };
        },
        "focus-workspace": () => ({ dispatcher: "workspace", args: ws(a[0]) }),
        "focus-workspace-down": () => ({ dispatcher: "workspace", args: "r+1" }),
        "focus-workspace-up": () => ({ dispatcher: "workspace", args: "r-1" }),
        "focus-workspace-previous": () => ({ dispatcher: "workspace", args: "previous" }),
        "move-column-to-workspace": () => ({ dispatcher: node.props?.focus === false ? "movetoworkspacesilent" : "movetoworkspace", args: ws(a[0]) }),
        "move-window-to-workspace": () => ({ dispatcher: node.props?.focus === false ? "movetoworkspacesilent" : "movetoworkspace", args: ws(a[0]) }),
        "move-column-to-workspace-down": () => ({ dispatcher: "movetoworkspace", args: "r+1" }),
        "move-column-to-workspace-up": () => ({ dispatcher: "movetoworkspace", args: "r-1" }),
        "move-window-to-workspace-down": () => ({ dispatcher: "movetoworkspace", args: "r+1" }),
        "move-window-to-workspace-up": () => ({ dispatcher: "movetoworkspace", args: "r-1" }),
        "focus-monitor-left": () => ({ dispatcher: "focusmonitor", args: "l" }),
        "focus-monitor-right": () => ({ dispatcher: "focusmonitor", args: "r" }),
        "focus-monitor-up": () => ({ dispatcher: "focusmonitor", args: "u" }),
        "focus-monitor-down": () => ({ dispatcher: "focusmonitor", args: "d" }),
        "focus-monitor-next": () => ({ dispatcher: "focusmonitor", args: "+1" }),
        "focus-monitor-previous": () => ({ dispatcher: "focusmonitor", args: "-1" }),
        "toggle-overview": () => ({ dispatcher: "global", args: "caelestia:overview" }),
        "open-overview": () => ({ dispatcher: "global", args: "caelestia:overview" }),
        "screenshot": () => ({ dispatcher: "global", args: "caelestia:screenshot" }),
        "screenshot-screen": () => ({ dispatcher: "global", args: "caelestia:screenshot" }),
        "screenshot-window": () => ({ dispatcher: "global", args: "caelestia:screenshot" }),
        "quit": () => ({ problem: "quit would sign you out of Windows - not bound" }),
        "power-off-monitors": () => ({ problem: "turning monitors off is left to Windows" }),
    };
    const f = table[node.name];
    return f ? f() : { problem: `niri action ${node.name} is not in the Windows port yet` };
}

// ------------------------------------------------------------ the whole config -> HyprConf's shapes
// returns { options, binds: [{ keys, dispatcher, args, repeat, description }], execOnce, rules: [{match, effects, text}],
//           floatLines, notes: [{ text, why }] }
function convert(tree) {
    const out = { options: { "general:layout": "scrolling" }, binds: [], execOnce: [], rules: [], floatLines: [], notes: [] };
    const top = { children: tree };
    const note = (text, why) => out.notes.push({ text, why });

    // input { mod-key "Alt" } - niri's Mod is Super unless you say otherwise
    const input = child(top, "input");
    const modKey = String(arg(input, "mod-key", "Super"));
    // niri moves windows with Mod + drag out of the box
    out.dragMod = /^alt$/i.test(modKey) ? "alt" : /^(super|win)$/i.test(modKey) ? "super" : "";
    if (input) note("input { ... }", "keyboard, mouse and touchpad settings are Windows' own - ignored");

    const layout = child(top, "layout");
    if (layout) {
        const gaps = arg(layout, "gaps", null);
        if (typeof gaps === "number") {
            out.options["general:gaps_in"] = String(Math.round(gaps / 2));
            out.options["general:gaps_out"] = String(gaps);
        }
        const center = String(arg(layout, "center-focused-column", "never"));
        out.options["scrolling:focus_fit_method"] = center === "always" ? "0" : "1";
        const dcw = child(layout, "default-column-width");
        const prop = dcw ? arg(dcw, "proportion", null) : null;
        if (typeof prop === "number") out.options["scrolling:column_width"] = String(prop);
        else if (dcw && has(dcw, "fixed")) note("default-column-width { fixed ... }", "fixed pixel widths are not in the port yet - 0.5 is used");
        const presets = children(child(layout, "preset-column-widths"), "proportion").map(p => p.args[0]).filter(x => typeof x === "number");
        if (presets.length) out.options["scrolling:explicit_column_widths"] = presets.join(", ");
        // the focus ring and the border both become the port's window border (the ring wins when both are on)
        const ring = child(layout, "focus-ring");
        const border = child(layout, "border");
        const on = b => b && !has(b, "off");
        const shown = on(ring) ? ring : on(border) ? border : null;
        if (shown) {
            out.options["general:border_size"] = String(arg(shown, "width", 4));
            const ac = arg(shown, "active-color", null);
            const ic = arg(shown, "inactive-color", null);
            if (ac) out.options["general:col.active_border"] = String(ac);
            if (ic) out.options["general:col.inactive_border"] = String(ic);
        } else if (ring || border) {
            out.options["general:border_size"] = "0";
        }
        for (const c of layout.children)
            if (!["gaps", "center-focused-column", "default-column-width", "preset-column-widths", "focus-ring", "border"].includes(c.name))
                note(`layout { ${c.name} ... }`, "not in the Windows port yet");
    }

    const anim = child(top, "animations");
    if (anim) {
        if (has(anim, "off")) out.options["animations:enabled"] = "false";
        const slow = arg(anim, "slowdown", 1);
        // niri's window movement is ~0.25 s; Hyprland speed is in deciseconds
        out.options["animations:animation:windowsMove"] = `windowsMove, 1, ${(2.5 * (typeof slow === "number" ? slow : 1)).toFixed(2)}, default`;
    }

    for (const sp of children(top, "spawn-at-startup")) out.execOnce.push(sp.args.map(quote).join(" "));
    for (const sp of children(top, "spawn-sh-at-startup")) out.execOnce.push(String(sp.args[0] ?? ""));

    for (const r of children(top, "window-rule")) {
        const rule = { match: {}, effects: {}, text: "window-rule" };
        const m = child(r, "match");
        if (m) {
            if (m.props["app-id"] !== undefined) rule.match.class = String(m.props["app-id"]);
            if (m.props.title !== undefined) rule.match.title = String(m.props.title);
            const more = Object.keys(m.props).filter(k => !["app-id", "title"].includes(k));
            if (more.length) rule.match.unsupported = more.join(",");
        }
        if (children(r, "match").length > 1) note("window-rule with several match lines", "only the first match is used");
        const flag = name => { const c = child(r, name); return c ? (c.args.length ? c.args[0] !== false : true) : undefined; };
        if (flag("open-floating") !== undefined) rule.effects.float = flag("open-floating");
        if (flag("open-maximized") === true) rule.effects.maximize = true;
        if (flag("open-fullscreen") === true) rule.effects.fullscreen = true;
        const ow = arg(r, "open-on-workspace", null);
        if (ow !== null) rule.effects.workspace = typeof ow === "number" ? String(ow) : `name:${ow}`;
        if (Object.keys(rule.effects).length && Object.keys(rule.match).length) {
            rule.text = `window-rule ${JSON.stringify(rule.match)}`;
            out.rules.push(rule);
            if (rule.effects.float === true) {
                if (rule.match.class) out.floatLines.push(`float, class:${rule.match.class}`);
                if (rule.match.title) out.floatLines.push(`float, title:${rule.match.title}`);
            }
        }
        for (const c of r.children)
            if (!["match", "exclude", "open-floating", "open-maximized", "open-fullscreen", "open-on-workspace"].includes(c.name))
                note(`window-rule { ${c.name} ... }`, "not in the Windows port yet");
    }

    for (const b of children(top, "binds")[0]?.children ?? []) {
        const act = b.children[0];
        if (!act) continue;
        const r = action(act);
        const keys = String(b.name).replace(/\bMod\b/g, modKey);
        if (r.problem) { note(`${b.name} { ${act.name} }`, r.problem); continue; }
        if (/Wheel|Mouse|Touchpad/i.test(keys)) { note(`${b.name} { ${act.name} }`, "mouse and touchpad binds are done by Windows"); continue; }
        out.binds.push({ keys, dispatcher: r.dispatcher, args: r.args, repeat: b.props.repeat !== false,
                         description: b.props["hotkey-overlay-title"] ? String(b.props["hotkey-overlay-title"]) : "" });
    }

    for (const c of tree)
        if (!["input", "layout", "animations", "spawn-at-startup", "spawn-sh-at-startup", "window-rule", "binds"].includes(c.name))
            note(`${c.name} ...`, "Windows handles this itself, or it is not in the port yet - ignored");
    return out;
}
