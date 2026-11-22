// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import Quickshell
import Quickshell.Services.Notifications
import QtQuick

// The notification server of the shell (only while Settings > Notifications says the shell shows them; swaync is
// stopped then). Every notification is kept until you dismiss it in the control center.
NotificationServer {
    id: srv
    signal arrived(var n)
    keepOnReload: true
    actionsSupported: true
    bodySupported: true
    bodyMarkupSupported: true
    bodyHyperlinksSupported: true
    imageSupported: true
    persistenceSupported: true
    onNotification: function(n) { n.tracked = true; srv.arrived(n) }
}
