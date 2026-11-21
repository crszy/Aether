import QtQml
import Quickshell.Windows

QtObject {
    id: root

    property string appid: "quickshell"
    property string name: ""
    property string description: ""
    property string triggerDescription: ""

    signal pressed()
    signal released()

    Component.onCompleted: Hotkeys.registerShortcut(root)
    Component.onDestruction: Hotkeys.unregisterShortcut(root)
}
