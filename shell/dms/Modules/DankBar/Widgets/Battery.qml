import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.Battery {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "Battery", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
