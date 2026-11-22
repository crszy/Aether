#pragma once

#include <functional>
#include <string>

#include <wayfire/config/types.hpp>
#include <wayfire/option-wrapper.hpp>

namespace ryoku::decor
{
struct option_values_t
{
    int radius;
    double corner_power;
    int border_width;
    wf::color_t active_color;
    wf::color_t inactive_color;
    bool shadow_enabled;
    int shadow_radius;
    int shadow_spread;
    int shadow_offset_x;
    int shadow_offset_y;
    wf::color_t shadow_color;

    bool has_effect() const;
};

class option_state_t
{
  public:
    option_state_t();

    option_values_t values() const;
    void set_callback(std::function<void()> callback);

  private:
    wf::option_wrapper_t<int> radius{"ryoku-decor/radius"};
    wf::option_wrapper_t<double> corner_power{"ryoku-decor/corner_power"};
    wf::option_wrapper_t<int> border_width{"ryoku-decor/border_width"};
    wf::option_wrapper_t<wf::color_t> active_color{"ryoku-decor/active_color"};
    wf::option_wrapper_t<wf::color_t> inactive_color{"ryoku-decor/inactive_color"};
    wf::option_wrapper_t<bool> shadow_enabled{"ryoku-decor/shadow_enabled"};
    wf::option_wrapper_t<int> shadow_radius{"ryoku-decor/shadow_radius"};
    wf::option_wrapper_t<int> shadow_spread{"ryoku-decor/shadow_spread"};
    wf::option_wrapper_t<int> shadow_offset_x{"ryoku-decor/shadow_offset_x"};
    wf::option_wrapper_t<int> shadow_offset_y{"ryoku-decor/shadow_offset_y"};
    wf::option_wrapper_t<wf::color_t> shadow_color{"ryoku-decor/shadow_color"};
    wf::option_wrapper_t<std::string> ignore_views{"ryoku-decor/ignore_views"};
};
}
