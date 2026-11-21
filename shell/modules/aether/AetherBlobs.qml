// Aether style: the splash's two slow morphing tonal blobs (top-left primary container, bottom-right tertiary),
// laid out like the splash card at any size. clipRadius > 0: clipped to a rounded rectangle (inside a card).
// Repaints at ~30 fps - they move slowly.
import QtQuick
import qs.services
import "AetherDraw.js" as Draw

Item {
    id: root

    property real clipRadius: 0
    // the rounded rectangle to clip to (default: this whole item)
    property real clipX: 0
    property real clipY: 0
    property real clipW: width
    property real clipH: height
    property real strength: 1          // 1 = the splash's alpha
    property real t: 0

    FrameAnimation {
        property real acc: 0

        running: root.visible && root.opacity > 0
        onTriggered: {
            root.t += Math.min(0.1, frameTime);
            acc += frameTime;
            if (acc >= 1 / 30) {
                acc = 0;
                canvas.requestPaint();
            }
        }
    }

    Canvas {
        id: canvas

        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const g = getContext("2d");
            const S = AetherStyle;
            g.reset();
            g.clearRect(0, 0, width, height);
            if (root.clipRadius > 0) {
                Draw.rr(g, root.clipX, root.clipY, root.clipW, root.clipH, root.clipRadius);
                g.clip();
            }
            const k = Math.max(width / 640, height / 400);
            Draw.blob(g, 40 * k, 30 * k, 260 * k, root.t, 0, S.css(S.primaryContainer, 46 / 255 * root.strength));
            Draw.blob(g, width - 30 * k, height + 10 * k, 230 * k, root.t, 2, S.css(S.tertiary, 13 / 255 * root.strength));
        }
    }
}
