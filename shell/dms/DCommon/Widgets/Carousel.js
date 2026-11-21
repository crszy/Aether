.pragma library

// M3 multi-browse carousel layout.

function lerp(a, b, t) {
    return a + (b - a) * t;
}

function arrange(width, preferredWidth, smallWidth, spacing) {
    if (width <= 0)
        return null;
    let best = null;
    for (let large = 1;; large++) {
        const size = (width - (large + 1) * spacing - 1.5 * smallWidth) / (large + 0.5);
        if (size <= smallWidth)
            break;
        const cost = Math.abs(size - preferredWidth);
        if (best && cost >= best.cost)
            break;
        best = { large, size, cost };
    }
    const itemWidth = best ? best.size : width;
    const sizes = best ? Array(best.large).fill(itemWidth).concat([(itemWidth + smallWidth) / 2, smallWidth]) : [width];
    const start = keylines(sizes, itemWidth, width, spacing);
    const end = keylines(sizes.slice().reverse(), itemWidth, width, spacing);
    const last = start.length - 2;
    return { itemWidth, spacing, start, end, overhang: start[last].unadjusted - end[last].unadjusted };
}

function keylines(sizes, itemWidth, width, spacing) {
    const result = [];
    let position = 0;
    for (const size of sizes) {
        result.push({ size, offset: position + size / 2 });
        position += size + spacing;
    }
    const focal = sizes.indexOf(itemWidth);
    const pivot = result[focal].offset;
    result.forEach((keyline, i) => keyline.unadjusted = pivot + (i - focal) * (itemWidth + spacing));
    const stride = itemWidth + spacing;
    result.unshift({ size: 0, offset: -spacing, unadjusted: result[0].unadjusted - stride });
    result.push({ size: 0, offset: width + spacing, unadjusted: result[result.length - 1].unadjusted + stride });
    return result;
}

function stateAt(arrangement, count, width, scroll) {
    const { start, end, itemWidth, spacing } = arrangement;
    const maxScroll = Math.max(0, count * itemWidth + (count - 1) * spacing - width);
    const window = Math.min(maxScroll, arrangement.overhang);
    const t = window > 0 ? Math.max(0, Math.min(1, (scroll - (maxScroll - window)) / window)) : 0;
    if (t === 0)
        return start;
    if (t === 1)
        return end;
    return start.map((keyline, i) => ({
        size: lerp(keyline.size, end[i].size, t),
        offset: lerp(keyline.offset, end[i].offset, t),
        unadjusted: lerp(keyline.unadjusted, end[i].unadjusted, t)
    }));
}

function itemGeometry(state, center) {
    const first = state[0];
    const last = state[state.length - 1];
    if (center <= first.unadjusted)
        return { center: first.offset + center - first.unadjusted, size: 0 };
    if (center >= last.unadjusted)
        return { center: last.offset + center - last.unadjusted, size: 0 };
    let i = 0;
    while (state[i + 1].unadjusted < center)
        i++;
    const before = state[i];
    const after = state[i + 1];
    const t = (center - before.unadjusted) / (after.unadjusted - before.unadjusted);
    return { center: lerp(before.offset, after.offset, t), size: lerp(before.size, after.size, t) };
}
