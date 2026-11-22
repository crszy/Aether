import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash as New

New.OverviewTab {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "OverviewTab", "qs.Modules.DankDash", "qs.Modules.DDash")
}
