import QtQuick
import qs.end4.services as E4Win_
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.common.widgets

QuickToggleModel {
    name: Translation.tr("Virtual Keyboard")
    toggled: GlobalStates.oskOpen
    icon: toggled ? "keyboard_hide" : "keyboard"
    
    mainAction: () => {
        E4Win_.WinActions.toggleOsk()
    }

    tooltipText: Translation.tr("On-screen keyboard")
}
