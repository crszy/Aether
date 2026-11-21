import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DListView {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankListView", "DListView", "qs.DCommon.Widgets")
}
