.pragma library
.import "../Widgets/MaterialShapes.js" as Shapes
.import "Accents.js" as Accents
.import "Hct.js" as Hct

var builtinIds = ["dank", "bloom", "orbit", "garden", "petal", "dune"];
var defaultPreset = "dank";
var defaultPattern = "topography";
var defaultStrength = 0.1;
var defaultSeed = "#d0bcff";
var designWidth = 1920;
var designHeight = 1080;
var patterns = ["none", "bank-note", "bubbles", "endless-clouds", "graph-paper", "plus", "topography", "wiggle"];
var fills = ["primary", "secondary", "tertiary", "primaryContainer", "secondaryContainer", "tertiaryContainer", "inversePrimary", "black"];
var strengthRange = [0.02, 0.4];
var sizeRange = [0.05, 4];
var opacityRange = [0.05, 1];
var blobEdgeRange = [3, 15];
var smoothnessRoundness = [0, 0.3, 0.55, 0.8, 0.9];
var maxLayers = 64;
var maxProfiles = 128;
var maxPathLength = 4000;
var maxNameLength = 64;
var maxPayloadLength = 262144;
var idPattern = /^[A-Za-z0-9_-]{1,64}$/;
var safePath = /^[MCLZ0-9eE.,\s+-]+$/;

function layer(shape, x, y, size, rotation, fill, overlap = "none") {
    return {
        shape,
        x,
        y,
        size,
        rotation,
        fill,
        overlap
    };
}

function preset(id, seed = defaultSeed) {
    return Object.assign({
        version: 1,
        seed
    }, builtinLayout(id) ?? builtinLayout(defaultPreset));
}

function builtinLayout(id) {
    let layers;
    let pattern = defaultPattern;
    let strength = defaultStrength;
    switch (id) {
    case "dank":
        layers = [layer("softBurst", 0.842, 0.45, 1.5, 0, "primaryContainer"), layer("dankLogo", 0.842, 0.45, 0.38, 0, "primary"), layer("sunny", 0.133, 0.21, 0.26, 10, "tertiary"), layer("pill", 0.241, 0.91, 0.5, -25, "secondaryContainer")];
        break;
    case "bloom":
        layers = [layer("flower", 0.903, 0.495, 1.15, 12, "primaryContainer"), layer("circle", 0.497, 0.75, 0.7, 0, "secondary", "tertiaryContainer"), layer("cookie6", 0.144, 0.27, 0.3, 0, "primary")];
        break;
    case "orbit":
        layers = [layer("circle", 0.9, 1.02, 1.6, 0, "primaryContainer"), layer("pill", 0.373, 0.25, 0.9, -35, "secondary", "secondaryContainer"), layer("circle", 0.119, 0.21, 0.14, 0, "tertiary")];
        pattern = "bubbles";
        strength = 0.08;
        break;
    case "garden":
        layers = [layer("puffy", 0.117, 0.175, 0.95, 0, "tertiary"), layer("circle", 0.575, 0.625, 1.05, 0, "primaryContainer", "tertiaryContainer"), layer("clover4", 0.867, 0.855, 0.95, 15, "secondary", "secondaryContainer"), layer("circle", 0.871, 0.15, 0.18, 0, "primary")];
        pattern = "graph-paper";
        strength = 0.08;
        break;
    case "petal":
        layers = [layer("square", 0.119, 0.21, 0.78, 0, "primary"), layer("softBoom", 0.239, 0.425, 0.85, 0, "primaryContainer", "tertiaryContainer"), layer("arrow", 0.726, 0.95, 1.3, 225, "tertiary"), layer("boom", 0.943, 0.695, 1.15, 10, "secondary", "primaryContainer")];
        pattern = "plus";
        strength = 0.15;
        break;
    case "dune":
        layers = [layer("semiCircle", 0.711, 0.555, 0.75, 0, "secondary"), layer("oval", 0.544, 1.2, 1.4, -10, "primaryContainer", "tertiaryContainer"), layer("sunny", 0.156, 0.2, 0.2, 0, "tertiary")];
        pattern = "wiggle";
        break;
    default:
        return null;
    }
    return {
        preset: id,
        name: "",
        layers,
        pattern,
        strength
    };
}

function validSeed(value) {
    return /^#[\da-f]{6}$/i.test(value);
}

function hueOf(seed) {
    return Hct.toHct(Qt.color(seed)).hue;
}

function seedForHue(hue, reference = defaultSeed) {
    const key = Hct.toHct(Qt.color(reference));
    return String(Hct.fromHct(hue, key.chroma, key.tone));
}

var seedCandidateCache = null;

function seedCandidates() {
    if (seedCandidateCache)
        return seedCandidateCache;
    const defaultHue = Hct.toHct(Qt.color(defaultSeed)).hue;
    const distance = slot => Math.abs(Accents.ANCHOR_HUES[slot] - defaultHue);
    const defaultSlot = Accents.SLOTS.reduce((best, slot) => distance(slot) < distance(best) ? slot : best);
    seedCandidateCache = Accents.SLOTS.map(slot => ({
                slot,
                seed: slot === defaultSlot ? defaultSeed : seedForHue(Accents.ANCHOR_HUES[slot])
            }));
    return seedCandidateCache;
}

function finite(value) {
    return typeof value === "number" && isFinite(value);
}

function clamp(value, range) {
    return Math.min(range[1], Math.max(range[0], value));
}

function round(value, digits = 3) {
    const factor = Math.pow(10, digits);
    return Math.round(value * factor) / factor;
}

var shapeKeys = null;

function shapeKey(name) {
    if (typeof name !== "string")
        return null;
    if (!shapeKeys) {
        shapeKeys = {};
        for (const key of Object.keys(Shapes.catalog))
            shapeKeys[key.toLowerCase()] = key;
    }
    return shapeKeys[name.replace(/(Sided|Leaf)$/, "").toLowerCase()] ?? null;
}

function normalizePoints(value) {
    if (!Array.isArray(value) || value.length < blobEdgeRange[0] || value.length > blobEdgeRange[1])
        return null;
    const points = [];
    for (const point of value) {
        if (!point || !finite(point.x) || !finite(point.y))
            return null;
        points.push({
            x: round(point.x, 4),
            y: round(point.y, 4)
        });
    }
    return points;
}

function normalizeLayer(raw, index) {
    if (!raw || typeof raw !== "object")
        return null;
    const blob = raw.shape === "blob";
    const shape = blob ? "blob" : shapeKey(raw.shape);
    if (!shape || !finite(raw.x) || !finite(raw.y) || !finite(raw.size) || !fills.includes(raw.fill))
        return null;
    const result = {
        shape,
        x: round(raw.x),
        y: round(raw.y),
        size: round(clamp(raw.size, sizeRange)),
        rotation: finite(raw.rotation) ? round(raw.rotation, 1) : 0,
        fill: raw.fill,
        overlap: index > 0 && fills.includes(raw.overlap) ? raw.overlap : "none"
    };
    if (finite(raw.opacity) && raw.opacity < 1)
        result.opacity = round(clamp(raw.opacity, opacityRange), 2);
    if (!blob)
        return result;
    const points = normalizePoints(raw.points);
    const smoothness = Number.isInteger(raw.smoothness) && raw.smoothness >= 0 && raw.smoothness < smoothnessRoundness.length ? raw.smoothness : 0;
    const path = typeof raw.path === "string" && raw.path.length <= maxPathLength && safePath.test(raw.path) ? raw.path : null;
    if (!path && !points)
        return null;
    result.path = path ?? blobPath(points, smoothness);
    if (points) {
        result.points = points;
        result.smoothness = smoothness;
    }
    return result;
}

function normalizeLayout(value) {
    if (!value || typeof value !== "object" || !Array.isArray(value.layers))
        return null;
    const layers = [];
    for (const raw of value.layers.slice(0, maxLayers)) {
        const normalized = normalizeLayer(raw, layers.length);
        if (normalized)
            layers.push(normalized);
    }
    if (!layers.length)
        return null;
    return {
        name: typeof value.name === "string" ? value.name.trim().slice(0, maxNameLength) : "",
        layers,
        pattern: patterns.includes(value.pattern) ? value.pattern : defaultPattern,
        strength: finite(value.strength) ? round(clamp(value.strength, strengthRange), 2) : defaultStrength
    };
}

function normalizeEntry(value) {
    const valid = value && typeof value === "object";
    return {
        preset: valid && typeof value.preset === "string" && idPattern.test(value.preset) ? value.preset : defaultPreset,
        seed: valid && validSeed(value.seed) ? value.seed.toLowerCase() : defaultSeed
    };
}

function normalizeStore(value) {
    const result = {};
    if (!value || typeof value !== "object" || Array.isArray(value))
        return result;
    for (const key of Object.keys(value).slice(0, 64)) {
        if (key === "__proto__" || key === "constructor" || key === "prototype")
            continue;
        const slots = value[key];
        if (!slots || typeof slots !== "object")
            continue;
        const target = {};
        for (const slot of ["shared", "light", "dark"]) {
            if (slots[slot] && typeof slots[slot] === "object")
                target[slot] = normalizeEntry(slots[slot]);
        }
        result[key] = target;
    }
    return result;
}

function normalizeProfiles(value) {
    const result = {};
    if (!value || typeof value !== "object" || Array.isArray(value))
        return result;
    for (const key of Object.keys(value).slice(0, maxProfiles)) {
        if (!idPattern.test(key) || key === "constructor")
            continue;
        if (value[key] === null) {
            if (builtinIds.includes(key))
                result[key] = null;
            continue;
        }
        if (builtinIds.includes(key))
            continue;
        const layout = normalizeLayout(value[key]);
        if (layout)
            result[key] = layout;
    }
    return result;
}

function layoutOf(id, store) {
    if (builtinIds.includes(id))
        return store?.[id] === null ? null : builtinLayout(id);
    const layout = store?.[id];
    return layout && typeof layout === "object" ? layout : null;
}

function profiles(store) {
    const result = [];
    for (const id of builtinIds) {
        if (store?.[id] !== null)
            result.push({
                id,
                name: "",
                builtin: true
            });
    }
    for (const id of Object.keys(store ?? {})) {
        if (builtinIds.includes(id) || !store[id])
            continue;
        result.push({
            id,
            name: store[id].name || id,
            builtin: false
        });
    }
    return result;
}

function hiddenBuiltins(store) {
    return builtinIds.filter(id => store?.[id] === null);
}

function profileId(name, store) {
    const base = (name || "").toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 48) || "wallpaper";
    const taken = id => builtinIds.includes(id) || (store && id in store);
    if (!taken(base))
        return base;
    for (let n = 2; ; n++) {
        if (!taken(base + "-" + n))
            return base + "-" + n;
    }
}

function entry(store, key, slot) {
    const target = store[key] ?? store[""] ?? {};
    return target[slot] ?? target.shared ?? store[""]?.[slot] ?? store[""]?.shared ?? normalizeEntry(null);
}

function resolvePreset(id, store) {
    if (layoutOf(id, store))
        return id;
    return profiles(store)[0]?.id ?? defaultPreset;
}

function composition(value, profileStore) {
    const normalized = normalizeEntry(value);
    const id = resolvePreset(normalized.preset, profileStore);
    return Object.assign({
        version: 1,
        preset: id,
        seed: normalized.seed
    }, layoutOf(id, profileStore) ?? builtinLayout(defaultPreset));
}

var base64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

function decodePayload(text) {
    if (typeof text !== "string" || !text || text.length > maxPayloadLength)
        return "";
    const input = text.replace(/=+$/, "").replace(/\+/g, "-").replace(/\//g, "_");
    let bits = 0;
    let buffer = 0;
    let encoded = "";
    for (const char of input) {
        const value = base64Alphabet.indexOf(char);
        if (value < 0)
            return "";
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits < 8)
            continue;
        bits -= 8;
        encoded += "%" + ((buffer >> bits) & 0xff).toString(16).padStart(2, "0");
    }
    try {
        return decodeURIComponent(encoded);
    } catch (error) {
        return "";
    }
}

function parseInstallPayload(text) {
    const json = decodePayload(text);
    if (!json)
        return null;
    try {
        return normalizeLayout(JSON.parse(json));
    } catch (error) {
        return null;
    }
}

function roundPoints(points, t) {
    if (t === 0)
        return points;
    const n = points.length;
    const polar = points.map(p => ({
                a: Math.atan2(p.y - 0.5, p.x - 0.5),
                r: Math.hypot(p.x - 0.5, p.y - 0.5)
            }));
    const meanR = polar.reduce((sum, p) => sum + p.r, 0) / n;
    return polar.map(({
            a,
            r
        }, i) => {
        const ideal = i / n * Math.PI * 2;
        const delta = Math.atan2(Math.sin(a - ideal), Math.cos(a - ideal));
        const na = ideal + delta * (1 - t);
        const nr = r + (meanR - r) * t;
        return {
            x: 0.5 + Math.cos(na) * nr,
            y: 0.5 + Math.sin(na) * nr
        };
    });
}

function clampVec(vx, vy, maxLen) {
    const len = Math.hypot(vx, vy);
    if (len === 0 || len <= maxLen)
        return [vx, vy];
    return [vx * maxLen / len, vy * maxLen / len];
}

var blobTension = 1 / 6;
var blobHandleRatio = 0.5;

function blobCubics(rawPoints, smoothness) {
    const points = roundPoints(rawPoints, smoothnessRoundness[smoothness] ?? 0);
    const n = points.length;
    const cubics = [];
    for (let i = 0; i < n; i++) {
        const p0 = points[(i - 1 + n) % n];
        const p1 = points[i];
        const p2 = points[(i + 1) % n];
        const p3 = points[(i + 2) % n];
        const maxLen = Math.hypot(p2.x - p1.x, p2.y - p1.y) * blobHandleRatio;
        const [h1x, h1y] = clampVec((p2.x - p0.x) * blobTension, (p2.y - p0.y) * blobTension, maxLen);
        const [h2x, h2y] = clampVec((p3.x - p1.x) * blobTension, (p3.y - p1.y) * blobTension, maxLen);
        cubics.push([p1.x, p1.y, p1.x + h1x, p1.y + h1y, p2.x - h2x, p2.y - h2y, p2.x, p2.y].map(v => round(v, 4)));
    }
    return cubics;
}

function blobPath(points, smoothness) {
    const cubics = blobCubics(points, smoothness);
    let d = "M" + cubics[0][0] + " " + cubics[0][1];
    for (const c of cubics)
        d += "C" + c.slice(2).join(" ");
    return d + "Z";
}

function pathCubics(d) {
    const tokens = d.match(/[MLCZ]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?/g) ?? [];
    const cubics = [];
    let command = "";
    let start = null;
    let current = null;
    let i = 0;
    const number = () => parseFloat(tokens[i++]);
    const line = to => {
        cubics.push([current[0], current[1], current[0] + (to[0] - current[0]) / 3, current[1] + (to[1] - current[1]) / 3, current[0] + (to[0] - current[0]) * 2 / 3, current[1] + (to[1] - current[1]) * 2 / 3, to[0], to[1]]);
        current = to;
    };
    while (i < tokens.length) {
        if (/^[MLCZ]$/.test(tokens[i]))
            command = tokens[i++];
        switch (command) {
        case "M":
            if (i + 1 >= tokens.length)
                return cubics;
            current = [number(), number()];
            start = current;
            command = "L";
            break;
        case "L":
            if (!current || i + 1 >= tokens.length)
                return cubics;
            line([number(), number()]);
            break;
        case "C":
            if (!current || i + 5 >= tokens.length)
                return cubics;
            cubics.push([current[0], current[1], number(), number(), number(), number(), number(), number()]);
            current = cubics[cubics.length - 1].slice(6);
            break;
        case "Z":
            if (current && start && (current[0] !== start[0] || current[1] !== start[1]))
                line(start);
            command = "";
            break;
        default:
            return cubics;
        }
    }
    return cubics;
}

var blobCache = {};
var blobCacheSize = 0;

function cubicsOf(value) {
    if (value.shape !== "blob")
        return Shapes.catalog[value.shape] ?? Shapes.catalog.circle;
    const key = value.path ?? "";
    if (blobCache[key])
        return blobCache[key];
    if (blobCacheSize >= 64) {
        blobCache = {};
        blobCacheSize = 0;
    }
    const cubics = key ? pathCubics(key) : blobCubics(value.points, value.smoothness);
    blobCache[key] = cubics.length ? cubics : Shapes.catalog.circle;
    blobCacheSize++;
    return blobCache[key];
}

function colorOf(palette, name) {
    if (name === "black")
        return "#000000";
    return palette[name] ?? palette.primary;
}

function place(ctx, value, width, height) {
    const size = value.size * Math.min(width, height);
    ctx.translate(value.x * width, value.y * height);
    ctx.rotate(value.rotation * Math.PI / 180);
    ctx.scale(size, size);
    ctx.translate(-0.5, -0.5);
}

function outline(ctx, value, width, height) {
    ctx.save();
    place(ctx, value, width, height);
    let end = null;
    for (const c of cubicsOf(value)) {
        if (!end || end[0] !== c[0] || end[1] !== c[1]) {
            if (end)
                ctx.closePath();
            ctx.moveTo(c[0], c[1]);
        }
        ctx.bezierCurveTo(c[2], c[3], c[4], c[5], c[6], c[7]);
        end = [c[6], c[7]];
    }
    ctx.closePath();
    ctx.restore();
}

function trace(ctx, value, width, height) {
    ctx.beginPath();
    outline(ctx, value, width, height);
}

function clipOutside(ctx, value, width, height) {
    ctx.fillRule = Qt.OddEvenFill;
    ctx.beginPath();
    ctx.rect(-width, -height, width * 3, height * 3);
    outline(ctx, value, width, height);
    ctx.clip();
    ctx.fillRule = Qt.WindingFill;
}
