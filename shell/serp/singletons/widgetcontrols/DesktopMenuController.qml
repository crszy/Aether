// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
pragma Singleton
import QtQuick
import Quickshell
import "../../"

Item {
    id: controller

    signal menuTriggered()

    property bool isVisible: false
    property real targetX: 0
    property real targetY: 0
    property var screen: null
    property string mode: "desktop"
    property string targetWidgetId: ""
    property real menuOriginX: 0.5
    property real menuOriginY: 0.5
    property bool isMenuShuffle: false

    function openAt(scr, posX, posY, menuMode, widgetId) {
        controller.screen = scr;
        controller.targetX = posX;
        controller.targetY = posY;
        controller.mode = menuMode !== undefined ? menuMode : "desktop";
        controller.targetWidgetId = widgetId !== undefined ? String(widgetId) : "";
        controller.isVisible = true;
        controller.menuTriggered();
    }

    function show(scr, posX, posY, menuMode, widgetId) {
        openAt(scr, posX, posY, menuMode, widgetId);
    }

    function hide() {
        controller.isVisible = false;
    }

    function toggle(scr, posX, posY, menuMode, widgetId) {
        let m = menuMode !== undefined ? menuMode : "desktop";
        let wId = widgetId !== undefined ? String(widgetId) : "";
        let sName = (scr && scr.name) ? scr.name : "";
        let curSName = (controller.screen && controller.screen.name) ? controller.screen.name : "";

        if (controller.isVisible) {
            let sameScreen = (!sName || !curSName || sName === curSName);
            let dx = Math.abs(controller.targetX - posX);
            let dy = Math.abs(controller.targetY - posY);
            let isSamePos = sameScreen && (dx < 10 && dy < 10);

            if (m === "widget") {
                // Widget logic: hide if clicked exact same spot, otherwise fall through to move/reopen
                if (controller.mode === "widget" && controller.targetWidgetId === wId && isSamePos) {
                    controller.hide();
                    return;
                }
            } else if (m === "desktop") {
                // Desktop logic: hide whenever clicked on wallpaper again while in desktop mode
                if (controller.mode === "desktop") {
                    controller.hide();
                    return;
                }
            }
        }

        openAt(scr, posX, posY, m, wId);
    }
}
