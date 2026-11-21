import QtQuick
import Quickshell
import "../../" as Shell // To access Theme
import ".."     // This imports the parent directory where iconfetcher.qml lives

Item {
    id: root
    
    property string appName: ""
    property string desktopEntry: ""
    property string iconName: ""

    // Call the Singleton directly using its QML file name type (IconsFetcher)
    readonly property string resolvedSource: {
        if (!root.appName && !root.desktopEntry && !root.iconName) return "";
        let src = IconsFetcher.getValidIcon(root.appName, root.desktopEntry, root.iconName);
        return src;
    }
    readonly property bool showLetter: resolvedSource === "" || resolvedSource === "image://icon/application-x-executable" || icon.status === Image.Error

    Image {
        id: icon
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        smooth: true
        asynchronous: true
        visible: !root.showLetter
        source: root.resolvedSource
        autoTransform: true
    }

    Rectangle {
        anchors.fill: parent
        visible: root.showLetter
        color: (Shell.Theme && Shell.Theme.surface0) ? Shell.Theme.surface0 : "#252525"
        radius: width / 4
        border.color: (Shell.Theme && Shell.Theme.surface1) ? Shell.Theme.surface1 : "#353535"
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: (root.appName && root.appName !== "") ? root.appName.charAt(0).toUpperCase() : "?"
            font.pixelSize: parent.height * 0.6
            font.bold: true
            color: (Shell.Theme && Shell.Theme.text) ? Shell.Theme.text : "#cba6f7"
        }
    }
}