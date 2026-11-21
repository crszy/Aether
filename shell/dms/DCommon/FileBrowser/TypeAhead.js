.pragma library

function find(count, nameAt, start, needle) {
    for (let step = 0; step < count; step++) {
        const index = (start + step) % count;
        if (nameAt(index).toLowerCase().startsWith(needle))
            return index;
    }
    return -1;
}

function next(count, nameAt, current, prefix, text) {
    const letter = text.toLowerCase();
    const repeated = prefix !== "" && Array.from(prefix).every(typed => typed === letter);
    const continued = repeated ? find(count, nameAt, Math.max(0, current + 1), letter) : find(count, nameAt, 0, prefix + letter);
    if (continued >= 0)
        return {
            "index": continued,
            "prefix": prefix + letter
        };
    const fresh = prefix === "" ? -1 : find(count, nameAt, 0, letter);
    return {
        "index": fresh,
        "prefix": fresh >= 0 ? letter : ""
    };
}
