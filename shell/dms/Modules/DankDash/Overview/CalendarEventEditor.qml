import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash.Overview as New

New.CalendarEventEditor {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "CalendarEventEditor", "qs.Modules.DankDash.Overview", "qs.Modules.DDash.Overview")
}
