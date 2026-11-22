import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash as New

New.WallpaperTab {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "WallpaperTab", "qs.Modules.DankDash", "qs.Modules.DDash")
}
