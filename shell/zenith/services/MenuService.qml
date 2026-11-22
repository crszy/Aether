import QtQuick
import Quickshell

pragma Singleton

QtObject {
    id: root

    property var openMenus: []

    function register(menu) {
        if (!openMenus.includes(menu)) {
            let newMenus = [...openMenus, menu];
            openMenus = newMenus;
        }
    }

    function unregister(menu) {
        if (!menu) return;
        let index = openMenus.indexOf(menu);
        if (index !== -1) {
            let newMenus = [...openMenus];
            newMenus.splice(index, 1);
            openMenus = newMenus;
        }
    }

    // Emitted so anything holding a menu can react. Nothing here closes a
    // window directly: these surfaces bind their visibility to service state,
    // and writing to `visible` destroys that binding, after which the menu
    // never opens again.
    //
    // The per-service closes used to sit here behind
    // `typeof X !== "undefined"` guards. This file imports only QtQuick and
    // Quickshell, so those names never resolved, all three guards were always
    // false, and closeAll() silently closed nothing. DismissOverlay does that
    // job now -- it imports the services and is what observes the click.
    signal closeRequested()

    function closeAll() {
        openMenus = [];
        closeRequested();
    }
}

