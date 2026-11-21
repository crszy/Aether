.pragma library

const uriPattern = /^[A-Za-z][A-Za-z0-9+.-]*:\//;

function isUri(text) {
    return uriPattern.test(text);
}

function isPathLike(text) {
    return /^(\/|[A-Za-z]:)/.test(text) || text.startsWith("~") || text.startsWith(".") || text.includes("/");
}

function normalize(path) {
    const parts = [];
    for (const part of path.split("/")) {
        switch (part) {
        case "":
        case ".":
            break;
        case "..":
            parts.pop();
            break;
        default:
            parts.push(part);
        }
    }
    return "/" + parts.join("/");
}

function absolute(text, cwd, home) {
    if (/^(\/|[A-Za-z]:)/.test(text))
        return normalize(text);
    if (text === "~" || text.startsWith("~/"))
        return home === "" ? "" : normalize(home + text.substring(1));
    if (text.startsWith("~") || !/^(\/|[A-Za-z]:)/.test(cwd))
        return "";
    return normalize(cwd + "/" + text);
}

function target(text, cwd, home) {
    const typed = text.trim();
    if (typed === "" || isUri(typed))
        return null;
    if (typed === "~")
        return home === "" ? null : {
            "dir": normalize(home),
            "prefix": "",
            "base": "~/"
        };
    const whole = absolute(typed, cwd, home);
    if (whole === "")
        return null;
    if (!typed.endsWith("/") && /^(\/|[A-Za-z]:)/.test(cwd) && whole === normalize(cwd))
        return {
            "dir": whole,
            "prefix": "",
            "base": typed + "/"
        };
    const slash = typed.lastIndexOf("/");
    const base = typed.substring(0, slash + 1);
    const dir = absolute(base === "" ? "." : base, cwd, home);
    if (dir === "")
        return null;
    return {
        "dir": dir,
        "prefix": typed.substring(slash + 1),
        "base": base
    };
}

function compact(path, home) {
    if (home === "" || home === "/")
        return path;
    if (path === home)
        return "~";
    if (path.startsWith(home + "/"))
        return "~" + path.substring(home.length);
    return path;
}

function split(display) {
    const end = display.length > 1 && display.endsWith("/") ? display.length - 1 : display.length;
    const body = display.substring(0, end);
    const authority = body.indexOf("://");
    const floor = authority >= 0 ? authority + 3 : 0;
    const cut = body.lastIndexOf("/");
    if (cut < floor)
        return {
            "start": floor,
            "end": end,
            "hintEnd": floor
        };
    if (cut === 0)
        return {
            "start": body === "/" ? 0 : 1,
            "end": end,
            "hintEnd": body === "/" ? 0 : 1
        };
    return {
        "start": cut + 1,
        "end": end,
        "hintEnd": cut
    };
}

function ranges(positions, from, to) {
    const sorted = [...new Set(positions)].filter(index => index >= from && index < to).sort((a, b) => a - b);
    const out = [];
    for (const index of sorted) {
        const local = index - from;
        const last = out[out.length - 1];
        if (last && last[1] === local) {
            last[1]++;
            continue;
        }
        out.push([local, local + 1]);
    }
    return out;
}

function row(match, kind) {
    const display = match.display;
    const parts = split(display);
    const positions = match.positions ?? [];
    return {
        "path": match.path,
        "kind": kind,
        "name": display.substring(parts.start, parts.end),
        "hint": display.substring(0, parts.hintEnd),
        "nameMarks": ranges(positions, parts.start, parts.end),
        "hintMarks": ranges(positions, 0, parts.hintEnd),
        "replacement": match.replacement ?? (isUri(display) || display.endsWith("/") ? display : display + "/")
    };
}

function rank(entries, completion, home, limit) {
    const needle = completion.prefix.toLowerCase();
    const starts = [];
    const contains = [];
    for (const entry of entries) {
        const at = entry.name.toLowerCase().indexOf(needle);
        if (at < 0)
            continue;
        (at === 0 ? starts : contains).push({
            "entry": entry,
            "at": at
        });
    }
    return starts.concat(contains).slice(0, limit).map(hit => {
        const display = compact(hit.entry.path, home);
        const offset = split(display).start + hit.at;
        const positions = [];
        for (let index = 0; index < needle.length; index++)
            positions.push(offset + index);
        return row({
            "path": hit.entry.path,
            "display": display,
            "positions": positions,
            "replacement": completion.base + hit.entry.name + "/"
        }, "folder");
    });
}

function merge(first, second) {
    const seen = {};
    const out = [];
    for (const candidate of first.concat(second)) {
        if (seen[candidate.path])
            continue;
        seen[candidate.path] = true;
        out.push(candidate);
    }
    return out;
}

function commonPrefix(texts) {
    if (texts.length === 0)
        return "";
    let prefix = texts[0];
    for (const text of texts) {
        let length = 0;
        while (length < prefix.length && length < text.length && prefix[length] === text[length])
            length++;
        prefix = prefix.substring(0, length);
    }
    return prefix;
}

function tab(text, rows, highlight) {
    if (rows.length === 0)
        return null;
    if (highlight >= 0 && highlight < rows.length)
        return {
            "text": rows[highlight].replacement,
            "highlight": -1
        };
    if (rows.length === 1)
        return {
            "text": rows[0].replacement,
            "highlight": -1
        };
    const typed = text.toLowerCase();
    const extending = rows.map(candidate => candidate.replacement).filter(replacement => replacement.toLowerCase().startsWith(typed));
    const common = commonPrefix(extending);
    if (common.length > text.length)
        return {
            "text": common,
            "highlight": -1
        };
    return {
        "text": text,
        "highlight": 0
    };
}
