// Windows (Aether): the Linux programs Ryoku runs that are not on Windows, answered here (Quickshell.setCommandHandler
// in quickshell-windows io/processcore). Git's Unix tools (bash, mkdir, cat, test...) come first on Ryoku's PATH (Aether's
// RyokuHost sets it), so most `bash -c` scripts just run; this covers what is left:
//   - Ryoku's launchers: ryoku-shell <verb> (its panels, the Hub, lock), ryoku-kit, ryoku-cmd-ryoshot, ryopin,
//     ryoku-hub (no arguments) and `qs -c <app> [ipc call ...]` start or reach Ryoku's own apps (ryoku/apps/<name>)
//   - ryoku-cmd-grim -o <screen> <file>: a screenshot of one screen (ryoshot's fallback)
//   - Ryoku's daemons and CLIs written in Go (the rest of ryoku-shell / ryoku-hub, ryogami, ryostore, ryovm...) and
//     Linux-only system tools (bluetoothctl, rfkill, nmcli...): a quiet "not here" (code 127) instead of a missing program
//   - notify-send, xdg-open: Windows' own
//   - mkdir / cat when Git is not installed
// Loaded by Ryoku's shell.qml and by every app's shell.qml (ryoku-port.js hooks them).
pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as WinFs

Singleton {
    id: root

    // Ryoku's Go programs built for Windows (ryoku-go-port.js -> ryoku/bin, first on PATH) run for real; the rest
    // answers "not here"
    readonly property var notHere: ["ryoku-hub", "ryoku-rashin", "ryoku-plugins-place",
        "ryoku-notifications", "ryoku-bar", "ryoku-cmd-color-picker", "ryoku-cmd-radio",
        "ryostage", "ryogami", "ryostore", "ryovm", "ryossh", "bluetoothctl", "rfkill", "dbus-monitor", "nmcli", "busctl", "gdbus", "udevadm",
        "systemd-inhibit", "loginctl", "systemctl", "pkexec", "hyprpicker", "grim", "slurp", "wf-recorder", "gpu-screen-recorder",
        "adb", "avahi-browse", "ydotool", "wtype", "pw-dump", "pw-cli", "pactl", "playerctl", "fastfetch", "pacman", "paru", "yay"]
        .filter(p => !(p.startsWith("ryo") && WinFs.FsUtils.findExecutable(p) !== ""))
    readonly property bool realShell: WinFs.FsUtils.findExecutable("ryoku-shell") !== ""
    // + app launching: every Ryoku launch is `ryoku-host app [--cwd d] [--label l] -- argv`; gtk-launch <id> and
    // `ryoku-app terminal -- cmd` inside it
    readonly property var launchers: ["ryoku-shell", "ryoku-kit", "ryoku-cmd-ryoshot", "ryopin", "ryoku-cmd-grim", "qs",
        "ryoku-host", "ryoku-app", "gtk-launch", "rashin-app", "ryoku-summon"]
    readonly property bool gitTools: WinFs.FsUtils.findExecutable("bash") !== ""
    readonly property var programs: notHere.concat(launchers).concat(["notify-send", "xdg-open"]).concat(gitTools ? [] : ["mkdir", "cat", "bash", "sh"])

    // <package>/ryoku (Aether sets RYOKU_DIR; a dev run has it first on QML_IMPORT_PATH) and <package>/qs next to it
    readonly property string ryokuDir: slash(Quickshell.env("RYOKU_DIR") || String(Quickshell.env("QML_IMPORT_PATH") || "").split(";")[0])
    readonly property string qsDir: slash(Quickshell.env("AETHER_QS_DIR") || (ryokuDir ? ryokuDir.replace(/\/[^\/]+$/, "") + "/qs" : ""))
    // Ryoku's surface verbs (shell/ipc/daemon.go surfaceCommands): each opens one surface of the running shell
    readonly property var surfaces: ({
        "bar-toggle": "barToggle", "launcher": "launcher", "overview": "overview", "visualizer": "visualizer",
        "visualizer-overlay": "visualizer-overlay", "visualizer-place": "visualizer-place", "quicksettings": "sidebar-left",
        "wallpaper-menu": "wallpaper", "clipboard": "clipboard", "gamebar": "gamebar", "settings": "settings",
        "cheatsheet": "cheatsheet", "tools": "tools", "screenshot": "screenshot", "ask": "ask", "ask chat": "ask#chat",
        "ask tools": "ask#tools", "compress": "ask#tools/compress", "install": "ask#tools/install",
        "menu quick-settings": "sidebar-left", "menu screenshot": "screenshot", "menu app-launcher": "launcher"
    })
    property var apps: ({})

    Component.onCompleted: {
        if (typeof Quickshell.setCommandHandler === "function")
            Quickshell.setCommandHandler(programs, command => root.handle(command));
    }

    function slash(p) {
        return String(p || "").replace(/\\/g, "/").replace(/\/+$/, "");
    }

    function base(p) {
        return String(p).split(/[\\/]/).pop().replace(/\.exe$/i, "").toLowerCase();
    }

    function config(name) {
        return name === "shell" ? (slash(Quickshell.env("RYOKU_SHELL")) || `${ryokuDir}/shell/shell.qml`) : `${ryokuDir}/apps/${name}/shell.qml`;
    }

    function ipc(name, args) {
        Quickshell.execDetached([`${qsDir}/qs.exe`, "ipc", "-p", config(name), "call", ...args]);
    }

    // one instance per app from this process: a second ask reaches the running one through its IPC instead
    function launch(name, env, ipcArgs) {
        const running = apps[name];
        if (running && running.running) {
            if (ipcArgs)
                ipc(name, ipcArgs);
            return { output: "", code: 0 };
        }
        if (!ryokuDir || WinFs.FsUtils.classify(config(name)) !== "file")
            return { output: "", code: 127 };
        const p = appProcess.createObject(root, { command: [`${qsDir}/qsw.exe`, "-p", config(name)], environment: env ?? {} });
        apps[name] = p;
        p.running = true;
        return { output: "", code: 0 };
    }

    function handle(command) {
        const cmd = Array.from(command, a => String(a));
        const prog = base(cmd[0]);
        const args = cmd.slice(1);
        if (root.notHere.includes(prog))
            return { output: "", code: 127 };
        switch (prog) {
        case "ryoku-shell":
            return shellVerb(args);
        case "ryoku-kit":
            return launch("keys", { "RYOKU_KIT_TAB": args[0] ?? "", "RYOKU_KIT_RAIL": args[1] ?? "" }, ["kit", "open", args[0] ?? "", args[1] ?? ""]);
        case "ryoku-cmd-ryoshot":
            return launch("ryoshot", {}, null);
        case "ryopin":
            return launch("ryopin", {}, null);
        case "ryoku-cmd-grim":
            return grim(args);
        case "ryoku-host":
            return hostApp(args);
        case "rashin-app":
            return openRashin();
        case "ryoku-summon":   // ryoku-summon <name> <command...>: raise or start an app
            return args.join(" ").indexOf("rashin-app") >= 0 ? openRashin() : hostApp(["app", "--"].concat(args.slice(1)));
        case "gtk-launch":
            return launchEntry(args[0] ?? "", "");
        case "ryoku-app":
            return args[0] === "terminal" ? terminal(args.slice(args.indexOf("--") + 1), "") : { output: "", code: 127 };
        case "qs":
            return qsCall(args);
        case "notify-send":
            return { code: 0 }; // Ryoku shows its own notifications; Linux's sender has no Windows counterpart here
        case "xdg-open":
            if (args.length)
                Qt.openUrlExternally(/^[a-z][a-z0-9+.-]+:/i.test(args[0]) && !/^[a-z]:[\\/]/i.test(args[0]) ? args[0] : "file:///" + args[0]);
            return { code: 0 };
        case "mkdir":
            for (const a of args.filter(a => !a.startsWith("-")))
                WinFs.FsUtils.makePath(a);
            return { code: 0 };
        case "cat":
            return cat(args.filter(a => !a.startsWith("-")));
        case "bash":
        case "sh":
            return { output: "", code: 127 }; // no Git: Ryoku's scripts cannot run
        }
        return false;
    }

    function shellVerb(args) {
        const verb = args.join(" ");
        if (surfaces[verb] !== undefined) {
            ipc("shell", ["shell", "openSurface", "", surfaces[verb]]);
            return { output: "", code: 0 };
        }
        if (args[0] === "hub")
            return launch("hub", {}, args[1] === "open" && args[2] ? ["nav", "open", args[2]] : null);
        if (args[0] === "lock" && WinFs.FsUtils.classify(`${ryokuDir}/lockscreen/lock_shell.qml`) === "file") {
            // Ryoku's own lock (qylock, Windows password through Aether's check); its theme imports come first
            if (apps["qylock"] && apps["qylock"].running)
                return { output: "", code: 0 };
            const p = appProcess.createObject(root, {
                command: [`${qsDir}/qsw.exe`, "-p", `${ryokuDir}/lockscreen/lock_shell.qml`],
                environment: { "QML_IMPORT_PATH": `${ryokuDir}/lockscreen/imports;${ryokuDir}`, "QML_XHR_ALLOW_FILE_READ": "1" }
            });
            apps["qylock"] = p;
            p.running = true;
            return { output: "", code: 0 };
        }
        if (args[0] === "aether") {
            // Aether Settings (every shell in one window) - ">aether" / "/Aether Settings" in Ryoku's launcher
            const aetherShell = Quickshell.env("AETHER_SHELL");
            // "ryoku-shell aether run <name>": one of your commands.json commands (AetherUser)
            if (aetherShell && args[1] === "run" && args[2])
                Quickshell.execDetached([`${qsDir}/qs.exe`, "ipc", "-p", aetherShell, "call", "user", "run", String(args[2])]);
            else if (aetherShell)
                Quickshell.execDetached([`${qsDir}/qs.exe`, "ipc", "-p", aetherShell, "call", "aether", "settings"]);
            return { output: "", code: aetherShell ? 0 : 1 };
        }
        if (args[0] === "lock") {
            // Aether's lock (Caelestia's, unlocked with the Windows password) when Aether started Ryoku, else Windows'
            const aether = Quickshell.env("AETHER_SHELL");
            if (aether)
                Quickshell.execDetached([`${qsDir}/qs.exe`, "ipc", "-p", aether, "call", "lock", "lock"]);
            else
                Quickshell.execDetached(["rundll32.exe", "user32.dll,LockWorkStation"]);
            return { output: "", code: 0 };
        }
        // the rest is Ryoku's daemon (record, sound, theme, clipboard history...): the real ryoku-shell when built
        return realShell ? false : { output: "", code: 127 };
    }

    // qs -c <name> [ipc call <target> <fn> args...] - Ryoku's packaged configs are its apps here
    function qsCall(args) {
        const c = args.indexOf("-c");
        if (c < 0 || !args[c + 1])
            return { output: "", code: 127 };
        const name = args[c + 1];
        const i = args.indexOf("ipc");
        if (i >= 0 && args[i + 1] === "call") {
            if (name === "shell" || (apps[name] && apps[name].running))
                ipc(name, args.slice(i + 2));
            return { output: "", code: 0 };
        }
        return launch(name, {}, null);
    }

    // ryoku-host app [--cwd d] [--label l] -- argv: start an app the Windows way
    function hostApp(args) {
        if (args[0] !== "app")
            return { output: "", code: 127 };
        const sep = args.indexOf("--");
        const opts = sep >= 0 ? args.slice(1, sep) : args.slice(1);
        const argv = sep >= 0 ? args.slice(sep + 1) : [];
        const c = opts.indexOf("--cwd");
        const cwd = c >= 0 ? opts[c + 1] ?? "" : "";
        if (argv.length === 0)
            return { output: "", code: 1 };
        const prog = base(argv[0]);
        // Rashin's app (Linux: `ryoku-summon Rashin ... rashin-app`, a GTK WebKit window)
        if (prog === "rashin-app" || argv.join(" ").indexOf("rashin-app") >= 0)
            return openRashin();
        if (prog === "gtk-launch")
            return launchEntry(argv[1] ?? "", cwd);
        if (prog === "ryoku-app" && argv[1] === "terminal")
            return terminal(argv.slice(argv.indexOf("--") + 1), cwd);
        // a Start-menu entry's command is one token (shortcut / AppsFolder id): Windows' shell starts it
        if (argv.length === 1) {
            const entry = DesktopEntries.applications.values.find(e => e.command && e.command[0] === argv[0]);
            if (entry) {
                entry.execute();
                return { output: "", code: 0 };
            }
        }
        // anything else Ryoku (or its own handler) knows how to run
        const handled = handle(argv);
        if (handled !== false)
            return handled;
        Quickshell.execDetached({ command: argv, workingDirectory: cwd });
        return { output: "", code: 0 };
    }

    // Rashin's console: ryoku-rashin serves it on 127.0.0.1:<port> (~/.config/ryoku/rashin.json, default 3600);
    // on Windows it opens as an Edge app window (Linux uses a GTK WebKit window). The server is started if needed.
    property int rashinTries: 0
    function rashinPort() {
        const x = new XMLHttpRequest();
        try {
            x.open("GET", "file:///" + slash(Quickshell.env("HOME") || Quickshell.env("USERPROFILE")).replace(/^\/+/, "") + "/.config/ryoku/rashin.json", false);
            x.send();
            const p = JSON.parse(x.responseText || "{}").port;
            return p > 0 ? p : 3600;
        } catch (e) {
            return 3600;
        }
    }
    function openRashin() {
        const exe = `${ryokuDir}/bin/ryoku-rashin.exe`;
        if (WinFs.FsUtils.classify(exe) === "file")
            Quickshell.execDetached([exe, "serve"]);   // a second server just exits: the port is taken
        rashinTries = 0;
        rashinWait.restart();
        return { output: "", code: 0 };
    }
    Timer {
        id: rashinWait

        interval: 400
        repeat: true
        onTriggered: {
            const url = `http://127.0.0.1:${root.rashinPort()}`;
            const x = new XMLHttpRequest();
            x.onreadystatechange = () => {
                if (x.readyState !== XMLHttpRequest.DONE || !rashinWait.running)
                    return;
                if (x.status === 200 || ++root.rashinTries > 25) {
                    rashinWait.stop();
                    const edge = WinFs.FsUtils.findExecutable("msedge")
                        || ["C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe", "C:/Program Files/Microsoft/Edge/Application/msedge.exe"]
                            .find(p => WinFs.FsUtils.classify(p) === "file") || "";
                    if (edge)
                        Quickshell.execDetached([edge, `--app=${url}/#/agent`, "--window-size=1200,820"]);
                    else
                        Qt.openUrlExternally(`${url}/#/agent`);
                }
            };
            x.open("GET", `${url}/api/ping`);
            x.send();
        }
    }

    function launchEntry(id, cwd) {
        const key = String(id).replace(/\.desktop$/i, "").toLowerCase();
        const entry = DesktopEntries.byId(key) ?? DesktopEntries.applications.values.find(e => String(e.id).toLowerCase() === key);
        if (!entry)
            return { output: "", code: 1 };
        entry.execute();
        return { output: "", code: 0 };
    }

    // a terminal app (btop, nvim...): Windows Terminal, else Alacritty, else a console window
    function terminal(cmd, cwd) {
        const wt = WinFs.FsUtils.findExecutable("wt");
        const ala = WinFs.FsUtils.findExecutable("alacritty");
        let argv;
        if (wt)
            argv = [wt].concat(cmd.length ? ["--", ...cmd] : []);
        else if (ala)
            argv = [ala].concat(cmd.length ? ["-e", ...cmd] : []);
        else
            argv = ["cmd.exe", "/c", "start", "", "cmd.exe"].concat(cmd.length ? ["/k", ...cmd] : []);
        Quickshell.execDetached({ command: argv, workingDirectory: cwd });
        return { output: "", code: 0 };
    }

    // grim -o <screen> <file>: Quickshell's own screen capture (Windows Graphics Capture)
    function grim(args) {
        const o = args.indexOf("-o");
        const file = args[args.length - 1];
        if (o < 0 || !file || file === args[o + 1])
            return { output: "", code: 1 };
        const dir = file.replace(/[\\/][^\\/]*$/, "");
        if (dir)
            WinFs.FsUtils.makePath(dir);
        return { output: "", code: WinFs.Screenshot.captureScreen(args[o + 1], file) ? 0 : 1 };
    }

    function cat(files) {
        let out = "";
        for (const f of files) {
            const x = new XMLHttpRequest();
            try {
                x.open("GET", "file:///" + f.replace(/^\/+/, ""), false);
                x.send();
                out += x.responseText;
            } catch (e) {
                return { output: out, code: 1 };
            }
        }
        return { output: out, code: 0 };
    }

    Component {
        id: appProcess

        Process {}
    }
}
