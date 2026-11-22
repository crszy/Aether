import QtQuick
import QtQuick.Shapes
import qs.dms.DCommon.Common

Item {
    id: root

    property real value: -1
    property real startAngle: -90
    property real spanAngle: 360
    property real strokeWidth: Style.spacingXS
    property real trackGap: 0
    property color ringColor: Style.primary
    property color trackColor: Style.withAlpha(Style.onSurface, Style.stateLayerFocus)
    property bool animated: true
    property int animationDuration: Style.expressiveDurations.normal
    default property alias content: center.data

    readonly property bool hasRing: value >= 0
    readonly property bool splitTrack: trackGap > 0 && sweep > 0
    readonly property real ringRadius: Math.max(0, Math.min(width, height) / 2 - strokeWidth / 2)
    readonly property real innerSize: Math.max(0, Math.min(width, height) - strokeWidth * 2)
    readonly property real gapAngle: ringRadius > 0 ? (strokeWidth + trackGap) / ringRadius * 180 / Math.PI : 0
    readonly property real trackStart: splitTrack ? startAngle + sweep + gapAngle : startAngle
    readonly property real trackSweep: splitTrack ? spanAngle - sweep - gapAngle * (spanAngle >= 360 ? 2 : 1) : spanAngle
    property real sweep: hasRing ? Math.min(Math.max(value, 0), 1) * spanAngle : 0

    implicitWidth: Style.iconButtonSize
    implicitHeight: implicitWidth

    Behavior on sweep {
        enabled: root.animated && !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
        NumberAnimation {
            duration: root.animationDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Style.expressiveCurves.standard
        }
    }

    Shape {
        anchors.fill: parent
        visible: root.hasRing
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: "transparent"
            strokeColor: root.trackSweep > 0 ? root.trackColor : "transparent"
            strokeWidth: root.strokeWidth
            capStyle: root.spanAngle >= 360 && !root.splitTrack ? ShapePath.FlatCap : ShapePath.RoundCap

            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: root.ringRadius
                radiusY: root.ringRadius
                startAngle: root.trackStart
                sweepAngle: Math.max(0, root.trackSweep)
            }
        }

        ShapePath {
            fillColor: "transparent"
            strokeColor: root.sweep > 0 ? root.ringColor : "transparent"
            strokeWidth: root.strokeWidth
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: root.ringRadius
                radiusY: root.ringRadius
                startAngle: root.startAngle
                sweepAngle: root.sweep
            }
        }
    }

    Item {
        id: center
        anchors.centerIn: parent
        width: root.innerSize
        height: root.innerSize
    }
}
