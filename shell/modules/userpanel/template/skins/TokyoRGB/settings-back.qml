// TokyoRGB - behind Aether Settings: Tokyo at night. Neon glows drifting through the dark, a synthwave grid
// rolling towards you, faint 東京 on the side and CRT scanlines. Everything stops while the window is closed.
// Change anything: colours, speeds, the text. Save and it reloads.
import QtQuick
import QtQuick.Effects
import qs.services

Item {
    id: root

    property var surface              // the Aether Settings window (handed in by Aether)
    property bool active: true        // false while the window is hidden: animations pause

    readonly property color pink: "#ff2bd6"
    readonly property color cyan: "#00e5ff"
    readonly property color violet: "#7b2bff"

    // the night sky
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#05030d" }
            GradientStop { position: 0.55; color: "#120a2e" }
            GradientStop { position: 1.0; color: "#2a0838" }
        }
        opacity: 0.94
    }

    // neon glows, blurred together
    Item {
        id: glows

        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            blurEnabled: true
            blur: 1
            blurMax: 64
        }

        Repeater {
            model: [
                { c: root.pink, x: 0.15, y: 0.25, s: 0.42, d: 19000 },
                { c: root.cyan, x: 0.78, y: 0.2, s: 0.36, d: 23000 },
                { c: root.violet, x: 0.55, y: 0.7, s: 0.5, d: 27000 }
            ]

            Rectangle {
                id: blob

                required property var modelData
                property real t: 0

                width: root.width * modelData.s
                height: width
                radius: width / 2
                color: modelData.c
                opacity: 0.32
                x: root.width * modelData.x - width / 2 + Math.sin(t * Math.PI * 2) * root.width * 0.08
                y: root.height * modelData.y - height / 2 + Math.cos(t * Math.PI * 2) * root.height * 0.06

                NumberAnimation on t {
                    from: 0
                    to: 1
                    duration: blob.modelData.d
                    loops: Animation.Infinite
                    running: root.active
                }
            }
        }
    }

    // 東京, huge and faint, down the right side
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 28
        anchors.verticalCenter: parent.verticalCenter
        text: "東\n京"
        color: root.pink
        opacity: 0.07
        font.pixelSize: root.height * 0.3
        font.weight: Font.Black
        lineHeight: 0.9
    }

    // the synthwave floor: perspective lines, and horizontal lines rolling towards you
    Item {
        id: floor

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.36
        clip: true
        opacity: 0.55

        property real roll: 0

        NumberAnimation on roll {
            from: 0
            to: 1
            duration: 2400
            loops: Animation.Infinite
            running: root.active
        }

        Canvas {
            id: rays

            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const g = getContext("2d");
                g.reset();
                g.strokeStyle = "rgba(255,43,214,0.55)";
                g.lineWidth = 1;
                const cx = width / 2;
                for (let i = -14; i <= 14; i++) {
                    g.beginPath();
                    g.moveTo(cx + i * 6, 0);
                    g.lineTo(cx + i * width / 9, height);
                    g.stroke();
                }
            }
        }

        Repeater {
            model: 9

            Rectangle {
                required property int index
                // lines bunch up near the horizon (top) and spread out towards you
                readonly property real p: (index + floor.roll) / 9
                width: floor.width
                height: 1 + p * 1.5
                y: floor.height * p * p
                color: root.cyan
                opacity: 0.25 + p * 0.6
            }
        }

        // the horizon glow
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 2
            color: root.pink
            opacity: 0.9
        }
    }

    // CRT scanlines
    Column {
        anchors.fill: parent
        opacity: 0.06

        Repeater {
            model: Math.ceil(root.height / 3)

            Rectangle {
                width: root.width
                height: 3
                color: index % 2 ? "transparent" : "black"
            }
        }
    }
}
