import QtQuick
import qs.dms.DCommon.Common as DCommon

DCommon.DColorAnim {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankColorAnim", "DColorAnim", "qs.DCommon.Common")
}
