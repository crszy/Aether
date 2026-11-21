import QtQuick
import QtQuick.Controls
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets
import "ScrollConstants.js" as Scroll
import "Reveal.js" as Reveal
import "../Common/WheelInput.js" as WheelInput

ListView {
    id: listView

    property real scrollBarTopMargin: 0
    property bool wheelEnabled: true
    property bool showScrollBar: true
    property bool fadeEdges: true
    property real fadeLength: Style.spacingXL
    property real fadeTopInset: 0
    property real fadeBottomInset: 0
    property real fadeSideInset: 0
    readonly property real revealInsetTop: fadeEdges ? fadeTopInset + fadeLength : 0
    readonly property real revealInsetBottom: fadeEdges ? fadeBottomInset + fadeLength : 0

    function revealRange(top, bottom) {
        contentY = Reveal.contentYFor(listView, top, bottom, revealInsetTop, revealInsetBottom);
    }

    function resetScroll() {
        savedY = originY;
        contentY = originY;
    }

    function revealIndex(index) {
        positionViewAtIndex(index, ListView.Contain);
        const item = itemAtIndex(index);
        if (!item)
            return;
        contentY = Reveal.contentYFor(listView, item.y, item.y + item.height, revealInsetTop, revealInsetBottom);
    }
    property real mouseWheelSpeed: Scroll.mouseWheelSpeed
    property real savedY: 0
    property bool justChanged: false
    property bool isUserScrolling: false
    property real momentumVelocity: 0
    property bool isMomentumActive: false
    property real friction: Scroll.friction
    readonly property real maximumContentY: Math.max(originY, contentHeight - height + originY)
    // Rows the model just inserted fade in; rows the view creates while scrolling do not
    readonly property bool populating: orphanSweep.running
    property bool rowFadeEnabled: Math.floor(Style.currentAnimationBaseDuration * 0.4) >= 1
    property bool fadeInitialRows: true
    property int _insertFirst: 0
    property bool _populateInsert: false

    // Called from a delegate's Component.onCompleted. A ViewTransition would park every position the
    // view writes while it runs (QQuickItemViewTransitionableItem::moveTo), so rows fade on their own.
    function fadeIn(row: Item, index: int) {
        if (!rowFadeEnabled || !populating || (!fadeInitialRows && _populateInsert))
            return;
        row.opacity = 0;
        rowFade.createObject(row, {
            "target": row,
            "stagger": Math.min(8, Math.max(0, index - _insertFirst))
        }).start();
    }

    Component {
        id: rowFade

        SequentialAnimation {
            id: fade

            required property Item target
            required property int stagger

            PauseAnimation {
                duration: fade.stagger * Math.round(Style.currentAnimationBaseDuration * 0.03)
            }

            DAnim {
                target: fade.target
                property: "opacity"
                from: 0
                to: 1
                duration: Style.expressiveDurations.fast
                easing.bezierCurve: Style.expressiveCurves.emphasizedDecel
            }

            onStopped: destroy()
        }
    }

    property bool highlightSelection: false
    property bool animateSelection: true
    property bool _selectionMotionReady: false

    highlightFollowsCurrentItem: !highlightSelection
    highlight: highlightSelection ? selectionHighlight : null
    onHighlightSelectionChanged: {
        _selectionMotionReady = false;
        if (highlightSelection)
            selectionReady.restart();
    }

    Timer {
        id: selectionReady
        interval: 0
        onTriggered: listView._selectionMotionReady = true
    }

    Component {
        id: selectionHighlight
        DListHighlight {
            readonly property Item row: listView.currentItem
            visible: row !== null
            animate: listView.animateSelection && listView._selectionMotionReady
            x: row?.x ?? 0
            y: row?.y ?? 0
            width: row?.width ?? 0
            height: row?.height ?? 0
            topLeftRadius: row?.topLeftRadius ?? radius
            topRightRadius: row?.topRightRadius ?? radius
            bottomLeftRadius: row?.bottomLeftRadius ?? radius
            bottomRightRadius: row?.bottomRightRadius ?? radius
        }
    }

    flickDeceleration: Scroll.flickDeceleration
    maximumFlickVelocity: Scroll.maximumFlickVelocity
    boundsBehavior: Flickable.StopAtBounds
    boundsMovement: Flickable.FollowBoundsBehavior
    pressDelay: 0
    flickableDirection: Flickable.VerticalFlick

    // A released delegate keeps painting at its old spot until deleteLater runs; Qt marks it with index -1.
    // itemAtIndex is not a safe test: the view only remaps it at its window's next polish.
    Connections {
        target: listView.model?.objectName !== undefined ? listView.model : null
        ignoreUnknownSignals: true
        function onRowsInserted(parent, first, last) {
            listView._insertFirst = first;
            listView._populateInsert = last - first + 1 >= (listView.model.rowCount?.() ?? listView.count);
            orphanSweep.arm();
        }
        function onDataChanged() {
            orphanSweep.arm();
        }
        function onRowsRemoved() {
            orphanSweep.arm();
        }
        function onRowsMoved() {
            orphanSweep.arm();
        }
        function onModelReset() {
            listView._populateInsert = true;
            orphanSweep.arm();
        }
    }

    FrameAnimation {
        id: orphanSweep

        property int frames: 0

        function arm() {
            frames = 0;
            running = true;
        }

        onTriggered: {
            for (const c of listView.contentItem.children) {
                if (c?.index < 0 && c.visible)
                    c.visible = false;
            }
            if (++frames >= 3)
                running = false;
        }
    }

    onMovementStarted: {
        isUserScrolling = true;
        vbar._scrollBarActive = true;
        vbar.hideTimer.stop();
    }
    onMovementEnded: {
        isUserScrolling = false;
        vbar.hideTimer.restart();
    }

    onContentYChanged: {
        if (!justChanged && isUserScrolling) {
            savedY = contentY;
        }
        justChanged = false;
    }

    property real _lastOriginY: 0

    // A header that grows after the first layout extends upward past the viewport; a view resting at the top follows the new origin.
    onOriginYChanged: {
        const wasAtTop = contentY <= _lastOriginY + 0.5;
        _lastOriginY = originY;
        if (!wasAtTop || isUserScrolling || isMomentumActive)
            return;
        contentY = originY;
        savedY = originY;
    }

    function forwardWheel(event) {
        if (!wheelEnabled)
            return;
        wheelHandler.handleWheel(event);
    }

    function forwardWheelEnd() {
        wheelHandler.release();
    }

    function stopMomentum() {
        cancelFlick();
        momentumAnim.running = false;
        isMomentumActive = false;
        momentumVelocity = 0;
        wheelHandler.momentum = 0;
        wheelHandler.velocitySamples = [];
    }

    onModelChanged: {
        _selectionMotionReady = false;
        if (highlightSelection)
            selectionReady.restart();
        justChanged = true;
        contentY = savedY;
    }

    WheelHandler {
        id: wheelHandler
        enabled: listView.wheelEnabled
        property real touchpadSpeed: Scroll.touchpadSpeed
        property real lastWheelTime: 0
        property real momentum: 0
        property var velocitySamples: []
        property bool sessionUsedMouseWheel: false

        function startMomentum() {
            isMomentumActive = true;
            momentumAnim.running = true;
        }

        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad

        onWheel: event => handleWheel(event)

        function handleWheel(event) {
            isUserScrolling = true;
            vbar._scrollBarActive = true;
            vbar.hideTimer.restart();

            const currentTime = Date.now();
            const timeDelta = currentTime - lastWheelTime;
            lastWheelTime = currentTime;

            const kind = WheelInput.verticalKind(event);
            const deltaY = event.angleDelta.y;

            if (kind === WheelInput.Kind.wheel) {
                sessionUsedMouseWheel = true;
                momentumAnim.running = false;
                isMomentumActive = false;
                velocitySamples = [];
                momentum = 0;
                momentumVelocity = 0;

                const lines = Math.round(Math.abs(deltaY) / 120);
                const scrollAmount = (deltaY > 0 ? -lines : lines) * mouseWheelSpeed;
                let newY = listView.contentY + scrollAmount;
                const maxY = listView.maximumContentY;
                newY = Math.max(listView.originY, Math.min(maxY, newY));

                if (listView.flicking) {
                    listView.cancelFlick();
                }

                listView.contentY = newY;
                savedY = newY;
            } else if (kind === WheelInput.Kind.highResWheel) {
                sessionUsedMouseWheel = true;
                momentumAnim.running = false;
                isMomentumActive = false;
                velocitySamples = [];
                momentum = 0;
                momentumVelocity = 0;

                let delta = deltaY / 8 * touchpadSpeed;
                let newY = listView.contentY - delta;
                const maxY = listView.maximumContentY;
                newY = Math.max(listView.originY, Math.min(maxY, newY));

                if (listView.flicking) {
                    listView.cancelFlick();
                }

                listView.contentY = newY;
                savedY = newY;
            } else if (kind === WheelInput.Kind.touchpad) {
                sessionUsedMouseWheel = false;
                momentumAnim.running = false;
                isMomentumActive = false;

                let delta = event.pixelDelta.y * touchpadSpeed;

                velocitySamples.push({
                    "delta": delta,
                    "time": currentTime
                });
                velocitySamples = velocitySamples.filter(s => currentTime - s.time < Scroll.velocitySampleWindowMs);

                if (velocitySamples.length > 1) {
                    const totalDelta = velocitySamples.reduce((sum, s) => sum + s.delta, 0);
                    const timeSpan = currentTime - velocitySamples[0].time;
                    if (timeSpan > 0) {
                        momentumVelocity = Math.max(-Scroll.maxMomentumVelocity, Math.min(Scroll.maxMomentumVelocity, totalDelta / timeSpan * 1000));
                    }
                }

                if (timeDelta < Scroll.momentumTimeThreshold) {
                    momentum = momentum * Scroll.momentumRetention + delta * Scroll.momentumDeltaFactor;
                    delta += momentum;
                } else {
                    momentum = 0;
                }

                let newY = listView.contentY - delta;
                const maxY = listView.maximumContentY;
                newY = Math.max(listView.originY, Math.min(maxY, newY));

                if (listView.flicking) {
                    listView.cancelFlick();
                }

                listView.contentY = newY;
                savedY = newY;
            }

            event.accepted = true;
        }

        onActiveChanged: {
            if (!active)
                release();
        }

        function release() {
            isUserScrolling = false;
            if (!sessionUsedMouseWheel && Math.abs(momentumVelocity) >= Scroll.minMomentumVelocity) {
                startMomentum();
            } else {
                velocitySamples = [];
                momentumVelocity = 0;
            }
        }
    }

    FrameAnimation {
        id: momentumAnim
        running: false

        onTriggered: {
            const dt = frameTime;
            const newY = contentY - momentumVelocity * dt;
            const maxY = listView.maximumContentY;
            const minY = originY;

            if (newY < minY || newY > maxY) {
                contentY = newY < minY ? minY : maxY;
                savedY = contentY;
                running = false;
                isMomentumActive = false;
                momentumVelocity = 0;
                return;
            }

            contentY = newY;
            savedY = newY;
            momentumVelocity *= Math.pow(friction, dt / 0.016);

            if (Math.abs(momentumVelocity) < Scroll.momentumStopThreshold) {
                running = false;
                isMomentumActive = false;
                momentumVelocity = 0;
            }
        }
    }

    Loader {
        parent: listView
        anchors.fill: parent
        anchors.topMargin: listView.fadeTopInset
        anchors.bottomMargin: listView.fadeBottomInset
        anchors.leftMargin: listView.fadeSideInset
        anchors.rightMargin: listView.fadeSideInset
        z: 1
        active: listView.fadeEdges && listView.contentHeight > listView.height + 1
        sourceComponent: DEdgeFade {
            target: listView
            length: listView.fadeLength
        }
    }

    ScrollBar.vertical: DScrollbar {
        id: vbar
        z: 2
        targetFlickable: listView
        allowed: listView.showScrollBar
        topMargin: listView.scrollBarTopMargin
    }
}
