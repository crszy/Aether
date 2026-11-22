import QtQuick
import qs.dms.Common
import qs.dms.Modules.ControlCenter
import qs.dms.Services
import qs.dms.DCommon.Widgets

DActionButton {
    property string settingsTab: ""

    buttonSize: CcMetrics.headerActionSize
    iconSize: CcMetrics.headerActionIconSize
    iconName: "settings"
    iconColor: Theme.surfaceText
    Accessible.name: I18n.tr("Settings")
    onClicked: {
        PopoutService.closeControlCenter();
        PopoutService.openSettingsWithTab(settingsTab);
    }
}
