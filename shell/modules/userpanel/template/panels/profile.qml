// Your profile card. Opened by ">profile" in the launcher (commands.json), or: qs ipc call user panel profile
// Change anything you like and save - it is read fresh every time it opens.
import QtQuick
import QtQuick.Layouts
import Quickshell
import Caelestia.Config
import qs.components
import qs.services
import qs.utils

Rectangle {
    id: card

    signal close()          // Aether closes the panel when this fires

    implicitWidth: 420
    implicitHeight: column.implicitHeight + 48
    radius: 28
    color: Colours.palette.m3surfaceContainer

    ColumnLayout {
        id: column

        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        // avatar + name
        RowLayout {
            spacing: 16

            Rectangle {
                implicitWidth: 72
                implicitHeight: 72
                radius: 36
                color: Colours.palette.m3primaryContainer
                clip: true

                Image {
                    id: face

                    anchors.fill: parent
                    source: `file:///${Paths.home}/.face`
                    fillMode: Image.PreserveAspectCrop
                    visible: status === Image.Ready
                }

                MaterialIcon {
                    anchors.centerIn: parent
                    visible: !face.visible
                    text: "person"
                    color: Colours.palette.m3onPrimaryContainer
                    fontStyle: Tokens.font.icon.extraLarge
                }
            }

            ColumnLayout {
                spacing: 2

                StyledText {
                    text: Quickshell.env("USERNAME") || "you"
                    color: Colours.palette.m3onSurface
                    font.pointSize: 18
                    font.weight: 600
                }

                StyledText {
                    text: `${Quickshell.env("COMPUTERNAME") || ""}  ·  ${Qt.formatTime(Time.date, "hh:mm")}`
                    color: Colours.palette.m3onSurfaceVariant
                    font.pointSize: 11
                }
            }
        }

        // what's playing
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            radius: 18
            color: Colours.palette.m3surfaceContainerHigh

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                MaterialIcon {
                    text: Players.active?.isPlaying ? "graphic_eq" : "music_note"
                    color: Colours.palette.m3primary
                    fontStyle: Tokens.font.icon.medium
                }

                StyledText {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: Players.active ? `${Players.active.trackTitle || "?"}  -  ${Players.active.trackArtist || ""}` : "Nothing playing"
                    color: Colours.palette.m3onSurface
                }

                MaterialIcon {
                    visible: !!Players.active
                    text: Players.active?.isPlaying ? "pause" : "play_arrow"
                    color: Colours.palette.m3onSurface
                    fontStyle: Tokens.font.icon.medium

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Players.active?.togglePlaying()
                    }
                }
            }
        }

        // buttons: each one is just a MouseArea doing something
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Repeater {
                model: [
                    { icon: "tune", label: "Aether", action: "aether" },
                    { icon: "lock", label: "Lock", action: "lock" },
                    { icon: "close", label: "Close", action: "close" }
                ]

                Rectangle {
                    required property var modelData

                    Layout.fillWidth: true
                    implicitHeight: 44
                    radius: 22
                    color: area.containsMouse ? Colours.palette.m3secondaryContainer : Colours.palette.m3surfaceContainerHighest

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 6

                        MaterialIcon {
                            text: modelData.icon
                            color: Colours.palette.m3onSecondaryContainer
                        }

                        StyledText {
                            text: modelData.label
                            color: Colours.palette.m3onSecondaryContainer
                        }
                    }

                    MouseArea {
                        id: area

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.action === "aether")
                                WinSettings.ipc("aether", "settings", []);
                            else if (modelData.action === "lock")
                                WinSettings.ipc("lock", "lock", []);
                            card.close();
                        }
                    }
                }
            }
        }
    }
}
