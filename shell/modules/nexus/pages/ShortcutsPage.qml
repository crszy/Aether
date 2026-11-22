pragma ComponentBehavior: Bound

// Settings -> Shortcuts: Caelestia's default keys (each can be turned off) and your own keys.
// Writes %LOCALAPPDATA%\caelestia\keybinds.json ({"extends": "defaults", "binds": [...]}); Quickshell for
// Windows reloads it as soon as it is saved. Keys look like "Super+Return", "Ctrl+Alt+T", "Super+Shift+F".
// The long lists are built a moment after the page shows, so the page itself opens straight away.
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Caelestia.Config
import Caelestia.I18n
import qs.components
import qs.services
import qs.modules.nexus.common

PageBase {
    id: root

    title: Tr.tr("Shortcuts")

    readonly property string userPath: `${(Quickshell.env("LOCALAPPDATA") || "").replace(/\\/g, "/")}/caelestia/keybinds.json`
    property var defaults: []
    property var mine: []
    property string newKeys: ""
    property string newKind: "exec"       // "exec" | "open" | "caelestia"
    property string newTarget: ""
    property bool listsReady: false

    // every shortcut Caelestia (and Caelestia for Windows) listens for
    readonly property var actions: [
        { name: "launcher", label: Tr.tr("Launcher") }, { name: "wallpaper", label: Tr.tr("Wallpaper picker") },
        { name: "dashboard", label: Tr.tr("Dashboard") }, { name: "nexus", label: Tr.tr("Settings") },
        { name: "sidebar", label: Tr.tr("Sidebar") }, { name: "utilities", label: Tr.tr("Utilities") },
        { name: "session", label: Tr.tr("Power menu") }, { name: "showall", label: Tr.tr("Show all panels") },
        { name: "lock", label: Tr.tr("Lock") }, { name: "taskview", label: Tr.tr("Task view") },
        { name: "switcher", label: Tr.tr("Window switcher") }, { name: "switcherBack", label: Tr.tr("Switch backwards") },
        { name: "screenshot", label: Tr.tr("Screenshot") }, { name: "screenshotClip", label: Tr.tr("Screenshot to clipboard") },
        { name: "screenshotFreeze", label: Tr.tr("Screenshot (freeze)") }, { name: "screenshotFreezeClip", label: Tr.tr("Screenshot (freeze, clipboard)") },
        { name: "mediaToggle", label: Tr.tr("Play / pause") }, { name: "mediaNext", label: Tr.tr("Next track") },
        { name: "mediaPrev", label: Tr.tr("Previous track") }, { name: "mediaStop", label: Tr.tr("Stop media") },
        { name: "brightnessUp", label: Tr.tr("Brightness up") }, { name: "brightnessDown", label: Tr.tr("Brightness down") },
        { name: "toggleDnd", label: Tr.tr("Do not disturb") }, { name: "clearNotifs", label: Tr.tr("Clear notifications") }
    ]

    readonly property var presets: [
        { keys: "Super+Return", kind: "exec", target: "wt.exe", label: Tr.tr("Terminal") },
        { keys: "Super+E", kind: "open", target: Quickshell.env("USERPROFILE") || "C:\\", label: Tr.tr("Your files") },
        { keys: "Super+B", kind: "open", target: "https://", label: Tr.tr("Browser") },
        { keys: "Super+Shift+S", kind: "exec", target: "explorer.exe ms-screenclip:", label: Tr.tr("Screenshot") },
        { keys: "Ctrl+Shift+Escape", kind: "exec", target: "taskmgr.exe", label: Tr.tr("Task Manager") },
        { keys: "Super+W", kind: "caelestia", target: "wallpaper", label: Tr.tr("Wallpaper picker") },
        { keys: "Super+I", kind: "caelestia", target: "nexus", label: Tr.tr("Settings") }
    ]

    // keys need Super, Ctrl or Alt, unless the key is one nothing types (F-keys, media keys, Print...)
    function keyProblem(keys: string): string {
        const k = keys.trim();
        if (!k) return "";
        const parts = k.split("+").map(x => x.trim()).filter(x => x.length);
        const mods = ["super", "win", "ctrl", "control", "alt", "shift"];
        const key = parts.filter(x => !mods.includes(x.toLowerCase()));
        if (key.length !== 1) return Tr.tr("Use one key plus modifiers, like Super+E");
        const solo = /^(f([1-9]|1[0-9]|2[0-4])|print|pause|scroll_lock|volume(up|down|mute)|media.*|launch.*|browser.*)$/i.test(key[0]);
        const hasMod = parts.some(x => ["super", "win", "ctrl", "control", "alt"].includes(x.toLowerCase()));
        if (!hasMod && !solo) return Tr.tr("Add Super, Ctrl or Alt - a plain key would stop that letter typing everywhere");
        return "";
    }

    function describe(b: var): string {
        if (b.action === "exec") return Tr.tr("Run %1").arg(b.command);
        if (b.action === "global") {
            const n = String(b.name ?? "").replace(/^caelestia:/, "");
            return actions.find(a => a.name === n)?.label ?? n;
        }
        if (b.action === "ipc") return `${b.target} ${b["function"]}`;
        if (b.action === "reload") return Tr.tr("Reload the shell");
        if (b.action === "dispatch") return b.dispatch;
        return b.action ?? "";
    }

    function baseOf(b: var): string {
        if (b.action === "global") return `global:${String(b.name).includes(":") ? b.name : "quickshell:" + b.name}`;
        if (b.action === "exec") return `exec:${b.command}`;
        if (b.action === "ipc") return `ipc:${b.target}:${b["function"]}`;
        return b.action ?? "";
    }

    function isOff(b: var): bool {
        return mine.some(m => m.base === baseOf(b) && m.disabled);
    }

    function save(): void {
        userFile.setText(JSON.stringify({ extends: "defaults", binds: mine }, null, 2) + "\n");
    }

    function setOff(b: var, off: bool): void {
        const base = baseOf(b);
        mine = mine.filter(m => m.base !== base).concat(off ? [{ base, disabled: true }] : []);
        save();
    }

    function add(keys: string, kind: string, target: string): void {
        keys = keys.trim();
        target = target.trim();
        if (!keys || !target || keyProblem(keys)) return;
        if (kind === "caelestia" && !actions.some(a => a.name === target)) return;
        let b;
        if (kind === "caelestia") b = { keys, action: "global", name: `caelestia:${target}` };
        else if (kind === "open") b = { keys, action: "exec", command: `explorer.exe "${target}"` };
        else b = { keys, action: "exec", command: target };
        mine = mine.filter(m => m.keys !== keys).concat([b]);
        save();
    }

    function remove(i: int): void {
        const copy = mine.slice();
        copy.splice(i, 1);
        mine = copy;
        save();
    }

    readonly property Timer listsTimer: Timer {
        interval: 60
        running: true
        onTriggered: root.listsReady = true
    }

    readonly property FileView defaultsFile: FileView {
        path: Quickshell.shellPath("defaults/windows/keybinds.json")
        onLoaded: {
            try { root.defaults = JSON.parse(text()).binds ?? []; } catch (e) { root.defaults = []; }
        }
    }

    readonly property FileView userFile: FileView {
        path: root.userPath
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            try { root.mine = JSON.parse(text()).binds ?? []; } catch (e) { root.mine = []; }
        }
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: root.cappedWidth
        spacing: Tokens.spacing.extraSmall / 2

        // ---------------- add ----------------
        SectionHeader {
            first: true
            text: Tr.tr("Add a shortcut")
        }

        TextFieldRow {
            first: true
            label: Tr.tr("Keys")
            subtext: root.keyProblem(root.newKeys) || Tr.tr("Like Super+Return, Ctrl+Alt+T, Super+Shift+F")
            placeholderText: "Super+…"
            value: root.newKeys
            onValueEdited: v => root.newKeys = v
        }

        Repeater {
            model: [
                { id: "exec", icon: "terminal", name: Tr.tr("Run an app or command"), hint: Tr.tr("wt.exe, notepad, code, \"C:\\Program Files\\App\\app.exe\"") },
                { id: "open", icon: "folder_open", name: Tr.tr("Open a folder, file or website"), hint: Tr.tr("C:\\Users\\you\\Downloads or https://...") },
                { id: "caelestia", icon: "auto_awesome", name: Tr.tr("A Caelestia action"), hint: Tr.tr("Pick one below") }
            ]

            RowButton {
                required property var modelData

                icon: modelData.icon
                text: modelData.name
                subtext: modelData.hint
                trailingIcon: root.newKind === modelData.id ? "check_circle" : "radio_button_unchecked"
                onClicked: {
                    if (root.newKind !== modelData.id) root.newTarget = "";
                    root.newKind = modelData.id;
                }
            }
        }

        TextFieldRow {
            visible: root.newKind !== "caelestia"
            label: root.newKind === "open" ? Tr.tr("Folder, file or website") : Tr.tr("Command")
            placeholderText: root.newKind === "open" ? "C:\\Users\\..." : "wt.exe"
            value: root.newKind === "caelestia" ? "" : root.newTarget
            onValueEdited: v => root.newTarget = v
        }

        Flow {
            visible: root.newKind === "caelestia"
            Layout.fillWidth: true
            Layout.topMargin: Tokens.spacing.small
            Layout.bottomMargin: Tokens.spacing.small
            spacing: Tokens.spacing.small

            Repeater {
                model: root.newKind === "caelestia" ? root.actions : []

                StyledRect {
                    id: chip

                    required property var modelData
                    readonly property bool on: root.newTarget === modelData.name

                    implicitWidth: chipText.implicitWidth + Tokens.padding.large * 2
                    implicitHeight: chipText.implicitHeight + Tokens.padding.small * 2
                    radius: Tokens.rounding.full
                    color: on ? Colours.palette.m3secondaryContainer : Colours.tPalette.m3surfaceContainer

                    StyledText {
                        id: chipText
                        anchors.centerIn: parent
                        text: chip.modelData.label
                        color: chip.on ? Colours.palette.m3onSecondaryContainer : Colours.palette.m3onSurfaceVariant
                    }

                    StateLayer {
                        radius: parent.radius
                        onClicked: root.newTarget = chip.modelData.name
                    }
                }
            }
        }

        RowButton {
            last: true
            icon: "add"
            text: Tr.tr("Add")
            disabled: !root.newKeys.trim() || !root.newTarget.trim() || root.keyProblem(root.newKeys).length > 0
                      || (root.newKind === "caelestia" && !root.actions.some(a => a.name === root.newTarget))
            onClicked: {
                root.add(root.newKeys, root.newKind, root.newTarget);
                root.newKeys = "";
                root.newTarget = "";
            }
        }

        SectionHeader {
            text: Tr.tr("Quick add")
        }

        Repeater {
            model: root.listsReady ? root.presets : []

            RowButton {
                required property var modelData
                required property int index

                first: index === 0
                last: index === root.presets.length - 1
                icon: "bolt"
                text: `${modelData.label}  ·  ${modelData.keys}`
                subtext: modelData.kind === "caelestia" ? (root.actions.find(a => a.name === modelData.target)?.label ?? modelData.target) : modelData.target
                trailingIcon: root.mine.some(m => m.keys === modelData.keys) ? "check" : "add"
                onClicked: root.add(modelData.keys, modelData.kind, modelData.target)
            }
        }

        // ---------------- yours ----------------
        SectionHeader {
            text: Tr.tr("Your shortcuts")
        }

        InfoRow {
            visible: root.mine.filter(m => m.keys).length === 0
            first: true
            last: true
            label: Tr.tr("None yet")
            subtext: Tr.tr("Add one above")
        }

        Repeater {
            model: root.listsReady ? root.mine : []

            RowButton {
                required property var modelData
                required property int index

                visible: !!modelData.keys
                icon: "keyboard"
                text: modelData.keys ?? ""
                subtext: root.describe(modelData)
                trailingIcon: "delete"
                onClicked: root.remove(index)
            }
        }

        // ---------------- defaults ----------------
        SectionHeader {
            text: Tr.tr("Caelestia's shortcuts")
        }

        Repeater {
            model: root.listsReady ? root.defaults : []

            ToggleRow {
                required property var modelData
                required property int index

                first: index === 0
                last: index === root.defaults.length - 1
                text: `${modelData.keys}  ·  ${root.describe(modelData)}`
                subtext: root.isOff(modelData) ? Tr.tr("Off - the keys go back to Windows") : ""
                checked: !root.isOff(modelData)
                onToggled: root.setOff(modelData, !checked)
            }
        }

        RowButton {
            Layout.topMargin: Tokens.spacing.small
            first: true
            last: true
            icon: "open_in_new"
            text: Tr.tr("Open keybinds.json")
            subtext: root.userPath.replace(/\//g, "\\")
            onClicked: {
                if (!root.userFile.loaded) root.save();
                Qt.callLater(() => Qt.openUrlExternally("file:///" + root.userPath));
            }
        }
    }
}
