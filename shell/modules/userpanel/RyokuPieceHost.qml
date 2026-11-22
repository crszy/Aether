// Bar pieces on Ryoku's bar. Ryoku is its own Quickshell instance with its own bar styles, so it can't load Aether's
// pieces itself; while Ryoku draws the bar (Mix & match > Bar = Ryoku), Aether - running next to it - shows the pieces
// you picked (WinSettings.ryokuPieces: "user:<Name>", "noctalia:Tray", "dms:CpuMonitor" ...) in a small strip on top
// of it. Where: ryokuPiecesEdge ("top-left" | "top" | "top-right" | "bottom-left" | "bottom" | "bottom-right" | "left"
// | "right"), ryokuPiecesOffset (px along the edge from that end), ryokuPiecesInset (px in from the edge).
// Pick them in Aether Settings > Your pieces.
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.services

Scope {
    id: root

    readonly property bool active: WinSettings.mixBar === "ryoku" && (WinSettings.ryokuPieces ?? []).length > 0
    readonly property string edge: WinSettings.ryokuPiecesEdge || "top-right"
    readonly property bool vertical: edge === "left" || edge === "right"

    LazyLoader {
        active: root.active

        Variants {
            model: Quickshell.screens

            PanelWindow {
                id: win

                required property var modelData

                screen: modelData
                color: "transparent"
                exclusionMode: ExclusionMode.Ignore
                WlrLayershell.layer: WlrLayer.Overlay
                WlrLayershell.namespace: "aether-ryoku-pieces"

                anchors.top: root.edge.startsWith("top") || root.vertical
                anchors.bottom: root.edge.startsWith("bottom")
                anchors.left: root.edge.endsWith("left")
                anchors.right: root.edge.endsWith("right")
                margins.top: root.edge.startsWith("top") ? (WinSettings.ryokuPiecesInset ?? 0) : root.vertical ? (WinSettings.ryokuPiecesOffset ?? 0) : 0
                margins.bottom: root.edge.startsWith("bottom") ? (WinSettings.ryokuPiecesInset ?? 0) : 0
                margins.left: root.edge.endsWith("left") ? (root.vertical ? (WinSettings.ryokuPiecesInset ?? 0) : (WinSettings.ryokuPiecesOffset ?? 0)) : 0
                margins.right: root.edge.endsWith("right") ? (root.vertical ? (WinSettings.ryokuPiecesInset ?? 0) : (WinSettings.ryokuPiecesOffset ?? 0)) : 0

                implicitWidth: Math.max(1, strip.implicitWidth)
                implicitHeight: Math.max(1, strip.implicitHeight)

                Grid {
                    id: strip

                    anchors.centerIn: parent
                    flow: root.vertical ? Grid.TopToBottom : Grid.LeftToRight
                    columns: root.vertical ? 1 : Math.max(1, (WinSettings.ryokuPieces ?? []).length)
                    spacing: 8
                    verticalItemAlignment: Grid.AlignVCenter
                    horizontalItemAlignment: Grid.AlignHCenter

                    Repeater {
                        model: WinSettings.ryokuPieces ?? []

                        Loader {
                            required property string modelData

                            source: Qt.resolvedUrl("../bar/pieces/MixPiece.qml")
                            onLoaded: {
                                item.vertical = root.vertical;
                                item.screen = win.modelData;
                                item.pieceId = modelData;
                            }
                        }
                    }
                }
            }
        }
    }
}
