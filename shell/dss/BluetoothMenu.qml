// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick

// The Bluetooth menu: click the Bluetooth icon on the bar and it drops down under it. Your devices (battery too), connect,
// disconnect, pair a new one, forget one, Bluetooth on / off. It talks to BlueZ through bluetoothctl.
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
    property string busyFor: ""
    property string msg: ""
    property bool scanning: false
    readonly property int cardW: 360
    readonly property int rowH: 54
    readonly property int maxRows: 7

    function parse(text) {
        const lines = text.split("\n")
        const list = []
        for (let i = 0; i < lines.length; i++) {
            const l = lines[i]
            if (l.indexOf("power:") === 0) { radio = l.substring(6).trim() === "yes"; continue }
            const f = l.split("|")
            if (f.length < 6 || f[0] === "") continue
            const paired = f[3].trim() === "yes", conn = f[2].trim() === "yes"
            if (!paired && !conn && f[1].replace(/-/g, ":") === f[0]) continue      // an unnamed device nearby
            list.push({ mac: f[0], name: f[1], active: conn, paired: paired, icon: f[4].trim(), batt: parseInt(f[5]) })
        }
        list.sort(function(a, b) { return a.active !== b.active ? (a.active ? -1 : 1) : (a.paired !== b.paired ? (a.paired ? -1 : 1) : a.name.localeCompare(b.name)) })
        nets = list
    }

    DssProc {
        id: stateProc
        command: ["bash", "-c", "\"$HOME/.local/bin/rice-bluetooth\" list"]
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
            if (code === 0) { wm.msg = ""; Sfx.play("done") }
            else {
                // rice-bluetooth connect: 3 = no Bluetooth audio in PipeWire, 4 = WirePlumber is not running
                wm.msg = code === 3 ? Tr.t("Headphones need PipeWire's Bluetooth audio: install pipewire-audio (Arch), then log in again")
                       : (code === 4 ? Tr.t("Headphones need WirePlumber running: systemctl --user enable --now wireplumber")
                       : Tr.t("Could not connect. Is the device on and close by?"))
                Sfx.play("remove")
            }
            refreshTimer.restart()
        }
    }
    DssProc { id: scanProc; command: ["bluetoothctl", "--timeout", "12", "scan", "on"]; onExited: wm.scanning = false }
    function sh(cmd, label) { act.command = ["bash", "-c", cmd]; busyFor = label || ""; act.running = true }
    function refresh() { if (!stateProc.running) stateProc.running = true }
    Timer { id: refreshTimer; interval: 700; onTriggered: wm.refresh() }
    Timer { interval: 4000; running: wm.open; repeat: true; onTriggered: wm.refresh() }

    function pick(n) {
        msg = ""
        if (n.active) return
        // rice-bluetooth connect: stops searching first, trusts the device, checks it stays connected (tries again)
        if (scanProc.running) scanProc.running = false
        sh("\"$HOME/.local/bin/rice-bluetooth\" connect " + n.mac, n.mac)
    }
    function scan() { if (scanning) return; scanning = true; scanProc.running = true; refreshTimer.interval = 2500; refreshTimer.restart() }
    function power(on) { radio = on; sh((on ? "rfkill unblock bluetooth; " : "") + "bluetoothctl power " + (on ? "on" : "off"), ""); if (on) scan() }
    function iconOf(n) {
        const i = n.icon
        if (i.indexOf("headphone") >= 0 || i.indexOf("headset") >= 0) return ["headphones", 0xF02CB]
        if (i.indexOf("audio") >= 0) return ["speaker", 0xF04C3]
        if (i.indexOf("keyboard") >= 0) return ["keyboard", 0xF030C]
        if (i.indexOf("mouse") >= 0) return ["mouse", 0xF037D]
        if (i.indexOf("gaming") >= 0) return ["sports_esports", 0xF0297]
        if (i.indexOf("phone") >= 0) return ["smartphone", 0xF011C]
        if (i.indexOf("computer") >= 0) return ["computer", 0xF0379]
        return ["bluetooth", 0xF00AF]
    }

    // ---------- opening and closing ----------
    function show(x, y) { cy = (y === undefined || isNaN(y)) ? -1 : y;
        cx = x
        msg = ""
        barProc.running = true
        refresh()
        open = true
    }
    function hide() { open = false }
    Trigger {
        name: "bluetooth-open"; fresh: true
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
    WlrLayershell.namespace: "rice-bluetooth"
    WlrLayershell.keyboardFocus: open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    MouseArea { anchors.fill: parent; onClicked: wm.hide() }

    readonly property int shown: Math.min(Math.max(nets.length, 1), maxRows)
    readonly property real listH: shown * rowH + (nets.length === 0 && radio && scanning ? 52 : 0)
    Rectangle {
        id: card
        width: wm.cardW
        height: head.height + 14 + wm.listH + (wm.msg !== "" ? (wm.msg.length > 52 ? 48 : 34) : 0) + 14
        Behavior on height { NumberAnimation { duration: 360; easing.type: Easing.BezierSpline; easing.bezierCurve: Look.emph } }
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
            MIcon { id: hi; name: wm.radio ? "bluetooth" : "bluetooth_disabled"; glyph: String.fromCodePoint(wm.radio ? 0xF00AF : 0xF00B2); size: 22; color: wm.fg; anchors.verticalCenter: parent.verticalCenter }
            Text { x: 34; anchors.verticalCenter: parent.verticalCenter; text: Tr.t("Bluetooth"); color: wm.fg; font.family: Fonts.main; font.pixelSize: 15; font.weight: Font.DemiBold }
            Item {   // rescan
                id: rs
                anchors.right: sw.left; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                width: 30; height: 30; visible: wm.radio
                MIcon { id: ri; anchors.centerIn: parent; name: "refresh"; glyph: String.fromCodePoint(0xF0450); size: 18; color: rsm.containsMouse ? wm.fg : wm.dim
                    RotationAnimation on rotation { id: spin; running: false; from: 0; to: 360; duration: 700; easing.type: Easing.OutCubic } }
                MouseArea { id: rsm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { spin.restart(); wm.scan() } }
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
                    onClicked: { Sfx.play(wm.radio ? "switch-off" : "switch-on"); wm.power(!wm.radio) } }
            }
        }

        // ---------- networks ----------
        Text {
            visible: wm.nets.length === 0
            x: 18; y: head.height + 24; width: parent.width - 36; horizontalAlignment: Text.AlignHCenter
            text: wm.radio ? (wm.scanning ? Tr.t("Looking for devices…") : Tr.t("No devices. Press refresh to search")) : Tr.t("Bluetooth is off")
            color: wm.dim; font.family: Fonts.main; font.pixelSize: 13
        }
        // while it looks: your shape (Settings > Appearance > Shapes) turns and breathes
        Blob {
            visible: wm.nets.length === 0 && wm.radio && wm.scanning
            anchors.horizontalCenter: parent.horizontalCenter
            y: head.height + 54
            width: 34; height: 34
            kind: Look.shape
            color: Qt.rgba(1, 1, 1, 0.75)
            RotationAnimator on rotation { from: 0; to: 360; duration: 2200; loops: Animation.Infinite; running: wm.open && wm.scanning }
            SequentialAnimation on scale { running: wm.open && wm.scanning; loops: Animation.Infinite
                NumberAnimation { to: 0.8; duration: 700; easing.type: Easing.InOutSine } NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutSine } }
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
                        readonly property bool busy: wm.busyFor === modelData.mac
                        width: listCol.width
                        height: wm.rowH
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
                            name: wm.iconOf(modelData)[0]; glyph: String.fromCodePoint(wm.iconOf(modelData)[1]) }
                        Column {
                            x: 50; y: (wm.rowH - height) / 2; spacing: 2
                            width: parent.width - 50 - 60
                            Text { width: parent.width; elide: Text.ElideRight; text: modelData.name; color: wm.fg; font.family: Fonts.main; font.pixelSize: 14; font.weight: modelData.active ? Font.DemiBold : Font.Normal }
                            Text { visible: text !== ""; width: parent.width; elide: Text.ElideRight; color: modelData.active ? Look.accentHi : wm.dim; font.family: Fonts.main; font.pixelSize: 11
                                text: row.busy ? Tr.t("Connecting…") : (modelData.active ? Tr.t("Connected") + (isNaN(modelData.batt) ? "" : Tr.t("  \u00b7  ") + modelData.batt + "%") : (modelData.paired ? Tr.t("Paired") : Tr.t("Tap to pair"))) }
                        }
                        Row {
                            anchors.right: parent.right; anchors.rightMargin: 14; y: (wm.rowH - height) / 2; spacing: 10
                            Item {   // disconnect (the connected one) or forget (a paired one)
                                visible: modelData.paired || modelData.active
                                width: 26; height: 26; anchors.verticalCenter: parent.verticalCenter
                                MIcon { anchors.centerIn: parent; name: modelData.active ? "link_off" : "delete"; glyph: String.fromCodePoint(modelData.active ? 0xF0337 : 0xF01B4); size: 16
                                    color: xm.containsMouse ? "#ff6b6b" : wm.dim }
                                MouseArea { id: xm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: wm.sh(modelData.active ? "bluetoothctl disconnect " + modelData.mac : "bluetoothctl remove " + modelData.mac, modelData.mac) }
                            }
                        }
                    }
                }
            }
        }

        // ---------- an error, if there was one ----------
        Text {
            visible: wm.msg !== ""
            x: 18; y: parent.height - 14 - height; width: parent.width - 36
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            text: wm.msg; color: "#ff8a8a"; font.family: Fonts.main; font.pixelSize: 12
        }
    }
}
