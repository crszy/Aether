// Aether's lock screen, made the way real rices do it: your wallpaper is the hero (lightly blurred by Aether -
// Settings > Lock > blur), colours come from your colour scheme (so they follow the wallpaper), a big thin clock,
// and small cards along the bottom edge. No logo, no floating card. Motion is quick and clean: things slide in
// ~250 ms with a decelerate curve; only the password shapes bounce.
// Every character you type is a Material shape that pops in; a wrong password shakes the pill.
// Use it: Aether Settings > Lock screen > AetherLock, or windows.json "mixLock": "user:AetherLock".
// Typing goes to the password check by itself - this file only shows it:
//   pam.buffer   what was typed      pam.lockMessage   a message from the check      lock.unlocking   unlocking now
import QtQuick
import QtQuick.Layouts
import M3Shapes
import qs.components
import qs.services

Item {
    id: root

    property var pam
    property var lock
    property var screen

    readonly property var pal: Colours.palette
    // the shapes a typed character can be, in order (they repeat), and their colours (all from your scheme)
    readonly property var shapes: [MaterialShape.Cookie4Sided, MaterialShape.Sunny, MaterialShape.Clover4Leaf, MaterialShape.Heart,
        MaterialShape.Gem, MaterialShape.Flower, MaterialShape.SoftBurst, MaterialShape.Pentagon, MaterialShape.Puffy,
        MaterialShape.Cookie6Sided, MaterialShape.Ghostish, MaterialShape.Triangle, MaterialShape.Arch, MaterialShape.Bun]
    readonly property var shapeColours: [pal.m3primary, pal.m3tertiary, pal.m3secondary]

    readonly property int typed: pam?.buffer?.length ?? 0
    readonly property bool unlocking: !!lock?.unlocking
    property bool wrong: false
    property bool checking: false
    property bool shown: false          // drives the enter / leave slide
    Component.onCompleted: shown = true

    // M3 emphasized curves: decelerate to enter, accelerate to leave
    readonly property var decel: [0.05, 0.7, 0.1, 1, 1, 1]
    readonly property var accel: [0.3, 0, 0.8, 0.15, 1, 1]
    readonly property bool on: shown && !unlocking

    onTypedChanged: {
        // Enter hands the text to the check and empties it: keep the shapes while it checks
        if (typed === 0 && chars.count > 0 && root.pam?.passwd?.active) {
            root.checking = true;
            return;
        }
        root.checking = false;
        while (chars.count < typed)
            chars.append({ shapeIdx: chars.count % shapes.length, colourIdx: chars.count % shapeColours.length });
        while (chars.count > typed)
            chars.remove(chars.count - 1);
        if (typed > 0) {
            root.wrong = false;
            boing.restart();
        }
    }

    onUnlockingChanged: if (unlocking) root.checking = false

    Connections {
        target: root.pam
        ignoreUnknownSignals: true

        function onFlashMsg(): void {
            root.checking = false;
            root.wrong = true;
            shake.restart();
            clearShapes.restart();
        }
    }

    Timer {
        id: clearShapes

        interval: 900
        onTriggered: chars.clear()
    }

    ListModel {
        id: chars
    }

    // only a soft shade along the bottom (for the text), the wallpaper stays bright
    Rectangle {
        anchors.fill: parent
        opacity: root.on ? 1 : 0
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.alpha(root.pal.m3scrim, 0.05) }
            GradientStop { position: 0.55; color: Qt.alpha(root.pal.m3scrim, 0.12) }
            GradientStop { position: 1.0; color: Qt.alpha(root.pal.m3scrim, 0.55) }
        }

        Behavior on opacity {
            NumberAnimation { duration: 250; easing.type: Easing.BezierSpline; easing.bezierCurve: root.on ? root.decel : root.accel }
        }
    }

    // ---- the clock: big, thin, upper third ----
    ColumnLayout {
        id: clock

        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.17 + (root.on ? 0 : -32)
        opacity: root.on ? 1 : 0
        spacing: -8

        Behavior on y {
            NumberAnimation { duration: 280; easing.type: Easing.BezierSpline; easing.bezierCurve: root.on ? root.decel : root.accel }
        }
        Behavior on opacity {
            NumberAnimation { duration: 220 }
        }

        StyledText {
            Layout.alignment: Qt.AlignHCenter
            text: Qt.formatTime(Time.date, "hh:mm")
            color: root.pal.m3onSurface
            font.pointSize: Math.max(64, root.height / 9)
            font.weight: 200
            style: Text.Raised
            styleColor: Qt.alpha(root.pal.m3shadow, 0.25)
        }

        StyledText {
            Layout.alignment: Qt.AlignHCenter
            text: Qt.formatDate(Time.date, "dddd, d MMMM")
            color: root.pal.m3onSurface
            opacity: 0.85
            font.pointSize: 15
            font.weight: 400
        }
    }

    // ---- the password pill: one Material shape per character ----
    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.62 + (root.on ? 0 : 40)
        opacity: root.on ? 1 : 0
        spacing: 10

        Behavior on y {
            NumberAnimation { duration: 300; easing.type: Easing.BezierSpline; easing.bezierCurve: root.on ? root.decel : root.accel }
        }
        Behavior on opacity {
            NumberAnimation { duration: 220 }
        }

        Rectangle {
            id: pill

            Layout.alignment: Qt.AlignHCenter
            implicitWidth: Math.max(280, shapesRow.contentWidth + 52)
            implicitHeight: 56
            radius: height / 2
            color: root.wrong ? Qt.alpha(root.pal.m3errorContainer, 0.92) : Qt.alpha(root.pal.m3surfaceContainer, 0.82)
            border.width: 1
            border.color: Qt.alpha(root.wrong ? root.pal.m3error : root.pal.m3outlineVariant, 0.6)
            transform: Translate {
                id: shakeX
            }

            Behavior on implicitWidth {
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }
            Behavior on color {
                ColorAnimation { duration: 160 }
            }

            SequentialAnimation {
                id: boing

                NumberAnimation { target: pill; property: "scale"; to: 1.04; duration: 60; easing.type: Easing.OutQuad }
                NumberAnimation { target: pill; property: "scale"; to: 1; duration: 380; easing.type: Easing.OutElastic; easing.amplitude: 1.1; easing.period: 0.4 }
            }

            SequentialAnimation {
                id: shake

                NumberAnimation { target: shakeX; property: "x"; to: -12; duration: 50 }
                NumberAnimation { target: shakeX; property: "x"; to: 10; duration: 70 }
                NumberAnimation { target: shakeX; property: "x"; to: -6; duration: 70 }
                NumberAnimation { target: shakeX; property: "x"; to: 0; duration: 90; easing.type: Easing.OutCubic }
            }

            // while it checks: the shapes breathe together
            Item {
                id: pulse

                property real s: 1

                SequentialAnimation on s {
                    running: root.checking
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.75; duration: 260; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.08; duration: 300; easing.type: Easing.OutBack }
                }
            }

            RowLayout {
                anchors.centerIn: parent
                visible: root.typed === 0 && chars.count === 0
                spacing: 8

                MaterialIcon {
                    text: "lock"
                    color: root.pal.m3onSurfaceVariant
                    fill: 1
                }
                StyledText {
                    text: "Enter password"
                    color: root.pal.m3onSurfaceVariant
                    font.pointSize: 11
                }
            }

            ListView {
                id: shapesRow

                anchors.centerIn: parent
                width: Math.min(contentWidth, 460)
                height: 34
                orientation: ListView.Horizontal
                spacing: 5
                interactive: false
                model: chars

                delegate: Item {
                    id: cell

                    required property int index
                    required property int shapeIdx
                    required property int colourIdx

                    width: 28
                    height: 34

                    MaterialShape {
                        anchors.centerIn: parent
                        implicitSize: 24
                        shape: root.wrong ? MaterialShape.Burst : root.shapes[cell.shapeIdx]
                        color: root.wrong ? root.pal.m3error : root.shapeColours[cell.colourIdx]
                        animationDuration: 300
                        scale: root.unlocking ? 0 : root.checking ? pulse.s : 1

                        Behavior on scale {
                            NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
                        }

                        NumberAnimation on rotation {
                            from: 0
                            to: cell.index % 2 ? -360 : 360
                            duration: 7000 + (cell.index % 5) * 900
                            loops: Animation.Infinite
                        }
                    }
                }

                add: Transition {
                    ParallelAnimation {
                        NumberAnimation { property: "scale"; from: 0; to: 1; duration: 480; easing.type: Easing.OutElastic; easing.amplitude: 1.2; easing.period: 0.4 }
                        NumberAnimation { property: "rotation"; from: -90; to: 0; duration: 420; easing.type: Easing.OutBack }
                    }
                }
                remove: Transition {
                    NumberAnimation { property: "scale"; to: 0; duration: 140; easing.type: Easing.InCubic }
                }
                displaced: Transition {
                    NumberAnimation { property: "x"; duration: 220; easing.type: Easing.OutCubic }
                }
            }
        }

        StyledText {
            Layout.alignment: Qt.AlignHCenter
            text: root.unlocking ? "" : root.checking ? "Checking…" : root.wrong ? "Wrong password" : root.pam?.lockMessage || ""
            color: root.wrong ? root.pal.m3error : root.pal.m3onSurface
            opacity: text ? 0.9 : 0
            font.pointSize: 10.5

            Behavior on opacity {
                NumberAnimation { duration: 150 }
            }
        }
    }

    // ---- small cards along the bottom edge: what's playing (left), battery / user (right) ----
    Rectangle {
        id: media

        readonly property var player: Players.active
        visible: !!player && !!player.trackTitle
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 32
        anchors.bottomMargin: root.on ? 32 : -height
        width: Math.min(380, root.width * 0.3)
        height: 76
        radius: 20
        color: Qt.alpha(root.pal.m3surfaceContainer, 0.82)
        border.width: 1
        border.color: Qt.alpha(root.pal.m3outlineVariant, 0.5)

        Behavior on anchors.bottomMargin {
            NumberAnimation { duration: 300; easing.type: Easing.BezierSpline; easing.bezierCurve: root.on ? root.decel : root.accel }
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                radius: 14
                color: root.pal.m3secondaryContainer
                clip: true

                Image {
                    anchors.fill: parent
                    source: media.player ? Players.getArtUrl(media.player) : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 112
                    sourceSize.height: 112
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                StyledText {
                    Layout.fillWidth: true
                    text: media.player?.trackTitle ?? ""
                    color: root.pal.m3onSurface
                    elide: Text.ElideRight
                    font.pointSize: 11
                    font.weight: 600
                }
                StyledText {
                    Layout.fillWidth: true
                    text: media.player?.trackArtist ?? ""
                    color: root.pal.m3onSurfaceVariant
                    elide: Text.ElideRight
                    font.pointSize: 10
                }
            }

            MaterialIcon {
                text: media.player?.isPlaying ? "pause" : "play_arrow"
                color: root.pal.m3primary
                fill: 1

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -8
                    cursorShape: Qt.PointingHandCursor
                    onClicked: media.player?.togglePlaying()
                }
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 32
        anchors.bottomMargin: root.on ? 32 : -height
        width: userRow.implicitWidth + 32
        height: 48
        radius: height / 2
        color: Qt.alpha(root.pal.m3surfaceContainer, 0.82)
        border.width: 1
        border.color: Qt.alpha(root.pal.m3outlineVariant, 0.5)

        Behavior on anchors.bottomMargin {
            NumberAnimation { duration: 340; easing.type: Easing.BezierSpline; easing.bezierCurve: root.on ? root.decel : root.accel }
        }

        RowLayout {
            id: userRow

            anchors.centerIn: parent
            spacing: 8

            MaterialIcon {
                text: "account_circle"
                color: root.pal.m3primary
                fill: 1
            }
            StyledText {
                text: WinSettings.displayName || "Locked"
                color: root.pal.m3onSurface
                font.pointSize: 10.5
            }
        }
    }
}
