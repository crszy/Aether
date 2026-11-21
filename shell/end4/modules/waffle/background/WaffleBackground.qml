pragma ComponentBehavior: Bound

import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.widgets
import qs.end4.modules.common.widgets.widgetCanvas
import QtQuick
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Hyprland

import qs.end4.modules.ii.background.widgets
import qs.end4.modules.ii.background.widgets.clock
import qs.end4.modules.ii.background.widgets.weather

Variants {
    id: root
    model: Quickshell.screens

    PanelWindow {
        id: panelRoot
        required property var modelData

        screen: modelData
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Bottom
        WlrLayershell.namespace: "quickshell:background"
        anchors {
            top: true
            bottom: true
            left: true
            right: true
        }
        color: "transparent"

        StyledImage {
            anchors.fill: parent
            source: Config.options.background.wallpaperPath
            fillMode: Image.PreserveAspectCrop
        }
    }
}
