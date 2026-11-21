pragma Singleton

// Aether user folder (~/.config/caelestia/user): your own Quickshell parts, no dev build needed.
//   widgets/<Name>.qml   a bar widget - put "user:<Name>" in any bar (Caelestia's bar.json, or barPieces)
//   panels/<name>.qml    a panel that opens over the desktop - from a launcher command, a widget or IPC
//   commands.json        your own launcher commands (">profile" ...): open a panel, run a program, change a
//                        setting, call IPC, open a link
// Files are plain QML and can `import qs.services`, `qs.components`, `qs.utils` like Aether's own parts do.
// Saving a file reloads it live. A broken file shows its error instead of taking anything down.
//   lock/<Name>.qml      a whole lock screen (windows.json "mixLock": "user:<Name>"), or a piece of Caelestia's
//                        lock ("lockClock" / "lockMedia": "user:<Name>")
//   osd/<Name>.qml       the volume / brightness pop-up ("mixOsd": "user:<Name>")
//   dashboard/<Name>.qml an extra tab in the dashboard
//   desktop/<Name>.qml   a widget on the desktop; where it sits: desktop.json
// A file whose name starts with "_" is ignored (switched off).
// IPC: qs ipc call user panel <name> | close | run <command name> | list
import QtQuick
import Qt.labs.folderlistmodel
import Quickshell
import Quickshell.Io
import Quickshell.Windows as WinFs
import qs.utils

Singleton {
    id: root

    readonly property string templateVersion: "8"   // bump when modules/userpanel/template gains new examples
    readonly property string dir: `${Paths.config}/user`

    // the files in each folder (names without .qml), live
    readonly property var widgetNames: widgetsDir.names
    readonly property var panelNames: panelsDir.names
    readonly property var lockNames: lockDir.names
    readonly property var osdNames: osdDir.names
    readonly property var dashboardNames: dashboardDir.names
    readonly property var desktopNames: desktopDir.names
    readonly property var modNames: modsDir.names        // mods/<Name>.qml: code that runs with Aether (like Windhawk mods)
    readonly property var skinNames: skinsDir.names      // skins/<Name>/: a theme folder (style.json + parts)
    property var desktopConfig: ({})       // desktop.json: { "<Name>": { screen, corner, x, y } }

    // "user:<Name>" -> "<Name>", anything else -> ""
    function userName(value: string): string {
        const v = String(value ?? "");
        return v.startsWith("user:") && validName(v.slice(5)) ? v.slice(5) : "";
    }
    property string panel: ""            // the open panel's name ("" = none)
    property int panelRev: 0             // bumped on every open: the file is read fresh each time
    // [{ name, description, icon, panel | exec | set | ipc | url }]: commands.json, then one "config <name>" per
    // Quickshell config in ~/.config/quickshell (runs it, like `qs -c <name>`)
    readonly property var commands: fileCommands.concat(configCommands)
    property var fileCommands: []
    property var configCommands: []
    function scanConfigs(): void {
        const base = `${Paths.home}/.config/quickshell`;
        const launcher = WinSettings.packageDir ? `${WinSettings.packageDir}/Caelestia.exe` : "";
        const running = String(Quickshell.shellDir).replace(/\\/g, "/").replace(/\/$/, "").split("/").pop();
        root.configCommands = !launcher ? [] : (WinFs.FsUtils.listDetailed(base, false) ?? []).filter(e => e.isDir).map(e => e.name)
            .filter(n => n !== "ryoku" && n !== running && !/\.backup-\d/.test(n) && WinFs.FsUtils.classify(`${base}/${n}/shell.qml`) === "file")
            .map(n => ({ name: `config ${n}`, description: n === "aether" ? "Back to Aether" : `Run the Quickshell config "${n}" instead of Aether`, icon: "code", exec: [launcher, "--config", n] }));
    }
    Component.onCompleted: scanConfigs()
    property string commandsError: ""
    property bool seeded: false

    function fileUrl(kind: string, name: string, rev: int): string {
        return `file:///${root.dir}/${kind}/${name}.qml?v=${rev}`;
    }

    function filePath(kind: string, name: string): string {
        return `${root.dir}/${kind}/${name}.qml`;
    }

    // is `path` inside folder `dir`? (either slash, any case - Windows)
    function under(path: var, dir: string): bool {
        const norm = s => String(s ?? "").split(String.fromCharCode(92)).join("/").toLowerCase();
        return norm(path).startsWith(norm(dir) + "/");
    }

    function validName(name: string): bool {
        return /^[A-Za-z0-9][\w-]*$/.test(name ?? "");
    }

    function openPanel(name: string): void {
        if (!validName(name))
            return;
        root.panel = name;   // name first: the window rebuilds on panelRev
        root.panelRev++;
    }

    function closePanel(): void {
        root.panel = "";
    }

    function togglePanel(name: string): void {
        if (root.panel === name)
            closePanel();
        else
            openPanel(name);
    }

    function find(name: string): var {
        const n = String(name ?? "").toLowerCase();
        return root.commands.find(c => String(c.name).toLowerCase() === n) ?? null;
    }

    // runs one command object from commands.json; returns "" or what went wrong
    function runCommand(c: var): string {
        if (!c)
            return "no such command";
        if (c.panel)
            openPanel(String(c.panel));
        if (c.exec) {
            const argv = Array.isArray(c.exec) ? c.exec.map(String) : ["cmd.exe", "/c", String(c.exec)];
            Quickshell.execDetached(argv);
        }
        if (c.set && typeof c.set === "object")
            for (const k of Object.keys(c.set))
                if (k in WinSettings)
                    WinSettings[k] = c.set[k];
        if (Array.isArray(c.ipc) && c.ipc.length >= 2)
            WinSettings.ipc(String(c.ipc[0]), String(c.ipc[1]), c.ipc.slice(2).map(String));
        if (c.url)
            Qt.openUrlExternally(String(c.url));
        return "";
    }

    function run(name: string): string {
        return runCommand(find(name));
    }

    // copy the examples in (assets/seed-examples.ps1): missing ones are added, ones you never changed are updated,
    // files you changed are left alone; README.txt is always the current one
    function seed(): void {
        const tpl = Quickshell.shellPath("modules/userpanel/template").replace(/\//g, "\\");
        const dst = root.dir.replace(/\//g, "\\");
        Quickshell.execDetached(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", Quickshell.shellPath("assets/seed-examples.ps1").replace(/\//g, "\\"), "-From", tpl, "-To", dst]);
        seedReload.restart();
    }

    Timer {
        id: seedReload

        interval: 4000
        onTriggered: {
            commandsFile.reload();
            root.rescan();   // new example folders (skins/, mods/) appear without a restart
        }
    }

    FileView {
        id: commandsFile

        path: `${root.dir}/commands.json`
        watchChanges: true
        onFileChanged: reload()
        onLoaded: {
            try {
                const j = JSON.parse(text());
                const list = Array.isArray(j) ? j : (j.commands ?? []);
                root.fileCommands = list.filter(c => c && typeof c.name === "string" && c.name.length > 0);
                root.scanConfigs();
                root.commandsError = "";
            } catch (e) {
                root.commandsError = `commands.json: ${e}`;
                console.warn("[user]", root.commandsError);
            }
        }
        onLoadFailed: root.fileCommands = []
    }

    // examples: copied in on first start, and again when a newer Aether brings new ones (.version). robocopy
    // only adds missing files - nothing you changed or wrote is touched; an example you deleted comes back
    // only after an update that brings new examples.
    FileView {
        id: versionFile

        path: `${root.dir}/.version`
        printErrors: false
        onLoaded: {
            if (text().trim() !== root.templateVersion)
                root.seedOnce();
        }
        onLoadFailed: root.seedOnce()
    }

    function seedOnce(): void {
        if (root.seeded)
            return;
        root.seeded = true;
        root.seed();
        versionWrite.restart();
    }

    Timer {
        id: versionWrite

        interval: 2500
        onTriggered: {
            versionFile.setText(root.templateVersion + "\n");
            root.rescan();
        }
    }

    FileView {
        id: desktopFile

        path: `${root.dir}/desktop.json`
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            try {
                root.desktopConfig = JSON.parse(text()) ?? {};
            } catch (e) {
                console.warn("[user] desktop.json:", e);
            }
        }
        onLoadFailed: root.desktopConfig = {}
    }

    // folders made after start are not watched yet: look again (after copying examples; `qs ipc call user rescan`)
    function rescan(): void {
        for (const d of [widgetsDir, panelsDir, lockDir, osdDir, dashboardDir, desktopDir, modsDir, skinsDir]) {
            const f = d.folder;
            d.folder = "";
            d.folder = f;
        }
        desktopFile.reload();
        scanConfigs();
    }

    component KindDir: FolderListModel {
        required property string kind
        required property string base
        property var names: []

        function refresh(): void {
            const a = [];
            for (let i = 0; i < count; i++) {
                // a folder that did not exist yet makes Qt list its working folder instead: only keep real entries
                if (!root.under(get(i, "filePath"), `${base}/${kind}`))
                    continue;
                const n = String(get(i, "fileBaseName") ?? "");
                if (n && !n.startsWith("_") && /^[A-Za-z0-9][\w-]*$/.test(n))
                    a.push(n);
            }
            names = a;
        }

        folder: `file:///${base}/${kind}`
        nameFilters: ["*.qml"]
        showDirs: false
        showDotAndDotDot: false
        sortField: FolderListModel.Name
        onCountChanged: refresh()
        onStatusChanged: refresh()
    }

    KindDir {
        id: widgetsDir

        kind: "widgets"
        base: root.dir
    }

    KindDir {
        id: panelsDir

        kind: "panels"
        base: root.dir
    }

    KindDir {
        id: lockDir

        kind: "lock"
        base: root.dir
    }

    KindDir {
        id: osdDir

        kind: "osd"
        base: root.dir
    }

    KindDir {
        id: dashboardDir

        kind: "dashboard"
        base: root.dir
    }

    KindDir {
        id: desktopDir

        kind: "desktop"
        base: root.dir
    }

    KindDir {
        id: modsDir

        kind: "mods"
        base: root.dir
    }

    // skins are folders: skins/<Name>/style.json, settings-back.qml, settings-front.qml ...
    FolderListModel {
        id: skinsDir

        property var names: []

        function refresh(): void {
            const a = [];
            for (let i = 0; i < count; i++) {
                if (!root.under(get(i, "filePath"), `${root.dir}/skins`))
                    continue;
                const n = String(get(i, "fileName") ?? "");
                if (n && !n.startsWith("_") && /^[A-Za-z0-9][\w-]*$/.test(n))
                    a.push(n);
            }
            names = a;
        }

        folder: `file:///${root.dir}/skins`
        showFiles: false
        showDirs: true
        showDotAndDotDot: false
        sortField: FolderListModel.Name
        onCountChanged: refresh()
        onStatusChanged: refresh()
    }

    IpcHandler {
        function panel(name: string): void {
            root.openPanel(name);
        }

        function toggle(name: string): void {
            root.togglePanel(name);
        }

        function close(): void {
            root.closePanel();
        }

        function run(name: string): string {
            return root.run(name) || "ok";
        }

        function list(): string {
            return root.commands.map(c => c.name).join("\n");
        }

        // every file Aether sees in your folder, by kind
        function rescan(): void {
            root.rescan();
        }

        function files(): string {
            return [`widgets: ${root.widgetNames.join(", ")}`, `panels: ${root.panelNames.join(", ")}`,
                `lock: ${root.lockNames.join(", ")}`, `osd: ${root.osdNames.join(", ")}`,
                `dashboard: ${root.dashboardNames.join(", ")}`, `desktop: ${root.desktopNames.join(", ")}`,
                `mods: ${root.modNames.join(", ")}`, `skins: ${root.skinNames.join(", ")}`].join("\n");
        }

        // turn a mod on / off without renaming files: qs ipc call user mod <Name> on|off
        function mod(name: string, state: string): string {
            if (!root.validName(name))
                return "bad name";
            const off = (WinSettings.modsOff ?? []).filter(n => n !== name);
            if (state === "off")
                off.push(name);
            WinSettings.modsOff = off;
            return `${name}: ${state === "off" ? "off" : "on"}`;
        }

        function mods(): string {
            return root.modNames.map(n => `${n}${(WinSettings.modsOff ?? []).includes(n) ? " (off)" : ""}`).join("\n");
        }

        target: "user"
    }
}
