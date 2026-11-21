// Mix & match bar piece: your own widget from ~/.config/caelestia/user/widgets/<Name>.qml ("user:<Name>").
// Works in every bar that takes pieces (Caelestia's bar.json entries, barPieces in end-4 / Noctalia / DMS / Zenith).
// Saving the file reloads it; a file with a mistake shows its error instead.
// The widget may declare `property bool vertical` and `property ShellScreen screen` - both are filled in.
import QtQuick
import Quickshell
import qs.modules.userpanel

UserPiece {
    id: root

    property ShellScreen screen
    property string widgetId: ""
    property bool vertical: true

    kind: "widgets"
    name: widgetId
    context: ({ vertical: root.vertical, screen: root.screen })
}
