import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.RamMonitor {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "RamMonitor", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
