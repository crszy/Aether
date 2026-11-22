import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DRefreshButton {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankRefreshButton", "DRefreshButton", "qs.DCommon.Widgets")
}
