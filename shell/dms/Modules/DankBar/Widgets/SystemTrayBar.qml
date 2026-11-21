import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.SystemTrayBar {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "SystemTrayBar", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
