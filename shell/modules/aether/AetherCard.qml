// Aether style: the splash's card, with its container transform. Give it a size (width/height or implicit) and
// put content inside. When it appears, a 64 px tonal pill springs open into the card (spatial spring), the corners
// settle to 28 px, the colour washes from primary container to surface, the star spins in the middle while it
// opens, and the content eases in from 94 %. dismiss() plays the splash's exit (scale to 95 %, fade), then closed().
//   blobs     the two morphing blobs inside (default true)      radius   the settled corner (28)
//   restart() plays the opening again
import QtQuick
import qs.services
import "AetherDraw.js" as Draw

Item {
    id: root

    default property alias content: inner.data
    property bool blobs: true
    property real radius: AetherStyle.cornerXL
    property color surface: AetherStyle.surface

    signal closed

    property real t: 0                 // seconds since it started opening
    property real te: -1               // seconds since dismiss() (-1 = not closing)
    readonly property bool settled: t > 1.6 && te < 0

    // opening (splash.cpp: lt = t - .15)
    readonly property real lt: t - 0.15
    readonly property real ow: Draw.lerp(64, width, AetherStyle.spatial(lt - 0.04))
    readonly property real oh: Draw.lerp(64, height, AetherStyle.spatial(lt))
    readonly property real orad: Draw.lerp(Math.min(ow, oh) / 2, radius, AetherStyle.emphDec(lt / 0.5))
    readonly property real ca: AetherStyle.effects(t - 0.3)
    readonly property real exitE: te < 0 ? 0 : AetherStyle.emphAcc(te / 0.28)

    function restart(): void {
        root.te = -1;
        root.t = 0;
    }

    function dismiss(): void {
        if (root.te < 0)
            root.te = 0;
    }

    scale: 1 - 0.05 * exitE
    opacity: 1 - exitE

    FrameAnimation {
        running: root.visible && (root.t < 1.6 || root.te >= 0)
        onTriggered: {
            const dt = Math.min(0.1, frameTime);
            root.t += dt;
            if (root.te >= 0) {
                root.te += dt;
                if (root.te > 0.3) {
                    root.te = -1;
                    root.closed();
                }
            }
        }
    }

    // the soft shadow: 8 growing layers under the card, as in the splash
    Repeater {
        model: 8

        Rectangle {
            required property int index

            readonly property real gr: index * 4
            readonly property real sh: AetherStyle.clamp01((root.ow - 64) / 300)

            x: (root.width - root.ow) / 2 - gr
            y: (root.height - root.oh) / 2 - gr + 8
            width: root.ow + 2 * gr
            height: root.oh + 2 * gr
            radius: root.orad + gr
            color: Qt.rgba(0, 0, 0, 5 * sh / 255)
        }
    }

    // the card itself
    Rectangle {
        id: card

        x: (root.width - root.ow) / 2
        y: (root.height - root.oh) / 2
        width: root.ow
        height: root.oh
        radius: root.orad
        color: AetherStyle.mix(AetherStyle.primaryContainer, root.surface, root.ca)
        border.width: 1
        border.color: Qt.rgba(AetherStyle.outlineVar.r, AetherStyle.outlineVar.g, AetherStyle.outlineVar.b, 150 / 255 * root.ca)
    }

    // the blobs inside, laid out on the full card and clipped to its current rounded shape
    AetherBlobs {
        anchors.fill: parent
        visible: root.blobs
        opacity: root.ca
        clipRadius: Math.max(0.5, root.orad)
        clipX: card.x
        clipY: card.y
        clipW: card.width
        clipH: card.height
    }

    // the content: eases in from 94 % once the pill has opened
    Item {
        id: inner

        anchors.fill: parent
        opacity: root.ca
        scale: Draw.lerp(0.94, 1, AetherStyle.emphDec((root.t - 0.3) / 0.6))
    }

    // the star in the middle while the pill is still opening
    Canvas {
        id: star

        anchors.centerIn: parent
        width: 40
        height: 40
        visible: root.ca < 0.99
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const g = getContext("2d");
            g.reset();
            g.clearRect(0, 0, width, height);
            Draw.star4(g, 20, 20, 15 * (1 - root.ca), root.t * 1.2, AetherStyle.css(AetherStyle.textOnPrimaryContainer, 1 - root.ca));
        }

        Connections {
            target: root

            function onTChanged(): void {
                if (star.visible)
                    star.requestPaint();
            }
        }
    }
}
