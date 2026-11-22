import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell
import Quickshell.Io
import "../../.."

ColumnLayout {
    id: root
    spacing: Theme.scaled(25)
    Layout.fillWidth: true
    focus: true
    Keys.onPressed: (event) => handleKeys(event)
    
    function handleKeys(event) {
        let cols = 2;
        let maxIdx = powerButtons.count - 1;
        if (maxIdx < 0) return;

        if (event.key === Qt.Key_Right) {
            root.selectedIndex = Math.min(root.selectedIndex + 1, maxIdx);
            event.accepted = true;
        }
        else if (event.key === Qt.Key_Left) {
            root.selectedIndex = Math.max(root.selectedIndex - 1, 0);
            event.accepted = true;
        }
        else if (event.key === Qt.Key_Down) {
            root.selectedIndex = Math.min(root.selectedIndex + cols, maxIdx);
            event.accepted = true;
        }
        else if (event.key === Qt.Key_Up) {
            root.selectedIndex = Math.max(root.selectedIndex - cols, 0);
            event.accepted = true;
        }
        else if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
            let modelData = powerButtons.model[root.selectedIndex];
            if (modelData) {
                let cmd = modelData.cmd;
                powerProc.command = ["sh", "-c", cmd];
                powerProc.running = true;
            }
            event.accepted = true;
        }
    }
    
    // Explicit sizing for ScrollView integration
    implicitHeight: mainLayout.implicitHeight

    // SHUTDOWN is the default selection, by request.
    //
    // Worth knowing: the panel calls forceActiveFocus() when it opens and Enter
    // activates whatever is selected, so opening this menu and pressing Enter
    // powers the machine off. That is the intended behaviour here -- it is only
    // a hazard if a keystroke arrives before you have looked at the panel.
    //
    // Found by label rather than written as a number, so reordering the buttons
    // below cannot silently point this at REBOOT.
    readonly property int defaultIndex: {
        var buttons = powerButtons.model;
        for (var i = 0; i < buttons.length; i++)
            if (buttons[i].label === "SHUTDOWN") return i;
        return 0;
    }
    property int selectedIndex: defaultIndex

    // Reset selection every time the menu is opened
    onVisibleChanged: {
        if (visible) {
            selectedIndex = defaultIndex;
            root.forceActiveFocus();
        }
    }

    ColumnLayout {
        id: mainLayout
        Layout.fillWidth: true
        spacing: Theme.scaled(25)
        
        // ... (existing content)


        Text {
            text: "SESSION"
            color: Theme.blue
            font.pixelSize: 10
            font.weight: Font.Black
            font.letterSpacing: 2
            Layout.leftMargin: Theme.scaled(5)
        }

        GridLayout {
            columns: 2
            Layout.fillWidth: true
            rowSpacing: Theme.scaled(12)
            columnSpacing: Theme.scaled(12)

            Repeater {
                id: powerButtons
                // Same reason as PowerProfileContent: a Theme.* value inside the model
                // array makes the array itself a palette binding, so a recolour
                // destroys and rebuilds every delegate. Resolved per delegate below.
                model: [
                    { icon: "󰌾", label: "LOCK",     cmd: "zenith lock" },
                    { icon: "󰒲", label: "BIOS",     cmd: "systemctl reboot --firmware-setup" },
                    { icon: "󰗼", label: "LOGOUT",   cmd: "hyprctl dispatch exit" },
                    { icon: "󰤄", label: "SUSPEND",  cmd: "systemctl suspend" },
                    { icon: "󰑐", label: "REBOOT",   cmd: "reboot" },
                    { icon: "󰐥", label: "SHUTDOWN", cmd: "shutdown now" }
                ]

                delegate: Rectangle {
                    id: powerBtn

                    readonly property color accent: modelData.label === "LOGOUT"   ? Theme.powerGreen
                                                  : modelData.label === "REBOOT"   ? Theme.blue
                                                  : modelData.label === "SHUTDOWN" ? Theme.powerRed
                                                  : modelData.label === "BIOS"     ? Theme.mauve
                                                  : Theme.lavender

                    Layout.fillWidth: true
                    height: Theme.scaled(100)
                    anchors.margins: 2 
                    
                    property bool isSelected: index === root.selectedIndex
                    
                    color: isSelected ? Qt.rgba(powerBtn.accent.r, powerBtn.accent.g, powerBtn.accent.b, 0.20) : (m.containsMouse ? Theme.cardBackgroundHover : Theme.cardBackground)
                    radius: Theme.bubbleRadiusMedium
                    border.color: isSelected ? powerBtn.accent : (m.containsMouse ? powerBtn.accent : Theme.glassBorder)
                    border.width: 1
                    
                    scale: m.pressed ? 0.94 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
                    Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                    Behavior on border.color { ColorAnimation { duration: Theme.animInstant } }

                    MouseArea {
                        id: m
                        anchors.fill: parent; hoverEnabled: true
                        onEntered: root.selectedIndex = index
                        onClicked: {
                            if (modelData.label === "LOCK") {
                                LockService.lock();
                            } else {
                                powerProc.command = ["sh", "-c", modelData.cmd];
                                powerProc.running = true;
                            }
                        }
                    }

                    RowLayout {
                        anchors.centerIn: parent; spacing: 15
                        Rectangle {
                            width: 44; height: 44; radius: 12; color: "transparent"; border.width: 0
                            Text { anchors.centerIn: parent; text: modelData.icon; font.family: Theme.iconFont; font.pixelSize: 28; color: powerBtn.accent }
                        }
                        Text { text: modelData.label; font.pixelSize: 11; font.weight: Font.Black; color: m.containsMouse ? Theme.text : Theme.subtext0 }
                    }
                }
            }
        }
    }

    Item { Layout.fillHeight: true }

    Process { id: powerProc }
}
