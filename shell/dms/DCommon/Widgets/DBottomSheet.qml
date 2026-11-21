import QtQuick
import QtQuick.Window
import qs.dms.DCommon.Common

FocusScope {
    id: root

    property bool opened: false
    property bool dismissible: true
    property string title: ""
    readonly property real titleExtent: title !== "" ? heading.implicitHeight + Style.spacingS : 0
    property real maximumWidth: Style.dialogMaxWidth
    property real sheetHeight: -1
    property real topMargin: Style.spacingL
    property real padding: Style.spacingL
    property real contentSpacing: Style.spacingS
    property real scrimRadius: 0
    property vector4d scrimRadii: Qt.vector4d(scrimRadius, scrimRadius, scrimRadius, scrimRadius)
    property bool scrimVisible: true
    property color surfaceColor: Style.cardSurface
    property Item initialFocusItem: null
    property Item returnFocusItem: null
    default property alias content: body.data
    readonly property alias contentItem: body
    readonly property bool animating: slide.running
    readonly property bool active: opened || progress > 0
    readonly property Item focusTarget: initialFocusItem?.visible && initialFocusItem.enabled ? initialFocusItem : focusItems(body)[0] ?? root
    readonly property real focusPadding: Style.focusRingOffset + Style.focusRingWidth
    readonly property bool animationsEnabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
    property real progress: opened ? 1 : 0
    property real dragOffset: 0
    property bool backwardsFocus: false
    property Item savedFocusItem: null

    signal dismissRequested

    anchors.fill: parent
    visible: active
    clip: true
    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true
    Accessible.role: Accessible.Dialog
    Accessible.name: title

    onOpenedChanged: {
        if (!opened) {
            scroll.stopMomentum();
            focusTimer.restart();
            return;
        }
        savedFocusItem = root.Window.window?.activeFocusItem ?? null;
        dragOffset = 0;
        scroll.contentY = 0;
        focusTimer.restart();
    }

    function requestDismiss() {
        if (opened && dismissible)
            dismissRequested();
    }

    function containsItem(item) {
        for (let ancestor = item; ancestor; ancestor = ancestor.parent) {
            if (ancestor === root)
                return true;
        }
        return false;
    }

    function focusItems(item) {
        if (!item.visible || !item.enabled)
            return [];
        const targets = item.activeFocusOnTab ? [item] : [];
        for (const child of item.children)
            targets.push(...focusItems(child));
        return targets;
    }

    function cycleFocus(backwards, reason) {
        const targets = focusItems(surface);
        const current = root.Window.window?.activeFocusItem;
        const index = targets.indexOf(current);
        const next = index < 0 ? (backwards ? targets.length - 1 : 0) : (index + (backwards ? -1 : 1) + targets.length) % targets.length;
        (targets[next] ?? root).forceActiveFocus(reason ?? (backwards ? Qt.BacktabFocusReason : Qt.TabFocusReason));
        return true;
    }

    function revealFocus() {
        const item = root.Window.window?.activeFocusItem;
        if (!opened || !visible || !item || !containsItem(item))
            return;
        let ancestor = item;
        while (ancestor && ancestor !== body)
            ancestor = ancestor.parent;
        if (!ancestor)
            return;
        const point = item.mapToItem(scroll.contentItem, 0, 0);
        scroll.revealRange(point.y - focusPadding, point.y + item.height + focusPadding);
    }

    readonly property Item windowFocusItem: root.Window.window?.activeFocusItem ?? null
    onWindowFocusItemChanged: {
        if (!opened || !visible || !enabled || !windowFocusItem)
            return;
        if (containsItem(windowFocusItem)) {
            revealFocus();
            return;
        }
        if (windowFocusItem.focusReason === Qt.MouseFocusReason)
            return;
        containFocusTimer.restart();
    }

    Keys.onShortcutOverride: event => {
        if (!opened)
            return;
        backwardsFocus = event.key === Qt.Key_Backtab || !!(event.modifiers & Qt.ShiftModifier);
        event.accepted = true;
    }

    function handleKeyEvent(event) {
        if (!opened)
            return false;
        switch (event.key) {
        case Qt.Key_Escape:
            requestDismiss();
            break;
        case Qt.Key_Tab:
        case Qt.Key_Backtab:
        case Qt.Key_F6:
            cycleFocus(event.key === Qt.Key_Backtab || !!(event.modifiers & Qt.ShiftModifier));
            break;
        }
        return true;
    }
    Keys.onPressed: event => event.accepted = handleKeyEvent(event)
    Keys.onReleased: event => event.accepted = opened

    Behavior on progress {
        enabled: root.animationsEnabled
        DAnim {
            id: slide
            duration: Style.expressiveDurations.expressiveDefaultSpatial
            easing.bezierCurve: Style.expressiveCurves.expressiveDefaultSpatial
        }
    }

    onActiveChanged: {
        if (!active)
            dragOffset = 0;
    }

    Behavior on dragOffset {
        enabled: root.animationsEnabled && root.opened && !handleDrag.active
        DAnim {
            duration: Style.expressiveDurations.expressiveDefaultSpatial
            easing.bezierCurve: Style.expressiveCurves.expressiveDefaultSpatial
        }
    }

    data: [
        Timer {
            id: containFocusTimer
            interval: 0
            onTriggered: {
                if (root.opened && root.visible && root.enabled && !root.containsItem(root.windowFocusItem))
                    root.cycleFocus(root.backwardsFocus, Qt.OtherFocusReason);
            }
        },
        Timer {
            id: focusTimer
            interval: 0
            onTriggered: {
                if (!root.opened) {
                    const target = root.returnFocusItem ?? root.savedFocusItem;
                    if (target?.visible && target.enabled)
                        target.forceActiveFocus(Qt.PopupFocusReason);
                    root.savedFocusItem = null;
                    return;
                }
                if (!root.visible || !root.enabled)
                    return;
                const target = root.focusTarget;
                (target?.visible && target.enabled ? target : root).forceActiveFocus(Qt.PopupFocusReason);
            }
        },
        Rectangle {
            anchors.fill: parent
            topLeftRadius: root.scrimRadii.x
            topRightRadius: root.scrimRadii.y
            bottomRightRadius: root.scrimRadii.z
            bottomLeftRadius: root.scrimRadii.w
            visible: root.scrimVisible
            color: Style.backdropColor
            opacity: root.opened ? Style.scrimAlpha : 0

            Behavior on opacity {
                enabled: root.animationsEnabled
                DAnim {
                    duration: Style.expressiveDurations.expressiveEffects
                    easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                }
            }
        },
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            preventStealing: true
            onClicked: root.requestDismiss()
            onWheel: wheel => wheel.accepted = true
        },
        Rectangle {
            id: surface
            width: Math.min(root.maximumWidth, root.width)
            height: Math.max(0, Math.min(root.height - root.topMargin, root.sheetHeight >= 0 ? root.sheetHeight : header.y + root.titleExtent + body.implicitHeight + root.padding))
            x: (root.width - width) / 2
            y: root.height - (height - Math.max(0, Math.min(height, root.dragOffset))) * Math.max(0, Math.min(1, root.progress))
            topLeftRadius: Style.cornerRadiusXL
            topRightRadius: Style.cornerRadiusXL
            bottomLeftRadius: width >= root.width ? root.scrimRadii.w : 0
            bottomRightRadius: width >= root.width ? root.scrimRadii.z : 0
            color: root.surfaceColor
            border.width: Style.layerOutlineWidth
            border.color: Style.outlineMedium
            enabled: root.opened

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
                hoverEnabled: true
                onWheel: wheel => wheel.accepted = true
            }

            Item {
                id: handleArea
                width: parent.width
                height: Style.minimumTouchTargetSize
                enabled: root.opened && root.dismissible

                Rectangle {
                    anchors.centerIn: parent
                    width: Style.bottomSheetHandleWidth
                    height: Style.bottomSheetHandleHeight
                    radius: Style.fullRadius(width, height)
                    color: Style.onSurfaceVariant_40
                }

                HoverHandler {
                    cursorShape: handleDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                }

                DragHandler {
                    id: handleDrag
                    target: null
                    xAxis.enabled: false
                    onCentroidChanged: {
                        if (!active)
                            return;
                        root.dragOffset = Math.max(0, Math.min(surface.height, centroid.scenePosition.y - centroid.scenePressPosition.y));
                    }
                    onGrabChanged: (transition, point) => {
                        switch (transition) {
                        case PointerDevice.UngrabExclusive:
                            if (root.dragOffset >= Math.min(Style.minimumTouchTargetSize, surface.height / 3))
                                root.requestDismiss();
                            if (root.opened)
                                root.dragOffset = 0;
                            return;
                        case PointerDevice.CancelGrabExclusive:
                            if (root.opened)
                                root.dragOffset = 0;
                            return;
                        }
                    }
                }
            }

            Item {
                id: header
                x: root.padding
                y: handleArea.height
                width: parent.width - root.padding * 2
                height: heading.implicitHeight
                visible: root.title !== ""

                StyledText {
                    id: heading
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.title
                    font.pixelSize: Style.fontSizeLarge
                    font.weight: Style.fontWeightMedium
                    color: Style.onSurface
                    elide: Text.ElideRight
                }
            }

            DFlickable {
                id: scroll
                x: root.padding - root.focusPadding
                y: header.y + root.titleExtent - root.focusPadding
                width: parent.width - root.padding * 2 + root.focusPadding * 2
                height: Math.max(0, parent.height - y - root.padding + root.focusPadding)
                contentWidth: width
                contentHeight: body.implicitHeight + root.focusPadding * 2
                clip: true

                Column {
                    id: body
                    x: root.focusPadding
                    y: root.focusPadding
                    width: scroll.width - root.focusPadding * 2
                    spacing: root.contentSpacing
                }
            }
        }
    ]
}
