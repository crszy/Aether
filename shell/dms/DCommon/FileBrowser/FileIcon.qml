pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

Item {
    id: root

    property string iconName: ""
    property string iconOverride: ""
    property string glyph: ""
    property string thumbnail: ""
    property int size: Style.iconSize
    property color color: Style.surfaceText
    property color tint: "transparent"
    property bool asynchronous: true

    readonly property string themeIconPath: glyph !== "" ? "" : (Paths.resolveIconPath(iconOverride) || Paths.resolveIconPath(iconName))
    readonly property real devicePixelRatio: Window.window?.devicePixelRatio ?? Screen.devicePixelRatio
    readonly property int rasterSize: Style.iconRasterSize(size * devicePixelRatio)

    implicitWidth: size
    implicitHeight: size

    function glyphFor(name) {
        switch (true) {
        case name.startsWith("user-trash"):
            return "delete";
        case name.startsWith("folder"):
        case name.startsWith("user-"):
        case name === "inode-directory":
            return "folder";
        case name === "inode-symlink":
            return "link";
        case name === "emblem-unreadable":
        case name === "lock":
            return "lock";
        case name.startsWith("drive-removable"):
            return "usb";
        case name.startsWith("drive-"):
            return "hard_drive";
        case name === "media-optical":
            return "album";
        case name === "multimedia-player":
        case name === "phone":
            return "smartphone";
        case name === "camera-photo":
            return "photo_camera";
        case name === "application-pdf":
            return "picture_as_pdf";
        case name.startsWith("image"):
            return "image";
        case name.startsWith("video"):
            return "movie";
        case name.startsWith("audio"):
            return "music_note";
        case name.startsWith("text"):
            return "description";
        case name.startsWith("application-x-executable"):
        case name.startsWith("application-x-shellscript"):
            return "terminal";
        case name.startsWith("application-zip"):
        case name.startsWith("application-gzip"):
        case name.startsWith("application-zstd"):
        case name.startsWith("application-x-7z"):
        case name.startsWith("application-x-tar"):
            return "folder_zip";
        default:
            return "draft";
        }
    }

    Loader {
        anchors.fill: parent
        active: root.thumbnail !== ""
        sourceComponent: Image {
            source: FilePaths.toFileUrl(root.thumbnail)
            sourceSize.width: root.rasterSize
            sourceSize.height: root.rasterSize
            fillMode: Image.PreserveAspectFit
            asynchronous: root.asynchronous
            cache: true
            smooth: true
        }
    }

    Loader {
        anchors.fill: parent
        active: root.thumbnail === "" && root.themeIconPath !== ""
        sourceComponent: DSVGIcon {
            source: root.themeIconPath
            size: root.size
            colorOverride: root.tint
            asynchronous: root.asynchronous
        }
    }

    Loader {
        anchors.centerIn: parent
        active: root.thumbnail === "" && root.themeIconPath === ""
        sourceComponent: DIcon {
            name: root.glyph !== "" ? root.glyph : root.glyphFor(root.iconName)
            size: root.size
            color: root.tint.a > 0 ? root.tint : root.color
        }
    }
}
