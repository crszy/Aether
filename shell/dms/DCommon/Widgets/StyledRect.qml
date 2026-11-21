import QtQuick
import qs.dms.DCommon.Common

Rectangle {
    color: "transparent"
    radius: Style.cornerRadiusM

    readonly property var standardAnimation: {
        "duration": Appearance.anim.durations.normal,
        "easing.type": Easing.BezierSpline,
        "easing.bezierCurve": Appearance.anim.curves.standard
    }

    Behavior on opacity {
        NumberAnimation {
            duration: standardAnimation.duration
            easing.type: standardAnimation["easing.type"]
            easing.bezierCurve: standardAnimation["easing.bezierCurve"]
        }
    }
}
