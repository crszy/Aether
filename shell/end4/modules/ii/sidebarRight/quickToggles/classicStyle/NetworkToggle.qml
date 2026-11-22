import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.widgets
import qs.end4.modules.common.functions
import qs.end4.modules.ii.sidebarRight.quickToggles
import qs.end4
import QtQuick
import qs.end4.services as E4Win_
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland

QuickToggleButton {
    toggled: Network.wifiStatus !== "disabled"
    buttonIcon: Network.materialSymbol
    onClicked: Network.toggleWifi()
    altAction: () => {
        E4Win_.WinActions.open(Network.ethernet ? Config.options.apps.networkEthernet : Config.options.apps.network)
        GlobalStates.sidebarRightOpen = false
    }
    StyledToolTip {
        text: Translation.tr("%1 | Right-click to configure").arg(Network.networkName)
    }
}
