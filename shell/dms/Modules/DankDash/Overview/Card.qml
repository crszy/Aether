import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash.Overview as New

New.Card {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "Card", "qs.Modules.DankDash.Overview", "qs.Modules.DDash.Overview")
}
