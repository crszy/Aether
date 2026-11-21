pragma Singleton

import QtQuick
import qs.dms.DCommon.Common

QtObject {
    readonly property real clockSize: Style.fontSizeDisplayLarge * 2
    readonly property real clockDigitWidth: clockSize * 0.625
    readonly property real fieldWidth: Style.fieldDefaultWidth + Style.buttonHeightM * 2
    readonly property real fieldHeight: Style.buttonHeightM
    readonly property real avatarSize: fieldHeight
    readonly property real passwordRowWidth: fieldWidth + avatarSize + Style.spacingM
    readonly property real notificationCardWidth: passwordRowWidth
    readonly property real notificationMaxHeight: Style.listItemTwoLineHeight * 4
    readonly property int notificationLimit: 5
    readonly property real keyboardKeySize: Style.buttonHeightM
    readonly property real keyboardWidth: keyboardKeySize * 10 + Style.spacingS * 11
    readonly property real keyboardHeight: keyboardKeySize * 4 + Style.spacingS * 5
    readonly property real powerMenuWidth: Style.fieldDefaultWidth * 2
    readonly property real powerGridColumnWidth: Style.buttonHeightM * 3
    readonly property real powerGridWidth: powerMenuWidth + Style.buttonHeightM * 3
    readonly property real powerButtonHeight: Style.buttonHeightM
    readonly property real powerGridButtonHeight: Style.listItemTwoLineHeight + Style.spacingL
    readonly property real shakeDistance: Style.spacingS
    readonly property int effectsDuration: Style.reduceMotion ? 0 : Style.expressiveDurations.expressiveEffects
    readonly property int shakeDuration: Style.reduceMotion ? 0 : Style.expressiveDurations.expressiveFastSpatial
}
