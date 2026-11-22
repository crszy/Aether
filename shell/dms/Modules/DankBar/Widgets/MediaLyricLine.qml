import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.MediaLyricLine {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "MediaLyricLine", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
