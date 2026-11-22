import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DNFIcon {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankNFIcon", "DNFIcon", "qs.DCommon.Widgets")
}
