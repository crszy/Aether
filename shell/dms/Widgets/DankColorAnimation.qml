import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DColorAnimation {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankColorAnimation", "DColorAnimation", "qs.DCommon.Widgets")
}
