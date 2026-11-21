pragma ComponentBehavior: Bound

// Shows the panel from ~/.config/caelestia/user/panels/<name>.qml (UserExt.panel) over the focused monitor.
// Esc or a click outside closes it. A file with a mistake shows the error instead (line, column, message).
// The panel is your own QML: it sizes itself (implicitWidth/Height) and is centred.
// It may declare `property string panelName` and `signal close()` - both are wired up if present.
import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.components
import qs.components.containers
import qs.services

Scope {
    id: scope

    LazyLoader {
        active: UserExt.panel !== ""

        StyledWindow {
            id: win

            screen: Screens.screens.find(s => Hypr.monitorFor(s) === Hypr.focusedMonitor) ?? Screens.screens[0]
            name: "aether-user-panel"
            exclusionMode: ExclusionMode.Ignore
            color: "transparent"
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive

            anchors.top: true
            anchors.bottom: true
            anchors.left: true
            anchors.right: true

            // dim backdrop: a click outside the panel closes it
            Rectangle {
                anchors.fill: parent
                // a panel may set its own dim: `readonly property real backdrop: 0.1`
                color: Qt.alpha(AetherStyle.palette.m3scrim, piece.item && typeof piece.item.backdrop === "number" ? piece.item.backdrop : 0.35)
                opacity: 0
                Component.onCompleted: opacity = 1

                Behavior on opacity {
                    Anim {}
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: UserExt.closePanel()
                }
            }

            Item {
                anchors.fill: parent
                focus: true
                Keys.onEscapePressed: UserExt.closePanel()
            }

            UserPiece {
                id: piece

                anchors.centerIn: parent
                width: implicitWidth
                height: implicitHeight
                kind: "panels"
                name: UserExt.panel
                context: ({ panelName: UserExt.panel })

                onItemChanged: {
                    if (item && typeof item.close === "function" && item.close.connect)
                        item.close.connect(() => UserExt.closePanel());
                    // Aether motion: the panel springs open - unless it animates itself (`ownMotion: true`)
                    if (item && item.ownMotion === true) {
                        piece.animOn = false;
                        piece.scale = 1;
                        piece.opacity = 1;
                        return;
                    }
                    piece.animOn = false;
                    piece.scale = 0.82;
                    piece.opacity = 0;
                    piece.animOn = true;
                    piece.scale = 1;
                    piece.opacity = 1;
                }

                property bool animOn: false

                Behavior on scale {
                    enabled: piece.animOn

                    SpringAnimation {
                        spring: AetherStyle.springExpressive.s
                        damping: AetherStyle.springExpressive.d
                        epsilon: 0.002
                    }
                }

                Behavior on opacity {
                    enabled: piece.animOn

                    NumberAnimation {
                        duration: 160
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: AetherStyle.emphasizedDecel
                    }
                }

                // clicks on the panel never reach the backdrop
                MouseArea {
                    anchors.fill: parent
                    z: -1
                }
            }
        }
    }
}
