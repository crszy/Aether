import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell
import Quickshell.Io
import "../../../"
import "../../../services"
import "../../../Settings"

ColumnLayout {
    id: root
    spacing: Theme.scaled(14)
    Layout.fillWidth: true
    Layout.fillHeight: true

    property string searchQuery: ""
    readonly property var filteredPresets: {
        let q = searchQuery.toLowerCase().trim();
        if (!q) return ThemeService.presets;
        return ThemeService.presets.filter(function(p) {
            let name = (p.name || "").toLowerCase();
            let id = (p.id || "").toLowerCase();
            let cat = (p.category || "").toLowerCase();
            return name.includes(q) || id.includes(q) || cat.includes(q);
        });
    }

    // --- ACCENT COLOR PRESETS ---
    readonly property var accentOptions: [
        { name: "Default", color: "" },
        { name: "Mauve", color: "#cba6f7" },
        { name: "Sapphire", color: "#74c7ec" },
        { name: "Teal", color: "#94e2d5" },
        { name: "Green", color: "#a6e3a1" },
        { name: "Peach", color: "#fab387" },
        { name: "Flamingo", color: "#f2cdcd" },
        { name: "Rose", color: "#ebbcba" },
        { name: "Red", color: "#f38ba8" },
        { name: "Yellow", color: "#f9e2af" },
        { name: "Electric Cyan", color: "#00f0ff" }
    ]

    // --- TOP BAR: MODE SWITCHER & ACTIVE THEME BADGE ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.scaled(12)

        // Pill Switcher
        Rectangle {
            height: Theme.scaled(36)
            width: Theme.scaled(280)
            radius: height / 2
            color: Theme.cardBackground
            border.width: 0

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(3)
                spacing: Theme.scaled(4)

                // Dynamic Mode Tab
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: height / 2
                    color: ThemeService.themeMode === "dynamic" ? Theme.accentColor : (dynMouse.containsMouse ? Theme.menuDarkHover : "transparent")
                    scale: dynMouse.pressed ? 0.95 : 1.0

                    Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                    Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(6)

                        Text {
                            text: "󰔎"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(18)
                            color: ThemeService.themeMode === "dynamic" ? Colors.on_primary : Theme.text
                        }
                        Text {
                            text: "WALLPAPER"
                            font.pixelSize: Theme.scaled(10.5)
                            font.weight: Font.Black
                            font.letterSpacing: 1
                            color: ThemeService.themeMode === "dynamic" ? Colors.on_primary : Theme.text
                        }
                    }

                    MouseArea {
                        id: dynMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ThemeService.applyDynamic()
                    }
                }

                // Presets Mode Tab
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: height / 2
                    color: ThemeService.themeMode === "preset" ? Theme.accentColor : (presetMouse.containsMouse ? Theme.menuDarkHover : "transparent")
                    scale: presetMouse.pressed ? 0.95 : 1.0

                    Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                    Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(6)

                        Text {
                            text: "󰏘"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(18)
                            color: ThemeService.themeMode === "preset" ? Colors.on_primary : Theme.text
                        }
                        Text {
                            text: "PRESETS"
                            font.pixelSize: Theme.scaled(10.5)
                            font.weight: Font.Black
                            font.letterSpacing: 1
                            color: ThemeService.themeMode === "preset" ? Colors.on_primary : Theme.text
                        }
                    }

                    MouseArea {
                        id: presetMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (ThemeService.themeMode !== "preset") {
                                ThemeService.applyPreset(ThemeService.currentPreset);
                            }
                        }
                    }
                }
            }
        }

        // Search Input Capsule (Visible in Preset Mode)
        Rectangle {
            visible: ThemeService.themeMode === "preset" && !Theme.isSmallScreen
            Layout.preferredWidth: Theme.scaled(180)
            height: Theme.scaled(36)
            radius: height / 2
            color: "transparent"
            border.color: searchInput.activeFocus ? Theme.accentColor : Theme.glassBorder
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.scaled(10)
                anchors.rightMargin: Theme.scaled(10)
                spacing: Theme.scaled(6)

                Text {
                    text: "󰍉"
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.scaled(13)
                    color: Theme.accentColor
                    Layout.alignment: Qt.AlignVCenter
                }

                TextInput {
                    id: searchInput
                    Layout.fillWidth: true
                    font.pixelSize: Theme.scaled(11)
                    color: "#ffffff"
                    clip: true
                    onTextChanged: root.searchQuery = text

                    Text {
                        anchors.fill: parent
                        text: "Filter themes..."
                        color: Qt.rgba(255, 255, 255, 0.35)
                        font.pixelSize: Theme.scaled(11)
                        visible: !searchInput.text && !searchInput.activeFocus
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        // Active State Badge
        Rectangle {
            height: Theme.scaled(36)
            width: activeRow.implicitWidth + Theme.scaled(20)
            radius: height / 2
            color: "transparent"
            border.color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.4)
            border.width: 1

            RowLayout {
                id: activeRow
                anchors.centerIn: parent
                spacing: Theme.scaled(8)

                Rectangle {
                    width: Theme.scaled(8)
                    height: Theme.scaled(8)
                    radius: 999
                    color: Theme.accentColor

                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 0.4; duration: 1000 }
                        NumberAnimation { from: 0.4; to: 1.0; duration: 1000 }
                    }
                }

                Text {
                    text: ThemeService.themeMode === "dynamic" ? "DYNAMIC MATUGEN" : ThemeService.currentPreset.toUpperCase().replace("-", " ")
                    font.pixelSize: Theme.scaled(10)
                    font.weight: Font.Bold
                    font.letterSpacing: 0.5
                    color: Theme.text
                }
            }
        }
    }

    // --- MAIN CONTENT AREA: PRESET GRID OR DYNAMIC WALLPAPER HERO ---
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: Theme.scaled(18)
        color: Qt.rgba(0, 0, 0, 0.25)
        border.color: Theme.glassBorder
        border.width: 1
        clip: true

        // --- PRESET THEMES GRID ---
        ScrollView {
            id: presetScrollView
            anchors.fill: parent
            anchors.margins: Theme.scaled(12)
            visible: ThemeService.themeMode === "preset"
            clip: true

            FastWheel {}

            ScrollBar.vertical: ScrollBar {
                parent: presetScrollView
                anchors.top: presetScrollView.top
                anchors.bottom: presetScrollView.bottom
                anchors.right: presetScrollView.right
                width: 3
                policy: ScrollBar.AsNeeded
                contentItem: Rectangle { radius: 999; color: Theme.accentColor; opacity: 0.6 }
            }

            GridLayout {
                width: presetScrollView.availableWidth
                columns: Theme.isSmallScreen ? 1 : 2
                columnSpacing: Theme.scaled(12)
                rowSpacing: Theme.scaled(12)

                Repeater {
                    model: root.filteredPresets

                    delegate: Rectangle {
                        id: card
                        Layout.fillWidth: true
                        height: Theme.scaled(80)
                        radius: Theme.bubbleRadiusMedium

                        readonly property bool isSelected: (ThemeService.themeMode === "preset" && ThemeService.currentPreset === modelData.id)

                        color: isSelected
                            ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.20)
                            : (cardMouse.containsMouse ? Theme.cardBackgroundHover : Theme.cardBackground)

                        border.color: isSelected
                            ? Theme.accentColor
                            : (cardMouse.containsMouse ? Qt.rgba(255, 255, 255, 0.25) : Theme.glassBorder)
                        border.width: 1

                        scale: cardMouse.pressed ? 0.97 : 1.0
                        Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
                        Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                        Behavior on border.color { ColorAnimation { duration: Theme.animInstant } }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(12)
                            spacing: Theme.scaled(12)

                            // 4-Color Swatch Pill
                            Rectangle {
                                width: Theme.scaled(44)
                                height: Theme.scaled(44)
                                radius: Theme.bubbleRadiusSmall
                                color: "transparent"
                                border.width: 0

                                GridLayout {
                                    anchors.centerIn: parent
                                    columns: 2
                                    columnSpacing: Theme.scaled(4)
                                    rowSpacing: Theme.scaled(4)

                                    Rectangle { width: Theme.scaled(12); height: Theme.scaled(12); radius: 999; color: modelData.surface || "#181825" }
                                    Rectangle { width: Theme.scaled(12); height: Theme.scaled(12); radius: 999; color: modelData.primary || "#cba6f7" }
                                    Rectangle { width: Theme.scaled(12); height: Theme.scaled(12); radius: 999; color: modelData.secondary || "#89b4fa" }
                                    Rectangle { width: Theme.scaled(12); height: Theme.scaled(12); radius: 999; color: modelData.background || "#1e1e2e" }
                                }
                            }

                            // Theme Details
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(3)

                                Text {
                                    text: modelData.name || "Theme"
                                    font.pixelSize: Theme.scaled(13)
                                    font.weight: Font.Bold
                                    color: card.isSelected ? Theme.accentColor : Theme.text
                                }

                                Text {
                                    text: modelData.category || "Dark Theme"
                                    font.pixelSize: Theme.scaled(10)
                                    color: Theme.subtext0
                                }
                            }

                            // Active Checkmark Badge
                            Rectangle {
                                width: Theme.scaled(28)
                                height: Theme.scaled(28)
                                radius: 999
                                color: card.isSelected ? Theme.accentColor : "transparent"
                                border.width: 0

                                Text {
                                    anchors.centerIn: parent
                                    text: card.isSelected ? "󰄬" : ""
                                    font.family: Theme.iconFont
                                    font.pixelSize: Theme.scaled(18)
                                    font.bold: true
                                    color: Colors.on_primary
                                }
                            }
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ThemeService.applyPreset(modelData.id)
                        }
                    }
                }
            }
        }

        // --- DYNAMIC WALLPAPER VIEW ---
        ColumnLayout {
            anchors.centerIn: parent
            spacing: Theme.scaled(14)
            visible: ThemeService.themeMode === "dynamic"

            Rectangle {
                width: Theme.scaled(72)
                height: Theme.scaled(72)
                radius: 999
                color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.15)
                border.color: Theme.accentColor
                border.width: 2
                Layout.alignment: Qt.AlignHCenter

                Text {
                    anchors.centerIn: parent
                    text: "󰔎"
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.scaled(32)
                    color: Theme.accentColor
                }
            }

            Text {
                text: "Material 3 Wallpaper Palette"
                font.pixelSize: Theme.scaled(17)
                font.weight: Font.Black
                color: Theme.text
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: "Colors dynamically harmonize with your current wallpaper\nLive-recolored across the shell, bar, and Hyprland without restart."
                font.pixelSize: Theme.scaled(11.5)
                color: Theme.subtext0
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignHCenter
            }

            RowLayout {
                spacing: Theme.scaled(12)
                Layout.alignment: Qt.AlignHCenter

                // Re-extract Button
                Rectangle {
                    width: reextractText.implicitWidth + Theme.scaled(32)
                    height: Theme.scaled(38)
                    radius: height / 2
                    color: Theme.accentColor
                    scale: reextractMouse.pressed ? 0.95 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(8)

                        Text {
                            text: "󰑐"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(14)
                            color: Colors.on_primary
                        }
                        Text {
                            id: reextractText
                            text: "Re-extract Wallpaper Palette"
                            font.pixelSize: Theme.scaled(11)
                            font.weight: Font.Bold
                            color: Colors.on_primary
                        }
                    }

                    MouseArea {
                        id: reextractMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ThemeService.applyDynamic()
                    }
                }

                // Go to Wallpaper Tab Button
                Rectangle {
                    width: wallTabBtnText.implicitWidth + Theme.scaled(32)
                    height: Theme.scaled(38)
                    radius: height / 2
                    color: wallTabMouse.containsMouse ? Theme.menuBackground2Hover : Theme.menuBackground2
                    border.color: Theme.glassBorder
                    border.width: 1
                    scale: wallTabMouse.pressed ? 0.95 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(8)

                        Text {
                            text: "󰉏"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(14)
                            color: Theme.text
                        }
                        Text {
                            id: wallTabBtnText
                            text: "Wallpaper Gallery"
                            font.pixelSize: Theme.scaled(11)
                            font.weight: Font.Bold
                            color: Theme.text
                        }
                    }

                    MouseArea {
                        id: wallTabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: CenterState.wallpaperSubTab = 0
                    }
                }
            }
        }
    }

    // --- ACCENT COLOR QUICK-PICKER BAR ---
    Rectangle {
        Layout.fillWidth: true
        height: Theme.scaled(58)
        radius: Theme.bubbleRadiusMedium
        color: Theme.menuBackground2
        border.color: Theme.glassBorder
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.scaled(16)
            anchors.rightMargin: Theme.scaled(16)
            spacing: Theme.scaled(12)

            RowLayout {
                spacing: Theme.scaled(6)
                Text {
                    text: "󰈊"
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.scaled(14)
                    color: Theme.accentColor
                }
                Text {
                    text: "ACCENT"
                    font.pixelSize: Theme.scaled(10)
                    font.weight: Font.Black
                    font.letterSpacing: 1
                    color: Theme.subtext0
                }
            }

            Item { Layout.fillWidth: true }

            RowLayout {
                spacing: Theme.scaled(8)

                Repeater {
                    model: root.accentOptions

                    delegate: Rectangle {
                        id: accDot
                        width: Theme.scaled(28)
                        height: Theme.scaled(28)
                        radius: 999

                        readonly property bool isSelected: (modelData.color === "" && ThemeService.accentColor === "") || (modelData.color !== "" && ThemeService.accentColor.toLowerCase() === modelData.color.toLowerCase())

                        color: modelData.color !== "" ? modelData.color : Qt.rgba(255, 255, 255, 0.2)
                        border.color: isSelected ? "#ffffff" : Theme.glassBorder
                        border.width: isSelected ? 2 : 1

                        scale: accMouse.pressed ? 0.92 : 1.0
                        Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Theme.elasticEasing; easing.overshoot: 1.15 } }

                        Text {
                            anchors.centerIn: parent
                            visible: modelData.color === ""
                            text: "↺"
                            font.pixelSize: Theme.scaled(13)
                            font.bold: true
                            color: Theme.text
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: modelData.color !== "" && accDot.isSelected
                            text: "✓"
                            font.pixelSize: Theme.scaled(12)
                            font.bold: true
                            color: "#11111b"
                        }

                        MouseArea {
                            id: accMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ThemeService.setAccent(modelData.color)
                        }
                    }
                }
            }
        }
    }
}
