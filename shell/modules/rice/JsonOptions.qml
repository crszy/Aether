pragma ComponentBehavior: Bound

// Every option in one section of a shell's JSON config, as switches / number fields / text fields, written
// straight back to that file (each shell picks its file up live: Caelestia's per-part files, Noctalia's
// settings.json, windows.json ...). Lists and other values are shown read-only with a button to open the file.
//   path: the JSON file   section: "a.b" inside it ("" = all)   include / exclude: regexes on the key path
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Caelestia.Config
import qs.components
import qs.services

ColumnLayout {
    id: root

    property string title
    property string subtitle
    property string path
    property string section: ""
    property string include: ""
    property string exclude: "pinHash|pinSalt"
    property string query: ""
    property color accent: AetherStyle.palette.m3primary
    property bool active: false        // the shell that draws this part right now

    property var choices: ({})         // key -> [values]: that string option is picked with buttons
    property var multiChoices: ({})    // key -> { group: [ids] }: that list option is toggled with buttons
    property var doc: null             // the whole file
    property int indent: 2
    readonly property var rows: {
        const out = [];
        const base = root.sectionValue();
        if (!base || typeof base !== "object")
            return out;
        const inc = include ? new RegExp(include, "i") : null;
        const exc = exclude ? new RegExp(exclude, "i") : null;
        const q = query.trim().toLowerCase();
        const walk = (obj, prefix, depth) => {
            for (const key of Object.keys(obj)) {
                const v = obj[key];
                const p = prefix.concat([key]);
                const name = p.join(".");
                if (v && typeof v === "object" && !Array.isArray(v) && depth < 4) {
                    walk(v, p, depth + 1);
                    continue;
                }
                if (exc && exc.test(name))
                    continue;
                if (inc && !inc.test(name))
                    continue;
                const label = p.map(root.human).join(" › ");
                if (q && !label.toLowerCase().includes(q) && !name.toLowerCase().includes(q))
                    continue;
                const kind = root.multiChoices[name] ? "multi" : (typeof v === "string" && root.choices[name]) ? "choice" : typeof v === "boolean" ? "bool" : typeof v === "number" ? "number" : typeof v === "string" ? "string" : "other";
                out.push({ keys: p, label, value: v, kind });
            }
        };
        walk(base, [], 0);
        // list options with buttons are offered even before the file has them (they start empty)
        for (const key of Object.keys(root.multiChoices)) {
            if (key in base || (exc && exc.test(key)) || (inc && !inc.test(key)))
                continue;
            const label = root.human(key);
            if (q && !label.toLowerCase().includes(q) && !key.toLowerCase().includes(q))
                continue;
            out.push({ keys: [key], label, value: [], kind: "multi" });
        }
        return out;
    }

    function sectionValue(): var {
        if (!doc)
            return null;
        let v = doc;
        for (const k of section ? section.split(".") : []) {
            if (!v || typeof v !== "object")
                return null;
            v = v[k];
        }
        return v;
    }

    // "lockScreenBlur" -> "Lock screen blur"
    function human(key: string): string {
        const s = String(key).replace(/[_-]+/g, " ").replace(/([a-z0-9])([A-Z])/g, "$1 $2").toLowerCase();
        return s.charAt(0).toUpperCase() + s.slice(1);
    }

    function setValue(keys: var, value: var): void {
        if (!doc)
            return;
        const copy = JSON.parse(JSON.stringify(doc));
        let obj = copy;
        for (const k of section ? section.split(".") : [])
            obj = obj[k];
        for (let i = 0; i < keys.length - 1; i++)
            obj = obj[keys[i]];
        obj[keys[keys.length - 1]] = value;
        doc = copy;
        file.setText(JSON.stringify(copy, null, indent) + "\n");
    }

    spacing: 3
    visible: path.length > 0

    FileView {
        id: file

        path: root.path
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            try {
                const t = text();
                const m = /\n( +)"/.exec(t);
                root.indent = m ? m[1].length : 2;
                root.doc = JSON.parse(t);
            } catch (e) {
                root.doc = null;
            }
        }
        onLoadFailed: root.doc = null
    }

    // header: which shell these options belong to
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 10
        spacing: 10

        Rectangle {
            implicitWidth: 6
            implicitHeight: 30
            radius: 3
            color: root.accent
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            StyledText {
                text: root.title + (root.active ? "  ·  in use" : "")
                font: Tokens.font.title.medium
                color: AetherStyle.palette.m3onSurface
            }
            StyledText {
                Layout.fillWidth: true
                text: root.subtitle || root.path.replace(/\//g, "\\") + (root.section ? "  →  " + root.section : "")
                font: Tokens.font.label.medium
                color: AetherStyle.palette.m3onSurfaceVariant
                elide: Text.ElideMiddle
            }
        }

        GlassButton {
            text: "Open file"
            icon: "open_in_new"
            onClicked: Qt.openUrlExternally("file:///" + root.path)
        }
    }

    StyledText {
        visible: root.doc === null
        text: "Not created yet - it appears once that shell has run (or open the file to create it)."
        color: AetherStyle.palette.m3onSurfaceVariant
        font: Tokens.font.label.medium
    }

    StyledText {
        visible: root.doc !== null && root.rows.length === 0
        text: root.query ? "No option matches the search." : "No options here."
        color: AetherStyle.palette.m3onSurfaceVariant
        font: Tokens.font.label.medium
    }

    Repeater {
        model: root.rows

        Rectangle {
            id: row

            required property var modelData
            required property int index
            readonly property bool multi: modelData.kind === "multi"

            Layout.fillWidth: true
            implicitHeight: multi ? control.implicitHeight + 64 : 56
            radius: 6
            topLeftRadius: row.index === 0 ? 20 : 6
            topRightRadius: row.index === 0 ? 20 : 6
            bottomLeftRadius: row.index === root.rows.length - 1 ? 20 : 6
            bottomRightRadius: row.index === root.rows.length - 1 ? 20 : 6
            color: Qt.alpha(hover.hovered ? AetherStyle.palette.m3surfaceContainerHigh : AetherStyle.palette.m3surfaceContainer, 0.82)

            Behavior on color {
                ColorAnimation {
                    duration: 120
                }
            }

            HoverHandler {
                id: hover
            }

            StyledText {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.right: row.multi ? parent.right : control.left
                anchors.rightMargin: 12
                anchors.verticalCenter: row.multi ? undefined : parent.verticalCenter
                anchors.top: row.multi ? parent.top : undefined
                anchors.topMargin: 16
                text: row.modelData.label
                color: AetherStyle.palette.m3onSurface
                font: Tokens.font.body.medium
                elide: Text.ElideRight
            }

            Item {
                id: control

                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: row.multi ? undefined : parent.verticalCenter
                // a list of buttons goes under the label, full width
                anchors.left: row.multi ? parent.left : undefined
                anchors.leftMargin: 14
                anchors.bottom: row.multi ? parent.bottom : undefined
                anchors.bottomMargin: 14
                implicitWidth: loader.implicitWidth
                implicitHeight: loader.implicitHeight

                Loader {
                    id: loader

                    width: row.multi ? control.width : implicitWidth
                    sourceComponent: ({ bool: boolComp, number: numberComp, string: stringComp, choice: choiceComp, multi: multiComp })[row.modelData.kind] ?? otherComp
                }
            }

            Component {
                id: choiceComp

                Row {
                    spacing: 6

                    Repeater {
                        model: root.choices[row.modelData.keys.join(".")] ?? []

                        GlassButton {
                            required property string modelData

                            text: modelData
                            accent: root.accent
                            highlighted: row.modelData.value === modelData
                            onClicked: root.setValue(row.modelData.keys, modelData)
                        }
                    }
                }
            }

            Component {
                id: multiComp

                // one group of toggle buttons per shell; the list keeps the order they were switched on in
                Column {
                    spacing: 10

                    Repeater {
                        model: Object.keys(root.multiChoices[row.modelData.keys.join(".")] ?? {})

                        Column {
                            id: group

                            required property string modelData

                            width: parent.width
                            spacing: 6

                            StyledText {
                                text: group.modelData
                                color: AetherStyle.palette.m3onSurfaceVariant
                                font: Tokens.font.label.medium
                            }

                            Flow {
                                width: parent.width
                                spacing: 6

                                Repeater {
                                    model: root.multiChoices[row.modelData.keys.join(".")][group.modelData]

                                    GlassButton {
                                        required property string modelData
                                        readonly property var list: Array.isArray(row.modelData.value) ? row.modelData.value : []
                                        readonly property bool on: list.includes(modelData)

                                        text: modelData.slice(modelData.indexOf(":") + 1)
                                        accent: root.accent
                                        highlighted: on
                                        onClicked: root.setValue(row.modelData.keys, on ? list.filter(p => p !== modelData) : list.concat([modelData]))
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Component {
                id: boolComp

                GlassSwitch {
                    accent: root.accent
                    checked: row.modelData.value
                    onToggled: v => root.setValue(row.modelData.keys, v)
                }
            }

            Component {
                id: numberComp

                GlassNumber {
                    value: row.modelData.value
                    onCommitted: v => root.setValue(row.modelData.keys, v)
                }
            }

            Component {
                id: stringComp

                GlassField {
                    text: row.modelData.value
                    onCommitted: v => root.setValue(row.modelData.keys, v)
                }
            }

            Component {
                id: otherComp

                StyledText {
                    text: Array.isArray(row.modelData.value) ? `list of ${row.modelData.value.length} - edit in the file` : "edit in the file"
                    color: AetherStyle.palette.m3onSurfaceVariant
                    font: Tokens.font.label.medium
                }
            }
        }
    }
}
