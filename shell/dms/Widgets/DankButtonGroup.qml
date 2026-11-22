import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DButtonGroup {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankButtonGroup", "DButtonGroup", "qs.DCommon.Widgets")
}
