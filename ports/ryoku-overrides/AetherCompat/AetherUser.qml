pragma Singleton

// Aether: your own launcher commands (~/.config/caelestia/user/commands.json) for Ryoku's launcher.
// Ryoku only lists them; Aether runs them (ryoku-shell aether run <name> -> qs ipc call user run <name>).
import QtQuick
import Quickshell
import Quickshell.Io

Singleton {
    id: root

    readonly property string home: String(Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").replace(/\\/g, "/")
    property var commands: []

    // rows for Ryoku's launcher: Aether Settings + your commands whose name starts with / contains the query
    function rows(text: string, Spawn: var): var {
        const q = String(text || "").replace(/^>\s*/, "").trim().toLowerCase();
        const out = [];
        const row = (id, title, subtitle, argv) => ({
            id: id, title: title, subtitle: subtitle, icon: "", type: "Aether", score: 0,
            actions: [{ id: "run", name: "Open", icon: "", execute: function () { Spawn.run(argv); } }]
        });
        if ("aether".indexOf(q) === 0 && q.length > 0)
            out.push(row("aether:settings", "Aether Settings", "Every shell, rices and pieces", ["ryoku-shell", "aether"]));
        for (const c of root.commands) {
            const n = String(c.name).toLowerCase();
            if (q.length === 0 || n.indexOf(q) >= 0)
                out.push(row("aether:user:" + c.name, c.name, c.description || "Your command (commands.json)", ["ryoku-shell", "aether", "run", String(c.name)]));
        }
        return out;
    }

    FileView {
        path: `${root.home}/.config/caelestia/user/commands.json`
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            try {
                const j = JSON.parse(text());
                const list = Array.isArray(j) ? j : (j.commands ?? []);
                root.commands = list.filter(c => c && typeof c.name === "string" && c.name.length > 0);
            } catch (e) {
                console.warn("[aether] commands.json:", e);
            }
        }
    }
}
