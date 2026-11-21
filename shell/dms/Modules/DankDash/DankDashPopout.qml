import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash as New

New.DDashPopout {
    id: shim
    Component.onCompleted: Deprecation.type(shim, "DankDashPopout", "DDashPopout", "qs.Modules.DDash")
}
