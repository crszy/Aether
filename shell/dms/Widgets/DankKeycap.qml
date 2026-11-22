import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DKeycap {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankKeycap", "DKeycap", "qs.DCommon.Widgets")
}
