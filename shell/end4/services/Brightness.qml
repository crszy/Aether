pragma Singleton
pragma ComponentBehavior: Bound

import qs.end4.modules.common
import Quickshell
import Quickshell.Io
import QtQuick
import qs.services as Cae

// Windows version of end-4's Brightness (upstream: brightnessctl / ddcutil). Each monitor is a thin wrapper
// over Caelestia's Brightness service, which does laptop panels (WMI) and external monitors (DDC/CI) on
// Windows. end-4's "anti-flashbang" (grim + magick screenshots) is not ported.
Singleton {
    id: root
    signal brightnessChanged()

    readonly property list<BrightnessMonitor> monitors: Quickshell.screens.map(screen => monitorComp.createObject(root, {
        screen
    }))

    function getMonitorForScreen(screen: ShellScreen): var {
        return monitors.find(m => m.screen === screen);
    }

    function focused(): var {
        const s = Cae.Hypr?.focusedMonitor?.name;
        return monitors.find(m => m.screen.name === s) ?? monitors[0];
    }

    function increaseBrightness(): void {
        const monitor = focused();
        if (monitor)
            monitor.setBrightness(monitor.brightness + 0.05);
    }

    function decreaseBrightness(): void {
        const monitor = focused();
        if (monitor)
            monitor.setBrightness(monitor.brightness - 0.05);
    }

    component BrightnessMonitor: QtObject {
        id: monitor

        required property ShellScreen screen
        readonly property var cae: Cae.Brightness.getMonitorForScreen(screen)
        readonly property bool isDdc: !!cae?.isDdc
        property string busNum: ""
        property int rawMaxBrightness: 100
        readonly property real brightness: cae?.brightness ?? 1
        property real brightnessMultiplier: 1.0
        readonly property real multipliedBrightness: Math.max(0, Math.min(1, brightness))
        readonly property bool ready: !!cae?.available
        property bool animateChanges: !isDdc

        onBrightnessChanged: root.brightnessChanged()

        function initialize() {}
        function syncBrightness() {}

        function setBrightness(value: real): void {
            cae?.setBrightness(Math.max(0, Math.min(1, value)));
        }

        function setBrightnessMultiplier(value: real): void {
            brightnessMultiplier = value;
        }
    }

    Component {
        id: monitorComp
        BrightnessMonitor {}
    }

    IpcHandler {
        target: "end4:brightness"

        function increment() {
            root.increaseBrightness();
        }

        function decrement() {
            root.decreaseBrightness();
        }
    }
}
