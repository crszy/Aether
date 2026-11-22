pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import qs.dms.DCommon.Common

Singleton {
    readonly property int densityLevel: Style.densityLevel
    readonly property real densityStepS: densityLevel * Style.spacingXS
    readonly property real densityStepM: densityLevel * Style.spacingS

    readonly property real headerHeight: Style.buttonHeightS
    readonly property real contentMargin: Style.windowInset
    readonly property real contentSpacing: Style.spacingM
    readonly property real controlSize: Style.buttonHeightS + densityStepS
    readonly property real controlSpacing: Style.spacingS
    readonly property real navPairSegmentWidth: controlSize + Style.spacingS
    readonly property real navPairInnerRadius: Style.cornerRadiusS
    readonly property real navPairGap: Style.spacingXXS

    readonly property real pathBarSpacing: Style.spacingXS
    readonly property real pathPillHeight: Style.buttonHeightXS
    readonly property real pathPillRadius: Style.cornerRadiusS
    readonly property real pathPillPadding: Style.spacingS
    readonly property real pathPillSpacing: Style.spacingXS
    readonly property real pathIconSize: Style.iconSizeSmall

    readonly property var gridIconSizes: [Style.iconSizeLarge * 1.5, Style.iconSizeLarge * 2, Style.iconSizeLarge * 3, Style.iconSizeLarge * 4, Style.iconSizeLarge * 5, Style.iconSizeLarge * 6, Style.iconSizeLarge * 8]
    readonly property var listIconSizes: [Style.iconSizeMedium, Style.iconSize, Style.iconSizeLarge, Style.iconSizeLarge * 1.25, Style.iconSizeLarge * 1.5, Style.iconSizeLarge * 2]
    readonly property int defaultGridZoom: 2
    readonly property int defaultListZoom: 0
    readonly property int defaultColumnZoom: 2
    readonly property var columnZoomFactors: [0.7, 0.85, 1, 1.15, 1.35, 1.6]
    readonly property real columnPaneMaxWidth: Style.sidebarWidth * 2.5
    readonly property real gridTilePadding: Style.spacingM
    readonly property real gridTileVerticalPadding: Math.max(Style.spacingS, Style.spacingM + densityStepS)
    readonly property real gridTileRadius: Style.cornerRadiusL
    readonly property real gridGap: Style.spacingM
    readonly property real gridNameSpacing: Style.spacingXS

    readonly property real columnPaneWidth: Style.sidebarWidth
    readonly property real columnMinPaneWidth: Style.sidebarWidth * 0.7
    readonly property real previewPaneWidth: Style.sidebarWidth * 1.5
    readonly property real columnSlideDistance: Style.spacingXL
    readonly property real previewIconSize: gridIconSizes[4]
    readonly property int previewBytes: 262144
    readonly property int previewLineChunk: 1024
    readonly property real previewInfoRatio: 0.5
    readonly property int previewRefreshInterval: 1000
    readonly property int previewImagePool: 4
    readonly property real previewCardRadius: Style.cornerRadiusL
    readonly property real previewCardPadding: Style.spacingL
    readonly property real previewKeyStep: Style.spacingXL
    readonly property real previewStripHeight: gridIconSizes[1]
    readonly property real previewStripSmallWidth: Style.buttonHeightS
    readonly property real previewDrawerMinWidth: Style.sidebarWidth
    readonly property real previewViewMinWidth: Style.sidebarWidth
    readonly property real previewDrawerRatio: 0.35

    readonly property real listRowHeight: Style.minimumTouchTargetSize + densityStepM
    readonly property real listRowPadding: Style.spacingL
    readonly property real listRowGap: Math.max(Style.spacingS, Style.spacingM + densityStepS)
    readonly property real listCompactGap: Style.spacingS + densityStepS
    readonly property real listCompactRowHeight: Style.minimumTouchTargetSize * 0.75
    readonly property real nameColumnMinWidth: Style.spacingXL * 6

    function iconSizesFor(viewMode) {
        return viewMode === "grid" ? gridIconSizes : listIconSizes;
    }

    function defaultZoom(viewMode) {
        return viewMode === "grid" ? defaultGridZoom : defaultListZoom;
    }

    function listRowHeightFor(iconSize, compact) {
        if (compact)
            return Math.max(iconSize + listCompactGap * 2, listCompactRowHeight);
        return Math.max(listRowHeight, iconSize + listRowGap * 2);
    }

    function columnZoomFactor(level) {
        const factors = columnZoomFactors;
        return factors[Math.max(0, Math.min(factors.length - 1, level))];
    }
    readonly property real columnHeaderHeight: Style.buttonHeightXS
    readonly property real columnHeaderPadding: Style.spacingS
    readonly property real columnGap: Style.spacingM
    readonly property real columnGripHeight: Style.iconSizeSmall
    readonly property real columnMinWidth: Style.spacingXL * 3
    readonly property real sizeColumnWidth: Style.spacingXL * 4
    readonly property real modifiedColumnWidth: Style.spacingXL * 6
    readonly property real typeColumnWidth: Style.spacingXL * 5
    readonly property real ownerColumnWidth: Style.spacingXL * 4
    readonly property real permissionsColumnWidth: Style.spacingXL * 4
    readonly property real locationColumnWidth: Style.spacingXL * 8

    readonly property real selectionFooterHeight: Style.buttonHeightS
    readonly property real emblemMinSize: Style.iconSizeSmall
    readonly property real emblemRatio: 0.3
    readonly property real hiddenOpacity: Style.pendingOpacity
    readonly property real skeletonOpacity: Style.pendingOpacity
    readonly property real rubberBandMinimum: Style.spacingS

    readonly property real sidebarWidth: Style.sidebarWidth
    readonly property int sidebarBreakpoint: Style.mediumBreakpoint
    readonly property real sidebarPadding: Style.spacingM
    readonly property real sidebarIconSize: Style.buttonHeightXS + densityStepS
    readonly property real sidebarRowHeight: Math.max(Style.buttonHeightXS, Style.buttonHeightS + densityStepM)
    readonly property real sidebarRowRadius: Style.cornerRadiusM
    readonly property real sidebarRowGap: Math.max(0, Style.groupedListGap + densityLevel * Style.spacingXXS)
    readonly property real sidebarGlyphSize: densityLevel < -1 ? Style.iconSizeSmall : Style.iconSizeMedium
    readonly property real usageWarnRatio: 0.9
    readonly property real paneRadius: Style.windowRadius
    readonly property real paneMargin: Style.spacingM
    readonly property real paneMarginHorizontal: Style.spacingL
    readonly property real menuWidth: Style.spacingXL * 9
    readonly property real menuDividerHeight: Style.spacingS * 2
    readonly property real detailLabelWidth: Style.spacingXL * 5
    readonly property real splitterWidth: Style.spacingM
    readonly property real splitterHandleHeight: Style.iconSizeLarge
    readonly property real menuMargin: Style.spacingL
    readonly property real pickerWidth: Style.launcherWidthLarge
    readonly property real pickerHeight: Style.launcherHeightDefault + Style.spacingXL
    readonly property real pickerMinWidth: Style.launcherWidthMicro
    readonly property real pickerMinHeight: Style.smallBreakpoint
    readonly property real pickerScreenFraction: 0.9
    readonly property int settleInterval: 150
    readonly property int countPrefetchScreens: 3
    readonly property int skeletonDelay: 150
    readonly property int typeAheadInterval: 1000
    readonly property int springLoadDelay: 700
    readonly property real dragScrollRowsPerSecond: 12
    readonly property real dragScrollEdge: Style.listItemHeight
    readonly property int activationDebounce: 400
    readonly property int suggestionRows: 8
    readonly property int suggestionCap: 50
    readonly property real suggestionHintRatio: 0.45
    readonly property real suggestionMinWidth: Style.launcherWidthMicro * 0.6
    readonly property int completionLimit: 500

    readonly property bool animationsEnabled: !Style.reduceMotion && Style.currentAnimationBaseDuration > 0
}
