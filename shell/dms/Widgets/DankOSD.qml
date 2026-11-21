import QtQuick
import qs.dms.Common

DOSD {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankOSD", "DOSD", "qs.Widgets")
}
