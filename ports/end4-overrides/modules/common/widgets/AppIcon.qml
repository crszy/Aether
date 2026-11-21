import QtQuick
import Quickshell.Widgets

// Windows: end-4's AppIcon is KDE's Kirigami.Icon (not on Windows); Quickshell's IconImage shows the same icon
// sources (Quickshell.iconPath(...) / file URLs).
IconImage {
    id: root

    property real implicitSize: 26
    property bool roundToIconSize: false
    property bool animated: false
    property color color: "transparent"
    property bool isMask: false
    implicitWidth: implicitSize
    implicitHeight: implicitSize
    asynchronous: true
}
