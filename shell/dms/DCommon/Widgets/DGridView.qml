import QtQuick
import QtQuick.Controls
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets
import "ScrollConstants.js" as Scroll
import "Reveal.js" as Reveal
import "../Common/WheelInput.js" as WheelInput

GridView {
    id: gridView

    property real momentumVelocity: 0
    property bool isMomentumActive: false
    property real friction: Scroll.friction
    readonly property real maximumContentY: Math.max(originY, contentHeight - height + originY)
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
        contentY = Reveal.contentYFor(gridView, top, bottom, revealInsetTop, revealInsetBottom);
    }

    function revealIndex(index) {
        positionViewAtIndex(index, GridView.Contain);
        const item = itemAtIndex(index);
        if (!item)
            return;
        contentY = Reveal.contentYFor(gridView, item.y, item.y + item.height, revealInsetTop, revealInsetBottom);
    }
    property real mouseWheelSpeed: Scroll.mouseWheelSpeed

    flickDeceleration: Scroll.flickDeceleration
    maximumFlickVelocity: Scroll.maximumFlickVelocity
    boundsBehavior: Flickable.StopAtBounds
    boundsMovement: Flickable.FollowBoundsBehavior
    pressDelay: 0
    flickableDirection: Flickable.VerticalFlick

    onMovementStarted: {
        vbar._scrollBarActive = true;
        vbar.hideTimer.stop();
    }
    onMovementEnded: vbar.hideTimer.restart()

    property real _lastOriginY: 0

    // A header that grows after the first layout extends upward past the viewport; a view resting at the top follows the new origin.
    onOriginYChanged: {
        const wasAtTop = contentY <= _lastOriginY + 0.5;
        _lastOriginY = originY;
        if (!wasAtTop || moving || isMomentumActive)
            return;
        contentY = originY;
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

    WheelHandler {
        id: wheelHandler
        enabled: gridView.wheelEnabled

        property real touchpadSpeed: Scroll.touchpadSpeed
        property real momentumRetention: Scroll.momentumRetention
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
                let newY = contentY + scrollAmount;
                newY = Math.max(originY, Math.min(maximumContentY, newY));

                if (flicking) {
                    cancelFlick();
                }

                contentY = newY;
            } else if (kind === WheelInput.Kind.highResWheel) {
                sessionUsedMouseWheel = true;
                momentumAnim.running = false;
                isMomentumActive = false;
                velocitySamples = [];
                momentum = 0;
                momentumVelocity = 0;

                let delta = deltaY / 8 * touchpadSpeed;
                let newY = contentY - delta;
                newY = Math.max(originY, Math.min(maximumContentY, newY));

                if (flicking) {
                    cancelFlick();
                }

                contentY = newY;
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
                    momentum = momentum * momentumRetention + delta * Scroll.momentumDeltaFactor;
                    delta += momentum;
                } else {
                    momentum = 0;
                }

                let newY = contentY - delta;
                newY = Math.max(originY, Math.min(maximumContentY, newY));

                if (flicking) {
                    cancelFlick();
                }

                contentY = newY;
            }

            event.accepted = true;
        }
        onActiveChanged: {
            if (!active)
                release();
        }

        function release() {
            if (!sessionUsedMouseWheel && Math.abs(momentumVelocity) >= Scroll.minMomentumVelocity) {
                startMomentum();
                return;
            }
            velocitySamples = [];
            momentumVelocity = 0;
        }
    }

    FrameAnimation {
        id: momentumAnim
        running: false

        onTriggered: {
            const dt = frameTime;
            const newY = contentY - momentumVelocity * dt;
            const minY = originY;
            const maxY = maximumContentY;

            if (newY < minY || newY > maxY) {
                contentY = newY < minY ? minY : maxY;
                running = false;
                isMomentumActive = false;
                momentumVelocity = 0;
                return;
            }

            contentY = newY;
            momentumVelocity *= Math.pow(friction, dt / 0.016);

            if (Math.abs(momentumVelocity) < Scroll.momentumStopThreshold) {
                running = false;
                isMomentumActive = false;
                momentumVelocity = 0;
            }
        }
    }

    Loader {
        parent: gridView
        anchors.fill: parent
        anchors.topMargin: gridView.fadeTopInset
        anchors.bottomMargin: gridView.fadeBottomInset
        anchors.leftMargin: gridView.fadeSideInset
        anchors.rightMargin: gridView.fadeSideInset
        z: 1
        active: gridView.fadeEdges && gridView.contentHeight > gridView.height + 1
        sourceComponent: DEdgeFade {
            target: gridView
            length: gridView.fadeLength
        }
    }

    ScrollBar.vertical: DScrollbar {
        id: vbar
        z: 2
        targetFlickable: gridView
        allowed: gridView.showScrollBar
    }
}
