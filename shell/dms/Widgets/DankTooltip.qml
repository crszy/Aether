import QtQuick
import qs.dms.Common

DTooltip {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankTooltip", "DTooltip", "qs.Widgets")
}
