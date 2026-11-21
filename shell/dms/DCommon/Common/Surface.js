.pragma library

function foregroundAlpha(enabled, opacity) {
    if (!enabled)
        return 0;
    if (typeof opacity !== "number" || !isFinite(opacity))
        return 1;
    return Math.max(0, Math.min(1, opacity));
}

function isFloatingWindow(item) {
    for (let current = item; current; current = current.parent) {
        if (typeof current.isFloatingWindowSurface === "boolean")
            return current.isFloatingWindowSurface;
        if (current.disablePopupTransparency === true)
            return true;
    }
    return false;
}
