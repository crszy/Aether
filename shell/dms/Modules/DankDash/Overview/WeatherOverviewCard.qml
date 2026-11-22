import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash.Overview as New

New.WeatherOverviewCard {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "WeatherOverviewCard", "qs.Modules.DankDash.Overview", "qs.Modules.DDash.Overview")
}
