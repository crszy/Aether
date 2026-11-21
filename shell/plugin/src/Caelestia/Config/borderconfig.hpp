#pragma once

#include "settings/objectnode.hpp"
#include "common.hpp"
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_CONFIG_API


namespace caelestia::shell::config {

class CAE_CONFIG_API BorderConfig : public settings::ObjectNode {
    CONFIG_NODE(BorderConfig, settings::ObjectNode)

    CONFIG_PROPERTY(int, thickness, 10)
    CONFIG_PROPERTY(int, rounding, 25)
    CONFIG_PROPERTY(int, smoothing, 20)

    Q_PROPERTY(int minThickness READ minThickness CONSTANT)
    Q_PROPERTY(int clampedThickness READ clampedThickness NOTIFY thicknessChanged)

public:
    [[nodiscard]] static int minThickness();
    [[nodiscard]] int clampedThickness() const;
};

} // namespace caelestia::shell::config
