import QtQuick
import qs.dms.Common

DModal {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankModal", "DModal", "qs.Modals.Common")
}
