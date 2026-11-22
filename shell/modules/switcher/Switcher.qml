pragma ComponentBehavior: Bound

// Alt+Tab switcher and Super+Tab task view for Caelestia on Windows.
//   Alt+Tab        a row of live window thumbnails, most recently used first; Tab / Shift+Tab move,
//                  letting go of Alt switches (Enter / a click too, Esc cancels)
//   Super+Tab      task view: every open window in a grid, stays open; click to switch, x to close, Esc leaves
// IPC: qs ipc call switcher open | next | prev | taskview | close
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Widgets
import Caelestia.Config
import qs.components
import qs.components.misc
import qs.services
import qs.utils

Scope {
    id: root

    property bool open: false
    property bool taskView: false
    property int index: 0
    property var mru: []          // window objects, most recently focused first

    readonly property var windows: {
        const all = ToplevelManager.toplevels.values.filter(t => t && t.appId && !["qs", "qsw", "quickshell"].includes(t.appId.toLowerCase()) && !t.parent);
        const known = mru.filter(w => all.includes(w));
        return known.concat(all.filter(w => !known.includes(w)));
    }

    Connections {
        target: ToplevelManager
        function onActiveToplevelChanged(): void {
            const t = ToplevelManager.activeToplevel;
            if (!t) return;
            root.mru = [t].concat(root.mru.filter(w => w !== t)).slice(0, 64);
        }
    }

    function show(view: bool, step: int): void {
        if (!windows.length) return;
        if (!open) {
            taskView = view;
            // Alt+Tab starts on the window you were in before (index 1); task view on the current one
            index = view ? 0 : Math.min(1, windows.length - 1);
            open = true;
        } else if (!taskView) {
            move(step);
        }
    }

    function move(step: int): void {
        if (windows.length) index = ((index + step) % windows.length + windows.length) % windows.length;
    }

    function commit(): void {
        const w = windows[index];
        open = false;
        if (w) {
            if (w.minimized) w.minimized = false;
            w.activate();
        }
    }

    function cancel(): void {
        open = false;
    }

    CustomShortcut {
        name: "switcher"
        description: "Alt+Tab window switcher"
        onPressed: root.show(false, 1)
    }

    CustomShortcut {
        name: "switcherBack"
        description: "Alt+Shift+Tab: switch backwards"
        onPressed: root.open ? root.move(-1) : root.show(false, -1)
    }

    CustomShortcut {
        name: "taskview"
        description: "Task view: every open window"
        onPressed: root.open ? root.cancel() : root.show(true, 0)
    }

    // "wallpaper": open the launcher straight in its wallpaper picker
    function openWallpaperPicker(): void {
        ShellState.forActive().launcher = true;
        wallpaperTimer.restart();
    }

    Timer {
        id: wallpaperTimer

        interval: 120
        onTriggered: {
            const search = ShellState.componentsForActive()?.find("launcherSearch");
            if (search) {
                search.text = `${GlobalConfig.launcher.actionPrefix}wallpaper `;
                search.forceActiveFocus();
            }
        }
    }

    CustomShortcut {
        name: "wallpaper"
        description: "Open the wallpaper picker"
        onPressed: root.openWallpaperPicker()
    }

    IpcHandler {
        target: "switcher"

        function wallpaper(): void { root.openWallpaperPicker(); }
        function open(): void { root.show(false, 1); }
        function next(): void { root.open ? root.move(1) : root.show(false, 1); }
        function prev(): void { root.open ? root.move(-1) : root.show(false, -1); }
        function taskview(): void { root.open ? root.cancel() : root.show(true, 0); }
        function close(): void { root.cancel(); }
        function isOpen(): bool { return root.open; }
    }

    LazyLoader {
        active: root.open

        PanelWindow {
            id: win

            screen: Quickshell.screens.find(s => s.name === Hypr.focusedMonitor?.name) ?? Quickshell.screens[0]
            color: "transparent"
            anchors {
                top: true
                bottom: true
                left: true
                right: true
            }
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.namespace: "caelestia-switcher"
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive

            readonly property int cols: root.taskView ? Math.min(4, root.windows.length) : Math.min(7, root.windows.length)
            readonly property real cardW: Math.max(140, Math.min(root.taskView ? 420 : 280, (width - 200) / Math.max(1, cols) - Tokens.spacing.large))
            readonly property real cardH: cardW * 0.68

            Rectangle {
                anchors.fill: parent
                color: Qt.alpha(Colours.palette.m3scrim, root.taskView ? 0.55 : 0.3)
                TapHandler { onTapped: root.cancel() }
            }

            Item {
                id: keys

                anchors.fill: parent
                focus: true
                Component.onCompleted: forceActiveFocus()

                Keys.onPressed: e => {
                    if (e.key === Qt.Key_Escape) root.cancel();
                    else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space) root.commit();
                    else if (e.key === Qt.Key_Backtab || (e.key === Qt.Key_Tab && (e.modifiers & Qt.ShiftModifier)) || e.key === Qt.Key_Left || e.key === Qt.Key_Up) root.move(-1);
                    else if (e.key === Qt.Key_Tab || e.key === Qt.Key_Right || e.key === Qt.Key_Down) root.move(1);
                    else if (e.key === Qt.Key_Delete && root.windows[root.index]) root.windows[root.index].close();
                    else return;
                    e.accepted = true;
                }
                Keys.onReleased: e => {
                    if (!root.taskView && e.key === Qt.Key_Alt) {
                        root.commit();
                        e.accepted = true;
                    }
                }
            }

            StyledRect {
                anchors.centerIn: parent
                width: grid.implicitWidth + Tokens.padding.extraLarge * 2
                height: grid.implicitHeight + Tokens.padding.extraLarge * 2
                radius: Tokens.rounding.extraLarge
                color: Colours.tPalette.m3surfaceContainer

                GridLayout {
                    id: grid

                    anchors.centerIn: parent
                    columns: Math.max(1, win.cols)
                    rowSpacing: Tokens.spacing.large
                    columnSpacing: Tokens.spacing.large

                    Repeater {
                        model: ScriptModel {
                            values: root.windows
                        }

                        Item {
                            id: card

                            required property var modelData
                            required property int index
                            readonly property bool selected: card.index === root.index

                            implicitWidth: win.cardW
                            implicitHeight: win.cardH + title.implicitHeight + Tokens.spacing.small

                            StyledRect {
                                id: frame

                                anchors.top: parent.top
                                width: parent.width
                                height: win.cardH
                                radius: Tokens.rounding.large
                                color: Colours.tPalette.m3surfaceContainerHigh
                                border.width: card.selected ? 3 : 0
                                border.color: Colours.palette.m3primary
                                clip: true

                                WindowThumbnail {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    captureSource: card.modelData
                                }

                                IconImage {
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 8
                                    implicitSize: 32
                                    source: (Quickshell.iconPath(DesktopEntries.heuristicLookup(card.modelData.appId)?.icon ?? card.modelData.appId, true) || Quickshell.shellPath("assets/app-generic.svg"))

                                    onStatusChanged: if (status === Image.Error && source != Quickshell.shellPath("assets/app-generic.svg")) source = Quickshell.shellPath("assets/app-generic.svg")
                                }

                                StyledRect {
                                    visible: root.taskView && cardHover.hovered
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 6
                                    implicitWidth: 28
                                    implicitHeight: 28
                                    radius: 14
                                    color: Colours.palette.m3errorContainer

                                    MaterialIcon {
                                        anchors.centerIn: parent
                                        text: "close"
                                        color: Colours.palette.m3onErrorContainer
                                    }

                                    TapHandler { onTapped: card.modelData.close() }
                                }
                            }

                            StyledText {
                                id: title

                                anchors.top: frame.bottom
                                anchors.topMargin: Tokens.spacing.small
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                text: card.modelData.title || card.modelData.appId
                                color: card.selected ? Colours.palette.m3primary : Colours.palette.m3onSurface
                            }

                            HoverHandler {
                                id: cardHover
                                onHoveredChanged: if (hovered) root.index = card.index
                            }

                            TapHandler {
                                onTapped: {
                                    root.index = card.index;
                                    root.commit();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
