// Drop-in wheel scrolling for any Flickable, ListView, GridView or ScrollView.
//
// Qt's built-in wheel step on a Flickable is a few pixels per notch, which is
// what makes long menus feel like they barely move. This handler takes the
// wheel events and scrolls by a normal desktop step, clamped to the content.
//
// Usage: put `FastWheel {}` inside the scrollable.
import QtQuick

WheelHandler {
    id: fastWheel

    // Deliberately `var`, not a typed Flickable, and no instanceof test: an
    // earlier version used `parent instanceof Flickable`, which left `view`
    // null while the handler still consumed the wheel event -- so scrolling
    // stopped entirely. Anything that quacks like a Flickable is driven; if it
    // does not, the handler disables itself and Qt's own scrolling is left
    // alone rather than being swallowed.
    property var view: parent
    property real pixelsPerNotch: 120

    readonly property bool usable: view !== null
                                   && view !== undefined
                                   && view.contentY !== undefined
                                   && view.contentHeight !== undefined

    enabled: usable
    target: null

    onWheel: (event) => {
        if (!usable) return;

        var notchesY = event.angleDelta.y / 120.0;
        var notchesX = event.angleDelta.x / 120.0;

        if (notchesY !== 0 && view.contentHeight > view.height) {
            var maxY = view.contentHeight - view.height;
            view.contentY = Math.max(0, Math.min(maxY, view.contentY - notchesY * pixelsPerNotch));
        }
        if (notchesX !== 0 && view.contentWidth > view.width) {
            var maxX = view.contentWidth - view.width;
            view.contentX = Math.max(0, Math.min(maxX, view.contentX - notchesX * pixelsPerNotch));
        }
    }
}
