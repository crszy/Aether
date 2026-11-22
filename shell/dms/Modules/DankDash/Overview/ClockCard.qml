import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash.Overview as New

New.ClockCard {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "ClockCard", "qs.Modules.DankDash.Overview", "qs.Modules.DDash.Overview")
}
