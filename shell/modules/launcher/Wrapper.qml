pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Caelestia.Config
import qs.components
import qs.components.effects
import qs.services
import qs.modules.launcher.services

Item {
    id: root

    required property ShellScreen screen
    required property ScreenState screenState
    required property var panels

    readonly property bool shouldBeActive: screenState.launcher && Config.launcher.enabled && WinSettings.mixLauncher === "caelestia"

    // Mix & match: the launcher key opens Noctalia's launcher instead
    Connections {
        target: root.screenState
        function onLauncherChanged(): void {
            if (root.screenState.launcher && WinSettings.mixLauncher !== "caelestia") {
                root.screenState.launcher = false;
                WinSettings.callPart("launcher", WinSettings.mixLauncher);
            }
        }
    }

    // Aether shell (Mix & match bar = "aether"): the launcher floats in the middle of the screen instead of
    // rising from the bottom edge. Its search bar stays put at 30% from the bottom, results grow upwards.
    readonly property bool centred: WinSettings.mixBar === "aether"

    readonly property real maxHeight: {
        if (centred)
            return screen.height * 0.7 - Config.border.thickness * 2;
        let max = screen.height - Config.border.thickness * 2 + Tokens.padding.extraLarge;
        if (screenState.dashboard)
            max -= panels.dashboard.nonAnimHeight;
        return max;
    }

    property real offsetScale: shouldBeActive ? 0 : 1
    // centred (Aether): the panel grows out of one point and shrinks back into it, with the content zooming in
    // from the depth with it. The panel background follows this size, so at 0 nothing is left on screen.
    readonly property real morph: centred ? 1 - offsetScale : 1
    property real fullH: content.implicitHeight

    onShouldBeActiveChanged: {
        if (shouldBeActive)
            fullH = Qt.binding(() => content.implicitHeight);
        else
            fullH = fullH; // Break binding during close anim
    }

    visible: offsetScale < 1
    clip: centred
    anchors.bottomMargin: centred ? (parent ? parent.height * 0.3 : 0) + fullH * (1 - morph) / 2 : (-implicitHeight - 5) * offsetScale
    implicitHeight: fullH * morph
    implicitWidth: (content.implicitWidth || 630) * (centred ? 0.25 + 0.75 * morph : 1) // Hard coded fallback for first open
    opacity: centred ? Math.min(1, morph * 1.6) : 1 - offsetScale

    Component.onCompleted: Qt.callLater(() => Apps) // Load apps on init

    Behavior on offsetScale {
        Anim {
            type: root.centred ? Anim.SlowSpatial : Anim.DefaultSpatial // spatial curves overshoot a little: the bounce
        }
    }

    // Aether liquid glass (windows.json "glass": "liquid"): the launcher's own glass instead of the opaque blob
    // (ContentWindow.qml shrinks launcherBg to nothing in this case)
    Loader {
        anchors.fill: parent
        active: root.centred && WinSettings.glass === "liquid" && !WinSettings.distroMode && root.visible
        sourceComponent: LiquidGlass {
            radius: Tokens.rounding.extraLarge
            bezel: 14
            refraction: 10
            source: Wallpapers.current ? "file:///" + Wallpapers.current.split("\\").join("/") : ""
            screenSize: Qt.size(root.screen.width, root.screen.height)
            tint: Qt.alpha(Colours.palette.m3surface, 0.5)
        }
    }

    Loader {
        id: content

        anchors.top: root.centred ? undefined : parent.top
        anchors.horizontalCenter: root.centred ? undefined : parent.horizontalCenter
        anchors.centerIn: root.centred ? parent : undefined
        scale: root.centred ? 0.82 + 0.18 * root.morph : 1

        active: root.shouldBeActive || root.visible

        sourceComponent: Content {
            screenState: root.screenState
            panels: root.panels
            maxHeight: root.maxHeight
        }
    }
}
