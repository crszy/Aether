// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Wayland
import QtQuick

// Audio bars that grow away from the Waybar edge (down from a top bar, up from a
// bottom bar). Bottom layer, never takes input.
PanelWindow {
    screen: Mon.screen
    id: vz
    property bool active: false
    property string barPos: "top"
    property int barH: 44
    property var bars: []
    readonly property int count: 64
    readonly property int reach: 100
    readonly property bool atBottom: barPos === "bottom"

    visible: active
    color: "transparent"
    anchors { left: true; right: true; top: !atBottom; bottom: atBottom }
    implicitHeight: barH + reach
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Bottom
    WlrLayershell.namespace: "rice-viz"
    mask: Region { }

    CavaUser { active: vz.active }
    Connections {
        target: CavaFeed
        enabled: vz.active
        function onFrame() {
            const p = CavaFeed.bands
            if (p.length === 0) return
            // nothing changes while it is silent: skip the update
            let loud = 0
            for (let i = 0; i < p.length; i++) if (p[i] > loud) loud = p[i]
            if (loud < 0.5 && vz.quiet) return
            vz.quiet = loud < 0.5
            vz.bars = p
        }
    }

    property bool quiet: false
    Row {
        anchors.fill: parent
        spacing: 3
        Repeater {
            model: vz.count
            delegate: Item {
                width: (vz.width - 3 * (vz.count - 1)) / vz.count
                height: vz.height
                readonly property real v: vz.bars[index] !== undefined ? vz.bars[index] : 0
                Rectangle {
                    width: parent.width
                    height: Math.max(3, vz.reach * parent.v / 100)
                    radius: Math.min(width / 2, 3)
                    color: "#F6F5F4"
                    opacity: 0.3 + 0.5 * parent.v / 100
                    // top bar: waves fall down from its lower edge; bottom bar: they rise from its upper edge
                    y: vz.atBottom ? vz.height - vz.barH - height : vz.barH
                    Behavior on height { NumberAnimation { duration: 70 } }
                }
            }
        }
    }
}
