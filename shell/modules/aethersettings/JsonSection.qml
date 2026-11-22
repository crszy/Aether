pragma ComponentBehavior: Bound
// Every option in one section of a shell's JSON config, drawn with DarkShadowShell's rows (switches, fields,
// connected buttons) and written straight back to that file. Same logic as modules/rice/JsonOptions.qml.
//   path: the JSON file   section: "a.b" inside it ("" = all)   include / exclude: regexes on the key path
import QtQuick
import Quickshell
import Quickshell.Io
import "catalog.js" as Cat

Column {
    id: root

    property string title
    property string path
    property string section: ""
    property string include: ""
    property string exclude: "pinHash|pinSalt"
    property string query: ""
    property color accent: M3.primary
    property bool active: false
    property var choices: ({})
    property var multiChoices: ({})
    property var doc: null
    property int indent: 2
    readonly property int count: rows.length
    // Simple mode (Prefs.expert off): no file path or Open file, rows that only the file can edit are hidden, and a
    // shell that is not in use starts folded (click its header to open it)
    property bool simple: !Prefs.expert
    property bool userOpen: false
    readonly property bool open: !simple || active || userOpen || query.trim() !== ""
    readonly property var shown: simple ? rows.filter(r => r.kind !== "other") : rows

    readonly property var rows: {
        const out = [];
        const base = sectionValue();
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
                const label = p.map(Cat.human).join(" › ");
                if (q && !label.toLowerCase().includes(q) && !name.toLowerCase().includes(q))
                    continue;
                const kind = multiChoices[name] ? "multi" : (typeof v === "string" && choices[name]) ? "choice" : typeof v === "boolean" ? "bool" : typeof v === "number" ? "number" : typeof v === "string" ? "string" : "other";
                out.push({ keys: p, label: label, value: v, kind: kind });
            }
        };
        walk(base, [], 0);
        for (const key of Object.keys(multiChoices)) {
            if (key in base || (exc && exc.test(key)) || (inc && !inc.test(key)))
                continue;
            const label = Cat.human(key);
            if (q && !label.toLowerCase().includes(q) && !key.toLowerCase().includes(q))
                continue;
            out.push({ keys: [key], label: label, value: [], kind: "multi" });
        }
        return out;
    }

    function sectionValue() {
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
    function setValue(keys, value) {
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

    width: parent ? parent.width : 400
    spacing: 4
    visible: path.length > 0 && (query.trim() === "" || rows.length > 0)

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
    Item {
        width: parent.width
        height: 58
        MouseArea {
            anchors.fill: parent
            enabled: root.simple && !root.active
            cursorShape: Qt.PointingHandCursor
            onClicked: { Sfx.play("select"); root.userOpen = !root.userOpen; }
        }
        Rectangle {
            id: bar
            width: 6
            height: 30
            radius: 3
            color: root.accent
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 6
        }
        Column {
            anchors.left: bar.right
            anchors.leftMargin: 12
            anchors.right: openBtn.left
            anchors.rightMargin: 12
            anchors.verticalCenter: bar.verticalCenter
            spacing: 2
            T {
                kind: "title"
                text: root.title + (root.active ? "  ·  in use" : "")
                font.pixelSize: 16
                font.weight: Font.DemiBold
                width: parent.width
                elide: Text.ElideRight
            }
            T {
                text: root.simple ? (root.active ? "Your desktop uses these now - changes show straight away"
                    : root.doc === null ? "This shell has not made its settings yet"
                    : root.shown.length + (root.shown.length === 1 ? " option · " : " options · ") + (root.open ? "click to fold" : "click to show"))
                    : root.path.replace(/\//g, "\\") + (root.section ? "  →  " + root.section : "")
                color: M3.inkDim
                font.pixelSize: 11
                width: parent.width
                elide: Text.ElideMiddle
            }
        }
        Icon {
            visible: root.simple && !root.active
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: bar.verticalCenter
            name: root.open ? "expand_less" : "expand_more"
            glyph: root.open ? "▴" : "▾"
            size: 22
            color: M3.inkDim
        }
        Btn {
            id: openBtn
            visible: !root.simple
            anchors.right: parent.right
            anchors.verticalCenter: bar.verticalCenter
            variant: "text"
            text: "Open file"
            sym: "open_in_new"
            onClicked: Qt.openUrlExternally("file:///" + root.path)
        }
    }

    T {
        visible: root.doc === null && !root.simple
        text: "Not created yet: it appears once that shell has run (or open the file to create it)."
        color: M3.inkDim
        font.pixelSize: 12
        leftPadding: 18
    }
    T {
        visible: root.open && root.doc !== null && root.shown.length === 0
        text: "No options here."
        color: M3.inkDim
        font.pixelSize: 12
        leftPadding: 18
    }

    Repeater {
        model: root.open ? root.shown : []

        Item {
            id: row
            required property var modelData
            required property int index
            readonly property string key: modelData.keys.join(".")
            width: root.width
            height: loader.item ? loader.item.height : 56

            Loader {
                id: loader
                width: parent.width
                sourceComponent: ({ bool: boolComp, number: numberComp, string: stringComp, choice: choiceComp, multi: multiComp })[row.modelData.kind] || otherComp
            }

            Component {
                id: boolComp
                ToggleRow {
                    label: row.modelData.label
                    checked: row.modelData.value === true
                    onToggled: v => root.setValue(row.modelData.keys, v)
                }
            }
            Component {
                id: numberComp
                SettingRow {
                    label: row.modelData.label
                    reserved: 150
                    Field {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: 120
                        height: 38
                        text: String(row.modelData.value)
                        onAccepted: {
                            const n = Number(text);
                            if (isFinite(n))
                                root.setValue(row.modelData.keys, n);
                        }
                    }
                }
            }
            Component {
                id: stringComp
                SettingRow {
                    label: row.modelData.label
                    reserved: Math.min(340, width * 0.45) + 24
                    Field {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(340, parent.width * 0.45)
                        height: 38
                        text: row.modelData.value
                        onAccepted: root.setValue(row.modelData.keys, text)
                    }
                }
            }
            Component {
                id: choiceComp
                SettingRow {
                    label: row.modelData.label
                    reserved: seg.width + 24
                    Seg {
                        id: seg
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        options: (root.choices[row.key] || []).map(c => ({ value: c, label: c }))
                        current: String(row.modelData.value)
                        onChosen: v => root.setValue(row.modelData.keys, v)
                    }
                }
            }
            Component {
                id: multiComp
                Rectangle {
                    height: multiCol.implicitHeight + 32
                    radius: M3.rM
                    color: M3.surfaceC
                    Column {
                        id: multiCol
                        x: 18
                        y: 16
                        width: parent.width - 36
                        spacing: 10
                        T {
                            text: row.modelData.label
                            font.pixelSize: 14
                            font.weight: Font.Medium
                        }
                        Repeater {
                            model: Object.keys(root.multiChoices[row.key] || {})
                            Column {
                                id: group
                                required property string modelData
                                width: multiCol.width
                                spacing: 6
                                T {
                                    text: group.modelData
                                    color: M3.inkDim
                                    font.pixelSize: 12
                                }
                                Flow {
                                    width: parent.width
                                    spacing: 6
                                    Repeater {
                                        model: root.multiChoices[row.key][group.modelData]
                                        Rectangle {
                                            id: chip
                                            required property string modelData
                                            readonly property var list: Array.isArray(row.modelData.value) ? row.modelData.value : []
                                            readonly property bool on: list.includes(modelData)
                                            width: chipText.implicitWidth + (on ? 40 : 28)
                                            height: 32
                                            radius: 8 * M3.shapeK
                                            color: on ? M3.secondaryContainer : (chipArea.containsMouse ? M3.surfaceHigh : "transparent")
                                            border.width: on ? 0 : 1
                                            border.color: M3.outlineVariant
                                            Row {
                                                anchors.centerIn: parent
                                                spacing: 4
                                                Icon {
                                                    visible: chip.on
                                                    name: "check"
                                                    glyph: "✓"
                                                    size: 14
                                                    color: M3.inkSecondaryC
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                                T {
                                                    id: chipText
                                                    text: chip.modelData.slice(chip.modelData.indexOf(":") + 1)
                                                    font.pixelSize: 12
                                                    color: chip.on ? M3.inkSecondaryC : M3.ink
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                            }
                                            MouseArea {
                                                id: chipArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    Sfx.play("select");
                                                    root.setValue(row.modelData.keys, chip.on ? chip.list.filter(p => p !== chip.modelData) : chip.list.concat([chip.modelData]));
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Component {
                id: otherComp
                SettingRow {
                    label: row.modelData.label
                    reserved: 220
                    T {
                        anchors.right: parent.right
                        anchors.rightMargin: 18
                        anchors.verticalCenter: parent.verticalCenter
                        text: Array.isArray(row.modelData.value) ? "list of " + row.modelData.value.length + " · edit in the file" : "edit in the file"
                        color: M3.inkDim
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
