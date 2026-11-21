import QtQuick
import qs.dms.Common

DFocusGrab {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankFocusGrab", "DFocusGrab", "qs.Widgets")
}
