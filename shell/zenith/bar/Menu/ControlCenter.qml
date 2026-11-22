import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell
import Quickshell.Hyprland
import Quickshell.Wayland
import "../.."
import "../../services"
import "../../Settings"
import "./components"

MenuWindow {
    id: root

    card: mainContent
    namespaceName: "controlcenter"
    onDismissed: CenterState.close()

    shown: CenterState.qsVisible

    onShownChanged: {
        Variables.controlCenterOpen = shown;
        if (shown) {
            Qt.callLater(() => {
                mainContent.forceActiveFocus();
                updateFocusForTab(CenterState.activeTab);
            });
        }
    }

    // Outer clicks are dismissed by DismissOverlay; the input mask and the
    // reason a dismiss MouseArea cannot live here are documented in
    // MenuWindow.qml.
    Rectangle {
        id: mainContent
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Theme.barMarginTop + Theme.scaled(4)

        // Masterwork Adaptive Sizing
        width: Math.min(Theme.scaled(840), (screen ? screen.width : Theme.screenWidth) - Theme.scaled(24))
        height: Math.min(Theme.scaled(680), (screen ? screen.height : Theme.screenHeight) - Theme.barHeight - Theme.scaled(20))

        color: Theme.glassBackground
        radius: Theme.cardRadius
        border.color: Theme.glassBorder
        border.width: 1
        clip: true

        focus: true
        Keys.onPressed: (event) => {
            if (CenterState.activeTab === "Wallpaper") {
                wallpaperContent.handleKeys(event);
                if (event.accepted) return;
            }
            let currentContent = contentStack.children[contentStack.currentIndex];
            if (currentContent && typeof currentContent.handleKeys === 'function') {
                currentContent.handleKeys(event);
                if (event.accepted) return;
            }
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                let tabs = ["Default", "Pomodoro", "Mail", "Wallpaper"];
                let idx = tabs.indexOf(CenterState.activeTab);
                if (idx !== -1) {
                    let nextIdx = (event.key === Qt.Key_Right) ? (idx + 1) % tabs.length : (idx - 1 + tabs.length) % tabs.length;
                    CenterState.activeTab = tabs[nextIdx];
                    event.accepted = true;
                    return;
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.scaled(18)
            spacing: Theme.scaled(14)

            // --- HEADER WITH BUBBLE SWITCHER & CONTROLS ---
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(12)
                
                Rectangle {
                    width: Theme.scaled(4)
                    height: Theme.scaled(24)
                    color: Theme.accentColor
                    radius: 999
                }

                Text { 
                    text: "DASHBOARD"
                    color: "#ffffff"
                    font.pixelSize: Theme.scaled(12.5)
                    font.weight: Font.Black
                    font.letterSpacing: 2
                    visible: !Theme.isSmallScreen
                }

                // Bubble Tab Switcher Capsule (Transparent on Base Canvas)
                Rectangle {
                    height: Theme.scaled(38)
                    implicitWidth: tabRow.implicitWidth + Theme.scaled(8)
                    radius: 999
                    color: "transparent"
                    border.color: Theme.glassBorder
                    border.width: 1

                    RowLayout {
                        id: tabRow
                        anchors.centerIn: parent
                        spacing: Theme.scaled(4)

                        Repeater {
                            model: ["Default", "Pomodoro", "Mail", "Wallpaper"]
                            delegate: Rectangle {
                                id: tabRect
                                width: Math.max(Theme.scaled(82), tabText.implicitWidth + Theme.scaled(18))
                                height: Theme.scaled(30)
                                radius: 999
                                color: CenterState.activeTab === modelData ? Theme.accentColor : (tabMouse.containsMouse ? Theme.menuBackground2Hover : "transparent")
                                scale: tabMouse.pressed ? 0.95 : 1.0
                                
                                Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
                                Behavior on color { ColorAnimation { duration: Theme.animInstant } }

                                Text {
                                    id: tabText
                                    anchors.centerIn: parent
                                    text: root.tabLabel(modelData)
                                    font.pixelSize: Theme.scaled(11)
                                    font.weight: Font.Bold
                                    color: CenterState.activeTab === modelData ? Colors.on_primary : (tabMouse.containsMouse ? Theme.subtext0 : "#ffffff")
                                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                                }
                                MouseArea { 
                                    id: tabMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: CenterState.activeTab = modelData 
                                }
                            }
                        }
                    }
                }
                
                Item { Layout.fillWidth: true }
                
                // --- TOP RIGHT POWER USER TOOLS ICON CLUSTER ---
                RowLayout {
                    spacing: Theme.scaled(6)
                    Layout.alignment: Qt.AlignVCenter

                    // 1. Media Player Auto-Focus
                    Rectangle {
                        id: focusRect
                        width: Theme.scaled(36); height: Theme.scaled(36)
                        radius: 999
                        color: MediaPlayerService.mediaFocus ? Theme.accentColor : (focusMouse.containsMouse ? Theme.menuDarkHover : "transparent")
                        border.width: 0
                        scale: focusMouse.pressed ? 0.94 : 1.0
                        
                        Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                        Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                        Text {
                            anchors.centerIn: parent
                            text: MediaPlayerService.mediaFocus ? "󰎆" : "󰎇"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(20)
                            color: MediaPlayerService.mediaFocus ? Colors.on_primary : (focusMouse.containsMouse ? Theme.subtext0 : "#ffffff")
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        }
                        MouseArea { 
                            id: focusMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: MediaPlayerService.mediaFocus = !MediaPlayerService.mediaFocus
                        }
                        ToolTip {
                            visible: focusMouse.containsMouse
                            delay: 350; timeout: 3000
                            y: parent.height + Theme.scaled(6)
                            contentItem: Text { text: "Media Auto-Focus: " + (MediaPlayerService.mediaFocus ? "ON" : "OFF"); color: "#ffffff"; font.pixelSize: Theme.scaled(10.5); font.weight: Font.DemiBold }
                            background: Rectangle { color: Theme.cardBackground; border.color: Theme.glassBorder; border.width: 1; radius: Theme.scaled(7) }
                        }
                    }

                    // 2. Caffeine (Keep Awake)
                    Rectangle {
                        id: caffeineRect
                        width: Theme.scaled(36); height: Theme.scaled(36)
                        radius: 999
                        color: CaffeineService.active ? Theme.accentColor : (caffeineMouse.containsMouse ? Theme.cardBackgroundHover : "transparent")
                        border.width: 0
                        scale: caffeineMouse.pressed ? 0.94 : 1.0
                        
                        Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                        Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                        Text {
                            anchors.centerIn: parent
                            text: CaffeineService.active ? "󰅶" : "󰛊"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(20)
                            color: CaffeineService.active ? Colors.on_primary : (caffeineMouse.containsMouse ? Theme.subtext0 : "#ffffff")
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        }
                        MouseArea { 
                            id: caffeineMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: CaffeineService.toggle() 
                        }
                        ToolTip {
                            visible: caffeineMouse.containsMouse
                            delay: 350; timeout: 3000
                            y: parent.height + Theme.scaled(6)
                            contentItem: Text { text: "Caffeine (Prevent Sleep): " + (CaffeineService.active ? "ACTIVE" : "OFF"); color: "#ffffff"; font.pixelSize: Theme.scaled(10.5); font.weight: Font.DemiBold }
                            background: Rectangle { color: Theme.cardBackground; border.color: Theme.glassBorder; border.width: 1; radius: Theme.scaled(7) }
                        }
                    }

                    // 3. Night Light Toggle (Gammastep)
                    Rectangle {
                        id: nlRect
                        width: Theme.scaled(36); height: Theme.scaled(36)
                        radius: 999
                        color: PowerToolsService.nightLight ? Qt.rgba(250/255, 179/255, 135/255, 0.3) : (nlMouse.containsMouse ? Theme.cardBackgroundHover : "transparent")
                        border.width: 0
                        scale: nlMouse.pressed ? 0.94 : 1.0
                        
                        Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                        Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                        Text {
                            anchors.centerIn: parent
                            text: PowerToolsService.nightLight ? "󱩌" : "󱩍"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(20)
                            color: PowerToolsService.nightLight ? "#fab387" : (nlMouse.containsMouse ? Theme.subtext0 : "#ffffff")
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        }
                        MouseArea { 
                            id: nlMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: PowerToolsService.toggleNightLight()
                        }
                        ToolTip {
                            visible: nlMouse.containsMouse
                            delay: 350; timeout: 3000
                            y: parent.height + Theme.scaled(6)
                            contentItem: Text { text: "Night Light: " + (PowerToolsService.nightLight ? "WARM (3800K)" : "OFF"); color: "#ffffff"; font.pixelSize: Theme.scaled(10.5); font.weight: Font.DemiBold }
                            background: Rectangle { color: Theme.cardBackground; border.color: Theme.glassBorder; border.width: 1; radius: Theme.scaled(7) }
                        }
                    }

                    // 4. Do Not Disturb Toggle
                    Rectangle {
                        id: dndRect
                        width: Theme.scaled(36); height: Theme.scaled(36)
                        radius: 999
                        color: PowerToolsService.dnd ? Qt.rgba(203/255, 166/255, 247/255, 0.3) : (dndMouse.containsMouse ? Theme.cardBackgroundHover : "transparent")
                        border.width: 0
                        scale: dndMouse.pressed ? 0.94 : 1.0
                        
                        Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                        Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                        Text {
                            anchors.centerIn: parent
                            text: PowerToolsService.dnd ? "󰂛" : "󰂚"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(20)
                            color: PowerToolsService.dnd ? "#cba6f7" : (dndMouse.containsMouse ? Theme.subtext0 : "#ffffff")
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        }
                        MouseArea { 
                            id: dndMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: PowerToolsService.toggleDnd()
                        }
                        ToolTip {
                            visible: dndMouse.containsMouse
                            delay: 350; timeout: 3000
                            y: parent.height + Theme.scaled(6)
                            contentItem: Text { text: "Do Not Disturb: " + (PowerToolsService.dnd ? "SILENCED" : "NORMAL"); color: "#ffffff"; font.pixelSize: Theme.scaled(10.5); font.weight: Font.DemiBold }
                            background: Rectangle { color: Theme.cardBackground; border.color: Theme.glassBorder; border.width: 1; radius: Theme.scaled(7) }
                        }
                    }

                    // 5. Region Screenshot Snip
                    Rectangle {
                        id: snipRect
                        width: Theme.scaled(36); height: Theme.scaled(36)
                        radius: 999
                        color: snipMouse.containsMouse ? Theme.cardBackgroundHover : "transparent"
                        border.width: 0
                        scale: snipMouse.pressed ? 0.94 : 1.0
                        
                        Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                        Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                        Text {
                            anchors.centerIn: parent
                            text: "󰹑"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(20)
                            color: snipMouse.containsMouse ? Theme.accentColor : "#ffffff"
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        }
                        MouseArea { 
                            id: snipMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: PowerToolsService.triggerScreenshot("region")
                        }
                        ToolTip {
                            visible: snipMouse.containsMouse
                            delay: 350; timeout: 3000
                            y: parent.height + Theme.scaled(6)
                            contentItem: Text { text: "Region Screenshot (grim + slurp)"; color: "#ffffff"; font.pixelSize: Theme.scaled(10.5); font.weight: Font.DemiBold }
                            background: Rectangle { color: Theme.cardBackground; border.color: Theme.glassBorder; border.width: 1; radius: Theme.scaled(7) }
                        }
                    }

                    // 6. Color Picker
                    Rectangle {
                        id: pickRect
                        width: Theme.scaled(36); height: Theme.scaled(36)
                        radius: 999
                        color: pickMouse.containsMouse ? Theme.cardBackgroundHover : "transparent"
                        border.width: 0
                        scale: pickMouse.pressed ? 0.94 : 1.0
                        
                        Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                        Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                        Text {
                            anchors.centerIn: parent
                            text: "󰈊"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(20)
                            color: pickMouse.containsMouse ? Theme.accentColor : "#ffffff"
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        }
                        MouseArea { 
                            id: pickMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: PowerToolsService.triggerColorPicker()
                        }
                        ToolTip {
                            visible: pickMouse.containsMouse
                            delay: 350; timeout: 3000
                            y: parent.height + Theme.scaled(6)
                            contentItem: Text { text: "Color Picker (hyprpicker to clipboard)"; color: "#ffffff"; font.pixelSize: Theme.scaled(10.5); font.weight: Font.DemiBold }
                            background: Rectangle { color: Theme.cardBackground; border.color: Theme.glassBorder; border.width: 1; radius: Theme.scaled(7) }
                        }
                    }
                }
            }

            ParallelAnimation {
                id: fadeAnim
                NumberAnimation { target: contentStack; property: "opacity"; from: 0; to: 1; duration: Theme.animFast; easing.type: Easing.OutCubic }
                NumberAnimation { target: contentTranslate; property: "y"; from: 8; to: 0; duration: Theme.animFast; easing.type: Easing.OutCubic }
            }

            // --- CONTENT AREA ---
            StackLayout {
                id: contentStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: ["Default", "Pomodoro", "Mail", "Wallpaper"].indexOf(CenterState.activeTab)
                transform: Translate { id: contentTranslate }
                onCurrentIndexChanged: fadeAnim.restart()

                // Default Tab
                GridLayout {
                    columns: (Theme.isSmallScreen && Theme.isPortrait) ? 1 : 2
                    columnSpacing: Theme.scaled(14)
                    rowSpacing: Theme.scaled(14)

                    // LEFT COLUMN: Notifications (Top) + Media Player (Bottom)
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.rowSpan: (Theme.isSmallScreen && Theme.isPortrait) ? 1 : 2
                        spacing: Theme.scaled(12)

                        // 1. Notifications Bubble Card
                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            color: Theme.cardBackground
                            radius: Theme.cardRadius
                            border.color: Theme.glassBorder
                            border.width: 1
                            clip: true
                            
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: Theme.scaled(14); spacing: Theme.scaled(8)
                                
                                RowLayout {
                                    Layout.fillWidth: true; spacing: Theme.scaled(8)
                                    Text { text: "󰂚"; font.family: Theme.iconFont; color: Theme.accentColor; font.pixelSize: Theme.scaled(20) }
                                    Text { text: "NOTIFICATIONS"; color: Theme.subtext0; font.pixelSize: Theme.scaled(10); font.weight: Font.Black; font.letterSpacing: 1 }
                                    Rectangle {
                                        width: Theme.scaled(22); height: Theme.scaled(22); radius: 999
                                        color: "transparent"
                                        border.width: 0
                                        Label {
                                             anchors.centerIn: parent
                                             text: NotificationService.notifications.count
                                             color: Theme.accentColor
                                             font.pixelSize: Theme.scaled(11); font.bold: true
                                         }
                                    }

                                    Item { Layout.fillWidth: true }
                                    
                                    Button {
                                        id: fullscreenBtn
                                        flat: true
                                        padding: Theme.scaled(3)
                                        visible: !Theme.isSmallScreen
                                        contentItem: RowLayout {
                                            spacing: 4
                                            Text {
                                                text: NotificationSettings.fullscreenNotification ? "󰊓" : "󰊔"
                                                font.family: Theme.iconFont
                                                color: NotificationSettings.fullscreenNotification ? Theme.accentColor : "#ffffff"
                                                font.pixelSize: 16
                                            }
                                            Text { text: "NOTIFY"; font.pixelSize: 9; font.weight: Font.Black; color: "#ffffff" }
                                        }
                                        background: Rectangle { color: fullscreenBtn.hovered ? Theme.cardBackgroundHover : "transparent"; radius: 999; border.width: 0 }
                                        onClicked: NotificationSettings.fullscreenNotification = !NotificationSettings.fullscreenNotification
                                    }
                                    Button {
                                        id: osdFullscreenBtn
                                        flat: true
                                        padding: Theme.scaled(3)
                                        visible: !Theme.isSmallScreen
                                        contentItem: RowLayout {
                                            spacing: 4
                                            Text {
                                                text: NotificationSettings.fullscreenOSD ? "󰊓" : "󰊔"
                                                font.family: Theme.iconFont
                                                color: NotificationSettings.fullscreenOSD ? Theme.accentColor : "#ffffff"
                                                font.pixelSize: 16
                                            }
                                            Text { text: "OSD"; font.pixelSize: 9; font.weight: Font.Black; color: "#ffffff" }
                                        }
                                        background: Rectangle { color: osdFullscreenBtn.hovered ? Theme.cardBackgroundHover : "transparent"; radius: 999; border.width: 0 }
                                        onClicked: NotificationSettings.fullscreenOSD = !NotificationSettings.fullscreenOSD
                                    }
                                    Button {
                                        id: clearBtn
                                        flat: true
                                        padding: Theme.scaled(3)
                                        contentItem: Text { text: "󰃢"; font.family: Theme.iconFont; color: clearBtn.hovered ? Theme.subtext0 : "#ffffff"; font.pixelSize: Theme.scaled(18) }
                                        background: Rectangle { color: clearBtn.hovered ? Theme.cardBackgroundHover : "transparent"; radius: 999; border.width: 0 }
                                        onClicked: NotificationService.clearAll()
                                    }
                                }
                                ScrollView {
                                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                                    NotificationList {
                                        id: notificationList
                                        visible: NotificationSettings.enableNotifications
                                        Layout.fillWidth: true; height: parent.height 
                                    }
                                }
                            }
                        }
                    }

                    // RIGHT COLUMN: Calendar (Top) + Weather (Bottom)
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        visible: !Theme.isSmallScreen || !Theme.isPortrait
                        color: Theme.cardBackground
                        radius: Theme.cardRadius
                        border.color: Theme.glassBorder
                        border.width: 1
                        clip: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: Theme.scaled(14)
                            CalendarWidget { Layout.fillWidth: true; Layout.fillHeight: true }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        visible: (!Theme.isSmallScreen || !Theme.isPortrait) && WidgetSettings.enableWeather
                        color: Theme.cardBackground
                        radius: Theme.cardRadius
                        border.color: Theme.glassBorder
                        border.width: 1
                        clip: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: Theme.scaled(14)
                            WeatherWidget {
                                Layout.fillWidth: true; Layout.fillHeight: true
                            }
                        }
                    }
                }

                // Pomodoro Tab
                PomodoroContent {
                    Layout.fillWidth: true; Layout.fillHeight: true
                }

                // Mail Tab
                MailContent {
                    Layout.fillWidth: true; Layout.fillHeight: true
                }

                // Wallpaper Tab
                WallpaperContent {
                    id: wallpaperContent
                    Layout.fillWidth: true; Layout.fillHeight: true
                }
            }
        }
    }

    Connections {
        target: CenterState
        function onActiveTabChanged() {
            if (CenterState.activeTab === "Wallpaper") {
                mainContent.Keys.forwardTo = [wallpaperContent];
            } else {
                mainContent.Keys.forwardTo = [];
            }
        }
    }

    // The tab strings double as identifiers, so this maps identifier -> label
    // rather than renaming them. "Default" is meaningless to a reader; the tab
    // shows the overview cards.
    function tabLabel(tabId) {
        // "Pomodoro" names one technique, but the tab holds a todo list and a
        // timer; "Focus" covers both.
        if (tabId === "Default") return "Overview";
        if (tabId === "Pomodoro") return "Focus";
        return tabId;
    }

    function updateFocusForTab(tab) {
        if (tab === "Wallpaper") {
            wallpaperContent.forceActiveFocus();
        }
    }
}
