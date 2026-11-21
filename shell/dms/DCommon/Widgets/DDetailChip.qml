import QtQuick
import qs.dms.DCommon.Common

Rectangle {
    id: root

    property string label: ""
    property string value: ""

    implicitWidth: content.implicitWidth + Style.spacingM * 2
    implicitHeight: Style.buttonHeightXS
    radius: Style.cornerRadiusS
    color: Style.floatingWindowFieldColor
    border.width: Style.outlineWidth
    border.color: Style.floatingWindowFieldBorderColor
    Accessible.role: Accessible.StaticText
    Accessible.name: label + ": " + value

    Row {
        id: content
        anchors.centerIn: parent
        spacing: Style.spacingXS

        StyledText {
            text: root.label + ":"
            font.pixelSize: Style.fontSizeSmall
            color: Style.surfaceVariantText
            anchors.verticalCenter: parent.verticalCenter
        }

        StyledText {
            text: root.value
            font.pixelSize: Style.fontSizeSmall
            font.weight: Style.fontWeightMedium
            color: Style.surfaceText
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
