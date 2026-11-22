import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DScrollbar {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankScrollbar", "DScrollbar", "qs.DCommon.Widgets")
}
