import QtQuick
import qs.dms.DCommon.Common

Flow {
    id: root

    property var model: []
    property int currentIndex: 0
    property bool multiSelect: false
    property var selectedValues: []
    property int chipHeight: Style.buttonHeightXS
    property int chipPadding: Style.spacingL
    property real chipRadius: Style.cornerRadiusS
    property bool showCheck: true
    property bool showCounts: true
    readonly property int focusIndex: Math.max(0, Math.min(currentIndex, (model?.length ?? 0) - 1))

    readonly property real singleRowWidth: {
        let total = spacing * Math.max(0, chipRepeater.count - 1);
        for (let i = 0; i < chipRepeater.count; i++)
            total += chipRepeater.itemAt(i)?.width ?? 0;
        return total;
    }

    signal selectionChanged(int index)
    signal selectionToggled(int index, bool selected)

    spacing: Style.spacingS
    width: parent ? parent.width : Style.smallBreakpoint
    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    function requestFocus(backwards) {
        chipRepeater.itemAt(focusIndex)?.forceActiveFocus(backwards ? Qt.BacktabFocusReason : Qt.TabFocusReason);
    }

    Keys.onPressed: event => {
        const count = model?.length ?? 0;
        if (!enabled || count === 0 || multiSelect)
            return;
        const forwardKey = I18n.isRtl ? Qt.Key_Left : Qt.Key_Right;
        const backwardKey = I18n.isRtl ? Qt.Key_Right : Qt.Key_Left;
        let next = -1;
        if (event.key === forwardKey)
            next = (currentIndex + 1) % count;
        if (event.key === backwardKey)
            next = (currentIndex - 1 + count) % count;
        if (next < 0)
            return;
        currentIndex = next;
        selectionChanged(next);
        chipRepeater.itemAt(next)?.forceActiveFocus(Qt.TabFocusReason);
        event.accepted = true;
    }

    Repeater {
        id: chipRepeater
        model: root.model

        StyledButton {
            id: chip
            required property var modelData
            required property int index

            property var value: typeof modelData === "string" ? modelData : (modelData.value !== undefined ? modelData.value : (modelData.label || ""))
            focusPolicy: activeFocus || root.multiSelect || index === root.focusIndex ? Qt.StrongFocus : Qt.ClickFocus
            Accessible.role: root.multiSelect ? Accessible.CheckBox : Accessible.RadioButton
            Accessible.name: label
            Accessible.description: tooltip
            checkable: true
            checked: selected
            onClicked: activate()

            function activate() {
                if (!root.enabled)
                    return;
                if (root.multiSelect) {
                    root.selectionToggled(index, !selected);
                    return;
                }
                root.currentIndex = index;
                root.selectionChanged(index);
            }

            property bool selected: root.multiSelect ? root.selectedValues.includes(value) : (index === root.currentIndex)
            property string label: typeof modelData === "string" ? modelData : (modelData.label || "")
            property int count: typeof modelData === "object" ? (modelData.count || 0) : 0
            property string tooltip: typeof modelData === "object" ? (modelData.tooltip || "") : ""
            property string leadingIcon: typeof modelData === "object" ? (modelData.icon || "") : ""
            property bool showCount: root.showCounts && count > 0
            readonly property color contentColor: !enabled ? Style.onSurface_38 : selected ? Style.onSecondaryContainer : Style.onSurfaceVariant

            readonly property bool hasCheck: root.showCheck && selected
            readonly property bool hasLeadingIcon: hasCheck || leadingIcon !== ""
            readonly property real leadingPadding: hasLeadingIcon ? root.chipPadding * 0.5 : root.chipPadding

            width: contentRow.implicitWidth + leadingPadding + root.chipPadding
            height: root.chipHeight
            radius: root.chipRadius

            Behavior on radius {
                enabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                DAnim {
                    duration: Style.expressiveDurations.expressiveFastSpatial
                    easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                }
            }

            color: selected ? (enabled ? Style.secondaryContainer : Style.onSurface_12) : "transparent"
            border.width: selected ? 0 : Style.outlineWidth
            border.color: enabled ? Style.outlineVariant : Style.onSurface_38

            FocusRing {
                visible: chip.visualFocus
            }

            Behavior on color {
                enabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                DColorAnim {
                    duration: Style.expressiveDurations.expressiveEffects
                    easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                }
            }

            StateLayer {
                id: stateLayer
                control: chip
                disabled: !root.enabled
                stateColor: chip.contentColor
                tooltipText: chip.tooltip || null
                transitionDuration: Style.expressiveDurations.expressiveEffects
                transitionCurve: Style.expressiveCurves.expressiveEffects
            }

            Row {
                id: contentRow
                anchors.left: parent.left
                anchors.leftMargin: chip.leadingPadding
                anchors.verticalCenter: parent.verticalCenter
                spacing: Style.spacingS

                DIcon {
                    name: chip.hasCheck ? "check" : chip.leadingIcon
                    size: Style.chipIconSize
                    anchors.verticalCenter: parent.verticalCenter
                    color: chip.selected || !chip.enabled ? chip.contentColor : Style.primary
                    visible: chip.hasLeadingIcon
                }

                StyledText {
                    text: chip.label + (chip.showCount ? " (" + chip.count + ")" : "")
                    font.pixelSize: Style.fontSizeSmall
                    font.weight: Style.fontWeightMedium
                    color: chip.contentColor
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
