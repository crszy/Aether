pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Widgets
import qs.dms.DCommon.Common

DActionButton {
    id: customButtonKeyboard
    circular: false
    buttonSize: Style.iconButtonSize
    focusPolicy: Qt.TabFocus
    Accessible.name: text
    property bool isShift: false
    color: Style.chipSurface

    property bool isIcon: text === "keyboard_hide" || text === "Backspace" || text === "Enter"

    DIcon {
        anchors.centerIn: parent
        name: {
            if (parent.text === "keyboard_hide")
                return "keyboard_hide";
            if (parent.text === "Backspace")
                return "backspace";
            if (parent.text === "Enter")
                return "keyboard_return";
            return "";
        }
        size: 20
        color: Style.surfaceText
        visible: parent.isIcon
    }

    StyledText {
        id: contentItem
        anchors.centerIn: parent
        text: parent.text
        color: Style.surfaceText
        font.pixelSize: Style.fontSizeXLarge
        font.weight: Style.fontWeight
        visible: !parent.isIcon
    }
}
