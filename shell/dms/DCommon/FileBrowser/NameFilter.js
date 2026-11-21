.pragma library

function isPattern(query) {
    return query.includes("*") || query.includes("?");
}

function needle(query) {
    if (!isPattern(query))
        return query;
    return query.split(/[*?]+/).reduce((longest, part) => part.length > longest.length ? part : longest, "");
}

function wildcard(name, pattern) {
    let at = 0;
    let p = 0;
    let star = -1;
    let mark = 0;
    while (at < name.length) {
        if (p < pattern.length && (pattern[p] === "?" || pattern[p] === name[at])) {
            at++;
            p++;
            continue;
        }
        if (p < pattern.length && pattern[p] === "*") {
            star = p++;
            mark = at;
            continue;
        }
        if (star < 0)
            return false;
        p = star + 1;
        at = ++mark;
    }
    while (p < pattern.length && pattern[p] === "*")
        p++;
    return p === pattern.length;
}

function compile(query) {
    const folded = query.toLowerCase();
    if (folded === "")
        return {
            "test": () => true
        };
    if (!isPattern(folded))
        return {
            "test": name => name.toLowerCase().includes(folded)
        };
    return {
        "test": name => wildcard(name.toLowerCase(), folded)
    };
}

function entryMatches(matcher, entry) {
    const display = entry.displayName ?? "";
    return matcher.test(entry.name) || (display !== "" && matcher.test(display));
}
