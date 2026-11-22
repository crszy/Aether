pragma Singleton

import QtQuick
import Quickshell
import qs.noctalia.Commons
import qs.services as Cae

// Windows version of Noctalia's BrightnessService (upstream: /sys/class/backlight, brightnessctl, ddcutil,
// asdbctl). Same API; each monitor wraps Caelestia's Brightness monitor for that screen, which does laptop
// panels (WMI) and external monitors (DDC/CI) on Windows.
Singleton {
  id: root

  property list<var> ddcMonitors: []
  readonly property list<Monitor> monitors: variants.instances
  property bool appleDisplayPresent: false
  property list<var> availableBacklightDevices: []

  function getMonitorForScreen(screen: ShellScreen): var {
    return monitors.find(m => m.modelData === screen);
  }

  signal monitorBrightnessChanged(var monitor, real newBrightness)

  function getAvailableMethods(): list<string> {
    return monitors.some(m => m.brightnessControlAvailable) ? ["internal"] : [];
  }

  function increaseBrightness(): void {
    monitors.forEach(m => m.increaseBrightness());
  }

  function decreaseBrightness(): void {
    monitors.forEach(m => m.decreaseBrightness());
  }

  function setBrightness(value: real): void {
    monitors.forEach(m => m.setBrightnessDebounced(value));
  }

  function getDetectedDisplays(): list<var> {
    return [];
  }

  // backlight device mapping is a Linux setting: nothing to map on Windows
  function normalizeBacklightDevicePath(devicePath): string {
    return String(devicePath ?? "");
  }
  function getBacklightDeviceName(devicePath): string {
    return "";
  }
  function getMappedBacklightDevice(outputName): string {
    return "";
  }
  function setMappedBacklightDevice(outputName, devicePath): void {}
  function scanBacklightDevices(): void {}

  Variants {
    id: variants
    model: Quickshell.screens
    Monitor {}
  }

  component Monitor: QtObject {
    id: monitor

    required property ShellScreen modelData
    readonly property var cae: Cae.Brightness.monitors.find(m => m.modelData === modelData) ?? null
    readonly property bool isDdc: !!cae?.isDdc
    readonly property string busNum: ""
    readonly property bool isAppleDisplay: false
    readonly property string method: isDdc ? "ddcutil" : "internal"
    readonly property bool brightnessControlAvailable: cae !== null && (cae.available ?? true)

    readonly property real brightness: cae?.brightness ?? 0
    property real lastBrightness: 0
    property real queuedBrightness: NaN
    property string backlightDevice: ""
    readonly property real stepSize: Settings.data.brightness.brightnessStep / 100.0

    signal brightnessUpdated(real newBrightness)

    onBrightnessChanged: {
      lastBrightness = brightness;
      brightnessUpdated(brightness);
      root.monitorBrightnessChanged(monitor, brightness);
    }

    readonly property Timer timer: Timer {
      interval: monitor.isDdc ? 250 : 33
      onTriggered: {
        if (!isNaN(monitor.queuedBrightness)) {
          monitor.setBrightness(monitor.queuedBrightness);
          monitor.queuedBrightness = NaN;
        }
      }
    }

    function setBrightnessDebounced(value: real): void {
      monitor.queuedBrightness = value;
      timer.start();
    }

    function increaseBrightness(): void {
      const value = !isNaN(monitor.queuedBrightness) ? monitor.queuedBrightness : monitor.brightness;
      setBrightnessDebounced(Math.min(1, value + stepSize));
    }

    function decreaseBrightness(): void {
      const value = !isNaN(monitor.queuedBrightness) ? monitor.queuedBrightness : monitor.brightness;
      setBrightnessDebounced(Math.max(0, value - stepSize));
    }

    function setBrightness(value: real): void {
      if (cae)
        cae.setBrightness(Math.max(0, Math.min(1, value)));
    }

    function initBrightness(): void {}
    function refreshBrightnessFromSystem(): void {}
  }
}
