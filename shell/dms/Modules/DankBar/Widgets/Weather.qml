import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.Weather {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "Weather", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
