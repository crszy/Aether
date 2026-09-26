// src/services/Colours.h  —  Aether shell
// Native port of Caelestia's Material-3 colour roles.
// Source of truth: caelestia-dots/shell  services/Colours.qml (M3Palette).
// Aether is a GPL-3.0 derivative of Caelestia Shell.
//
// These are the DEFAULT (fallback) scheme roles. Like upstream, the live scheme is derived from
// the wallpaper (matugen / Material-You) at runtime into the shell's dynamic COL_* palette; these
// constants are the exact upstream defaults, used as fallback and as the canonical role reference
// when porting components (e.g. pill = m3surfaceContainer, clock = m3tertiary, power = m3error).
#pragma once

namespace Col {
    // #RRGGBB -> ImGui packed ABGR with full alpha (matches IM_COL32 byte order without needing imgui.h)
    constexpr unsigned int hx(unsigned int rgb){
        return 0xFF000000u | ((rgb & 0xFFu)<<16) | (rgb & 0xFF00u) | ((rgb>>16)&0xFFu);
    }
    // ---- surfaces / neutrals ----
    constexpr unsigned int m3background             = hx(0x191114);
    constexpr unsigned int m3onBackground           = hx(0xefdfe2);
    constexpr unsigned int m3surface                = hx(0x191114);
    constexpr unsigned int m3surfaceBright          = hx(0x403739);
    constexpr unsigned int m3surfaceContainerLowest = hx(0x130c0e);   // bar / border background (near-black)
    constexpr unsigned int m3surfaceContainerLow    = hx(0x22191c);
    constexpr unsigned int m3surfaceContainer       = hx(0x261d20);   // pill / capsule background
    constexpr unsigned int m3surfaceContainerHigh   = hx(0x31282a);
    constexpr unsigned int m3surfaceContainerHighest= hx(0x3c3235);
    constexpr unsigned int m3onSurface              = hx(0xefdfe2);
    constexpr unsigned int m3surfaceVariant         = hx(0x514347);
    constexpr unsigned int m3onSurfaceVariant       = hx(0xd5c2c6);
    constexpr unsigned int m3outline                = hx(0x9e8c91);
    constexpr unsigned int m3outlineVariant         = hx(0x514347);   // dividers
    // ---- accents ----
    constexpr unsigned int m3primary                = hx(0xffb0ca);
    constexpr unsigned int m3onPrimary              = hx(0x541d34);
    constexpr unsigned int m3primaryContainer       = hx(0x6f334a);
    constexpr unsigned int m3secondary              = hx(0xe2bdc7);   // StatusIcons default colour
    constexpr unsigned int m3secondaryContainer     = hx(0x5a3f48);
    constexpr unsigned int m3tertiary               = hx(0xf0bc95);   // clock / distro logo colour
    constexpr unsigned int m3tertiaryContainer      = hx(0xb58763);
    constexpr unsigned int m3error                  = hx(0xffb4ab);   // power button
    constexpr unsigned int m3success                = hx(0xb5ccba);
}
