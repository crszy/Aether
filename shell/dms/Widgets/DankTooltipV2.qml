import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DTooltipV2 {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankTooltipV2", "DTooltipV2", "qs.DCommon.Widgets")
}
