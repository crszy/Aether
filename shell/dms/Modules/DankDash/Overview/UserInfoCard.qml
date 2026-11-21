import QtQuick
import qs.dms.Common
import qs.dms.Modules.DDash.Overview as New

New.UserInfoCard {
    id: shim
    Component.onCompleted: Deprecation.module(shim, "UserInfoCard", "qs.Modules.DankDash.Overview", "qs.Modules.DDash.Overview")
}
