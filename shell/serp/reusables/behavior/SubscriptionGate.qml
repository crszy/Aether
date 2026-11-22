// serpantinum (https://github.com/ilyamiro/serpantinum) - Copyright (C) 2026 Illia Miroshnichenko, AGPL-3.0.
// Windows changes by dotfiles/serp-port.js (Aether, AGPL-3.0-or-later).
import QtQuick

QtObject {
    id: gate

    property var target: null
    property string acquire: "subscribe"
    property string release: "unsubscribe"
    property bool wanted: false
    property bool active: false
    property bool ready: false

    function sync() {
        if (!ready || !target) return;
        if (wanted && !active) {
            active = true;
            target[acquire]();
        } else if (!wanted && active) {
            active = false;
            target[release]();
        }
    }

    onWantedChanged: sync()
    Component.onCompleted: {
        ready = true;
        sync();
    }
    Component.onDestruction: {
        if (active && target) {
            active = false;
            target[release]();
        }
    }
}
