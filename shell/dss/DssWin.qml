pragma Singleton
// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows helper added by dotfiles/dss-port.js. DSS runs bash and its ~/.local/bin/rice-* scripts for almost
// everything; on Windows there is no bash, so every Process / execDetached of the port comes here instead
// (DssProc.qml): a small sh interpreter (words, quotes, $VAR, $1.., "$@", $(..), ; && || | > >> < [ test ]) runs
// the command in JS, and the rice-* programs it names are answered from Caelestia's services or done the
// Windows way. What has no Windows meaning (nmcli, hyprctl, swaync ...) gives no output and exit status 1.
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as WinFs
import Caelestia.State
import qs.services as Cae
import qs.modules.aethersettings as AS

Singleton {
    id: root

    readonly property string home: (Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")
    readonly property string rice: home + "/.config/rice"
    readonly property string cache: home + "/.cache/rice"
    readonly property string temp: (Quickshell.env("TEMP") || home + "/AppData/Local/Temp").split("\\").join("/")
    property var told: ({})          // programs already reported as Linux-only (once each)

    // ---- entry points -------------------------------------------------------------------------------------

    // a Process: { out, status, stream } - stream: a program that would run on and on (it never ends here)
    function exec(argv: var): var {
        try {
            return root.runArgv(argv, false);
        } catch (e) {
            console.warn("dss: command failed:", e, JSON.stringify(argv).slice(0, 200));
            return { out: "", status: 1, stream: false };
        }
    }

    // Quickshell.execDetached
    function run(argv: var): void {
        root.exec(argv);
    }

    // open one of DSS's panels the way its scripts do: a line in ~/.cache/rice/<name>
    function trigger(name: string, rest: string): void {
        root.writeFile(root.cache + "/" + name, (Date.now() / 1000).toFixed(3) + (rest !== "" ? " " + rest : "") + "\n");
    }

    // ---- the interpreter ------------------------------------------------------------------------------------

    readonly property var streams: ["python3", "python", "rice-cava", "rice-stats", "niri", "rice-ws-watch", "bluetoothctl", "rice-notifyd", "rice-canvasd"]

    function runArgv(argv: var, inner: bool): var {
        const a = (Array.isArray(argv) ? argv : [argv]).map(x => String(x ?? ""));
        if (!a.length)
            return { out: "", status: 0, stream: false };
        const prog = root.base(a[0]);
        if ((prog === "bash" || prog === "sh") && a[1] === "-c")
            return root.script(a[2] || "", a.slice(3), {});
        if (prog === "bash" || prog === "sh")
            return { out: "", status: 1, stream: false };
        if (root.streams.includes(prog))
            return { out: "", status: 1, stream: true };
        const r = root.cmd(a, "", {});
        return { out: r.out, status: r.status, stream: false };
    }

    // run sh text; args[0] is $0
    function script(text: string, args: var, env: var): var {
        const e = Object.assign({
            HOME: root.home, USER: Quickshell.env("USERNAME") || "", ID: "windows", ID_LIKE: "windows",
            NAME: "Windows", PRETTY_NAME: "Windows 11", XDG_SESSION_TYPE: "windows", "?": "0"
        }, env);
        const ctx = { args: args, env: e };
        const items = root.splitTop(text);
        let out = "", status = 0, skip = false;
        for (let i = 0; i < items.length; i++) {
            const it = items[i];
            if (!skip) {
                const r = root.pipeline(it.cmds, ctx);
                if (r.unsupported)
                    return { out: out, status: 1, stream: false };
                out += r.out;
                status = r.status;
                ctx.env["?"] = String(status);
            }
            // after this item: && runs the next only on success, || only on failure
            // a skipped part keeps the status of the last part that ran (a && b || c: c runs when a failed)
            skip = it.sep === "&&" ? status !== 0 : it.sep === "||" ? status === 0 : false;
        }
        return { out: out, status: status, stream: false };
    }

    // top level: items split at ; & newline && ||, each a list of commands split at |
    function splitTop(s: string): var {
        const items = [];
        let cmds = [], cur = "", i = 0, q = "", depth = 0;
        const push = sep => {
            cmds.push(cur);
            cur = "";
            if (cmds.some(c => c.trim() !== ""))
                items.push({ cmds: cmds.filter(c => c.trim() !== ""), sep: sep });
            cmds = [];
        };
        while (i < s.length) {
            const c = s[i];
            if (q === "'") {
                if (c === "'")
                    q = "";
                cur += c;
                i++;
                continue;
            }
            if (c === "\\") {
                cur += s.substr(i, 2);
                i += 2;
                continue;
            }
            if (q === "\"") {
                if (c === "\"" && depth === 0)
                    q = "";
                else if (c === "$" && s[i + 1] === "(")
                    depth++;
                else if (c === ")" && depth > 0)
                    depth--;
                cur += c;
                i++;
                continue;
            }
            if (c === "'" || c === "\"") {
                q = c;
                cur += c;
                i++;
                continue;
            }
            if (c === "(") {
                depth++;
                cur += c;
                i++;
                continue;
            }
            if (c === ")") {
                depth = Math.max(0, depth - 1);
                cur += c;
                i++;
                continue;
            }
            if (depth > 0) {
                cur += c;
                i++;
                continue;
            }
            if (c === "#" && (cur === "" || /\s$/.test(cur))) {
                while (i < s.length && s[i] !== "\n")
                    i++;
                continue;
            }
            if (c === "&" && s[i + 1] === "&") {
                push("&&");
                i += 2;
                continue;
            }
            if (c === "|" && s[i + 1] === "|") {
                push("||");
                i += 2;
                continue;
            }
            if (c === "|") {
                cmds.push(cur);
                cur = "";
                i++;
                continue;
            }
            // & as a separator, not in >&2 / 2>&1 / &>
            if (c === "&" && !/[<>]$/.test(cur) && s[i + 1] !== ">") {
                push(";");
                i++;
                continue;
            }
            if (c === ";" || c === "\n") {
                push(";");
                i++;
                continue;
            }
            cur += c;
            i++;
        }
        push(";");
        return items;
    }

    readonly property var keywords: ["if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done", "case", "esac", "{", "}", "function", "[[", "select", "(("]

    function pipeline(cmds: var, ctx: var): var {
        let input = "", out = "", status = 0;
        for (let i = 0; i < cmds.length; i++) {
            const w = root.words(cmds[i], ctx);
            if (w.unsupported || (w.argv.length && root.keywords.includes(w.argv[0]) && !w.quoted0)) {
                if (!root.told["sh:" + cmds[i].trim().split(/\s+/)[0]]) {
                    root.told["sh:" + cmds[i].trim().split(/\s+/)[0]] = true;
                    console.info("dss: shell syntax not run on Windows:", cmds[i].trim().slice(0, 120));
                }
                return { unsupported: true, out: "", status: 1 };
            }
            let argv = w.argv;
            // NAME=value ... : a lone assignment sets it, before a command it is only for that command
            const env = {};
            while (argv.length && /^[A-Za-z_][A-Za-z0-9_]*=/.test(argv[0])) {
                const k = argv[0].slice(0, argv[0].indexOf("="));
                env[k] = argv[0].slice(k.length + 1);
                argv = argv.slice(1);
            }
            if (!argv.length) {
                Object.assign(ctx.env, env);
                status = 0;
                out = "";
                input = "";
                continue;
            }
            for (let r of w.redirs)
                if (r.op === "<") {
                    const t = root.readFile(r.target);
                    input = t === null ? "" : t;
                }
            const res = root.cmd(argv, input, Object.assign({}, ctx.env, env));
            let o = res.out;
            for (let r of w.redirs) {
                if (r.op === "<" || r.fd !== 1)
                    continue;
                if (r.target === "&2" || r.target === "/dev/null") {
                    o = "";
                    continue;
                }
                if (r.target === "&1")
                    continue;
                const old = r.op === ">>" ? (root.readFile(r.target) || "") : "";
                root.writeFile(root.path(r.target), old + o);
                o = "";
            }
            status = res.status;
            input = o;
            out = o;
        }
        return { out: out, status: status };
    }

    // one command's words: quotes, escapes, $ expansions, ~, redirections
    function words(s: string, ctx: var): var {
        const argv = [], redirs = [], literal = [];
        let cur = null, i = 0, quoted0 = false;
        const end = () => {
            if (cur !== null) {
                argv.push(cur);
                literal.push(false);
            }
            cur = null;
        };
        const add = x => {
            cur = (cur === null ? "" : cur) + x;
        };
        while (i < s.length) {
            const c = s[i];
            if (/\s/.test(c)) {
                end();
                i++;
                continue;
            }
            if (c === "'") {
                const j = s.indexOf("'", i + 1);
                add(s.slice(i + 1, j < 0 ? s.length : j));
                if (!argv.length)
                    quoted0 = true;
                i = j < 0 ? s.length : j + 1;
                continue;
            }
            if (c === "\"") {
                if (!argv.length)
                    quoted0 = true;
                if (cur === null)
                    cur = "";
                i++;
                while (i < s.length && s[i] !== "\"") {
                    if (s[i] === "\\" && "\"\\$`\n".includes(s[i + 1])) {
                        add(s[i + 1]);
                        i += 2;
                    } else if (s[i] === "$") {
                        const x = root.dollar(s, i, ctx, true);
                        if (x.unsupported)
                            return { unsupported: true };
                        if (x.split) {
                            // "$@": each argument its own word
                            const parts = x.split;
                            if (parts.length === 0) {
                                if (cur === "")
                                    cur = null;
                            } else {
                                add(parts[0]);
                                for (let k = 1; k < parts.length; k++) {
                                    end();
                                    cur = parts[k];
                                }
                            }
                        } else
                            add(x.val);
                        i = x.next;
                    } else if (s[i] === "`") {
                        return { unsupported: true };
                    } else {
                        add(s[i]);
                        i++;
                    }
                }
                i++;
                continue;
            }
            if (c === "\\") {
                if (s[i + 1] !== "\n")
                    add(s[i + 1] || "");
                i += 2;
                continue;
            }
            if (c === "$") {
                const x = root.dollar(s, i, ctx, false);
                if (x.unsupported)
                    return { unsupported: true };
                if (x.split) {
                    end();
                    for (let p of x.split) {
                        argv.push(p);
                        literal.push(false);
                    }
                } else {
                    // unquoted: split on blanks
                    const parts = x.val.split(/\s+/);
                    for (let k = 0; k < parts.length; k++) {
                        if (k > 0)
                            end();
                        if (parts[k] !== "")
                            add(parts[k]);
                    }
                }
                i = x.next;
                continue;
            }
            if (c === "`" || (c === "<" && s[i + 1] === "<"))
                return { unsupported: true };
            if (c === ">" || c === "<" || (c === "&" && s[i + 1] === ">")) {
                let fd = c === "<" ? 0 : 1, op = c;
                if (cur !== null && /^[0-9]$/.test(cur)) {
                    fd = Number(cur);
                    cur = null;
                } else
                    end();
                if (c === "&") {
                    fd = 1;
                    op = ">";
                    i++;
                }
                i++;
                if (op === ">" && s[i] === ">") {
                    op = ">>";
                    i++;
                }
                if (s[i] === "&") {
                    // >&2, 2>&1
                    let j = i + 1;
                    while (j < s.length && /[0-9-]/.test(s[j]))
                        j++;
                    redirs.push({ fd: fd, op: op, target: "&" + s.slice(i + 1, j) });
                    i = j;
                    continue;
                }
                while (i < s.length && /\s/.test(s[i]))
                    i++;
                const tok = s.slice(i).match(/^(?:"[^"]*"|'[^']*'|[^\s<>|;&])*/)[0];
                const tw = root.words(tok, ctx);
                redirs.push({ fd: fd, op: op, target: tw.argv && tw.argv.length ? tw.argv[0] : "" });
                i += tok.length;
                continue;
            }
            if (c === "~" && cur === null && (s[i + 1] === undefined || s[i + 1] === "/" || /\s/.test(s[i + 1]))) {
                add(root.home);
                i++;
                continue;
            }
            add(c);
            i++;
        }
        end();
        return { argv: argv, redirs: redirs, literal: literal, quoted0: quoted0 };
    }

    // $NAME ${NAME} ${NAME:-x} $1 $@ $# $? $(cmd)
    function dollar(s: string, i: int, ctx: var, inQuotes: bool): var {
        const args = ctx.args;
        const n = s[i + 1];
        if (n === "(" && s[i + 2] === "(")
            return { unsupported: true };
        if (n === "(") {
            // find the matching ) (quotes aware)
            let d = 1, j = i + 2, q = "";
            while (j < s.length && d > 0) {
                const c = s[j];
                if (q) {
                    if (c === "\\" && q === "\"")
                        j++;
                    else if (c === q)
                        q = "";
                } else if (c === "'" || c === "\"")
                    q = c;
                else if (c === "\\")
                    j++;
                else if (c === "(")
                    d++;
                else if (c === ")")
                    d--;
                j++;
            }
            const inner = s.slice(i + 2, j - 1);
            const r = root.script(inner, args, ctx.env);
            return { val: r.out.replace(/\n+$/, ""), next: j };
        }
        if (n === "{") {
            // the matching } (a default can hold another ${...}, as in ${PRETTY_NAME:-${NAME:-Linux}})
            let j = i + 2, d = 1;
            for (; j < s.length; j++) {
                if (s[j] === "\\") {
                    j++;
                    continue;
                }
                if (s[j] === "{" && s[j - 1] === "$")
                    d++;
                else if (s[j] === "}" && --d === 0)
                    break;
            }
            const body = s.slice(i + 2, j);
            if (body === "@" || body === "*")
                return inQuotes && body === "@" ? { split: args.slice(1), next: j + 1 } : { val: args.slice(1).join(" "), next: j + 1 };
            const m = body.match(/^([A-Za-z_][A-Za-z0-9_]*|[0-9]+|[#?])(?::?([-=+])(.*))?$/);
            if (!m)
                return { unsupported: true };
            let v = root.varOf(m[1], ctx);
            if (m[2] === "-" || m[2] === "=")
                v = v !== "" ? v : root.script("echo " + (m[3] || ""), args, ctx.env).out.replace(/\n$/, "");
            else if (m[2] === "+")
                v = v !== "" ? (m[3] || "") : "";
            return { val: v, next: j + 1 };
        }
        if (n === "@")
            return inQuotes ? { split: args.slice(1), next: i + 2 } : { val: args.slice(1).join(" "), next: i + 2 };
        if (n === "*")
            return { val: args.slice(1).join(" "), next: i + 2 };
        if (n !== undefined && /[0-9#?$!]/.test(n))
            return { val: root.varOf(n, ctx), next: i + 2 };
        const m = s.slice(i + 1).match(/^[A-Za-z_][A-Za-z0-9_]*/);
        if (!m)
            return { val: "$", next: i + 1 };
        return { val: root.varOf(m[0], ctx), next: i + 1 + m[0].length };
    }

    function varOf(name: string, ctx: var): string {
        if (/^[0-9]+$/.test(name))
            return String(ctx.args[Number(name)] ?? "");
        if (name === "#")
            return String(Math.max(0, ctx.args.length - 1));
        if (name === "$" || name === "!")
            return "1";
        const v = ctx.env[name];
        return v === undefined ? (Quickshell.env(name) || "") : String(v);
    }

    // ---- the programs -----------------------------------------------------------------------------------------

    function ok(out: var): var {
        return { out: out === undefined || out === null ? "" : String(out), status: 0 };
    }
    function fail(out: var): var {
        return { out: out ? String(out) : "", status: 1 };
    }

    function cmd(argv: var, input: string, env: var): var {
        let a = argv.slice();
        // wrappers: exec, setsid -f, timeout N, nice, env, command
        for (;;) {
            const p = root.base(a[0] || "");
            if (p === "exec" || p === "nice" || p === "command" && a[1] !== "-v")
                a = a.slice(1);
            else if (p === "setsid" || p === "nohup")
                a = a.slice(1).filter((x, k) => !(k === 0 && x === "-f"));
            else if (p === "timeout")
                a = a.slice(2);
            else if (p === "env")
                a = a.slice(1).filter(x => !/^[A-Za-z_]+=/.test(x));
            else
                break;
        }
        if (!a.length)
            return root.ok("");
        const prog = root.base(a[0]);
        const x = a.slice(1);
        switch (prog) {
        case "true":
        case ":":
        case ".":
        case "source":
        case "sleep":
        case "wait":
        case "disown":
        case "export":
        case "local":
        case "set":
        case "trap":
            return root.ok("");
        case "false":
            return root.fail("");
        case "exit":
            return { out: "", status: Number(x[0] || 0) };
        case "echo": {
            let nl = true, k = 0;
            while (x[k] === "-n" || x[k] === "-e") {
                if (x[k] === "-n")
                    nl = false;
                k++;
            }
            return root.ok(x.slice(k).join(" ") + (nl ? "\n" : ""));
        }
        case "printf":
            return root.ok(root.printf(x));
        case "cat": {
            const files = x.filter(f => f !== "-" && !/^-/.test(f));
            if (!files.length)
                return root.ok(input);
            let o = "", st = 0;
            for (let f of files) {
                const t = root.readFile(f);
                if (t === null)
                    st = 1;
                else
                    o += t;
            }
            return { out: o, status: st };
        }
        case "head":
        case "tail": {
            let n = 10;
            for (let k = 0; k < x.length; k++) {
                if (x[k] === "-n")
                    n = Number(x[++k]);
                else if (/^-[0-9]+$/.test(x[k]))
                    n = Number(x[k].slice(1));
            }
            const lines = input.split("\n");
            if (lines[lines.length - 1] === "")
                lines.pop();
            const pick = prog === "head" ? lines.slice(0, n) : lines.slice(-n);
            return root.ok(pick.length ? pick.join("\n") + "\n" : "");
        }
        case "mkdir":
            x.filter(p => !/^-/.test(p)).forEach(p => WinFs.FsUtils.makePath(root.path(p)));
            return root.ok("");
        case "rm":
            x.filter(p => !/^-/.test(p)).forEach(p => WinFs.FsUtils.removeFile(root.path(p)));
            return root.ok("");
        case "touch":
            x.filter(p => !/^-/.test(p)).forEach(p => {
                if (!root.exists(p))
                    root.writeFile(root.path(p), "");
            });
            return root.ok("");
        case "mv":
            return root.fail("");
        case "[":
        case "test":
            return root.test(prog === "[" ? x.slice(0, x[x.length - 1] === "]" ? -1 : x.length) : x) ? root.ok("") : root.fail("");
        case "date":
            if (x[0] && x[0].indexOf("+%s") === 0)
                return root.ok((x[0].indexOf("%N") > 0 ? (Date.now() / 1000).toFixed(3) : String(Math.floor(Date.now() / 1000))) + "\n");
            return root.ok(new Date().toString() + "\n");
        case "sed":
            // the accent colour DSS took from Waybar's colors.css: Aether's own accent
            if (x.some(f => /waybar\/colors\.css$/.test(f)))
                return root.ok(root.accent() + "\n");
            return root.fail("");
        case "wl-copy":
        case "xclip":
            Quickshell.clipboardText = input;
            return root.ok("");
        case "xdg-open":
        case "gio":
            if (x.length) {
                const t = x[x.length - 1];
                Qt.openUrlExternally(/^[a-z][a-z0-9+.-]*:/i.test(t) && !/^[A-Za-z]:[\\/]/.test(t) ? t : root.url(t));
            }
            return root.ok("");
        case "notify-send":
            return root.notify(x);
        case "kitty":
        case "foot":
        case "alacritty":
        case "rice-term":
        case "wezterm":
            Quickshell.execDetached(["wt.exe"]);
            return root.ok("");
        case "pavucontrol":
        case "pwvucontrol":
            Qt.openUrlExternally("ms-settings:sound");
            return root.ok("");
        case "missioncenter":
        case "resources":
        case "gnome-system-monitor":
        case "plasma-systemmonitor":
            Quickshell.execDetached(["taskmgr.exe"]);
            return root.ok("");
        case "wpctl":
            return root.wpctl(x);
        case "desktop-set":
            return root.desktopSet(x);
        case "rice-bar":
            return root.riceBar(x);
        case "rice-services":
            return root.services(x);
        case "rice-weather":
            return root.weather(x);
        case "rice-battery":
            return root.ok(JSON.stringify({ main: null, devices: [] }) + "\n");
        case "rice-launch":
            root.trigger("launcher-open", "toggle");
            return root.ok("");
        case "rice-wifi":
            root.trigger("wifi-open", (x[0] || "0") + " " + (x[1] || ""));
            return root.ok("");
        case "rice-bluetooth":
            if (x[0] && !/^[0-9.]+$/.test(x[0]))
                return root.fail("");
            root.trigger("bluetooth-open", (x[0] || "0") + " " + (x[1] || ""));
            return root.ok("");
        case "rice-power":
            if (x[0] === "lock")
                return root.power("lock");
            root.trigger("power-open", (x[0] || "0") + " " + (x[1] || ""));
            return root.ok("");
        case "rice-sys":
            return root.power(x[0] || "");
        case "logout":
            return root.power("logout");
        case "rice-lock":
            return root.power("lock");
        case "rice-volume":
            return root.volume(x);
        case "rice-sound":
            if (x[0] === "step")
                return root.volume([String(x[1]).charAt(0) === "-" ? "down" : "up", String(x[1]).replace(/^[+-]/, "")]);
            return root.ok("");
        case "rice-gamebar":
            root.trigger("gamebar-open", x[0] || "toggle");
            return root.ok("");
        case "rice-cc":
            return root.cc(x);
        case "rice-dashboard":
            return root.dashboard(x);
        case "rice-walls":
            if (x[0] === "next")
                Cae.Wallpapers.nextWallpaper();
            else if (x[0] === "random")
                Cae.Wallpapers.setRandom();
            else if (x[0] === "set" && x[1])
                Cae.Wallpapers.setWallpaper(root.path(x[1]));
            else if (!x.length || x[0] === "toggle" || x[0] === "open")
                root.trigger("walls-open", "toggle");
            else
                return root.fail("");
            return root.ok("");
        case "rice-wallpaper":
            if (x.length && x[x.length - 1] !== "" && !/^-/.test(x[x.length - 1]))
                Cae.Wallpapers.setWallpaper(root.path(x[x.length - 1]));
            return root.ok("");
        case "rice-clip":
            if (!x.length || ["toggle", "open", "menu", "show"].includes(x[0])) {
                root.trigger("clip-open", x[0] || "toggle");
                return root.ok("");
            }
            return root.fail("");
        case "rice-tools":
            if (["open", "toggle", "close"].includes(x[0])) {
                root.trigger("tools-open", x[0] + " " + (x[1] || "-"));
                return root.ok("");
            }
            if (!x.length) {
                root.trigger("tools-open", "toggle -");
                return root.ok("");
            }
            return root.fail("");
        case "rice-taskview":
            if (x[0] === "data")
                return root.fail("");
            root.trigger("taskview-open", x[0] || "toggle");
            return root.ok("");
        case "rice-keymap":
            if (!x.length || x[0] === "toggle" || x[0] === "show") {
                root.trigger("keys-open", "toggle");
                return root.ok("");
            }
            return root.fail("");
        case "rice-settings":
            AS.AetherSettings.open("dss");
            return root.ok("");
        case "rice-hotcorner":
            if (x[0] === "shot") {
                Qt.openUrlExternally("ms-screenclip:");
                return root.ok("");
            }
            return root.fail("");
        case "rice-update":
        case "rice-sfx":
        case "rice-effects":
        case "rice-fps":
        case "rice-tray":
        case "font-set":
        case "ws-go":
        case "brightnessctl":
        case "rice-notify":
            return root.quiet(prog);
        default:
            return root.quiet(prog);
        }
    }

    // a Linux-only program: no output, exit 1, said once in the log
    function quiet(prog: string): var {
        if (!root.told[prog]) {
            root.told[prog] = true;
            console.info("dss: not on Windows:", prog);
        }
        return root.fail("");
    }

    function printf(x: var): string {
        if (!x.length)
            return "";
        const fmt = x[0].replace(/\\n/g, "\n").replace(/\\t/g, "\t").replace(/\\\\/g, "\\");
        const args = x.slice(1);
        let out = "", k = 0;
        do {
            out += fmt.replace(/%(%|[-0-9.]*[sdif])/g, (m, f) => {
                if (f === "%")
                    return "%";
                const v = args[k++] ?? "";
                return /[dif]$/.test(f) ? String(Number(v) || 0) : v;
            });
        } while (k > 0 && k < args.length);
        return out;
    }

    function test(x: var): bool {
        if (x[0] === "!")
            return !root.test(x.slice(1));
        if (x.length === 2) {
            const o = x[0], v = x[1];
            if (o === "-e" || o === "-f" || o === "-d" || o === "-s" || o === "-x" || o === "-r")
                return root.exists(v);
            if (o === "-n")
                return v !== "";
            if (o === "-z")
                return v === "";
        }
        if (x.length === 3) {
            if (x[1] === "=" || x[1] === "==")
                return x[0] === x[2];
            if (x[1] === "!=")
                return x[0] !== x[2];
            const a = Number(x[0]), b = Number(x[2]);
            return { "-eq": a === b, "-ne": a !== b, "-lt": a < b, "-le": a <= b, "-gt": a > b, "-ge": a >= b }[x[1]] ?? false;
        }
        return x.length === 1 && x[0] !== "";
    }

    function notify(x: var): var {
        const pos = [], opts = [];
        for (let k = 0; k < x.length; k++) {
            if (/^-[aiut]$/.test(x[k]) || /^--(app-name|icon|urgency|expire-time|hint|category)$/.test(x[k]))
                opts.push(x[k], x[++k] ?? "");
            else if (/^-/.test(x[k]))
                opts.push(x[k]);
            else
                pos.push(x[k]);
        }
        Cae.Notifs.notifySend(pos[0] || "", pos[1] || "", opts);
        return root.ok("");
    }

    function power(what: string): var {
        if (what === "lock")
            Quickshell.execDetached(["rundll32.exe", "user32.dll,LockWorkStation"]);
        else if (what === "suspend")
            Quickshell.execDetached(["rundll32.exe", "powrprof.dll,SetSuspendState", "0,1,0"]);
        else if (what === "logout")
            Quickshell.execDetached(["shutdown.exe", "/l"]);
        else if (what === "reboot")
            Quickshell.execDetached(["shutdown.exe", "/r", "/t", "0"]);
        else if (what === "poweroff")
            Quickshell.execDetached(["shutdown.exe", "/s", "/t", "0"]);
        else
            return root.fail("");
        return root.ok("");
    }

    function wpctl(x: var): var {
        const src = x.some(v => /SOURCE/.test(v));
        const node = src ? Cae.Audio.source : Cae.Audio.sink;
        if (x[0] === "get-volume") {
            const v = src ? Cae.Audio.sourceVolume : Cae.Audio.volume;
            const m = src ? Cae.Audio.sourceMuted : Cae.Audio.muted;
            return root.ok("Volume: " + v.toFixed(2) + (m ? " [MUTED]" : "") + "\n");
        }
        if (!node || !node.audio)
            return root.fail("");
        if (x[0] === "set-mute")
            node.audio.muted = x[2] === "toggle" ? !node.audio.muted : x[2] === "1";
        else if (x[0] === "set-volume") {
            const v = x[x.length - 1];
            const m = String(v).match(/^([0-9.]+)(%?)([+-]?)$/);
            if (m) {
                let d = Number(m[1]) / (m[2] ? 100 : 1);
                node.audio.volume = Math.max(0, Math.min(1.5, m[3] === "+" ? node.audio.volume + d : m[3] === "-" ? node.audio.volume - d : d));
            }
        }
        return root.ok("");
    }

    function volume(x: var): var {
        const step = Number(x[1]) > 0 ? Number(x[1]) / 100 : 0.05;
        if (x[0] === "up") {
            if (Cae.Audio.sink && Cae.Audio.sink.audio)
                Cae.Audio.sink.audio.muted = false;
            Cae.Audio.incrementVolume(step);
        } else if (x[0] === "down")
            Cae.Audio.decrementVolume(step);
        else if (x[0] === "mute") {
            if (Cae.Audio.sink && Cae.Audio.sink.audio)
                Cae.Audio.sink.audio.muted = !Cae.Audio.muted;
        } else if (x[0] === "mic") {
            if (Cae.Audio.source && Cae.Audio.source.audio)
                Cae.Audio.source.audio.muted = !Cae.Audio.sourceMuted;
        } else if (x[0] === "menu")
            root.trigger("volume-open", (x[1] || "0") + " " + (x[2] || ""));
        else
            return root.fail("");
        return root.ok("");
    }

    function cc(x: var): var {
        if (x[0] === "dnd") {
            PersistentState.doNotDisturb = x[1] === "toggle" || !x[1] ? !PersistentState.doNotDisturb : x[1] === "on";
            return root.ok("");
        }
        if (!x.length || ["toggle", "open", "close"].includes(x[0])) {
            root.trigger("cc-open", x[0] || "toggle");
            return root.ok("");
        }
        return root.ok("");   // engine shell|swaync: notifications are Mix & match's
    }

    function dashboard(x: var): var {
        if (x[0] === "wall" && x[1]) {
            Cae.Wallpapers.setWallpaper(root.path(x[1]));
            return root.ok("");
        }
        const tab = x[0] === "open" || x[0] === "open_tab" ? (x[1] || "today") : (x[0] || "today");
        if (tab === "control")
            root.trigger("cc-open", "open");
        else if (tab === "wallpaper" || tab === "walls")
            root.trigger("walls-open", "toggle");
        else if (tab === "performance" || tab === "system")
            root.trigger("island-open", "system toggle");
        else if (tab === "about")
            root.trigger("island-open", "about toggle");
        else
            root.trigger("island-open", "today toggle");
        return root.ok("");
    }

    function services(x: var): var {
        if (x[0] === "state") {
            const s = {
                have: { nmcli: false, bluetoothctl: false, wpctl: true, "swaync-client": false, hyprsunset: false, systemctl: false, powerprofilesctl: false },
                wifi: false, network: true, bluetooth: false,
                sound: !Cae.Audio.muted, mic: !Cae.Audio.sourceMuted,
                dnd: PersistentState.doNotDisturb, awake: false, nightlight: false,
                profile: "balanced", airplane: false, night_temp: 4500, units: []
            };
            return root.ok(JSON.stringify(s) + "\n");
        }
        if (x[0] === "is")
            return root.ok((x[1] === "dnd" ? (PersistentState.doNotDisturb ? "on" : "off") : "off") + "\n");
        if (x[0] === "toggle" && x[1] === "dnd") {
            PersistentState.doNotDisturb = !PersistentState.doNotDisturb;
            return root.ok("");
        }
        if (x[0] === "toggle" && (x[1] === "sound" || x[1] === "mic"))
            return root.volume([x[1] === "mic" ? "mic" : "mute"]);
        if (x[0] === "set" && x[1] === "dnd") {
            PersistentState.doNotDisturb = x[2] === "on" || x[2] === "true" || x[2] === "1";
            return root.ok("");
        }
        return root.quiet("rice-services " + (x[0] || "") + " " + (x[1] || ""));
    }

    function weather(x: var): var {
        const w = Cae.Weather;
        const cc = w.cc;
        if (!cc)
            return root.fail("");
        const cond = w.description || "";
        if (x[0] === "json") {
            const hours = (w.hourlyForecast || []).slice(0, 12).map(h => {
                const d = new Date(h.timestamp);
                return { h: String(d.getHours()).padStart(2, "0") + ":00", t: Math.round(h.tempC), c: w.getWeatherCondition(h.weatherCode), rain: 0 };
            });
            const days = (w.forecast || []).slice(0, 7).map(d => ({
                d: new Date(d.date).toLocaleDateString(Qt.locale(), "ddd"), hi: Math.round(d.maxTempC), lo: Math.round(d.minTempC),
                c: w.getWeatherCondition(d.weatherCode)
            }));
            const today = (w.forecast || [])[0];
            return root.ok(JSON.stringify({
                temp: Math.round(cc.tempC), feels: Math.round(cc.feelsLikeC), cond: cond, hum: cc.humidity, wind: Math.round(cc.windSpeed),
                unit: "C", place: w.city || "", country: "", hi: today ? Math.round(today.maxTempC) : null, lo: today ? Math.round(today.minTempC) : null,
                hours: hours, days: days
            }) + "\n");
        }
        return root.ok(cond + "|" + (cc.tempC >= 0 ? "+" : "") + Math.round(cc.tempC) + "°C|" + (w.city || "") + "\n");
    }

    function accent(): string {
        try {
            const c = Cae.Colours.palette.m3primary;
            return "#" + [c.r, c.g, c.b].map(v => Math.round(v * 255).toString(16).padStart(2, "0")).join("");
        } catch (e) {
            return "#7aa2f7";
        }
    }

    // ---- rice-bar (bar.json) -------------------------------------------------------------------------------

    readonly property var barDefaults: ({
        enabled: true, style: "dynamic", position: "top", height: 38, workspaces: 5, seconds: false, opacity: 0.72, ws_style: "dots",
        logo: "auto", autohide: false, shadow: true, island_dynamic: true, island_hover: true, island_volume: true, island_ws: true,
        island_notifs: true, tray_size: 18, island_date: true, island_weather: true, island_charge: true, island_off: false,
        island_tab: "today", island_close: 4, logo_color: "default", title_icon: "right", edge_frame: true, logo_move: false
    })
    readonly property var barChoices: ({
        style: ["flat", "island", "islands", "pills", "notch", "dynamic", "edge"], position: ["top", "bottom", "left", "right"],
        ws_style: ["dots", "pacman", "letters", "numbers"], title_icon: ["left", "right", "off"],
        logo_color: ["default", "white", "gold", "ice", "rose", "violet", "theme"], island_tab: ["today", "system", "about"]
    })
    readonly property var barRanges: ({ height: [28, 56], workspaces: [3, 10], opacity: [0.3, 1], tray_size: [12, 28], island_close: [2, 10] })
    readonly property var barModules: ["launcher", "workspaces", "title", "media", "island", "tray", "volume", "network", "bluetooth", "battery", "notifications", "power", "visualizer", "weather", "sysmon", "music"]
    readonly property var barLayout: ({ left: ["launcher", "workspaces", "title"], center: ["island"], right: ["music", "tray", "volume", "bluetooth", "network", "battery", "notifications", "power"] })

    function barLoad(): var {
        let raw = {};
        try {
            raw = JSON.parse(root.readFile(root.rice + "/bar.json") || "{}") || {};
        } catch (e) {
            raw = {};
        }
        const c = {};
        for (let k in root.barDefaults) {
            const d = root.barDefaults[k], v = raw[k];
            if (typeof d === "boolean")
                c[k] = k in raw ? v === true : d;
            else if (root.barChoices[k])
                c[k] = root.barChoices[k].includes(v) ? v : d;
            else if (k === "logo")
                c[k] = typeof v === "string" && v !== "" ? v : d;
            else if (root.barRanges[k]) {
                const r = root.barRanges[k], n = Number(v);
                c[k] = isNaN(n) || v === undefined ? d : k === "opacity" ? Math.round(Math.max(r[0], Math.min(r[1], n)) * 100) / 100 : Math.round(Math.max(r[0], Math.min(r[1], n)));
            } else
                c[k] = v === undefined ? d : v;
        }
        c.layout = root.barCleanLayout(raw.layout && typeof raw.layout === "object" ? raw.layout : root.barLayout);
        c.music_added = true;
        c.v = typeof raw.v === "number" ? raw.v : 1;
        return c;
    }
    function barCleanLayout(raw: var): var {
        const seen = {}, out = {};
        for (let z of ["left", "center", "right"]) {
            out[z] = [];
            for (let m of (Array.isArray(raw[z]) ? raw[z] : [])) {
                if (m === "clock")
                    m = "island";
                if (root.barModules.includes(m) && !seen[m]) {
                    seen[m] = true;
                    out[z].push(m);
                }
            }
        }
        out.available = root.barModules.filter(m => !seen[m]);
        return out;
    }
    function barSave(c: var): void {
        const s = Object.assign({}, c);
        if (s.layout)
            s.layout = { left: s.layout.left, center: s.layout.center, right: s.layout.right };
        s.island_off = !["left", "center", "right"].some(z => (s.layout[z] || []).includes("island"));
        root.writeFile(root.rice + "/bar.json", JSON.stringify(s));
    }
    function riceBar(x: var): var {
        const c = root.barLoad();
        if (!x.length || x[0] === "state")
            return root.ok(JSON.stringify(c) + "\n");
        if (x[0] === "set" && x.length >= 3) {
            const k = x[1], v = x[2];
            if (k === "island_off" || typeof root.barDefaults[k] === "boolean")
                c[k] = ["on", "true", "1", "yes"].includes(String(v).toLowerCase());
            else if (root.barChoices[k] || k === "logo")
                c[k] = v;
            else if (root.barRanges[k])
                c[k] = Number(v);
            else
                return root.fail("unknown key");
            root.barSave(c);
            return root.ok("");
        }
        if (x[0] === "layout-set") {
            try {
                c.layout = root.barCleanLayout(JSON.parse(x[1]));
            } catch (e) {
                return root.fail("");
            }
            root.barSave(c);
            return root.ok("");
        }
        if (x[0] === "layout-add" && x[1] && x[2]) {
            const L = { left: c.layout.left.filter(m => m !== x[2]), center: c.layout.center.filter(m => m !== x[2]), right: c.layout.right.filter(m => m !== x[2]) };
            const z = L[x[1]] || L.right;
            const at = x[3] ? z.indexOf(x[3]) : -1;
            if (at >= 0)
                z.splice(at, 0, x[2]);
            else
                z.push(x[2]);
            c.layout = root.barCleanLayout(L);
            root.barSave(c);
            return root.ok("");
        }
        if (x[0] === "layout-reset") {
            c.layout = root.barCleanLayout(root.barLayout);
            root.barSave(c);
            return root.ok("");
        }
        if (x[0] === "on" || x[0] === "off") {
            c.enabled = x[0] === "on";
            root.barSave(c);
            return root.ok("");
        }
        if (x[0] === "enabled")
            return c.enabled ? root.ok("") : root.fail("");
        return root.ok("");
    }

    // ---- desktop-set (desktop.json) -----------------------------------------------------------------------------

    readonly property var dWidgets: ["weather", "media", "resources", "usercard", "calendar", "visualizer", "battery", "todo", "focus", "notes", "gauge", "date", "sky", "nowring", "player", "playpill", "lyrics", "storage", "trio", "forecast", "music", "datepill"]
    readonly property var dGeom: ["x", "y", "scale", "w", "h", "opacity", "bg", "style", "flip", "color", "font", "focus", "metric", "bars", "shape", "gap", "gain"]
    readonly property var dViz: ({ bars: [16, 32, 48, 64], shape: ["square", "rounded", "pill"], gap: ["thin", "normal", "wide"], gain: [1, 1.4, 2, 2.8] })
    readonly property var dStyles: ({
        visualizer: ["bars", "mirror", "wave", "dots"], clock: ["digital", "line", "cookie", "pixel"],
        gauge: ["water", "pentagon", "cookie", "flower", "clover", "hexagon", "sun", "circle"], date: ["flower", "cookie", "clover", "sun", "square", "circle"]
    })
    readonly property var dMetrics: ["cpu", "ram", "gpu", "temp", "battery"]
    readonly property var dColors: ({ visualizer: ["accent", "gradient", "rainbow", "mono"] })
    readonly property var dFonts: ["default", "bebas", "orbitron", "outfit", "grotesk", "unbounded", "shoulders"]
    readonly property var dThemes: ["shadow", "google", "outline"]
    readonly property var dChoices: ({ placement: ["draggable", "leastBusy", "mostBusy"], style: ["digital", "line", "cookie", "pixel"], font: ["default", "bebas", "orbitron", "outfit", "grotesk", "unbounded", "shoulders"] })

    function dDefaults(): var {
        const w = {};
        root.dWidgets.forEach(id => w[id] = { enabled: false });
        return {
            widgets: w,
            clock: { enabled: true, onlyLocked: false, placement: "draggable", style: "digital", font: "bebas" },
            dock: {
                enabled: false, position: "bottom", background: true, hover: true, pinnedStartup: false, media: true, nowText: true, pinButton: true,
                appsButton: true, tint: false, shade: true, appMenu: true, opacity: 0.78, blur: true, reserve: true,
                apps: ["kitty", "org.gnome.Nautilus", "firefox", "spotify", "discord", "steam"]
            },
            lock: { toolbars: false, widgets: false, media: false },
            gamebar: { opacity: 0.9, blur: true },
            widgetBlur: true, widgetTheme: "google", widgetAdapt: true, autoHide: true, editing: false
        };
    }

    function clamp(v: var, lo: real, hi: real, places: int): var {
        const n = Number(v);
        if (isNaN(n))
            return null;
        const c = Math.max(lo, Math.min(hi, n));
        const f = Math.pow(10, places);
        return places >= 0 ? Math.round(c * f) / f : c;
    }

    function cleanGeom(id: string, g: var): var {
        const o = {};
        for (let k of root.dGeom) {
            if (!(k in g))
                continue;
            const v = g[k];
            let r = null;
            if (k === "x" || k === "y")
                r = isNaN(Number(v)) ? null : Math.max(0, Math.round(Number(v)));
            else if (k === "w" || k === "h")
                r = root.clamp(v, 40, 8000, 0);
            else if (k === "scale")
                r = root.clamp(v, 0.4, 3, 3);
            else if (k === "opacity")
                r = root.clamp(v, 0.1, 1, 2);
            else if (k === "bg")
                r = root.clamp(v, 0, 1, 2);
            else if (k === "flip")
                r = v === true || v === "true" || v === "on";
            else if (k === "style")
                r = (root.dStyles[id] || []).includes(v) ? v : null;
            else if (k === "color")
                r = (root.dColors[id] || []).includes(v) ? v : null;
            else if (k === "font")
                r = id === "clock" && root.dFonts.includes(v) ? v : null;
            else if (k === "metric")
                r = id === "gauge" && root.dMetrics.includes(v) ? v : null;
            else if (k === "focus")
                r = id === "focus" ? root.clamp(v, 5, 120, 0) : null;
            else if (root.dViz[k]) {
                const want = typeof root.dViz[k][0] === "number" ? Number(v) : v;
                r = id === "visualizer" && root.dViz[k].includes(want) ? want : null;
            }
            if (r !== null)
                o[k] = r;
        }
        return o;
    }

    function dLoad(): var {
        let raw = {};
        try {
            raw = JSON.parse(root.readFile(root.rice + "/desktop.json") || "{}") || {};
        } catch (e) {
            raw = {};
        }
        const d = root.dDefaults();
        const c = Object.assign({}, raw, d);
        const rw = raw.widgets && typeof raw.widgets === "object" ? raw.widgets : {};
        for (let id of root.dWidgets) {
            const r = rw[id] && typeof rw[id] === "object" ? rw[id] : {};
            c.widgets[id] = Object.assign({ enabled: r.enabled === true }, root.cleanGeom(id, r));
        }
        const rc = raw.clock && typeof raw.clock === "object" ? raw.clock : {};
        for (let k in d.clock) {
            if (k in rc && typeof rc[k] === typeof d.clock[k] && (!root.dChoices[k] || root.dChoices[k].includes(rc[k])))
                c.clock[k] = rc[k];
        }
        Object.assign(c.clock, root.cleanGeom("clock", rc), { style: c.clock.style, font: c.clock.font });
        for (let sec of ["dock", "lock", "gamebar"]) {
            const r = raw[sec] && typeof raw[sec] === "object" ? raw[sec] : {};
            for (let k in r)
                if (!(k in d[sec]) || typeof r[k] === typeof d[sec][k])
                    c[sec][k] = r[k];
        }
        for (let k of ["widgetBlur", "widgetAdapt", "autoHide", "editing"])
            if (typeof raw[k] === "boolean")
                c[k] = raw[k];
        if (root.dThemes.includes(raw.widgetTheme))
            c.widgetTheme = raw.widgetTheme;
        return c;
    }

    function dSave(c: var): void {
        root.writeFile(root.rice + "/desktop.json", JSON.stringify(c, null, 2) + "\n");
    }

    function truth(v: var): bool {
        return ["on", "true", "1", "yes"].includes(String(v).toLowerCase());
    }

    function desktopSet(x: var): var {
        const c = root.dLoad();
        const a = x[0] || "state";
        if (a === "state")
            return root.ok(JSON.stringify(c) + "\n");
        if (a === "widget" && root.dWidgets.includes(x[1]))
            c.widgets[x[1]].enabled = root.truth(x[2]);
        else if (a === "pos" && x.length >= 4) {
            const t = x[1] === "clock" ? c.clock : c.widgets[x[1]];
            if (!t)
                return root.fail("");
            Object.assign(t, root.cleanGeom(x[1], { x: x[2], y: x[3] }));
            if (x[1] === "clock")
                c.clock.placement = "draggable";
        } else if (a === "clock" && x[1]) {
            if (x[1] === "enabled" || x[1] === "onlyLocked")
                c.clock[x[1]] = root.truth(x[2]);
            else if (root.dChoices[x[1]] && root.dChoices[x[1]].includes(x[2]))
                c.clock[x[1]] = x[2];
            else
                return root.fail("");
        } else if (a === "dock-apps") {
            try {
                const l = JSON.parse(x[1]);
                if (Array.isArray(l))
                    c.dock.apps = l.map(String).slice(0, 24);
            } catch (e) {
                return root.fail("");
            }
        } else if (a === "dock-pin" && x[1]) {
            if (!c.dock.apps.includes(x[1]))
                c.dock.apps = c.dock.apps.concat([x[1]]).slice(0, 24);
        } else if (a === "dock-unpin" && x[1])
            c.dock.apps = c.dock.apps.filter(v => v !== x[1]);
        else if ((a === "dock" || a === "lock") && x[1]) {
            if (a === "dock" && x[1] === "position")
                c.dock.position = ["bottom", "left", "right"].includes(x[2]) ? x[2] : "bottom";
            else if (a === "dock" && x[1] === "opacity")
                c.dock.opacity = root.clamp(x[2], 0.2, 1, 2) ?? c.dock.opacity;
            else
                c[a][x[1]] = root.truth(x[2]);
        } else if (a === "dock-blur")
            c.dock.blur = true;
        else if (a === "gamebar" && x[1]) {
            if (x[1] === "opacity")
                c.gamebar.opacity = root.clamp(x[2], 0.2, 1, 2) ?? c.gamebar.opacity;
            else
                c.gamebar.blur = root.truth(x[2]);
        } else if (a === "widgets-blur")
            c.widgetBlur = root.truth(x[1]);
        else if (a === "widgets-theme" && root.dThemes.includes(x[1]))
            c.widgetTheme = x[1];
        else if (a === "widgets-opacity")
            c.widgetOpacity = root.clamp(x[1], 0.2, 1, 2) ?? 1;
        else if (a === "widgets-fps") {
            const n = Math.round(Number(x[1]) || 0);
            c.widgetFps = n === 0 ? 0 : Math.max(20, Math.min(240, n));
        } else if (a === "widgets-adapt")
            c.widgetAdapt = root.truth(x[1]);
        else if (a === "auto-hide")
            c.autoHide = root.truth(x[1]);
        else if (a === "geom" && x[1]) {
            const t = x[1] === "clock" ? c.clock : c.widgets[x[1]];
            if (!t)
                return root.fail("");
            const g = {};
            ["x", "y", "scale", "w", "h"].forEach((k, n) => {
                if (x[2 + n] !== undefined && x[2 + n] !== "-")
                    g[k] = x[2 + n];
            });
            Object.assign(t, root.cleanGeom(x[1], g));
            if (x[1] === "clock")
                c.clock.placement = "draggable";
        } else if (a === "wset" && x[1] && x[2]) {
            const t = x[1] === "clock" ? c.clock : c.widgets[x[1]];
            if (!t)
                return root.fail("");
            const g = {};
            g[x[2]] = x[2] === "flip" ? root.truth(x[3]) : x[3];
            const cl = root.cleanGeom(x[1], g);
            if (x[2] in cl)
                t[x[2]] = cl[x[2]];
            else
                delete t[x[2]];
        } else if (a === "reset-widget" && x[1]) {
            const t = x[1] === "clock" ? c.clock : c.widgets[x[1]];
            if (t)
                for (let k of ["x", "y", "scale", "w", "h", "opacity", "bg", "flip"])
                    delete t[k];
        } else if (a === "edit")
            c.editing = x[1] === "toggle" ? !c.editing : root.truth(x[1]);
        else if (a === "reset-pos") {
            for (let id of root.dWidgets)
                for (let k of ["x", "y", "scale", "w", "h"])
                    delete c.widgets[id][k];
            for (let k of ["x", "y", "scale", "w", "h"])
                delete c.clock[k];
        } else
            return root.fail("unknown");
        root.dSave(c);
        return root.ok("");
    }

    // ---- files ----------------------------------------------------------------------------------------------------

    function base(p: string): string {
        return String(p).split(/[\\/]/).pop();
    }

    function path(p: var): string {
        let s = String(p ?? "").split("\\").join("/");
        if (s === "~" || s.indexOf("~/") === 0)
            s = root.home + s.slice(1);
        else if (s.indexOf("/tmp/") === 0)
            s = root.temp + s.slice(4);
        return s;
    }

    function url(x: var): string {
        return "file:///" + root.path(x).replace(/^\/+/, "");
    }

    function exists(p: var): bool {
        const s = WinFs.FsUtils.statPath(root.path(p));
        return !!(s && s.path);
    }

    function readFile(p: var): var {
        const s = root.path(p);
        if (s === "" || s === "/dev/null" || !root.exists(s))
            return null;
        if (reader.path === s)
            reader.reload();
        else
            reader.path = s;
        return reader.text();
    }

    function writeFile(p: string, text: string): void {
        if (p === "/dev/null" || p === "")
            return;
        WinFs.FsUtils.makePath(p.slice(0, p.lastIndexOf("/")));
        writer.path = p;
        writer.setText(text);
    }

    FileView {
        id: reader
        blockLoading: true
        blockAllReads: true
        printErrors: false
    }

    // not atomic: a rename would lose the change watch of DSS's Trigger.qml on this file
    FileView {
        id: writer
        blockWrites: true
        atomicWrites: false
        printErrors: false
        preload: false
    }
}
