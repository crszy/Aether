pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

Rectangle {
    id: footer

    property int count: 0
    property real bytes: 0

    signal cleared

    readonly property string summary: {
        const items = count === 1 ? I18n.tr("1 selected", "a single selected item") : I18n.tr("%1 selected", "count of selected items").arg(count);
        if (bytes < 0)
            return items;
        return `${items} · ${FileFormat.size(bytes)}`;
    }

    implicitWidth: label.implicitWidth + clearButton.width + Style.spacingM * 2 + Style.spacingXS
    height: FileBrowserMetrics.selectionFooterHeight
    radius: Style.cornerRadiusFull
    color: Style.secondaryContainer

    StyledText {
        id: label

        anchors.left: parent.left
        anchors.leftMargin: Style.spacingM
        anchors.right: clearButton.left
        anchors.rightMargin: Style.spacingXS
        anchors.verticalCenter: parent.verticalCenter
        text: footer.summary
        color: Style.onSecondaryContainer
        font.pixelSize: Style.fontSizeSmall
        elide: Text.ElideRight
    }

    DActionButton {
        id: clearButton

        anchors.right: parent.right
        anchors.rightMargin: Style.spacingXXS
        anchors.verticalCenter: parent.verticalCenter
        buttonSize: parent.height - Style.spacingXXS * 2
        iconName: "close"
        iconSize: Style.iconSizeSmall
        iconColor: Style.onSecondaryContainer
        tooltipText: I18n.tr("Clear the selection", "tooltip on the button clearing the file selection")
        onClicked: footer.cleared()
    }
}
