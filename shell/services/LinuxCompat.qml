pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as Win
import Caelestia.Services as CaeS

// Windows: the Linux programs the ported shells (DMS, Noctalia, Zenith, end-4) run - wl-copy, xdg-open,
// notify-send, wpctl, brightnessctl, cliphist, systemctl/loginctl power verbs, wlogout - answered with
// Windows' and Caelestia's own services, so the same buttons work here. Hooked into every Process and
// execDetached by Quickshell.setCommandHandler (quickshell-windows io/processcore.hpp): a command not
// handled here (false) runs as before.
Singleton {
    id: root

    readonly property var programs: ["wl-copy", "wl-paste", "xdg-open", "notify-send", "wpctl", "brightnessctl", "cliphist", "systemctl", "loginctl", "wlogout", "fc-list", "nproc"]

    Component.onCompleted: {
        if (typeof Quickshell.setCommandHandler === "function")
            Quickshell.setCommandHandler(programs, command => root.handle(command));
    }

    function handle(command) {
        const cmd = Array.from(command, a => String(a));
        let prog = base(cmd[0]);
        if (prog === "env") { // env LC_ALL=C prog ...
            while (cmd.length > 1 && (base(cmd[0]) === "env" || cmd[0].includes("=")))
                cmd.shift();
            prog = base(cmd[0]);
        }
        const args = cmd.slice(1);
        switch (prog) {
        case "sh":
        case "bash":
            return script(args[args.length - 1] ?? "");
        case "wl-copy":
            return wlCopy(args);
        case "wl-paste":
            return wlPaste(args);
        case "xdg-open":
            return open(args[0]);
        case "notify-send":
            return notifySend(args);
        case "wpctl":
            return wpctl(args);
        case "brightnessctl":
            return brightnessctl(args);
        case "cliphist":
            return cliphist(args);
        case "systemctl":
        case "loginctl":
            return session(prog, args);
        case "wlogout":
            sessionMenu();
            return true;
        case "fc-list": // font pickers: Windows' fonts
            return fonts(args.includes(":mono") || args.includes(":spacing=mono"));
        case "nproc":
            return `${Quickshell.env("NUMBER_OF_PROCESSORS") || 1}\n`;
        }
        return false;
    }

    function base(path) {
        return String(path ?? "").replace(/^.*[\\/]/, "").replace(/\.exe$/i, "").toLowerCase();
    }

    // `echo -n 'x' | wl-copy`, `printf '%s' 'x' | wl-copy`, `cliphist decode 12 | wl-copy` - the shapes the
    // shells use; any other script runs as before
    function script(s) {
        // availability checks: `command -v cliphist`, `which wl-copy`
        let m = s.match(/^\s*(?:command -v|which)\s+([\w.-]+)\s*(?:>\s*\/dev\/null(?:\s+2>&1)?)?\s*$/);
        if (m)
            return root.programs.includes(m[1]) ? { output: m[1] + "\n", code: 0 } : false;
        m = s.match(/^\s*echo\s+'?(\d+)[^|]*\|\s*cliphist delete\s*$/);
        if (m) {
            Win.Clipboard.deleteEntry(Number(m[1]));
            return true;
        }
        if (/^\s*cliphist decode \d+\s*\|\s*base64\b/.test(s))
            return { code: 1 }; // image previews: not from here
        m = s.match(/^\s*(?:echo -n|printf '%s')\s+'((?:[^']|'\\'')*)'\s*\|\s*wl-copy\b/);
        if (m) {
            Win.Clipboard.copyText(m[1].split("'\\''").join("'"));
            return true;
        }
        m = s.match(/^\s*(?:echo -n|printf '%s')\s+"((?:[^"\\]|\\.)*)"\s*\|\s*wl-copy\b/);
        if (m) {
            Win.Clipboard.copyText(m[1].replace(/\\(.)/g, "$1"));
            return true;
        }
        m = s.match(/^\s*cliphist decode (\d+)\s*\|\s*wl-copy\b/);
        if (m) {
            Win.Clipboard.copy(Number(m[1]));
            return true;
        }
        return false;
    }

    function wlCopy(args) {
        const rest = [];
        for (let i = 0; i < args.length; i++) {
            if (args[i] === "--type" || args[i] === "-t")
                i++;
            else if (!args[i].startsWith("-"))
                rest.push(args[i]);
        }
        if (rest.length === 0)
            return false; // text on stdin: not here
        Win.Clipboard.copyText(rest.join(" "));
        return true;
    }

    function wlPaste(args) {
        if (args.includes("--watch") || args.includes("-w"))
            return false; // a history daemon: Windows keeps the clipboard history itself
        const text = String(Quickshell.clipboardText ?? "");
        return { output: args.includes("--no-newline") || args.includes("-n") ? text : text + "\n", code: text ? 0 : 1 };
    }

    function open(what) {
        if (!what)
            return { code: 1 };
        if (what.startsWith("trash:"))
            Quickshell.execDetached(["explorer.exe", "shell:RecycleBinFolder"]);
        else if (/^[a-z][a-z0-9+.-]+:/i.test(what) && !/^[a-z]:[\\/]/i.test(what))
            Qt.openUrlExternally(what);
        else
            Qt.openUrlExternally("file:///" + what.replace(/\\/g, "/").replace(/^\/+/, ""));
        return true;
    }

    // a real notification (every shell's pop-ups show it), with notify-send's own options
    function notifySend(args) {
        const withValue = ["-u", "--urgency", "-t", "--expire-time", "-a", "--app-name", "-i", "--icon", "-c", "--category", "-h", "--hint", "-r", "--replace-id", "-A", "--action"];
        const options = [];
        const texts = [];
        for (let i = 0; i < args.length; i++) {
            const a = args[i];
            if (withValue.includes(a)) {
                options.push(a, args[i + 1] ?? "");
                i++;
            } else if (a.startsWith("-") && a.length > 1) {
                options.push(a);
            } else {
                texts.push(a);
            }
        }
        Notifs.notifySend(texts[0] ?? "", texts[1] ?? "", options);
        return true;
    }

    // wpctl get-volume / set-volume / set-mute on @DEFAULT_AUDIO_SINK@ / @DEFAULT_AUDIO_SOURCE@
    function wpctl(args) {
        const verb = args[0];
        const input = String(args[1] ?? "").includes("SOURCE");
        const node = input ? Audio.source : Audio.sink;
        if (!node?.audio)
            return { code: 1 };
        if (verb === "get-volume")
            return `Volume: ${node.audio.volume.toFixed(2)}${node.audio.muted ? " [MUTED]" : ""}\n`;
        if (verb === "set-mute") {
            const v = args[2];
            node.audio.muted = v === "toggle" ? !node.audio.muted : (v === "1" || v === "true");
            return true;
        }
        if (verb === "set-volume") {
            const target = relative(String(args[args.length - 1] ?? ""), node.audio.volume);
            if (target < 0)
                return { code: 1 };
            if (input)
                Audio.setSourceVolume(target);
            else
                Audio.setVolume(target);
            return true;
        }
        return { code: 1 }; // settings, status...: nothing to report on Windows
    }

    // "0.5", "50%", "5%+", "5%-", "+5%" (wpctl / brightnessctl style) against `current` (0..1)
    function relative(v, current) {
        const m = v.match(/^([+-]?)(\d+(?:\.\d+)?)(%?)([+-]?)$/);
        if (!m)
            return -1;
        const amount = Number(m[2]) / (m[3] ? 100 : 1);
        const sign = m[1] || m[4];
        const target = sign === "+" ? current + amount : sign === "-" ? current - amount : amount;
        return Math.max(0, Math.min(1, target));
    }

    // brightnessctl get / max / set N%: the focused monitor's brightness (Caelestia's: DDC/CI or the laptop panel)
    function brightnessctl(args) {
        const rest = [];
        for (let i = 0; i < args.length; i++) {
            if (["-d", "--device", "-c", "--class", "-n", "--min-value"].includes(args[i]))
                i++;
            else if (!args[i].startsWith("-"))
                rest.push(args[i]);
        }
        const monitor = Brightness.getMonitor("active") ?? Brightness.monitors[0];
        if (!monitor)
            return { code: 1 };
        const verb = rest[0] ?? "info";
        if (verb === "get" || verb === "g")
            return `${Math.round(monitor.brightness * 100)}\n`;
        if (verb === "max" || verb === "m")
            return "100\n";
        if (verb === "set" || verb === "s") {
            const target = relative(String(rest[1] ?? ""), monitor.brightness);
            if (target < 0)
                return { code: 1 };
            monitor.setBrightness(target);
            return true;
        }
        return `Device 'aether' of class 'backlight':\n\tCurrent brightness: ${Math.round(monitor.brightness * 100)} (${Math.round(monitor.brightness * 100)}%)\n\tMax brightness: 100\n`;
    }

    // cliphist on Windows' clipboard history (the same "id<TAB>preview" lines)
    function cliphist(args) {
        const verb = args[0];
        const entries = Win.Clipboard.entries ?? [];
        if (verb === "list")
            return entries.length ? entries.join("\n") + "\n" : "";
        if (verb === "decode") {
            const id = String(args[1] ?? "").split("\t")[0];
            const line = entries.find(l => l.split("\t")[0] === id);
            if (!line)
                return { code: 1 };
            const text = line.slice(line.indexOf("\t") + 1);
            return /^\[\[ binary data/.test(text) ? { code: 1 } : text;
        }
        if (verb === "wipe") {
            Win.Clipboard.wipe();
            return true;
        }
        return false; // store / delete read stdin
    }

    // power verbs through Caelestia's session actions; every other systemctl/loginctl call: "not active"
    function session(prog, args) {
        const verb = args.filter(a => !a.startsWith("-"))[0] ?? "";
        if (["poweroff", "reboot", "suspend", "hibernate", "suspend-then-hibernate"].includes(verb)) {
            CaeS.SessionManager.exec([prog, verb]);
            return true;
        }
        if (prog === "loginctl" && (verb === "terminate-session" || verb === "terminate-user")) {
            CaeS.SessionManager.exec(["logout"]);
            return true;
        }
        if (prog === "loginctl" && verb === "lock-session") {
            WinSettings.ipc("lock", "lock", []);
            return true;
        }
        return { code: 3 };
    }

    function fonts(monoOnly) {
        let list = Qt.fontFamilies().filter(f => !f.startsWith("@"));
        if (monoOnly)
            list = list.filter(f => /mono|code|consol|courier|cascadia|fixed|terminal|nerd font mono/i.test(f));
        return list.join("\n") + "\n";
    }

    // debugging: what a command would get - `qs ipc call linuxcompat answer "wpctl get-volume @DEFAULT_AUDIO_SINK@"`
    // (it really runs the action, so only ask about read-only commands)
    IpcHandler {
        target: "linuxcompat"

        function answer(command: string): string {
            const r = root.handle(command.split(" "));
            return typeof r === "string" ? r : JSON.stringify(r);
        }
    }

    function sessionMenu() {
        WinSettings.ipc("drawers", "toggle", ["session"]);
    }
}
