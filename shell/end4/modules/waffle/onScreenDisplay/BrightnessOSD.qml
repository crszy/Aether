import QtQuick
import Quickshell
import Quickshell.Hyprland
import qs.end4.services
import qs.end4.modules.waffle.looks

OSDValue {
    id: root
    property var focusedScreen: Quickshell.screens.find(s => s.name === Hyprland.focusedMonitor?.name)
    property var brightnessMonitor: Brightness.getMonitorForScreen(focusedScreen)
    iconName: "weather-sunny"
    value: brightnessMonitor?.brightness ?? 0
    showNumber: false

    Connections {
        target: Brightness
        function onBrightnessChanged() {
            root.timer.restart();
        }
    }
}
