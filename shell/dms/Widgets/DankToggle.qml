import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DToggle {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankToggle", "DToggle", "qs.DCommon.Widgets")
}
