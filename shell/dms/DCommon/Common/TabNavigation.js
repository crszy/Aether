.pragma library
.import "FocusNavigation.js" as FocusNavigation

function focusItem(item) {
    if (!item)
        return false;
    if (item.focusTarget && item.focusTarget !== item)
        return focusItem(item.focusTarget);
    Qt.callLater(() => FocusNavigation.focusItem(item, false));
    return true;
}

function selectNext(tabBar, repeater, step) {
    let index = tabBar.currentIndex;
    if (index < 0 || index >= repeater.count)
        index = step > 0 ? -1 : 0;
    for (let i = 0; i < repeater.count; i++) {
        index = (index + step + repeater.count) % repeater.count;
        const item = repeater.itemAt(index);
        if (!item || item.isAction)
            continue;
        if (index !== tabBar.currentIndex)
            tabBar.tabClicked(index);
        return true;
    }
    return false;
}

function handleKeyEvent(event, tabBar, repeater, rtl) {
    if (!FocusNavigation.containsFocus(tabBar) || repeater.count === 0)
        return false;
    if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
        return false;
    switch (event.key) {
    case Qt.Key_Right:
    case Qt.Key_L:
        return tabBar.enableArrowNavigation && selectNext(tabBar, repeater, rtl ? -1 : 1);
    case Qt.Key_Left:
    case Qt.Key_H:
        return tabBar.enableArrowNavigation && selectNext(tabBar, repeater, rtl ? 1 : -1);
    case Qt.Key_Tab:
        if (tabBar.cycleOnTab)
            return selectNext(tabBar, repeater, event.modifiers & Qt.ShiftModifier ? -1 : 1);
        return focusItem(event.modifiers & Qt.ShiftModifier ? tabBar.previousFocusTarget : tabBar.nextFocusTarget);
    case Qt.Key_Backtab:
        if (tabBar.cycleOnTab)
            return selectNext(tabBar, repeater, -1);
        return focusItem(tabBar.previousFocusTarget);
    case Qt.Key_Up:
    case Qt.Key_K:
        return focusItem(tabBar.previousFocusTarget);
    case Qt.Key_Down:
    case Qt.Key_J:
        return focusItem(tabBar.nextFocusTarget);
    }
    return false;
}
