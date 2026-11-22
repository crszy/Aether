#pragma once

#include <wayfire/geometry.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/region.hpp>
#include <wayfire/render.hpp>

#include "options.hpp"

namespace ryoku::decor
{
class decoration_shader_t
{
  public:
    void compile();
    void release();
    bool is_ready() const;

    void render(
        const wf::render_target_t& target,
        const wf::regionf_t& damage,
        const wf::gles_texture_t& texture,
        const wf::geometry_t& draw_box,
        const wf::geometry_t& texture_box,
        const wf::geometry_t& window_box,
        const option_values_t& options,
        const wf::color_t& border_color);

  private:
    OpenGL::program_t program;
    bool ready = false;
};
}
