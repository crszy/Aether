import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DIcon {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankIcon", "DIcon", "qs.DCommon.Widgets")
}
