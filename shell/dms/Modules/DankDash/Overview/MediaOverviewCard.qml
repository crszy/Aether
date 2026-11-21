import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash.Overview as New

New.MediaOverviewCard {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "MediaOverviewCard", "qs.Modules.DankDash.Overview", "qs.Modules.DDash.Overview")
}
