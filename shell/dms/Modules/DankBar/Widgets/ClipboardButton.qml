import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Widgets as New

New.ClipboardButton {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "ClipboardButton", "qs.Modules.DankBar.Widgets", "qs.Modules.DBar.Widgets")
}
