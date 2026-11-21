pragma ComponentBehavior: Bound

import QtQuick
import Quickshell.Widgets
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets as Base

Rectangle {
    id: root

    property var actions: []
    property var actionProvider: null
    property bool gridLayout: false
    property int gridColumns: 1
    property int selectedIndex: 0
    property int holdActionIndex: -1
    property real holdProgress: 0
    property bool showHint: false
    property bool hintWarning: false
    property string hintText: ""
    property string hintIcon: "touch_app"
    readonly property real itemGap: gridLayout ? Style.spacingXS : Style.groupedListGap
    readonly property real desiredWidth: gridLayout ? Math.min(LockMetrics.powerGridWidth, Math.max(1, gridColumns) * LockMetrics.powerGridColumnWidth + itemGap * (Math.max(1, gridColumns) - 1) + Style.spacingL * 2) : LockMetrics.powerMenuWidth

    signal actionPressed(int index)
    signal actionReleased
    signal actionCanceled

    implicitWidth: desiredWidth
    implicitHeight: buttons.implicitHeight + Style.spacingL * 2 + (showHint ? hint.implicitHeight + Style.spacingM : 0)
    color: Style.cardSurface
    border.width: Style.layerOutlineWidth
    border.color: Style.outlineMedium
    radius: Style.windowRadius
    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    Grid {
        id: buttons
        x: Style.spacingL
        y: Style.spacingL
        width: parent.width - Style.spacingL * 2
        columns: root.gridLayout ? Math.max(1, root.gridColumns) : 1
        spacing: root.itemGap

        Repeater {
            model: root.actions

            Base.DListItem {
                id: button
                required property int index
                required property string modelData
                readonly property var actionData: root.actionProvider ? root.actionProvider(modelData) : ({})
                readonly property bool holding: root.holdActionIndex === index && root.holdProgress > 0
                readonly property bool warningAction: modelData === "reboot" || modelData === "softreboot" || modelData === "poweroff"
                readonly property color tint: {
                    if (warningAction && (hovered || holding))
                        return modelData === "poweroff" ? Style.error : Style.warning;
                    return contentColor;
                }
                width: (buttons.width - buttons.spacing * (buttons.columns - 1)) / buttons.columns
                height: root.gridLayout ? Math.max(LockMetrics.powerGridButtonHeight, content.implicitHeight + Style.spacingM * 2) : Style.listItemHeight
                isSelected: root.selectedIndex === index
                firstInGroup: root.gridLayout || index === 0
                lastInGroup: root.gridLayout || index === root.actions.length - 1
                focusPolicy: Qt.NoFocus
                Accessible.name: actionData.label || ""
                onPressed: root.actionPressed(index)
                onReleased: root.actionReleased()
                onCanceled: root.actionCanceled()

                ClippingRectangle {
                    anchors.fill: parent
                    color: "transparent"
                    topLeftRadius: button.topLeftRadius
                    topRightRadius: button.topRightRadius
                    bottomLeftRadius: button.bottomLeftRadius
                    bottomRightRadius: button.bottomRightRadius
                    visible: button.holding

                    Rectangle {
                        anchors.left: parent.left
                        height: parent.height
                        width: parent.width * root.holdProgress
                        color: Style.withAlpha(button.tint, Style.stateLayerPressed)
                    }
                }

                Loader {
                    id: content
                    anchors.fill: parent
                    sourceComponent: root.gridLayout ? tileContent : rowContent
                }

                Component {
                    id: rowContent

                    Item {
                        Base.DIcon {
                            id: icon
                            anchors.left: parent.left
                            anchors.leftMargin: Style.spacingL
                            anchors.verticalCenter: parent.verticalCenter
                            name: button.actionData.icon || ""
                            size: Style.iconSizeMedium
                            color: button.tint
                        }

                        Base.StyledText {
                            anchors.left: icon.right
                            anchors.leftMargin: Style.spacingL
                            anchors.right: keycap.visible ? keycap.left : parent.right
                            anchors.rightMargin: Style.spacingL
                            anchors.verticalCenter: parent.verticalCenter
                            text: button.actionData.label || ""
                            textFormat: Text.PlainText
                            font.pixelSize: Style.fontSizeLarge
                            font.weight: Style.fontWeightMedium
                            color: button.tint
                            elide: Text.ElideRight
                        }

                        Base.DKeycap {
                            id: keycap
                            anchors.right: parent.right
                            anchors.rightMargin: Style.spacingL
                            anchors.verticalCenter: parent.verticalCenter
                            text: button.actionData.key || ""
                            textColor: button.tint
                            visible: text.length > 0
                        }
                    }
                }

                Component {
                    id: tileContent

                    Item {
                        implicitHeight: column.implicitHeight

                        Column {
                            id: column
                            anchors.centerIn: parent
                            width: parent.width - Style.spacingM * 2
                            spacing: Style.spacingS

                            Base.DIcon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: button.actionData.icon || ""
                                size: Style.iconSizeLarge
                                color: button.tint
                            }

                            Base.StyledText {
                                width: parent.width
                                text: button.actionData.label || ""
                                textFormat: Text.PlainText
                                font.pixelSize: Style.fontSizeSmall
                                font.weight: Style.fontWeightMedium
                                color: button.tint
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }

                        Base.DKeycap {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: Style.spacingXS
                            text: button.actionData.key || ""
                            textColor: button.tint
                            visible: text.length > 0
                        }
                    }
                }
            }
        }
    }

    Row {
        id: hint
        anchors.top: buttons.bottom
        anchors.topMargin: Style.spacingM
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - Style.spacingL * 2, hintLabel.implicitWidth + hintSymbol.width + spacing)
        spacing: Style.spacingXS
        visible: root.showHint

        Base.DIcon {
            id: hintSymbol
            name: root.hintIcon
            size: Style.iconSizeSmall
            color: root.hintWarning ? Style.warning : Style.onSurfaceVariant
            anchors.verticalCenter: parent.verticalCenter
        }

        Base.StyledText {
            id: hintLabel
            width: parent.width - hintSymbol.width - parent.spacing
            text: root.hintText
            font.pixelSize: Style.fontSizeSmall
            color: root.hintWarning ? Style.warning : Style.onSurfaceVariant
            anchors.verticalCenter: parent.verticalCenter
            wrapMode: Text.WordWrap
        }
    }
}
