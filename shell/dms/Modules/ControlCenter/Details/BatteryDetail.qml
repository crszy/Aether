import QtQuick
import qs.dms.Common
import qs.dms.Modules.DBar.Popouts
import qs.dms.Services

BatteryPopoutContent {
    readonly property string title: BatteryService.batteryAvailable ? I18n.tr("Battery") : I18n.tr("Power")

    active: true
    contentPadding: 0
}
