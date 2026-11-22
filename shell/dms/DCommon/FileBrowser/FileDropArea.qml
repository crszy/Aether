pragma ComponentBehavior: Bound

import QtQuick

DropArea {
    id: area

    property string dropPath: ""
    property bool springLoad: false
    property Item scrollView: null

    signal springLoaded

    onEntered: drag => {
        FileDragHover.track(area, dropPath, drag);
        if (springLoad)
            spring.restart();
    }
    onPositionChanged: drag => FileDragHover.track(area, dropPath, drag)
    onContainsDragChanged: {
        if (containsDrag)
            return;
        spring.stop();
        FileDragHover.release(area);
    }
    onDropped: {
        spring.stop();
        FileDragHover.release(area);
    }
    Component.onDestruction: FileDragHover.release(area)

    Timer {
        id: spring

        interval: FileBrowserMetrics.springLoadDelay
        onTriggered: {
            if (FileDragHover.scrolled)
                return;
            area.springLoaded();
        }
    }
}
