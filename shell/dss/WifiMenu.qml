// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick

// The Wi-Fi menu: click the Wi-Fi icon on the bar and it drops down under it. Networks, signal, connect (asks for the
// password when it needs one), disconnect, forget, Wi-Fi on / off. It talks to NetworkManager through nmcli.
PanelWindow {
    screen: Mon.screen
    id: wm
    property bool open: false
    property real t: 0
    // t runs evenly; the menu eases on its own: it grows out of the bar (quick start, soft landing) and
    // folds back up quickly when it closes
    Behavior on t { NumberAnimation { duration: wm.open ? 520 : 200; easing.type: Easing.Linear } }
    readonly property real et: 1 - Math.pow(1 - t, 4)
    onOpenChanged: t = open ? 1 : 0

    // ---------- look ----------
    readonly property color fg: "#F6F5F4"
    readonly property color dim: Qt.rgba(0.965, 0.961, 0.957, 0.6)
    readonly property color line: Qt.rgba(1, 1, 1, 0.09)
    readonly property color hot: Qt.rgba(1, 1, 1, 0.07)
    readonly property real alpha: Look.flat ? 1 : 0.62
    readonly property color bg: Qt.rgba(0.05, 0.05, 0.05, alpha)
    property var bc: ({})
    readonly property bool atBottom: bc.position === "bottom"
    readonly property real barSpace: (bc.enabled === false ? 34 : (bc.height ? bc.height : 38)) + (bc.style === "flat" ? 0 : 16) + 8

    // ---------- data ----------
    property real cx: 0
    // the bar on the left or right edge (VBar.qml): the menu opens beside the bar, level with the icon (cy)
    property real cy: -1
    readonly property bool side: bc.position === "left" || bc.position === "right"
    readonly property bool atRight: bc.position === "right"
    readonly property real sideSpace: (bc.enabled === false ? 34 : (bc.height ? bc.height + 4 : 42)) + (bc.style === "flat" ? 0 : 16) + 8
    property bool radio: true
    property var nets: []
    property var known: ({})
    property string askFor: ""
    property string busyFor: ""
    property string msg: ""
    readonly property int cardW: 360
    readonly property int rowH: 54
    readonly property int maxRows: 7

    function split(line) {                 // nmcli -t: fields split by ":", a ":" inside a field is "\:"
        const out = []
        let cur = ""
        for (let i = 0; i < line.length; i++) {
            const c = line[i]
            if (c === "\\" && i + 1 < line.length) { cur += line[i + 1]; i++ }
            else if (c === ":") { out.push(cur); cur = "" }
            else cur += c
        }
        out.push(cur)
        return out
    }
    property int emptyN: 0
    function parse(text) {
        const parts = text.split("@@\n")
        if (parts.length < 3) return          // nmcli was cut off (timeout / hiccup): keep what is shown
        const best = {}
        const lines = (parts[0] || "").split("\n")
        for (let i = 0; i < lines.length; i++) {
            if (lines[i] === "") continue
            const f = split(lines[i])
            const ssid = f[1]
            if (!ssid) continue
            const n = { ssid: ssid, active: f[0] === "*", signal: parseInt(f[2]) || 0, secure: (f[3] || "") !== "" && f[3] !== "--" }
            const o = best[ssid]
            if (!o || n.active || (!o.active && n.signal > o.signal)) best[ssid] = n
        }
        radio = ((parts[1] || "").trim() !== "disabled")
        const k = {}
        const kl = (parts[2] || "").split("\n")
        for (let i = 0; i < kl.length; i++) { const f = split(kl[i]); if (f[1] === "802-11-wireless" || f[1] === "wifi") k[f[0]] = true }
        known = k
        const list = Object.keys(best).map(function(s) { const n = best[s]; n.known = k[s] === true; return n })
        list.sort(function(a, b) { return a.active !== b.active ? (a.active ? -1 : 1) : b.signal - a.signal })
        // NetworkManager answers with an empty list for a moment while it scans or connects: keep the last list
        // for a few reads instead of showing "no networks" (that was the list that sometimes went blank)
        if (list.length === 0 && nets.length > 0 && radio && emptyN < 3) { emptyN++; return }
        emptyN = 0
        nets = list
    }

    DssProc {
        id: stateProc
        command: ["bash", "-c", "timeout 8 nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY dev wifi list 2>/dev/null; echo '@@'; timeout 4 nmcli radio wifi 2>/dev/null; echo '@@'; timeout 4 nmcli -t -f NAME,TYPE connection show 2>/dev/null"]
        stdout: DssCollector { onStreamFinished: wm.parse(text) }
    }
    DssProc {
        id: barProc
        command: ["bash", "-c", "\"$HOME/.local/bin/rice-bar\" state 2>/dev/null"]
        stdout: DssCollector { onStreamFinished: { try { wm.bc = JSON.parse(text) } catch (e) { } } }
    }
    DssProc {
        id: act
        stderr: DssCollector { id: actErr }
        onExited: function(code) {
            wm.busyFor = ""
            if (code === 0) { wm.msg = ""; wm.askFor = ""; Sfx.play("done") }
            else if (code === 124) { wm.msg = "Timed out. Try again"; Sfx.play("remove") }
            else {
                const e = (actErr.text || "").trim()
                wm.msg = /secrets were required|password|802-1x|auth/i.test(e) ? "Wrong password, or the network needs a different login" : (e !== "" ? e.split("\n")[0].replace(/^Error: /, "") : "Could not connect")
                Sfx.play("remove")
            }
            refreshTimer.restart()
        }
    }
    // one action at a time; a stuck one (nmcli waiting for a network) is stopped after 45 s so the next click always works
    function nm(args, label) {
        if (act.running) { act.signal(15); return }   // stop the stuck one; the next click goes through
        act.command = ["timeout", "45", "nmcli"].concat(args); busyFor = label || ""; act.running = true
    }
    // a saved profile can be named differently than its network ("Home 1"): fall back to connecting by SSID
    function nmKnown(ssid) {
        if (act.running) { act.signal(15); return }
        act.command = ["bash", "-c", "e=$(timeout 45 nmcli connection up id \"$1\" 2>&1); r=$?; if [ $r -ne 0 ] && echo \"$e\" | grep -qiE 'not (a )?(valid|found)|unknown connection|no such'; then timeout 45 nmcli dev wifi connect \"$1\"; exit $?; fi; [ $r -ne 0 ] && echo \"$e\" >&2; exit $r", "x", ssid]
        busyFor = ssid; act.running = true
    }
    function refresh() { if (!stateProc.running) stateProc.running = true }
    Timer { id: refreshTimer; interval: 700; onTriggered: wm.refresh() }
    Timer { interval: 5000; running: wm.open; repeat: true; onTriggered: wm.refresh() }

    function connect(n, pw) {
        msg = ""
        if (n.known || !n.secure) nmKnown(n.ssid)
        else nm(["dev", "wifi", "connect", n.ssid, "password", pw], n.ssid)
    }
    function pick(n) {
        if (n.active) return
        if (!n.known && n.secure) { askFor = askFor === n.ssid ? "" : n.ssid; msg = ""; return }
        askFor = ""
        if (!n.known && !n.secure) nm(["dev", "wifi", "connect", n.ssid], n.ssid)
        else connect(n)
    }
    function rescan() { DssWin.run(["nmcli", "dev", "wifi", "rescan"]); refreshTimer.interval = 1800; refreshTimer.restart() }

    // ---------- opening and closing ----------
    function show(x, y) { cy = (y === undefined || isNaN(y)) ? -1 : y;
        cx = x
        msg = ""; askFor = ""
        barProc.running = true
        refresh()
        open = true
    }
    function hide() { open = false; askFor = "" }
    Trigger {
        name: "wifi-open"; fresh: true
        onFired: function(o) {
            const x = parseFloat(o.split(" ")[1]), yv = parseFloat(o.split(" ")[2])
            if (wm.open) wm.hide(); else wm.show(isNaN(x) ? wm.width - 200 : x, yv)
        }
    }

    // ---------- window ----------
    visible: open || (t > 0.01 && !closeT.done)
    // if the closing animation ever stalls (the compositor sent no frames for a moment), it still goes away
    Timer { id: closeT; property bool done: false; interval: 600; running: !open; onRunningChanged: if (running) done = false; onTriggered: done = true }
    // niri: blur behind the card (Hyprland blurs this layer with its own rule)
    NiriBlur { win: wm; items: [card]; radius: card.radius; on: wm.open && wm.et > 0.94 }
    color: "transparent"
    anchors { top: true; left: true; right: true; bottom: true }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "rice-wifi"
    WlrLayershell.keyboardFocus: open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    MouseArea { anchors.fill: parent; onClicked: wm.hide() }

    readonly property int shown: Math.min(Math.max(nets.length, 1), maxRows)
    readonly property real listH: shown * rowH + (askFor !== "" && nets.length <= maxRows ? 52 : 0)
    Rectangle {
        id: card
        width: wm.cardW
        height: head.height + 14 + wm.listH + (wm.msg !== "" ? 34 : 0) + 14
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        x: wm.side ? (wm.atRight ? wm.width - wm.sideSpace - width : wm.sideSpace)
                     : Math.max(12, Math.min(wm.width - width - 12, wm.cx - width / 2))
        y: wm.side ? Math.max(12, Math.min(wm.height - height - 12, (wm.cy < 0 ? wm.height / 2 : wm.cy) - 30))
                     : (wm.atBottom ? wm.height - wm.barSpace - height : wm.barSpace)
        radius: 20
        color: wm.bg
        border.width: 1; border.color: wm.line
        opacity: Math.min(1, wm.et * 1.6)
        scale: 0.9 + 0.1 * wm.et
        transformOrigin: wm.side ? (wm.atRight ? Item.Right : Item.Left) : (wm.atBottom ? Item.Bottom : Item.Top)
        // it comes out of the bar: down from a top bar, up from a bottom one, sideways from a bar on the side
        transform: Translate { x: wm.side ? (1 - wm.et) * (wm.atRight ? 26 : -26) : 0
                               y: wm.side ? 0 : (1 - wm.et) * (wm.atBottom ? 26 : -26) }
        focus: true
        Keys.onEscapePressed: wm.hide()
        MouseArea { anchors.fill: parent }

        // ---------- header: title, rescan, Wi-Fi switch ----------
        Item {
            id: head
            x: 18; y: 14; width: parent.width - 36; height: 36
            MIcon { id: hi; name: wm.radio ? "wifi" : "wifi_off"; glyph: String.fromCodePoint(wm.radio ? 0xF05A9 : 0xF05AA); size: 22; color: wm.fg; anchors.verticalCenter: parent.verticalCenter }
            Text { x: 34; anchors.verticalCenter: parent.verticalCenter; text: Tr.t("Wi-Fi"); color: wm.fg; font.family: Fonts.main; font.pixelSize: 15; font.weight: Font.DemiBold }
            Item {   // rescan
                id: rs
                anchors.right: sw.left; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                width: 30; height: 30; visible: wm.radio
                MIcon { id: ri; anchors.centerIn: parent; name: "refresh"; glyph: String.fromCodePoint(0xF0450); size: 18; color: rsm.containsMouse ? wm.fg : wm.dim
                    RotationAnimation on rotation { id: spin; running: false; from: 0; to: 360; duration: 700; easing.type: Easing.OutCubic } }
                MouseArea { id: rsm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { spin.restart(); wm.rescan() } }
            }
            Rectangle {   // the switch
                id: sw
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                width: 42; height: 24; radius: 12
                color: wm.radio ? Look.accentHi : Qt.rgba(1, 1, 1, 0.14)
                Behavior on color { ColorAnimation { duration: 160 } }
                Rectangle { y: 3; x: wm.radio ? 21 : 3; width: 18; height: 18; radius: 9; color: wm.radio ? "#0d0d0d" : "#F6F5F4"
                    Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutBack } } }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: { Sfx.play(wm.radio ? "switch-off" : "switch-on"); wm.radio = !wm.radio; wm.nm(["radio", "wifi", wm.radio ? "on" : "off"], ""); if (wm.radio) wm.rescan() } }
            }
        }

        // ---------- networks ----------
        Text {
            visible: wm.nets.length === 0
            x: 18; y: head.height + 24; width: parent.width - 36; horizontalAlignment: Text.AlignHCenter
            text: wm.radio ? Tr.t("No networks found") : Tr.t("Wi-Fi is off")
            color: wm.dim; font.family: Fonts.main; font.pixelSize: 13
        }
        Flickable {
            id: fl
            x: 8; y: head.height + 22
            width: parent.width - 16; height: wm.listH
            contentHeight: listCol.height
            clip: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            NumberAnimation { id: flAnim; target: fl; property: "contentY"; duration: 150; easing.type: Easing.OutCubic }
            WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function(e) {
                    if (e.pixelDelta.y !== 0) { flAnim.stop(); fl.contentY = Math.max(0, Math.min(fl.contentHeight - fl.height, fl.contentY - e.pixelDelta.y)); return }
                    const base = flAnim.running ? flAnim.to : fl.contentY
                    const to = Math.max(0, Math.min(fl.contentHeight - fl.height, base - e.angleDelta.y * 1.4))
                    flAnim.stop(); flAnim.from = fl.contentY; flAnim.to = to; flAnim.start()
                } }
            Column {
                id: listCol
                width: fl.width
                Repeater {
                    model: wm.radio ? wm.nets : []
                    delegate: Item {
                        id: row
                        readonly property bool asking: wm.askFor === modelData.ssid
                        readonly property bool busy: wm.busyFor === modelData.ssid
                        width: listCol.width
                        height: asking ? wm.rowH + 52 : wm.rowH
                        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                        opacity: 0
                        property real slide: 10
                        transform: Translate { y: row.slide }
                        Component.onCompleted: enter.start()
                        SequentialAnimation { id: enter
                            PauseAnimation { duration: Math.min(index, 8) * 22 }
                            ParallelAnimation {
                                NumberAnimation { target: row; property: "opacity"; to: 1; duration: 200 }
                                NumberAnimation { target: row; property: "slide"; to: 0; duration: 260; easing.type: Easing.OutCubic }
                            }
                        }
                        Rectangle {
                            x: 0; y: 2; width: parent.width; height: parent.height - 4; radius: 14
                            color: modelData.active ? Qt.rgba(1, 1, 1, 0.1) : (rm.containsMouse ? wm.hot : "transparent")
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                        MouseArea { id: rm; x: 0; y: 0; width: parent.width; height: wm.rowH; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: wm.pick(modelData) }
                        MIcon { x: 14; y: (wm.rowH - height) / 2; size: 22; color: modelData.active ? Look.accentHi : wm.fg
                            name: modelData.signal > 66 ? "wifi" : (modelData.signal > 33 ? "wifi_2_bar" : "wifi_1_bar")
                            glyph: String.fromCodePoint(modelData.signal > 75 ? 0xF0928 : (modelData.signal > 50 ? 0xF0925 : (modelData.signal > 25 ? 0xF0922 : 0xF091F))) }
                        Column {
                            x: 50; y: (wm.rowH - height) / 2; spacing: 2
                            width: parent.width - 50 - 90
                            Text { width: parent.width; elide: Text.ElideRight; text: modelData.ssid; color: wm.fg; font.family: Fonts.main; font.pixelSize: 14; font.weight: modelData.active ? Font.DemiBold : Font.Normal }
                            Text { visible: text !== ""; width: parent.width; elide: Text.ElideRight; color: modelData.active ? Look.accentHi : wm.dim; font.family: Fonts.main; font.pixelSize: 11
                                text: row.busy ? Tr.t("Connecting…") : (modelData.active ? Tr.t("Connected") : (modelData.known ? Tr.t("Saved") : "")) }
                        }
                        Row {
                            anchors.right: parent.right; anchors.rightMargin: 14; y: (wm.rowH - height) / 2; spacing: 10
                            MIcon { visible: modelData.secure; name: "lock"; glyph: String.fromCodePoint(0xF033E); size: 14; color: wm.dim; anchors.verticalCenter: parent.verticalCenter }
                            Item {   // disconnect (the connected one) or forget (a saved one)
                                visible: modelData.active || modelData.known
                                width: 26; height: 26; anchors.verticalCenter: parent.verticalCenter
                                MIcon { anchors.centerIn: parent; name: modelData.active ? "link_off" : "delete"; glyph: String.fromCodePoint(modelData.active ? 0xF0337 : 0xF01B4); size: 16
                                    color: xm.containsMouse ? "#ff6b6b" : wm.dim }
                                MouseArea { id: xm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: wm.nm(modelData.active ? ["connection", "down", "id", modelData.ssid] : ["connection", "delete", "id", modelData.ssid], modelData.ssid) }
                            }
                        }
                        // the password
                        Rectangle {
                            visible: row.asking
                            x: 14; y: wm.rowH - 2; width: parent.width - 28; height: 40; radius: 12
                            color: Qt.rgba(1, 1, 1, 0.07)
                            TextInput {
                                id: pwField
                                x: 14; width: parent.width - 80; anchors.verticalCenter: parent.verticalCenter
                                echoMode: showPw.checked ? TextInput.Normal : TextInput.Password
                                color: wm.fg; font.family: Fonts.main; font.pixelSize: 14; clip: true
                                passwordCharacter: "•"
                                onVisibleChanged: if (visible) { text = ""; forceActiveFocus() }
                                Keys.onReturnPressed: if (text.length >= 8) wm.connect(modelData, text)
                                Keys.onEnterPressed: if (text.length >= 8) wm.connect(modelData, text)
                                Keys.onEscapePressed: wm.askFor = ""
                                Text { visible: pwField.text.length === 0; text: Tr.t("Password"); color: wm.dim; font: pwField.font }
                            }
                            Item { id: showPw; property bool checked: false; anchors.right: parent.right; anchors.rightMargin: 44; anchors.verticalCenter: parent.verticalCenter; width: 24; height: 24
                                MIcon { anchors.centerIn: parent; name: showPw.checked ? "visibility_off" : "visibility"; glyph: String.fromCodePoint(showPw.checked ? 0xF0209 : 0xF0208); size: 16; color: wm.dim }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: showPw.checked = !showPw.checked } }
                            Rectangle { anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; width: 32; height: 28; radius: 9
                                color: pwField.text.length >= 8 ? Look.accentHi : Qt.rgba(1, 1, 1, 0.1)
                                MIcon { anchors.centerIn: parent; name: "arrow_forward"; glyph: String.fromCodePoint(0xF0054); size: 16; color: pwField.text.length >= 8 ? "#0d0d0d" : wm.dim }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: if (pwField.text.length >= 8) wm.connect(modelData, pwField.text) } }
                        }
                    }
                }
            }
        }

        // ---------- an error, if there was one ----------
        Text {
            visible: wm.msg !== ""
            x: 18; y: parent.height - 38; width: parent.width - 36; elide: Text.ElideRight
            text: wm.msg; color: "#ff8a8a"; font.family: Fonts.main; font.pixelSize: 12
        }
    }
}
