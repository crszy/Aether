import QtQuick
import qs.dms.Common

DSlideout {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankSlideout", "DSlideout", "qs.Widgets")
}
