import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DFilterChips {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankFilterChips", "DFilterChips", "qs.DCommon.Widgets")
}
