.pragma library

function percentToUnit(v) {
    if (v === undefined || v === null)
        return undefined;
    return v > 1 ? v / 100 : v;
}

function mergeSpec(shared, local) {
    var out = {};
    for (var k in shared)
        out[k] = shared[k];
    for (var k in local) {
        if (k in out)
            console.warn("spec: local key shadows the shared schema:", k);
        out[k] = local[k];
    }
    return out;
}

function stableStringify(value) {
    if (value === null || typeof value !== "object")
        return JSON.stringify(value);
    if (Array.isArray(value))
        return "[" + value.map(stableStringify).join(",") + "]";
    return "{" + Object.keys(value).sort().map(function (k) {
        return JSON.stringify(k) + ":" + stableStringify(value[k]);
    }).join(",") + "}";
}

function isDefault(value, def) {
    return stableStringify(value) === stableStringify(def);
}

function cloneDef(def) {
    if (def === null || typeof def !== "object")
        return def;
    return JSON.parse(JSON.stringify(def));
}

function stripDefaults(obj, SPEC) {
    for (var k in SPEC) {
        if (!(k in obj))
            continue;
        if (isDefault(obj[k], SPEC[k].def))
            delete obj[k];
    }
}
