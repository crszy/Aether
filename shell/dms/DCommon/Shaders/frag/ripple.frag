#version 450

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float widthPx;
    float heightPx;
    float rippleCenterX;
    float rippleCenterY;
    float rippleRadius;
    float rippleOpacity;
    float offsetX;
    float offsetY;
    float parentWidth;
    float parentHeight;
    vec4 rippleCol;
    vec4 cornerRadiiPx;
} ubuf;

float cornerRadius(vec2 p, vec4 radii) {
    if (p.y < 0.0)
        return p.x < 0.0 ? radii.x : radii.y;
    return p.x < 0.0 ? radii.w : radii.z;
}

float sdRoundRect(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - (b - vec2(r));
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    if (ubuf.rippleOpacity <= 0.0 || ubuf.rippleRadius <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    vec2 px = qt_TexCoord0 * vec2(ubuf.widthPx, ubuf.heightPx) + vec2(ubuf.offsetX, ubuf.offsetY);

    float circleD = length(px - vec2(ubuf.rippleCenterX, ubuf.rippleCenterY)) - ubuf.rippleRadius;
    float aaCircle = max(fwidth(circleD), 0.001);
    float circleMask = 1.0 - smoothstep(-aaCircle, aaCircle, circleD);
    if (circleMask <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    float rrMask = 1.0;
    if (any(greaterThan(ubuf.cornerRadiiPx, vec4(0.0)))) {
        vec2 halfSize = vec2(ubuf.parentWidth, ubuf.parentHeight) * 0.5;
        vec2 p = px - halfSize;
        float r = clamp(cornerRadius(p, ubuf.cornerRadiiPx), 0.0, min(halfSize.x, halfSize.y));
        float rrD = sdRoundRect(p, halfSize, r);
        float aaRR = max(fwidth(rrD), 0.001);
        rrMask = 1.0 - smoothstep(-aaRR, aaRR, rrD);
    }

    float mask = circleMask * rrMask;
    if (mask <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    float a = ubuf.rippleCol.a * ubuf.rippleOpacity * mask * ubuf.qt_Opacity;
    fragColor = vec4(ubuf.rippleCol.rgb * a, a);
}
