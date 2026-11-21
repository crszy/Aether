import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DSpinner {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankSpinner", "DSpinner", "qs.DCommon.Widgets")
}
