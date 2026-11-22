pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common

QtObject {
    id: root

    property bool enabled: true
    property bool reducedMotion: false
    readonly property bool systemReducedMotion: Style.reduceMotion
    readonly property bool effectiveReducedMotion: reducedMotion || systemReducedMotion

    property real stiffness: 100
    property real damping: 16
    property real mass: 1
    property real positionEpsilon: 0.01
    property real velocityEpsilon: 0.01
    property real maximumFrameTime: 1 / 30
    property real integrationStep: 1 / 240

    property real value: 0
    property real target: value
    property real velocity: 0

    property bool _running: false
    readonly property bool running: _running
    readonly property bool settled: !_running
    readonly property real timeConstantMs: effectiveReducedMotion ? 0 : 2000 * mass / Math.max(1, damping)
    property int settleDurationMs: 0

    function isSettled() {
        return Math.abs(target - value) <= positionEpsilon && Math.abs(velocity) <= velocityEpsilon;
    }

    function snapTo(v) {
        target = v;
        value = v;
        velocity = 0;
        _running = false;
    }

    function retarget(v) {
        if (!enabled || effectiveReducedMotion) {
            snapTo(v);
            return;
        }
        if (target === v && !_running)
            return;
        target = v;
        const tau = timeConstantMs;
        const distance = Math.abs(target - value);
        settleDurationMs = tau > 0 ? Math.round(Math.max(tau * 3, tau * Math.log(Math.max(distance, positionEpsilon) / positionEpsilon))) : 0;
        if (!isSettled())
            _running = true;
    }

    function advance(rawFrameTime) {
        if (!enabled || !_running || effectiveReducedMotion)
            return;

        const frameTime = Math.min(Math.max(rawFrameTime, 0), maximumFrameTime);
        if (frameTime <= 0)
            return;

        const inverseMass = 1 / Math.max(0.001, mass);
        const stableStep = 1 / Math.max(1, damping * inverseMass, Math.sqrt(stiffness * inverseMass));
        const steps = Math.max(1, Math.ceil(frameTime / Math.min(integrationStep, stableStep)));
        const step = frameTime / steps;
        let nextValue = value;
        let nextVelocity = velocity;

        for (let i = 0; i < steps; i++) {
            nextVelocity += (stiffness * (target - nextValue) - damping * nextVelocity) * inverseMass * step;
            nextValue += nextVelocity * step;
        }

        if (Math.abs(target - nextValue) <= positionEpsilon && Math.abs(nextVelocity) <= velocityEpsilon) {
            velocity = 0;
            value = target;
            _running = false;
            return;
        }

        velocity = nextVelocity;
        value = nextValue;
    }

    onEffectiveReducedMotionChanged: {
        if (effectiveReducedMotion)
            snapTo(target);
    }

    onEnabledChanged: {
        if (!enabled) {
            snapTo(target);
            return;
        }
        retarget(target);
    }

    property FrameAnimation driver: FrameAnimation {
        running: root.enabled && root._running && !root.effectiveReducedMotion
        onTriggered: root.advance(frameTime)
    }
}
