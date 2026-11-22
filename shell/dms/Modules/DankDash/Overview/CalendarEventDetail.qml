import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash.Overview as New

New.CalendarEventDetail {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "CalendarEventDetail", "qs.Modules.DankDash.Overview", "qs.Modules.DDash.Overview")
}
