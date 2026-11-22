pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io

Scope {
    id: root

    property string source: "auto"
    property string dmsPath: ""
    property string family: ""
    property string monoFamily: ""
    property string displayFamily: ""
    property int weight: Font.Normal
    property real scale: 1.0

    property var dms: null
    readonly property bool dmsAvailable: dms !== null
    readonly property bool followingDms: source === "auto" && dmsAvailable

    readonly property string fontFamily: {
        if (followingDms)
            return dms.fontFamily || Fonts.sans;
        if (source === "system")
            return Fonts.system;
        return family || Fonts.sans;
    }
    readonly property string monoFontFamily: {
        if (followingDms)
            return dms.monoFontFamily || Fonts.mono;
        if (source === "system")
            return Fonts.systemMono;
        return monoFamily || Fonts.mono;
    }
    readonly property string displayFontFamily: {
        if (followingDms)
            return dms.displayFontFamily || Fonts.display;
        if (source === "system")
            return Fonts.display;
        return displayFamily || Fonts.display;
    }
    readonly property int fontWeight: followingDms ? (dms.fontWeight ?? Font.Normal) : weight
    readonly property real fontScale: followingDms ? (dms.fontScale || 1.0) : scale

    FileView {
        id: dmsView
        path: root.source === "auto" ? root.dmsPath : ""
        blockLoading: false
        watchChanges: true
        printErrors: false

        onLoaded: {
            try {
                root.dms = JSON.parse(dmsView.text()) || null;
            } catch (e) {
                root.dms = null;
            }
        }
        onFileChanged: dmsView.reload()
        onLoadFailed: root.dms = null
    }
}
