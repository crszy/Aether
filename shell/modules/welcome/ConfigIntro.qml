pragma ComponentBehavior: Bound

// Shown once, the first time Caelestia starts: where every config file is, with buttons to open them.
// The per-part files (bar.json, lock.json ...) are written by ConfigParts before this appears.
// Settings -> Windows -> "Config files" lists the same files later; WinSettings.configIntroShown keeps it closed.
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Windows as Win
import Caelestia.Config
import qs.components
import qs.components.controls
import qs.services
import qs.utils

LazyLoader {
    id: loader

    active: WinSettings.loaded && !WinSettings.configIntroShown

    FloatingWindow {
        id: win

        title: "Caelestia - your config files"
        color: Qt.alpha(Colours.palette.m3surface, WinSettings.nexusOpacity)
        surfaceFormat.opaque: false
        implicitWidth: 720
        implicitHeight: col.implicitHeight + 48

        function open(path: string): void {
            Qt.openUrlExternally("file:///" + path.replace(/\\/g, "/"));
        }

        function done(): void {
            WinSettings.configIntroShown = true;
        }

        function applyFrame(): void {
            const w = col.Window.window;
            if (!w) return;
            Win.WindowFrame.setBackdrop(w, WinSettings.nexusBackdrop, !Colours.light);
            Win.WindowFrame.setTopmost(w, true);
            Win.WindowFrame.bringToFront(w);
        }

        onVisibleChanged: if (visible) Qt.callLater(applyFrame)
        Component.onCompleted: frameTimer.start()

        Timer {
            id: frameTimer

            interval: 300
            onTriggered: win.applyFrame()
        }

        // no system frame (that is what makes it see-through): drag anywhere to move it
        DragHandler {
            target: null
            onActiveChanged: if (active && col.Window.window) col.Window.window.startSystemMove()
        }

        ColumnLayout {
            id: col

            anchors.fill: parent
            anchors.margins: 24
            spacing: 10

            RowLayout {
                spacing: 12

                MaterialIcon {
                    text: "folder_special"
                    color: Colours.palette.m3primary
                    fontStyle: Tokens.font.icon.large
                }
                StyledText {
                    Layout.fillWidth: true
                    text: "Your config files"
                    font: Tokens.font.title.medium
                }
            }

            StyledText {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Colours.palette.m3onSurfaceVariant
                text: "Everything in Caelestia can be changed from these files - edit them yourself, or send one to an AI and save what it gives back. Saving applies the change right away. A file you delete is written again with every option."
            }

            Repeater {
                model: [
                    { icon: "dashboard_customize", name: "One file per part", detail: "bar.json, lock.json, launcher.json, notifs.json, dashboard.json ... (every option of that part) + README.txt", path: Paths.config, folder: true },
                    { icon: "desktop_windows", name: "Windows options", detail: "windows.json - taskbar, lock, PIN, Settings window, notifications", path: `${Paths.config}/windows.json` },
                    { icon: "view_agenda", name: "Taskbar apps", detail: "taskbar.json - icons, badges, grouping, clicks", path: `${Paths.config}/taskbar.json` },
                    { icon: "tune", name: "Main store", detail: "shell.json, shell-tokens.json (sizes, animations), keybinds.json, monitors\\", path: Paths.appConfig, folder: true }
                ]

                StyledRect {
                    id: row

                    required property var modelData

                    Layout.fillWidth: true
                    implicitHeight: rowLayout.implicitHeight + 20
                    radius: Tokens.rounding.medium
                    color: Qt.alpha(Colours.palette.m3surfaceContainer, 0.8)

                    RowLayout {
                        id: rowLayout

                        anchors.fill: parent
                        anchors.margins: 10
                        anchors.leftMargin: 14
                        spacing: 12

                        MaterialIcon {
                            text: row.modelData.icon
                            color: Colours.palette.m3primary
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            StyledText {
                                text: row.modelData.name
                            }
                            StyledText {
                                Layout.fillWidth: true
                                text: row.modelData.detail
                                color: Colours.palette.m3onSurfaceVariant
                                font: Tokens.font.label.small
                                wrapMode: Text.WordWrap
                            }
                            StyledText {
                                Layout.fillWidth: true
                                text: row.modelData.path.replace(/\//g, "\\")
                                color: Colours.palette.m3outline
                                font: Tokens.font.label.small
                                elide: Text.ElideMiddle
                            }
                        }

                        IconTextButton {
                            icon: "open_in_new"
                            text: "Open"
                            onClicked: win.open(row.modelData.path)
                        }
                    }
                }
            }

            RowLayout {
                Layout.topMargin: 6
                spacing: 10

                StyledText {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Colours.palette.m3outline
                    font: Tokens.font.label.small
                    text: "You can find this list again in Settings > Windows > Config files."
                }

                IconTextButton {
                    icon: "check"
                    text: "Got it"
                    onClicked: win.done()
                }
            }
        }
    }
}
