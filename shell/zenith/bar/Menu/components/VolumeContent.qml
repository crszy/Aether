import "../.."
import "../../../"
import "../../../services"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell
import Quickshell.Io

ColumnLayout {
    id: root
    spacing: Theme.scaled(20)
    Layout.fillWidth: true

    opacity: 0
    scale: 0.98
    Component.onCompleted: {
        VolumeService.update();
        entryAnim.start();
    }

    ParallelAnimation {
        id: entryAnim
        NumberAnimation { target: root; property: "opacity"; to: 1; duration: Theme.animNormal; easing.type: Theme.animEasing }
        NumberAnimation { target: root; property: "scale"; to: 1; duration: Theme.animNormal; easing.type: Theme.elasticEasing }
    }

    // --- Header ---
    RowLayout {
        Layout.fillWidth: true
        ColumnLayout {
            spacing: Theme.scaled(2); Layout.fillWidth: true
            Text { text: "AUDIO CONTROL"; color: Theme.accentColor; font.pixelSize: Theme.scaled(14); font.letterSpacing: 2; font.weight: Font.Black }
            Text { text: "OUTPUT & INPUT DEVICES"; color: Theme.subtext0; font.pixelSize: Theme.scaled(10); font.weight: Font.Bold; font.letterSpacing: 1 }
        }
    }

    // Main Controls Row (Output & Input Cards)
    GridLayout {
        columns: (Theme.isSmallScreen && Theme.isPortrait) ? 1 : 2
        Layout.fillWidth: true
        columnSpacing: Theme.scaled(15)
        rowSpacing: Theme.scaled(15)

        // Output Speaker Card
        Rectangle {
            id: outputCard
            Layout.fillWidth: true
            property bool dropdownOpen: false
            property int targetHeight: dropdownOpen ? (Theme.scaled(165) + Math.max(Theme.scaled(76), VolumeService.sinks.length * Theme.scaled(36))) : Theme.scaled(165)
            implicitHeight: targetHeight
            Layout.preferredHeight: targetHeight
            height: targetHeight
            color: Theme.cardBackground
            radius: Theme.bubbleRadiusLarge
            border.color: Theme.glassBorder
            border.width: 1
            clip: true

            Behavior on Layout.preferredHeight { NumberAnimation { duration: Theme.animNormal; easing.type: Theme.animEasing } }
            Behavior on implicitHeight { NumberAnimation { duration: Theme.animNormal; easing.type: Theme.animEasing } }
            Behavior on height { NumberAnimation { duration: Theme.animNormal; easing.type: Theme.animEasing } }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(16)
                spacing: Theme.scaled(12)

                VolumeSlider {
                    label: "OUTPUT VOLUME"
                    icon: VolumeService.muted ? "󰝟" : (VolumeService.btActive ? "󰓃" : "󰕾")
                    value: VolumeService.outputVolume
                    sliderColor: Theme.accentColor
                    onChange: (v) => VolumeService.setOutputVolume(v)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    // Output Device Dropdown Button
                    Rectangle {
                        Layout.fillWidth: true
                        height: Theme.scaled(38)
                        radius: Theme.bubbleRadiusSmall
                        color: outputCard.dropdownOpen ? Theme.menuDarkHover : "transparent"
                        border.color: Theme.glassBorder
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(10)
                            spacing: Theme.scaled(8)

                            Text {
                                text: "󰓃"
                                font.family: Theme.iconFont
                                font.pixelSize: Theme.scaled(20)
                                color: Theme.accentColor
                            }

                            Text {
                                Layout.fillWidth: true
                                text: {
                                    for (let s of VolumeService.sinks) {
                                        if (s.isDefault) return s.name;
                                    }
                                    return VolumeService.sinks.length > 0 ? VolumeService.sinks[0].name : "Select Output Device";
                                }
                                color: Theme.text
                                font.pixelSize: Theme.scaled(11)
                                font.weight: Font.Bold
                                elide: Text.ElideRight
                            }

                            Text {
                                text: outputCard.dropdownOpen ? "󰅀" : "󰅂"
                                font.family: Theme.iconFont
                                font.pixelSize: Theme.scaled(14)
                                color: Theme.subtext0
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: outputCard.dropdownOpen = !outputCard.dropdownOpen
                        }
                    }

                    // Mute Output Toggle
                    Rectangle {
                        width: Theme.scaled(40)
                        height: Theme.scaled(40)
                        radius: Theme.bubbleRadiusSmall
                        color: VolumeService.muted ? Qt.rgba(Theme.red.r, Theme.red.g, Theme.red.b, 0.25) : (muteMouse.containsMouse ? Theme.menuDarkHover : "transparent")
                        border.width: 0

                        Text {
                            anchors.centerIn: parent
                            text: VolumeService.muted ? "󰝟" : "󰕾"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(22)
                            color: VolumeService.muted ? Theme.powerRed : Theme.text
                        }

                        MouseArea {
                            id: muteMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: VolumeService.toggleMute()
                        }
                    }
                }

                // Dropdown Sinks List
                ListView {
                    visible: outputCard.dropdownOpen
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: VolumeService.sinks
                    spacing: Theme.scaled(4)

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: Theme.scaled(32)
                        radius: Theme.bubbleRadiusSmall
                        color: modelData.isDefault ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.2) : (devMouse.containsMouse ? Theme.menuBackground2Hover : "transparent")

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(6)
                            spacing: Theme.scaled(8)

                            Text {
                                text: modelData.isDefault ? "󰄬" : "󰓃"
                                font.family: Theme.iconFont
                                font.pixelSize: Theme.scaled(12)
                                color: modelData.isDefault ? Theme.powerGreen : Theme.subtext0
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: modelData.isDefault ? Theme.accentColor : Theme.text
                                font.pixelSize: Theme.scaled(10)
                                font.weight: modelData.isDefault ? Font.Bold : Font.Normal
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: devMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                VolumeService.setDefaultDevice(modelData.id);
                                outputCard.dropdownOpen = false;
                            }
                        }
                    }
                }
            }
        }

        // Input Microphone Card
        Rectangle {
            id: inputCard
            Layout.fillWidth: true
            property bool dropdownOpen: false
            property int targetHeight: dropdownOpen ? (Theme.scaled(165) + Math.max(Theme.scaled(76), VolumeService.sources.length * Theme.scaled(36))) : Theme.scaled(165)
            implicitHeight: targetHeight
            Layout.preferredHeight: targetHeight
            height: targetHeight
            color: Theme.cardBackground
            radius: Theme.bubbleRadiusLarge
            border.color: Theme.glassBorder
            border.width: 1
            clip: true

            Behavior on Layout.preferredHeight { NumberAnimation { duration: Theme.animNormal; easing.type: Theme.animEasing } }
            Behavior on implicitHeight { NumberAnimation { duration: Theme.animNormal; easing.type: Theme.animEasing } }
            Behavior on height { NumberAnimation { duration: Theme.animNormal; easing.type: Theme.animEasing } }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(16)
                spacing: Theme.scaled(12)

                VolumeSlider {
                    label: "INPUT VOLUME"
                    icon: VolumeService.micMuted ? "󰍭" : "󰍬"
                    value: VolumeService.micVolume
                    sliderColor: Theme.accentColor
                    onChange: (v) => VolumeService.setMicVolume(v)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    // Input Device Dropdown Button
                    Rectangle {
                        Layout.fillWidth: true
                        height: Theme.scaled(38)
                        radius: Theme.bubbleRadiusSmall
                        color: inputCard.dropdownOpen ? Theme.menuDarkHover : "transparent"
                        border.color: Theme.glassBorder
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(10)
                            spacing: Theme.scaled(8)

                            Text {
                                text: "󰍬"
                                font.family: Theme.iconFont
                                font.pixelSize: Theme.scaled(20)
                                color: Theme.accentColor
                            }

                            Text {
                                Layout.fillWidth: true
                                text: {
                                    for (let s of VolumeService.sources) {
                                        if (s.isDefault) return s.name;
                                    }
                                    return VolumeService.sources.length > 0 ? VolumeService.sources[0].name : "Select Input Device";
                                }
                                color: Theme.text
                                font.pixelSize: Theme.scaled(11)
                                font.weight: Font.Bold
                                elide: Text.ElideRight
                            }

                            Text {
                                text: inputCard.dropdownOpen ? "󰅀" : "󰅂"
                                font.family: Theme.iconFont
                                font.pixelSize: Theme.scaled(14)
                                color: Theme.subtext0
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: inputCard.dropdownOpen = !inputCard.dropdownOpen
                        }
                    }

                    // Mute Input Toggle
                    Rectangle {
                        width: Theme.scaled(40)
                        height: Theme.scaled(40)
                        radius: Theme.bubbleRadiusSmall
                        color: VolumeService.micMuted ? Qt.rgba(Theme.red.r, Theme.red.g, Theme.red.b, 0.25) : (micMuteMouse.containsMouse ? Theme.menuDarkHover : "transparent")
                        border.width: 0

                        Text {
                            anchors.centerIn: parent
                            text: VolumeService.micMuted ? "󰍭" : "󰍬"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(22)
                            color: VolumeService.micMuted ? Theme.powerRed : Theme.text
                        }

                        MouseArea {
                            id: micMuteMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: VolumeService.toggleMicMute()
                        }
                    }
                }

                // Dropdown Sources List
                ListView {
                    visible: inputCard.dropdownOpen
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: VolumeService.sources
                    spacing: Theme.scaled(4)

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: Theme.scaled(32)
                        radius: Theme.bubbleRadiusSmall
                        color: modelData.isDefault ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.2) : (srcMouse.containsMouse ? Theme.menuBackground2Hover : "transparent")

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(6)
                            spacing: Theme.scaled(8)

                            Text {
                                text: modelData.isDefault ? "󰄬" : "󰍬"
                                font.family: Theme.iconFont
                                font.pixelSize: Theme.scaled(12)
                                color: modelData.isDefault ? Theme.powerGreen : Theme.subtext0
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: modelData.isDefault ? Theme.accentColor : Theme.text
                                font.pixelSize: Theme.scaled(10)
                                font.weight: modelData.isDefault ? Font.Bold : Font.Normal
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: srcMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                VolumeService.setDefaultDevice(modelData.id);
                                inputCard.dropdownOpen = false;
                            }
                        }
                    }
                }
            }
        }
    }

    // Application Streams Volume Control
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.scaled(12)
        visible: VolumeService.appsModel.count > 0

        Text { 
            text: "APPLICATION STREAMS"
            color: Theme.accentColor
            font.pixelSize: Theme.scaled(11)
            font.weight: Font.Black
            font.letterSpacing: 2
        }

        GridLayout {
            columns: 2
            Layout.fillWidth: true
            columnSpacing: Theme.scaled(12)
            rowSpacing: Theme.scaled(12)

            Repeater {
                model: VolumeService.appsModel
                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: Theme.scaled(110)
                    color: Theme.menuBackground2
                    radius: Theme.bubbleRadiusMedium
                    border.color: Theme.glassBorder
                    border.width: 1

                    required property string name
                    required property int volume
                    required property int appId
                    required property string icon

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(12)

                        VolumeSlider {
                            label: (name || "Unknown Stream").toUpperCase()
                            icon: "󰓃"
                            value: volume
                            sliderColor: Theme.powerGreen
                            onChange: (v) => { 
                                VolumeService.setAppVolume(appId, v); // Windows 
                            }
                        }
                    }
                    Process { id: setAppVol }
                }
            }
        }
    }

    Item { Layout.fillHeight: true }
}
