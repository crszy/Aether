.pragma library

function fit(ids, available, stored, defaultFor, gap) {
    const kept = [];
    const widths = {};
    let room = available;
    for (const id of ids) {
        const fallback = defaultFor(id);
        const wanted = stored[id] ?? fallback;
        if (room - gap < Math.min(wanted, fallback))
            break;
        const width = Math.min(wanted, room - gap);
        kept.push(id);
        widths[id] = width;
        room -= width + gap;
    }
    return {
        "ids": kept,
        "widths": widths,
        "slack": Math.max(0, room)
    };
}

function clampResize(width, current, slack, minWidth) {
    return Math.max(minWidth, Math.min(width, current + slack));
}
