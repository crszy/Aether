pragma ComponentBehavior: Bound

// Settings -> All settings: every option in Caelestia's config (shell.json), read straight from the config
// schema, so nothing is left out - bar, launcher, dashboard, lock, appearance, services, everything.
// Switches for on/off options, fields for numbers and text; lists open in shell.json. Each option has a reset.
import QtQuick
import QtQuick.Layouts
import Quickshell
import Caelestia.Config
import Caelestia.I18n
import qs.components
import qs.components.controls
import qs.services
import qs.utils
import qs.modules.nexus.common

PageBase {
    id: root

    title: Tr.tr("All settings")

    property string query: ""
    property string openGroup: ""     // a group shows its options; none open = the list of groups
    property var all: []              // every option, read once from the schema
    property var groups: []           // [{ name, count }]
    property var options: []          // the rows on screen

    // "showOnHover" -> "Show on hover"
    function human(key: string): string {
        if (key.includes(": ")) return key.split(": ")[0] + ": " + human(key.split(": ")[1]);
        const s = key.replace(/([a-z0-9])([A-Z])/g, "$1 $2").replace(/_/g, " ").toLowerCase();
        return s.charAt(0).toUpperCase() + s.slice(1);
    }

    function walk(node: var, prefix: string, out: var, depth: int): void {
        if (!node || depth > 8)
            return;
        for (const key in node) {
            if (key === "objectName" || key.startsWith("_"))
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
            const path = prefix + key;
            if (d.isNode) {
                const child = node[key];
                if (child && child.values !== undefined && child.count !== undefined)
                    out.push({ path, node, key, kind: "list", type: "list" });
                else
                    walk(child, path + ".", out, depth + 1);
                continue;
            }
            const v = node[key];
            const kind = typeof v === "boolean" ? "bool" : typeof v === "number" ? "number" : typeof v === "string" ? "string" : "other";
            out.push({ path, node, key, kind, type: d.type, def: d.defaultValue });
        }
    }

    function rebuild(): void {
        if (!all.length) {
            const found = [], sizes = [];
            try {
                walk(GlobalConfig, "", found, 0);
                walk(TokenConfig, "", sizes, 0);     // shell-tokens.json: sizes of everything, curves, durations
            } catch (e) {
                console.warn("All settings: could not read the config schema:", e);
            }
            for (const o of found)
                o.group = o.path.split(".")[0];
            for (const o of sizes) {
                const p = o.path.split(".");
                o.group = p[0] === "sizes" && p.length > 2 ? `Size: ${p[1]}` : `Tokens: ${p[0]}`;
                o.path = "tokens." + o.path;
                found.push(o);
            }
            const counts = {};
            for (const o of found)
                counts[o.group] = (counts[o.group] || 0) + 1;
            all = found;
            groups = Object.keys(counts).map(name => ({ name, count: counts[name] }));
        }
        const q = query.trim().toLowerCase();
        let shown;
        if (q)
            shown = all.filter(o => o.path.toLowerCase().includes(q) || human(o.key).toLowerCase().includes(q)).slice(0, 80);
        else if (openGroup)
            shown = all.filter(o => o.group === openGroup);
        else
            shown = [];
        shown = shown.map(o => Object.assign({}, o));
        // mark the first/last row of each group (rounded corners, section headers)
        for (let i = 0; i < shown.length; i++) {
            const top = shown[i].group;
            shown[i].first = i === 0 || shown[i - 1].group !== top;
            shown[i].last = false;
            if (i > 0 && shown[i].first)
                shown[i - 1].last = true;
        }
        if (shown.length)
            shown[shown.length - 1].last = true;
        options = shown;
    }

    Component.onCompleted: rebuild()
    onQueryChanged: searchDebounce.restart()
    onOpenGroupChanged: rebuild()

    // (PageBase's children go into its scroll area, which only takes Items - helpers live in properties)
    readonly property Timer searchDebounce: Timer {
        interval: 200
        onTriggered: root.rebuild()
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: root.cappedWidth
        spacing: Tokens.spacing.extraSmall / 2

        TextFieldRow {
            first: true
            last: true
            label: Tr.tr("Search")
            // TRANSLATORS: %1 = number of options
            subtext: Tr.tr("%1 options - changes save to shell.json straight away").arg(root.all.length)
            placeholderText: Tr.tr("bar, clock, rounding, blur…")
            onValueEdited: v => root.query = v
        }

        RowButton {
            Layout.topMargin: Tokens.spacing.small
            first: true
            last: true
            icon: "data_object"
            text: Tr.tr("Open shell.json")
            subtext: `${Paths.config}/shell.json`.replace(/\//g, "\\")
            trailingIcon: "open_in_new"
            onClicked: Qt.openUrlExternally("file:///" + `${Paths.config}/shell.json`)
        }

        // the groups (no search, nothing open)
        SectionHeader {
            visible: !root.query && !root.openGroup
            text: Tr.tr("Groups")
        }

        Repeater {
            model: root.groups

            RowButton {
                required property var modelData
                required property int index

                // hidden, not removed: Caelestia's rows animate out, and swapping the list left ghosts behind
                visible: !root.query && !root.openGroup

                first: index === 0
                last: index === root.groups.length - 1
                icon: ({ appearance: "palette", general: "settings", background: "wallpaper", bar: "dock_to_left", border: "border_outer",
                         dashboard: "dashboard", launcher: "apps", lock: "lock", nexus: "tune", notifs: "notifications", osd: "tune",
                         services: "build", session: "power_settings_new", sidebar: "view_sidebar", utilities: "handyman", paths: "folder" })[modelData.name] ?? "tune"
                text: root.human(modelData.name)
                // TRANSLATORS: %1 = number of options in a group
                subtext: Tr.tr("%1 options").arg(modelData.count)
                trailingIcon: "chevron_right"
                onClicked: root.openGroup = modelData.name
            }
        }

        RowButton {
            visible: !root.query && root.openGroup.length > 0
            Layout.topMargin: Tokens.spacing.small
            first: true
            last: true
            icon: "arrow_back"
            text: Tr.tr("All groups")
            onClicked: root.openGroup = ""
        }

        Repeater {
            model: root.options

            ColumnLayout {
                id: entry

                required property var modelData

                Layout.fillWidth: true
                spacing: 0

                SectionHeader {
                    visible: entry.modelData.first
                    text: root.human(entry.modelData.group)
                }

                Loader {
                    Layout.fillWidth: true
                    sourceComponent: entry.modelData.kind === "bool" ? boolRow : entry.modelData.kind === "list" || entry.modelData.kind === "other" ? infoRow : textRow
                }

                Component {
                    id: boolRow

                    ToggleRow {
                        first: entry.modelData.first
                        last: entry.modelData.last
                        text: root.human(entry.modelData.key)
                        subtext: entry.modelData.path
                        checked: entry.modelData.node[entry.modelData.key]
                        onToggled: entry.modelData.node[entry.modelData.key] = checked
                    }
                }

                Component {
                    id: textRow

                    TextFieldRow {
                        first: entry.modelData.first
                        last: entry.modelData.last
                        smallField: entry.modelData.kind === "number"
                        label: root.human(entry.modelData.key)
                        subtext: entry.modelData.path + (entry.modelData.def !== undefined && entry.modelData.def !== null ? `  ·  default ${entry.modelData.def}` : "")
                        value: String(entry.modelData.node[entry.modelData.key])
                        onEditingFinished: v => {
                            const node = entry.modelData.node, key = entry.modelData.key;
                            if (entry.modelData.kind === "number") {
                                const n = Number(v);
                                if (v.trim().length && !isNaN(n))
                                    node[key] = n;
                            } else {
                                node[key] = v;
                            }
                        }
                    }
                }

                Component {
                    id: infoRow

                    InfoRow {
                        first: entry.modelData.first
                        last: entry.modelData.last
                        label: root.human(entry.modelData.key)
                        subtext: entry.modelData.path + "  ·  " + Tr.tr("edit in shell.json")
                        value: {
                            const v = entry.modelData.node[entry.modelData.key];
                            const n = v && v.count !== undefined ? v.count : (Array.isArray(v) ? v.length : -1);
                            // TRANSLATORS: %1 = number of entries in a list setting
                            return n >= 0 ? Tr.tr("%1 entries").arg(n) : "";
                        }
                    }
                }
            }
        }
    }
}
