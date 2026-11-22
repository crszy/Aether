import QtQuick
import qs.dms.DCommon.Widgets
import qs.dms.DCommon.Common

DActionButton {
    radius: Style.buttonRadius(width, height, buttonSize, pressed, circular)
    shapeDuration: LockMetrics.effectsDuration
    shapeCurve: Style.expressiveCurves.expressiveEffects
    stateDuration: LockMetrics.effectsDuration
    stateCurve: Style.expressiveCurves.expressiveEffects
}
