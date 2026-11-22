import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DFlickable {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankFlickable", "DFlickable", "qs.DCommon.Widgets")
}
