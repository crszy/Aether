import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.dms.DCommon.Common

Item {
    id: root

    visible: false

    required property var targetWindow
    property bool blurEnabled: Style.connectedSurfaceBlurEnabled
    property real blurX: 0
    property real blurY: 0
    property real blurWidth: 0
    property real blurHeight: 0
    property real blurRadius: 0
    property real blurBottomRadius: blurRadius
    property bool clipEnabled: false
    property real clipX: blurX
    property real clipY: blurY
    property real clipWidth: blurWidth
    property real clipHeight: blurHeight

    readonly property bool _active: blurEnabled && Style.blurLayersActive && !!targetWindow

    Region {
        id: blurRegion
        x: root.blurX
        y: root.blurY
        width: root.blurWidth
        height: root.blurHeight
        radius: root.blurRadius

        Region {
            readonly property bool needed: root.blurBottomRadius < root.blurRadius
            x: root.blurX
            y: root.blurY + root.blurRadius
            width: needed ? root.blurWidth : 0
            height: needed ? Math.max(0, root.blurHeight - root.blurRadius) : 0
            radius: root.blurBottomRadius
        }

        Region {
            intersection: Intersection.Intersect
            x: root.clipEnabled ? root.clipX : root.blurX
            y: root.clipEnabled ? root.clipY : root.blurY
            width: root.clipEnabled ? root.clipWidth : root.blurWidth
            height: root.clipEnabled ? root.clipHeight : root.blurHeight
        }
    }

    function _apply() {
        if (!targetWindow)
            return;
        void 0; // Windows: no compositor blur
    }

    function _clear() {
        if (!targetWindow)
            return;
        void 0; // Windows: no compositor blur
    }

    // Republish after wl_surface remaps and geometry changes.
    function kick() {
        if (!targetWindow)
            return;
        void 0; // Windows: no compositor blur
        void 0; // Windows: no compositor blur
    }

    onBlurXChanged: settleTimer.restart()
    onBlurYChanged: settleTimer.restart()
    onBlurWidthChanged: settleTimer.restart()
    onBlurHeightChanged: settleTimer.restart()
    onBlurRadiusChanged: settleTimer.restart()
    onBlurBottomRadiusChanged: settleTimer.restart()
    onClipEnabledChanged: settleTimer.restart()
    onClipXChanged: settleTimer.restart()
    onClipYChanged: settleTimer.restart()
    onClipWidthChanged: settleTimer.restart()
    onClipHeightChanged: settleTimer.restart()

    Timer {
        id: settleTimer
        interval: 16
        onTriggered: {
            if (!root.targetWindow?.visible)
                return;
            root.kick();
            settleRepeatTimer.restart();
        }
    }

    Timer {
        id: settleRepeatTimer
        interval: 96
        onTriggered: {
            if (!root.targetWindow?.visible)
                return;
            root.kick();
        }
    }

    Timer {
        id: lifecycleTimer
        interval: 0
        onTriggered: {
            if (!root.targetWindow)
                return;
            if (root.targetWindow.visible) {
                root.kick();
                return;
            }
            root._apply();
        }
    }

    on_ActiveChanged: {
        if (!_active) {
            _clear();
            return;
        }
        lifecycleTimer.restart();
    }

    onTargetWindowChanged: {
        lifecycleTimer.stop();
        _apply();
    }

    Connections {
        target: root.targetWindow ?? null
        ignoreUnknownSignals: true
        function onVisibleChanged() {
            if (!root.targetWindow?.visible) {
                root._clear();
                return;
            }
            lifecycleTimer.restart();
        }
        function onWidthChanged() {
            settleTimer.restart();
        }
        function onHeightChanged() {
            settleTimer.restart();
        }
        function onResourcesLost() {
            root._clear();
            lifecycleTimer.restart();
        }
        function onWindowConnected() {
            lifecycleTimer.restart();
        }
    }

    Component.onCompleted: lifecycleTimer.restart()
    Component.onDestruction: {
        lifecycleTimer.stop();
        if (true) // Windows: no compositor blur
            return;
        void 0; // Windows: no compositor blur
    }
}
