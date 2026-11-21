// Shared parts of the shell ports (Noctalia has its own older script; DMS and Zenith use this).
// A port copies another Quickshell shell's QML into Caelestia's config folder with Windows fixes, and turns
// its shell.qml into a host Scope whose parts are switched by Caelestia's WinSettings (Mix & match).
const fs = require("fs");
const path = require("path");

const HOME_EXPR = '((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\\\").join("/"))';

function addImport(t, line) {
    if (t.includes(line)) return t;
    if (/^import QtQuick\s*$/m.test(t)) return t.replace(/^(import QtQuick\s*)$/m, `$1\n${line}`);
    return t.replace(/^(import [^\n]+)$/m, `$1\n${line}`);
}

// fixes every Quickshell shell needs on Windows
function common(t, { rootExpr }) {
    if (rootExpr) t = t.replace(/\bQuickshell\.shellDir\b/g, rootExpr);
    t = t.replace(/\bQuickshell\.env\("HOME"\)/g, HOME_EXPR);
    // "file://" + "C:/..." is not a valid URL
    t = t.replace(/(["'`])file:\/\/(?!\/)/g, "$1file:///");
    // mkdir -p -> FsUtils.makePath
    if (/Quickshell\.execDetached\(\["mkdir", "-p", /.test(t)) {
        t = t.replace(/Quickshell\.execDetached\(\["mkdir", "-p", ([^\]]+)\]\)/g,
            (m, a) => a.includes(",") ? `[${a}].forEach(p => WinFs.FsUtils.makePath(p))` : `WinFs.FsUtils.makePath(${a})`);
        t = addImport(t, "import Quickshell.Windows as WinFs");
    }
    // layer-shell properties Windows' PanelWindow has itself
    t = t.replace(/\bWlrLayershell\.exclusionMode:/g, "exclusionMode:");
    t = t.replace(/\bWlrLayershell\.(anchors|margins)\s*\{/g, "$1 {");
    t = t.replace(/\bWlrLayershell\.(anchors|margins)\.(top|bottom|left|right):/g, "$1.$2:");
    t = t.replace(/\bWlrLayershell\.(exclusiveZone|margins):/g, "$1:");
    t = t.replace(/^(\s*)(BackgroundEffect\.blurRegion:.*)$/gm, "$1// Windows: no compositor blur - $2");
    // Wayland's IdleMonitor -> Caelestia's Windows stand-in (config/shims)
    if (/\bIdleMonitor \{/.test(t)) t = addImport(t, "import qs.shims");
    // PAM -> Caelestia's PasswordCheck stand-in (config/shims)
    if (/\bPamContext \{|\bPamResult\./.test(t)) t = addImport(t, "import qs.shims");
    // modules that do not exist on Windows (their users are replaced in overrides)
    t = t.replace(/^import Quickshell\.Services\.(Pam|Polkit)\s*\n/gm, "");
    return t;
}

// wraps the first `Name {` block (at line start, any indent) in a LazyLoader that is active only when cond holds;
// returns the text unchanged when the block is not found
function gate(t, name, cond, loaderId) {
    const re = new RegExp(`\\n([ \\t]+)${name} \\{`);
    const m = re.exec(t);
    if (!m) return t;
    const indent = m[1];
    const start = m.index + 1;
    let i = t.indexOf("{", start);
    let depth = 0;
    for (; i < t.length; i++) {
        if (t[i] === "{") depth++;
        else if (t[i] === "}" && --depth === 0) break;
    }
    const block = t.slice(start, i + 1);
    const inner = block.split("\n").map(l => (l.length ? "    " + l : l)).join("\n");
    const id = loaderId ? `${indent}    id: ${loaderId}\n` : "";
    return t.slice(0, start) + `${indent}LazyLoader {\n${id}${indent}    active: ${cond}\n${inner}\n${indent}}` + t.slice(i + 1);
}

function copyTree(srcRoot, rel, outRoot, opts) {
    for (const name of fs.readdirSync(path.join(srcRoot, rel))) {
        if (opts.skip && opts.skip.has(name)) continue;
        const r = path.join(rel, name);
        const rs = r.replace(/\\/g, "/");
        if (opts.skipPaths && opts.skipPaths.some(p => rs === p || rs.startsWith(p + "/"))) continue;
        const src = path.join(srcRoot, r);
        let st;
        try {
            st = fs.statSync(src);
        } catch (e) {
            continue; // dangling link
        }
        const dst = path.join(outRoot, r);
        if (st.isDirectory()) {
            fs.mkdirSync(dst, { recursive: true });
            copyTree(srcRoot, r, outRoot, opts);
        } else if (opts.special && opts.special[rs]) {
            opts.special[rs](src, outRoot);
        } else if (/\.(qml|js)$/.test(name)) {
            fs.writeFileSync(dst, opts.transform(rs, fs.readFileSync(src, "utf8")));
        } else {
            fs.copyFileSync(src, dst);
        }
    }
}

function applyOverrides(dir, out, transform) {
    if (!fs.existsSync(dir)) return;
    const walk = rel => {
        for (const name of fs.readdirSync(path.join(dir, rel))) {
            const r = path.join(rel, name);
            if (fs.statSync(path.join(dir, r)).isDirectory()) {
                fs.mkdirSync(path.join(out, r), { recursive: true });
                walk(r);
            } else if (!name.endsWith(".part")) {
                fs.copyFileSync(path.join(dir, r), path.join(out, r));
            }
        }
    };
    walk("");
}

// empty (not remove: the folder may be open somewhere) and refill each output
function emptyDir(out) {
    fs.mkdirSync(out, { recursive: true });
    for (const name of fs.readdirSync(out)) fs.rmSync(path.join(out, name), { recursive: true, force: true });
}

module.exports = { HOME_EXPR, addImport, common, gate, copyTree, applyOverrides, emptyDir };
