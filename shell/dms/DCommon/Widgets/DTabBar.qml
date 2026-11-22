import QtQuick
import QtQuick.Templates as T
import QtQuick.Shapes
import "../Common/TabNavigation.js" as TabNavigation
import qs.dms.DCommon.Common

T.Control {
    id: tabBar

    property alias model: tabRepeater.model
    property int currentIndex: 0
    spacing: Style.spacingL
    property int tabHeight: Style.buttonHeightM
    property bool showIcons: true
    property bool showDivider: true
    property bool equalWidthTabs: true
    property bool enableArrowNavigation: true
    property bool cycleOnTab: false
    property Item nextFocusTarget: null
    property Item previousFocusTarget: null

    signal tabClicked(int index)
    signal actionTriggered(int index)

    focus: false
    focusPolicy: Qt.TabFocus
    implicitHeight: Math.max(tabHeight, tabRow.implicitHeight + Style.tabIndicatorHeight)
    height: implicitHeight

    Keys.onPressed: event => {
        if (!enabled)
            return;
        switch (event.key) {
        case Qt.Key_Space:
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (!event.isAutoRepeat)
                tabRepeater.itemAt(currentIndex)?.click();
            event.accepted = true;
            return;
        }
        event.accepted = TabNavigation.handleKeyEvent(event, tabBar, tabRepeater, I18n.isRtl);
    }

    Row {
        id: tabRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: tabBar.spacing
        LayoutMirroring.enabled: false
        layoutDirection: I18n.isRtl ? Qt.RightToLeft : Qt.LeftToRight
        onLayoutDirectionChanged: indicatorUpdate.restart()

        Repeater {
            id: tabRepeater

            onItemAdded: indicatorUpdate.restart()
            onItemRemoved: indicatorUpdate.restart()

            StyledButton {
                id: tabItem
                focusPolicy: isAction ? Qt.TabFocus : Qt.NoFocus
                property bool isAction: modelData && modelData.isAction === true
                onIsActionChanged: indicatorUpdate.restart()
                property bool isActive: !isAction && tabBar.currentIndex === index
                property bool hasIcon: tabBar.showIcons && !!modelData?.icon?.length
                property bool hasText: !!modelData?.text?.length
                Accessible.role: isAction ? Accessible.Button : Accessible.PageTab
                Accessible.name: modelData?.text ?? ""
                Accessible.selected: isActive
                onClicked: {
                    if (!tabBar.enabled)
                        return;
                    if (isAction) {
                        tabBar.actionTriggered(index);
                        return;
                    }
                    tabBar.tabClicked(index);
                }
                readonly property real contentWidth: contentCol.implicitWidth
                radius: Style.cornerRadiusM

                width: tabBar.equalWidthTabs ? Math.max(0, tabBar.width - tabBar.spacing * Math.max(0, tabRepeater.count - 1)) / Math.max(1, tabRepeater.count) : Math.max(contentCol.implicitWidth + Style.spacingXL, Style.tabMinWidth)
                height: Math.max(tabBar.tabHeight - Style.tabIndicatorHeight, contentCol.implicitHeight + Style.spacingXS * 2)
                anchors.verticalCenter: tabRow.verticalCenter

                Column {
                    id: contentCol
                    onImplicitWidthChanged: indicatorUpdate.restart()
                    anchors.centerIn: parent
                    spacing: Style.spacingXS

                    DIcon {
                        name: modelData.icon || ""
                        anchors.horizontalCenter: parent.horizontalCenter
                        size: Style.iconSize
                        color: tabItem.isActive ? Style.primary : Style.onSurfaceVariant
                        filled: tabItem.isActive
                        visible: hasIcon

                        Behavior on color {
                            enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                            DColorAnim {
                                duration: Style.expressiveDurations.expressiveEffects
                                easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                            }
                        }
                    }

                    StyledText {
                        text: modelData.text || ""
                        anchors.horizontalCenter: parent.horizontalCenter
                        font.pixelSize: Style.fontSizeMedium
                        color: tabItem.isActive ? Style.primary : Style.onSurfaceVariant
                        font.weight: Style.fontWeightMedium
                        visible: hasText

                        Behavior on color {
                            enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                            DColorAnim {
                                duration: Style.expressiveDurations.expressiveEffects
                                easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                            }
                        }
                    }
                }

                StateLayer {
                    control: tabItem
                    disabled: !tabBar.enabled
                    stateColor: Style.primary
                    transitionDuration: Style.expressiveDurations.expressiveEffects
                    transitionCurve: Style.expressiveCurves.expressiveEffects
                }

                FocusRing {
                    radius: Math.min(Style.fullRadius(width, height), Style.cornerRadiusM + Style.focusRingOffset)
                    visible: tabItem.visualFocus || (tabBar.visualFocus && tabItem.isActive)
                }
            }
        }
    }

    Rectangle {
        width: parent.width
        height: Style.dividerWidth
        anchors.bottom: parent.bottom
        color: Style.outlineVariant
        visible: tabBar.showDivider
    }

    Shape {
        id: indicator

        property bool animationEnabled: false
        property bool initialSetupComplete: false
        property bool movingRight: true
        property real leftX: 0
        property real rightX: 0
        readonly property real cornerRadius: Math.min(width / 2, height, Style.cornerRadiusS)

        anchors.bottom: parent.bottom
        height: Style.tabIndicatorHeight
        x: leftX
        width: Math.max(0, rightX - leftX)
        visible: false
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: 0
            fillColor: Style.primary
            startX: 0
            startY: indicator.height

            PathLine {
                x: 0
                y: indicator.cornerRadius
            }
            PathArc {
                x: indicator.cornerRadius
                y: 0
                radiusX: indicator.cornerRadius
                radiusY: indicator.cornerRadius
            }
            PathLine {
                x: indicator.width - indicator.cornerRadius
                y: 0
            }
            PathArc {
                x: indicator.width
                y: indicator.cornerRadius
                radiusX: indicator.cornerRadius
                radiusY: indicator.cornerRadius
            }
            PathLine {
                x: indicator.width
                y: indicator.height
            }
            PathLine {
                x: 0
                y: indicator.height
            }
        }

        Behavior on leftX {
            enabled: indicator.animationEnabled && !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
            DAnim {
                duration: indicator.movingRight ? Style.expressiveDurations.expressiveDefaultSpatial : Style.expressiveDurations.expressiveFastSpatial
                easing.bezierCurve: Style.expressiveCurves.emphasized
            }
        }

        Behavior on rightX {
            enabled: indicator.animationEnabled && !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
            DAnim {
                duration: indicator.movingRight ? Style.expressiveDurations.expressiveFastSpatial : Style.expressiveDurations.expressiveDefaultSpatial
                easing.bezierCurve: Style.expressiveCurves.emphasized
            }
        }
    }

    Timer {
        id: indicatorUpdate
        interval: 0
        onTriggered: tabBar.updateIndicator()
    }

    function updateIndicator() {
        if (tabRepeater.count === 0 || currentIndex < 0 || currentIndex >= tabRepeater.count) {
            indicator.visible = false;
            indicator.initialSetupComplete = false;
            return;
        }

        const item = tabRepeater.itemAt(currentIndex);
        if (!item || item.isAction) {
            indicator.visible = false;
            indicator.initialSetupComplete = false;
            return;
        }

        tabRow.forceLayout();
        const tabPos = item.mapToItem(tabBar, 0, 0);
        const tabCenterX = tabPos.x + item.width / 2;
        const indicatorWidth = Math.max(0, Math.min(item.width, Math.max(Style.tabIndicatorMinWidth, item.contentWidth - Style.tabIndicatorInset * 2)));
        const targetLeft = tabCenterX - indicatorWidth / 2;
        const targetRight = tabCenterX + indicatorWidth / 2;

        indicator.movingRight = targetLeft >= indicator.leftX;
        if (!indicator.initialSetupComplete) {
            indicator.animationEnabled = false;
            indicator.leftX = targetLeft;
            indicator.rightX = targetRight;
            indicator.visible = true;
            indicator.initialSetupComplete = true;
            indicator.animationEnabled = true;
            return;
        }
        indicator.leftX = targetLeft;
        indicator.rightX = targetRight;
        indicator.visible = true;
    }

    function snapIndicator() {
        indicator.initialSetupComplete = false;
        updateIndicator();
    }

    onCurrentIndexChanged: {
        indicatorUpdate.restart();
    }
    onWidthChanged: indicatorUpdate.restart()
    onSpacingChanged: indicatorUpdate.restart()
    onEqualWidthTabsChanged: indicatorUpdate.restart()
    Component.onCompleted: indicatorUpdate.restart()
}
