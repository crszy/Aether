import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DGridView {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankGridView", "DGridView", "qs.DCommon.Widgets")
}
