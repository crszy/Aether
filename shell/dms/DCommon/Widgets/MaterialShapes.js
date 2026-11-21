.pragma library
.import "MaterialShapeData.js" as Data
.import "DLogoShape.js" as Logo

var catalog = Object.assign({
    dankLogo: Logo.cubics
}, Data.cubics);

function rotationScale(kind, aspectRatio = 1) {
    const cubics = catalog[kind] ?? catalog.circle;
    let radiusSquared = 0;
    for (const cubic of cubics) {
        const centered = cubic.map((value, index) => (value - 0.5) * (index % 2 === 0 ? aspectRatio : 1));
        radiusSquared = Math.max(radiusSquared, cubicRadiusSquared(centered, 4));
    }
    return radiusSquared > 0 ? 0.5 / Math.sqrt(radiusSquared) : 1;
}

function cubicRadiusSquared(cubic, depth) {
    if (depth === 0) {
        let radiusSquared = 0;
        for (let i = 0; i < cubic.length; i += 2)
            radiusSquared = Math.max(radiusSquared, cubic[i] * cubic[i] + cubic[i + 1] * cubic[i + 1]);
        return radiusSquared;
    }
    const midpoint = (a, b) => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
    const start = cubic.slice(0, 2);
    const control1 = cubic.slice(2, 4);
    const control2 = cubic.slice(4, 6);
    const end = cubic.slice(6, 8);
    const a = midpoint(start, control1);
    const b = midpoint(control1, control2);
    const c = midpoint(control2, end);
    const d = midpoint(a, b);
    const e = midpoint(b, c);
    const split = midpoint(d, e);
    return Math.max(cubicRadiusSquared(start.concat(a, d, split), depth - 1), cubicRadiusSquared(split.concat(e, c, end), depth - 1));
}

var polarProfiles = {};

function cubicPoint(cubic, t) {
    const u = 1 - t;
    const a = u * u * u;
    const b = 3 * u * u * t;
    const c = 3 * u * t * t;
    const d = t * t * t;
    return [a * cubic[0] + b * cubic[2] + c * cubic[4] + d * cubic[6], a * cubic[1] + b * cubic[3] + c * cubic[5] + d * cubic[7]];
}

function polarProfile(kind, samples) {
    const key = kind + "/" + samples;
    if (polarProfiles[key])
        return polarProfiles[key];
    const outline = [];
    for (const cubic of catalog[kind] ?? catalog.circle) {
        for (let i = 0; i < 16; i++) {
            const point = cubicPoint(cubic, i / 16);
            outline.push({
                angle: Math.atan2(point[1] - 0.5, point[0] - 0.5),
                radius: Math.hypot(point[0] - 0.5, point[1] - 0.5)
            });
        }
    }
    outline.sort((a, b) => a.angle - b.angle);
    const turn = 2 * Math.PI;
    const radii = [];
    let next = 0;
    for (let i = 0; i < samples; i++) {
        const angle = -Math.PI + turn * i / samples;
        while (next < outline.length && outline[next].angle < angle)
            next++;
        const after = outline[next % outline.length];
        const before = outline[(next + outline.length - 1) % outline.length];
        const span = (after.angle - before.angle + turn) % turn || turn;
        const offset = (angle - before.angle + turn) % turn;
        radii.push(before.radius + (after.radius - before.radius) * offset / span);
    }
    polarProfiles[key] = radii;
    return radii;
}

function polarPoints(radii, scale) {
    return radii.map((radius, i) => {
        const angle = -Math.PI + 2 * Math.PI * i / radii.length;
        return [Math.cos(angle) * radius * scale, Math.sin(angle) * radius * scale];
    });
}

function polarBounds(radii) {
    const points = polarPoints(radii, 1);
    const xs = points.map(point => point[0]);
    const ys = points.map(point => point[1]);
    return {
        x: Math.min(...xs),
        y: Math.min(...ys),
        width: Math.max(...xs) - Math.min(...xs),
        height: Math.max(...ys) - Math.min(...ys)
    };
}

// Compose divides by the control-point box, which is the unit square for these normalized shapes.
function morphScale(profiles) {
    return Math.min(1, ...profiles.map(radii => 1 / (2 * Math.max(...radii))));
}

function buildPath(kind, width, height, square) {
    if (width <= 0 || height <= 0)
        return "";
    if (square)
        return "M 0 0 H " + width + " V " + height + " H 0 Z";
    const cubics = catalog[kind] ?? catalog.circle;
    const pair = (x, y) => (x * width).toFixed(4) + " " + (y * height).toFixed(4);
    let path = "";
    let end = null;
    for (const cubic of cubics) {
        if (!end || end[0] !== cubic[0] || end[1] !== cubic[1])
            path += (end ? " Z " : "") + "M " + pair(cubic[0], cubic[1]);
        path += " C " + pair(cubic[2], cubic[3]) + " " + pair(cubic[4], cubic[5]) + " " + pair(cubic[6], cubic[7]);
        end = [cubic[6], cubic[7]];
    }
    return path + " Z";
}
