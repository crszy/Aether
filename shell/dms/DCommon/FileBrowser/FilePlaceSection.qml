pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

Column {
    id: section

    property string title: ""
    property string emptyText: ""
    property bool showEmpty: false

    default property alias rows: body.data

    spacing: FileBrowserMetrics.sidebarRowGap

    StyledText {
        text: section.title
        color: Style.primary
        font.pixelSize: Style.fontSizeMedium
        font.weight: Style.fontWeightMedium
        leftPadding: Style.spacingM
        bottomPadding: Style.spacingXXS
    }

    Column {
        id: body

        width: section.width
        spacing: FileBrowserMetrics.sidebarRowGap
    }

    StyledText {
        width: section.width
        visible: section.showEmpty && section.emptyText !== ""
        text: section.emptyText
        color: Style.surfaceVariantText
        font.pixelSize: Style.fontSizeSmall
        leftPadding: Style.spacingM
        rightPadding: Style.spacingM
        wrapMode: Text.Wrap
    }
}
