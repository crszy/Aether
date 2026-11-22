import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DDropdown {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankDropdown", "DDropdown", "qs.DCommon.Widgets")
}
