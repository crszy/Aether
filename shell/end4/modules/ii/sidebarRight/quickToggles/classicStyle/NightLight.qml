import QtQuick
import qs.end4.modules.common
import qs.end4.modules.common.widgets
import qs.end4.services
import Quickshell.Io

QuickToggleButton {
    id: nightLightButton
    toggled: Hyprsunset.temperatureActive
    buttonIcon: Config.options.light.night.automatic ? "night_sight_auto" : "bedtime"
    onClicked: {
        Hyprsunset.toggleTemperature()
    }

    altAction: () => {
        Config.options.light.night.automatic = !Config.options.light.night.automatic
    }

    Component.onCompleted: {
        Hyprsunset.fetchState()
    }
    
    StyledToolTip {
        text: Translation.tr("Night Light | Right-click to toggle Auto mode")
    }
}
