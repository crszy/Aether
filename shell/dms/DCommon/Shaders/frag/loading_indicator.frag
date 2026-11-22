#version 450

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 color;
    vec2 offset;
    float sizePx;
    float unitPx;
    float fromRow;
    float toRow;
    float progress;
    float angle;
    float rows;
    float samples;
} ubuf;

layout(binding = 1) uniform sampler2D profiles;

const float TAU = 6.28318530718;

float radiusAt(float row, float column) {
    vec2 uv = vec2((mod(column, ubuf.samples) + 0.5) / ubuf.samples, (row + 0.5) / ubuf.rows);
    return dot(texture(profiles, uv).rg, vec2(255.0 * 256.0, 255.0)) / 65535.0;
}

float morphedRadius(float column) {
    return mix(radiusAt(ubuf.fromRow, column), radiusAt(ubuf.toRow, column), ubuf.progress);
}

void main() {
    vec2 p = (qt_TexCoord0 - 0.5) * ubuf.sizePx;
    float c = cos(ubuf.angle);
    float s = sin(ubuf.angle);
    vec2 v = vec2(p.x * c + p.y * s, p.y * c - p.x * s) + ubuf.offset;

    float rho = length(v);
    float u = (atan(v.y, v.x) / TAU + 0.5) * ubuf.samples;
    float column = floor(u);
    float before = morphedRadius(column);
    float after = morphedRadius(column + 1.0);
    float radius = mix(before, after, u - column) * ubuf.unitPx;
    float slope = (after - before) * ubuf.unitPx * ubuf.samples / TAU;

    float distancePx = (radius - rho) / sqrt(1.0 + slope * slope / max(radius * radius, 1e-4));
    float devicePx = max(length(vec2(dFdx(rho), dFdy(rho))), 1e-4);
    float coverage = clamp(distancePx / devicePx + 0.5, 0.0, 1.0);

    float a = ubuf.color.a * coverage * ubuf.qt_Opacity;
    fragColor = vec4(ubuf.color.rgb * a, a);
}
