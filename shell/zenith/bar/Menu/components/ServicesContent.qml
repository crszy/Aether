// bar/Menu/components/ServicesContent.qml
//
// Professional & Modern App-Level Services Controller.
// Controls primary daemons and cascades actions to all sub-services.
// Clean layout with flush right-aligned controls, memory tracking, and expandable filtered logs.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell
import Quickshell.Io
import "../../../"
import "../../../services"
import "../../../Settings"

Item {
    id: root

    property string searchQuery: ""
    property string activeCategoryFilter: "All"
    property string expandedServiceId: ""
    property string logFilterLevel: "All"

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.scaled(12)

        // --- TOP TOOLBAR: SEARCH, STATS & REFRESH ---
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(10)

            // Search Bar
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(38)
                radius: Theme.bubbleRadiusMedium
                color: "transparent"
                border.color: searchInput.activeFocus ? Theme.accentColor : Theme.glassBorder
                border.width: 1

                Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.scaled(12)
                    anchors.rightMargin: Theme.scaled(10)
                    spacing: Theme.scaled(8)

                    Text {
                        text: "󰍉"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(15)
                        color: searchInput.activeFocus ? Theme.accentColor : Theme.subtext0
                    }

                    TextInput {
                        id: searchInput
                        Layout.fillWidth: true
                        font.pixelSize: Theme.scaled(12)
                        font.bold: true
                        color: "#ffffff"
                        clip: true
                        verticalAlignment: TextInput.AlignVCenter
                        onTextChanged: root.searchQuery = text

                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            text: "Search main app services (docker, samba, audio, bluetooth)..."
                            color: Qt.rgba(255, 255, 255, 0.35)
                            font.pixelSize: Theme.scaled(11)
                            visible: !searchInput.text && !searchInput.activeFocus
                        }
                    }

                    // Clear button
                    Rectangle {
                        width: Theme.scaled(20); height: Theme.scaled(20); radius: 999
                        color: clearMouse.containsMouse ? Qt.rgba(255, 255, 255, 0.2) : "transparent"
                        visible: searchInput.text.length > 0

                        Text {
                            anchors.centerIn: parent
                            text: "󰅖"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(11)
                            color: "#ffffff"
                        }

                        MouseArea {
                            id: clearMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { searchInput.text = ""; root.searchQuery = ""; }
                        }
                    }
                }
            }

            // Stats Pill
            Rectangle {
                Layout.preferredHeight: Theme.scaled(38)
                implicitWidth: statsRow.implicitWidth + Theme.scaled(20)
                radius: Theme.bubbleRadiusMedium
                color: "transparent"
                border.color: Theme.glassBorder
                border.width: 1

                RowLayout {
                    id: statsRow
                    anchors.centerIn: parent
                    spacing: Theme.scaled(10)

                    RowLayout {
                        spacing: Theme.scaled(4)
                        Rectangle { width: Theme.scaled(7); height: Theme.scaled(7); radius: 999; color: "#a6e3a1" }
                        Text {
                            text: SystemServicesService.runningCount + " Active"
                            color: "#a6e3a1"
                            font.pixelSize: Theme.scaled(11)
                            font.bold: true
                        }
                    }

                    Rectangle { width: 1; height: Theme.scaled(14); color: Qt.rgba(255, 255, 255, 0.2) }

                    RowLayout {
                        spacing: Theme.scaled(4)
                        Text { text: "󰍛"; font.family: Theme.iconFont; font.pixelSize: Theme.scaled(12); color: Theme.accentColor }
                        Text {
                            text: SystemServicesService.totalMemoryFormatted
                            color: "#ffffff"
                            font.pixelSize: Theme.scaled(11)
                            font.bold: true
                        }
                    }
                }
            }

            // Refresh Button
            Rectangle {
                id: refreshBtn
                width: Theme.scaled(38); height: Theme.scaled(38)
                radius: 999
                color: refreshMouse.containsMouse ? Theme.menuBackground2Hover : Theme.menuBackground2
                border.color: Theme.glassBorder
                border.width: 1
                scale: refreshMouse.pressed ? 0.94 : 1.0

                Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: Theme.animInstant } }

                Text {
                    id: refreshIcon
                    anchors.centerIn: parent
                    text: "󰑐"
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.scaled(16)
                    color: SystemServicesService.refreshing ? Theme.accentColor : "#ffffff"

                    RotationAnimation on rotation {
                        from: 0; to: 360; duration: 600
                        loops: Animation.Infinite
                        running: SystemServicesService.refreshing
                    }
                }

                MouseArea {
                    id: refreshMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: SystemServicesService.refresh()
                }

                ToolTip {
                    visible: refreshMouse.containsMouse
                    delay: 350
                    contentItem: Text { text: "Refresh Daemons & Sub-services"; color: "#ffffff"; font.pixelSize: Theme.scaled(10.5) }
                    background: Rectangle { color: Qt.rgba(20/255, 20/255, 30/255, 0.95); radius: 6; border.color: Theme.glassBorder; border.width: 1 }
                }
            }
        }

        // --- CATEGORY FILTER CHIPS ---
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(6)

            Repeater {
                model: ["All", "Running", "Stopped", "Containers", "AI & Dev", "Sharing & Network", "Virtualization", "Audio"]
                delegate: Rectangle {
                    id: chipRect
                    implicitWidth: chipText.implicitWidth + Theme.scaled(16)
                    height: Theme.scaled(26)
                    radius: Theme.bubbleRadiusPill
                    color: root.activeCategoryFilter === modelData ? Theme.accentColor : (chipMouse.containsMouse ? Theme.cardBackgroundHover : "transparent")
                    border.width: 0
                    scale: chipMouse.pressed ? 0.94 : 1.0

                    Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
                    Behavior on color { ColorAnimation { duration: Theme.animInstant } }

                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: Theme.scaled(10.5)
                        font.bold: true
                        color: root.activeCategoryFilter === modelData ? Colors.on_primary : (chipMouse.containsMouse ? "#ffffff" : Theme.subtext0)
                    }

                    MouseArea {
                        id: chipMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activeCategoryFilter = modelData
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }

        // Action Status Message Banner
        Rectangle {
            Layout.fillWidth: true
            height: Theme.scaled(24)
            radius: Theme.bubbleRadiusSmall
            color: Theme.menuBackground2
            border.color: Theme.glassBorder
            border.width: 1
            visible: SystemServicesService.lastActionMessage !== ""

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.scaled(10)
                anchors.rightMargin: Theme.scaled(10)
                spacing: Theme.scaled(6)

                Text { text: "󰋽"; font.family: Theme.iconFont; color: Theme.accentColor; font.pixelSize: Theme.scaled(12) }
                Text {
                    text: SystemServicesService.lastActionMessage
                    color: "#ffffff"
                    font.pixelSize: Theme.scaled(10)
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: "󰅖"
                    font.family: Theme.iconFont
                    color: Theme.subtext0
                    font.pixelSize: Theme.scaled(11)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: SystemServicesService.lastActionMessage = ""
                    }
                }
            }
        }

        // --- MAIN APP SERVICES LIST ---
        ScrollView {
            id: servicesScrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ListView {
                id: servicesListView
                width: servicesScrollView.width
                spacing: Theme.scaled(8)
                boundsBehavior: Flickable.StopAtBounds

                model: root.filteredServices()

                delegate: Rectangle {
                    id: serviceCard
                    width: servicesListView.width - Theme.scaled(6)
                    height: isExpanded ? (cardContentCol.implicitHeight + Theme.scaled(20)) : Theme.scaled(58)
                    radius: Theme.bubbleRadiusMedium
                    color: isExpanded ? Theme.cardBackgroundHover : (itemHoverMouse.containsMouse ? Theme.cardBackgroundHover : Theme.cardBackground)
                    border.color: isExpanded ? Theme.accentColor : (modelData.isRunning ? Qt.rgba(166/255, 227/255, 161/255, 0.35) : Theme.glassBorder)
                    border.width: 1
                    clip: true

                    readonly property bool isExpanded: root.expandedServiceId === modelData.id
                    readonly property bool isBusy: SystemServicesService.actionInProgress.indexOf(modelData.id) !== -1

                    Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                    Behavior on border.color { ColorAnimation { duration: Theme.animInstant } }
                    Behavior on height { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }

                    MouseArea {
                        id: itemHoverMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        propagateComposedEvents: true
                        onClicked: (mouse) => {
                            if (root.expandedServiceId === modelData.id) {
                                root.expandedServiceId = "";
                                SystemServicesService.clearLogs();
                            } else {
                                root.expandedServiceId = modelData.id;
                                SystemServicesService.fetchLogs(modelData.scope, modelData.id);
                            }
                        }
                    }

                    ColumnLayout {
                        id: cardContentCol
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: Theme.scaled(10)
                        anchors.leftMargin: Theme.scaled(12)
                        anchors.rightMargin: Theme.scaled(12)
                        spacing: Theme.scaled(10)

                        // --- SERVICE HEADER ROW (FLUSH ALIGNED) ---
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(12)

                            // Left App Icon
                            Rectangle {
                                width: Theme.scaled(38); height: Theme.scaled(38)
                                radius: Theme.scaled(10)
                                color: modelData.isRunning ? Qt.rgba(166/255, 227/255, 161/255, 0.15) : (modelData.activeState === "failed" ? Qt.rgba(243/255, 139/255, 168/255, 0.15) : Qt.rgba(255, 255, 255, 0.08))
                                border.width: 0

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.icon || "󰒍"
                                    font.family: Theme.iconFont
                                    font.pixelSize: Theme.scaled(24)
                                    color: modelData.isRunning ? "#a6e3a1" : (modelData.activeState === "failed" ? "#f38ba8" : "#ffffff")
                                }
                            }

                            // App Title, Badges & Unit ID (Clean, No verbose descriptions)
                            ColumnLayout {
                                spacing: Theme.scaled(2)

                                RowLayout {
                                    spacing: Theme.scaled(8)

                                    Text {
                                        text: modelData.name
                                        color: "#ffffff"
                                        font.pixelSize: Theme.scaled(13)
                                        font.bold: true
                                    }

                                    // Scope Badge (System / User)
                                    Rectangle {
                                        height: Theme.scaled(16)
                                        implicitWidth: scopeText.implicitWidth + Theme.scaled(8)
                                        radius: Theme.scaled(8)
                                        color: modelData.scope === "system" ? Qt.rgba(137/255, 180/255, 250/255, 0.2) : Qt.rgba(203/255, 166/255, 247/255, 0.2)
                                        border.color: modelData.scope === "system" ? "#89b4fa" : "#cba6f7"
                                        border.width: 1

                                        Text {
                                            id: scopeText
                                            anchors.centerIn: parent
                                            text: modelData.scope.toUpperCase()
                                            font.pixelSize: Theme.scaled(8)
                                            font.bold: true
                                            color: modelData.scope === "system" ? "#89b4fa" : "#cba6f7"
                                        }
                                    }

                                    // Category Badge
                                    Rectangle {
                                        height: Theme.scaled(16)
                                        implicitWidth: catText.implicitWidth + Theme.scaled(8)
                                        radius: Theme.scaled(8)
                                        color: Qt.rgba(255, 255, 255, 0.08)

                                        Text {
                                            id: catText
                                            anchors.centerIn: parent
                                            text: modelData.category
                                            font.pixelSize: Theme.scaled(8)
                                            font.weight: Font.DemiBold
                                            color: Theme.subtext0
                                        }
                                    }
                                }

                                // Secondary Line: Main Unit + Sub-services Tag + RAM
                                RowLayout {
                                    spacing: Theme.scaled(6)

                                    Text {
                                        text: modelData.id
                                        color: Qt.rgba(255, 255, 255, 0.5)
                                        font.pixelSize: Theme.scaled(10)
                                        font.family: "monospace"
                                    }

                                    // Sub-services Count Badge
                                    Rectangle {
                                        visible: modelData.unitCount > 1
                                        height: Theme.scaled(14)
                                        implicitWidth: subBadgeText.implicitWidth + Theme.scaled(8)
                                        radius: Theme.scaled(7)
                                        color: Qt.rgba(203/255, 166/255, 247/255, 0.15)
                                        border.color: Qt.rgba(203/255, 166/255, 247/255, 0.3)
                                        border.width: 1

                                        Text {
                                            id: subBadgeText
                                            anchors.centerIn: parent
                                            text: "+" + (modelData.unitCount - 1) + " sub-daemons"
                                            color: "#cba6f7"
                                            font.pixelSize: Theme.scaled(8.5)
                                            font.bold: true
                                        }

                                        ToolTip {
                                            id: subTip
                                            visible: subMouse.containsMouse
                                            delay: 350
                                            contentItem: Text {
                                                text: "Sub-units: " + (modelData.subUnitsSummary || "None") + "\n(Controls affect all sub-services together)"
                                                color: "#ffffff"
                                                font.pixelSize: Theme.scaled(9.5)
                                            }
                                            background: Rectangle { color: Qt.rgba(20/255, 20/255, 30/255, 0.95); radius: 6; border.color: Theme.glassBorder; border.width: 1 }
                                        }

                                        MouseArea {
                                            id: subMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                        }
                                    }

                                    // Live RAM consumed across all sub-units
                                    Text {
                                        text: "•"
                                        color: Qt.rgba(255, 255, 255, 0.3)
                                        font.pixelSize: Theme.scaled(10)
                                        visible: modelData.isRunning && modelData.memoryFormatted !== "0 B"
                                    }
                                    Text {
                                        text: "RAM: " + modelData.memoryFormatted
                                        color: "#89dceb"
                                        font.pixelSize: Theme.scaled(9.5)
                                        font.bold: true
                                        visible: modelData.isRunning && modelData.memoryFormatted !== "0 B"
                                    }
                                }
                            }

                            // SPACER: Pushes all controls strictly to the right edge!
                            Item { Layout.fillWidth: true }

                            // --- RIGHT-SIDE CONTROLS (PIXEL-PERFECT FLUSH ALIGNMENT) ---
                            RowLayout {
                                spacing: Theme.scaled(8)
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                                // 1. Live Status Badge (Fixed Width: 78)
                                Rectangle {
                                    width: Theme.scaled(78)
                                    height: Theme.scaled(28)
                                    radius: Theme.scaled(14)
                                    color: modelData.isRunning ? Qt.rgba(166/255, 227/255, 161/255, 0.18) : (modelData.activeState === "failed" ? Qt.rgba(243/255, 139/255, 168/255, 0.18) : Qt.rgba(255, 255, 255, 0.08))
                                    border.width: 0

                                    RowLayout {
                                        anchors.centerIn: parent
                                        spacing: Theme.scaled(5)

                                        Rectangle {
                                            width: Theme.scaled(6); height: Theme.scaled(6); radius: 999
                                            color: modelData.isRunning ? "#a6e3a1" : (modelData.activeState === "failed" ? "#f38ba8" : "#6c7086")
                                        }

                                        Text {
                                            text: modelData.isRunning ? "RUNNING" : (modelData.activeState === "failed" ? "FAILED" : "STOPPED")
                                            font.pixelSize: Theme.scaled(9)
                                            font.bold: true
                                            color: modelData.isRunning ? "#a6e3a1" : (modelData.activeState === "failed" ? "#f38ba8" : "#a6adc8")
                                        }
                                    }
                                }

                                // 2. Enable / Disable Startup Toggle (Fixed Width: 88)
                                Rectangle {
                                    id: enableToggleBtn
                                    width: Theme.scaled(88)
                                    height: Theme.scaled(28)
                                    radius: Theme.scaled(14)
                                    color: modelData.isEnabled ? Qt.rgba(137/255, 180/255, 250/255, 0.2) : Qt.rgba(255, 255, 255, 0.08)
                                    border.width: 0
                                    scale: enableMouse.pressed ? 0.93 : 1.0

                                    Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
                                    Behavior on color { ColorAnimation { duration: Theme.animInstant } }

                                    RowLayout {
                                        anchors.centerIn: parent
                                        spacing: Theme.scaled(4)

                                        Text {
                                            text: modelData.isEnabled ? "󰄲" : "󰄱"
                                            font.family: Theme.iconFont
                                            font.pixelSize: Theme.scaled(16)
                                            color: modelData.isEnabled ? "#89b4fa" : Theme.subtext0
                                        }

                                        Text {
                                            text: modelData.isEnabled ? "BOOT: ON" : "BOOT: OFF"
                                            font.pixelSize: Theme.scaled(9.5)
                                            font.bold: true
                                            color: modelData.isEnabled ? "#ffffff" : Theme.subtext0
                                        }
                                    }

                                    MouseArea {
                                        id: enableMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            SystemServicesService.toggleEnable(modelData.scope, modelData.id, modelData.isEnabled);
                                        }
                                    }

                                    ToolTip {
                                        visible: enableMouse.containsMouse
                                        delay: 350
                                        contentItem: Text {
                                            text: (modelData.isEnabled ? "Startup enabled." : "Startup disabled.") + "\n(Applies to primary & all sub-services)"
                                            color: "#ffffff"
                                            font.pixelSize: Theme.scaled(10)
                                        }
                                        background: Rectangle { color: Qt.rgba(20/255, 20/255, 30/255, 0.95); radius: 6; border.color: Theme.glassBorder; border.width: 1 }
                                    }
                                }

                                // 3. Start / Stop Action Button (Fixed Width: 80)
                                Rectangle {
                                    id: actionBtn
                                    width: Theme.scaled(80)
                                    height: Theme.scaled(28)
                                    radius: Theme.scaled(14)
                                    color: serviceCard.isBusy ? Qt.rgba(255, 255, 255, 0.1) : (modelData.isRunning ? (actionMouse.containsMouse ? Qt.rgba(243/255, 139/255, 168/255, 0.35) : Qt.rgba(243/255, 139/255, 168/255, 0.2)) : (actionMouse.containsMouse ? Qt.rgba(166/255, 227/255, 161/255, 0.35) : Qt.rgba(166/255, 227/255, 161/255, 0.2)))
                                    border.width: 0
                                    scale: actionMouse.pressed ? 0.93 : 1.0

                                    Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
                                    Behavior on color { ColorAnimation { duration: Theme.animInstant } }

                                    RowLayout {
                                        anchors.centerIn: parent
                                        spacing: Theme.scaled(5)

                                        Text {
                                            text: serviceCard.isBusy ? "󰑐" : (modelData.isRunning ? "󰓛" : "󰐊")
                                            font.family: Theme.iconFont
                                            font.pixelSize: Theme.scaled(16)
                                            color: modelData.isRunning ? "#f38ba8" : "#a6e3a1"

                                            RotationAnimation on rotation {
                                                from: 0; to: 360; duration: 600
                                                loops: Animation.Infinite
                                                running: serviceCard.isBusy
                                            }
                                        }

                                        Text {
                                            text: serviceCard.isBusy ? "WAIT..." : (modelData.isRunning ? "STOP" : "START")
                                            font.pixelSize: Theme.scaled(9.5)
                                            font.bold: true
                                            color: modelData.isRunning ? "#f38ba8" : "#a6e3a1"
                                        }
                                    }

                                    MouseArea {
                                        id: actionMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (serviceCard.isBusy) return;
                                            if (modelData.isRunning) {
                                                SystemServicesService.stopService(modelData.scope, modelData.id);
                                            } else {
                                                SystemServicesService.startService(modelData.scope, modelData.id);
                                            }
                                        }
                                    }

                                    ToolTip {
                                        visible: actionMouse.containsMouse
                                        delay: 350
                                        contentItem: Text {
                                            text: (modelData.isRunning ? "Stop " : "Start ") + modelData.name + " and all sub-services"
                                            color: "#ffffff"
                                            font.pixelSize: Theme.scaled(10)
                                        }
                                        background: Rectangle { color: Qt.rgba(20/255, 20/255, 30/255, 0.95); radius: 6; border.color: Theme.glassBorder; border.width: 1 }
                                    }
                                }

                                // 4. Restart Button (Visible when running, Fixed Width: 28)
                                Rectangle {
                                    id: restartBtn
                                    width: Theme.scaled(28); height: Theme.scaled(28)
                                    radius: 999
                                    visible: modelData.isRunning
                                    color: restartMouse.containsMouse ? Theme.menuDarkHover : "transparent"
                                    border.width: 0
                                    scale: restartMouse.pressed ? 0.91 : 1.0

                                    Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                                    Text {
                                        anchors.centerIn: parent
                                        text: "󰑐"
                                        font.family: Theme.iconFont
                                        font.pixelSize: Theme.scaled(18)
                                        color: "#ffffff"
                                    }

                                    MouseArea {
                                        id: restartMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: SystemServicesService.restartService(modelData.scope, modelData.id)
                                    }

                                    ToolTip {
                                        visible: restartMouse.containsMouse
                                        delay: 350
                                        contentItem: Text { text: "Restart " + modelData.name + " & sub-services"; color: "#ffffff"; font.pixelSize: Theme.scaled(10) }
                                        background: Rectangle { color: Qt.rgba(20/255, 20/255, 30/255, 0.95); radius: 6; border.color: Theme.glassBorder; border.width: 1 }
                                    }
                                }

                                // Placeholder spacer if stopped to keep chevron column strictly aligned!
                                Item {
                                    width: Theme.scaled(28); height: Theme.scaled(28)
                                    visible: !modelData.isRunning
                                }

                                // 5. Expand Chevron Button (Fixed Width: 28)
                                Rectangle {
                                    width: Theme.scaled(28); height: Theme.scaled(28)
                                    radius: 999
                                    color: chevronMouse.containsMouse ? Qt.rgba(255, 255, 255, 0.12) : "transparent"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "󰅂"
                                        font.family: Theme.iconFont
                                        font.pixelSize: Theme.scaled(16)
                                        color: isExpanded ? Theme.accentColor : Theme.subtext0
                                        rotation: isExpanded ? 90 : 0
                                        Behavior on rotation { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
                                    }

                                    MouseArea {
                                        id: chevronMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (root.expandedServiceId === modelData.id) {
                                                root.expandedServiceId = "";
                                                SystemServicesService.clearLogs();
                                            } else {
                                                root.expandedServiceId = modelData.id;
                                                SystemServicesService.fetchLogs(modelData.scope, modelData.id);
                                            }
                                        }
                                    }

                                    ToolTip {
                                        visible: chevronMouse.containsMouse
                                        delay: 350
                                        contentItem: Text { text: isExpanded ? "Hide Logs" : "View Filtered Journal Logs"; color: "#ffffff"; font.pixelSize: Theme.scaled(10) }
                                        background: Rectangle { color: Qt.rgba(20/255, 20/255, 30/255, 0.95); radius: 6; border.color: Theme.glassBorder; border.width: 1 }
                                    }
                                }
                            }
                        }

                        // --- ACCORDION LOGS DRAWER (EXPANDED VIEW) ---
                        Rectangle {
                            id: logsDrawer
                            Layout.fillWidth: true
                            Layout.preferredHeight: isExpanded ? Theme.scaled(230) : 0
                            height: Layout.preferredHeight
                            radius: Theme.scaled(12)
                            color: Qt.rgba(12/255, 12/255, 18/255, 0.85)
                            border.color: Theme.glassBorder
                            border.width: 1
                            clip: true
                            visible: isExpanded

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(10)
                                spacing: Theme.scaled(8)

                                // Logs Header Bar
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.scaled(8)

                                    Text {
                                        text: "󰞋"
                                        font.family: Theme.iconFont
                                        font.pixelSize: Theme.scaled(14)
                                        color: Theme.accentColor
                                    }

                                    Text {
                                        text: "FILTERED JOURNAL: " + modelData.name.toUpperCase() + " (" + modelData.id + ")"
                                        color: "#ffffff"
                                        font.pixelSize: Theme.scaled(10)
                                        font.bold: true
                                        font.family: "monospace"
                                    }

                                    Item { Layout.fillWidth: true }

                                    // Filter pills for logs
                                    RowLayout {
                                        spacing: Theme.scaled(4)
                                        Repeater {
                                            model: ["All", "Errors", "Warnings", "System"]
                                            delegate: Rectangle {
                                                implicitWidth: lFilterText.implicitWidth + Theme.scaled(12)
                                                height: Theme.scaled(20)
                                                radius: Theme.scaled(10)
                                                color: root.logFilterLevel === modelData ? Theme.accentColor : (lFilterMouse.containsMouse ? Qt.rgba(255,255,255,0.12) : Qt.rgba(255,255,255,0.06))
                                                Text {
                                                    id: lFilterText
                                                    anchors.centerIn: parent
                                                    text: modelData
                                                    font.pixelSize: Theme.scaled(8.5)
                                                    font.bold: true
                                                    color: root.logFilterLevel === modelData ? Colors.on_primary : "#ffffff"
                                                }
                                                MouseArea {
                                                    id: lFilterMouse
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: root.logFilterLevel = modelData
                                                }
                                            }
                                        }
                                    }

                                    // Copy Logs Button
                                    Rectangle {
                                        width: Theme.scaled(24); height: Theme.scaled(24); radius: 999
                                        color: copyLogMouse.containsMouse ? Qt.rgba(255, 255, 255, 0.15) : "transparent"
                                        Text { anchors.centerIn: parent; text: "󰆏"; font.family: Theme.iconFont; color: "#ffffff"; font.pixelSize: Theme.scaled(13) }
                                        MouseArea {
                                            id: copyLogMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                copyLogProc.command = ["wl-copy", SystemServicesService.logRawText];
                                                copyLogProc.running = true;
                                            }
                                        }
                                        ToolTip {
                                            visible: copyLogMouse.containsMouse
                                            delay: 350
                                            contentItem: Text { text: "Copy Raw Logs to Clipboard"; color: "#ffffff"; font.pixelSize: Theme.scaled(10) }
                                            background: Rectangle { color: Qt.rgba(20/255, 20/255, 30/255, 0.95); radius: 6; border.color: Theme.glassBorder; border.width: 1 }
                                        }
                                    }

                                    // Refresh Logs Button
                                    Rectangle {
                                        width: Theme.scaled(24); height: Theme.scaled(24); radius: 999
                                        color: refreshLogMouse.containsMouse ? Qt.rgba(255, 255, 255, 0.15) : "transparent"
                                        Text { anchors.centerIn: parent; text: "󰑐"; font.family: Theme.iconFont; color: Theme.accentColor; font.pixelSize: Theme.scaled(13) }
                                        MouseArea {
                                            id: refreshLogMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: SystemServicesService.fetchLogs(modelData.scope, modelData.id)
                                        }
                                    }
                                }

                                // Terminal Console Box
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: Theme.scaled(8)
                                    color: Qt.rgba(0, 0, 0, 0.6)
                                    border.color: Qt.rgba(255, 255, 255, 0.08)
                                    border.width: 1
                                    clip: true

                                    // Loading state
                                    ColumnLayout {
                                        anchors.centerIn: parent
                                        visible: SystemServicesService.logsLoading && SystemServicesService.activeLogUnit === modelData.id
                                        spacing: Theme.scaled(6)
                                        Text {
                                            Layout.alignment: Qt.AlignHCenter
                                            text: "󰑐"
                                            font.family: Theme.iconFont
                                            font.pixelSize: Theme.scaled(20)
                                            color: Theme.accentColor
                                            rotation: 0
                                            NumberAnimation on rotation { from: 0; to: 360; duration: 800; loops: Animation.Infinite }
                                        }
                                        Text {
                                            Layout.alignment: Qt.AlignHCenter
                                            text: "Fetching journalctl logs..."
                                            color: Theme.subtext0
                                            font.pixelSize: Theme.scaled(10.5)
                                        }
                                    }

                                    // Empty state
                                    Text {
                                        anchors.centerIn: parent
                                        visible: !SystemServicesService.logsLoading && root.getFilteredLogs().length === 0
                                        text: "No journal logs found matching filter."
                                        color: Theme.subtext0
                                        font.pixelSize: Theme.scaled(11)
                                        font.italic: true
                                    }

                                    // Log Lines List
                                    ScrollView {
                                        anchors.fill: parent
                                        anchors.margins: Theme.scaled(6)
                                        visible: !SystemServicesService.logsLoading && root.getFilteredLogs().length > 0
                                        clip: true

                                        ListView {
                                            id: logLinesView
                                            width: parent.width
                                            spacing: Theme.scaled(2)
                                            model: root.getFilteredLogs()

                                            delegate: RowLayout {
                                                width: logLinesView.width
                                                spacing: Theme.scaled(6)

                                                Text {
                                                    text: modelData.timestamp || ""
                                                    color: Qt.rgba(255, 255, 255, 0.4)
                                                    font.family: "monospace"
                                                    font.pixelSize: Theme.scaled(9.5)
                                                    visible: modelData.timestamp !== ""
                                                }

                                                Rectangle {
                                                    width: Theme.scaled(36); height: Theme.scaled(14); radius: Theme.scaled(3)
                                                    color: modelData.level === "error" ? Qt.rgba(243/255, 139/255, 168/255, 0.25) : (modelData.level === "warning" ? Qt.rgba(249/255, 226/255, 175/255, 0.25) : (modelData.level === "system" ? Qt.rgba(137/255, 180/255, 250/255, 0.2) : Qt.rgba(255, 255, 255, 0.08)))
                                                    border.color: modelData.level === "error" ? "#f38ba8" : (modelData.level === "warning" ? "#f9e2af" : (modelData.level === "system" ? "#89b4fa" : "transparent"))
                                                    border.width: 1

                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: modelData.level.toUpperCase().substring(0, 4)
                                                        font.pixelSize: Theme.scaled(8)
                                                        font.bold: true
                                                        font.family: "monospace"
                                                        color: modelData.level === "error" ? "#f38ba8" : (modelData.level === "warning" ? "#f9e2af" : (modelData.level === "system" ? "#89b4fa" : "#a6adc8"))
                                                    }
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: modelData.message || modelData.raw
                                                    color: modelData.level === "error" ? "#f38ba8" : (modelData.level === "warning" ? "#f9e2af" : (modelData.level === "system" ? "#b4befe" : "#cdd6f4"))
                                                    font.family: "monospace"
                                                    font.pixelSize: Theme.scaled(9.5)
                                                    wrapMode: Text.WrapAnywhere
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

    Process {
        id: copyLogProc
    }

    function filteredServices() {
        let list = SystemServicesService.services || [];
        let q = root.searchQuery.trim().toLowerCase();
        let cat = root.activeCategoryFilter;

        return list.filter(s => {
            if (q !== "") {
                let match = (s.name && s.name.toLowerCase().indexOf(q) !== -1) ||
                            (s.id && s.id.toLowerCase().indexOf(q) !== -1) ||
                            (s.subUnitsSummary && s.subUnitsSummary.toLowerCase().indexOf(q) !== -1) ||
                            (s.category && s.category.toLowerCase().indexOf(q) !== -1);
                if (!match) return false;
            }

            if (cat === "Running") return s.isRunning;
            if (cat === "Stopped") return !s.isRunning;
            if (cat !== "All" && s.category !== cat) return false;

            return true;
        });
    }

    function getFilteredLogs() {
        let entries = SystemServicesService.logEntries || [];
        let f = root.logFilterLevel;
        if (f === "All") return entries;
        if (f === "Errors") return entries.filter(e => e.level === "error");
        if (f === "Warnings") return entries.filter(e => e.level === "warning");
        if (f === "System") return entries.filter(e => e.level === "system");
        return entries;
    }
}
