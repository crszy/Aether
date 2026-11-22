// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Widgets
import QtQuick

// The menu of an app in the tray (right-click its icon), drawn in the shell's Material 3 look: the app's own
// entries (with their submenus, checks and separators) and, at the bottom, Close app, which ends the app that
// runs in the background (rice-tray quit). It grows out of the bar under the icon and closes with Esc, a click
// outside or a click on an entry.
Item {
    id: tmu
    property var pal: null
    property var item: null                    // the SystemTrayItem
    property real ax: 0                        // where the icon is (centre, in the bar)
    property bool down: true                   // opens below the bar
    property real areaW: 1920
    property bool open: false
    property real maxH: 420                    // the most it can be tall: a longer menu scrolls
    function show(it, x) { item = it; ax = x; stack = []; open = true }
    function close() { open = false }

    // the menu it shows: the app's menu, or the submenu you went into
    property var stack: []
    readonly property var cur: stack.length > 0 ? stack[stack.length - 1] : (item ? item.menu : null)
    QsMenuOpener { id: op; menu: tmu.cur }
    readonly property var entries: op.children ? op.children.values : []

    property real t: open ? 1 : 0
    Behavior on t { NumberAnimation { duration: tmu.open ? 420 : 180; easing.type: Easing.BezierSpline; easing.bezierCurve: tmu.open ? Look.decel : Look.accel } }
    visible: t > 0.01
    width: 264
    height: Math.min(col.implicitHeight + 16, maxH)
    x: Math.max(8, Math.min(areaW - width - 8, ax - width / 2))
    opacity: Math.min(1, t * 1.6)
    transform: [
        Scale { origin.x: tmu.ax - tmu.x; origin.y: tmu.down ? 0 : tmu.height; xScale: 0.86 + 0.14 * tmu.t; yScale: 0.6 + 0.4 * tmu.t },
        Translate { y: (1 - tmu.t) * (tmu.down ? -10 : 10) }
    ]

    Shadow { anchors.fill: bg; radius: bg.radius; on: Look.shadows && Look.shadowPanels; size: Look.shadowSize * 0.8; strength: Look.shadowStrength }
    Rectangle {
        id: bg
        anchors.fill: parent
        radius: 24
        color: tmu.pal ? tmu.pal.surface : "#141414"
    }
    Column {
        id: col
        x: 8; y: 8; width: parent.width - 16
        spacing: 2
        // the app: its icon in your shape and its name; in a submenu, the way back
        Item {
            width: parent.width; height: 52
            Item { id: hIc; x: 6; anchors.verticalCenter: parent.verticalCenter; width: 38; height: 38
                Blob { anchors.fill: parent; kind: Look.shape; color: tmu.pal ? tmu.pal.container : "#333" }
                IconImage { anchors.centerIn: parent; implicitSize: 20; source: tmu.item ? Icons.src(tmu.item.icon) : ""; visible: tmu.stack.length === 0 }
                MIcon { anchors.centerIn: parent; size: 18; name: "arrow_back"; glyph: ""; visible: tmu.stack.length > 0; color: tmu.pal ? tmu.pal.inkContainer : "white" }
                MouseArea { anchors.fill: parent; enabled: tmu.stack.length > 0; cursorShape: Qt.PointingHandCursor
                    onClicked: tmu.stack = tmu.stack.slice(0, tmu.stack.length - 1) } }
            Column {
                anchors.left: hIc.right; anchors.leftMargin: 12; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                Text { width: parent.width; elide: Text.ElideRight; color: tmu.pal ? tmu.pal.fg : "white"; font.family: Fonts.main; font.pixelSize: 14; font.weight: Font.DemiBold
                    text: tmu.item ? (tmu.item.tooltipTitle || tmu.item.title || tmu.item.id) : "" }
                Text { width: parent.width; elide: Text.ElideRight; color: tmu.pal ? tmu.pal.dim : "#aaa"; font.family: Fonts.main; font.pixelSize: 11
                    text: tmu.stack.length > 0 ? Tr.t("Back") : Tr.t("Running in the background") }
            }
        }
        // the entries scroll when the menu is taller than the room it has (header 52, line, Close app 44)
        Flickable {
            id: fl
            width: parent.width
            height: Math.min(ecol.implicitHeight, Math.max(120, tmu.maxH - 16 - 52 - 44 - 9 - 3 * col.spacing))
            contentWidth: width
            contentHeight: ecol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            interactive: contentHeight > height
            Column {
            id: ecol
            width: fl.width
            spacing: 2
        Repeater {
            model: tmu.entries
            delegate: Item {
                id: en
                readonly property var e: modelData
                width: col.width
                height: e.isSeparator ? 9 : 40
                // it comes in a moment after the one before it
                readonly property real it: Math.max(0, Math.min(1, (tmu.t - Math.min(index, 10) * 0.04) / 0.6))
                opacity: it
                Rectangle { visible: en.e.isSeparator; anchors.centerIn: parent; width: parent.width - 24; height: 1; color: tmu.pal ? tmu.pal.line : "#333" }
                Rectangle {
                    visible: !en.e.isSeparator
                    anchors.fill: parent; radius: 14
                    color: tmu.pal ? tmu.pal.fg : "white"
                    opacity: em.containsMouse && en.e.enabled ? 0.08 : 0
                    Behavior on opacity { NumberAnimation { duration: 140 } }
                }
                Row {
                    visible: !en.e.isSeparator
                    x: 14; anchors.verticalCenter: parent.verticalCenter; spacing: 10
                    width: parent.width - 28
                    // a check or a dot for entries that can be on
                    MIcon { visible: en.e.buttonType !== 0; anchors.verticalCenter: parent.verticalCenter; size: 17; glyph: ""; filled: true
                        name: en.e.buttonType === 2 ? (en.e.checkState === Qt.Checked ? "radio_button_checked" : "radio_button_unchecked")
                                                     : (en.e.checkState === Qt.Checked ? "check_box" : "check_box_outline_blank")
                        color: tmu.pal ? tmu.pal.primary : "white" }
                    IconImage { visible: en.e.buttonType === 0 && en.e.icon !== ""; anchors.verticalCenter: parent.verticalCenter; implicitSize: 16; source: Icons.src(en.e.icon) }
                    Text { anchors.verticalCenter: parent.verticalCenter; width: parent.width - 60; elide: Text.ElideRight
                        text: String(en.e.text || "").replace(/_(?=\S)/, ""); color: tmu.pal ? tmu.pal.fg : "white"; opacity: en.e.enabled ? 1 : 0.4
                        font.family: Fonts.main; font.pixelSize: 13 }
                }
                MIcon { visible: en.e.hasChildren; anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                    size: 18; name: "chevron_right"; glyph: ""; color: tmu.pal ? tmu.pal.dim : "#aaa" }
                MouseArea {
                    id: em; anchors.fill: parent; hoverEnabled: true; enabled: !en.e.isSeparator && en.e.enabled
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (en.e.hasChildren) { tmu.stack = tmu.stack.concat([en.e]); return }
                        en.e.triggered()
                        tmu.close()
                    }
                }
            }
        }
            }
        }
        Rectangle { width: parent.width - 24; x: 12; height: 1; color: tmu.pal ? tmu.pal.line : "#333"; visible: tmu.entries.length > 0 }
        // end the app
        Item {
            width: parent.width; height: 44
            Rectangle { anchors.fill: parent; radius: 14; color: "#ff8a80"; opacity: qm.containsMouse ? 0.16 : 0
                Behavior on opacity { NumberAnimation { duration: 140 } } }
            Row { x: 14; anchors.verticalCenter: parent.verticalCenter; spacing: 10
                MIcon { anchors.verticalCenter: parent.verticalCenter; size: 18; name: "close"; glyph: ""; color: "#ffb4ab" }
                Text { anchors.verticalCenter: parent.verticalCenter; text: Tr.t("Close app"); color: "#ffb4ab"; font.family: Fonts.main; font.pixelSize: 13; font.weight: Font.Medium } }
            MouseArea {
                id: qm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (tmu.item) DssWin.run(["bash", "-c", "\"$HOME/.local/bin/rice-tray\" quit \"$1\"", "_", tmu.item.id])
                    tmu.close()
                }
            }
        }
    }
}
