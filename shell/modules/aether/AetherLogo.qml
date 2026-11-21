// Aether style: the Aether mark from the splash - the "A" drifting in, the orbit ring passing behind and in front,
// its moving primary arc, the star turning a quarter every 1.6 s (a clover on a lucky launch).
//   size    height in px of the mark (the splash's is ~135)     close   0..1 closes the arc into a full ring
//   restart()  plays the entrance again
import QtQuick
import qs.services
import "AetherDraw.js" as Draw

Item {
    id: root

    property real size: 135
    property real close: 0
    property color arcColour: AetherStyle.primary
    property real t: 0

    // logo space: the mark spans ~1100 x 730 around (628, 598); the splash draws it at scale 0.193
    readonly property real s: size / 730

    implicitWidth: 1110 * s
    implicitHeight: size

    function restart(): void {
        root.t = 0;
    }

    FrameAnimation {
        running: root.visible && root.opacity > 0
        onTriggered: {
            root.t += Math.min(0.1, frameTime);
            canvas.requestPaint();
        }
    }

    Canvas {
        id: canvas

        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const g = getContext("2d");
            g.reset();
            g.clearRect(0, 0, width, height);
            Draw.logo(AetherStyle, g, root.t, root.close, width / 2, height / 2 - 8 * root.s, root.s, root.arcColour);
        }
    }
}
