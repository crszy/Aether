// Hyprlock for Aether: draws ~/.config/hypr/hyprlock.conf - the lock screen file of Hyprland rices - as your lock
// screen. Get styles from the AetherStore (Lock screens), or edit the file: it is read again every time you lock.
// Use it: Aether Settings > Lock screen > Hyprlock, or windows.json "mixLock": "user:Hyprlock".
// What it understands (hyprlock's own rules): background, label, image, shape, input-field; position / size in px
// or %, halign / valign, y counted upward; colours rgba(r, g, b, a) / rgba(hex) / rgb() / 0xAARRGGBB / #hex;
// $variables, `source = file`, ## for a literal #; label markup <span foreground="..">, <b>, <i>, <big>, <small>;
// $USER $TIME $TIME12 $FAIL $ATTEMPTS. cmd[update:ms] labels: `date` formats and songs (playerctl / *song* /
// *music* scripts) are answered here; any other command runs in Git's bash (installed with git).
// Make it yours: everything below is plain QML - the drawing of each kind is one component near the bottom.
import QtQuick
import QtQuick.Effects
import Quickshell
import Quickshell.Io
import Quickshell.Widgets
import Quickshell.Windows as WinFs
import qs.services

Item {
    id: root

    property var pam
    property var lock
    property var screen

    readonly property string home: (Quickshell.env("USERPROFILE") || Quickshell.env("HOME") || "").split(String.fromCharCode(92)).join("/")
    readonly property string confPath: `${home}/.config/hypr/hyprlock.conf`
    readonly property string bash: ["C:/Program Files/Git/usr/bin/bash.exe", "C:/Program Files/Git/bin/bash.exe"].find(p => fileExists(p)) ?? ""
    function fileExists(p) { return fileCheck.exists(p); }

    property var items: []      // [{ kind, props }] in file order
    property var general: ({})
    property int attempts: 0
    property string failText: ""
    readonly property int typed: pam?.buffer?.length ?? 0
    readonly property bool checking: !!pam?.passwd?.active
    readonly property bool unlocking: !!lock?.unlocking
    property bool failed: false

    // ---------------------------------------------------------------- reading hyprlock.conf
    function stripComment(line) {   // # starts a comment, ## is a literal #
        let out = "";
        for (let i = 0; i < line.length; i++) {
            if (line[i] === "#") {
                if (line[i + 1] === "#") {
                    out += "#";
                    i++;
                    continue;
                }
                break;
            }
            out += line[i];
        }
        return out;
    }
    function expand(p) {
        return String(p ?? "").trim().replace(/^~/, home).replace(/\$HOME/g, home).split(String.fromCharCode(92)).join("/");
    }
    function parse(text, vars, depth) {
        const out = [];
        let cur = null;
        for (const raw of String(text ?? "").split(/\r?\n/)) {
            const line = stripComment(raw).trim();
            if (!line)
                continue;
            let m = /^([\w-]+)\s*\{$/.exec(line);
            if (m) {
                cur = { kind: m[1], props: {} };
                continue;
            }
            if (line === "}") {
                if (cur)
                    out.push(cur);
                cur = null;
                continue;
            }
            m = /^(\$?[\w:.-]+)\s*=\s*(.*)$/.exec(line);
            if (!m)
                continue;
            let key = m[1], val = m[2].trim();
            val = val.replace(/\$([A-Za-z_]\w*)/g, (s, n) => vars[n] !== undefined ? vars[n] : s);
            if (key.startsWith("$")) {
                vars[key.slice(1)] = val;
            } else if (!cur && key === "source" && depth < 4) {
                const f = expand(val);
                out.push(...parse(fileCheck.read(f), vars, depth + 1));
            } else if (cur) {
                cur.props[key] = val;
            }
        }
        return out;
    }
    function reload() {
        const all = parse(conf.text(), {}, 0);
        root.general = (all.find(i => i.kind === "general") ?? { props: {} }).props;
        root.items = all.filter(i => i.kind !== "general" && root.forThisScreen(i.props.monitor));
    }
    function forThisScreen(mon) {
        mon = String(mon ?? "").trim();
        return !mon || !root.screen || mon === root.screen.name || mon === root.screen.model;
    }

    // ---------------------------------------------------------------- values
    function num(v, d) {
        const n = parseFloat(v);
        return isNaN(n) ? d : n;
    }
    function bool(v) {
        return /^(true|yes|on|1)$/i.test(String(v ?? "").trim());
    }
    function colour(v, d) {
        let s = String(v ?? "").trim();
        if (!s)
            return d;
        let m = /^rgba?\(\s*([0-9a-fA-F]{6,8})\s*\)$/.exec(s);          // rgba(RRGGBBAA)
        if (m)
            s = "#" + m[1];
        m = /^rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*(?:,\s*([\d.]+)\s*)?\)$/.exec(s);
        if (m)
            return Qt.rgba(+m[1] / 255, +m[2] / 255, +m[3] / 255, m[4] === undefined ? 1 : +m[4]);
        m = /^0x([0-9a-fA-F]{8})$/.exec(s);                            // 0xAARRGGBB
        if (m)
            return "#" + m[1];
        m = /^#?([0-9a-fA-F]{8})$/.exec(s);                            // RRGGBBAA -> Qt wants AARRGGBB
        if (m)
            return "#" + m[1].slice(6) + m[1].slice(0, 6);
        m = /^#?([0-9a-fA-F]{6})$/.exec(s);
        if (m)
            return "#" + m[1];
        return d;
    }
    function pair(v, d0, d1, w, h) {   // "x, y" in px or %, -> [x, y]
        const parts = String(v ?? "").split(",").map(s => s.trim());
        const one = (s, full, d) => !s ? d : s.endsWith("%") ? parseFloat(s) / 100 * full : num(s, d);
        return [one(parts[0], w, d0), one(parts[1] ?? parts[0], h, d1)];
    }
    // hyprlock's placing: offset from the aligned spot, y counted from the bottom upward
    function placeX(p, w) {
        const off = pair(p.position, 0, 0, root.width, root.height)[0];
        const a = String(p.halign ?? "center");
        return off + (a === "left" ? 0 : a === "right" ? root.width - w : (root.width - w) / 2);
    }
    function placeY(p, h) {
        const off = pair(p.position, 0, 0, root.width, root.height)[1];
        const a = String(p.valign ?? "center");
        const fromBottom = off + (a === "bottom" ? 0 : a === "top" ? root.height - h : (root.height - h) / 2);
        return root.height - fromBottom - h;
    }
    // fonts: hyprlock names faces the Linux way ("SF Pro Display Bold"); Windows knows the family ("SF Pro Display")
    // plus a weight. Fonts a style brings (~/.config/hypr/**/*.ttf|otf) are loaded here, so they work right away.
    property int fontsTick: 0
    readonly property var fontFiles: {
        items;   // looked again whenever hyprlock.conf is read (a new style may have brought fonts)
        return WinFs.FsUtils.findFiles([`${home}/.config/hypr`], ["*.ttf", "*.otf"], 1, 4);
    }
    readonly property var families: {
        fontsTick;
        const m = {};
        for (const f of Qt.fontFamilies())
            m[f.toLowerCase().replace(/\s+/g, "")] = f;
        return m;
    }
    readonly property var weightWords: /\s+(thin|extralight|ultralight|light|regular|book|medium|semibold|demibold|extrabold|ultrabold|bold|heavy|black|italic|oblique)$/i
    function family(v) {
        let name = String(v ?? "").split(",")[0].trim();
        if (!name)
            return "Sans Serif";
        for (;;) {
            const k = name.toLowerCase().replace(/\s+/g, "");
            const hit = families[k] || families[k + "font"];   // "JetBrains Mono Nerd" = "JetBrainsMono Nerd Font"
            if (hit)
                return hit;
            const shorter = name.replace(weightWords, "");
            if (shorter === name)
                return String(v).split(",")[0].trim();
            name = shorter;
        }
    }
    function weight(v) {
        const s = String(v ?? "").toLowerCase().replace(/\s+/g, "");
        return /black|heavy/.test(s) ? Font.Black : /extrabold|ultrabold/.test(s) ? Font.ExtraBold : /semibold|demibold/.test(s) ? Font.DemiBold : /bold/.test(s) ? Font.Bold : /medium/.test(s) ? Font.Medium : /extralight|ultralight/.test(s) ? Font.ExtraLight : /light/.test(s) ? Font.Light : /thin/.test(s) ? Font.Thin : Font.Normal;
    }
    Repeater {
        model: root.fontFiles

        Item {
            id: fontItem

            required property string modelData

            FontLoader {
                source: "file:///" + fontItem.modelData
                onStatusChanged: if (status === FontLoader.Ready) root.fontsTick++
            }
        }
    }

    // ---------------------------------------------------------------- label text
    function strftime(fmt, d) {
        const pad = (n, l) => String(n).padStart(l ?? 2, "0");
        const days = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"];
        const months = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
        const h12 = d.getHours() % 12 || 12;
        const map = {
            H: pad(d.getHours()), I: pad(h12), l: String(h12), k: String(d.getHours()), M: pad(d.getMinutes()), S: pad(d.getSeconds()),
            p: d.getHours() < 12 ? "AM" : "PM", P: d.getHours() < 12 ? "am" : "pm", A: days[d.getDay()], a: days[d.getDay()].slice(0, 3),
            B: months[d.getMonth()], b: months[d.getMonth()].slice(0, 3), h: months[d.getMonth()].slice(0, 3), d: pad(d.getDate()),
            e: String(d.getDate()).padStart(2, " "), m: pad(d.getMonth() + 1), Y: String(d.getFullYear()), y: pad(d.getFullYear() % 100),
            j: pad(Math.floor((d - new Date(d.getFullYear(), 0, 0)) / 864e5), 3), u: String(d.getDay() || 7), w: String(d.getDay()),
            n: "\n", t: "\t", "%": "%", R: `${pad(d.getHours())}:${pad(d.getMinutes())}`, T: `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`,
            D: `${pad(d.getMonth() + 1)}/${pad(d.getDate())}/${pad(d.getFullYear() % 100)}`, F: `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`
        };
        return fmt.replace(/%-?([A-Za-z%])/g, (s, c) => {
            const v = map[c];
            return v === undefined ? s : s[1] === "-" ? String(v).replace(/^[0 ]+(?=.)/, "") : v;
        });
    }
    function song() {
        const p = Players.active;
        if (!p || !p.trackTitle)
            return "";
        return p.trackArtist ? `${p.trackTitle} - ${p.trackArtist}` : p.trackTitle;
    }
    function vars(s) {
        const d = Time.date;
        return String(s).replace(/\$USER\b/g, Quickshell.env("USERNAME") || "").replace(/\$TIME12\b/g, Qt.formatTime(d, "hh:mm AP"))
            .replace(/\$TIME\b/g, Qt.formatTime(d, "HH:mm")).replace(/\$FAIL\b/g, root.failText).replace(/\$ATTEMPTS\b/g, String(root.attempts))
            .replace(/\$LAYOUT\b/g, "").replace(/\$DESC\b/g, "");
    }
    // a cmd[...] command answered here, or null (then it runs in bash)
    function answer(cmd) {
        let s = String(cmd).trim();
        const d = Time.date;
        // $(date +"FMT") / $(date +FMT) / `date +FMT` anywhere
        s = s.replace(/\$\(\s*date\s+\+?(?:"([^"]*)"|'([^']*)'|(\S+))\s*\)/g, (m, a, b, c) => strftime(a ?? b ?? c, d));
        s = s.replace(/`\s*date\s+\+?(?:"([^"]*)"|'([^']*)'|(\S+))\s*`/g, (m, a, b, c) => strftime(a ?? b ?? c, d));
        // songs: playerctl / scripts named *song* / *music* / *media* / *playing*
        s = s.replace(/\$\(\s*(?:playerctl[^)]*|[^)]*(?:song|music|media|playing|mpris)[^)]*)\)/gi, () => song());
        s = s.replace(/\$\(\s*whoami\s*\)/g, Quickshell.env("USERNAME") || "");
        // a bare date command
        let m = /^date\s+\+?(?:"([^"]*)"|'([^']*)'|(\S+))\s*$/.exec(s);
        if (m)
            return strftime(m[1] ?? m[2] ?? m[3], d);
        if (/\$\(|`/.test(s))
            return null;   // something else: bash
        m = /^(?:echo|printf)(?:\s+-[ne]+)*\s+(?:"([\s\S]*)"|'([\s\S]*)'|([\s\S]*))$/.exec(s);
        if (m)
            return (m[1] ?? m[2] ?? m[3] ?? "").replace(/\\n/g, "\n");
        return null;
    }
    function markup(s) {   // Pango -> Qt rich text
        return String(s).replace(/<span([^>]*)>/g, (m, attrs) => {
            const fg = /(?:foreground|color|fgcolor)\s*=\s*["']([^"']+)["']/.exec(attrs);
            const size = /font_size\s*=\s*["'](\d+)(?:pt)?["']/.exec(attrs);
            const fam = /font_family\s*=\s*["']([^"']+)["']/.exec(attrs);
            let st = "";
            if (fg) {
                const c = root.colour(fg[1], null);
                if (c)
                    st += `color:${c.toString()};`;
            }
            if (size)
                st += `font-size:${size[1]}pt;`;
            if (fam)
                st += `font-family:'${root.family(fam[1])}';`;
            return `<span style="${st}">`;
        }).replace(/<big>/g, "<span style=\"font-size:large\">").replace(/<\/big>/g, "</span>")
            .replace(/<small>/g, "<span style=\"font-size:small\">").replace(/<\/small>/g, "</span>").replace(/\n/g, "<br>");
    }

    // ---------------------------------------------------------------- password state
    onTypedChanged: if (typed > 0) failed = false
    Connections {
        target: root.pam
        ignoreUnknownSignals: true

        function onFlashMsg(): void {
            root.attempts++;
            root.failText = root.pam?.lockMessage || "Wrong password";
            root.failed = true;
        }
    }

    FileView {
        id: conf

        path: root.confPath
        blockLoading: true
        printErrors: false
        watchChanges: true
        onFileChanged: reload()
        onLoaded: root.reload()
        onLoadFailed: root.items = [{ kind: "label", props: { text: "No ~/.config/hypr/hyprlock.conf yet - get a style from the AetherStore (Lock screens)", position: "0, 0", font_size: "16", color: "rgba(255, 255, 255, 0.9)" } }, { kind: "input-field", props: { position: "0, -80" } }]
    }
    QtObject {   // small file helpers for `source =` and the bash lookup
        id: fileCheck

        function exists(p) {
            return WinFs.FsUtils.classify(p) === "file";
        }
        function read(p) {
            if (!exists(p))
                return "";
            srcFile.path = p;
            return srcFile.text() ?? "";
        }
    }
    FileView {   // reads files named by `source = ...`
        id: srcFile

        blockLoading: true
        printErrors: false
    }

    opacity: 0
    Component.onCompleted: opacity = 1
    Behavior on opacity {
        NumberAnimation { duration: root.bool(root.general.no_fade_in) ? 0 : 300 }
    }

    // ---------------------------------------------------------------- drawing, in file order
    Repeater {
        model: root.items

        Loader {
            id: piece

            required property var modelData
            required property int index
            z: modelData.kind === "background" ? -1 : index
            anchors.fill: parent
            sourceComponent: ({ background: backgroundC, label: labelC, image: imageC, shape: shapeC, "input-field": inputC })[modelData.kind] ?? null
            onLoaded: item.p = modelData.props
        }
    }

    Component {
        id: backgroundC

        Item {
            property var p: ({})
            readonly property string path: root.expand(p.path ?? "")
            Rectangle {
                anchors.fill: parent
                visible: !img.visible
                color: root.colour(parent.p.color, path === "screenshot" ? "transparent" : "#111111")
            }
            Image {
                id: img

                anchors.fill: parent
                visible: !!parent.path && parent.path !== "screenshot" && status === Image.Ready
                source: parent.path && parent.path !== "screenshot" ? "file:///" + parent.path : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                layer.enabled: true
                layer.effect: MultiEffect {
                    readonly property real passes: root.num(img.parent.p.blur_passes, 0)
                    blurEnabled: passes > 0
                    blur: Math.min(1, passes * root.num(img.parent.p.blur_size, 7) / 40)
                    blurMax: 64
                    brightness: root.num(img.parent.p.brightness, 1) - 1
                    contrast: root.num(img.parent.p.contrast, 1) - 1
                    saturation: root.num(img.parent.p.vibrancy, 0) * 0.5
                }
            }
        }
    }

    Component {
        id: labelC

        Item {
            id: lab

            property var p: ({})
            readonly property string raw: String(p.text ?? "")
            readonly property var cmd: /^cmd\[([^\]]*)\]\s*([\s\S]*)$/.exec(raw)
            readonly property int every: {
                const m = /update:\s*(\d+)/.exec(cmd?.[1] ?? "");
                return m ? Math.max(200, +m[1]) : 0;
            }
            property string shown: ""
            property int tick: 0

            function refresh() {
                if (!cmd) {
                    shown = root.vars(raw);
                    return;
                }
                const a = root.answer(root.vars(cmd[2]));
                if (a !== null) {
                    shown = a;
                } else if (root.bash && !run.running) {
                    run.command = [root.bash, "-c", cmd[2]];
                    run.running = true;
                }
            }
            Component.onCompleted: Qt.callLater(refresh)
            onRawChanged: refresh()
            Connections {   // $TIME / dates without an update interval still move
                target: Time
                function onDateChanged(): void {
                    if (!lab.cmd || lab.every === 0)
                        lab.refresh();
                }
            }
            Connections {
                target: root
                function onFailTextChanged(): void { lab.refresh(); }
                function onAttemptsChanged(): void { lab.refresh(); }
            }
            Timer {
                running: lab.every > 0
                interval: lab.every
                repeat: true
                onTriggered: lab.refresh()
            }
            Process {
                id: run

                environment: ({ HOME: root.home, USER: Quickshell.env("USERNAME") || "" })
                stdout: StdioCollector {
                    onStreamFinished: lab.shown = text.replace(/\n+$/, "")
                }
            }

            Text {
                id: t

                x: root.placeX(lab.p, implicitWidth)
                y: root.placeY(lab.p, implicitHeight)
                text: root.markup(lab.shown)
                textFormat: Text.RichText
                color: root.colour(lab.p.color, "white")
                font.family: root.family(lab.p.font_family)
                font.weight: root.weight(lab.p.font_family)
                font.pointSize: root.num(lab.p.font_size, 16)
                horizontalAlignment: lab.p.text_align === "right" ? Text.AlignRight : lab.p.text_align === "left" ? Text.AlignLeft : Text.AlignHCenter
                rotation: -root.num(lab.p.rotate, 0)
                style: root.num(lab.p.shadow_passes, 0) > 0 ? Text.Raised : Text.Normal
                styleColor: Qt.alpha("black", 0.5)
            }
        }
    }

    Component {
        id: imageC

        Item {
            id: im

            property var p: ({})
            readonly property real s: root.num(p.size, 150)
            readonly property real r: root.num(p.rounding, -1)
            readonly property real bw: root.num(p.border_size, 4)

            Rectangle {   // the border ring
                x: root.placeX(im.p, im.s + im.bw * 2)
                y: root.placeY(im.p, im.s + im.bw * 2)
                width: im.s + im.bw * 2
                height: width
                radius: im.r < 0 ? width / 2 : im.r + im.bw
                color: root.colour(im.p.border_color, "#dddddd")
                rotation: -root.num(im.p.rotate, 0)

                ClippingRectangle {
                    anchors.fill: parent
                    anchors.margins: im.bw
                    radius: im.r < 0 ? width / 2 : im.r
                    color: "transparent"
                    Image {
                        anchors.fill: parent
                        source: im.p.path ? "file:///" + root.expand(im.p.path) : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
            }
        }
    }

    Component {
        id: shapeC

        Item {
            id: sh

            property var p: ({})
            readonly property var sz: root.pair(p.size, 100, 100, root.width, root.height)
            Rectangle {
                x: root.placeX(sh.p, sh.sz[0])
                y: root.placeY(sh.p, sh.sz[1])
                width: sh.sz[0]
                height: sh.sz[1]
                radius: root.num(sh.p.rounding, -1) < 0 ? Math.min(width, height) / 2 : root.num(sh.p.rounding, 0)
                color: root.colour(sh.p.color, Qt.rgba(1, 1, 1, 0.1))
                border.width: root.num(sh.p.border_size, 0)
                border.color: root.colour(sh.p.border_color, "transparent")
                rotation: -root.num(sh.p.rotate, 0)
            }
        }
    }

    Component {
        id: inputC

        Item {
            id: inp

            property var p: ({})
            readonly property var sz: root.pair(p.size, 400, 90, root.width, root.height)
            readonly property real ot: root.num(p.outline_thickness, 4)
            readonly property real dot: Math.max(2, (sz[1] - ot * 2) * Math.min(0.8, Math.max(0.2, root.num(p.dots_size, 0.33))))
            readonly property color edge: root.failed ? root.colour(p.fail_color, "#cc2222") : root.checking ? root.colour(p.check_color, "#cc8822") : root.colour(p.outer_color, "#111111")
            readonly property bool hideEmpty: root.bool(p.fade_on_empty ?? "true") && root.typed === 0 && !root.failed

            Rectangle {
                id: field

                x: root.placeX(inp.p, inp.sz[0])
                y: root.placeY(inp.p, inp.sz[1])
                width: inp.sz[0]
                height: inp.sz[1]
                radius: root.num(inp.p.rounding, -1) < 0 ? height / 2 : root.num(inp.p.rounding, 0)
                color: inp.edge
                opacity: inp.hideEmpty ? 0.6 : 1
                Behavior on opacity { NumberAnimation { duration: 200 } }
                Behavior on color { ColorAnimation { duration: 150 } }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: inp.ot
                    radius: Math.max(0, parent.radius - inp.ot)
                    color: root.colour(inp.p.inner_color, "#c8c8c8")

                    Text {   // placeholder / fail text
                        anchors.centerIn: parent
                        visible: root.typed === 0
                        text: root.markup(root.failed ? root.vars(inp.p.fail_text ?? "<i>$FAIL</i>") : root.vars(inp.p.placeholder_text ?? "<i>Input Password...</i>"))
                        textFormat: Text.RichText
                        color: root.colour(inp.p.font_color, "#0a0a0a")
                        font.family: root.family(inp.p.font_family)
                        font.weight: root.weight(inp.p.font_family)
                        font.pointSize: Math.max(9, parent.height * 0.28)
                    }
                    Row {   // one dot per character (or the text itself with hide_input = false and a font)
                        anchors.verticalCenter: parent.verticalCenter
                        x: root.bool(inp.p.dots_center ?? "true") ? (parent.width - width) / 2 : parent.height / 2
                        spacing: inp.dot * root.num(inp.p.dots_spacing, 0.15) * 2
                        Repeater {
                            model: root.typed
                            Rectangle {
                                width: inp.dot
                                height: inp.dot
                                radius: root.num(inp.p.dots_rounding, -1) < 0 ? width / 2 : root.num(inp.p.dots_rounding, 0)
                                color: root.colour(inp.p.font_color, "#0a0a0a")
                                scale: 0
                                Component.onCompleted: scale = 1
                                Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutBack } }
                            }
                        }
                    }
                }
            }
        }
    }
}
