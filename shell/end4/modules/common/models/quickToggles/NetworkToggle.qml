import QtQuick
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.common.widgets

QuickToggleModel {
    name: Translation.tr("Internet")
    statusText: Network.networkName
    tooltipText: Translation.tr("%1 | Right-click to configure").arg(Network.networkName)
    icon: Network.materialSymbol

    toggled: Network.wifiStatus !== "disabled"
    mainAction: () => Network.toggleWifi()
    hasMenu: true
}
