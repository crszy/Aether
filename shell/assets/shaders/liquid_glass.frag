#version 440
// Liquid Glass (Apple, iOS 26 / macOS 26 style) for Aether's own panels: the wallpaper behind the panel, seen
// through a rounded slab of glass. Light blur, a lens at the edges (content pulled in and magnified, a touch of
// colour fringing), a bright rim lit from the top-left, a faint inner glow and a tint for readability.
// Build: qsb --glsl "100es,120,150" --hlsl 50 --msl 12 -o liquid_glass.frag.qsb liquid_glass.frag

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;        // the glass in px
    vec2 origin;      // its top-left on the screen, px
    vec2 screenSize;  // the wallpaper's size (= the screen), px
    float radius;     // corner radius, px
    float bezel;      // how far in from the edge the lens bends, px
    float refraction; // how far the lens pulls the picture, px
    float blur;       // 0..1
    float rim;        // rim light strength 0..1
    vec4 tint;        // rgb + amount
};
layout(binding = 1) uniform sampler2D wall;

float box(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

vec3 sampleBlur(vec2 uv, float spread) {
    // 9 taps on a small disc, on a mipmapped texture: cheap, smooth frosted look
    vec2 px = spread / screenSize;
    vec3 c = texture(wall, uv).rgb * 0.2;
    c += texture(wall, uv + vec2( 1.0,  0.0) * px).rgb * 0.1;
    c += texture(wall, uv + vec2(-1.0,  0.0) * px).rgb * 0.1;
    c += texture(wall, uv + vec2( 0.0,  1.0) * px).rgb * 0.1;
    c += texture(wall, uv + vec2( 0.0, -1.0) * px).rgb * 0.1;
    c += texture(wall, uv + vec2( 0.7,  0.7) * px).rgb * 0.1;
    c += texture(wall, uv + vec2(-0.7,  0.7) * px).rgb * 0.1;
    c += texture(wall, uv + vec2( 0.7, -0.7) * px).rgb * 0.1;
    c += texture(wall, uv + vec2(-0.7, -0.7) * px).rgb * 0.1;
    return c;
}

void main() {
    vec2 px = qt_TexCoord0 * size;
    vec2 half_ = size * 0.5;
    vec2 p = px - half_;
    float r = min(radius, min(half_.x, half_.y));
    float d = box(p, half_, r);                 // < 0 inside, 0 on the edge
    float alpha = clamp(0.5 - d, 0.0, 1.0);    // anti-aliased shape
    if (alpha <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    // outward normal of the rounded box
    vec2 e = vec2(0.5, 0.0);
    vec2 n = vec2(box(p + e.xy, half_, r) - box(p - e.xy, half_, r), box(p + e.yx, half_, r) - box(p - e.yx, half_, r));
    n = n / max(length(n), 1e-4);

    // lens: strongest at the edge, gone after `bezel` px (a squircle-like falloff)
    float t = clamp(-d / max(bezel, 1.0), 0.0, 1.0);
    float lens = 1.0 - sqrt(1.0 - (1.0 - t) * (1.0 - t));
    vec2 bend = -n * lens * refraction;         // pull the picture in from the edge

    vec2 uv = (origin + px + bend) / screenSize;
    float spread = 2.0 + blur * 14.0;
    // colour fringing at the rim, like real glass
    vec2 ca = n * lens * 1.6 / screenSize;
    vec3 col;
    col.r = sampleBlur(uv + ca, spread).r;
    col.g = sampleBlur(uv, spread).g;
    col.b = sampleBlur(uv - ca, spread).b;

    // tint for contrast, and a little extra saturation like Apple's
    float luma = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(luma), col, 1.15);
    col = mix(col, tint.rgb, tint.a);

    // inner glow along the edge, and the rim light: brighter where the edge faces the light (top-left) and
    // its opposite corner, like Apple's specular highlight
    float edge = 1.0 - clamp(-d / 1.6, 0.0, 1.0);
    float facing = abs(dot(n, normalize(vec2(-0.6, -1.0))));
    col += vec3(1.0) * edge * rim * (0.25 + 0.75 * pow(facing, 3.0));
    col += vec3(1.0) * lens * rim * 0.06;

    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0) * alpha * qt_Opacity;
}
