pragma ComponentBehavior: Bound

import QtQuick

FrameAnimation {
    id: autoScroll

    property Item target: null
    property real direction: 0

    signal scrolled

    running: target !== null && direction !== 0

    onTriggered: {
        const view = target;
        const maximum = view.originY + Math.max(0, view.contentHeight - view.height);
        const next = Math.max(view.originY, Math.min(maximum, view.contentY + direction * FileBrowserMetrics.listRowHeight * FileBrowserMetrics.dragScrollRowsPerSecond * frameTime));
        if (next === view.contentY)
            return;
        view.contentY = next;
        scrolled();
    }
}
