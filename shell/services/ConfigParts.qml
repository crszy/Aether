pragma Singleton
pragma ComponentBehavior: Bound

// One config file per part of the shell, so you (or an AI) can open just the part you want to change:
//   ~/.config/caelestia/bar.json, lock.json, launcher.json, notifs.json, dashboard.json, ...
// Each holds EVERY option of that part with its current value (not just the ones you changed). Edit and save
// one and the shell applies it at once (it ends up in shell.json, which stays the real store). Options changed
// in the Settings window show up in these files a few seconds later. A missing file is written again.
// README.txt in the same folder lists where every config file lives.
import QtQuick
import Quickshell
import Quickshell.Io
import Caelestia.Config
import qs.utils

Singleton {
    id: root

    readonly property string dir: Paths.config
    readonly property string appConfig: Paths.appConfig
    // the parts of GlobalConfig that get a file ("paths" etc. included: everything at the top level)
    property var groups: []
    property var lastWritten: ({})      // group -> the text we last wrote, so our own writes are not re-applied

    function isList(v: var): bool {
        return v && v.values !== undefined && v.count !== undefined;
    }

    // a config node -> plain JSON (lists as arrays of their values)
    function dump(node: var, depth: int): var {
        const out = {};
        if (!node || depth > 8)
            return out;
        for (const key in node) {
            if (key === "objectName" || key.startsWith("_") || key.endsWith("Changed") || typeof node[key] === "function")
                continue;
            if (typeof node.hasOption === "function" && !node.hasOption(key))
                continue; // not an option (a signal, method...): no warning
            let d;
            try {
                d = node.descriptorFor(key);
            } catch (e) {
                continue;
            }
            if (!d || d.key !== key)
                continue;
            const v = node[key];
            if (d.isNode) {
                if (isList(v)) {
                    try {
                        out[key] = JSON.parse(JSON.stringify(v.values));
                    } catch (e) {}
                } else {
                    out[key] = dump(v, depth + 1);
                }
            } else if (typeof v === "boolean" || typeof v === "number" || typeof v === "string") {
                out[key] = v;
            } else {
                try {
                    out[key] = JSON.parse(JSON.stringify(v));
                } catch (e) {}
            }
        }
        return out;
    }

    // plain JSON -> a config node (only options that exist and changed; lists are left to shell.json)
    function apply(node: var, obj: var, depth: int): int {
        let changed = 0;
        if (!node || !obj || typeof obj !== "object" || depth > 8)
            return 0;
        for (const key in obj) {
            if (typeof node.hasOption === "function" && !node.hasOption(key))
                continue; // not an option (a signal, method...): no warning
            let d;
            try {
                d = node.descriptorFor(key);
            } catch (e) {
                continue;
            }
            if (!d || d.key !== key)
                continue;
            const want = obj[key];
            const cur = node[key];
            if (d.isNode) {
                if (!isList(cur))
                    changed += apply(cur, want, depth + 1);
                continue;
            }
            if (typeof want !== typeof cur && !(typeof cur === "object"))
                continue;
            if (JSON.stringify(cur) !== JSON.stringify(want)) {
                try {
                    node[key] = want;
                    changed++;
                } catch (e) {
                    console.warn(`config part: could not set ${key}:`, e);
                }
            }
        }
        return changed;
    }

    function textFor(group: string): string {
        return JSON.stringify(dump(GlobalConfig[group], 0), null, 2) + "\n";
    }

    function writeChanged(): void {
        for (let i = 0; i < files.count; i++) {
            const f = files.objectAt(i);
            if (!f || !f.ready)
                continue;
            const text = textFor(f.group);
            if (text !== root.lastWritten[f.group] && text !== f.lastSeen) {
                root.lastWritten[f.group] = text;
                f.lastSeen = text;
                f.setText(text);
            }
        }
    }

    Component.onCompleted: {
        const found = [];
        for (const key in GlobalConfig) {
            if (key === "objectName" || key.startsWith("_") || key.endsWith("Changed") || typeof GlobalConfig[key] === "function")
                continue;
            if (typeof GlobalConfig.hasOption === "function" && !GlobalConfig.hasOption(key))
                continue; // not an option (a signal, method...): no warning
            let d;
            try {
                d = GlobalConfig.descriptorFor(key);
            } catch (e) {
                continue;
            }
            if (d && d.key === key && d.isNode && !isList(GlobalConfig[key]))
                found.push(key);
        }
        groups = found;
        readme.setText(readmeText());
    }

    function readmeText(): string {
        const w = p => p.replace(/\//g, "\\");
        return [
            "Caelestia for Windows - where your config lives",
            "",
            `This folder (${w(root.dir)}):`,
            ...root.groups.map(g => `  ${g}.json`.padEnd(24) + `every "${g}" option with its current value - edit + save = applied at once`),
            "  windows.json            Windows-only options (taskbar, lock style, PIN, Settings window, notifications)",
            "  taskbar.json            the running-apps list on the bar (icons, badges, grouping, clicks)",
            "",
            `Main store (${w(root.appConfig)}):`,
            "  shell.json              the options you changed (the files above are written into it)",
            "  shell-tokens.json       sizes, roundings, animation curves and durations",
            "  keybinds.json           keyboard shortcuts",
            "  monitors\\<name>\\        per-monitor overrides of shell.json",
            "",
            "Other:",
            `  ${w(Paths.home)}\\.config\\hypr\\hyprland.conf   Hyprland mode (tiling rules, binds)`,
            `  ${w(Paths.home)}\\komorebi.json                   Aether tiling (komorebi)`,
            `  ${w(Paths.home)}\\.config\\whkdrc                 Aether tiling keys`,
            `  ${w(Quickshell.shellDir)}   the shell's QML files`,
            "",
            "Tip: send any of these files to an AI and ask it to change what you want, then save it back here.",
            ""
        ].join("\r\n");
    }

    FileView {
        id: readme

        path: `${root.dir}/README.txt`
        printErrors: false
    }

    Instantiator {
        id: files

        model: root.groups

        FileView {
            id: part

            required property string modelData
            readonly property string group: modelData
            property bool ready: false
            property string lastSeen: ""

            path: `${root.dir}/${group}.json`
            watchChanges: true
            printErrors: false
            onFileChanged: reload()

            onLoaded: {
                const t = text();
                ready = true;
                if (t === part.lastSeen || t === root.lastWritten[group])
                    return;
                part.lastSeen = t;
                try {
                    const n = root.apply(GlobalConfig[group], JSON.parse(t), 0);
                    if (n > 0)
                        console.log(`${group}.json: applied ${n} change(s)`);
                } catch (e) {
                    console.warn(`${group}.json: ${e} - not applied`);
                }
            }
            onLoadFailed: err => {
                // an editor that saves by replacing the file leaves it unreadable for a moment: read again
                if (err !== FileViewError.FileNotFound) {
                    retry.restart();
                    return;
                }
                // missing: write it with every option
                ready = true;
                const t = root.textFor(group);
                root.lastWritten[group] = t;
                part.lastSeen = t;
                setText(t);
            }

            property Timer retry: Timer {
                interval: 250
                onTriggered: part.reload()
            }
        }
    }

    // options changed in the Settings window (or in shell.json) reach the part files
    Timer {
        interval: 4000
        running: root.groups.length > 0
        repeat: true
        onTriggered: root.writeChanged()
    }
}
