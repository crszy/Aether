#pragma once

#include <qstring.h>

#include "settings/objectnode.hpp"
#include "common.hpp"
#include "enums.hpp"
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_CONFIG_API


namespace caelestia::shell::config {

using Qt::StringLiterals::operator""_s;

class CAE_CONFIG_API DesktopClockBackground : public settings::ObjectNode {
    CONFIG_NODE(DesktopClockBackground, settings::ObjectNode)

    CONFIG_PROPERTY(bool, enabled, false)
    CONFIG_PROPERTY(qreal, opacity, 0.7)
    CONFIG_PROPERTY(bool, blur, true)
};

class CAE_CONFIG_API DesktopClockShadow : public settings::ObjectNode {
    CONFIG_NODE(DesktopClockShadow, settings::ObjectNode)

    CONFIG_PROPERTY(bool, enabled, true)
    CONFIG_PROPERTY(qreal, opacity, 0.7)
    CONFIG_PROPERTY(qreal, blur, 0.4)
};

class CAE_CONFIG_API DesktopClock : public settings::ObjectNode {
    CONFIG_NODE(DesktopClock, settings::ObjectNode)

    CONFIG_PROPERTY(bool, enabled, false)
    CONFIG_PROPERTY(qreal, scale, 1.0)
    CONFIG_PROPERTY(QString, position, u"bottom-right"_s)
    CONFIG_PROPERTY(bool, invertColors, false)
    CONFIG_SUBOBJECT(DesktopClockBackground, background)
    CONFIG_SUBOBJECT(DesktopClockShadow, shadow)
};

class CAE_CONFIG_API BackgroundVisualiser : public settings::ObjectNode {
    CONFIG_NODE(BackgroundVisualiser, settings::ObjectNode)

    CONFIG_PROPERTY(bool, enabled, false)
    CONFIG_PROPERTY(bool, autoHide, true)
    CONFIG_PROPERTY(bool, blur, false)
    CONFIG_PROPERTY(qreal, rounding, 1)
    CONFIG_PROPERTY(qreal, spacing, 1)
};

class CAE_CONFIG_API BackgroundConfig : public settings::ObjectNode {
    CONFIG_NODE(BackgroundConfig, settings::ObjectNode)

    CONFIG_PROPERTY(bool, enabled, true)
    CONFIG_PROPERTY(bool, wallpaperEnabled, true)
    CONFIG_ENUM_PROPERTY(WallpaperTransition, wallpaperTransition, WallpaperTransition::Crossfade)
    CONFIG_SUBOBJECT(DesktopClock, desktopClock)
    CONFIG_SUBOBJECT(BackgroundVisualiser, visualiser)
};

} // namespace caelestia::shell::config
