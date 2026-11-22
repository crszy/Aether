import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DButton {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankButton", "DButton", "qs.DCommon.Widgets")
}
