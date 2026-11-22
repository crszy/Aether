pragma Singleton

import QtQuick
import Quickshell
import Caelestia.Config
import Quickshell.Windows as Win
import Caelestia.I18n
import qs.components
import qs.services
import qs.modules.nexus
import qs.modules.rice
import qs.modules.aethersettings as AS

Singleton {
    id: root

    property var current: null

    function create(parent: Item, props: var): void {
        // Settings style (Mix & match): Noctalia's settings window instead, unless Caelestia's was asked for
        // the rice panel (all shells in one) unless Caelestia's own was asked for
        // Aether settings (DarkShadowShell style, every shell in one window): the default
        if (WinSettings.settingsStyle === "aether" && !(props && props.caelestia)) {
            AS.AetherSettings.open(props && props.startPage ? String(props.startPage).toLowerCase() : "");
            return;
        }
        if (WinSettings.settingsStyle === "rice" && !(props && props.caelestia)) {
            RiceFactory.open("");
            return;
        }
        // DMS's own settings window (Aether's pages stay reachable: "Aether settings" in DMS's launcher)
        if (WinSettings.settingsStyle === "dms" && !(props && props.caelestia)) {
            WinSettings.ipc("dms:settings", "toggle", []);
            return;
        }
        if (WinSettings.settingsStyle === "noctalia" && !(props && props.caelestia)) {
            WinSettings.ipc("settings", "open", []);
            return;
        }
        if (props && props.caelestia !== undefined) {
            props = Object.assign({}, props);
            delete props.caelestia;
        }
        if (root.current && !root.current.closing) {
            if (props && props.startPage) root.current.startPage = props.startPage;
            Win.WindowFrame.bringToFront(root.current.contentItem.Window.window);
            return;
        }
        nexusComp.createObject(parent ?? dummy, props);
    }

    QtObject {
        id: dummy
    }

    Component {
        id: nexusComp

        FloatingWindow {
            id: win

            property string startPage

            // see-through: the colour is partly transparent and Windows draws a blurred backdrop behind it
            // (WinSettings.nexusOpacity / nexusBackdrop); kept above every app when nexusTopmost is on
            color: Qt.alpha(Colours.palette.m3surface, WinSettings.nexusOpacity)
            surfaceFormat.opaque: false

            function applyFrame(): void {
                const w = nexus.Window.window;
                if (!w) return;
                Win.WindowFrame.setBackdrop(w, WinSettings.nexusBackdrop, !Colours.light);
                Win.WindowFrame.setTopmost(w, WinSettings.nexusTopmost);
            }

            Connections {
                target: WinSettings
                function onNexusBackdropChanged(): void { win.applyFrame(); }
                function onNexusTopmostChanged(): void { win.applyFrame(); }
            }

            // closing: hide first (the video background pauses), destroy a moment later - never in the same
            // instant as the close click / visibility change (that left the GUI thread hung after a few opens)
            property bool closing: false

            function close(): void {
                if (closing)
                    return;
                closing = true;
                if (root.current === win)
                    root.current = null;
                win.visible = false;
                destroyTimer.start();
            }

            Timer {
                id: destroyTimer

                interval: 400
                onTriggered: win.destroy()
            }

            onVisibleChanged: {
                if (!visible)
                    win.close();
                else
                    Qt.callLater(() => {
                        Win.WindowFrame.hideTitleBar(nexus.Window.window);
                        win.applyFrame();
                        Win.WindowFrame.bringToFront(nexus.Window.window);   // not behind the app you are in
                    });
            }

            Component.onCompleted: root.current = win
            Component.onDestruction: if (root.current === win) root.current = null
            onStartPageChanged: nexus.goToStartPage()

            implicitWidth: nexus.implicitWidth
            implicitHeight: nexus.implicitHeight

            minimumSize.width: contentItem.Tokens.sizes.nexus.minWidth
            minimumSize.height: contentItem.Tokens.sizes.nexus.minHeight

            contentItem.Config.screen: screen.name
            contentItem.Tokens.screen: screen.name

            title: Tr.tr("Nexus — %1").arg(PageRegistry.pages[nexus.nState.currentPageIdx].label)

            Nexus {
                id: nexus

                anchors.fill: parent
                nState.screen: win.screen
                nState.isWindow: true
                blobColour: Qt.alpha(Colours.palette.m3surfaceContainerLow, Math.min(1, WinSettings.nexusOpacity + 0.1))
                onClose: win.close()

                function goToStartPage(): void {
                    if (!win.startPage)
                        return;
                    const want = win.startPage.toLowerCase();
                    const i = PageRegistry.pages.findIndex(p => p.label.toLowerCase() === want || p.label.toLowerCase().startsWith(want));
                    if (i >= 0)
                        nState.currentPageIdx = i;
                }

                Component.onCompleted: goToStartPage()
            }

            // no system frame (that is what makes it see-through): resize from the edges and corners
            Repeater {
                model: [
                    { edges: Qt.LeftEdge, x: 0, y: 8, w: 6, h: -16, cursor: Qt.SizeHorCursor },
                    { edges: Qt.RightEdge, x: -6, y: 8, w: 6, h: -16, cursor: Qt.SizeHorCursor },
                    { edges: Qt.TopEdge, x: 8, y: 0, w: -16, h: 6, cursor: Qt.SizeVerCursor },
                    { edges: Qt.BottomEdge, x: 8, y: -6, w: -16, h: 6, cursor: Qt.SizeVerCursor },
                    { edges: Qt.BottomEdge | Qt.RightEdge, x: -12, y: -12, w: 12, h: 12, cursor: Qt.SizeFDiagCursor },
                    { edges: Qt.BottomEdge | Qt.LeftEdge, x: 0, y: -12, w: 12, h: 12, cursor: Qt.SizeBDiagCursor }
                ]

                MouseArea {
                    required property var modelData

                    x: modelData.x < 0 ? win.width + modelData.x : modelData.x
                    y: modelData.y < 0 ? win.height + modelData.y : modelData.y
                    width: modelData.w <= 0 ? win.width + modelData.w : modelData.w
                    height: modelData.h <= 0 ? win.height + modelData.h : modelData.h
                    z: 100
                    cursorShape: modelData.cursor
                    onPressed: nexus.Window.window.startSystemResize(modelData.edges)
                }
            }

            Behavior on color {
                CAnim {}
            }
        }
    }
}
