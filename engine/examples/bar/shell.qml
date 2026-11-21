import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import Quickshell.Wayland
import Quickshell.Services.Mpris

ShellRoot {
	id: root

	property bool shown: true
	readonly property color background: Qt.rgba(0.08, 0.08, 0.11, 0.85)
	readonly property color foreground: "#e6e6f0"
	readonly property color accent: "#9ccfff"
	readonly property MprisPlayer player: Mpris.players.values.find(p => p.isPlaying) ?? Mpris.players.values[0] ?? null

	SystemClock {
		id: clock
		precision: SystemClock.Minutes
	}

	IpcHandler {
		target: "bar"

		function toggle(): void {
			root.shown = !root.shown;
		}
	}

	Variants {
		model: Quickshell.screens

		PanelWindow {
			required property var modelData

			screen: modelData
			visible: root.shown
			color: "transparent"
			implicitHeight: 44
			exclusiveZone: implicitHeight

			anchors {
				top: true
				left: true
				right: true
			}

			Rectangle {
				anchors.fill: parent
				anchors.margins: 6
				anchors.bottomMargin: 2
				radius: height / 2
				color: root.background

				RowLayout {
					anchors.fill: parent
					anchors.leftMargin: 10
					anchors.rightMargin: 16
					spacing: 12

					Row {
						spacing: 4

						Repeater {
							model: Hyprland.workspaces

							Rectangle {
								required property HyprlandWorkspace modelData

								width: modelData.active ? 34 : 24
								height: 24
								radius: 12
								color: modelData.active ? root.accent : Qt.rgba(1, 1, 1, 0.08)

								Behavior on width {
									NumberAnimation {
										duration: 150
									}
								}

								Text {
									anchors.centerIn: parent
									text: parent.modelData.name
									color: parent.modelData.active ? "#10203a" : root.foreground
									font.pixelSize: 12
									font.bold: true
								}

								MouseArea {
									anchors.fill: parent
									onClicked: parent.modelData.activate()
								}
							}
						}
					}

					Text {
						Layout.fillWidth: true
						horizontalAlignment: Text.AlignHCenter
						text: ToplevelManager.activeToplevel?.activated ? ToplevelManager.activeToplevel.title : ""
						color: root.foreground
						elide: Text.ElideRight
						font.pixelSize: 13
					}

					Text {
						visible: root.player !== null && root.player.trackTitle !== ""
						Layout.maximumWidth: 320
						text: root.player ? (root.player.isPlaying ? "▶ " : "‖ ") + root.player.trackTitle + (root.player.trackArtist ? " · " + root.player.trackArtist : "") : ""
						color: root.foreground
						elide: Text.ElideRight
						font.pixelSize: 13

						MouseArea {
							anchors.fill: parent
							onClicked: root.player?.togglePlaying()
						}
					}

					Text {
						text: Qt.formatDateTime(clock.date, "ddd d MMM  HH:mm")
						color: root.foreground
						font.pixelSize: 13
						font.bold: true
					}
				}
			}
		}
	}
}
