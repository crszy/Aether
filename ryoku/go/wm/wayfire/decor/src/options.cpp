#include "options.hpp"

#include <utility>

namespace ryoku::decor
{
bool option_values_t::has_effect() const
{
    return (radius > 0) || (border_width > 0) || shadow_enabled;
}

option_state_t::option_state_t() = default;

option_values_t option_state_t::values() const
{
    return {
        .radius = radius,
        .corner_power = corner_power,
        .border_width = border_width,
        .active_color = active_color,
        .inactive_color = inactive_color,
        .shadow_enabled = shadow_enabled,
        .shadow_radius = shadow_radius,
        .shadow_spread = shadow_spread,
        .shadow_offset_x = shadow_offset_x,
        .shadow_offset_y = shadow_offset_y,
        .shadow_color = shadow_color,
    };
}

void option_state_t::set_callback(std::function<void()> callback)
{
    radius.set_callback(callback);
    corner_power.set_callback(callback);
    border_width.set_callback(callback);
    active_color.set_callback(callback);
    inactive_color.set_callback(callback);
    shadow_enabled.set_callback(callback);
    shadow_radius.set_callback(callback);
    shadow_spread.set_callback(callback);
    shadow_offset_x.set_callback(callback);
    shadow_offset_y.set_callback(callback);
    shadow_color.set_callback(callback);
    ignore_views.set_callback(std::move(callback));
}
}
