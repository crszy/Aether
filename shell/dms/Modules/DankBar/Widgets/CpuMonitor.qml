import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.CpuMonitor {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "CpuMonitor", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
