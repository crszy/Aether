import QtQuick
import qs.dms.Common
import qs.dms.Services
import qs.dms.DCommon.Widgets
import qs.dms.Modules.Settings.Widgets

SettingsFabBar {
    id: root

    property bool blocked: false

    shown: SessionData.greeterSyncPending && GreeterService.binaryExists

    DFab {
        text: I18n.tr("Revert")
        iconName: "undo"
        colorRole: "secondaryContainer"
        enabled: !GreeterService.syncing
        onClicked: SettingsData.revertGreeterSyncPending()
    }

    DFab {
        text: GreeterService.syncing ? I18n.tr("Syncing...", "greeter settings status while sync is running") : I18n.tr("Apply changes")
        iconName: "check"
        colorRole: "primary"
        busy: GreeterService.syncing
        enabled: !GreeterService.syncing && !root.blocked
        onClicked: GreeterService.sync()
    }
}
