pragma Singleton

// Waybar-compatible helpers: a style.css reader (the subset bars use) and Waybar's format strings.
import QtQuick
import Quickshell
import qs.services

Singleton {
    id: root

    // ---------------- config (JSON with comments, like Waybar's config.jsonc) ----------------
    function parseConfig(text: string): var {
        // strip // and /* */ comments outside strings, then trailing commas
        let out = "", i = 0, inStr = false;
        while (i < text.length) {
            const c = text[i], n = text[i + 1];
            if (inStr) {
                out += c;
                if (c === "\\") { out += n ?? ""; i += 2; continue; }
                if (c === '"') inStr = false;
                i++;
                continue;
            }
            if (c === '"') { inStr = true; out += c; i++; continue; }
            if (c === "/" && n === "/") { while (i < text.length && text[i] !== "\n") i++; continue; }
            if (c === "/" && n === "*") { i += 2; while (i < text.length && !(text[i] === "*" && text[i + 1] === "/")) i++; i += 2; continue; }
            out += c;
            i++;
        }
        out = out.replace(/,(\s*[}\]])/g, "$1");
        const v = JSON.parse(out);
        return Array.isArray(v) ? v : [v];
    }

    // ---------------- style.css ----------------
    // returns { rules: [{ sel, props }], colors: { name: value } }
    function parseCss(text: string): var {
        const colors = {};
        text = text.replace(/\/\*[\s\S]*?\*\//g, "");
        text = text.replace(/@define-color\s+([\w-]+)\s+([^;]+);/g, (m, name, val) => { colors[name] = val.trim(); return ""; });
        text = text.replace(/@import[^;]*;/g, "");
        const rules = [];
        const re = /([^{}]+)\{([^}]*)\}/g;
        let m;
        while ((m = re.exec(text)) !== null) {
            const props = {};
            for (const decl of m[2].split(";")) {
                const k = decl.indexOf(":");
                if (k < 0) continue;
                props[decl.slice(0, k).trim().toLowerCase()] = decl.slice(k + 1).trim().replace(/\s*!important$/, "");
            }
            for (const sel of m[1].split(","))
                rules.push({ sel: sel.trim().replace(/\s+/g, " "), props });
        }
        return { rules, colors };
    }

    // Caelestia's live colours, usable in style.css as @primary, @surface, @on_surface ...
    function themeColor(name: string): string {
        const key = "m3" + name.replace(/(^|_)([a-z])/g, (m, a, b) => b.toUpperCase());
        const c = Colours.palette[key];
        return c !== undefined ? c.toString() : "";
    }

    function color(css: var, value: string): var {
        if (!value) return undefined;
        value = value.trim();
        const ref = value.match(/^@([\w-]+)$/);
        if (ref) return color(css, css.colors[ref[1]] ?? themeColor(ref[1]));
        const alpha = value.match(/^alpha\(\s*(.+?)\s*,\s*([\d.]+)\s*\)$/);
        if (alpha) {
            const c = color(css, alpha[1]);
            return c === undefined ? undefined : Qt.alpha(c, parseFloat(alpha[2]));
        }
        const rgb = value.match(/^rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*(?:,\s*([\d.]+)\s*)?\)$/);
        if (rgb) return Qt.rgba(rgb[1] / 255, rgb[2] / 255, rgb[3] / 255, rgb[4] === undefined ? 1 : parseFloat(rgb[4]));
        if (value === "transparent" || value === "none") return "transparent";
        return value;   // #hex or a named colour
    }

    // CSS "font-family: A, "B C", sans-serif" -> ["A", "B C", "sans-serif"] for Qt's fallback list
    function families(value: string): var {
        return String(value ?? "").split(",").map(f => f.trim().replace(/^["']|["']$/g, "")).filter(f => f.length);
    }

    // the first family in a CSS list that is installed (Qt's QML font takes one family)
    readonly property var installed: Qt.fontFamilies()
    function family(value: string): string {
        const want = families(value);
        for (const f of want) {
            const hit = installed.find(i => i.toLowerCase() === f.toLowerCase());
            if (hit) return hit;
        }
        return want.length && !["sans-serif", "serif", "monospace"].includes(want[0]) ? want[0] : "Segoe UI";
    }

    // icons inside text: private-use characters (Material Symbols, Nerd Fonts) are drawn with an icon font
    function iconFamily(value: string, supplementary: bool): string {
        const list = families(value).map(f => installed.find(i => i.toLowerCase() === f.toLowerCase())).filter(f => !!f);
        const nerd = list.find(f => /nerd|symbols/i.test(f) && !/material/i.test(f)) ?? installed.find(i => /nerd font/i.test(i));
        const mat = list.find(f => /material/i.test(f)) ?? installed.find(i => /^material symbols/i.test(i));
        return supplementary ? (nerd ?? mat ?? "") : (mat ?? nerd ?? "");
    }
    function decorate(text: string, fontValue: string): string {
        if (!text) return "";
        let out = "";
        for (const ch of String(text)) {
            const cp = ch.codePointAt(0);
            const pua = cp >= 0xE000 && cp <= 0xF8FF, supp = cp >= 0xF0000;
            if (pua || supp) {
                const f = iconFamily(fontValue, supp);
                out += f ? `<font face="${f}">${ch}</font>` : ch;
            } else {
                out += ch;
            }
        }
        return out;
    }

    // a label split into runs: [{ text, font }] - icon characters get the icon font, the rest the text font
    function runs(text: string, fontValue: string): var {
        const out = [];
        const base = family(fontValue);
        for (const ch of String(text ?? "").replace(/<[^>]+>/g, "")) {
            const cp = ch.codePointAt(0);
            const icon = (cp >= 0xE000 && cp <= 0xF8FF) || cp >= 0xF0000;
            const f = icon ? (iconFamily(fontValue, cp >= 0xF0000) || base) : base;
            if (out.length && out[out.length - 1].font === f) out[out.length - 1].text += ch;
            else out.push({ text: ch, font: f });
        }
        return out;
    }

    function px(value: string, fallback: real): real {
        if (!value) return fallback;
        const n = parseFloat(value);
        return isNaN(n) ? fallback : (value.endsWith("em") ? n * 13 : value.endsWith("pt") ? n * 1.33 : n);
    }

    // CSS box shorthand "4px 8px" -> { t, r, b, l }
    function box(value: string): var {
        if (!value) return { t: 0, r: 0, b: 0, l: 0 };
        const p = value.split(/\s+/).map(v => px(v, 0));
        if (p.length === 1) return { t: p[0], r: p[0], b: p[0], l: p[0] };
        if (p.length === 2) return { t: p[0], r: p[1], b: p[0], l: p[1] };
        if (p.length === 3) return { t: p[0], r: p[1], b: p[2], l: p[1] };
        return { t: p[0], r: p[1], b: p[2], l: p[3] };
    }

    // merged properties for an element: `chain` is from outer to inner, e.g.
    // [ {id:"waybar", tag:"window"}, {cls:"modules-left"}, {id:"workspaces"}, {tag:"button", classes:["active"]} ]
    function matches(sel: string, chain: var): bool {
        const parts = sel.replace(/\s*>\s*/g, " ").split(" ").filter(s => s.length && s !== "*" || s === "*");
        let ci = chain.length - 1;
        for (let pi = parts.length - 1; pi >= 0; pi--) {
            const part = parts[pi];
            const one = el => {
                if (part === "*") return true;
                const m = part.replace(/:[\w-]+(\([^)]*\))?/g, "").match(/^([a-zA-Z]*)((?:[#.][\w-]+)*)$/);
                if (!m) return false;
                if (m[1] && m[1] !== el.tag) return false;
                for (const tok of (m[2].match(/[#.][\w-]+/g) ?? [])) {
                    if (tok[0] === "#" && el.id !== tok.slice(1)) return false;
                    if (tok[0] === "." && !(el.classes ?? []).includes(tok.slice(1))) return false;
                }
                return !part.includes(":hover");
            };
            if (pi === parts.length - 1) {
                if (!one(chain[ci])) return false;
                ci--;
            } else {
                while (ci >= 0 && !one(chain[ci])) ci--;
                if (ci < 0) return false;
                ci--;
            }
        }
        return true;
    }

    function style(css: var, chain: var): var {
        const out = {};
        if (!css) return out;
        for (const r of css.rules)
            if (matches(r.sel, chain))
                Object.assign(out, r.props);
        return out;
    }

    // ---------------- format strings ----------------
    function strftime(fmt: string, d: date): string {
        const p = (n, w) => String(n).padStart(w ?? 2, "0");
        const days = Qt.locale().standaloneDayName;
        return fmt.replace(/%([a-zA-Z%])/g, (m, c) => ({
            H: p(d.getHours()), M: p(d.getMinutes()), S: p(d.getSeconds()),
            I: p(d.getHours() % 12 || 12), p: d.getHours() < 12 ? "AM" : "PM", P: d.getHours() < 12 ? "am" : "pm",
            a: Qt.locale().dayName(d.getDay(), Locale.ShortFormat), A: Qt.locale().dayName(d.getDay(), Locale.LongFormat),
            b: Qt.locale().monthName(d.getMonth(), Locale.ShortFormat), B: Qt.locale().monthName(d.getMonth(), Locale.LongFormat),
            d: p(d.getDate()), e: String(d.getDate()).padStart(2, " "), m: p(d.getMonth() + 1),
            y: p(d.getFullYear() % 100), Y: String(d.getFullYear()), j: p(Math.floor((d - new Date(d.getFullYear(), 0, 0)) / 86400000), 3),
            R: `${p(d.getHours())}:${p(d.getMinutes())}`, T: `${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`,
            D: `${p(d.getMonth() + 1)}/${p(d.getDate())}/${p(d.getFullYear() % 100)}`, F: `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}`,
            "%": "%"
        })[c] ?? m);
    }

    // "{volume}% {icon}" with values { volume: 40, icon: "" }; "{}" -> values[""]; "{:%H:%M}" -> clock
    function format(fmt: string, values: var, now: date): string {
        if (fmt === undefined || fmt === null) return "";
        return String(fmt).replace(/\{([\w-]*)(?::([^}]*))?\}/g, (m, key, spec) => {
            if (key === "" && spec !== undefined && now) return strftime(spec.replace(/^%/, "%"), now);
            const v = values[key];
            if (v === undefined) return "";
            if (spec && typeof v === "number") {
                const prec = spec.match(/\.(\d+)f/);
                if (prec) return v.toFixed(parseInt(prec[1]));
                const width = spec.match(/>?(\d+)/);
                if (width) return String(Math.round(v)).padStart(parseInt(width[1]), " ");
            }
            return typeof v === "number" ? String(Math.round(v)) : String(v);
        });
    }

    // format-icons: a list (picked by percentage) or a map (picked by state/name)
    // config lists come back from Qt as list objects, which Array.isArray does not see
    function isList(v: var): bool {
        return Array.isArray(v) || (!!v && typeof v === "object" && typeof v.length === "number" && typeof v !== "string");
    }

    function icon(icons: var, percent: real, key: string): string {
        if (!icons) return "";
        if (isList(icons)) {
            if (!icons.length) return "";
            const i = Math.min(icons.length - 1, Math.max(0, Math.floor(percent / 100 * icons.length)));
            return icons[i];
        }
        if (key && icons[key] !== undefined) return isList(icons[key]) ? icon(icons[key], percent, "") : icons[key];
        if (icons["default"] !== undefined) return isList(icons["default"]) ? icon(icons["default"], percent, "") : icons["default"];
        return "";
    }

    // Waybar runs on-click etc. with sh -c; here with cmd /c (or a list as given)
    function run(command: var): void {
        if (!command) return;
        Quickshell.execDetached(isList(command) ? Array.from(command) : ["cmd.exe", "/c", String(command)]);
    }
}
