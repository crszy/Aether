pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import qs.dms.DCommon.Common
import "../Common/WheelInput.js" as WheelInput

T.Control {
    id: root

    property alias model: destinations.model
    property int currentIndex: -1
    property int orientation: Qt.Horizontal
    property bool editable: false
    property bool evenlySpaced: true
    readonly property bool vertical: orientation === Qt.Vertical
    readonly property int count: destinations.count
    readonly property real destinationHeight: Math.max(Style.navigationHeight, ...items.children.map(item => item.implicitHeight))
    property Item nextFocusTarget: null
    property Item previousFocusTarget: null
    property bool tabHighlights: false
    property int highlightedIndex: -1

    signal activated(int index)
    signal editRequested(int index)

    spacing: 0
    focusPolicy: Qt.TabFocus
    implicitWidth: vertical ? Style.navigationRailWidth : count * Style.navigationItemMinWidth + Math.max(0, count - 1) * spacing
    implicitHeight: vertical ? count * destinationHeight + Math.max(0, count - 1) * spacing : destinationHeight
    LayoutMirroring.enabled: false

    function revealCurrent() {
        items.forceLayout();
        const item = destinations.itemAt(highlightedIndex >= 0 ? highlightedIndex : currentIndex);
        if (!item)
            return;
        if (vertical) {
            viewport.contentY = Math.max(0, Math.min(item.y, Math.max(viewport.contentY, item.y + item.height - viewport.height)));
            return;
        }
        viewport.contentX = Math.max(0, Math.min(item.x, Math.max(viewport.contentX, item.x + item.width - viewport.width)));
    }

    function isAction(index) {
        return destinations.itemAt(index)?.modelData?.action === true;
    }

    // Selection follows the arrow keys, so action destinations are click-only or arrowing past one would fire it.
    function activateSelectable(start, step) {
        for (let i = 1; i <= count; i++) {
            const index = ((start + step * i) % count + count) % count;
            if (!isAction(index)) {
                activated(index);
                return;
            }
        }
    }

    function moveSelection(step) {
        if (count === 0)
            return;
        activateSelectable(currentIndex < 0 ? (step > 0 ? -1 : 0) : currentIndex, step);
    }

    function moveHighlight(step) {
        if (count === 0)
            return;
        const from = highlightedIndex >= 0 ? highlightedIndex : currentIndex;
        highlightedIndex = from < 0 ? (step > 0 ? 0 : count - 1) : ((from + step) % count + count) % count;
    }

    onCurrentIndexChanged: {
        highlightedIndex = -1;
        revealTimer.restart();
    }
    onHighlightedIndexChanged: revealTimer.restart()
    onActiveFocusChanged: {
        if (!activeFocus)
            highlightedIndex = -1;
    }
    onWidthChanged: revealTimer.restart()
    onHeightChanged: revealTimer.restart()
    onOrientationChanged: {
        viewport.contentX = 0;
        viewport.contentY = 0;
        revealTimer.restart();
    }

    Timer {
        id: revealTimer
        interval: 0
        onTriggered: root.revealCurrent()
    }

    Keys.onPressed: event => {
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return;
        const highlighted = root.highlightedIndex;
        if (event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab)
            root.highlightedIndex = -1;
        switch (event.key) {
        case Qt.Key_Tab:
        case Qt.Key_Backtab:
            if (!root.tabHighlights)
                return;
            root.moveHighlight(event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier) ? -1 : 1);
            break;
        case Qt.Key_Escape:
            if (highlighted < 0)
                return;
            break;
        case Qt.Key_F2:
            if (!root.editable || root.currentIndex < 0)
                return;
            root.editRequested(root.currentIndex);
            break;
        case Qt.Key_Left:
        case Qt.Key_Right:
            if (root.vertical)
                return;
            root.moveSelection((event.key === Qt.Key_Right) !== I18n.isRtl ? 1 : -1);
            break;
        case Qt.Key_Up:
        case Qt.Key_Down:
            if (!root.vertical)
                return;
            root.moveSelection(event.key === Qt.Key_Down ? 1 : -1);
            break;
        case Qt.Key_Home:
            root.activateSelectable(-1, 1);
            break;
        case Qt.Key_End:
            root.activateSelectable(root.count, -1);
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            if (!event.isAutoRepeat && (highlighted >= 0 || root.currentIndex >= 0))
                root.activated(highlighted >= 0 ? highlighted : root.currentIndex);
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    KeyNavigation.tab: tabHighlights ? null : nextFocusTarget
    KeyNavigation.backtab: tabHighlights ? null : previousFocusTarget

    DFlickable {
        id: viewport
        anchors.fill: parent
        contentWidth: root.vertical ? width : Math.max(width, items.implicitWidth)
        contentHeight: root.vertical ? items.implicitHeight : height
        flickableDirection: root.vertical ? Flickable.VerticalFlick : Flickable.HorizontalFlick
        wheelEnabled: root.vertical
        clip: true

        WheelHandler {
            enabled: !root.vertical && viewport.contentWidth > viewport.width
            onWheel: event => {
                const delta = WheelInput.anyAxisDelta(event);
                viewport.contentX = Math.max(0, Math.min(viewport.contentWidth - viewport.width, viewport.contentX - delta));
                event.accepted = true;
            }
        }

        Grid {
            id: items
            columns: root.vertical ? 1 : Math.max(1, destinations.count)
            spacing: root.spacing
            layoutDirection: root.vertical || !I18n.isRtl ? Qt.LeftToRight : Qt.RightToLeft
            onLayoutDirectionChanged: revealTimer.restart()
            x: root.vertical ? 0 : Math.max(0, (viewport.width - implicitWidth) / 2)
            y: root.vertical ? Math.max(0, (viewport.height - implicitHeight) / 2) : 0
            onImplicitWidthChanged: revealTimer.restart()
            onImplicitHeightChanged: revealTimer.restart()

            Repeater {
                id: destinations
                onItemAdded: revealTimer.restart()
                onItemRemoved: revealTimer.restart()

                StyledButton {
                    id: destination

                    required property int index
                    required property var modelData
                    readonly property bool selected: root.currentIndex === index
                    implicitHeight: Math.max(Style.navigationHeight, Style.navigationVerticalPadding * 2 + Style.navigationIndicatorHeight + Style.spacingXS + label.implicitHeight)
                    width: root.vertical ? root.width : root.evenlySpaced ? Math.max(Style.navigationItemMinWidth, (root.width - root.spacing * (root.count - 1)) / Math.max(1, root.count)) : Style.navigationItemMinWidth
                    height: root.vertical && root.evenlySpaced ? Math.max(root.destinationHeight, (root.height - root.spacing * (root.count - 1)) / Math.max(1, root.count)) : root.destinationHeight
                    focusPolicy: Qt.NoFocus
                    Accessible.role: modelData?.action === true ? Accessible.Button : Accessible.PageTab
                    Accessible.name: modelData?.text ?? ""
                    Accessible.selected: selected
                    onClicked: root.activated(index)

                    Rectangle {
                        id: indicator
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: (parent.height - destination.implicitHeight) / 2 + Style.navigationVerticalPadding
                        width: Style.navigationIndicatorWidth
                        height: Style.navigationIndicatorHeight
                        radius: Style.fullRadius(width, height)
                        color: destination.selected ? Style.selectedContainer : "transparent"

                        Behavior on color {
                            enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                            DColorAnim {
                                duration: Style.expressiveDurations.expressiveEffects
                                easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                            }
                        }

                        StateLayer {
                            control: destination
                            disabled: editButton.visible
                            hovered: destination.hovered || root.highlightedIndex === destination.index
                            stateColor: Style.onSelectedContainer
                        }
                    }

                    DIcon {
                        id: icon
                        x: (parent.width - width) / 2
                        y: indicator.y + (indicator.height - height) / 2
                        name: destination.modelData?.icon ?? ""
                        size: Style.iconSize
                        filled: destination.selected
                        color: destination.selected ? Style.onSelectedContainer : Style.onSurfaceVariant
                        visible: !editButton.revealed
                    }

                    StyledText {
                        id: label
                        x: Style.spacingXS
                        y: indicator.y + indicator.height + Style.spacingXS
                        width: parent.width - Style.spacingXS * 2
                        text: destination.modelData?.text ?? ""
                        font.pixelSize: Style.fontSizeSmall
                        font.weight: Style.fontWeightMedium
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: destination.selected ? Style.secondary : Style.onSurfaceVariant
                    }

                    StyledButton {
                        id: editButton
                        readonly property bool revealed: root.editable && destination.selected && hovered
                        anchors.centerIn: icon
                        width: indicator.width
                        height: Style.navigationIndicatorHeight
                        radius: Style.fullRadius(width, height)
                        visible: root.editable && destination.selected
                        focusPolicy: Qt.NoFocus
                        Accessible.name: I18n.tr("Edit")
                        onClicked: root.editRequested(destination.index)

                        DIcon {
                            anchors.centerIn: parent
                            name: "edit"
                            size: Style.iconSize
                            color: Style.onSelectedContainer
                            visible: editButton.revealed
                        }

                        StateLayer {
                            control: editButton
                            stateColor: Style.onSelectedContainer
                        }
                    }
                }
            }
        }
    }
}
