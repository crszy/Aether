pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

Item {
    id: root

    property string iconName: ""
    property string thumbnail: ""
    property string path: ""
    property string mime: ""
    property bool isDir: false
    property bool isSymlink: false
    property bool symlinkBroken: false
    property bool untrusted: false
    property bool unreadable: false
    property bool hidden: false
    property bool cut: false
    property bool selected: false
    property int iconSize: Style.iconSize
    property color tint: Style.surfaceText

    readonly property real emblemSize: Math.max(FileBrowserMetrics.emblemMinSize, iconSize * FileBrowserMetrics.emblemRatio)
    readonly property var look: root.isDir && Host.appearance ? Host.appearance.lookup(root.path) : null

    implicitWidth: iconSize
    implicitHeight: iconSize
    opacity: cut ? Style.pendingOpacity : hidden ? FileBrowserMetrics.hiddenOpacity : 1

    FileIcon {
        anchors.fill: parent
        iconName: root.iconName
        iconOverride: root.look?.icon ?? ""
        thumbnail: root.thumbnail !== "" ? root.thumbnail : root.mime === "image/svg+xml" ? root.path : ""
        size: root.iconSize
        color: root.isDir && !root.selected ? Style.primary : root.tint
        tint: root.look?.color || "transparent"
    }

    component Emblem: Rectangle {
        property alias iconName: badge.name
        property alias iconColor: badge.color

        width: root.emblemSize
        height: root.emblemSize
        radius: Style.fullRadius(width, height)
        color: Style.surfaceContainerLowest

        DIcon {
            id: badge

            anchors.centerIn: parent
            size: parent.width - Style.spacingXS
        }
    }

    Loader {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        active: root.isSymlink
        sourceComponent: Emblem {
            iconName: root.symlinkBroken ? "link_off" : "link"
            iconColor: root.symlinkBroken ? Style.error : Style.surfaceVariantText
        }
    }

    Loader {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        active: root.untrusted || root.unreadable
        sourceComponent: Emblem {
            iconName: root.unreadable ? "lock" : "warning"
            iconColor: Style.error
        }
    }

    Loader {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        active: (root.look?.emblem ?? "") !== "" && !root.untrusted && !root.unreadable
        sourceComponent: DSVGIcon {
            source: Paths.resolveIconPath(root.look?.emblem ?? "")
            size: root.emblemSize
        }
    }
}
