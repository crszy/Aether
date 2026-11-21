import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DLocationSearch {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankLocationSearch", "DLocationSearch", "qs.DCommon.Widgets")
}
