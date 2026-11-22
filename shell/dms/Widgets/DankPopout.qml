import QtQuick
import qs.dms.Common

DPopout {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankPopout", "DPopout", "qs.Widgets")
}
