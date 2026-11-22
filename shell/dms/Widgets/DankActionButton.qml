import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DActionButton {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankActionButton", "DActionButton", "qs.DCommon.Widgets")
}
