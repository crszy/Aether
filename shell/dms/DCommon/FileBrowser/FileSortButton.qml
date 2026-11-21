pragma ComponentBehavior: Bound

import QtQuick
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

Row {
    id: control

    property string sortKey: "name"
    property bool descending: false
    property bool showLabel: false

    signal sortKeySelected(string key)
    signal directionToggled

    property var keys: defaultKeys
    readonly property var defaultKeys: [
        {
            "key": "name",
            "icon": "sort_by_alpha",
            "label": I18n.tr("Name", "file browser sort criterion option")
        },
        {
            "key": "size",
            "icon": "straighten",
            "label": I18n.tr("Size", "file browser sort criterion option")
        },
        {
            "key": "mtime",
            "icon": "history",
            "label": I18n.tr("Modified", "file browser sort criterion option")
        },
        {
            "key": "type",
            "icon": "category",
            "label": I18n.tr("Type", "file browser sort criterion option")
        }
    ]
    readonly property var current: keys.find(entry => entry.key === sortKey) ?? keys[0]
    readonly property bool menuOpen: menu.menuVisible

    function openMenu() {
        menu.currentValue = current.label;
        menu.openDropdownMenu();
    }

    spacing: FileBrowserMetrics.navPairGap
    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    component Segment: StyledButton {
        id: segment

        focusPolicy: Qt.TabFocus
        required property bool leading
        property bool round: false
        property string tooltipText: ""

        readonly property real outerRadius: Style.fullRadius(width, height)
        readonly property real startRadius: round || leading ? outerRadius : FileBrowserMetrics.navPairInnerRadius
        readonly property real endRadius: round || !leading ? outerRadius : FileBrowserMetrics.navPairInnerRadius

        height: FileBrowserMetrics.controlSize
        color: Style.chipSurface
        topLeftRadius: mirrored ? endRadius : startRadius
        bottomLeftRadius: mirrored ? endRadius : startRadius
        topRightRadius: mirrored ? startRadius : endRadius
        bottomRightRadius: mirrored ? startRadius : endRadius
        Accessible.name: tooltipText

        Behavior on topRightRadius {
            enabled: FileBrowserMetrics.animationsEnabled
            DAnim {
                duration: Style.expressiveDurations.expressiveEffects
                easing.bezierCurve: Style.expressiveCurves.expressiveEffects
            }
        }

        Behavior on bottomRightRadius {
            enabled: FileBrowserMetrics.animationsEnabled
            DAnim {
                duration: Style.expressiveDurations.expressiveEffects
                easing.bezierCurve: Style.expressiveCurves.expressiveEffects
            }
        }

        Behavior on topLeftRadius {
            enabled: FileBrowserMetrics.animationsEnabled
            DAnim {
                duration: Style.expressiveDurations.expressiveEffects
                easing.bezierCurve: Style.expressiveCurves.expressiveEffects
            }
        }

        Behavior on bottomLeftRadius {
            enabled: FileBrowserMetrics.animationsEnabled
            DAnim {
                duration: Style.expressiveDurations.expressiveEffects
                easing.bezierCurve: Style.expressiveCurves.expressiveEffects
            }
        }

        StateLayer {
            control: segment
            stateColor: Style.surfaceText
            tooltipText: segment.tooltipText
        }

        FocusRing {
            visible: segment.visualFocus
            radius: segment.outerRadius
        }
    }

    Segment {
        id: keySegment

        leading: true
        width: control.showLabel ? Math.max(FileBrowserMetrics.navPairSegmentWidth, keyRow.implicitWidth + Style.spacingL * 2) : FileBrowserMetrics.navPairSegmentWidth
        tooltipText: control.showLabel ? "" : I18n.tr("Sort By", "file browser sort menu section header")
        Accessible.name: I18n.tr("Sort By", "file browser sort menu section header")
        Accessible.description: control.current.label
        onClicked: control.openMenu()
        Keys.onDownPressed: event => {
            control.openMenu();
            event.accepted = true;
        }

        Rectangle {
            anchors.fill: parent
            visible: control.menuOpen
            topLeftRadius: keySegment.topLeftRadius
            topRightRadius: keySegment.topRightRadius
            bottomLeftRadius: keySegment.bottomLeftRadius
            bottomRightRadius: keySegment.bottomRightRadius
            color: Style.withAlpha(Style.surfaceText, Style.stateLayerPressed)
        }

        Row {
            id: keyRow

            anchors.centerIn: parent
            spacing: Style.spacingS

            DIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: control.current.icon
                size: Style.iconSizeMedium
                color: Style.surfaceText
            }

            StyledText {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.showLabel
                text: control.current.label
                color: Style.surfaceText
                font.pixelSize: Style.fontSizeMedium
                font.weight: Style.fontWeightMedium
            }
        }

        DDropdown {
            id: menu

            showTrigger: false
            popupAnchorItem: control
            focusReturnTarget: keySegment
            alignPopupRight: !I18n.isRtl
            popupWidth: FileBrowserMetrics.menuWidth
            options: control.keys.map(entry => entry.label)
            optionIcons: control.keys.map(entry => entry.icon)
            onValueChanged: value => {
                const index = options.indexOf(value);
                if (index < 0)
                    return;
                control.sortKeySelected(control.keys[index].key);
            }
        }
    }

    Segment {
        leading: false
        round: control.descending
        width: FileBrowserMetrics.controlSize
        tooltipText: control.descending ? I18n.tr("Descending", "file browser sort order option") : I18n.tr("Ascending", "file browser sort order option")
        onClicked: control.directionToggled()

        DIcon {
            anchors.centerIn: parent
            name: control.descending ? "arrow_downward" : "arrow_upward"
            size: Style.iconSizeMedium
            color: Style.surfaceText
        }
    }
}
