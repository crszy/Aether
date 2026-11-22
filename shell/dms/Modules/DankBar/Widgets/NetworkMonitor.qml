import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.NetworkMonitor {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "NetworkMonitor", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
