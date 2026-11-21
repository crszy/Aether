import QtQuick
import qs.dms.DCommon.Common

Item {
    id: root

    property bool indeterminate: false
    property real progress: 0
    property color color: Style.circularWavyProgressColor
    property color trackColor: Style.circularWavyProgressTrackColor
    property real strokeWidth: Style.circularWavyProgressStrokeWidth
    property real trackStrokeWidth: Style.circularWavyProgressTrackStrokeWidth
    property real gapSize: Style.circularWavyProgressGapSize
    property real wavelength: Style.circularWavyProgressWavelength
    property real waveSpeed: wavelength
    property real maxWaveAmplitude: Style.circularWavyProgressWaveAmplitude
    property real indeterminateAmplitude: 1
    property var amplitudeFunction: function (p) {
        return (p <= Style.circularWavyProgressAmplitudeRampStart || p >= Style.circularWavyProgressAmplitudeRampEnd) ? 0 : 1;
    }
    property real startAngle: Style.circularWavyProgressStartAngle
    property real indeterminateMinProgress: Style.circularWavyProgressIndeterminateMinProgress
    property real indeterminateMaxProgress: Style.circularWavyProgressIndeterminateMaxProgress
    property int indeterminateCycleDuration: Style.circularWavyProgressIndeterminateCycleDuration
    property real spinDegreesPerCycle: Style.circularWavyProgressGlobalRotationTarget
    property int rotationStepDuration: Style.circularWavyProgressRotationStepDuration
    property int rotationStepInterval: Style.circularWavyProgressRotationStepDelay

    property real currentAmplitude: 0
    property real waveOffset: 0
    property real indeterminateSweep: indeterminateMinProgress
    property real globalRotation: 0
    property real additionalRotation: 0

    readonly property real clampedProgress: {
        const v = Number(progress);
        return isNaN(v) ? 0 : Math.max(0, Math.min(1, v));
    }
    readonly property real targetAmplitude: {
        const v = indeterminate ? indeterminateAmplitude : amplitudeFunction(clampedProgress);
        return Math.max(0, Math.min(1, v));
    }
    readonly property int waveCount: {
        const r = Math.min(width, height) / 2 - strokeWidth / 2;
        return (wavelength > 0 && r > 0) ? Math.max(5, Math.round(2 * Math.PI * r / wavelength)) : 5;
    }
    readonly property real spinAngle: globalRotation + additionalRotation
    readonly property bool waveAnimating: visible && currentAmplitude > 0 && waveSpeed > 0 && wavelength > 0 && width > 0 && height > 0
    readonly property real fullTurn: 2 * Math.PI
    readonly property real ringRadius: Math.max(0, (Math.min(width, height) - strokeWidth) / 2)
    readonly property real sweepAngle: (indeterminate ? indeterminateSweep : clampedProgress) * fullTurn
    // Pixels cleared between the progress arc and the track (gap + both round-cap insets).
    readonly property real trackClearance: {
        if (sweepAngle <= 0)
            return 0;
        const arcLength = sweepAngle * ringRadius;
        return 2 * Math.min(arcLength, strokeWidth / 2) + Math.min(arcLength, gapSize);
    }
    readonly property real trackStartAngle: sweepAngle + (ringRadius > 0 ? trackClearance / ringRadius : 0)
    readonly property real trackStopAngle: fullTurn - (ringRadius > 0 ? trackClearance / ringRadius : 0)

    implicitWidth: Style.circularWavyProgressSize
    implicitHeight: Style.circularWavyProgressSize
    Component.onCompleted: currentAmplitude = targetAmplitude
    onTargetAmplitudeChanged: {
        if (indeterminate) {
            amplitudeAnim.stop();
            currentAmplitude = targetAmplitude;
            return;
        }
        amplitudeAnim.stop();
        amplitudeAnim.from = currentAmplitude;
        amplitudeAnim.to = targetAmplitude;
        amplitudeAnim.easing.bezierCurve = (targetAmplitude > currentAmplitude) ? Style.expressiveCurves.standard : Style.expressiveCurves.emphasizedAccel;
        amplitudeAnim.start();
    }
    onIndeterminateChanged: {
        indeterminateSweep = indeterminateMinProgress;
        globalRotation = 0;
        additionalRotation = 0;
    }

    NumberAnimation {
        id: amplitudeAnim

        target: root
        property: "currentAmplitude"
        duration: Style.circularWavyProgressAmplitudeDuration
        easing.type: Easing.BezierSpline
    }

    NumberAnimation {
        id: waveOffsetAnim

        target: root
        property: "waveOffset"
        from: 0
        to: 1
        loops: Animation.Infinite
        running: root.waveAnimating
        duration: Math.max(50, Math.round(root.wavelength / Math.max(root.waveSpeed, 0.0001) * 1000 * root.waveCount))
    }

    NumberAnimation {
        target: root
        property: "globalRotation"
        from: 0
        to: root.spinDegreesPerCycle
        duration: root.indeterminateCycleDuration
        loops: Animation.Infinite
        running: root.indeterminate && root.visible
    }

    // Four 90 degree snaps, each followed by a hold.
    SequentialAnimation {
        loops: Animation.Infinite
        running: root.indeterminate && root.visible

        PropertyAction {
            target: root
            property: "additionalRotation"
            value: 0
        }

        NumberAnimation {
            target: root
            property: "additionalRotation"
            to: 90
            duration: root.rotationStepDuration
        }

        PauseAnimation {
            duration: root.rotationStepInterval - root.rotationStepDuration
        }

        NumberAnimation {
            target: root
            property: "additionalRotation"
            to: 180
            duration: root.rotationStepDuration
        }

        PauseAnimation {
            duration: root.rotationStepInterval - root.rotationStepDuration
        }

        NumberAnimation {
            target: root
            property: "additionalRotation"
            to: 270
            duration: root.rotationStepDuration
        }

        PauseAnimation {
            duration: root.rotationStepInterval - root.rotationStepDuration
        }

        NumberAnimation {
            target: root
            property: "additionalRotation"
            to: 360
            duration: root.rotationStepDuration
        }

        PauseAnimation {
            duration: root.indeterminateCycleDuration - 3 * root.rotationStepInterval - root.rotationStepDuration
        }
    }

    // Grows linearly, shrinks with the standard easing.
    SequentialAnimation {
        loops: Animation.Infinite
        running: root.indeterminate && root.visible

        NumberAnimation {
            target: root
            property: "indeterminateSweep"
            from: root.indeterminateMinProgress
            to: root.indeterminateMaxProgress
            duration: root.indeterminateCycleDuration / 2
        }

        NumberAnimation {
            target: root
            property: "indeterminateSweep"
            to: root.indeterminateMinProgress
            duration: root.indeterminateCycleDuration / 2
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Style.expressiveCurves.standard
        }
    }

    ShaderEffect {
        property real widthPx: root.width
        property real heightPx: root.height
        property real radiusPx: root.ringRadius
        property real waveCount: root.waveCount
        property real ampPx: root.currentAmplitude * root.maxWaveAmplitude
        property real phase: (root.currentAmplitude > 0 && root.waveSpeed > 0) ? root.fullTurn * root.waveCount * root.waveOffset : 0
        property real theta0: (root.startAngle + (root.indeterminate ? root.spinAngle : 0)) * Math.PI / 180
        property real sweep: root.sweepAngle
        property real trackStart: root.trackStartAngle
        property real trackStop: root.trackStopAngle
        property real lineWidthPx: root.strokeWidth
        property real trackWidthPx: root.trackStrokeWidth
        property real showTrack: (root.trackColor.a > 0 && root.trackStopAngle > root.trackStartAngle) ? 1 : 0
        property real showProgress: (root.color.a > 0 && root.sweepAngle > 0) ? 1 : 0
        property vector4d fillColor: Qt.vector4d(root.color.r, root.color.g, root.color.b, root.color.a)
        property vector4d trackColor: Qt.vector4d(root.trackColor.r, root.trackColor.g, root.trackColor.b, root.trackColor.a)

        anchors.fill: parent
        visible: root.ringRadius > 0
        fragmentShader: Qt.resolvedUrl("../Shaders/qsb/circular_wavy_progress.frag.qsb")
        blending: true
    }
}
