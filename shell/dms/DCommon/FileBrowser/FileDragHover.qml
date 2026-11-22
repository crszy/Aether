pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell

Singleton {
    property Item area: null
    property string dest: ""
    property point point: Qt.point(0, 0)
    property int proposedAction: Qt.IgnoreAction
    property bool scrolled: false

    function track(source, path, drag) {
        area = source;
        dest = path;
        point = source.mapToItem(null, drag.x, drag.y);
        proposedAction = drag.proposedAction;
        scrolled = false;
    }

    function release(source) {
        if (area !== source)
            return;
        area = null;
        dest = "";
    }
}
