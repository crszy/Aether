// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

// Falling white stars behind the whole Settings window: a few shooting stars with soft tails and tiny twinkling dots.
Item {
    id: sf
    property int count: 14
    property bool running: visible
    clip: true

    // tiny twinkling dots
    Repeater {
        model: 46
        Rectangle {
            readonly property real sz: Math.random() < 0.2 ? 2.4 : 1.5
            x: Math.random() * sf.width
            y: Math.random() * sf.height
            width: sz; height: sz; radius: sz / 2
            color: "#ffffff"
            opacity: 0.15
            SequentialAnimation on opacity {
                running: sf.running
                loops: Animation.Infinite
                PauseAnimation { duration: Math.random() * 3000 }
                NumberAnimation { to: 0.15 + Math.random() * 0.5; duration: 1400 + Math.random() * 1800; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.08; duration: 1400 + Math.random() * 1800; easing.type: Easing.InOutSine }
            }
        }
    }

    // shooting stars: fall down and a little to the left, then start again somewhere else
    Repeater {
        model: sf.count
        delegate: Item {
            id: st
            property real k: 0
            property real sx: Math.random() * (sf.width + 300)
            property real len: 60 + Math.random() * 150
            property real dur: 2400 + Math.random() * 3600
            property real size: 1.4 + Math.random() * 1.6
            readonly property real dx: -0.35
            function again() { sx = Math.random() * (sf.width + 300); len = 60 + Math.random() * 150; dur = 2400 + Math.random() * 3600 }
            x: sx + dx * k * (sf.height + 160)
            y: -60 + k * (sf.height + 160)
            opacity: (k < 0.12 ? k / 0.12 : (k > 0.75 ? Math.max(0, (1 - k) / 0.25) : 1)) * 0.85
            // tail (behind the head, pointing up and to the right)
            Rectangle {
                x: -width / 2
                y: -height
                width: st.size
                height: st.len
                radius: width / 2
                transformOrigin: Item.Bottom
                rotation: 19
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0) }
                    GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0.75) }
                }
            }
            // head with a little glow
            Rectangle { x: -st.size * 2; y: -st.size * 2; width: st.size * 4; height: width; radius: width / 2; color: Qt.rgba(1, 1, 1, 0.12) }
            Rectangle { x: -st.size; y: -st.size; width: st.size * 2; height: width; radius: width / 2; color: "#ffffff" }
            SequentialAnimation {
                running: sf.running
                loops: Animation.Infinite
                PauseAnimation { duration: 200 + Math.random() * 3200 }
                NumberAnimation { target: st; property: "k"; from: 0; to: 1; duration: st.dur; easing.type: Easing.InQuad }
                ScriptAction { script: st.again() }
            }
        }
    }
}
