import QtQuick
import Quickshell.Wayland
import qs.dms.Common

FadeOverlayWindow {
    WlrLayershell.namespace: "dms:fade-to-dpms"
    fadeEnabled: SettingsData.fadeToDpmsEnabled
    gracePeriod: SettingsData.fadeToDpmsGracePeriod
    overlayColor: Theme.screenOffColor
}
