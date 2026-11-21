import QtQuick
import qs.dms.Common
import qs.dms.Modules.ControlCenter
import qs.dms.Modules.Settings.Widgets

SettingsRow {
    minHeight: CcMetrics.rowHeight
    paddingH: CcMetrics.rowPaddingH
    paddingV: CcMetrics.rowPaddingV
    rowColor: CcMetrics.rowColor
    iconColor: active ? Theme.accentOnSelectedContainer : Theme.surfaceText
}
