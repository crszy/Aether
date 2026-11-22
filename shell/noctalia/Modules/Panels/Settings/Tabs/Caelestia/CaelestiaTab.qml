import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import qs.noctalia.Commons
import qs.noctalia.Widgets
import qs.services as Cae

// Caelestia for Windows: the "Caelestia" tab in Noctalia's settings - the same Mix & match choices as in
// Caelestia's Settings (WinSettings.mix*), the Settings style, and a button that opens Caelestia's Settings.
// So whichever settings window is open, you can always switch parts and get back.
ColumnLayout {
  id: root
  spacing: Style.marginL
  Layout.fillWidth: true

  readonly property var bothModel: [
    { "key": "caelestia", "name": "Caelestia" },
    { "key": "noctalia", "name": "Noctalia" },
    { "key": "dms", "name": "DankMaterialShell" },
    { "key": "end4", "name": "end-4" },
    { "key": "off", "name": "Off" }
  ]
  readonly property var allModel: [
    { "key": "caelestia", "name": "Caelestia" },
    { "key": "noctalia", "name": "Noctalia" },
    { "key": "zenith", "name": "Zenith" },
    { "key": "dms", "name": "DankMaterialShell" },
    { "key": "end4", "name": "end-4" },
    { "key": "off", "name": "Off" }
  ]
  readonly property var noctaliaModel: [
    { "key": "noctalia", "name": "Noctalia" },
    { "key": "dms", "name": "DankMaterialShell" },
    { "key": "end4", "name": "end-4" },
    { "key": "off", "name": "Off" }
  ]

  NButton {
    Layout.fillWidth: true
    icon: "settings"
    text: "Open Caelestia's settings"
    onClicked: Cae.WinSettings.ipc("nexus", "openCaelestia", [])
  }

  NComboBox {
    Layout.fillWidth: true
    label: "Settings style"
    description: "Which settings window the settings button opens"
    model: [
      { "key": "aether", "name": "Aether (all shells)" },
      { "key": "rice", "name": "Aether classic" },
      { "key": "caelestia", "name": "Caelestia" },
      { "key": "noctalia", "name": "Noctalia" }
    ]
    currentKey: Cae.WinSettings.settingsStyle
    onSelected: key => Cae.WinSettings.settingsStyle = key
  }

  NDivider {
    Layout.fillWidth: true
  }

  NLabel {
    label: "Mix & match"
    description: "Which shell draws each part. Noctalia stays loaded while any part uses it."
  }

  Repeater {
    model: [
      ["Bar", "The bar along the screen edge", "mixBar", "all"],
      ["Notifications", "Notification pop-ups", "mixNotifs", "all"],
      ["On-screen display", "Volume / brightness pop-up", "mixOsd", "all"],
      ["Launcher", "What the launcher key opens", "mixLauncher", "all"],
      ["Lock screen", "What locking shows", "mixLock", "all"],
      ["Wallpaper & desktop", "Wallpaper, desktop clock", "mixWallpaper", "both"],
      ["Dock", "A dock of apps (Noctalia or DMS)", "mixDock", "noctalia"],
      ["Desktop widgets", "Widgets on the desktop (Noctalia or DMS)", "mixDesktopWidgets", "noctalia"]
    ]

    NComboBox {
      required property var modelData

      Layout.fillWidth: true
      label: modelData[0]
      description: modelData[1]
      model: modelData[3] === "all" ? root.allModel : modelData[3] === "both" ? root.bothModel : root.noctaliaModel
      currentKey: Cae.WinSettings[modelData[2]]
      onSelected: key => Cae.WinSettings[modelData[2]] = key
    }
  }
}
