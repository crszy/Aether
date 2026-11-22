import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DTextField {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankTextField", "DTextField", "qs.DCommon.Widgets")
}
