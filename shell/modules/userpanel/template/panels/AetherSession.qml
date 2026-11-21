// AetherSession - the session menu as a dot that grows into a pill and stretches into a row of buttons (the way
// the rices on r/unixporn do it), no full-screen overlay. Colours come from your scheme / skin.
// Use it: windows.json "sessionMenu": "user:AetherSession" (the session key and power buttons open it), or
// a launcher command { "name": "session", "panel": "AetherSession" }.
// Keys: Left / Right to pick, Enter to do it, Esc to close. Log out, restart and shut down ask for a second press.
// Make it yours: change `actions` (any icon from fonts.google.com/icons, any command), sizes, timings.
import QtQuick
import QtQuick.Layouts
import Quickshell
import Caelestia.Config
import Caelestia.Services
import qs.components
import qs.services

Item {
    id: root

    signal close
    readonly property bool ownMotion: true     // this file animates itself; Aether's panel spring stays out
    readonly property real backdrop: 0.12      // how much the desktop dims behind it (0..1)

    readonly property var pal: AetherStyle.palette
    readonly property var actions: [
        { icon: "lock", label: "Lock", run: () => WinSettings.ipc("lock", "lock", []) },
        { icon: "bedtime", label: "Sleep", run: () => SessionManager.suspend() },
        { icon: "logout", label: "Log out", confirm: true, run: () => SessionManager.logout() },
        { icon: "restart_alt", label: "Restart", confirm: true, run: () => SessionManager.reboot() },
        { icon: "power_settings_new", label: "Shut down", confirm: true, danger: true, run: () => SessionManager.poweroff() }
    ]

    property int current: 0
    property int armed: -1                     // a button waiting for its second press
    property real phase: 0                     // 0 = a dot, 1 = the full row

    readonly property real btn: 58
    readonly property real gap: 10
    readonly property real fullW: actions.length * btn + (actions.length - 1) * gap + 28
    readonly property real fullH: btn + 28 + 26

    implicitWidth: fullW
    implicitHeight: fullH

    function ease(x: real): real {   // M3 emphasized decelerate
        x = Math.max(0, Math.min(1, x));
        return 1 - Math.pow(1 - x, 3.2);
    }

    function activate(i: int): void {
        const a = root.actions[i];
        if (!a)
            return;
        if (a.confirm && root.armed !== i) {
            root.armed = i;
            disarm.restart();
            return;
        }
        root.close();
        a.run();
    }

    Component.onCompleted: {
        grow.start();
        root.forceActiveFocus();
    }

    NumberAnimation {
        id: grow

        target: root
        property: "phase"
        from: 0
        to: 1
        duration: 620
    }

    Timer {
        id: disarm

        interval: 3000
        onTriggered: root.armed = -1
    }

    focus: true
    Keys.onLeftPressed: root.current = (root.current + root.actions.length - 1) % root.actions.length
    Keys.onRightPressed: root.current = (root.current + 1) % root.actions.length
    Keys.onReturnPressed: root.activate(root.current)
    Keys.onEnterPressed: root.activate(root.current)
    Keys.onEscapePressed: root.close()

    // the shape: a dot, then a pill (height first), then the full row (width)
    Rectangle {
        id: body

        anchors.centerIn: parent
        width: 14 + (root.fullW - 14) * root.ease((root.phase - 0.28) / 0.72)
        height: 14 + (root.fullH - 26 - 14) * root.ease(root.phase / 0.42)
        radius: Math.min(height / 2, 30)
        color: Qt.alpha(root.pal.m3surfaceContainer, 0.96)
        border.width: 1
        border.color: Qt.alpha(root.pal.m3outlineVariant, 0.7)
        anchors.verticalCenterOffset: -13

        RowLayout {
            anchors.centerIn: parent
            spacing: root.gap
            opacity: root.ease((root.phase - 0.7) / 0.3)

            Repeater {
                model: root.actions

                Rectangle {
                    id: b

                    required property var modelData
                    required property int index
                    readonly property bool sel: root.current === index
                    readonly property bool arm: root.armed === index

                    implicitWidth: root.btn
                    implicitHeight: root.btn
                    radius: sel ? 18 : root.btn / 2
                    color: arm ? root.pal.m3errorContainer : sel ? (modelData.danger ? root.pal.m3error : root.pal.m3primary) : Qt.alpha(root.pal.m3surfaceContainerHighest, 0.9)
                    scale: area.pressed ? 0.92 : 1

                    Behavior on radius {
                        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                    }
                    Behavior on color {
                        ColorAnimation { duration: 140 }
                    }
                    Behavior on scale {
                        NumberAnimation { duration: 110 }
                    }

                    MaterialIcon {
                        anchors.centerIn: parent
                        text: b.modelData.icon
                        fill: b.sel ? 1 : 0
                        color: b.arm ? root.pal.m3onErrorContainer : b.sel ? (b.modelData.danger ? root.pal.m3onError : root.pal.m3onPrimary) : root.pal.m3onSurface
                        fontStyle: Tokens.font.icon.large   // icon sizes: small, medium, large, extraLarge
                    }

                    MouseArea {
                        id: area

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: root.current = b.index
                        onClicked: root.activate(b.index)
                    }
                }
            }
        }
    }

    // what the picked button does, under the pill
    StyledText {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        text: root.armed >= 0 ? `Press again to ${root.actions[root.armed].label.toLowerCase()}` : root.actions[root.current].label
        color: root.armed >= 0 ? root.pal.m3error : root.pal.m3onSurface
        opacity: root.ease((root.phase - 0.75) / 0.25)
        font.pointSize: 10.5
        style: Text.Outline
        styleColor: Qt.alpha(root.pal.m3scrim, 0.35)
    }
}
