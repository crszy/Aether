import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DRipple {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankRipple", "DRipple", "qs.DCommon.Widgets")
}
