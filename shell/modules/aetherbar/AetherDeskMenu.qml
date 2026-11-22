// Aether shell (AGPL-3.0-or-later). The desktop menu: right-click the wallpaper. It opens where you clicked:
// your wallpapers in a row on top, then add / edit desktop widgets (the widget set chosen in Mix & match),
// the wallpaper picker, the dashboard and settings. Click anywhere else or press Esc to close it.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import Quickshell
import Quickshell.Wayland
import Quickshell.Widgets
import qs.services
import qs.utils
import "../../dss" as Dss

Scope {
    id: dm

    readonly property var cfg: AetherBarConfig
    readonly property string widgets: WinSettings.mixDesktopWidgets
    readonly property var sets: [{ id: "dss", name: "DarkShadow" }, { id: "serp", name: "serpantinum" }, { id: "noctalia", name: "Noctalia" }, { id: "dms", name: "DMS" }, { id: "ryoku", name: "Ryoku" }]

    property real t: 0
    Component.onCompleted: t = 1
    Behavior on t { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

    function close() {
        cfg.menuOpen = false;
    }
    function settings(tab) {
        close();
        WinSettings.ipc("aether", "open", [tab]);
    }
    function addWidgets() {
        close();
        if (widgets === "dss") {
            const home = (Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/");
            Dss.DssWin.run([home + "/.local/bin/desktop-set", "edit", "on"]);
        } else if (widgets === "off" || widgets === "") {
            WinSettings.mixDesktopWidgets = "dss";    // turn a widget set on, then its editor opens
            editT.start();
        } else {
            WinSettings.ipc("aether", "open", [widgets === "serp" ? "serp" : "widgets"]);
        }
    }
    Timer {
        id: editT
        interval: 900
        onTriggered: {
            const home = (Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/");
            Dss.DssWin.run([home + "/.local/bin/desktop-set", "edit", "on"]);
        }
    }

    PanelWindow {
        id: win
        screen: dm.cfg.menuScreen || Quickshell.screens[0]
        color: "transparent"
        anchors { top: true; left: true; right: true; bottom: true }
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.namespace: "aether-deskmenu"
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onPressed: m => {
                if (m.button === Qt.RightButton)
                    dm.cfg.openDeskMenu(dm.cfg.menuScreen, m.x, m.y);   // right-click elsewhere: move the menu
                else
                    dm.close();
            }
        }

        Rectangle {
            id: card
            focus: true
            Keys.onEscapePressed: dm.close()
            width: 300
            height: col.height + 16
            x: Math.max(12, Math.min(dm.cfg.menuX, win.width - width - 12))
            y: Math.max(12, Math.min(dm.cfg.menuY, win.height - height - 12))
            radius: 22
            color: AetherStyle.surface
            border.width: 1
            border.color: Qt.alpha(AetherStyle.outlineVar, 0.6)
            opacity: dm.t
            scale: 0.9 + 0.1 * dm.t
            transformOrigin: Item.TopLeft
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#000000"
                shadowOpacity: 0.4
                shadowBlur: 0.8
                shadowVerticalOffset: 4
            }
            MouseArea { anchors.fill: parent }   // clicks inside do not close it

            component Row1: Rectangle {
                id: r
                required property string icon
                required property string label
                property string hint: ""
                signal clicked
                width: col.width
                height: 40
                radius: 12
                color: ra.containsMouse ? Qt.alpha(AetherStyle.primary, 0.14) : "transparent"
                Behavior on color { ColorAnimation { duration: 140 } }
                Text {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: r.icon
                    font.family: "Material Symbols Rounded"
                    font.pixelSize: 20
                    color: ra.containsMouse ? AetherStyle.primary : AetherStyle.textSub
                }
                Text {
                    x: 44
                    anchors.verticalCenter: parent.verticalCenter
                    text: r.label
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: AetherStyle.textMain
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: r.hint
                    font.pixelSize: 11
                    color: AetherStyle.textSub
                }
                MouseArea {
                    id: ra
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: r.clicked()
                }
            }

            Column {
                id: col
                x: 8
                y: 8
                width: parent.width - 16
                spacing: 2

                // a row of wallpapers: click one to use it; the last tile opens the picker
                Row {
                    spacing: 6
                    leftPadding: 4
                    bottomPadding: 6
                    topPadding: 4
                    Repeater {
                        model: Wallpapers.list.slice(0, 4)
                        ClippingRectangle {
                            id: wt
                            required property var modelData
                            width: 62
                            height: 42
                            radius: 10
                            color: AetherStyle.palette.m3surfaceContainerHigh
                            Image {
                                anchors.fill: parent
                                source: wt.modelData.path
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                sourceSize.width: 140
                                opacity: wm.containsMouse ? 1 : 0.82
                                Behavior on opacity { NumberAnimation { duration: 140 } }
                            }
                            MouseArea {
                                id: wm
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    Wallpapers.setWallpaper(wt.modelData.path);
                                    dm.close();
                                }
                            }
                        }
                    }
                    Rectangle {
                        width: 62
                        height: 42
                        radius: 10
                        color: am.containsMouse ? AetherStyle.primaryContainer : AetherStyle.palette.m3surfaceContainerHigh
                        Text {
                            anchors.centerIn: parent
                            text: "more_horiz"
                            font.family: "Material Symbols Rounded"
                            font.pixelSize: 22
                            color: AetherStyle.textMain
                        }
                        MouseArea {
                            id: am
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                dm.close();
                                dm.cfg.pickerTap();
                            }
                        }
                    }
                }

                Row1 {
                    icon: "widgets"
                    label: dm.widgets === "off" || dm.widgets === "" ? "Add widgets" : "Add or move widgets"
                    onClicked: dm.addWidgets()
                }
                // which widget set the desktop uses
                Flow {
                    width: col.width
                    leftPadding: 10
                    spacing: 4
                    bottomPadding: 4
                    Repeater {
                        model: dm.sets.concat([{ id: "off", name: "None" }])
                        Rectangle {
                            id: sc
                            required property var modelData
                            readonly property bool on: dm.widgets === modelData.id
                            width: st.implicitWidth + 16
                            height: 24
                            radius: 12
                            color: on ? AetherStyle.primary : sm.containsMouse ? Qt.alpha(AetherStyle.primary, 0.14) : AetherStyle.palette.m3surfaceContainerHigh
                            Text {
                                id: st
                                anchors.centerIn: parent
                                text: sc.modelData.name
                                font.pixelSize: 11
                                color: sc.on ? AetherStyle.palette.m3onPrimary : AetherStyle.textMain
                            }
                            MouseArea {
                                id: sm
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: WinSettings.mixDesktopWidgets = sc.modelData.id
                            }
                        }
                    }
                }
                Row1 { icon: "wallpaper"; label: "Change wallpaper"; hint: "Super+W"; onClicked: { dm.close(); dm.cfg.pickerTap(); } }
                Row1 { icon: "shuffle"; label: "Random wallpaper"; onClicked: { Wallpapers.setRandom(); dm.close(); } }
                Row1 { icon: "space_dashboard"; label: "Dashboard"; onClicked: { dm.close(); dm.cfg.toggleDash("home", win.screen); } }
                Rectangle { width: col.width; height: 1; color: Qt.alpha(AetherStyle.outlineVar, 0.5) }
                Row1 { icon: "palette"; label: "Looks and themes"; onClicked: dm.settings("looks") }
                Row1 { icon: "tune"; label: "Aether settings"; onClicked: dm.settings("aetherbar") }
            }
        }
    }
}
