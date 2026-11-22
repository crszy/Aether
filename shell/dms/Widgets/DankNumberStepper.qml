import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DNumberStepper {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankNumberStepper", "DNumberStepper", "qs.DCommon.Widgets")
}
