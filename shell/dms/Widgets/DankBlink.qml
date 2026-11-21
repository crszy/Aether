import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DBlink {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankBlink", "DBlink", "qs.DCommon.Widgets")
}
