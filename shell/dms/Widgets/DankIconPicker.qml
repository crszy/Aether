import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DIconPicker {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankIconPicker", "DIconPicker", "qs.DCommon.Widgets")
}
