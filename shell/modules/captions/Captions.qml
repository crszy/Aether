pragma ComponentBehavior: Bound

// Title bars drawn by the shell for other apps (Settings > Mix & match > App title bars: DankMaterialShell's,
// Discord-style, KDE Plasma). Windows' title bar is removed from those apps (WindowManager.captionTargets lists them;
// never apps that draw their own, like Zen, Chrome, Discord, VS Code) and this header takes its place as the app's
// title bar (Win.CaptionSkin keeps it there and does what a title bar does). If the shell stops, Windows puts its own back.
//
// The shell window is the header plus a strip as tall as the window's corner radius that overlaps the top of the app:
// there it fills the two little gaps Windows' rounded corners leave under the header, in the app's own colour, so the
// header and the app are one rounded window. The app's outline is set to the header's colour, so no line between them.
//
// A style is one file in styles/ named <Style>Caption.qml with `required property var skin`, built on CaptionArea
// (drag / double-click / menu / resize), CaptionButton, CaptionIcon and CaptionTone (colours that blend with the app).
import QtQuick
import QtQuick.Shapes
import Quickshell
import Quickshell.Windows as Win
import qs.services

Scope {
    id: root

    readonly property string style: WinSettings.titleBars
    readonly property url styleFile: Qt.resolvedUrl(`styles/${style.charAt(0).toUpperCase() + style.slice(1)}Caption.qml`)

    Variants {
        model: Win.WindowManager.captionTargets

        FloatingWindow {
            id: win

            required property string modelData

            title: "Aether title bar"
            color: "transparent"
            visible: true // always up; the header itself hides while the app is minimised / on another desktop
            implicitWidth: 400
            implicitHeight: 32
            // only the header and the corner pieces take the mouse; hidden: nothing does
            mask: skin.shown ? shape : noInput

            Region {
                id: noInput
            }

            Region {
                id: shape

                item: header

                Region {
                    x: 0
                    y: skin.headerHeight
                    width: skin.cornerRadius
                    height: skin.cornerRadius
                }

                Region {
                    x: win.width - skin.cornerRadius
                    y: skin.headerHeight
                    width: skin.cornerRadius
                    height: skin.cornerRadius
                }
            }

            Win.CaptionSkin {
                id: skin

                window: content.Window.window
                target: win.modelData
                // the app's outline in the header's colour: header and app read as one window
                borderColor: header.item?.color ?? "transparent"
            }

            Item {
                id: content

                anchors.fill: parent
                opacity: skin.shown ? 1 : 0
                enabled: skin.shown

                Loader {
                    id: header

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: skin.headerHeight
                    Component.onCompleted: setSource(root.styleFile, { skin: skin })

                    Connections {
                        target: root
                        function onStyleFileChanged() {
                            header.setSource(root.styleFile, { skin: skin });
                        }
                    }
                }

                // the gaps Windows' rounded top corners leave under the header (square minus the quarter circle)
                Repeater {
                    model: skin.cornerRadius > 0 ? [false, true] : []

                    Shape {
                        id: wedge

                        required property bool modelData
                        readonly property real r: skin.cornerRadius

                        x: modelData ? win.width - r : 0
                        y: skin.headerHeight
                        width: r
                        height: r
                        preferredRendererType: Shape.CurveRenderer
                        transform: Scale {
                            origin.x: wedge.r / 2
                            xScale: wedge.modelData ? -1 : 1
                        }

                        ShapePath {
                            fillColor: skin.appColor.valid ? skin.appColor : (header.item?.color ?? "transparent")
                            strokeColor: "transparent"
                            startX: 0
                            startY: 0

                            PathLine {
                                x: wedge.r
                                y: 0
                            }
                            PathArc {
                                x: 0
                                y: wedge.r
                                radiusX: wedge.r
                                radiusY: wedge.r
                                direction: PathArc.Counterclockwise
                            }
                            PathLine {
                                x: 0
                                y: 0
                            }
                        }
                    }
                }
            }
        }
    }
}
