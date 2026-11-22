import "../../.."
import "../../../services"
import QtQuick
import QtQuick.Layouts
import Quickshell

ColumnLayout {
    id: root
    spacing: Theme.scaled(20)
    Layout.fillWidth: true

    opacity: 0
    scale: 0.98
    Component.onCompleted: {
        entryAnim.start();
    }
    ParallelAnimation {
        id: entryAnim
        NumberAnimation { target: root; property: "opacity"; to: 1; duration: Theme.animNormal; easing.type: Theme.animEasing }
        NumberAnimation { target: root; property: "scale"; to: 1; duration: Theme.animNormal; easing.type: Theme.elasticEasing; easing.overshoot: 1.05 }
    }

    // Header Row
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.scaled(5)
        Layout.rightMargin: Theme.scaled(5)

        Text {
            text: "POWER PROFILES"
            color: Theme.mauve
            font.pixelSize: Theme.scaled(10)
            font.weight: Font.Black
            font.letterSpacing: 2
            Layout.alignment: Qt.AlignVCenter
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            radius: Theme.scaled(10)
            color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.15)
            border.width: 1
            border.color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.3)
            implicitHeight: Theme.scaled(22)
            implicitWidth: activeLabel.implicitWidth + Theme.scaled(16)

            Text {
                id: activeLabel
                anchors.centerIn: parent
                text: PowerProfileService.currentProfile.toUpperCase()
                color: Theme.accentColor
                font.pixelSize: Theme.scaled(9)
                font.weight: Font.Bold
            }
        }
    }

    // 2x2 Grid of Power Profile Cards
    GridLayout {
        columns: (Theme.isSmallScreen && Theme.isPortrait) ? 1 : 2
        Layout.fillWidth: true
        rowSpacing: Theme.scaled(12)
        columnSpacing: Theme.scaled(12)

        Repeater {
            // No Theme.* values in here on purpose. Embedding them made the whole
            // array a binding on the palette, so every recolour rebuilt all four
            // delegates -- and when that rebuild landed inside a layout pass the
            // layout engine walked an item it had just destroyed and segfaulted.
            // Colours are resolved per delegate instead, updating in place.
            model: [
                { id: "performance", icon: "󰀦", label: "PERFORMANCE", desc: "Max Speed & Power" },
                { id: "balanced",    icon: "󰏤", label: "BALANCED", desc: "Optimal Performance" },
                { id: "powersave",   icon: "󰍛", label: "POWER SAVER", desc: "Extended Battery Life" },
                { id: "turbo",       icon: "󰞃", label: "TURBO", desc: "Peak Boost Mode" }
            ]

            delegate: Rectangle {
                id: profileCard

                readonly property color accent: modelData.id === "performance" ? Theme.powerRed
                                              : modelData.id === "balanced"    ? Theme.blue
                                              : modelData.id === "powersave"   ? Theme.powerGreen
                                              : Theme.powerYellow

                Layout.fillWidth: true
                height: Theme.scaled(82)
                color: PowerProfileService.currentProfile === modelData.id ? 
                       Qt.rgba(profileCard.accent.r, profileCard.accent.g, profileCard.accent.b, 0.20) : 
                       (m.containsMouse ? Theme.cardBackgroundHover : Theme.cardBackground)
                radius: Theme.bubbleRadiusMedium
                border.width: 1
                border.color: PowerProfileService.currentProfile === modelData.id ? profileCard.accent : Theme.glassBorder

                scale: m.pressed ? 0.96 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Theme.elasticEasing; easing.overshoot: 1.15 } }
                Behavior on color { ColorAnimation { duration: Theme.animInstant } }

                MouseArea {
                    id: m
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: PowerProfileService.setProfile(modelData.id)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(12)

                    Rectangle {
                        width: Theme.scaled(44); height: Theme.scaled(44); radius: Theme.bubbleRadiusSmall
                        color: "transparent"
                        border.width: 0

                        Text {
                            anchors.centerIn: parent
                            text: modelData.icon
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(28)
                            color: profileCard.accent
                        }
                    }

                    ColumnLayout {
                        spacing: Theme.scaled(2)
                        Layout.fillWidth: true

                        RowLayout {
                            spacing: Theme.scaled(6)
                            Text {
                                text: modelData.label
                                font.pixelSize: Theme.scaled(11)
                                font.weight: Font.Black
                                color: Theme.text
                            }

                            Rectangle {
                                visible: PowerProfileService.currentProfile === modelData.id
                                width: Theme.scaled(6); height: Theme.scaled(6); radius: 3
                                color: profileCard.accent
                            }
                        }

                        Text {
                            text: modelData.desc
                            font.pixelSize: Theme.scaled(9)
                            color: Theme.subtext0
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // Dynamic Hardware Battery Conservation Mode Card
    Rectangle {
        id: consCard
        visible: Boolean(PowerProfileService.conservativeSupported)
        Layout.fillWidth: true
        height: Theme.scaled(82)
        radius: Theme.bubbleRadiusMedium
        color: PowerProfileService.conservativeActive ? 
               Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.22) : 
               Theme.cardBackground
        border.width: 1
        border.color: PowerProfileService.conservativeActive ? Theme.accentColor : Theme.glassBorder

        Behavior on color { ColorAnimation { duration: Theme.animInstant } }

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.scaled(14)
            spacing: Theme.scaled(14)

            // Icon Badge
            Rectangle {
                width: Theme.scaled(44); height: Theme.scaled(44); radius: Theme.bubbleRadiusSmall
                color: "transparent"
                border.width: 0

                Text {
                    anchors.centerIn: parent
                    text: PowerProfileService.conservativeActive ? "󰂄" : "󰁹"
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.scaled(28)
                    color: Theme.accentColor
                }
            }

            // Info Column
            ColumnLayout {
                spacing: Theme.scaled(2)
                Layout.fillWidth: true

                RowLayout {
                    spacing: Theme.scaled(6)

                    Text {
                        // A refused write used to look exactly like a working
                        // one that changed nothing: the button is pressed, no
                        // password prompt appears because no polkit agent is
                        // running, and the card sits unchanged.
                        text: PowerProfileService.conservativeError !== ""
                              ? "Needs permission"
                              : (PowerProfileService.conservativeLabel || "Battery Care")
                        font.pixelSize: Theme.scaled(11)
                        font.weight: Font.Black
                        color: PowerProfileService.conservativeError !== ""
                               ? Theme.powerRed : Theme.text
                    }

                    Rectangle {
                        radius: Theme.scaled(6)
                        color: "transparent"
                        border.color: Theme.glassBorder
                        border.width: 1
                        implicitHeight: Theme.scaled(16)
                        implicitWidth: brandText.implicitWidth + Theme.scaled(10)

                        Text {
                            id: brandText
                            anchors.centerIn: parent
                            text: String(PowerProfileService.conservativeBrand || "Battery Care").toUpperCase()
                            color: Theme.accentColor
                            font.pixelSize: Theme.scaled(7)
                            font.weight: Font.Bold
                        }
                    }
                }

                Text {
                    text: PowerProfileService.conservativeActive ? 
                          "Charge capped to ~60-80% for maximum lifespan" : 
                          "Charges to 100% (Click switch to enable protection)"
                    font.pixelSize: Theme.scaled(9)
                    color: Theme.subtext0
                    elide: Text.ElideRight
                }
            }

            // Interactive Toggle Button
            Rectangle {
                id: consBtn
                width: Theme.scaled(84)
                height: Theme.scaled(36)
                radius: Theme.bubbleRadiusSmall
                color: PowerProfileService.conservativeActive ? Theme.accentColor : (btnMouse.containsMouse ? Theme.menuBackground2Hover : Theme.menuBackground2)
                border.width: 1
                border.color: PowerProfileService.conservativeActive ? Theme.accentColor : Theme.glassBorder

                scale: btnMouse.pressed ? 0.95 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: Theme.animInstant } }

                RowLayout {
                    anchors.centerIn: parent
                    spacing: Theme.scaled(6)

                    Text {
                        text: PowerProfileService.conservativeActive ? "󰄬" : "󰅖"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(12)
                        color: PowerProfileService.conservativeActive ? Theme.base : Theme.text
                    }

                    Text {
                        text: PowerProfileService.conservativeActive ? "ON" : "OFF"
                        color: PowerProfileService.conservativeActive ? Theme.base : Theme.text
                        font.pixelSize: Theme.scaled(11)
                        font.weight: Font.Black
                    }
                }

                MouseArea {
                    id: btnMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: PowerProfileService.toggleConservativeMode()
                }
            }
        }
    }

    Item { Layout.fillHeight: true }
}