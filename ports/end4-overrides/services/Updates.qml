pragma Singleton
import QtQuick
import Quickshell

// Windows: end-4 counts pending Arch package updates (checkupdates). Windows Update is Windows' own business,
// so this reports none.
Singleton {
    readonly property bool available: false
    readonly property bool checking: false
    readonly property int count: 0
    readonly property bool updateAdvised: false
    readonly property bool updateStronglyAdvised: false
    function load() {}
    function refresh() {}
}
