pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

Row {
    id: pair

    property bool backEnabled: false
    property bool forwardEnabled: false

    signal backRequested
    signal forwardRequested

    spacing: FileBrowserMetrics.navPairGap
    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    component Segment: StyledButton {
        id: segment

        focusPolicy: Qt.TabFocus
        required property bool leading
        property string iconName: ""
        property string tooltipText: ""
        readonly property real outerRadius: Style.fullRadius(FileBrowserMetrics.navPairSegmentWidth, FileBrowserMetrics.controlSize)
        readonly property real startRadius: leading ? outerRadius : FileBrowserMetrics.navPairInnerRadius
        readonly property real endRadius: leading ? FileBrowserMetrics.navPairInnerRadius : outerRadius

        width: FileBrowserMetrics.navPairSegmentWidth
        height: FileBrowserMetrics.controlSize
        color: enabled ? Style.chipSurface : Style.onSurface_12
        topLeftRadius: mirrored ? endRadius : startRadius
        bottomLeftRadius: mirrored ? endRadius : startRadius
        topRightRadius: mirrored ? startRadius : endRadius
        bottomRightRadius: mirrored ? startRadius : endRadius
        Accessible.name: tooltipText

        StateLayer {
            control: segment
            disabled: !segment.enabled
            stateColor: Style.surfaceText
            tooltipText: segment.tooltipText
        }

        DIcon {
            anchors.centerIn: parent
            name: segment.iconName
            size: Style.iconSizeMedium
            color: segment.enabled ? Style.surfaceText : Style.onSurface_38
        }

        FocusRing {
            visible: segment.visualFocus
            radius: segment.outerRadius
        }
    }

    Segment {
        leading: true
        enabled: pair.backEnabled
        iconName: I18n.isRtl ? "arrow_forward" : "arrow_back"
        tooltipText: I18n.tr("Back", "tooltip on the button going to the previous location")
        onClicked: pair.backRequested()
    }

    Segment {
        leading: false
        enabled: pair.forwardEnabled
        iconName: I18n.isRtl ? "arrow_back" : "arrow_forward"
        tooltipText: I18n.tr("Forward", "tooltip on the button going to the next location")
        onClicked: pair.forwardRequested()
    }
}
