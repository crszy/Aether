#include "shader.hpp"

#include <algorithm>

#include <glm/vec4.hpp>

namespace
{
const char *vertex_shader = R"(
#version 100

attribute highp vec2 position;
attribute highp vec2 texcoord;

varying highp vec2 fragment_position;
varying highp vec2 texture_position;

uniform mat4 mvp;

void main()
{
    gl_Position = mvp * vec4(position, 0.0, 1.0);
    fragment_position = position;
    texture_position = texcoord;
}
)";

const char *fragment_shader = R"(
#version 100
@builtin_ext@
precision highp float;
@builtin@

varying highp vec2 fragment_position;
varying highp vec2 texture_position;

uniform vec4 window_box;
uniform vec4 border_color;
uniform vec4 shadow_color;
uniform float radius;
uniform float corner_power;
uniform float border_width;
uniform float shadow_enabled;
uniform float shadow_radius;
uniform float shadow_spread;
uniform vec2 shadow_offset;
uniform float antialias_width;

float box_distance(vec2 point, vec4 box)
{
    vec2 half_size = max(box.zw * 0.5, vec2(0.0));
    vec2 delta = abs(point - (box.xy + half_size)) - half_size;
    return length(max(delta, 0.0)) + min(max(delta.x, delta.y), 0.0);
}

float rounded_distance(vec2 point, vec4 box, float requested_radius, float power)
{
    vec2 half_size = max(box.zw * 0.5, vec2(0.0));
    float limited_radius = min(max(requested_radius, 0.0), min(half_size.x, half_size.y));
    if (limited_radius < 0.001)
    {
        return box_distance(point, box);
    }

    vec2 corner = abs(point - (box.xy + half_size)) - (half_size - vec2(limited_radius));
    vec2 outside = max(corner, 0.0);
    float superellipse = pow(pow(outside.x, power) + pow(outside.y, power), 1.0 / power);
    return superellipse + min(max(corner.x, corner.y), 0.0) - limited_radius;
}

float shape_coverage(float distance_to_shape)
{
    return 1.0 - smoothstep(-antialias_width, antialias_width, distance_to_shape);
}

vec4 premultiply(vec4 color, float coverage)
{
    float alpha = color.a * coverage;
    return vec4(color.rgb * alpha, alpha);
}

vec4 over(vec4 foreground, vec4 background)
{
    return foreground + (1.0 - foreground.a) * background;
}

void main()
{
    float inner_distance = rounded_distance(
        fragment_position, window_box, radius, corner_power);
    float content_coverage = shape_coverage(inner_distance);

    vec4 outer_box = vec4(
        window_box.xy - vec2(border_width),
        window_box.zw + vec2(2.0 * border_width));
    float outer_distance = rounded_distance(
        fragment_position, outer_box, radius + border_width, corner_power);
    float outer_coverage = shape_coverage(outer_distance);
    float ring_coverage = border_width > 0.0 ?
        outer_coverage * (1.0 - content_coverage) : 0.0;

    vec4 result = vec4(0.0);
    if (shadow_enabled > 0.5)
    {
        vec4 shadow_box = vec4(
            window_box.xy + shadow_offset - vec2(shadow_spread),
            window_box.zw + vec2(2.0 * shadow_spread));
        float shadow_distance = rounded_distance(
            fragment_position, shadow_box, radius + shadow_spread, corner_power);
        float shadow_coverage;
        if (shadow_radius > 0.0)
        {
            shadow_coverage = 1.0 - smoothstep(
                -antialias_width, shadow_radius, shadow_distance);
            shadow_coverage *= shadow_coverage;
        } else
        {
            shadow_coverage = shape_coverage(shadow_distance);
        }

        result = premultiply(shadow_color, shadow_coverage);
    }

    result = over(premultiply(border_color, ring_coverage), result);

    vec4 content = vec4(0.0);
    if ((content_coverage > 0.0) &&
        all(greaterThanEqual(texture_position, vec2(0.0))) &&
        all(lessThanEqual(texture_position, vec2(1.0))))
    {
        content = get_pixel(texture_position) * content_coverage;
    }

    gl_FragColor = over(content, result);
}
)";

glm::vec4 to_vec4(const wf::color_t& color)
{
    return {
        static_cast<float>(color.r),
        static_cast<float>(color.g),
        static_cast<float>(color.b),
        static_cast<float>(color.a),
    };
}

float target_scale(const wf::render_target_t& target)
{
    float scale = target.scale;
    if (target.subbuffer)
    {
        const float scale_x = target.subbuffer->width / target.geometry.width;
        const float scale_y = target.subbuffer->height / target.geometry.height;
        scale *= std::max(scale_x, scale_y);
    }

    return std::max(scale, 0.001f);
}
}

namespace ryoku::decor
{
void decoration_shader_t::compile()
{
    program.compile(vertex_shader, fragment_shader);
    ready = program.get_program_id(wf::TEXTURE_TYPE_RGBA) != 0;
}

void decoration_shader_t::release()
{
    if (ready)
    {
        program.free_resources();
        ready = false;
    }
}

bool decoration_shader_t::is_ready() const
{
    return ready;
}

void decoration_shader_t::render(
    const wf::render_target_t& target,
    const wf::regionf_t& damage,
    const wf::gles_texture_t& texture,
    const wf::geometry_t& draw_box,
    const wf::geometry_t& texture_box,
    const wf::geometry_t& window_box,
    const option_values_t& options,
    const wf::color_t& border_color)
{
    const float x = draw_box.x;
    const float y = draw_box.y;
    const float width = draw_box.width;
    const float height = draw_box.height;
    const float texture_x = texture_box.x;
    const float texture_width = std::max<float>(texture_box.width, 0.001f);
    const float texture_height = std::max<float>(texture_box.height, 0.001f);

    const float positions[] = {
        x, y + height,
        x + width, y + height,
        x + width, y,
        x, y,
    };
    const float texture_bottom = texture_box.y + texture_box.height;
    const float coordinates[] = {
        (x - texture_x) / texture_width,
        (texture_bottom - (y + height)) / texture_height,
        (x + width - texture_x) / texture_width,
        (texture_bottom - (y + height)) / texture_height,
        (x + width - texture_x) / texture_width,
        (texture_bottom - y) / texture_height,
        (x - texture_x) / texture_width,
        (texture_bottom - y) / texture_height,
    };

    program.use(texture.type);
    program.set_active_texture(texture);
    program.attrib_pointer("position", 2, 0, positions);
    program.attrib_pointer("texcoord", 2, 0, coordinates);
    program.uniformMatrix4f("mvp", wf::gles::render_target_aligned_orthographic_projection(target));
    program.uniform4f("window_box", {
        window_box.x, window_box.y, window_box.width, window_box.height});
    program.uniform4f("border_color", to_vec4(border_color));
    program.uniform4f("shadow_color", to_vec4(options.shadow_color));
    program.uniform1f("radius", options.radius);
    program.uniform1f("corner_power", options.corner_power);
    program.uniform1f("border_width", options.border_width);
    program.uniform1f("shadow_enabled", options.shadow_enabled ? 1.0f : 0.0f);
    program.uniform1f("shadow_radius", options.shadow_radius);
    program.uniform1f("shadow_spread", options.shadow_spread);
    program.uniform2f("shadow_offset", options.shadow_offset_x, options.shadow_offset_y);
    program.uniform1f("antialias_width", 0.75f / target_scale(target));

    wf::gles::bind_render_buffer(target);
    wf::gles::for_each_scissor_rect(target, damage, [&]
    {
        GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
    });

    GL_CALL(glActiveTexture(GL_TEXTURE0));
    GL_CALL(glBindTexture(texture.target, 0));
    program.deactivate();
}
}
