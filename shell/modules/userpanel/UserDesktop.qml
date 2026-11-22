pragma ComponentBehavior: Bound

// Your own desktop widgets: every ~/.config/caelestia/user/desktop/<Name>.qml sits on the desktop (behind your
// apps), each in its own small window so the rest of the desktop stays clickable.
// Where: ~/.config/caelestia/user/desktop.json, e.g.
//   { "Clock": { "screen": "all", "corner": "top-right", "x": 60, "y": 60 } }
//   screen  "main" (default) | "all" | a monitor name
//   corner  "top-left" (default) | "top-right" | "bottom-left" | "bottom-right" | "center"
//   x, y    distance from that corner in px (default 60)
// Rename a file to start with "_" to switch it off.
import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.components.containers
import qs.services

Scope {
    id: scope

    Variants {
        // one entry per widget per monitor it is on
        model: {
            const out = [];
            for (const n of UserExt.desktopNames) {
                const c = UserExt.desktopConfig?.[n] ?? {};
                const where = String(c.screen ?? "main");
                const screens = where === "all" ? Quickshell.screens : where === "main" || where === "primary" ? Quickshell.screens.slice(0, 1) : Quickshell.screens.filter(s => s.name === where);
                for (const s of screens)
                    out.push({ name: n, screen: s, corner: String(c.corner ?? "top-left"), x: Number(c.x ?? 60), y: Number(c.y ?? 60) });
            }
            return out;
        }

        StyledWindow {
            id: win

            required property var modelData

            screen: modelData.screen
            name: "aether-user-desktop"
            exclusionMode: ExclusionMode.Ignore
            color: "transparent"
            WlrLayershell.layer: WlrLayer.Bottom
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

            readonly property string corner: modelData.corner
            readonly property bool centre: corner === "center"

            anchors.top: !centre && corner.includes("top")
            anchors.bottom: !centre && corner.includes("bottom")
            anchors.left: !centre && corner.includes("left")
            anchors.right: !centre && corner.includes("right")
            margins.top: corner.includes("top") ? modelData.y : 0
            margins.bottom: corner.includes("bottom") ? modelData.y : 0
            margins.left: corner.includes("left") ? modelData.x : 0
            margins.right: corner.includes("right") ? modelData.x : 0

            implicitWidth: Math.max(1, piece.implicitWidth)
            implicitHeight: Math.max(1, piece.implicitHeight)

            UserPiece {
                id: piece

                anchors.fill: parent
                kind: "desktop"
                name: win.modelData.name
                context: ({ screen: win.modelData.screen })
            }
        }
    }
}
