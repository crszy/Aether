import QtQuick
import Quickshell
import qs.end4
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.common.widgets

QuickToggleModel {
    name: Translation.tr("Keep awake")

    toggled: Idle.inhibit
    icon: "coffee"
    mainAction: () => {
        Idle.toggleInhibit()
    }
    tooltipText: Translation.tr("Keep system awake")
}
