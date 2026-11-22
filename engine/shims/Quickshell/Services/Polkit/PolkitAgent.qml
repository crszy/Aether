import QtQml

QtObject {
    property string path: "/org/quickshell/Polkit"
    readonly property bool isRegistered: false
    readonly property bool isActive: false
    readonly property QtObject flow: null

    signal authenticationRequestStarted()
}
