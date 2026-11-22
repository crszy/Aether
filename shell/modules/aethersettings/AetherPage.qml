// Aether shell (AGPL-3.0-or-later). Settings for the Aether shell's own parts: the bar (Ryoku-style rail with
// widgets from Ryoku, end-4, serpantinum and DarkShadowShell), the island dock and the dashboard.
// Everything is saved to ~/.config/caelestia/aether-bar.json through AetherBarConfig and shows live.
pragma ComponentBehavior: Bound
import QtQuick
import qs.services as Cae
import qs.modules.aetherbar as AB
import "../aetherbar/AetherWidgets.js" as W

PageScroll {
    id: page

    property var win: null
    property string section: "bar"     // bar | dock | dash
    readonly property var cfg: AB.AetherBarConfig
    property string target: "end"      // the zone "Add a widget" puts things in

    readonly property bool inUse: section === "dock" ? Cae.WinSettings.mixDock === "aether" : Cae.WinSettings.mixBar === "aether"
    readonly property var zoneNames: [{ value: "start", label: "Left" }, { value: "center", label: "Middle" }, { value: "end", label: "Right" }]

    SectionTitle {
        text: page.section === "bar" ? "Aether bar" : page.section === "dock" ? "Aether dock" : "Aether dashboard"
        sub: page.section === "dash" ? "Opens from the Aether logo, the clock or the song on the Aether bar."
            : page.inUse ? "In use now. Changes show straight away."
            : "Not in use: pick Aether for this part in Mix & match (or the Aether preset in Looks) to see it. Settings are kept either way."
    }
    Btn {
        visible: !page.inUse && page.section !== "dash"
        variant: "filled"
        text: "Use the Aether " + (page.section === "dock" ? "dock" : "bar")
        sym: "check"
        onClicked: Cae.WinSettings[page.section === "dock" ? "mixDock" : "mixBar"] = "aether"
    }

    // ================= bar =================
    Column {
        visible: page.section === "bar"
        width: parent.width
        spacing: 10

        SegRow {
            label: "Shape"
            desc: "Floating rail, edge to edge, or a pill per group"
            options: [{ value: "floating", label: "Floating" }, { value: "full", label: "Full" }, { value: "islands", label: "Islands" }]
            current: page.cfg.style
            onChosen: v => page.cfg.style = v
        }
        SliderRow { label: "Height"; from: 28; to: 56; step: 1; unit: " px"; value: page.cfg.barHeight; onCommitted: v => page.cfg.barHeight = Math.round(v) }
        SliderRow { label: "See-through"; desc: "1 is solid"; from: 0.3; to: 1; step: 0.01; decimals: 2; value: page.cfg.barOpacity; onCommitted: v => page.cfg.barOpacity = v }
        SliderRow { visible: page.cfg.style !== "full"; label: "Space round it"; from: 0; to: 24; step: 1; unit: " px"; value: page.cfg.gap; onCommitted: v => page.cfg.gap = Math.round(v) }
        SliderRow { visible: page.cfg.style !== "full"; label: "Roundness"; from: 0; to: 28; step: 1; unit: " px"; value: page.cfg.radius; onCommitted: v => page.cfg.radius = Math.round(v) }
        ToggleRow { label: "Keep windows below the bar"; desc: "Off lets windows go under it"; checked: page.cfg.reserve; onToggled: v => page.cfg.reserve = v }

        SectionTitle { text: "What's on it"; sub: "Arrows move a widget, × takes it off. Each row is one part of the bar." }
        Repeater {
            model: page.zoneNames
            Rectangle {
                id: zc
                required property var modelData
                readonly property var items: page.cfg.zone(modelData.value)
                width: parent.width
                height: zflow.height + 56
                radius: M3.rM
                color: M3.surfaceC
                T {
                    x: 18
                    y: 14
                    text: zc.modelData.label
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }
                T {
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    y: 16
                    text: zc.items.length === 0 ? "empty" : zc.items.length + (zc.items.length === 1 ? " widget" : " widgets")
                    color: M3.inkDim
                    font.pixelSize: 12
                }
                Flow {
                    id: zflow
                    x: 14
                    y: 42
                    width: parent.width - 28
                    spacing: 6
                    Repeater {
                        model: zc.items
                        Chip {
                            required property string modelData
                            required property int index
                            readonly property var w: W.byId(modelData)
                            label: w ? w.name : modelData
                            canLeft: index > 0
                            canRight: index < zc.items.length - 1
                            onMoveLeft: page.cfg.moveAt(zc.modelData.value, index, -1)
                            onMoveRight: page.cfg.moveAt(zc.modelData.value, index, 1)
                            onRemoved: page.cfg.removeAt(zc.modelData.value, index)
                        }
                    }
                }
            }
        }

        Item {
            width: parent.width
            height: addTitle.height
            SectionTitle {
                id: addTitle
                width: parent.width - addSeg.width - 16
                text: "Add a widget"
                sub: "Pick where it goes, then click one. Grouped by the shell the idea comes from."
            }
            Seg {
                id: addSeg
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                options: page.zoneNames
                current: page.target
                onChosen: v => page.target = v
            }
        }
        Repeater {
            model: W.sources
            Column {
                id: src
                required property string modelData
                width: parent.width
                spacing: 6
                T { text: src.modelData; font.pixelSize: 12; font.weight: Font.DemiBold; color: M3.inkDim }
                Flow {
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: W.list.filter(x => x.from === src.modelData)
                        Rectangle {
                            id: tile
                            required property var modelData
                            readonly property bool on: page.cfg.has(modelData.id) && modelData.id !== "spacer"
                            width: 220
                            height: 58
                            radius: M3.rM
                            color: on ? M3.surfaceC : tm.containsMouse ? M3.surfaceHighest : M3.secondaryContainer
                            opacity: on ? 0.55 : 1
                            Behavior on color { ColorAnimation { duration: 140 } }
                            Icon {
                                x: 14
                                anchors.verticalCenter: parent.verticalCenter
                                name: tile.modelData.icon
                                size: 20
                                color: M3.inkSecondaryC
                            }
                            Column {
                                x: 46
                                width: parent.width - 56
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                T { text: tile.modelData.name + (tile.on ? "  ✓" : ""); font.pixelSize: 13; font.weight: Font.Medium; width: parent.width; elide: Text.ElideRight }
                                T { kind: "reading"; text: tile.modelData.desc; font.pixelSize: 11; color: M3.inkDim; width: parent.width; elide: Text.ElideRight }
                            }
                            MouseArea {
                                id: tm
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: tile.on ? Qt.ArrowCursor : Qt.PointingHandCursor
                                onClicked: if (!tile.on) page.cfg.addWidget(tile.modelData.id, page.target)
                            }
                        }
                    }
                }
            }
        }

        SectionTitle { text: "Workspaces and clock" }
        SliderRow { label: "Workspaces shown"; desc: "Per screen"; from: 2; to: 10; step: 1; value: page.cfg.workspaces; onCommitted: v => page.cfg.workspaces = Math.round(v) }
        ToggleRow { label: "Numbers instead of dots"; checked: page.cfg.wsNumbers; onToggled: v => page.cfg.wsNumbers = v }
        ToggleRow { label: "24-hour clock"; checked: page.cfg.clock24h; onToggled: v => page.cfg.clock24h = v }
        ToggleRow { label: "Date beside the time"; checked: page.cfg.clockDate; onToggled: v => page.cfg.clockDate = v }

        Btn {
            variant: "outlined"
            text: "Put the widgets back as they were"
            sym: "restart_alt"
            onClicked: page.cfg.resetLayout()
        }

        SectionTitle { text: "More bar settings"; sub: "The Aether bar takes ideas from these shells; their own bars have their own pages." }
        Row {
            spacing: 10
            Btn { text: "DarkShadowShell bar"; sym: "dark_mode"; onClicked: page.win && page.win.go("dss", "dss") }
            Btn { text: "serpantinum"; sym: "widgets"; onClicked: page.win && page.win.go("serp", "") }
        }
    }

    // ================= dock =================
    Column {
        visible: page.section === "dock"
        width: parent.width
        spacing: 10

        ToggleRow { label: "Dock on the left edge"; desc: "Touch the left edge of a screen to bring it out"; checked: page.cfg.dock; onToggled: v => page.cfg.dock = v }
        SliderRow { label: "Icon size"; from: 32; to: 64; step: 1; unit: " px"; value: page.cfg.dockIcon; onCommitted: v => page.cfg.dockIcon = Math.round(v) }
        ToggleRow { label: "Icons grow under the mouse"; checked: page.cfg.dockMagnify; onToggled: v => page.cfg.dockMagnify = v }
        ToggleRow { label: "Show open apps too"; desc: "Not just the ones you pinned"; checked: page.cfg.dockRunning; onToggled: v => page.cfg.dockRunning = v }

        SectionTitle { text: "Pinned apps"; sub: "Right-click an app in the dock to pin or unpin it. × here unpins." }
        T {
            visible: page.cfg.dockApps.length === 0
            text: "Nothing pinned yet."
            color: M3.inkDim
            font.pixelSize: 13
        }
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: page.cfg.dockApps
                Chip {
                    required property string modelData
                    required property int index
                    label: modelData
                    canLeft: index > 0
                    canRight: index < page.cfg.dockApps.length - 1
                    onMoveLeft: page.movePin(index, -1)
                    onMoveRight: page.movePin(index, 1)
                    onRemoved: page.cfg.togglePin(modelData)
                }
            }
        }
    }

    // ================= dashboard =================
    Column {
        visible: page.section === "dash"
        width: parent.width
        spacing: 10

        ToggleRow { label: "Lyrics as a chat"; desc: "Each line is a message bubble, like a text conversation"; checked: page.cfg.lyricsBubbles; onToggled: v => page.cfg.lyricsBubbles = v }
        ToggleRow { label: "Lyric effects"; desc: "Zoom, glow and sparkle on the line being sung"; checked: page.cfg.lyricsFx; onToggled: v => page.cfg.lyricsFx = v }
        Btn {
            variant: "filled"
            text: "Open the dashboard"
            sym: "space_dashboard"
            onClicked: page.cfg.toggleDash("home")
        }
    }

    function movePin(i, step) {
        const l = page.cfg.dockApps.slice();
        const j = i + step;
        if (j < 0 || j >= l.length)
            return;
        const t = l[i];
        l[i] = l[j];
        l[j] = t;
        page.cfg.dockApps = l;
    }
}
