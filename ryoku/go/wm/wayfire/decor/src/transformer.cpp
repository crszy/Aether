#include "transformer.hpp"

#include <algorithm>
#include <utility>

#include <wayfire/scene-render.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/view.hpp>

namespace
{
wf::geometry_t union_boxes(const wf::geometry_t& first, const wf::geometry_t& second)
{
    const double left = std::min(first.x, second.x);
    const double top = std::min(first.y, second.y);
    const double right = std::max(first.x + first.width, second.x + second.width);
    const double bottom = std::max(first.y + first.height, second.y + second.height);
    return {left, top, right - left, bottom - top};
}
}

namespace ryoku::decor
{
class decoration_render_instance_t :
    public wf::scene::transformer_render_instance_t<decoration_node_t>
{
  public:
    using transformer_render_instance_t::transformer_render_instance_t;

    void schedule_instructions(
        std::vector<wf::scene::render_instruction_t>& instructions,
        const wf::render_target_t& target,
        wf::regionf_t& damage) override
    {
        if (!self->renders_effect())
        {
            for (auto& child : children)
            {
                child->schedule_instructions(instructions, target, damage);
            }

            return;
        }

        auto own_damage = damage & self->get_bounding_box();
        if (!own_damage.empty())
        {
            instructions.push_back({
                .instance = this,
                .target = target,
                .damage = std::move(own_damage),
            });
        }
    }

    void render(const wf::scene::render_instruction_t& data) override
    {
        // Rendering the children runs a nested pass that leaves blending off
        // after an opaque surface, so it must finish before the subpass sets GL up.
        wf::dimensionsf_t render_size;
        auto contents = get_texture(data.target.scale, &render_size);
        if (!contents)
        {
            return;
        }

        data.pass->custom_gles_subpass(data.target, [&]
        {
            auto children_box = self->get_children_bounding_box();
            auto texture_box = wf::construct_box(wf::origin(children_box), render_size);
            texture_box = data.target.aligned_geometry_from_geometry_box(texture_box);
            auto draw_box = data.target.aligned_geometry_from_geometry_box(self->get_bounding_box());
            auto window_box = data.target.aligned_geometry_from_geometry_box(self->window_geometry());
            const auto& options = self->current_options();
            const auto& color = self->is_activated() ? options.active_color : options.inactive_color;

            self->get_shader()->render(
                data.target,
                data.damage,
                wf::gles_texture_t{contents},
                draw_box,
                texture_box,
                window_box,
                options,
                color);
        });
    }

    wf::scene::direct_scanout try_scanout(wf::output_t *output) override
    {
        if (!self->renders_effect())
        {
            return wf::scene::try_scanout_from_list(children, output);
        }

        return wf::scene::direct_scanout::OCCLUSION;
    }
};

decoration_node_t::decoration_node_t(
    wayfire_toplevel_view view,
    decoration_shader_t *shader,
    option_values_t options,
    bool gles_renderer) :
    transformer_base_node_t(false),
    view(view->weak_from_this()),
    shader(shader),
    options(std::move(options)),
    geometry(view->get_geometry()),
    fullscreen(view->toplevel()->current().fullscreen),
    activated(view->activated),
    gles_renderer(gles_renderer)
{}

std::string decoration_node_t::stringify() const
{
    return "ryoku-decor";
}

wf::geometry_t decoration_node_t::get_bounding_box()
{
    if (!renders_effect())
    {
        return get_children_bounding_box();
    }

    const double border = options.border_width;
    wf::geometry_t result = {
        geometry.x - border,
        geometry.y - border,
        geometry.width + 2.0 * border,
        geometry.height + 2.0 * border,
    };

    if (options.shadow_enabled)
    {
        const double extent = options.shadow_spread + options.shadow_radius;
        const wf::geometry_t shadow = {
            geometry.x + options.shadow_offset_x - extent,
            geometry.y + options.shadow_offset_y - extent,
            geometry.width + 2.0 * extent,
            geometry.height + 2.0 * extent,
        };
        result = union_boxes(result, shadow);
    }

    return result;
}

void decoration_node_t::gen_render_instances(
    std::vector<wf::scene::render_instance_uptr>& instances,
    wf::scene::damage_callback push_damage,
    wf::output_t *shown_on)
{
    auto instance = std::make_unique<decoration_render_instance_t>(
        this, std::move(push_damage), shown_on);
    if (instance->has_instances())
    {
        instances.push_back(std::move(instance));
    }
}

void decoration_node_t::refresh(option_values_t new_options)
{
    auto locked_view = view.lock();
    if (!locked_view)
    {
        return;
    }

    auto manager = locked_view->get_transformed_node();
    manager->begin_transform_update();
    options = std::move(new_options);
    geometry = locked_view->get_geometry();
    fullscreen = locked_view->toplevel()->current().fullscreen;
    activated = locked_view->activated;
    manager->end_transform_update();
}

void decoration_node_t::set_activated(bool is_activated)
{
    if (activated == is_activated)
    {
        return;
    }

    activated = is_activated;
    wf::scene::damage_node(shared_from_this(), get_bounding_box());
}

bool decoration_node_t::renders_effect() const
{
    return gles_renderer && shader && shader->is_ready() && !fullscreen && options.has_effect();
}

const option_values_t& decoration_node_t::current_options() const
{
    return options;
}

const wf::geometry_t& decoration_node_t::window_geometry() const
{
    return geometry;
}

bool decoration_node_t::is_activated() const
{
    return activated;
}

decoration_shader_t *decoration_node_t::get_shader() const
{
    return shader;
}
}
