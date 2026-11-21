#version 450

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float widthPx;
    float heightPx;
    float ringWidthPx;
    float checkerPx;
    float showChecker;
    vec4 fillColor;
    vec4 secondaryFill;
    vec4 tertiaryFill;
    vec4 ringColor;
    vec4 checkerLight;
    vec4 checkerDark;
} ubuf;

vec4 blendOver(vec4 dst, vec4 premulSrc, float coverage) {
    vec4 src = premulSrc * coverage;
    return src + dst * (1.0 - src.a);
}

void main() {
    vec2 size = vec2(ubuf.widthPx, ubuf.heightPx);
    vec2 px = qt_TexCoord0 * size;
    vec2 fromCenter = px - size * 0.5;
    float radius = min(size.x, size.y) * 0.5;
    float dist = length(fromCenter) - radius;
    float outer = clamp(0.5 - dist, 0.0, 1.0);
    float inner = clamp(0.5 - (dist + ubuf.ringWidthPx), 0.0, 1.0);

    vec2 cell = floor(px / max(ubuf.checkerPx, 1.0));
    float parity = mod(cell.x + cell.y, 2.0);
    vec4 checker = mix(ubuf.checkerLight, ubuf.checkerDark, parity);

    vec4 lower = mix(ubuf.secondaryFill, ubuf.tertiaryFill, clamp(fromCenter.x + 0.5, 0.0, 1.0));
    vec4 fill = mix(lower, ubuf.fillColor, clamp(0.5 - fromCenter.y, 0.0, 1.0));

    vec4 acc = vec4(0.0);
    acc = blendOver(acc, checker, ubuf.showChecker * outer);
    acc = blendOver(acc, fill, outer);
    acc = blendOver(acc, ubuf.ringColor, outer - inner);
    fragColor = acc * ubuf.qt_Opacity;
}
