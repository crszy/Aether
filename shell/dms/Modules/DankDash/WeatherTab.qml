import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash as New

New.WeatherTab {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "WeatherTab", "qs.Modules.DankDash", "qs.Modules.DDash")
}
