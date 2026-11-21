import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DCollapsibleSection {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankCollapsibleSection", "DCollapsibleSection", "qs.DCommon.Widgets")
}
