import QtQuick
import qs.dms.DCommon.Common

Item {
    id: root

    property alias name: icon.text
    property alias size: icon.font.pixelSize
    property alias color: icon.color
    property bool filled: false
    property real fill: filled ? 1.0 : 0.0
    property int grade: Style.isLightMode ? 0 : -25
    property int weight: filled ? 500 : 400
    property bool smoothTransform: false

    property real _shownFill: fill
    property int _shownWeight: weight
    property bool _ready: false

    implicitWidth: Math.round(size)
    implicitHeight: Math.round(size)

    signal rotationCompleted

    onFillChanged: retarget()
    onWeightChanged: retarget()

    function retarget() {
        if (!_ready || !visible) {
            settle();
            return;
        }
        if (fill === _shownFill && weight === _shownWeight) {
            settle();
            return;
        }
        incoming.active = true;
        crossfade.restart();
    }

    function settle() {
        crossfade.stop();
        _shownFill = fill;
        _shownWeight = weight;
        shownLayer.opacity = 1;
        incoming.active = false;
    }

    Component.onCompleted: {
        _shownFill = fill;
        _shownWeight = weight;
        _ready = true;
    }

    Item {
        id: shownLayer

        anchors.fill: parent

        StyledText {
            id: icon

            anchors.fill: parent

            font.family: Fonts.icons
            font.pixelSize: Math.round(Style.fontSizeMedium)
            font.weight: root._shownWeight
            font.hintingPreference: Font.PreferNoHinting
            color: Style.surfaceText
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            renderType: root.smoothTransform ? Text.QtRendering : Text.NativeRendering

            font.variableAxes: {
                "FILL": root._shownFill,
                "GRAD": root.grade,
                "opsz": 24,
                "wght": root._shownWeight
            }
        }
    }

    Loader {
        id: incoming

        anchors.fill: parent
        active: false
        opacity: 0

        sourceComponent: StyledText {
            text: icon.text
            font.family: Fonts.icons
            font.pixelSize: icon.font.pixelSize
            font.weight: root.weight
            font.hintingPreference: Font.PreferNoHinting
            color: icon.color
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            renderType: icon.renderType

            font.variableAxes: {
                "FILL": root.fill,
                "GRAD": root.grade,
                "opsz": 24,
                "wght": root.weight
            }
        }
    }

    SequentialAnimation {
        id: crossfade

        NumberAnimation {
            target: incoming
            property: "opacity"
            from: 0
            to: 1
            duration: Style.shortDuration / 2
            easing.type: Style.standardEasing
        }
        NumberAnimation {
            target: shownLayer
            property: "opacity"
            from: 1
            to: 0
            duration: Style.shortDuration / 2
            easing.type: Style.standardEasing
        }

        onFinished: root.settle()
    }

    Timer {
        id: rotationTimer
        interval: 16
        repeat: false
        onTriggered: root.rotationCompleted()
    }

    onRotationChanged: {
        rotationTimer.restart();
    }
}
