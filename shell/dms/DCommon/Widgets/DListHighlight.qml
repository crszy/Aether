import QtQuick
import qs.dms.DCommon.Common

Rectangle {
    property bool animate: true
    property bool firstInGroup: true
    property bool lastInGroup: true
    color: Style.selectedContainer
    radius: Style.groupedListInnerRadius
    topLeftRadius: firstInGroup ? Style.groupedListOuterRadius : radius
    topRightRadius: topLeftRadius
    bottomLeftRadius: lastInGroup ? Style.groupedListOuterRadius : radius
    bottomRightRadius: bottomLeftRadius

    Behavior on x {
        enabled: parent && parent.visible && animate && !Style.reduceMotion && !Style.springMotionDisabled
        NumberAnimation {
            duration: Style.shorterDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
        }
    }
    Behavior on y {
        enabled: parent && parent.visible && animate && !Style.reduceMotion && !Style.springMotionDisabled
        NumberAnimation {
            duration: Style.shorterDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Style.expressiveCurves.expressiveFastSpatial
        }
    }
}
