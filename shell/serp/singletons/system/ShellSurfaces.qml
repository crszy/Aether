// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
pragma Singleton
import QtQuick

QtObject {
    id: root

    property var bars: ({})
    property var docks: ({})

    function register(kind, screenName, window) {
        if (!screenName || !window) return;
        root[kind][screenName] = window;
    }

    function unregister(kind, screenName, window) {
        if (root[kind][screenName] === window) delete root[kind][screenName];
    }
}
