// Aether (Windows): your wallpaper under a Linux shell that has none of its own (on Linux hyprpaper / swww draws it).
// Written by the AetherStore next to the shell's shell.qml; it shows the picture Aether last set
// (~/.local/state/caelestia/wallpaper/path.txt) and follows it when you change wallpaper in Aether.
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland

Scope {
    id: root

    property string path: ""

    FileView {
        path: (Quickshell.env("USERPROFILE") || "").split(String.fromCharCode(92)).join("/") + "/.local/state/caelestia/wallpaper/path.txt"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: root.path = text().trim().split(String.fromCharCode(92)).join("/")
    }

    Variants {
        model: Quickshell.screens

        PanelWindow {
            required property var modelData

            screen: modelData
            color: "black"
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.layer: WlrLayer.Background
            WlrLayershell.namespace: "aether-wallpaper"
            anchors {
                top: true
                bottom: true
                left: true
                right: true
            }

            Image {
                anchors.fill: parent
                source: root.path ? "file:///" + root.path : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: parent.width
                sourceSize.height: parent.height
            }
        }
    }
}
