import QtQuick
import qs.dms.Common

DAnimatedAlbumArt {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankAnimatedAlbumArt", "DAnimatedAlbumArt", "qs.Widgets")
}
