pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

Rectangle {
    id: root

    property string currentIcon: ""
    property string iconType: "icon"

    signal iconSelected(string iconName, string iconType)

    readonly property real windowWidth: root.Window.window?.width ?? Infinity
    readonly property real windowHeight: root.Window.window?.height ?? Infinity
    readonly property string currentLabel: root.currentIcon ? root.currentIcon.charAt(0).toUpperCase() + root.currentIcon.slice(1).replace(/_/g, " ") : I18n.tr("Choose icon")
    readonly property string query: searchField.text.trim().toLowerCase().replace(/\s+/g, "_")

    width: Math.min(240, windowWidth - Style.spacingL * 2)
    height: Style.buttonHeightS
    radius: Style.pillControls ? Style.fullRadius(width, height) : Style.cornerRadiusL
    color: triggerArea.containsMouse ? Style.surfaceHover : Style.chipSurface
    border.color: iconPopup.visible ? Style.primary : Style.outlineVariant
    border.width: Style.outlineWidth

    Behavior on color {
        ColorAnimation {
            duration: Style.shortDuration
            easing.type: Style.standardEasing
        }
    }

    property var iconCategories: [
        {
            "name": I18n.tr("Numbers"),
            "icons": ["looks_one", "looks_two", "looks_3", "looks_4", "looks_5", "looks_6", "filter_1", "filter_2", "filter_3", "filter_4", "filter_5", "filter_6", "filter_7", "filter_8", "filter_9", "filter_9_plus", "plus_one", "exposure_plus_1", "exposure_plus_2"]
        },
        {
            "name": I18n.tr("Workspace"),
            "icons": ["work", "laptop", "desktop_windows", "folder", "view_module", "dashboard", "apps", "grid_view"]
        },
        {
            "name": I18n.tr("Development"),
            "icons": ["code", "terminal", "bug_report", "build", "engineering", "integration_instructions", "data_object", "schema", "api", "webhook"]
        },
        {
            "name": I18n.tr("Communication"),
            "icons": ["chat", "mail", "forum", "message", "video_call", "call", "contacts", "group", "notifications", "campaign"]
        },
        {
            "name": I18n.tr("Media"),
            "icons": ["music_note", "headphones", "mic", "videocam", "photo", "movie", "library_music", "album", "radio", "volume_up"]
        },
        {
            "name": I18n.tr("System"),
            "icons": ["memory", "storage", "developer_board", "monitor", "keyboard", "mouse", "battery_std", "wifi", "bluetooth", "security", "settings"]
        },
        {
            "name": I18n.tr("Navigation"),
            "icons": ["home", "arrow_forward", "arrow_back", "expand_more", "expand_less", "menu", "close", "search", "filter_list", "sort"]
        },
        {
            "name": I18n.tr("Actions"),
            "icons": ["add", "remove", "edit", "delete", "save", "download", "upload", "share", "content_copy", "content_paste", "content_cut", "undo", "redo"]
        },
        {
            "name": I18n.tr("Status"),
            "icons": ["check", "error", "warning", "info", "done", "pending", "schedule", "update", "sync", "offline_bolt"]
        },
        {
            "name": I18n.tr("Fun"),
            "icons": ["celebration", "cake", "star", "favorite", "pets", "sports_esports", "local_fire_department", "bolt", "auto_awesome", "diamond", "blur_on"]
        }
    ]

    MouseArea {
        id: triggerArea

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (iconPopup.visible) {
                iconPopup.close();
                return;
            }
            const pos = root.mapToItem(Overlay.overlay, 0, 0);
            const overlayWidth = Overlay.overlay?.width ?? Infinity;
            const overlayHeight = Overlay.overlay?.height ?? 800;
            const margin = Style.spacingM;
            // Trigger sits at the row's trailing edge, so align the popup's right edge to it and keep it on screen.
            iconPopup.x = Math.max(margin, Math.min(pos.x + root.width - iconPopup.width, overlayWidth - iconPopup.width - margin));
            const below = pos.y + root.height + Style.spacingXS;
            const y = below + iconPopup.height > overlayHeight - margin ? pos.y - iconPopup.height - Style.spacingXS : below;
            iconPopup.y = Math.max(margin, Math.min(y, overlayHeight - iconPopup.height - margin));
            iconPopup.open();
        }
    }

    Rectangle {
        id: avatar

        readonly property real inset: Style.spacingXS + Style.outlineWidth

        anchors.left: parent.left
        anchors.leftMargin: inset
        anchors.verticalCenter: parent.verticalCenter
        width: root.height - inset * 2
        height: width
        radius: width / 2
        color: root.currentIcon ? Style.primaryContainer : Style.surfaceContainerHighest

        DIcon {
            anchors.centerIn: parent
            name: root.currentIcon || "add"
            size: Style.iconSizeSmall
            filled: root.currentIcon !== ""
            color: root.currentIcon ? Style.onPrimaryContainer : Style.outline
        }
    }

    StyledText {
        anchors.left: avatar.right
        anchors.leftMargin: Style.spacingS
        anchors.right: chevron.left
        anchors.rightMargin: Style.spacingS
        anchors.verticalCenter: parent.verticalCenter
        text: root.currentLabel
        font.pixelSize: Style.fontSizeSmall
        font.weight: Style.fontWeightMedium
        color: root.currentIcon ? Style.surfaceText : Style.outline
        elide: Text.ElideRight
    }

    DIcon {
        id: chevron

        name: iconPopup.visible ? "expand_less" : "expand_more"
        size: Style.iconSizeSmall
        color: Style.outline
        anchors.right: parent.right
        anchors.rightMargin: Style.spacingM
        anchors.verticalCenter: parent.verticalCenter
    }

    Popup {
        id: iconPopup

        parent: root.Overlay.overlay
        width: Math.min(360, root.windowWidth - Style.spacingL * 2)
        height: Math.min(520, root.windowHeight - Style.spacingL * 2, dropdownContent.height + searchRow.height + Style.spacingM * 3)
        padding: 0
        modal: true
        dim: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: searchField.forceActiveFocus()
        onClosed: searchField.text = ""

        background: Rectangle {
            color: "transparent"
        }

        contentItem: Rectangle {
            id: contentSurface

            color: Style.cardSurface
            border.width: Style.layerOutlineWidth
            border.color: Style.outlineMedium
            radius: Style.windowRadius

            ElevationShadow {
                anchors.fill: parent
                z: -1
                level: Style.elevationLevel2
                fallbackOffset: 4
                targetRadius: contentSurface.radius
                targetColor: contentSurface.color
                shadowOpacity: Style.elevationLevel2 && Style.elevationLevel2.alpha !== undefined ? Style.elevationLevel2.alpha : 0.25
                shadowEnabled: Style.elevationEnabled
            }

            Column {
                anchors.fill: parent
                anchors.margins: Style.spacingM
                spacing: Style.spacingM

                Item {
                    id: searchRow

                    width: parent.width
                    height: searchField.height

                    DSearchField {
                        id: searchField

                        anchors.left: parent.left
                        anchors.right: closeButton.left
                        anchors.rightMargin: Style.spacingS
                        anchors.verticalCenter: parent.verticalCenter
                        placeholderText: I18n.tr("Search...")
                    }

                    DActionButton {
                        id: closeButton

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "close"
                        Accessible.name: I18n.tr("Close")
                        onClicked: iconPopup.close()
                    }
                }

                DFlickable {
                    width: parent.width
                    height: parent.height - searchRow.height - parent.spacing
                    contentHeight: dropdownContent.height
                    clip: true
                    pressDelay: 0

                    Column {
                        id: dropdownContent

                        width: parent.width
                        spacing: Style.spacingM

                        Repeater {
                            model: root.iconCategories

                            Column {
                                id: category

                                required property var modelData
                                readonly property var shownIcons: root.query ? category.modelData.icons.filter(icon => icon.includes(root.query)) : category.modelData.icons

                                width: parent.width
                                spacing: Style.spacingS
                                visible: shownIcons.length > 0

                                StyledText {
                                    text: category.modelData.name
                                    font.pixelSize: Style.fontSizeSmall
                                    font.weight: Style.fontWeightMedium
                                    color: Style.primary
                                }

                                Flow {
                                    width: parent.width
                                    spacing: Style.spacingXS

                                    Repeater {
                                        model: category.shownIcons

                                        Rectangle {
                                            id: tile

                                            required property string modelData
                                            readonly property bool selected: root.currentIcon === modelData

                                            width: Style.buttonHeightS
                                            height: Style.buttonHeightS
                                            radius: tile.selected && Style.shapeMorph ? tile.width / 2 : Style.cornerRadiusS
                                            color: tile.selected ? Style.primaryContainer : iconMouseArea.containsMouse ? Style.surfaceHover : Style.withAlpha(Style.surfaceHover, 0)

                                            DIcon {
                                                anchors.centerIn: parent
                                                name: tile.modelData
                                                size: Style.iconSize
                                                filled: tile.selected
                                                color: tile.selected ? Style.onPrimaryContainer : Style.surfaceText
                                            }

                                            MouseArea {
                                                id: iconMouseArea

                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    root.iconSelected(tile.modelData, "icon");
                                                    iconPopup.close();
                                                }
                                            }

                                            Behavior on radius {
                                                enabled: !Style.reduceMotion
                                                DAnim {
                                                    duration: Style.expressiveDurations.expressiveEffects
                                                    easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                                                }
                                            }

                                            Behavior on color {
                                                ColorAnimation {
                                                    duration: Style.shortDuration
                                                    easing.type: Style.standardEasing
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    function setIcon(iconName, type) {
        root.iconType = "icon";
        root.currentIcon = iconName;
    }
}
