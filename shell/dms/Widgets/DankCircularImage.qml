import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DCircularImage {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankCircularImage", "DCircularImage", "qs.DCommon.Widgets")
}
