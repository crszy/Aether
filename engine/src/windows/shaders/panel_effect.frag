#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 itemSize;
    vec2 blurStep;
    float blurLod;
    float dissolve;
    float noiseScale;
    vec4 revealRect;
    float revealRadius;
};

layout(binding = 1) uniform sampler2D source;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

vec4 blurred(vec2 uv) {
    vec2 o = blurStep;
    vec4 c = texture(source, uv, blurLod) * 4.0;
    c += texture(source, uv + vec2(o.x, 0.0), blurLod) * 2.0;
    c += texture(source, uv - vec2(o.x, 0.0), blurLod) * 2.0;
    c += texture(source, uv + vec2(0.0, o.y), blurLod) * 2.0;
    c += texture(source, uv - vec2(0.0, o.y), blurLod) * 2.0;
    c += texture(source, uv + o, blurLod);
    c += texture(source, uv - o, blurLod);
    c += texture(source, uv + vec2(o.x, -o.y), blurLod);
    c += texture(source, uv + vec2(-o.x, o.y), blurLod);
    return c / 16.0;
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 p = uv * itemSize;
    float pixel = max(fwidth(p.x), 0.0001);

    vec4 color = blurStep.x > 0.0 ? blurred(uv) : texture(source, uv);
    float alpha = 1.0;

    if (dissolve < 1.0) {
        float n = valueNoise(p * noiseScale) * 0.7 + valueNoise(p * noiseScale * 3.1 + 7.3) * 0.3;
        float edge = 0.14;
        float t = dissolve * (1.0 + edge);
        float shown = 1.0 - smoothstep(t - edge, t, n);
        float rim = shown * (1.0 - shown) * 4.0;
        color.rgb = mix(color.rgb, vec3(color.a), rim * 0.35);
        alpha *= shown;
    }

    if (revealRadius >= 0.0) {
        vec2 halfSize = revealRect.zw * 0.5;
        vec2 q = abs(p - revealRect.xy - halfSize) - halfSize + revealRadius;
        float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - revealRadius;
        alpha *= clamp(0.5 - d / pixel, 0.0, 1.0);
    }

    fragColor = color * (alpha * qt_Opacity);
}
