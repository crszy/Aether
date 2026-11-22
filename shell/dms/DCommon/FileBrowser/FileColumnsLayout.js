.pragma library

function rootFor(activePath, homePath) {
    if (homePath !== "" && (activePath === homePath || activePath.startsWith(homePath + "/")))
        return homePath;
    return "/";
}

function chainFor(activePath, homePath, parentOf) {
    const root = rootFor(activePath, homePath);
    const out = [];
    let current = parentOf(activePath);
    while (current !== "" && current.length >= root.length) {
        out.unshift(current);
        if (current === root)
            break;
        current = parentOf(current);
    }
    return out;
}

function columnWidth(stripWidth, previewWidth, minWidth, maxWidth) {
    return Math.floor(Math.max(minWidth, Math.min(maxWidth, stripWidth - previewWidth)));
}

function revealTarget(contentX, depth, columnWidth, previewWidth, stripWidth) {
    const activeX = depth * columnWidth;
    const activeEnd = activeX + columnWidth;
    const stripEnd = Math.max(activeEnd + previewWidth, stripWidth);
    let x = Math.min(contentX, stripEnd - stripWidth);
    if (x > activeX)
        x = activeX;
    else if (x < activeEnd + previewWidth - stripWidth)
        x = activeEnd + previewWidth - stripWidth;
    return {
        "contentX": Math.max(0, x),
        "stripEnd": stripEnd
    };
}
