pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell

Singleton {
    id: root

    property bool ready: false
    property string _listing: "\n"

    function has(name) {
        return _listing.indexOf("\n" + name + "\n") !== -1;
    }

    function add(name) {
        if (!name || has(name))
            return;
        _listing += name + "\n";
    }

    Component.onCompleted: {
        Proc.runCommand(null, ["ls", "-1", Paths.strip(Paths.imagecache)], (output, exitCode) => {
            if (exitCode !== 0)
                return;
            root._listing = "\n" + output + root._listing.substring(1);
            root.ready = true;
        }, 0);
    }
}
