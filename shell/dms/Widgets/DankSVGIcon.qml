import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DSVGIcon {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankSVGIcon", "DSVGIcon", "qs.DCommon.Widgets")
}
