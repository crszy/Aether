// src/modules/bar/components/OsIcon.h  —  Aether shell
// Native port of Caelestia's bar logo / launcher button.
// Source of truth: caelestia-dots/shell  modules/bar/components/OsIcon.qml
// Aether is a GPL-3.0 derivative of Caelestia Shell.
//
// OsIcon.qml: the Caelestia/distro logo at the top; a PointingHand MouseArea toggles the launcher
// (ShellState.launcher). Bare — no pill. Here: ring+dot glyph, accent hover wash, click -> launcher.
#pragma once

// Returns tooltip while hovered (caller draws it), else nullptr. Handles its own click. hid = per-monitor hover-id base.
static const char* DrawBarLogo(ImDrawList* dl, ImGuiIO& io, float cx, float ox, float y, float half, float entrance, bool click, int hid){
    bool hov = io.MousePos.x>cx-half && io.MousePos.x<cx+half && io.MousePos.y>y-half && io.MousePos.y<y+half;
    float ha  = HoverAnim(hid+3100, hov);
    float pop = 0.85f + 0.15f*entrance;
    ImVec2 c = V(cx+ox,y);
    // BARE in the reference rice — the distro mark sits straight on the strip with no tile, and
    // only picks up a state layer while hovered. (A permanent accent tile made it read as a
    // notification badge rather than the logo.)
    if(ha>0.01f){ float s=13.0f*pop*(0.94f+0.06f*ha);
        dl->AddRectFilled(V(c.x-s,c.y-s),V(c.x+s,c.y+s), MulA(AccA((int)(ha*54)),entrance), s*0.56f); }
    // A real distro mark if the user picked one (Settings > Taskbar > Logo), otherwise the
    // built-in ring+dot. DrawLogoMark preserves the artwork's aspect ratio - several of these logos
    // are not square, and forcing them into a square box visibly squashes them.
    //
    // OsIcon.qml renders the distro mark through a ColouredIcon:
    //     ColouredIcon { source: SysInfo.osLogo; colour: Colours.palette.m3tertiary }
    // i.e. a flat accent-tinted silhouette, not the vendor's own colours - which is why the Arch
    // mark in the reference frames is the same muted gold as the clock underneath it. g_barLogoTint
    // (Settings > Taskbar > Logo) turns that off for anyone who wants the full-colour artwork.
    float box = 21.0f*pop*(1.0f+ha*0.06f);
    ImU32 tint = g_barLogoTint ? M3Tertiary() : 0;
    if(!DrawLogoMark(dl, c, box, entrance, tint)){
        ImU32 gl = g_barLogoTint ? M3Tertiary()
                                 : (g_darkUI ? IM_COL32(233,237,239,255) : IM_COL32(20,24,22,255));
        // the Aether mark, not a ring with a dot in it
        AetherMark(dl, c, 9.5f*pop, MulA(gl,entrance));
    }
    if(click && hov) g_launShow = true;
    return hov ? "Launcher" : nullptr;
}
