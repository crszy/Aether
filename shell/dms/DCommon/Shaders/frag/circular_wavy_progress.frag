#version 450

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float widthPx;
    float heightPx;
    float radiusPx;      // radius of the track circle / outer wave peaks (centerline of the strokes)
    float waveCount;     // number of waves around the circle (integer valued)
    float ampPx;         // radial half-amplitude in px (already scaled by animated amplitude)
    float phase;         // wave offset in radians
    float theta0;        // start angle in radians (includes indeterminate rotation)
    float sweep;         // progress sweep in radians
    float trackStart;    // track arc start, radians relative to theta0
    float trackStop;     // track arc end, radians relative to theta0
    float lineWidthPx;
    float trackWidthPx;
    float showTrack;
    float showProgress;
    vec4 fillColor;      // straight alpha
    vec4 trackColor;     // straight alpha
} ubuf;

const float TAU = 6.28318530718;
const float AA = 0.75;

// Composite a straight-alpha color over a premultiplied accumulator.
vec4 blendOver(vec4 dst, vec3 rgb, float a) {
    return vec4(rgb * a + dst.rgb * (1.0 - a), a + dst.a * (1.0 - a));
}

float wrapTau(float a) { return a - TAU * floor(a / TAU); }

// Soft disc mask centered at c with radius r.
float disc(vec2 p, vec2 c, float r) {
    return 1.0 - smoothstep(-AA, AA, length(p - c) - r);
}

// Radius of the wave centerline at an angle relative to the start.
float waveRadius(float rel) {
    return ubuf.radiusPx - ubuf.ampPx + ubuf.ampPx * cos(ubuf.waveCount * rel - ubuf.phase);
}

void main() {
    vec2 size = vec2(ubuf.widthPx, ubuf.heightPx);
    vec2 p = qt_TexCoord0 * size - 0.5 * size;
    float rad = length(p);
    float rel = wrapTau(atan(p.y, p.x) - ubuf.theta0);   // y-down, clockwise positive

    vec4 col = vec4(0.0);

    // 1. Track: plain circle, cut to [trackStart, trackStop], round caps at both ends.
    if (ubuf.showTrack > 0.5) {
        float halfT = ubuf.trackWidthPx * 0.5;
        float stroke = 1.0 - smoothstep(halfT - AA, halfT + AA, abs(rad - ubuf.radiusPx));
        float m = stroke * step(ubuf.trackStart, rel) * step(rel, ubuf.trackStop);

        float a0 = ubuf.theta0 + ubuf.trackStart;
        float a1 = ubuf.theta0 + ubuf.trackStop;
        m = max(m, disc(p, ubuf.radiusPx * vec2(cos(a0), sin(a0)), halfT));
        m = max(m, disc(p, ubuf.radiusPx * vec2(cos(a1), sin(a1)), halfT));

        col = blendOver(col, ubuf.trackColor.rgb, ubuf.trackColor.a * m);
    }

    // 2. Progress: wavy arc r(t) = R - a + a*cos(n*t - phase), t in [0, sweep].
    //    Exact stroke: min distance from the pixel to the curve (sampled as a short polyline
    //    around the pixel's own angle). Clamping t to [0, sweep] gives round caps for free.
    if (ubuf.showProgress > 0.5) {
        float halfW = ubuf.lineWidthPx * 0.5;
        float reach = halfW + AA;
        float lo = ubuf.radiusPx - 2.0 * ubuf.ampPx - reach;   // curve lives in [R - 2a, R]
        float hi = ubuf.radiusPx + reach;

        float m = 0.0;
        if (rad > lo && rad < hi) {
            // Pixel angle as a curve parameter, centered on the arc's midpoint so the gap
            // maps to either side of the arc (no wrap ambiguity).
            float mid = 0.5 * ubuf.sweep;
            float u = wrapTau(atan(p.y, p.x) - ubuf.theta0 - mid + 0.5 * TAU) - 0.5 * TAU;
            float t = mid + u;

            // The nearest curve point is within asin(reach / rad) of the pixel's angle.
            float win = min(asin(min(1.0, reach / max(rad, 1e-3))), 1.5);

            const int N = 16;
            float dMin = 1e9;
            vec2 prev = vec2(0.0);
            for (int i = 0; i <= N; ++i) {
                float tc = clamp(t - win + 2.0 * win * float(i) / float(N), 0.0, ubuf.sweep);
                float ang = ubuf.theta0 + tc;
                vec2 q = waveRadius(tc) * vec2(cos(ang), sin(ang));
                if (i == 0) {
                    dMin = length(p - q);
                } else {
                    vec2 ab = q - prev;
                    float l = dot(ab, ab);
                    float h = l > 1e-8 ? clamp(dot(p - prev, ab) / l, 0.0, 1.0) : 0.0;
                    dMin = min(dMin, length(p - prev - ab * h));
                }
                prev = q;
            }
            m = 1.0 - smoothstep(halfW - AA, halfW + AA, dMin);
        }

        col = blendOver(col, ubuf.fillColor.rgb, ubuf.fillColor.a * m);
    }

    fragColor = col * ubuf.qt_Opacity;
}
