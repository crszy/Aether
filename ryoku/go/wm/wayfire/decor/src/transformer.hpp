#pragma once

#include <memory>

#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-transform.hpp>

#include "options.hpp"
#include "shader.hpp"

namespace ryoku::decor
{
class decoration_node_t : public wf::scene::transformer_base_node_t
{
  public:
    decoration_node_t(
        wayfire_toplevel_view view,
        decoration_shader_t *shader,
        option_values_t options,
        bool gles_renderer);

    std::string stringify() const override;
    wf::geometry_t get_bounding_box() override;
    void gen_render_instances(
        std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage,
        wf::output_t *shown_on) override;

    void refresh(option_values_t options);
    void set_activated(bool activated);
    bool renders_effect() const;
    const option_values_t& current_options() const;
    const wf::geometry_t& window_geometry() const;
    bool is_activated() const;
    decoration_shader_t *get_shader() const;

  private:
    std::weak_ptr<wf::toplevel_view_interface_t> view;
    decoration_shader_t *shader;
    option_values_t options;
    wf::geometry_t geometry;
    bool fullscreen = false;
    bool activated = false;
    bool gles_renderer;
};
}
