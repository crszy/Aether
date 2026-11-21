import QtQuick
import qs.end4.services as E4Win_
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.common.widgets

QuickToggleModel {
    name: Translation.tr("EasyEffects")

    available: EasyEffects.available
    toggled: EasyEffects.active
    icon: "graphic_eq"

    Component.onCompleted: {
        EasyEffects.fetchActiveState()
    }

    mainAction: () => {
        EasyEffects.toggle()
    }

    altAction: () => {
        E4Win_.WinActions.open("ms-settings:sound")
        GlobalStates.sidebarRightOpen = false
    }

    tooltipText: Translation.tr("EasyEffects | Right-click to configure")
}
