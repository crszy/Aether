import QtQuick
import qs.dms.DCommon.Common

Item {
    id: root

    property int hours: 0
    property int minutes: 0
    property int seconds: 0
    property bool showSeconds: true
    property bool showNumbers: false
    property bool numbersOutside: false
    property string dateText: ""
    property color color: Style.primary
    property color numberColor: color
    property color backgroundColor: "transparent"
    property real facePadding: Style.spacingM

    readonly property real hourAngle: (hours % 12 + minutes / 60) * 30
    readonly property real minuteAngle: (minutes + seconds / 60) * 6
    readonly property real numberRing: showNumbers && numbersOutside ? Style.fontSizeSmall + Style.spacingXS : 0
    readonly property real dialSize: Math.min(width, height) - numberRing * 2
    readonly property real clockSize: dialSize - facePadding * 2
    readonly property real scaleFactor: clockSize / 200
    readonly property real centerX: width / 2
    readonly property real centerY: height / 2
    readonly property real faceRadius: Math.max(0, clockSize / 2 - 12)
    readonly property real handWidth: Math.max(8, 12 * scaleFactor)
    readonly property color dimColor: Style.withAlpha(color, 0.65)
    readonly property string datePosition: {
        const hRad = hourAngle * Math.PI / 180;
        const mRad = minutes * 6 * Math.PI / 180;
        const topWeight = Math.max(0, Math.cos(hRad)) + Math.max(0, Math.cos(mRad));
        const bottomWeight = Math.max(0, -Math.cos(hRad)) + Math.max(0, -Math.cos(mRad));
        const rightWeight = Math.max(0, Math.sin(hRad)) + Math.max(0, Math.sin(mRad));
        const leftWeight = Math.max(0, -Math.sin(hRad)) + Math.max(0, -Math.sin(mRad));
        const minWeight = Math.min(topWeight, bottomWeight, leftWeight, rightWeight);
        if (minWeight === bottomWeight)
            return "bottom";
        if (minWeight === topWeight)
            return "top";
        if (minWeight === rightWeight)
            return "right";
        return "left";
    }

    implicitWidth: Style.clockFaceSize
    implicitHeight: Style.clockFaceSize

    DOrganicBlob {
        anchors.centerIn: parent
        width: root.dialSize
        height: width
        fillColor: root.backgroundColor
        visible: root.backgroundColor.a > 0
        lobes: 12
        rotationDeg: -90
        lobeAmount: 0.075
        hillPower: 0.92
        roundness: 0.22
        paddingFrac: 0.02
        segments: 144
    }

    Repeater {
        model: root.showNumbers ? 12 : 0

        StyledText {
            required property int index
            readonly property real angle: (index + 1) * 30 * Math.PI / 180
            readonly property real numRadius: root.numbersOutside ? (root.dialSize + root.numberRing) / 2 : root.faceRadius + 10

            x: root.centerX + numRadius * Math.sin(angle) - width / 2
            y: root.centerY - numRadius * Math.cos(angle) - height / 2
            text: index + 1
            font.pixelSize: Style.fontSizeSmall
            font.weight: Style.fontWeightMedium
            color: root.numberColor
        }
    }

    Rectangle {
        id: hourHand
        readonly property real mainLength: root.faceRadius * 0.55

        x: root.centerX - width / 2
        y: root.centerY - mainLength
        width: root.handWidth
        height: mainLength + root.handWidth * 0.5
        radius: width / 2
        color: root.color
        antialiasing: true

        transform: Rotation {
            origin.x: hourHand.width / 2
            origin.y: hourHand.mainLength
            angle: root.hourAngle
        }
    }

    Rectangle {
        id: minuteHand
        readonly property real mainLength: root.faceRadius * 0.75

        x: root.centerX - width / 2
        y: root.centerY - mainLength
        width: root.handWidth
        height: mainLength + root.handWidth * 0.5
        radius: width / 2
        color: root.dimColor
        antialiasing: true

        transform: Rotation {
            origin.x: minuteHand.width / 2
            origin.y: minuteHand.mainLength
            angle: root.minuteAngle
        }
    }

    Rectangle {
        id: secondDot
        visible: root.showSeconds

        readonly property real angle: root.seconds * 6 * Math.PI / 180
        readonly property real orbitRadius: root.faceRadius * 0.92

        x: root.centerX + orbitRadius * Math.sin(angle) - width / 2
        y: root.centerY - orbitRadius * Math.cos(angle) - height / 2
        width: Math.max(10, root.clockSize * 0.07)
        height: width
        radius: width / 2
        color: root.color

        Behavior on x {
            NumberAnimation {
                duration: Style.shortDuration
                easing.type: Style.standardEasing
            }
        }
        Behavior on y {
            NumberAnimation {
                duration: Style.shortDuration
                easing.type: Style.standardEasing
            }
        }
    }

    StyledText {
        visible: root.dateText !== ""

        x: {
            switch (root.datePosition) {
            case "left":
                return root.centerX - root.faceRadius * 0.5 - width / 2;
            case "right":
                return root.centerX + root.faceRadius * 0.5 - width / 2;
            }
            return root.centerX - width / 2;
        }
        y: {
            switch (root.datePosition) {
            case "top":
                return root.centerY - root.faceRadius * 0.5 - height / 2;
            case "bottom":
                return root.centerY + root.faceRadius * 0.5 - height / 2;
            }
            return root.centerY - height / 2;
        }
        text: root.dateText
        font.pixelSize: Style.fontSizeSmall
        color: root.color

        Behavior on x {
            NumberAnimation {
                duration: Style.mediumDuration
                easing.type: Style.emphasizedEasing
            }
        }
        Behavior on y {
            NumberAnimation {
                duration: Style.mediumDuration
                easing.type: Style.emphasizedEasing
            }
        }
    }
}
