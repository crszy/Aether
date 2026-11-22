// Ryoku's Go programs (github.com/Ryoku-dev/ryoku, GPL-3.0) for Windows: the Hub backend (ryoku-hub), the shell
// daemon (ryoku-shell), Ryogami's daemon, the store backend - copied from ref/ryoku and patched where they call
// Linux-only system calls. ref/ryoku stays a clean checkout.
//
//   node ryoku-go-port.js            -> dotfiles/ryoku-go-win/<module>
//   bash ryoku-go-build.sh           -> dotfiles/ryoku-go-build/*.exe
//
// golang.org/x/sys/unix imports and the Linux-only names of `syscall` go to Aether's Windows `unix` package
// (dotfiles/ryoku-go-shim/aetherunix): real file calls through Go's os package, ENOSYS for inotify / netlink /
// eventfd / pidfd so that code takes its "not available" path. Each module (and each vendored Ryoku module) that
// needs it gets its own copy at <module>/aetherunix.
const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const SRC = path.join(HERE, "..", "ref", "ryoku", "ryoku");
const OUT = path.join(HERE, "ryoku-go-win");
const SHIM_SRC = path.join(HERE, "ryoku-go-shim", "aetherunix");

// [upstream folder, what it builds]
const MODULES = [
    ["hub/backend", "ryoku-hub"],
    ["apps/ryostore/backend", "ryostore"],
    ["wm", "ryoku-wm (library)"],
    ["shell/ipc", "ryoku-shell"],
    ["shell/ryogami/daemon", "ryogami"],
    ["rashin/backend", "ryoku-rashin"],
    ["cli", "ryoku"],
    ["palette-bridge", "ryoku-palette-bridge"],
];

// Windows' own syscall package keeps what it has (SIGKILL, SIGTERM, Errno values, WaitStatus...)
const LINUX_SYSCALL = [
    "Kill", "Flock", "LOCK_[A-Z]+", "Getpgid", "Setpgid", "Setsid", "Mkfifo", "Getuid", "Geteuid", "Getgid",
    "AF_NETLINK", "Bind", "F_SETLKW?", "F_UNLCK", "F_WRLCK", "F_RDLCK", "FcntlFlock", "Flock_t", "Getpgrp",
    "IN_[A-Z_]+", "Inotify[A-Za-z0-9]+", "NETLINK_[A-Z_]+", "Read", "Close", "SIGCONT", "SIGSTOP", "SIGUSR1",
    "SIGUSR2", "SIGHUP", "SIGCHLD", "SIGTSTP", "SOCK_[A-Z]+", "SockaddrNetlink", "Socket", "Recvfrom", "Stat",
    "Lstat", "Fstat", "Stat_t", "Umask", "Timespec", "Pipe2?", "Mkdir", "Unlink", "Rmdir", "Rename", "Open",
    "Write", "Pread", "O_[A-Z]+", "S_IF[A-Z]+", "Statfs_t", "Statfs", "Utsname", "Uname", "Termios", "Winsize",
    "SYS_IOCTL", "TCGETS", "TCSETS", "TIOCGWINSZ", "Syscall6", "Syscall",
    "Ucred", "GetsockoptUcred", "SO_PEERCRED", "SOL_SOCKET", "Sizeof[A-Za-z]+", "TIOC[A-Z]+", "Dup[23]?", "CloseOnExec", "SetNonblock",
].join("|");
const REWRITES = [
    [/&syscall\.SysProcAttr\{[^}]*\b(Setpgid|Setsid|Pdeathsig|Credential|Foreground|Noctty|Setctty)\b[^}]*\}/g, "unix.AetherProcAttr()"],
    [new RegExp("\\bsyscall\\.(" + LINUX_SYSCALL + ")\\b", "g"), "unix.$1"],
    // the daemons sockets: AF_UNIX + the named pipe Quickshell connects to (aetherunix/listen.go)
    [/\bnet\.Listen\("unix", /g, 'unix.AetherListen("unix", '],
    // cmd.SysProcAttr.Setpgid = true: no process groups on Windows
    [/^(\s*)([\w.]+)\.SysProcAttr\.(Setpgid|Setsid|Pdeathsig|Pgid|Credential)\s*=\s*[^\n]+$/gm, "$1_ = $2"],
];

function copyTree(src, dst) {
    fs.mkdirSync(dst, { recursive: true });
    for (const e of fs.readdirSync(src, { withFileTypes: true })) {
        if (e.name === ".git" || e.name === "testdata") continue;
        const s = path.join(src, e.name), d = path.join(dst, e.name);
        if (e.isDirectory()) copyTree(s, d);
        else if (!e.name.endsWith("_test.go")) fs.copyFileSync(s, d);
    }
}

function walk(dir, out = []) {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, e.name);
        if (e.isDirectory()) walk(p, out);
        else if (e.name.endsWith(".go")) out.push(p);
    }
    return out;
}

function esc(s) {
    return s.replace(/[.*+?^${}()|[\]\\/]/g, "\\$&");
}

// the shim's import path from a file: its module's <module>/aetherunix (a vendored Ryoku module gets its own copy,
// listed in vendor/modules.txt)
function shimFor(file) {
    const rel = path.relative(OUT, file).split(path.sep).join("/");
    const v = rel.match(/^(.*\/vendor)\/([^/]+)\//);
    if (v && !v[2].includes(".")) {
        return { importPath: v[2] + "/aetherunix", dir: path.join(OUT, v[1], v[2], "aetherunix"),
            modules: path.join(OUT, v[1], "modules.txt"), mod: v[2] };
    }
    let d = path.dirname(file);
    while (!fs.existsSync(path.join(d, "go.mod"))) d = path.dirname(d);
    const mod = fs.readFileSync(path.join(d, "go.mod"), "utf8").match(/^module\s+(\S+)/m)[1];
    return { importPath: mod + "/aetherunix", dir: path.join(d, "aetherunix") };
}

function addImport(t, line) {
    if (/^import \(\n/m.test(t)) return t.replace(/^import \(\n/m, "import (\n\t" + line + "\n");
    if (/^import "/m.test(t)) return t.replace(/^import "/m, "import " + line + "\nimport \"");
    return t.replace(/^(package \w+\n)/m, "$1\nimport " + line + "\n");
}

// a rewrite can leave an import unused, which Go refuses
function dropUnused(t, imp, name) {
    const body = t.replace(new RegExp('"' + esc(imp) + '"', "g"), "");
    if (new RegExp("\\b" + name + "\\.").test(body)) return t;
    return t.replace(new RegExp('^[ \\t]*(\\w+[ \\t]+)?"' + esc(imp) + '"\\n', "m"), "")
        .replace(/^import \(\n\)\n/m, "");
}

// file-specific patches (filled in as each program is brought up on Windows)
function patchFile(rel, t) {
    // Quickshell on Windows: qsw.exe (no console window) and Aether's layout - ryoku/shell/shell.qml and
    // ryoku/apps/<name>/shell.qml under RYOKU_DIR (Aether's RyokuHost sets it) instead of `qs -c <name>`
    if (rel.startsWith("shell/ipc/") && !rel.includes("/vendor/"))
        t = t.replace(/\bexec\.Command(Context\([^,]+, )?\("qs", /g, 'exec.Command$1("qsw", ').replace(/\bexec\.Command\("qs", /g, 'exec.Command("qsw", ');
    // Ryogami starts its picker as `quickshell -p <RYOGAMI_SHELL_QML>` (RyokuHost points it at apps/ryogami)
    if (rel.startsWith("shell/ryogami/daemon/") && !rel.includes("/vendor/"))
        t = t.replace(/\bcommand\("quickshell", /g, 'command("qsw", ');
    // videos play in Ryoku's shell (QtMultimedia) on Windows: the "ryogami" engine is a Linux player
    if (rel === "shell/ryogami/daemon/config.go") {
        const before = t;
        t = t.replace('p := wallTune{Engine: "ryogami", ', 'p := wallTune{Engine: "in_shell", ');
        if (t === before) throw new Error("ryoku-go-port: ryogami config.go wallTune changed upstream");
    }
    // the window manager helper reaches Aether's WM through named pipes, with monitor names translated
    // (ryoku-go-shim/wm-hyprland/aether_pipe_windows.go) - no workspaces in Ryoku's bar without it (2026-11-15)
    if (rel.startsWith("wm/hyprland/") && !rel.includes("/vendor/")) {
        t = t.replace(/net\.DialTimeout\("unix", /g, "aetherHyprDialTimeout(").replace(/net\.Dial\("unix", /g, "aetherHyprDial(")
            .replace(/os\.Stat\(filepath\.Join\(runDir\(\), sig, "\.socket\.sock"\)\)/g, 'aetherHyprStat(filepath.Join(runDir(), sig, ".socket.sock"))')
            .replace(/os\.Stat\(p\); err == nil/g, "aetherHyprStat(p); err == nil");
        const body = t.replace(/^import \([\s\S]*?\n\)\n/m, "");
        if (!/\bnet\./.test(body)) t = t.replace(/^\t"net"\n/m, "");
    }
    // Windows sources for the daemon's Linux (D-Bus) features: ryoku-go-shim/shell-ipc/*_windows.go
    if (rel === "shell/ipc/network.go") {
        const before = t;
        t = t.replace("func (d *daemon) startNetwork() {\n", "func (d *daemon) startNetwork() {\n\tif aetherStartNetwork(d) { // Windows (Aether): netsh instead of NetworkManager\n\t\treturn\n\t}\n");
        if (t === before) throw new Error("ryoku-go-port: network.go startNetwork changed upstream");
    }
    if (rel === "shell/ipc/powerprofiles.go") {
        const before = t;
        t = t.replace("func (d *daemon) startPowerProfiles() {\n", "func (d *daemon) startPowerProfiles() {\n\tif aetherStartPowerProfiles(d) { // Windows (Aether): power plans, never changed without the user\n\t\treturn\n\t}\n");
        if (t === before) throw new Error("ryoku-go-port: powerprofiles.go startPowerProfiles changed upstream");
    }
    if (rel === "shell/ipc/actions.go") {
        const before = t;
        t = t.replace("func qsSelect(name string) []string {\n",
            "func qsSelect(name string) []string {\n" +
            "\tif s := os.Getenv(\"RYOKU_SHELL\"); s != \"\" && name == \"shell\" { // Aether: your ~/.config/quickshell/ryoku\n" +
            "\t\treturn []string{\"-p\", s}\n\t}\n" +
            "\tif d := os.Getenv(\"RYOKU_DIR\"); d != \"\" { // Aether (Windows)\n" +
            "\t\tif name == \"shell\" {\n\t\t\treturn []string{\"-p\", filepath.Join(d, \"shell\", \"shell.qml\")}\n\t\t}\n" +
            "\t\treturn []string{\"-p\", filepath.Join(d, \"apps\", name, \"shell.qml\")}\n\t}\n");
        if (t === before) throw new Error("ryoku-go-port: actions.go qsSelect changed upstream");
    }
    return t;
}

fs.rmSync(OUT, { recursive: true, force: true });
for (const [from] of MODULES) {
    const s = path.join(SRC, from);
    if (!fs.existsSync(s)) throw new Error("ryoku-go-port: upstream moved " + from);
    copyTree(s, path.join(OUT, from));
}

// third-party vendored code is left as it is (it already builds on Windows)
const shims = new Map();
for (const f of walk(OUT)) {
    const rel = path.relative(OUT, f).split(path.sep).join("/");
    if (/\/vendor\/(github\.com|golang\.org|gopkg\.in)\//.test(rel) || rel.includes("/aetherunix/")) continue;
    let t = fs.readFileSync(f, "utf8").replace(/\r\n/g, "\n");
    if (/^\/\/go:build .*windows/m.test(t)) continue; // already split by build tags for Windows
    const before = t;
    const s = shimFor(f);
    t = t.replace(/^([ \t]*)(unix[ \t]+)?"golang\.org\/x\/sys\/unix"$/m, '$1unix "' + s.importPath + '"');
    for (const [re, to] of REWRITES) t = t.replace(re, to);
    t = patchFile(rel, t);
    if (t !== before) {
        if (/\bunix\./.test(t) && !t.includes('"' + s.importPath + '"')) t = addImport(t, 'unix "' + s.importPath + '"');
        t = dropUnused(t, "syscall", "syscall");
        fs.writeFileSync(f, t);
        shims.set(s.dir, s);
    }
}
// the Windows sources the daemon gains (network, ...)
fs.cpSync(path.join(HERE, "ryoku-go-shim", "shell-ipc"), path.join(OUT, "shell", "ipc"), { recursive: true });
fs.cpSync(path.join(HERE, "ryoku-go-shim", "wm-hyprland"), path.join(OUT, "wm", "hyprland"), { recursive: true });
for (const s of shims.values()) {
    fs.cpSync(SHIM_SRC, s.dir, { recursive: true });
    if (s.modules) {
        let m = fs.readFileSync(s.modules, "utf8").replace(/\r\n/g, "\n");
        const re = new RegExp("^(# " + esc(s.mod) + " [^\\n]*\\n(?:## [^\\n]*\\n)?)", "m");
        if (!m.includes("\n" + s.importPath + "\n")) m = m.replace(re, "$1" + s.importPath + "\n");
        fs.writeFileSync(s.modules, m);
    }
}

console.log("ryoku-go-port: " + shims.size + " modules use the Windows unix package, in " + OUT);
