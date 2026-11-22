import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DTextCursor {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankTextCursor", "DTextCursor", "qs.DCommon.Widgets")
}
