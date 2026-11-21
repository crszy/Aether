import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash.Overview as New

New.SystemMonitorCard {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "SystemMonitorCard", "qs.Modules.DankDash.Overview", "qs.Modules.DDash.Overview")
}
