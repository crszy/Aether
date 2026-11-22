import QtQuick
import qs.dms.Common

DSeekbar {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankSeekbar", "DSeekbar", "qs.Widgets")
}
