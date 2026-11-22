import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.ColorPicker {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "ColorPicker", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
