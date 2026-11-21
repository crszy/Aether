import QtQuick
import qs.dms.DCommon.Common as DCommon

DCommon.DSocket {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankSocket", "DSocket", "qs.DCommon.Common")
}
