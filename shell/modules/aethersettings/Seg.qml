// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

// Material 3 connected button group: the chosen one is filled and gets round corners all around.
Row {
    id: seg
    property var options: []
    property string current: ""
    signal chosen(string value)
    spacing: 2

    Repeater {
        model: seg.options
        delegate: Rectangle {
            readonly property bool on: seg.current === modelData.value
            readonly property bool first: index === 0
            readonly property bool last: index === seg.options.length - 1
            width: segText.implicitWidth + (on ? 40 : 28)
            height: 36
            radius: on ? 18 : 10
            color: on ? M3.primary : (segMouse.containsMouse ? M3.surfaceHighest : M3.surfaceHigh)
            Behavior on color { ColorAnimation { duration: 160 } }
            Behavior on radius { NumberAnimation { duration: M3.d(260); easing.type: M3.springy; easing.overshoot: 1.4 * Prefs.bounce } }
            Behavior on width { NumberAnimation { duration: M3.d(300); easing.type: M3.springy; easing.overshoot: 1.4 * Prefs.bounce } }
            Row {
                anchors.centerIn: parent
                spacing: 6
                Icon { visible: parent.parent.on; name: "check"; glyph: "\uf00c"; size: 14; color: M3.inkPrimary; anchors.verticalCenter: parent.verticalCenter }
                T {
                    id: segText
                    text: modelData.label
                    font.pixelSize: 13
                    font.weight: parent.parent.on ? Font.DemiBold : Font.Normal
                    color: parent.parent.on ? M3.inkPrimary : M3.ink
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            MouseArea {
                id: segMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: seg.chosen(modelData.value)
            }
        }
    }
}
