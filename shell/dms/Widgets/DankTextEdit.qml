import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DTextEdit {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankTextEdit", "DTextEdit", "qs.DCommon.Widgets")
}
