// One lock surface per screen: the screen's own contents blurred behind a
// rounded card that spins in from a lock glyph and holds the clock, profile
// picture, password field and a few live panels. Layout and motion follow
// caelestia-dots/shell's lock; the widgets are Zenith's own services.
import QtQuick
import qs.services as Cae
import QtQuick.Layouts
import QtQuick.Effects
import Quickshell
import Quickshell.Wayland
import "../.."
import "../../services"
import "../../Settings"

WlSessionLockSurface {
    id: root

    readonly property bool unlocking: unlockAnim.running
    readonly property real screenH: screen ? screen.height : 1080
    readonly property real screenW: screen ? screen.width : 1920

    // Card geometry: a wide landscape card that scales with the monitor.
    readonly property real cardH: Math.round(screenH * 0.64)
    readonly property real cardW: Math.round(Math.min(screenW * 0.9, cardH * 1.85))
    readonly property real centerScale: Math.min(1, screenH / 1440)
    readonly property int centerWidth: Math.round(420 * centerScale)
    readonly property int padding: Theme.scaled(28)

    color: "transparent"

    // Keyboard goes to the surface as a whole, not to a deeply nested item:
    // a FocusScope at the root owns focus from the first frame and hands every
    // key to LockService, so there is nothing to click and nothing to lose.
    FocusScope {
        id: keys
        anchors.fill: parent
        focus: true
        Component.onCompleted: forceActiveFocus()
        Keys.onPressed: (event) => {
            if (root.unlocking) return;
            LockService.handleKey(event);
            event.accepted = true;
        }
        MouseArea {
            anchors.fill: parent
            onClicked: keys.forceActiveFocus()
        }
    }

    Connections {
        target: LockService
        function onUnlockRequested() { unlockAnim.start(); }
    }

    // ---- Background: the screen itself, blurred, dimmed ------------------
    Item {
        id: background
        anchors.fill: parent
        opacity: 0

        // The compositor hands the keyboard to the lock only after its first
        // frame is committed, and keys typed before that are dropped. A
        // full-resolution 64px blur made that first frame slow enough to eat
        // the start of a quickly typed password. Blurring at quarter size is
        // visually identical for a heavy blur and many times cheaper.
        layer.enabled: IdleSettings.blurBackground
        layer.textureSize: Qt.size(Math.max(1, Math.round(width / 4)), Math.max(1, Math.round(height / 4)))
        layer.smooth: true
        layer.effect: MultiEffect {
            autoPaddingEnabled: false
            blurEnabled: true
            blur: 1
            blurMax: 32
            blurMultiplier: 1
        }

        ScreencopyView {
            anchors.fill: parent
            captureSource: root.screen
            live: false
        }

        Image { // Windows: Mix & match (WinSettings.lockWallpaper)
            anchors.fill: parent
            visible: Cae.WinSettings.lockWallpaper === "caelestia" && !!Cae.Wallpapers.current
            source: visible ? "file:///" + String(Cae.Wallpapers.current).split("\\").join("/") : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }

        Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.35) }
    }

    // ---- Card ------------------------------------------------------------
    Item {
        id: card

        readonly property int iconSize: Theme.scaled(96)
        readonly property int collapsedSize: iconSize + padding * 2
        readonly property int collapsedRadius: collapsedSize / 4

        anchors.centerIn: parent
        implicitWidth: collapsedSize
        implicitHeight: collapsedSize
        rotation: 180
        scale: 0

        Rectangle {
            id: cardBg
            anchors.fill: parent
            color: Qt.alpha(Colors.surface, 0.94)
            radius: card.collapsedRadius
            border.color: Theme.glassBorder
            border.width: 1

            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                blurMax: 24
                shadowColor: Qt.rgba(0, 0, 0, 0.6)
                shadowVerticalOffset: 6
            }
        }

        Text {
            id: lockIcon
            anchors.centerIn: parent
            text: "󰌾"
            font.family: Theme.iconFont
            font.pixelSize: card.iconSize
            color: Colors.primary
            rotation: 180
        }

        LockContent {
            id: content
            anchors.centerIn: parent
            width: root.cardW - root.padding * 2
            height: root.cardH - root.padding * 2
            surface: root
            opacity: 0
            scale: 0
        }
    }

    // ---- Motion -----------------------------------------------------------
    ParallelAnimation {
        id: initAnim
        running: true

        NumberAnimation { target: background; property: "opacity"; to: 1; duration: Theme.animSlow; easing.type: Easing.OutCubic }
        SequentialAnimation {
            ParallelAnimation {
                NumberAnimation { target: card; property: "scale"; to: 1; duration: Theme.animNormal; easing.type: Easing.OutBack; easing.overshoot: 1.2 }
                NumberAnimation { target: card; property: "rotation"; to: 360; duration: Theme.animNormal; easing.type: Easing.InOutCubic }
            }
            ParallelAnimation {
                NumberAnimation { target: lockIcon; property: "rotation"; to: 360; duration: Theme.animNormal; easing.type: Easing.OutCubic }
                NumberAnimation { target: lockIcon; property: "opacity"; to: 0; duration: Theme.animFast }
                NumberAnimation { target: content; property: "opacity"; to: 1; duration: Theme.animNormal; easing.type: Easing.OutCubic }
                NumberAnimation { target: content; property: "scale"; to: 1; duration: Theme.animNormal; easing.type: Easing.OutCubic }
                NumberAnimation { target: cardBg; property: "radius"; to: Theme.cardRadius * 1.5; duration: Theme.animNormal; easing.type: Easing.OutCubic }
                NumberAnimation { target: card; property: "implicitWidth"; to: root.cardW; duration: Theme.animNormal; easing.type: Easing.OutCubic }
                NumberAnimation { target: card; property: "implicitHeight"; to: root.cardH; duration: Theme.animNormal; easing.type: Easing.OutCubic }
            }
        }
    }

    SequentialAnimation {
        id: unlockAnim

        ParallelAnimation {
            NumberAnimation { target: card; property: "implicitWidth"; to: card.collapsedSize; duration: Theme.animNormal; easing.type: Easing.InOutCubic }
            NumberAnimation { target: card; property: "implicitHeight"; to: card.collapsedSize; duration: Theme.animNormal; easing.type: Easing.InOutCubic }
            NumberAnimation { target: cardBg; property: "radius"; to: card.collapsedRadius; duration: Theme.animNormal; easing.type: Easing.InOutCubic }
            NumberAnimation { target: content; property: "scale"; to: 0; duration: Theme.animNormal; easing.type: Easing.InCubic }
            NumberAnimation { target: content; property: "opacity"; to: 0; duration: Theme.animFast }
            NumberAnimation { target: lockIcon; property: "opacity"; to: 1; duration: Theme.animNormal }
            NumberAnimation { target: background; property: "opacity"; to: 0; duration: Theme.animSlow; easing.type: Easing.InCubic }
            SequentialAnimation {
                PauseAnimation { duration: Theme.animFast }
                NumberAnimation { target: card; property: "opacity"; to: 0; duration: Theme.animNormal }
            }
        }
        ScriptAction { script: LockService.locked = false }
    }
}
