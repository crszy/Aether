#pragma once

#include "settings/objectnode.hpp"
#include "common.hpp"
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_CONFIG_API


namespace caelestia::shell::config {

class CAE_CONFIG_API NexusConfig : public settings::ObjectNode {
    CONFIG_NODE(NexusConfig, settings::ObjectNode)

    CONFIG_PROPERTY(int, wallpapersPerRow, 4)
    CONFIG_PROPERTY(int, maxNetworksShown, 5)
    CONFIG_GLOBAL_PROPERTY(int, networkRescanInterval, 15000)
};

} // namespace caelestia::shell::config
