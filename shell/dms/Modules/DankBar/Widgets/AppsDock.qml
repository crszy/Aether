import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.AppsDock {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "AppsDock", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
