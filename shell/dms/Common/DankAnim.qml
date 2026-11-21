import QtQuick
import qs.dms.DCommon.Common as DCommon

DCommon.DAnim {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankAnim", "DAnim", "qs.DCommon.Common")
}
