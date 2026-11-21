// AetherCoverflow - your wallpapers as a coverflow across the middle of the screen (the rices on r/unixporn do this):
// the picked one faces you, the rest turn away in 3D on both sides and glide as you move.
// Make it THE wallpaper picker: windows.json "wallpaperPicker": "user:AetherCoverflow" (">wallpaper" in any launcher,
// keys and IPC then open this), or open it from a command: { "name": "walls", "panel": "AetherCoverflow" }.
// Keys: Left / Right (or the mouse wheel) to move, Enter or a click on the middle card to set it, Esc to close.
// Make it yours: card size, how far they turn (`turn`), how many show (`visibleCards`), speeds.
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Widgets
import qs.components
import qs.services

Item {
    id: root

    signal close
    readonly property bool ownMotion: true       // this file animates itself
    readonly property real backdrop: 0.45        // how much the desktop dims behind it

    readonly property var pal: AetherStyle.palette
    readonly property real cardW: 230
    readonly property real cardH: 360
    readonly property real turn: 62              // degrees the side cards turn away
    readonly property int visibleCards: 13
    readonly property var walls: Wallpapers.list

    function norm(x: var): string {
        return String(x ?? "").split(String.fromCharCode(92)).join("/").toLowerCase();
    }

    function apply(i: int): void {
        const w = root.walls[i];
        if (!w)
            return;
        Wallpapers.setWallpaper(String(w.path));
        root.close();
    }

    implicitWidth: Screen.width
    implicitHeight: root.cardH * 1.35 + 90

    // open: the strip grows out of the middle, quick and clean
    opacity: 0
    scale: 0.92
    Component.onCompleted: {
        const i = root.walls.findIndex(w => root.norm(w.path) === root.norm(Wallpapers.actualCurrent));
        flow.currentIndex = Math.max(0, i);
        opacity = 1;
        scale = 1;
        flow.forceActiveFocus();
    }
    Behavior on opacity {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }
    Behavior on scale {
        NumberAnimation { duration: 280; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.05, 0.7, 0.1, 1, 1, 1] }
    }

    PathView {
        id: flow

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.cardH * 1.35
        model: root.walls
        pathItemCount: Math.min(root.visibleCards, root.walls.length)
        preferredHighlightBegin: 0.5
        preferredHighlightEnd: 0.5
        highlightRangeMode: PathView.StrictlyEnforceRange

        // the fan: as wide as the cards need (a few wallpapers stay close together), never wider than the screen
        readonly property real mid: width / 2
        readonly property real span: Math.min(width / 2 - 20, root.cardW * 0.95 + Math.max(1, pathItemCount / 2 - 1) * root.cardW * 0.38)
        highlightMoveDuration: 240
        snapMode: PathView.SnapToItem
        focus: true

        Keys.onLeftPressed: decrementCurrentIndex()
        Keys.onRightPressed: incrementCurrentIndex()
        Keys.onReturnPressed: root.apply(currentIndex)
        Keys.onEnterPressed: root.apply(currentIndex)
        Keys.onEscapePressed: root.close()

        // the cards bunch up on the sides and open up in the middle
        path: Path {
            startX: flow.mid - flow.span
            startY: flow.height / 2

            PathAttribute { name: "rot"; value: root.turn }
            PathAttribute { name: "scl"; value: 0.78 }
            PathAttribute { name: "depth"; value: 0 }
            PathAttribute { name: "dim"; value: 0.55 }
            PathLine { x: flow.mid - root.cardW * 0.95; y: flow.height / 2 }
            PathPercent { value: 0.42 }
            PathAttribute { name: "rot"; value: root.turn * 0.85 }
            PathAttribute { name: "scl"; value: 0.88 }
            PathAttribute { name: "depth"; value: 50 }
            PathAttribute { name: "dim"; value: 0.3 }
            PathLine { x: flow.mid; y: flow.height / 2 }
            PathPercent { value: 0.5 }
            PathAttribute { name: "rot"; value: 0 }
            PathAttribute { name: "scl"; value: 1.18 }
            PathAttribute { name: "depth"; value: 100 }
            PathAttribute { name: "dim"; value: 0 }
            PathLine { x: flow.mid + root.cardW * 0.95; y: flow.height / 2 }
            PathPercent { value: 0.58 }
            PathAttribute { name: "rot"; value: -root.turn * 0.85 }
            PathAttribute { name: "scl"; value: 0.88 }
            PathAttribute { name: "depth"; value: 50 }
            PathAttribute { name: "dim"; value: 0.3 }
            PathLine { x: flow.mid + flow.span; y: flow.height / 2 }
            PathPercent { value: 1 }
            PathAttribute { name: "rot"; value: -root.turn }
            PathAttribute { name: "scl"; value: 0.78 }
            PathAttribute { name: "depth"; value: 0 }
            PathAttribute { name: "dim"; value: 0.55 }
        }

        delegate: Item {
            id: card

            required property var modelData
            required property int index
            readonly property bool current: PathView.isCurrentItem

            width: root.cardW
            height: root.cardH
            z: PathView.depth
            scale: PathView.scl
            transform: Rotation {
                origin.x: card.width / 2
                origin.y: card.height / 2
                axis { x: 0; y: 1; z: 0 }
                angle: card.PathView.rot
            }

            ClippingRectangle {
                anchors.fill: parent
                radius: 20
                color: root.pal.m3surfaceContainer
                border.width: card.current ? 3 : 0
                border.color: root.pal.m3primary

                Image {
                    anchors.fill: parent
                    source: "file:///" + String(card.modelData.path).split(String.fromCharCode(92)).join("/")
                    sourceSize.height: root.cardH * 1.4
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                }

                // the side cards sit in shadow
                Rectangle {
                    anchors.fill: parent
                    color: "black"
                    opacity: card.PathView.dim
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: card.current ? root.apply(card.index) : (flow.currentIndex = card.index)
            }
        }

        WheelHandler {
            onWheel: e => e.angleDelta.y > 0 || e.angleDelta.x > 0 ? flow.decrementCurrentIndex() : flow.incrementCurrentIndex()
        }
    }

    // the picked one's name, and the keys
    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: flow.bottom
        spacing: 2

        StyledText {
            Layout.alignment: Qt.AlignHCenter
            text: {
                const w = root.walls[flow.currentIndex];
                return w ? String(w.path).split(/[\\/]/).pop() : "No wallpapers in your wallpaper folder";
            }
            color: root.pal.m3onSurface
            font.pointSize: 12
            font.weight: 600
            style: Text.Outline
            styleColor: Qt.alpha(root.pal.m3scrim, 0.4)
        }
        StyledText {
            Layout.alignment: Qt.AlignHCenter
            text: "← →  choose     Enter  set     Esc  close"
            color: root.pal.m3onSurfaceVariant
            font.pointSize: 9.5
            style: Text.Outline
            styleColor: Qt.alpha(root.pal.m3scrim, 0.4)
        }
    }
}
