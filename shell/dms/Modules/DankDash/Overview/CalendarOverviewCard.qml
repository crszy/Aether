import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash.Overview as New

New.CalendarOverviewCard {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "CalendarOverviewCard", "qs.Modules.DankDash.Overview", "qs.Modules.DDash.Overview")
}
