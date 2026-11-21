import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DSlider {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankSlider", "DSlider", "qs.DCommon.Widgets")
}
