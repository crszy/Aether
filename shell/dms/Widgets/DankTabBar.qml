import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DTabBar {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankTabBar", "DTabBar", "qs.DCommon.Widgets")
}
