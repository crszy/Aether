// src/modules/bar/components/Power.h  —  Aether shell
// Native port of Caelestia's bar power button.
// Source of truth: caelestia-dots/shell  modules/bar/components/Power.qml
// Aether is a GPL-3.0 derivative of Caelestia Shell.
//
// Power.qml: a "power_settings_new" MaterialIcon coloured Colours.palette.m3error, with a StateLayer;
// clicking it toggles the session (power/logout) menu. Bare (no pill), pinned at the very bottom.
#pragma once

// Returns the tooltip text while hovered (caller draws it), else nullptr. Handles its own click.
static const char* DrawBarPower(ImDrawList* dl, ImGuiIO& io, float cx, float ox, float baseY, float half, float entrance, bool click){
    ImVec2 pw = V(cx+ox, baseY);
    float dx=io.MousePos.x-pw.x, dy=io.MousePos.y-pw.y; bool hov = dx*dx+dy*dy < half*half;
    float ha = HoverAnim(819600, hov);
    // StateLayer: a rounded squircle in m3error that springs open, matching the rest of the strip's
    // controls (it used to be the only circular wash in the bar).
    if(ha>0.01f){ float s=half*(0.92f+0.08f*ha);
        dl->AddRectFilled(V(pw.x-s,pw.y-s), V(pw.x+s,pw.y+s), IM_COL32(224,130,120,(int)(60*ha)), s*0.56f); }
    // m3error red; brightens on hover
    PowerIcon(dl, pw, MulA(hov?IM_COL32(244,124,114,255):IM_COL32(206,104,96,255), entrance));
    if(click && hov) g_sessShow = true;
    return hov ? "Power" : nullptr;
}
