import QtQuick
import qs.dms.Common
import qs.dms.DCommon.Widgets as DCommon

DCommon.DColorSwatch {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankColorSwatch", "DColorSwatch", "qs.DCommon.Widgets")
}
