// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import QtQuick

// Toolbox > System: the machine at a glance. Processor and graphics (how busy, how hot), memory, swap and disk,
// then the small things: network, how long it has been on, load, fan, battery. The same numbers the widgets use
// (Stats.qml, rice-stats), measured only while this is on the screen.
Item {
    id: sy
    property var pal
    // the opening wave (Tools.qml): 0 .. 1; each part comes a moment after the one above it
    property real enter: 1
    function e(i) { const p = Math.max(0, Math.min(1, (enter - i * 0.07) / 0.6)); return 1 - Math.pow(1 - p, 4) }
    property bool active: false
    StatsHold { on: sy.active }
    readonly property var st: Stats.st
    function n(v) { return v === undefined || v === null || isNaN(v) ? 0 : v }
    // cool: your key color; warm: amber; hot: red
    function heat(t) { return t >= 85 ? "#ff6b6b" : t >= 70 ? "#ffb86b" : pal.primary }
    function speed(b) { return b >= 1048576 ? (b / 1048576).toFixed(1) + " MB/s" : b >= 1024 ? Math.round(b / 1024) + " kB/s" : b + " B/s" }
    function since(s) {
        const d = Math.floor(s / 86400), h = Math.floor(s / 3600) % 24, m = Math.floor(s / 60) % 60
        return d > 0 ? d + " d " + h + " h" : h > 0 ? h + " h " + m + " min" : m + " min"
    }
    readonly property string summary: Tr.t("Processor") + " " + n(st.cpu) + "%" + (n(st.temp) > 0 ? "  ·  " + st.temp + "°C" : "")
                                      + (n(st.up) > 0 ? "  ·  " + Tr.t("on for") + " " + since(st.up) : "")

    // a ring: how busy, and under it how hot
    component Ring: Rectangle {
        id: rg
        property string title: ""
        property string icon: ""
        property real value: 0                 // 0 .. 100
        property bool known: true
        property real temp: 0
        property string note: ""
        readonly property color tint: sy.heat(temp)
        width: (sy.width - 12) / 2; height: 232; radius: 30; color: sy.pal.card
        Row { x: 18; y: 16; spacing: 8
            MIcon { anchors.verticalCenter: parent.verticalCenter; size: 17; name: rg.icon; glyph: ""; color: sy.pal.dim }
            Text { anchors.verticalCenter: parent.verticalCenter; text: rg.title; color: sy.pal.fg; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.DemiBold } }
        Item {
            anchors.horizontalCenter: parent.horizontalCenter; y: 46; width: 120; height: 120
            WavyArc { anchors.fill: parent; value: rg.value / 100; moving: sy.active; thick: 8; waves: 10; color: rg.tint; track: sy.pal.track
                wavy: Look.wavy && rg.value > 4
                Behavior on value { NumberAnimation { duration: 900; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: 500 } } }
            Column { anchors.centerIn: parent
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: rg.known ? Math.round(rg.value) + "%" : "—"; color: sy.pal.fg
                    font.family: Fonts.numbers; font.pixelSize: 26; font.weight: Font.DemiBold }
                Text { anchors.horizontalCenter: parent.horizontalCenter; visible: rg.note !== ""; text: rg.note; color: sy.pal.dim; font.family: Fonts.numbers; font.pixelSize: 10 } }
        }
        // the temperature: a pill that takes the color of the heat
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter; y: 180
            width: trow.implicitWidth + 24; height: 34; radius: 17
            color: rg.temp > 0 ? Qt.rgba(rg.tint.r, rg.tint.g, rg.tint.b, 0.16) : sy.pal.cardHi
            Behavior on color { ColorAnimation { duration: 500 } }
            Row { id: trow; anchors.centerIn: parent; spacing: 6
                MIcon { anchors.verticalCenter: parent.verticalCenter; size: 16; name: "device_thermostat"; glyph: String.fromCodePoint(0xF050F)
                    color: rg.temp > 0 ? rg.tint : sy.pal.dim }
                Text { anchors.verticalCenter: parent.verticalCenter; text: rg.temp > 0 ? Math.round(rg.temp) + "°C" : Tr.t("no sensor")
                    color: rg.temp > 0 ? rg.tint : sy.pal.dim; font.family: Fonts.numbers; font.pixelSize: 13; font.weight: Font.DemiBold } }
        }
    }
    component Meter: Rectangle {
        id: br
        property string title: ""
        property string icon: ""
        property real value: 0
        property string tail: ""
        width: sy.width; height: 80; radius: 26; color: sy.pal.card
        Row { x: 18; y: 15; spacing: 8
            MIcon { anchors.verticalCenter: parent.verticalCenter; size: 17; name: br.icon; glyph: ""; color: sy.pal.dim }
            Text { anchors.verticalCenter: parent.verticalCenter; text: br.title; color: sy.pal.fg; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.DemiBold }
            Text { anchors.verticalCenter: parent.verticalCenter; text: Math.round(br.value) + "%"; color: sy.pal.primary; font.family: Fonts.numbers; font.pixelSize: 12; font.weight: Font.DemiBold } }
        Text { anchors.right: parent.right; anchors.rightMargin: 18; y: 16; text: br.tail; color: sy.pal.dim; font.family: Fonts.numbers; font.pixelSize: 12 }
        WavyLine { x: 18; y: 46; width: parent.width - 36; handle: false; value: br.value / 100; moving: sy.active
            color: br.value >= 90 ? "#ff6b6b" : sy.pal.primary; track: sy.pal.track
            Behavior on value { NumberAnimation { duration: 900; easing.type: Easing.OutCubic } } }
    }
    // a small fact: an icon in a soft shape, a name, a value
    component Tile: Rectangle {
        id: tl
        property string title: ""
        property string icon: ""
        property string value: ""
        property string sub: ""
        width: (sy.width - 12) / 2; height: 76; radius: 24; color: sy.pal.card
        Rectangle { id: tbadge; x: 14; anchors.verticalCenter: parent.verticalCenter; width: 40; height: 40; radius: 14; color: sy.pal.container
            MIcon { anchors.centerIn: parent; size: 20; name: tl.icon; glyph: ""; color: sy.pal.inkContainer } }
        Column { anchors.left: tbadge.right; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 80
            Text { text: tl.title; color: sy.pal.dim; font.family: Fonts.main; font.pixelSize: 11 }
            Text { width: parent.width; elide: Text.ElideRight; text: tl.value; color: sy.pal.fg; font.family: Fonts.numbers; font.pixelSize: 15; font.weight: Font.DemiBold }
            Text { visible: tl.sub !== ""; width: parent.width; elide: Text.ElideRight; text: tl.sub; color: sy.pal.dim; font.family: Fonts.numbers; font.pixelSize: 11 } }
    }

    Flickable {
        anchors.fill: parent; contentHeight: col.height; clip: true; boundsBehavior: Flickable.StopAtBounds
        Column {
            id: col
            width: parent.width; spacing: 12
            Row {
                spacing: 12
                opacity: sy.e(0); transform: Translate { y: (1 - sy.e(0)) * 22 }
                Ring { title: Tr.t("Processor"); icon: "memory"; value: sy.n(sy.st.cpu); temp: sy.n(sy.st.temp)
                    note: sy.n(sy.st.freq) > 0 ? sy.st.freq + " GHz" : "" }
                Ring { title: Tr.t("Graphics"); icon: "developer_board"; value: sy.n(sy.st.gpu); known: Stats.haveGpu; temp: sy.n(sy.st.gtemp) }
            }
            Meter { title: Tr.t("Memory"); icon: "memory_alt"; value: sy.n(sy.st.ram); tail: sy.n(sy.st.ramu) + " / " + sy.n(sy.st.ramt) + " GB"
                opacity: sy.e(1); transform: Translate { y: (1 - sy.e(1)) * 22 } }
            Meter { visible: sy.n(sy.st.swapt) > 0; title: Tr.t("Swap"); icon: "swap_horiz"; value: sy.n(sy.st.swap); tail: sy.n(sy.st.swapu) + " / " + sy.n(sy.st.swapt) + " GB"
                opacity: sy.e(2); transform: Translate { y: (1 - sy.e(2)) * 22 } }
            Meter { title: Tr.t("Disk"); icon: "hard_drive"; value: sy.n(sy.st.disk); tail: sy.n(sy.st.disku) + " / " + sy.n(sy.st.diskt) + " GB"
                opacity: sy.e(3); transform: Translate { y: (1 - sy.e(3)) * 22 } }
            Flow {
                width: parent.width; spacing: 12
                opacity: sy.e(4); transform: Translate { y: (1 - sy.e(4)) * 22 }
                Tile { title: Tr.t("Download"); icon: "download"; value: sy.speed(sy.n(sy.st.rx)) }
                Tile { title: Tr.t("Upload"); icon: "upload"; value: sy.speed(sy.n(sy.st.tx)) }
                Tile { title: Tr.t("On for"); icon: "schedule"; value: sy.since(sy.n(sy.st.up)) }
                Tile { title: Tr.t("Load"); icon: "speed"; value: sy.n(sy.st.load).toFixed(2); sub: sy.n(sy.st.cores) > 0 ? sy.st.cores + " " + Tr.t("threads") : "" }
                Tile { visible: sy.n(sy.st.fan) > 0; title: Tr.t("Fan"); icon: "mode_fan"; value: sy.n(sy.st.fan) + " rpm" }
                Tile { visible: sy.st.bat !== undefined && sy.st.bat >= 0; title: Tr.t("Battery"); icon: sy.st.charging ? "battery_charging_full" : "battery_full"
                    value: sy.n(sy.st.bat) + "%"; sub: sy.st.charging ? Tr.t("Charging") : "" }
            }
            ToolBtn { pal: sy.pal; label: Tr.t("Open the system monitor"); icon: "monitoring"
                onClicked: DssWin.run(["bash", "-c", "for a in missioncenter resources gnome-system-monitor plasma-systemmonitor; do command -v $a >/dev/null && exec $a; done; exec \"$HOME/.local/bin/rice-term\" -e btop 2>/dev/null || exec \"$HOME/.local/bin/rice-term\" -e htop"]) }
            Item { width: 1; height: 4 }
        }
    }
}
