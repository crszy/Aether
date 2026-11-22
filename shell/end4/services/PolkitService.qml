pragma Singleton
import QtQuick
import Quickshell

// Windows: no polkit (UAC asks for itself). Same API as end-4's PolkitService, never active.
Singleton {
    property var agent: null
    readonly property bool active: false
    readonly property var flow: null
    property bool interactionAvailable: false
    property string cleanMessage: ""
    property string cleanPrompt: ""
    function cancel() {}
    function submit(string) {}
}
