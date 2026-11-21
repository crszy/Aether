import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.IdleInhibitor {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "IdleInhibitor", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
