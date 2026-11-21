import "../.."
import "../../../"
import "../../../services"
import QtQuick
import QtQuick.Controls 2.15
import QtQuick.Layouts
import Quickshell

ColumnLayout {
    id: root
    spacing: Theme.scaled(20)
    Layout.fillWidth: true
    implicitHeight: BluetoothService.serviceActive ? (mainCol.implicitHeight + (emptyState.visible ? emptyState.implicitHeight : 0)) : 250

    property bool showUnnamed: false

    readonly property bool isShowing: root.visible && QuickSettingsService.qsVisible
    onIsShowingChanged: {
        if (isShowing) BluetoothService.refresh(true);
    }

    Timer {
        running: root.isShowing
        interval: 3000
        repeat: true
        onTriggered: BluetoothService.refresh(true)
    }

    Component.onCompleted: {
        BluetoothService.refresh();
    }

    function getDeviceIcon(iconName) {
        if (!iconName) return "󰂯";
        let low = iconName.toLowerCase();
        if (low.includes("audio-card")) return "󰓃";
        if (low.includes("audio-headset") || low.includes("headphone")) return "󰋋";
        if (low.includes("keyboard")) return "󰌌";
        if (low.includes("mouse")) return "󰍽";
        if (low.includes("phone")) return "󰏲";
        if (low.includes("display")) return "󰍹";
        if (low.includes("computer")) return "󰟀";
        return "󰂯";
    }

    ColumnLayout {
        id: mainCol
        Layout.fillWidth: true
        spacing: Theme.scaled(20)
        visible: BluetoothService.serviceActive

        // --- Header ---
        ColumnLayout {
            Layout.fillWidth: true; spacing: Theme.scaled(15)
            visible: BluetoothService.serviceActive

            RowLayout {
                Layout.fillWidth: true; spacing: Theme.scaled(15)
                ColumnLayout {
                    spacing: Theme.scaled(2); Layout.fillWidth: true
                    Text { text: "BLUETOOTH"; color: Theme.blue; font.pixelSize: Theme.scaled(14); font.letterSpacing: 2; font.weight: Font.Black; opacity: 0.8 }
                    Text { 
                        text: BluetoothService.powered ? BluetoothService.state.toUpperCase() : "DISABLED"
                        color: Theme.subtext1; font.pixelSize: Theme.scaled(10); font.weight: Font.Bold; font.letterSpacing: 1
                    }
                }

                // Refresh Button
                //
                // Spins for userBusy, not busy: background status probes fire
                // constantly (every monitor event, plus three follow-up polls
                // after each action) and driving the spinner from them made
                // the button strobe. See BluetoothService.userBusy.
                Rectangle {
                    id: refreshBtn
                    readonly property bool spinning: BluetoothService.userBusy

                    width: Theme.scaled(44); height: Theme.scaled(44); radius: Theme.scaled(22)
                    color: (refreshMouse.containsMouse ? Qt.rgba(1,1,1,0.05) : "transparent")
                    border.color: spinning ? Theme.powerYellow : Theme.glassBorder
                    clip: true
                    visible: BluetoothService.powered

                    Behavior on color { ColorAnimation { duration: 200 } }
                    Behavior on border.color { ColorAnimation { duration: 200 } }

                    Text {
                        id: refreshIcon
                        anchors.centerIn: parent; text: "󰑐"; font.family: Theme.iconFont; font.pixelSize: Theme.scaled(18)
                        color: refreshBtn.spinning ? Theme.powerYellow : Theme.powerGreen
                        Behavior on color { ColorAnimation { duration: 200 } }
                    }

                    RotationAnimation {
                        target: refreshIcon
                        running: refreshBtn.spinning
                        from: 0; to: 360; duration: 1000; loops: Animation.Infinite
                        // Stopping mid-spin used to leave the glyph frozen at
                        // whatever angle it reached, so the next spin snapped
                        // back to 0. Park it upright when idle.
                        onRunningChanged: if (!running) refreshIcon.rotation = 0
                    }

                    MouseArea {
                        id: refreshMouse; anchors.fill: parent; hoverEnabled: true
                        onClicked: BluetoothService.refresh(true, true)
                    }
                }

                // Scan Button
                Rectangle {
                    width: Theme.scaled(44); height: Theme.scaled(44); radius: Theme.scaled(22)
                    color: scanMouse.containsMouse ? Theme.menuDarkHover : "transparent"
                    border.width: 0
                    clip: true
                    visible: BluetoothService.powered
                    Behavior on color { ColorAnimation { duration: 200 } }
                    Text {
                        id: scanIcon; anchors.centerIn: parent; text: "󰂰"; font.family: Theme.iconFont; font.pixelSize: Theme.scaled(22)
                        color: BluetoothService.scanning ? Theme.powerYellow : Theme.text
                    }
                    MouseArea { id: scanMouse; anchors.fill: parent; hoverEnabled: true; onClicked: BluetoothService.toggleScan() }
                }

                // Power Button
                Rectangle {
                    width: Theme.scaled(44); height: Theme.scaled(44); radius: Theme.scaled(22)
                    color: powerMouse.containsMouse ? Theme.menuDarkHover : "transparent"
                    border.width: 0
                    clip: true
                    Behavior on color { ColorAnimation { duration: 200 } }
                    Text {
                        anchors.centerIn: parent; text: BluetoothService.powered ? "󰂯" : "󰂲"
                        font.family: Theme.iconFont; font.pixelSize: Theme.scaled(24); color: BluetoothService.powered ? Theme.accentColor : Theme.powerRed
                    }
                    MouseArea { id: powerMouse; anchors.fill: parent; hoverEnabled: true; onClicked: BluetoothService.togglePower() }
                }
            }

            // --- Settings Toggles ---
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(10)
                visible: BluetoothService.powered

                // Show Unnamed Toggle
                Rectangle {
                    Layout.fillWidth: true; height: Theme.scaled(40); radius: Theme.scaled(12); color: unnamedMouse.containsMouse ? Theme.menuDarkHover : "transparent"; border.color: root.showUnnamed ? Theme.accentColor : Theme.glassBorder; border.width: 1; clip: true
                    RowLayout {
                        anchors.centerIn: parent; spacing: 8
                        Text { text: root.showUnnamed ? "󰈈" : "󰈉"; font.family: Theme.iconFont; color: root.showUnnamed ? Theme.accentColor : Theme.text; font.pixelSize: Theme.scaled(18) }
                        Text { text: "SHOW UNNAMED"; color: Theme.text; font.pixelSize: Theme.scaled(8); font.weight: Font.Black; font.letterSpacing: 1 }
                    }
                    MouseArea { id: unnamedMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.showUnnamed = !root.showUnnamed }
                }

                // Startup Status Toggle
                Rectangle {
                    Layout.fillWidth: true; height: Theme.scaled(40); radius: Theme.scaled(12); 
                    color: startupMouse.containsMouse ? Theme.menuDarkHover : "transparent"; 
                    border.color: BluetoothService.isServiceEnabled ? Theme.powerGreen : Theme.powerRed; border.width: 1; clip: true
                    RowLayout {
                        anchors.centerIn: parent; spacing: 8
                        Text { 
                            text: BluetoothService.isServiceEnabled ? "󰄬" : "󰅖"; 
                            font.family: Theme.iconFont; 
                            color: BluetoothService.isServiceEnabled ? Theme.powerGreen : Theme.powerRed; 
                            font.pixelSize: Theme.scaled(18) 
                        }
                        Text { 
                            text: "START AT BOOT"; 
                            color: Theme.text; font.pixelSize: Theme.scaled(8); font.weight: Font.Black; font.letterSpacing: 1 
                        }
                    }
                    MouseArea { 
                        id: startupMouse; anchors.fill: parent; hoverEnabled: true; 
                        onClicked: BluetoothService.toggleStartup() 
                    }
                }
            }

            // Current Connection Status Bar
            Rectangle {
                Layout.fillWidth: true; height: Theme.scaled(60); color: Theme.cardBackground; radius: Theme.bubbleRadiusMedium; 
                visible: BluetoothService.powered && BluetoothService.connectedName !== ""
                border.color: Theme.glassBorder; border.width: 1
                RowLayout {
                    anchors.fill: parent; anchors.margins: Theme.scaled(12); spacing: Theme.scaled(15)
                    Rectangle { 
                        width: Theme.scaled(36); height: Theme.scaled(36); radius: Theme.bubbleRadiusSmall; color: "transparent"; border.width: 0
                        Text { anchors.centerIn: parent; text: getDeviceIcon(BluetoothService.connectedIcon); font.family: Theme.iconFont; font.pixelSize: Theme.scaled(22); color: Theme.accentColor }
                    }
                    ColumnLayout { spacing: 0; Layout.fillWidth: true
                        Text { text: BluetoothService.connectedName; color: Theme.text; font.weight: Font.Bold; font.pixelSize: Theme.scaled(13); elide: Text.ElideRight }
                        Text { 
                            text: BluetoothService.connectedAddress; color: Theme.subtext0; font.pixelSize: Theme.scaled(9); font.weight: Font.Bold 
                        }
                    }
                    
                    // Disconnect Button
                    ColumnLayout { spacing: 0; Layout.alignment: Qt.AlignRight; visible: BluetoothService.connectedBattery !== -1
                        Text { text: BluetoothService.connectedBattery + "%"; color: Theme.powerGreen; font.pixelSize: Theme.scaled(10); font.weight: Font.Black; horizontalAlignment: Text.AlignRight }
                        Text { text: "BATTERY"; color: Theme.subtext0; font.pixelSize: Theme.scaled(8); font.weight: Font.Black; horizontalAlignment: Text.AlignRight }
                    }
                    
                    Rectangle {
                        width: Theme.scaled(32); height: Theme.scaled(32); radius: Theme.bubbleRadiusSmall; color: Qt.rgba(1,0,0,0.1); border.width: 0
                        visible: BluetoothService.connectedAddress !== ""
                        Text { anchors.centerIn: parent; text: "󰤄"; font.family: Theme.iconFont; font.pixelSize: Theme.scaled(18); color: Theme.powerRed }
                        MouseArea { anchors.fill: parent; onClicked: BluetoothService.action("disconnect", BluetoothService.connectedAddress) }
                    }

                }
            }
        }

        // --- Device List ---
        ListView {
            FastWheel {}
            id: deviceList
            Layout.fillWidth: true
            
            property var displayDevices: BluetoothService.devices.filter(d => d.hasName || root.showUnnamed || d.connected)
            
            Layout.preferredHeight: contentHeight
            visible: BluetoothService.powered && BluetoothService.serviceActive && displayDevices.length > 0
            model: displayDevices
            spacing: Theme.scaled(10)
            clip: true
            interactive: false

            delegate: Rectangle {
                id: delegateRoot
                width: deviceList.width; height: Theme.scaled(72)
                color: modelData.connected ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.20) : (delegateMouse.containsMouse ? Theme.cardBackgroundHover : Theme.cardBackground)
                radius: Theme.bubbleRadiusMedium; border.color: modelData.connected ? Theme.accentColor : Theme.glassBorder
                border.width: 1
                scale: delegateMouse.pressed ? 0.97 : 1.0
                
                Behavior on color { ColorAnimation { duration: 150 } }
                Behavior on border.color { ColorAnimation { duration: 150 } }
                Behavior on scale { NumberAnimation { duration: 100 } }

                MouseArea {
                    id: delegateMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (modelData.connected) BluetoothService.action("disconnect", modelData.address);
                        else BluetoothService.action("connect", modelData.address);
                    }
                }

                RowLayout {
                    anchors.fill: parent; anchors.margins: Theme.scaled(12); spacing: Theme.scaled(15)
                    
                    Rectangle {
                        width: Theme.scaled(44); height: Theme.scaled(44); radius: Theme.bubbleRadiusSmall; color: "transparent"; border.width: 0
                        Text { anchors.centerIn: parent; text: getDeviceIcon(modelData.icon); font.family: Theme.iconFont; font.pixelSize: Theme.scaled(26); color: modelData.connected ? Theme.accentColor : Theme.text }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 0
                        Text { text: modelData.name; color: Theme.text; font.weight: Font.Bold; font.pixelSize: Theme.scaled(14); elide: Text.ElideRight }
                        Text { 
                            text: modelData.connected ? "CONNECTED" : (modelData.paired ? "PAIRED" : "READY")
                            color: modelData.connected ? Theme.accentColor : Theme.subtext0; font.pixelSize: Theme.scaled(9); font.weight: Font.Black 
                        }
                    }

                    // --- Action Buttons & Battery (Right Aligned) ---
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        spacing: Theme.scaled(8)

                        // Pair / Unpair / Disconnect toggle.
                        //
                        // The button reflects the device's *pairing* state, so
                        // pressing it always undoes what it last did:
                        //   not paired  -> pair
                        //   paired      -> unpair (forget)
                        //   connected   -> disconnect
                        //
                        // Connecting to an already-paired device is the row
                        // click, so making this button unpair does not strand
                        // saved devices.
                        Rectangle {
                            width: Theme.scaled(40); height: Theme.scaled(40); radius: Theme.scaled(12);
                            color: (modelData.connected || modelData.paired) ? Theme.powerRed : Theme.blue
                            Behavior on color { ColorAnimation { duration: 200 } }
                            Text {
                                anchors.centerIn: parent
                                text: modelData.connected ? "󰂯" : (modelData.paired ? "󰂲" : "󰂴")
                                font.family: Theme.iconFont; color: Colors.background; font.pixelSize: Theme.scaled(20)
                            }
                            MouseArea {
                                id: actionMouse
                                anchors.fill: parent;
                                hoverEnabled: true
                                onClicked: {
                                    if (modelData.connected) {
                                        BluetoothService.action("disconnect", modelData.address);
                                    } else if (modelData.paired) {
                                        BluetoothService.action("remove", modelData.address);
                                    } else {
                                        BluetoothService.action("pair", modelData.address);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Service Error View ---
    Rectangle {
        Layout.fillWidth: true; height: Theme.scaled(250); color: Qt.rgba(0.1, 0.05, 0.05, 0.3); radius: Theme.scaled(20); 
        visible: !BluetoothService.serviceActive
        border.color: Theme.powerRed; border.width: 1
        ColumnLayout {
            anchors.centerIn: parent; spacing: Theme.scaled(20); width: parent.width * 0.8
            Text { text: "󰂲"; font.family: Theme.iconFont; font.pixelSize: Theme.scaled(48); color: Theme.powerRed; Layout.alignment: Qt.AlignHCenter }
            ColumnLayout {
                spacing: Theme.scaled(5); Layout.alignment: Qt.AlignHCenter
                Text { text: "SERVICE ERROR"; color: Theme.text; font.pixelSize: Theme.scaled(16); font.weight: Font.Black; Layout.alignment: Qt.AlignHCenter }
                Text { text: "The Bluetooth daemon is not running or has crashed."; color: Theme.subtext1; font.pixelSize: Theme.scaled(10); font.weight: Font.Bold; Layout.alignment: Qt.AlignHCenter; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter; width: Theme.scaled(200); height: Theme.scaled(48); radius: Theme.scaled(14); color: (restartMouse.containsMouse ? Theme.powerRed : "transparent"); border.color: Theme.powerRed
                Text { anchors.centerIn: parent; text: "RESTART SERVICE"; color: (restartMouse.containsMouse ? Colors.background : Theme.powerRed); font.weight: Font.Black; font.pixelSize: Theme.scaled(11); font.letterSpacing: 1 }
                MouseArea { id: restartMouse; anchors.fill: parent; hoverEnabled: true; onClicked: BluetoothService.restartService() }
            }
        }
    }

    // --- Empty State Message ---
    Rectangle {
        id: emptyState
        Layout.fillWidth: true; height: Theme.scaled(100); color: "transparent"
        visible: BluetoothService.powered && BluetoothService.serviceActive && deviceList.displayDevices.length === 0
        ColumnLayout {
            anchors.centerIn: parent; spacing: 10
            Text { text: "󰂲"; font.family: Theme.iconFont; font.pixelSize: Theme.scaled(32); color: Theme.surface2; Layout.alignment: Qt.AlignHCenter }
            Text { text: "NO DEVICES FOUND"; color: Theme.surface2; font.pixelSize: Theme.scaled(10); font.weight: Font.Black; font.letterSpacing: 1; Layout.alignment: Qt.AlignHCenter }
        }
    }
}