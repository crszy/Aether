import "../.."
import "../../services"
import "../../Settings"
import "./components"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell
import Quickshell.Hyprland
import Quickshell.Wayland

MenuWindow {
    id: root

    shown: QuickSettingsService.qsVisible
    card: mainContent
    namespaceName: "quicksettings"
    cardOrigin: Item.TopRight

    readonly property var allTabs: [
        { id: "network", icon: "󰤨", title: "WI-FI" },
        { id: "bluetooth", icon: "󰂯", title: "BT" },
        { id: "volume", icon: "󰕾", title: "AUDIO" },
        { id: "powerprofile", icon: "󰍛", title: "POWER" },
        { id: "battery", icon: "󰁹", title: "BATTERY" },
        { id: "power", icon: "󰐥", title: "SESSION" }
    ]

    readonly property var availableTabs: allTabs.filter(function (t) {
        if (t.id === "bluetooth") return HardwareService.hasBluetooth;
        if (t.id === "battery")   return HardwareService.hasBattery;
        if (t.id === "network")   return HardwareService.hasNetworking;
        return true;
    })
    // Typing a WiFi password flips keyboardFocus, which reconfigures the
    // surface and drops the grab; without this the panel vanished the
    // moment you clicked a network to connect to.
    dismissInhibited: typeof wifiContent !== "undefined" && wifiContent.isInputActive
    onDismissed: QuickSettingsService.close()


    WlrLayershell.keyboardFocus: (typeof wifiContent !== "undefined" && wifiContent.isInputActive) ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.OnDemand

    onShownChanged: {
        if (shown) Qt.callLater(() => mainContent.forceActiveFocus());
    }

    // Outer clicks are dismissed by DismissOverlay; the input mask and the
    // reason a dismiss MouseArea cannot live here are documented in
    // MenuWindow.qml.
    Rectangle {
        id: mainContent
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Theme.barMarginTop + Theme.scaled(4)
        anchors.rightMargin: Theme.isSmallScreen ? Theme.scaled(8) : Theme.scaled(14)

        // Production-ready responsive dimensions
        width: Math.min(Theme.scaled(560), (screen ? screen.width : Theme.screenWidth) - Theme.scaled(20))
        height: Math.min(Theme.scaled(500), (screen ? screen.height : Theme.screenHeight) - Theme.barHeight - Theme.scaled(20))

        focus: true
        Keys.onPressed: (event) => {
            if (QuickSettingsService.activeTab === "power") {
                powerContent.handleKeys(event);
                if (event.accepted) return;
            }
            if (QuickSettingsService.activeTab === "network") {
                wifiContent.handleKeys(event);
                if (event.accepted) return;
            }
            let currentContent = contentStack.children[contentStack.currentIndex];
            if (currentContent && typeof currentContent.handleKeys === 'function') {
                currentContent.handleKeys(event);
                if (event.accepted) return;
            }
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                let tabs = root.availableTabs;
                let idx = tabs.findIndex(t => t.id === QuickSettingsService.activeTab);
                if (idx !== -1) {
                    let nextIdx = (event.key === Qt.Key_Right) ? (idx + 1) % tabs.length : (idx - 1 + tabs.length) % tabs.length;
                    QuickSettingsService.activeTab = tabs[nextIdx].id;
                    event.accepted = true;
                    return;
                }
            }
        }

        color: Theme.glassBackground
        radius: Theme.cardRadius
        border.color: Theme.glassBorder
        border.width: 1
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.scaled(16)
            spacing: Theme.scaled(14)

            // --- MATERIAL 3 SEGMENTED TAB BAR (Transparent on Base Canvas) ---
            Rectangle {
                Layout.fillWidth: true
                height: Theme.scaled(44)
                color: "transparent"
                radius: 999
                border.color: Theme.glassBorder
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(4)
                    spacing: Theme.scaled(4)
                    
                    Repeater {
                        // Only the tabs this machine can actually use. A
                        // Bluetooth tab on a desktop with no adapter, or a
                        // battery tab in a VM, is a dead button.
                        model: root.availableTabs

                        delegate: Rectangle {
                            id: tabRect
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 999
                            color: QuickSettingsService.activeTab === modelData.id ? Theme.accentColor : (tabMouse.containsMouse ? Theme.cardBackgroundHover : "transparent")
                            border.width: 0
                            scale: tabMouse.pressed ? 0.95 : 1.0
                            
                            Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                            Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: Theme.scaled(5)
                                Text {
                                    text: modelData.icon
                                    font.family: Theme.iconFont
                                    font.pixelSize: Theme.scaled(20)
                                    color: QuickSettingsService.activeTab === modelData.id ? Colors.on_primary : (tabMouse.containsMouse ? Theme.subtext0 : "#ffffff")
                                    Layout.alignment: Qt.AlignVCenter
                                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                                }
                                Text {
                                    text: modelData.title
                                    font.pixelSize: Theme.scaled(9)
                                    font.weight: Font.Bold
                                    font.letterSpacing: 0.5
                                    visible: QuickSettingsService.activeTab === modelData.id || !Theme.isSmallScreen
                                    color: QuickSettingsService.activeTab === modelData.id ? Colors.on_primary : (tabMouse.containsMouse ? Theme.subtext0 : "#ffffff")
                                    Layout.alignment: Qt.AlignVCenter
                                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                                }
                            }

                            MouseArea {
                                id: tabMouse
                                anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: QuickSettingsService.activeTab = modelData.id
                            }
                        }
                    }
                }
            }

            // --- CONTENT AREA ---
            ScrollView {
                id: scrollArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                
                contentHeight: contentStack.height

                ScrollBar.vertical: ScrollBar {
                    parent: scrollArea
                    anchors.top: scrollArea.top
                    anchors.bottom: scrollArea.bottom
                    anchors.right: scrollArea.right
                    policy: ScrollBar.AsNeeded
                    width: 3
                    contentItem: Rectangle { radius: 999; color: Theme.accentColor; opacity: 0.6 }
                }

                ParallelAnimation {
                    id: transitionAnim
                    NumberAnimation { target: contentStack; property: "opacity"; from: 0; to: 1; duration: Theme.animFast; easing.type: Easing.OutCubic }
                    NumberAnimation { target: contentTranslate; property: "y"; from: 8; to: 0; duration: Theme.animFast; easing.type: Easing.OutCubic }
                }

                StackLayout {
                    id: contentStack
                    width: scrollArea.availableWidth
                    transform: Translate { id: contentTranslate }
                    // Own implicitHeight, not a walk over children. See the
                    // segfault note: the previous binding read
                    // children[currentIndex].implicitHeight, which re-enters
                    // Qt's layout engine from inside its own rearrange.
                    height: Math.max(implicitHeight, Theme.scaled(420))
                    // Indexed against allTabs, because the pages below are all
                    // still here in their original order -- only the tab strip
                    // is filtered. Indexing the filtered list would show the
                    // wrong page as soon as one was hidden.
                    //
                    // A tab that has been filtered out can still be the active
                    // one (a saved state, or a keybind), so fall back to the
                    // first tab this machine actually has.
                    currentIndex: {
                        var all = root.allTabs.map(function (t) { return t.id; });
                        var avail = root.availableTabs.map(function (t) { return t.id; });
                        var active = QuickSettingsService.activeTab;
                        if (avail.indexOf(active) < 0 && avail.length > 0)
                            active = avail[0];
                        var i = all.indexOf(active);
                        return i >= 0 ? i : 0;
                    }

                    onCurrentIndexChanged: transitionAnim.restart()

                    WifiContent { id: wifiContent }
                    BluetoothContent { }
                    VolumeContent { }
                    PowerProfileContent { }
                    BatteryContent { }
                    PowerContent { 
                        id: powerContent
                        Connections {
                            target: QuickSettingsService
                            function onActiveTabChanged() {
                                if (QuickSettingsService.activeTab === "power") {
                                    powerContent.forceActiveFocus();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
