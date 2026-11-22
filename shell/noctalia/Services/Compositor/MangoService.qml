import QtQuick

// Caelestia for Windows: never used (Noctalia talks to Caelestia's Hyprland emulation there), and its QML
// module does not exist on Windows - a stand-in so CompositorService still compiles.
Item {
  signal workspaceChanged
  signal activeWindowChanged
  signal windowListChanged
  signal displayScalesChanged
  function initialize() {}
}
