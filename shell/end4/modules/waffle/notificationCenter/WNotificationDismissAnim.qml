import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import qs.end4.modules.common
import qs.end4.modules.common.widgets
import qs.end4.modules.common.functions
import qs.end4.modules.waffle.looks

SequentialAnimation {
    id: root

    required property var target

    PropertyAction {
        target: root.target
        property: "ListView.delayRemove"
        value: true
    }
    NumberAnimation {
        target: root.target
        property: "x"
        to: root.target.width
        duration: 250
        easing.type: Easing.BezierSpline
        easing.bezierCurve: Looks.transition.easing.bezierCurve.easeIn
    }
    PropertyAction {
        target: root.target
        property: "ListView.delayRemove"
        value: false
    }
}
