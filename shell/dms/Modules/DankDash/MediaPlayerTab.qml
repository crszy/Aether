import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash as New

New.MediaPlayerTab {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "MediaPlayerTab", "qs.Modules.DankDash", "qs.Modules.DDash")
}
