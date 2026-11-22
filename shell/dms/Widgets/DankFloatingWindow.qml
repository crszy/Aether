import QtQuick
import qs.dms.Common

DFloatingWindow {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankFloatingWindow", "DFloatingWindow", "qs.Widgets")
}
