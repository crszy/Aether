import QtQuick
import QtQuick.Layouts
import qs.dms.DCommon.Common

FocusScope {
    id: root

    property int hour: 7
    property int minute: 0
    property bool is24Hour: false
    property string title: I18n.tr("Select time")
    property real faceSize: Math.min(Style.clockFaceSize, height - chromeHeight)

    signal accepted(int hour, int minute)
    signal rejected

    readonly property bool opened: _opened
    property bool _opened: false
    property int _hour: 7
    property int _minute: 0
    property bool _minuteMode: false
    property bool _dragging: false

    readonly property bool animationsEnabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
    readonly property real digitHeight: Style.iconButtonSize * 2
    readonly property real chromeHeight: Style.spacingXL * 5 + Style.spacingL + Style.spacingXS + Style.spacingM + titleText.implicitHeight + digitHeight + actionRow.implicitHeight
    readonly property real digitWidth: is24Hour ? digitHeight + Style.spacingL * 2 + Style.spacingXXS : digitHeight + Style.spacingL
    readonly property real outerRingRadius: faceSize * Style.clockOuterRingRatio
    readonly property real innerRingRadius: faceSize * Style.clockInnerRingRatio
    readonly property real numberSize: Style.clockHandleSize
    readonly property bool isPm: _hour >= 12
    readonly property int displayHour: is24Hour ? _hour : ((_hour % 12) === 0 ? 12 : _hour % 12)
    readonly property real handAngle: _minuteMode ? _minute * 6 : (_hour % 12) * 30
    readonly property real handLength: (!_minuteMode && is24Hour && (_hour === 0 || _hour > 12)) ? innerRingRadius : outerRingRadius

    function pad(v) {
        return (v < 10 ? "0" : "") + v;
    }

    function open() {
        _hour = Math.max(0, Math.min(23, hour));
        _minute = Math.max(0, Math.min(59, minute));
        _minuteMode = false;
        _opened = true;
        hourDigit.forceActiveFocus();
    }

    function close() {
        _opened = false;
    }

    function setPeriod(pm) {
        const base = _hour % 12;
        _hour = pm ? base + 12 : base;
    }

    function selectHour(h) {
        _hour = h;
        advanceTimer.restart();
    }

    function selectMinute(m) {
        _minute = m;
    }

    function angleFromPoint(x, y) {
        const dx = x - faceSize / 2;
        const dy = y - faceSize / 2;
        return (Math.atan2(dy, dx) * 180 / Math.PI + 90 + 360) % 360;
    }

    function distanceFromCenter(x, y) {
        const dx = x - faceSize / 2;
        const dy = y - faceSize / 2;
        return Math.sqrt(dx * dx + dy * dy);
    }

    function applyPoint(x, y) {
        const angle = angleFromPoint(x, y);
        if (_minuteMode) {
            _minute = Math.round(angle / 30) * 5 % 60;
            return;
        }
        const step = Math.round(angle / 30) % 12;
        if (!is24Hour) {
            _hour = isPm ? (step === 0 ? 12 : step + 12) : (step === 0 ? 0 : step);
            return;
        }
        const inner = distanceFromCenter(x, y) < (outerRingRadius + innerRingRadius) / 2;
        _hour = inner ? (step === 0 ? 0 : step + 12) : (step === 0 ? 12 : step);
    }

    anchors.fill: parent
    visible: opacity > 0
    opacity: _opened ? 1 : 0
    z: 1000

    Behavior on opacity {
        enabled: root.animationsEnabled
        DAnim {
            duration: Style.expressiveDurations.expressiveEffects
            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
        }
    }

    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_Escape:
            root.rejected();
            root.close();
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.accepted(root._hour, root._minute);
            root.close();
            break;
        case Qt.Key_Up:
        case Qt.Key_Right:
            root.nudge(1);
            break;
        case Qt.Key_Down:
        case Qt.Key_Left:
            root.nudge(-1);
            break;
        case Qt.Key_A:
            root.setPeriod(false);
            break;
        case Qt.Key_P:
            root.setPeriod(true);
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    function nudge(direction) {
        if (_minuteMode) {
            _minute = (_minute + direction * 5 + 60) % 60;
            return;
        }
        _hour = (_hour + direction + 24) % 24;
    }

    Timer {
        id: advanceTimer
        interval: Style.clockSwitchDelay
        onTriggered: {
            root._minuteMode = true;
            if (hourDigit.activeFocus)
                minuteDigit.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Style.withAlpha(Style.backdropColor, Style.scrimAlpha)

        MouseArea {
            anchors.fill: parent
            onClicked: {
                root.rejected();
                root.close();
            }
        }
    }

    Rectangle {
        id: card

        width: Math.max(root.faceSize, digitRow.implicitWidth) + Style.spacingXL * 3
        height: cardColumn.implicitHeight + Style.spacingXL * 2
        anchors.centerIn: parent
        radius: Style.cornerRadiusXL
        color: Style.cardSurface
        border.width: Style.layerOutlineWidth
        border.color: Style.outlineMedium
        scale: root._opened ? 1 : Style.popupEnterScale

        Behavior on scale {
            enabled: root.animationsEnabled
            DAnim {
                duration: Style.expressiveDurations.expressiveDefaultSpatial
                easing.bezierCurve: Style.expressiveCurves.expressiveDefaultSpatial
            }
        }

        MouseArea {
            anchors.fill: parent
        }

        ElevationShadow {
            anchors.fill: parent
            z: -1
            level: Style.elevationLevel3
            fallbackOffset: Style.spacingS
            targetRadius: card.radius
            targetColor: card.color
            shadowEnabled: Style.elevationEnabled
        }

        ColumnLayout {
            id: cardColumn

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Style.spacingXL
            spacing: 0

            StyledText {
                id: titleText
                text: root.title
                font.pixelSize: Style.fontSizeMedium
                font.weight: Style.fontWeightMedium
                color: Style.onSurfaceVariant
            }

            RowLayout {
                id: digitRow
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Style.spacingL + Style.spacingXS
                spacing: 0

                StyledButton {
                    id: hourDigit

                    Keys.forwardTo: [root]
                    Accessible.name: I18n.tr("Hour")
                    Accessible.description: root.pad(root.displayHour)
                    onClicked: root._minuteMode = false

                    Layout.preferredWidth: root.digitWidth
                    Layout.preferredHeight: root.digitHeight
                    radius: Style.cornerRadiusS
                    color: root._minuteMode ? Style.chipSurface : Style.primaryContainer
                    KeyNavigation.backtab: okButton
                    onActiveFocusChanged: {
                        if (!activeFocus)
                            return;
                        root._minuteMode = false;
                    }

                    Behavior on color {
                        enabled: root.animationsEnabled
                        DColorAnim {
                            duration: Style.expressiveDurations.expressiveEffects
                            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                        }
                    }

                    NumericText {
                        anchors.centerIn: parent
                        text: root.pad(root.displayHour)
                        reserveText: "00"
                        font.pixelSize: Style.fontSizeDisplayLarge
                        font.weight: Style.fontWeightMedium
                        color: root._minuteMode ? Style.surfaceText : Style.onPrimaryContainer
                    }

                    StateLayer {
                        control: hourDigit
                        stateColor: Style.primary
                    }

                    FocusRing {
                        visible: hourDigit.visualFocus
                    }
                }

                StyledText {
                    Layout.preferredWidth: Style.spacingXL
                    Layout.fillHeight: true
                    text: ":"
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: Style.fontSizeDisplayLarge
                    font.weight: Style.fontWeightMedium
                    color: Style.surfaceText
                }

                StyledButton {
                    id: minuteDigit

                    Keys.forwardTo: [root]
                    Accessible.name: I18n.tr("Minute")
                    Accessible.description: root.pad(root._minute)
                    onClicked: root._minuteMode = true

                    Layout.preferredWidth: root.digitWidth
                    Layout.preferredHeight: root.digitHeight
                    radius: Style.cornerRadiusS
                    color: root._minuteMode ? Style.primaryContainer : Style.chipSurface
                    onActiveFocusChanged: {
                        if (!activeFocus)
                            return;
                        root._minuteMode = true;
                    }

                    Behavior on color {
                        enabled: root.animationsEnabled
                        DColorAnim {
                            duration: Style.expressiveDurations.expressiveEffects
                            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                        }
                    }

                    NumericText {
                        anchors.centerIn: parent
                        text: root.pad(root._minute)
                        reserveText: "00"
                        font.pixelSize: Style.fontSizeDisplayLarge
                        font.weight: Style.fontWeightMedium
                        color: root._minuteMode ? Style.onPrimaryContainer : Style.surfaceText
                    }

                    StateLayer {
                        control: minuteDigit
                        stateColor: Style.primary
                    }

                    FocusRing {
                        visible: minuteDigit.visualFocus
                    }
                }

                Rectangle {
                    id: periodColumn

                    readonly property real innerRadius: radius - border.width

                    Layout.leftMargin: Style.spacingM
                    Layout.preferredWidth: Style.iconButtonSize + Style.spacingM
                    Layout.preferredHeight: root.digitHeight
                    radius: Style.cornerRadiusS
                    color: "transparent"
                    border.width: Style.outlineWidth
                    border.color: Style.outline
                    visible: !root.is24Hour

                    Column {
                        anchors.fill: parent
                        anchors.margins: periodColumn.border.width

                        StyledButton {
                            id: amItem

                            checkable: true
                            checked: !root.isPm
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: I18n.tr("AM")
                            onClicked: root.setPeriod(false)

                            width: parent.width
                            height: Math.floor((parent.height - periodDivider.height) / 2)
                            topLeftRadius: periodColumn.innerRadius
                            topRightRadius: periodColumn.innerRadius
                            bottomLeftRadius: 0
                            bottomRightRadius: 0
                            color: root.isPm ? "transparent" : Style.tertiaryContainer
                            StyledText {
                                anchors.centerIn: parent
                                text: I18n.tr("AM")
                                font.pixelSize: Style.fontSizeSmall
                                font.weight: Style.fontWeightMedium
                                color: root.isPm ? Style.onSurfaceVariant : Style.onTertiaryContainer
                            }

                            StateLayer {
                                control: amItem
                            }

                            FocusRing {
                                visible: amItem.visualFocus
                            }
                        }

                        Rectangle {
                            id: periodDivider

                            width: parent.width
                            height: periodColumn.border.width
                            color: periodColumn.border.color
                        }

                        StyledButton {
                            id: pmItem

                            checkable: true
                            checked: root.isPm
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: I18n.tr("PM")
                            onClicked: root.setPeriod(true)

                            width: parent.width
                            height: parent.height - amItem.height - periodDivider.height
                            topLeftRadius: 0
                            topRightRadius: 0
                            bottomLeftRadius: periodColumn.innerRadius
                            bottomRightRadius: periodColumn.innerRadius
                            color: root.isPm ? Style.tertiaryContainer : "transparent"
                            StyledText {
                                anchors.centerIn: parent
                                text: I18n.tr("PM")
                                font.pixelSize: Style.fontSizeSmall
                                font.weight: Style.fontWeightMedium
                                color: root.isPm ? Style.onTertiaryContainer : Style.onSurfaceVariant
                            }

                            StateLayer {
                                control: pmItem
                            }

                            FocusRing {
                                visible: pmItem.visualFocus
                                topLeftRadius: 0
                                topRightRadius: 0
                                bottomLeftRadius: parent.bottomLeftRadius + Style.focusRingWidth
                                bottomRightRadius: parent.bottomRightRadius + Style.focusRingWidth
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: face

                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Style.spacingXL + Style.spacingM
                Layout.preferredWidth: root.faceSize
                Layout.preferredHeight: root.faceSize
                radius: width / 2
                color: Style.chipSurface

                Behavior on color {
                    enabled: root.animationsEnabled
                    DColorAnim {
                        duration: Style.expressiveDurations.expressiveEffects
                        easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                    }
                }

                Item {
                    id: hand
                    x: root.faceSize / 2 - 1
                    y: root.faceSize / 2
                    width: Style.clockHandWidth
                    height: root.handLength
                    transformOrigin: Item.Top
                    rotation: root.handAngle + 180

                    Behavior on rotation {
                        enabled: root.animationsEnabled && !root._dragging
                        RotationAnimation {
                            direction: RotationAnimation.Shortest
                            duration: Style.expressiveDurations.expressiveFastSpatial
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: Style.expressiveCurves.expressiveDefaultSpatial
                        }
                    }

                    Behavior on height {
                        enabled: root.animationsEnabled && !root._dragging
                        DAnim {
                            duration: Style.expressiveDurations.expressiveFastSpatial
                            easing.bezierCurve: Style.expressiveCurves.standard
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: Style.primary
                        radius: width / 2
                    }

                    Rectangle {
                        width: root.numberSize
                        height: root.numberSize
                        radius: width / 2
                        color: Style.primary
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.bottom
                    }
                }

                Rectangle {
                    width: Style.clockCenterSize
                    height: Style.clockCenterSize
                    radius: width / 2
                    anchors.centerIn: parent
                    color: Style.primary
                }

                Repeater {
                    model: root._minuteMode ? 12 : (root.is24Hour ? 24 : 12)

                    Item {
                        required property int index
                        readonly property bool innerRing: !root._minuteMode && root.is24Hour && index >= 12
                        readonly property int step: index % 12
                        readonly property int value: root._minuteMode ? step * 5 : (innerRing ? (step === 0 ? 0 : step + 12) : (step === 0 ? 12 : step))
                        readonly property bool selected: root._minuteMode ? (root._minute === value) : (root.is24Hour ? root._hour === value : (root._hour % 12) === (value % 12))
                        readonly property real ringRadius: innerRing ? root.innerRingRadius : root.outerRingRadius
                        readonly property real angle: (step * 30 - 90) * Math.PI / 180

                        width: root.numberSize
                        height: root.numberSize
                        x: root.faceSize / 2 + ringRadius * Math.cos(angle) - width / 2
                        y: root.faceSize / 2 + ringRadius * Math.sin(angle) - height / 2

                        NumericText {
                            anchors.centerIn: parent
                            text: root._minuteMode || parent.innerRing ? root.pad(parent.value) : parent.value
                            reserveText: "00"
                            font.pixelSize: parent.innerRing ? Style.fontSizeSmall : Style.fontSizeMedium
                            font.weight: parent.selected ? Style.fontWeightBold : Style.fontWeightMedium
                            color: parent.selected ? Style.onPrimary : Style.surfaceText
                        }
                    }
                }

                MouseArea {
                    id: faceArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onPressed: mouse => {
                        root._dragging = true;
                        root.applyPoint(mouse.x, mouse.y);
                    }
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return;
                        root.applyPoint(mouse.x, mouse.y);
                    }
                    onReleased: {
                        root._dragging = false;
                        if (root._minuteMode)
                            return;
                        advanceTimer.restart();
                    }
                }
            }

            Row {
                id: actionRow
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: Style.spacingXL
                spacing: Style.spacingS

                DButton {
                    text: I18n.tr("Cancel")
                    variant: "text"
                    onClicked: {
                        root.rejected();
                        root.close();
                    }
                }

                DButton {
                    id: okButton

                    text: I18n.tr("OK")
                    KeyNavigation.tab: hourDigit
                    variant: "text"
                    onClicked: {
                        root.accepted(root._hour, root._minute);
                        root.close();
                    }
                }
            }
        }
    }
}
