import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.TrayItemIcon {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "TrayItemIcon", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
