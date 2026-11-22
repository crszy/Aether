import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.DWLLayout {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "DWLLayout", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
