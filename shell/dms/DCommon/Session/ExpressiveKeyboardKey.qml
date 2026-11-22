import QtQuick
import qs.dms.DCommon.Widgets
import qs.dms.DCommon.Common

DActionButton {
    id: root

    property bool isShift: false

    focusPolicy: Qt.TabFocus
    buttonSize: LockMetrics.keyboardKeySize
    backgroundColor: isShift ? Style.primaryContainer : Style.secondaryContainer
    iconColor: isShift ? Style.onPrimaryContainer : Style.onSecondaryContainer
    radius: pressed && Style.shapeMorph ? Style.cornerRadiusS : Style.cornerRadiusM
    shapeDuration: LockMetrics.effectsDuration
    shapeCurve: Style.expressiveCurves.expressiveEffects
    stateDuration: LockMetrics.effectsDuration
    stateCurve: Style.expressiveCurves.expressiveEffects
    iconName: {
        switch (text) {
        case "keyboard_hide":
            return "keyboard_hide";
        case "Backspace":
            return "backspace";
        case "Enter":
            return "keyboard_return";
        default:
            return "";
        }
    }
    Accessible.name: {
        switch (text) {
        case "keyboard_hide":
            return I18n.tr("Hide keyboard");
        case "Backspace":
            return I18n.tr("Backspace");
        case "Enter":
            return I18n.tr("Enter");
        case " ":
            return I18n.tr("Space");
        case "↑":
            return I18n.tr("Shift");
        default:
            return text;
        }
    }

    StyledText {
        anchors.centerIn: parent
        text: root.text
        color: root.iconColor
        font.pixelSize: Style.fontSizeXLarge
        visible: root.iconName === ""
    }
}
