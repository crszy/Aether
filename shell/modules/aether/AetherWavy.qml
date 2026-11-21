// Aether style: the splash's M3 Expressive wavy progress line (splash.cpp WavyBar), for any value 0..1 -
// volume, brightness, loading, checking a password. The shown value glides to `value` like the splash's progress
// (exponential approach), the wave flows while it runs, and it straightens as it nears 100%.
//   value       0..1                       colour      the line (default: primary)
//   thickness   line width (4.5 = splash)  flowing     false: a still wave
//   indeterminate  true: a loading line that keeps filling and draining
import QtQuick
import qs.services
import "AetherDraw.js" as Draw

Item {
    id: root

    property real value: 0
    property color colour: AetherStyle.primary
    property real thickness: 4.5
    property bool flowing: true
    property bool indeterminate: false

    property real shown: 0           // starts empty and glides in, like the splash
    property real t: 0

    implicitWidth: 260
    implicitHeight: Math.max(16, thickness * 3.6)

    FrameAnimation {
        running: root.visible && root.opacity > 0
        onTriggered: {
            const dt = Math.min(0.1, frameTime);
            if (root.flowing)
                root.t += dt;
            const target = root.indeterminate ? 0.5 + 0.45 * Math.sin(root.t * 1.3) : Math.max(0, Math.min(1, root.value));
            root.shown += (target - root.shown) * (1 - Math.exp(-dt * 6));
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
            const flat = root.indeterminate ? 0 : AetherStyle.clamp01((root.shown - 0.96) / 0.04);
            Draw.wavyBar(AetherStyle, g, root.thickness / 2 + 1, width - root.thickness / 2 - 1.5, height / 2, root.shown, root.t, flat, root.colour, 1, root.thickness);
        }
    }
}
