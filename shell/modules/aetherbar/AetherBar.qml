// Aether shell (AGPL-3.0-or-later). The Aether bar: a Ryoku-style rail along the top of every screen with
// start / centre / end zones (Settings > Bar > Aether bar picks the widgets). Three shapes:
// "floating" (Ryoku's rail, a gap all round), "full" (edge to edge) and "islands" (DarkShadowShell: a pill per zone).
// Shown while Mix & match > Bar is "aether".
pragma ComponentBehavior: Bound

import QtQuick
import Caelestia.Config
import QtQuick.Effects
import Quickshell
import Quickshell.Wayland
import qs.services

Variants {
    model: Screens.screens

    PanelWindow {
        id: bar

        required property ShellScreen modelData
        readonly property var cfg: AetherBarConfig
        readonly property bool full: cfg.style === "full"
        readonly property bool islands: cfg.style === "islands"
        readonly property real gap: full ? 0 : cfg.gap
        readonly property real h: cfg.barHeight
        // Caelestia draws a rounded frame round the screen (Settings > Look > border): keep the bar inside it so
        // its corners never cover the frame's rounded corners. Full bars round their top corners to match.
        readonly property real frameT: rail.Config.border.thickness
        readonly property real frameR: rail.Config.border.rounding

        // slides down on start, like Ryoku's rails
        property real shown: 0
        Component.onCompleted: shown = 1
        Behavior on shown { NumberAnimation { duration: 620; easing.type: Easing.OutCubic } }

        screen: modelData
        color: "transparent"
        anchors { top: true; left: true; right: true }
        implicitHeight: h + gap + 14   // + room for the shadow
        exclusiveZone: cfg.reserve ? h + gap : 0
        exclusionMode: cfg.reserve ? ExclusionMode.Normal : ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Top
        WlrLayershell.namespace: "aether-bar"

        component Zone: Row {
            id: zone
            required property string name
            spacing: 2
            Repeater {
                model: AetherBarConfig.zone(zone.name)
                BarWidget {
                    required property string modelData
                    wid: modelData
                    screen: bar.modelData
                    barH: bar.h
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                }
            }
        }

        component Island: Rectangle {
            radius: bar.full ? 0 : Math.max(AetherBarConfig.radius, Math.min(bar.h / 2, bar.frameR - bar.gap))
            topLeftRadius: bar.full ? bar.frameR : radius
            topRightRadius: bar.full ? bar.frameR : radius
            color: AetherStyle.surface
            opacity: 1
            border.width: bar.full ? 0 : 1
            border.color: Qt.alpha(AetherStyle.outlineVar, 0.6)
            layer.enabled: !bar.full
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#000000"
                shadowOpacity: 0.35
                shadowBlur: 0.6
                shadowVerticalOffset: 3
            }
        }

        Item {
            id: rail
            x: bar.frameT + bar.gap
            y: bar.gap - (1 - bar.shown) * (bar.h + bar.gap + 10)
            width: parent.width - (bar.frameT + bar.gap) * 2
            height: bar.h
            opacity: bar.shown

            // one rail behind everything (floating / full)
            Island {
                anchors.fill: parent
                visible: !bar.islands
                color: Qt.alpha(AetherStyle.surface, AetherBarConfig.barOpacity)
            }
            // or a pill behind each zone (islands)
            Island {
                visible: bar.islands && startZone.width > 0
                x: startZone.x - 4; width: startZone.width + 8; height: parent.height
                color: Qt.alpha(AetherStyle.surface, AetherBarConfig.barOpacity)
            }
            Island {
                visible: bar.islands && centerZone.width > 0
                x: centerZone.x - 4; width: centerZone.width + 8; height: parent.height
                color: Qt.alpha(AetherStyle.surface, AetherBarConfig.barOpacity)
            }
            Island {
                visible: bar.islands && endZone.width > 0
                x: endZone.x - 4; width: endZone.width + 8; height: parent.height
                color: Qt.alpha(AetherStyle.surface, AetherBarConfig.barOpacity)
            }

            Zone {
                id: startZone
                name: "start"
                x: 4
                anchors.verticalCenter: parent.verticalCenter
            }
            Zone {
                id: centerZone
                name: "center"
                x: Math.max(startZone.x + startZone.width + 8, Math.min((parent.width - width) / 2, endZone.x - width - 8))
                anchors.verticalCenter: parent.verticalCenter
            }
            Zone {
                id: endZone
                name: "end"
                x: parent.width - width - 4
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
