// Aether - includes, shared types, config plumbing, small utilities.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

#define IMGUI_DEFINE_MATH_OPERATORS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <d2d1.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <winhttp.h>
#include <lmcons.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <audiopolicy.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <powrprof.h>
#include <wlanapi.h>
#include <bluetoothapis.h>
#include <ras.h>
#include <raserror.h>
#include <shlobj.h>
#include <commoncontrols.h>   // IImageList / SHIL_JUMBO — the shell's 256px icon list
#include <wbemidl.h>
#include <sddl.h>
#include <commdlg.h>
#include "lua.hpp"          // Lua 5.4 — the plugin engine (built into lua54.lib by build.ps1)
#include <winioctl.h>
#include <winspool.h>
#include <netfw.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Security.Credentials.UI.h>
#include <winrt/Windows.Devices.Radios.h>
#include <UserConsentVerifierInterop.h>   // the DESKTOP entry point to Windows Hello (needs an HWND)
#include <mutex>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cwctype>
#include <cwchar>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <thread>
#include <atomic>
#include <exception>    // std::set_terminate — the crash guards that hand the taskbar back
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <map>
#include <deque>
#include <set>
#include <condition_variable>
#include <psapi.h>     // GetProcessMemoryInfo, for -s texdump
#pragma comment(lib, "windowsapp.lib")
#include <unordered_set>
#include "imgui/imgui.h"
#include "src/MaterialIcons.h"
#include "src/Toml.h"                  // config files are the source of truth; this writes them in place
#include "src/TomlTest.h"              // Aether.exe --tomltest
#include "imgui/backends/imgui_impl_dx11.h"
#include "imgui/backends/imgui_impl_win32.h"
#define NANOSVG_IMPLEMENTATION          // real Adwaita/Breeze SVG icons pulled from the CachyOS ISO
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "linux/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "linux/nanosvgrast.h"
#include "src/services/SafeLaunch.h"   // launches off the render thread, error popups, async pickers, hang watchdog
#include "src/services/Pace.h"         // DwmFlush with a timeout (the render loop and the slide workers)

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "Bthprops.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "winspool.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// --- stubs for the phobia ImGui fork's custom widgets ---
float   content_animation = 0.0f;
float   accent_colour[4]  = {0.78f,0.66f,0.29f,1.0f};
ImFont* poppins   = nullptr;
ImFont* font_icon = nullptr;

static ID3D11Device*           g_dev = nullptr;
static ID3D11DeviceContext*    g_ctx = nullptr;
static IDXGISwapChain1*        g_sc  = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static IDCompositionDevice*    g_dcDev = nullptr;
static IDCompositionTarget*    g_dcTarget = nullptr;
static IDCompositionVisual*    g_dcVisual = nullptr;
static HWND g_hwnd = nullptr;
static void ClipCapture();                 // clipboard history: defined with the launcher
// Whatever had the keyboard before the launcher opened. The launcher calls SetForegroundWindow on
// itself, so by the time an entry is picked the app you wanted to paste INTO is no longer focused -
// a synthetic Ctrl+V would land on nothing at all.
static HWND g_launPrevFg=nullptr;
static void ClipRebuild(const char* needleRaw);
// ---- monitors ----------------------------------------------------------------------------
// g_mx..g_mh is the ACTIVE monitor: the one the pop-up panels (drawer, quick settings, launcher,
// session, settings, dim, notifications) live on. It follows the cursor. The always-on layers —
// the desktop bubble and the taskbar — instead span the WHOLE virtual screen in one window each
// and draw themselves once per monitor, so every screen gets a desktop and a bar.
static int  g_mx=0, g_my=0, g_mw=1920, g_mh=1080;   // active monitor geometry (physical px)
// Per-monitor switches (Settings > Monitors). Turning a layer off on a screen is a real saving:
// the shell windows are resized to the bounding box of the screens that still use them, so a
// disabled monitor costs nothing to clear, draw or present.
struct MonCfg { bool desktop=true, bar=true, panels=true; };
static std::map<std::string,MonCfg> g_monCfg;        // keyed by the short device name, e.g. "DISPLAY2"
struct MonInfo { RECT rc; bool primary; std::string dev; MonCfg cfg; };
static std::vector<MonInfo> g_mons;                  // every monitor, primary first
static RECT g_vs={0,0,1920,1080};                    // area the shell layers cover (physical px)
// The TRUE virtual screen, every monitor, regardless of what the user switched off per display.
// g_vs deliberately shrinks to the monitors the shell paints on - but WINDOW DECORATIONS follow the
// focused window, which can be on a monitor with the bar and desktop turned off. Sizing the deco
// overlay to g_vs meant it silently could not draw there at all: the bar was computed at the right
// screen coordinates and then rendered outside its own window. Decorations get their own extent.
static RECT g_dvs={0,0,1920,1080};
// "Span" wallpaper (WallpaperStyle 22): ONE image laid across every monitor, each screen showing its
// own slice. Wallpaper Engine writes its desktop snapshot this way - a 3840x1080 image for two
// 1920x1080 screens - and Aether used to cover-fit that whole image into EACH bubble, so both monitors
// showed the middle of it: half one screen's wallpaper, half the other's (black, where WE had nothing
// assigned). Read whenever the wallpaper loads.
static bool g_wallSpan=false;
static void ReadWallStyle(){
    wchar_t v[16]={0}; DWORD sz=sizeof(v);
    g_wallSpan = RegGetValueW(HKEY_CURRENT_USER,L"Control Panel\\Desktop",L"WallpaperStyle",RRF_RT_REG_SZ,nullptr,v,&sz)==ERROR_SUCCESS
                 && wcscmp(v,L"22")==0;
}

static int  g_actMon=0;                              // index into g_mons
static bool g_monsDirty=false;                       // re-enumerate + resize the shell layers next frame
static BOOL CALLBACK MonEnumCb(HMONITOR h,HDC,LPRECT,LPARAM){
    MONITORINFOEXW mi={}; mi.cbSize=sizeof(mi);
    if(GetMonitorInfoW(h,(MONITORINFO*)&mi)){
        MonInfo m; m.rc=mi.rcMonitor; m.primary=(mi.dwFlags&MONITORINFOF_PRIMARY)!=0;
        std::wstring dev=mi.szDevice; size_t s=dev.find_last_of(L"\\");
        if(s!=std::wstring::npos) dev=dev.substr(s+1);          // "\\.\DISPLAY2" -> "DISPLAY2"
        m.dev=std::string(dev.begin(),dev.end());
        auto it=g_monCfg.find(m.dev); if(it!=g_monCfg.end()) m.cfg=it->second;
        if(m.primary) g_mons.insert(g_mons.begin(),m); else g_mons.push_back(m); }
    return TRUE;
}
static void RefreshMonitors(){
    g_mons.clear();
    EnumDisplayMonitors(nullptr,nullptr,MonEnumCb,0);
    if(g_mons.empty()){ MonInfo m; m.rc=RECT{0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
        m.primary=true; m.dev="DISPLAY1"; g_mons.push_back(m); }
    // the shell layers only need to cover the monitors that actually draw something
    bool any=false; RECT u={0,0,0,0};
    for(auto& m:g_mons){
        if(!m.cfg.desktop && !m.cfg.bar) continue;
        if(!any){ u=m.rc; any=true; }
        else { u.left=std::min(u.left,m.rc.left); u.top=std::min(u.top,m.rc.top);
               u.right=std::max(u.right,m.rc.right); u.bottom=std::max(u.bottom,m.rc.bottom); }
    }
    if(!any) u=g_mons[0].rc;            // everything off: keep a minimal window on the primary
    g_vs=u;
    // the decoration layer spans EVERY monitor, whatever the per-display switches say
    { RECT v{ g_mons[0].rc };
      for(auto& m:g_mons){ v.left=std::min(v.left,m.rc.left); v.top=std::min(v.top,m.rc.top);
                           v.right=std::max(v.right,m.rc.right); v.bottom=std::max(v.bottom,m.rc.bottom); }
      g_dvs=v; }
    if(g_actMon>=(int)g_mons.size()) g_actMon=0;
}
static int MonIndexAt(POINT p){
    for(size_t i=0;i<g_mons.size();i++){ const RECT& r=g_mons[i].rc;
        if(p.x>=r.left&&p.x<r.right&&p.y>=r.top&&p.y<r.bottom) return (int)i; }
    return 0;
}
static const RECT& MonRect(int i){ return g_mons[std::min(std::max(i,0),(int)g_mons.size()-1)].rc; }
static float g_reveal = 0.0f;                        // 0 hidden .. 1 shown (drawer slide)
static float g_tabX   = 2.0f;                        // animated tab position
// ---- dashboard -> tab transitions --------------------------------------------------------------
// Tabs always cross-slid horizontally off g_tabX. Jumping from a DASHBOARD CARD to the tab it
// represents wants a different feel per card, so a jump carries a STYLE: the media card slides, the
// weather card zooms out of the card you clicked, everything else defocuses. g_tabFxT runs 0->1
// over the switch; the tab strip still drives g_tabX, so a plain tab click keeps the old slide.
enum { TT_SLIDE=0, TT_ZOOM, TT_BLUR };
static int    g_tabFx      = TT_SLIDE;
static float  g_tabFxT     = 1.0f;                   // 1 = settled, 0 = just switched
static ImVec2 g_tabFxOrigin= ImVec2(0,0);            // screen point a zoom grows out of
static int    g_tabFxFrom  = 0;                      // the tab we left, so it can animate out
static void GoTab(int tab,int fx,ImVec2 origin);     // fwd: defined once g_drawerForceUntil exists

static float g_drawerMorphH = 0.0f;                  // drawer height morphs to fit the active tab's content
static float g_drawerMorphW = 0.0f, g_drawerMorphWV = 0.0f;   // ...and its WIDTH, on the same spring
// The drawer's own inner rect (below the tab bar), published by DrawUI so a page can paint a
// FULL-BLEED backdrop. Pages are laid out from a content box inset 16px from the drawer edge, which
// is right for widgets and wrong for the Media tab's album-art wash - the rice runs that wash to the
// panel edge, and an inset one reads as a card floating inside the drawer.
static ImVec2 g_pageBleed0=ImVec2(0,0), g_pageBleed1=ImVec2(0,0);
static float g_drawerMorphV = 0.0f;                  // spring velocity (Caelestia expressiveSpatial feel)
static RECT  g_drawerRect = {0,0,0,0};               // visible drawer bounds (client coords); outside = click-through
static float g_frameDt = 0.016f;                     // per-frame dt for animations
static std::unordered_map<int,float> g_hover;        // per-element hover animation state

#include "src/config/Tokens.h"      // Caelestia design tokens (rounding/spacing/padding/font/sizes)
#include "src/services/Colours.h"    // Caelestia Material-3 colour roles (default/fallback scheme)
#include "src/services/WorkspaceSlide.h"  // niri-style window slide on a workspace switch
#include "src/components/Anim.h"     // Caelestia motion system (curves + durations + time-based animator + HoverAnim)

// ---- config (config.json next to the exe, like Caelestia's shell.json) ----
static float g_cardRound   = 20.0f;
static float g_panelRound  = 26.0f;
static int   g_drawerAlpha = 240;                    // drawer panel opacity 0..255
// Every surface used to get a 1px rim stroke - a bright bevel line plus a dark outer line - to make
// it read as raised. On a ROUNDED corner a hard 1px stroke is the one thing that cannot be
// antialiased away: it traces the arc as a chain of discrete pixels, so the outline stays visibly
// stepped no matter how smooth the fill under it is. That is what actually read as "blocky".
// Off by default; the soft drop shadows stay, so surfaces still lift without being outlined.
static bool  g_panelOutline=false;                   // appearance.outlines
// The soft haze around everything: a vignette over the surround, a "recessed well" halo ringed
// around the desktop opening, a drop shadow under every panel, and the frosted wallpaper filling
// the margin. Individually subtle, stacked they read as a grey glow behind the whole shell.
// Off = flat surfaces on a flat surround.
static bool  g_deskGlow=true;                        // appearance.glow
// The drop shadow specifically under the BIG panel surfaces - the dashboard drawer, settings, the
// flyouts. Those sit over the desktop rather than in the margin, so a halo behind them reads as
// grime around the panel rather than depth. Separate from g_deskGlow, which is the desktop's own
// surround treatment and is wanted.
static bool  g_panelShadow=false;                    // appearance.panelShadow
static int   g_bgMode      = 0;                      // 0 acrylic (frosted desktop), 1 image/gif, 2 solid
static float g_bgOpacity   = 0.55f;
static bool  g_bgBlur      = true;
static bool  g_dynamicColor= true;                   // derive accent from the wallpaper (Material-You)
static bool  g_hideTaskbar = true;                   // hide the Windows taskbar (Cairo-style takeover)
enum { CWSBT_AUTO=0, CWSBT_NONE=1, CWSBT_MICA=2, CWSBT_ACRYLIC=3, CWSBT_TABBED=4 };  // DWM system backdrop kinds
static bool  g_micaMode=false;                       // "Mica for everyone": apply a backdrop to all app windows
static int   g_micaKind=CWSBT_MICA;                  // which backdrop (Mica/Acrylic/Tabbed)
static RECT  g_savedWorkArea={0,0,0,0}; static bool g_taskbarHidden=false;
static std::vector<RECT> g_savedWorkAreas;    // one per monitor, restored on exit
static std::string g_bgPath, g_profileName;
// animated background frames (png = 1 frame, gif = N frames)
static std::vector<ID3D11ShaderResourceView*> g_bgFrames;
static std::vector<int> g_bgDelays;                  // per-frame ms
static int    g_bgFrame=0; static double g_bgClock=0;
static ID3D11ShaderResourceView* g_acrylicTex=nullptr;
static ImFont* g_fReg = nullptr;   // Segoe UI 18
static ImFont* g_fMed = nullptr;   // Segoe UI Semibold 21
static ImFont* g_fBig = nullptr;   // Segoe UI 34
static ImFont* g_fSml = nullptr;   // Segoe UI 15
static ImFont* g_fHuge= nullptr;   // Segoe UI Light 68 (clock)
static ImFont* g_fMono= nullptr;   // Adwaita/Cascadia Mono — clock / code / terminal look
static ImFont* g_fClock=nullptr;
static ImFont* g_fCondBig=nullptr; // the same face baked at 256px, clock characters only (the lock clock is ~300px)
static ImFont* g_fCond=nullptr;    // condensed numerals (Bahnschrift) - the Caelestia v2 usage badges   // heavy geometric numerals for the desktop clock (Rubik stand-in)
// Material Symbols Rounded, subset to the glyphs the shell draws. This is the same mechanism
// Caelestia uses - their MaterialIcon.qml renders glyphs out of this very font - so pointing at it
// is what actually makes the iconography match, rather than hunting for PNGs they never shipped.
static ImFont* g_fIcon = nullptr;
// Defined down with ThemedSym, but the weather page and the dashboard tabs draw icons long before
// that point, so they are declared here.
static unsigned short MIconCp(const char* name);
static bool MSym(ImDrawList* dl,ImVec2 c,float px,ImU32 col,unsigned short cp);
// latin + general punctuation (em dash / curly quotes / ellipsis, which notification bodies use)
// + arrows. Without the punctuation block those characters render as "?".
// Window titles, notification bodies and track names are not Latin-1. A Discord title came out
// "@??jeru ???+??? ? - Discord" purely because the atlas had no Cyrillic - the text was fine, the
// glyphs were missing. Latin Extended-A/B, Greek and Cyrillic are cheap and cover almost everything
// a European desktop throws at the bar. (CJK is deliberately NOT here: it is thousands of glyphs
// across seven faces, and no shipped font covers it anyway.)
static const ImWchar RANGES[] = {0x0020,0x00FF, 0x0100,0x024F, 0x0370,0x03FF, 0x0400,0x04FF,
                                 0x2010,0x206F, 0x2190,0x21FF, 0x2600,0x26FF, 0};

// -------- palette — live, driven by the colour-scheme table below (Settings > Appearance) --------
// Every surface colour is mutable so a scheme swap recolours the whole shell at runtime.
static ImU32 COL_CARD   = IM_COL32(230,240,238,255);   // card surface
static ImU32 COL_CARD2  = IM_COL32(220,232,229,255);   // inner / secondary surface
static ImU32 COL_INK    = IM_COL32(24,44,41,255);      // primary text
static ImU32 COL_INK2   = IM_COL32(104,124,120,255);   // muted text
static ImU32 COL_GOLD   = IM_COL32(32,106,96,255);     // accent
static ImU32 COL_GOLDBG = IM_COL32(198,226,220,255);   // accent pill bg
static ImU32 COL_ACCD   = IM_COL32(40,187,94,255);     // accent on dark surfaces
static ImU32 COL_TRACK  = IM_COL32(206,222,218,255);   // gauge / slider track
static ImU32 COL_PANELL = IM_COL32(246,250,249,255);   // panel surface (drawer/bar/sidebar)
static ImU32 COL_BG     = IM_COL32(246,250,249,255);
static ImU32 COL_TABBG  = IM_COL32(232,242,239,235);
static const ImU32 COL_ERR = IM_COL32(186,64,52,255);
static bool  g_darkUI   = false;                       // true when the active scheme is dark

// colour helpers: recolour with a given alpha / multiply an existing colour's alpha
static ImU32 WithA(ImU32 c,int a){ return (c&0x00FFFFFF)|((ImU32)std::clamp(a,0,255)<<24); }
static ImU32 MulA(ImU32 c,float t){ return WithA(c,(int)(((c>>24)&0xFF)*std::clamp(t,0.0f,1.0f))); }
static ImU32 PanelCol(int a){ return WithA(COL_PANELL,a); }
static ImU32 AccA(int a){ return WithA(COL_GOLD,a); }
// Accent lightened toward white — stays legible as a label colour even when the wallpaper-derived accent
// is a dark hue (e.g. deep red). Used for small "Connected"/status text sitting on an accent wash.
static ImU32 AccBright(){ int r=COL_GOLD&0xFF,g=(COL_GOLD>>8)&0xFF,b=(COL_GOLD>>16)&0xFF;
    r+=(255-r)*55/100; g+=(255-g)*55/100; b+=(255-b)*55/100; return IM_COL32(r,g,b,255); }
// Material 3 builds secondary and tertiary from the SAME source hue as the primary: secondary
// keeps the hue at roughly a third of the chroma, tertiary rotates the hue by +60 degrees. Caelestia
// leans on both roles in the bar - Clock.qml and OsIcon.qml are m3tertiary, StatusIcons.qml is
// m3secondary - but this port's scheme table only stores one accent, so derive the other two rather
// than drawing those entries in plain ink (which is what made our strip read as flat white).
static ImU32 M3Role(float hueShift,float satMul){
    float r=(COL_GOLD&0xFF)/255.0f, g=((COL_GOLD>>8)&0xFF)/255.0f, b=((COL_GOLD>>16)&0xFF)/255.0f;
    float h,sv,v; ImGui::ColorConvertRGBtoHSV(r,g,b,h,sv,v);
    h=fmodf(h+hueShift+1.0f,1.0f);
    sv=std::clamp(sv*satMul,0.10f,0.85f);
    v = g_darkUI ? 0.80f : 0.44f;                 // legible on the strip in either scheme
    ImGui::ColorConvertHSVtoRGB(h,sv,v,r,g,b);
    return IM_COL32((int)(r*255+0.5f),(int)(g*255+0.5f),(int)(b*255+0.5f),255);
}
// M3's secondary/tertiary tonal palettes are built at chroma 16 and 24 against a primary that is
// usually 36-48, i.e. roughly a third and a half of it. Running tertiary near full chroma made the
// clock read as neon next to the reference's muted gold.
static ImU32 M3Secondary(){ return M3Role(0.0f, 0.38f); }
static ImU32 M3Tertiary (){ return M3Role(60.0f/360.0f, 0.44f); }
static ImU32 M3OnPrimary(){ return g_darkUI?IM_COL32(16,20,18,255):IM_COL32(250,254,252,255); }

static ImU32 Mix(ImU32 a,ImU32 b,float t){
    int ar=a&0xFF,ag=(a>>8)&0xFF,ab=(a>>16)&0xFF,aa=(a>>24)&0xFF;
    int br=b&0xFF,bg=(b>>8)&0xFF,bb=(b>>16)&0xFF,ba=(b>>24)&0xFF;
    return IM_COL32((int)(ar+(br-ar)*t),(int)(ag+(bg-ag)*t),(int)(ab+(bb-ab)*t),(int)(aa+(ba-aa)*t));
}

// ---- colour schemes (Settings > Appearance > Available color schemes) ----
struct Scheme {
    const char* id; const char* name; const char* family; bool dark;
    ImU32 accent, panel, card, card2, ink, ink2, track, deskbg;
};
static const Scheme BUILTIN_SCHEMES[] = {
  {"caelestia",            "default",   "caelestia",  false, IM_COL32(32,106,96,255),  IM_COL32(246,250,249,255), IM_COL32(230,240,238,255), IM_COL32(220,232,229,255), IM_COL32(24,44,41,255),    IM_COL32(104,124,120,255), IM_COL32(206,222,218,255), IM_COL32(9,9,11,255)},
  {"caelestia-dark",       "dark",      "caelestia",  true,  IM_COL32(40,187,94,255),  IM_COL32(16,20,18,255),    IM_COL32(26,32,29,255),    IM_COL32(34,42,38,255),    IM_COL32(226,240,233,255), IM_COL32(140,160,150,255), IM_COL32(48,58,53,255),    IM_COL32(6,8,7,255)},
  {"catppuccin-latte",     "latte",     "catppuccin", false, IM_COL32(136,57,239,255), IM_COL32(239,241,245,255), IM_COL32(230,233,239,255), IM_COL32(220,224,232,255), IM_COL32(76,79,105,255),   IM_COL32(124,127,147,255), IM_COL32(204,208,218,255), IM_COL32(220,224,232,255)},
  {"catppuccin-frappe",    "frappe",    "catppuccin", true,  IM_COL32(202,158,230,255),IM_COL32(48,52,70,255),    IM_COL32(41,44,60,255),    IM_COL32(51,56,76,255),    IM_COL32(198,208,245,255), IM_COL32(165,173,206,255), IM_COL32(65,69,89,255),    IM_COL32(35,38,52,255)},
  {"catppuccin-macchiato", "macchiato", "catppuccin", true,  IM_COL32(198,160,246,255),IM_COL32(36,39,58,255),    IM_COL32(30,32,48,255),    IM_COL32(42,45,66,255),    IM_COL32(202,211,245,255), IM_COL32(165,173,203,255), IM_COL32(54,58,79,255),    IM_COL32(24,25,38,255)},
  {"catppuccin-mocha",     "mocha",     "catppuccin", true,  IM_COL32(203,166,247,255),IM_COL32(30,30,46,255),    IM_COL32(24,24,37,255),    IM_COL32(49,50,68,255),    IM_COL32(205,214,244,255), IM_COL32(166,173,200,255), IM_COL32(49,50,68,255),    IM_COL32(17,17,27,255)},
  {"darkgreen-hard",       "hard",      "darkgreen",  true,  IM_COL32(40,187,94,255),  IM_COL32(10,14,11,255),    IM_COL32(16,22,18,255),    IM_COL32(22,30,25,255),    IM_COL32(224,240,228,255), IM_COL32(130,155,138,255), IM_COL32(38,48,41,255),    IM_COL32(4,6,5,255)},
  {"darkgreen-medium",     "medium",    "darkgreen",  true,  IM_COL32(56,200,110,255), IM_COL32(18,24,20,255),    IM_COL32(26,34,28,255),    IM_COL32(34,44,36,255),    IM_COL32(228,242,232,255), IM_COL32(140,164,146,255), IM_COL32(46,58,49,255),    IM_COL32(8,12,9,255)},
  // ---- real Linux desktop themes pulled from the CachyOS ISO (KDE Breeze + CachyOS green) ----
  {"breeze-dark",          "Breeze Dark","breeze",    true,  IM_COL32(61,174,233,255), IM_COL32(35,38,41,255),    IM_COL32(27,30,32,255),    IM_COL32(42,46,50,255),    IM_COL32(252,252,252,255), IM_COL32(127,140,141,255), IM_COL32(61,64,68,255),    IM_COL32(20,22,24,255)},
  {"breeze-light",         "Breeze",    "breeze",     false, IM_COL32(61,174,233,255), IM_COL32(239,240,241,255), IM_COL32(252,252,252,255), IM_COL32(224,226,228,255), IM_COL32(35,38,41,255),    IM_COL32(127,140,141,255), IM_COL32(210,212,214,255), IM_COL32(218,220,222,255)},
  {"cachyos",              "CachyOS",   "cachyos",    true,  IM_COL32(46,194,126,255), IM_COL32(30,33,36,255),    IM_COL32(24,27,30,255),    IM_COL32(38,42,46,255),    IM_COL32(248,250,249,255), IM_COL32(130,150,142,255), IM_COL32(52,58,55,255),    IM_COL32(16,18,20,255)},
  {"cachyos-light",        "CachyOS Light","cachyos", false, IM_COL32(38,166,110,255), IM_COL32(240,242,241,255), IM_COL32(252,253,252,255), IM_COL32(226,230,228,255), IM_COL32(28,34,31,255),    IM_COL32(120,138,130,255), IM_COL32(212,216,214,255), IM_COL32(220,224,222,255)},
};
// The schemes the shell actually offers: the built-ins above, followed by (or overridden by) the user's
// own files in config\schemes\*.toml - see LoadUserSchemes. Strings for user schemes live in
// g_schemeText, a deque, so the const char* in each Scheme stays valid as more are added.
static std::vector<Scheme> g_schemes(std::begin(BUILTIN_SCHEMES), std::end(BUILTIN_SCHEMES));
static std::deque<std::string> g_schemeText;
#define SCHEMES  g_schemes
#define NSCHEMES ((int)g_schemes.size())
static int  g_scheme = 0;
static int  g_themeMode = 0;                       // 0 scheme-as-is, 1 force light, 2 force dark, 3 follow Windows
static ImU32 COL_DESKBG = IM_COL32(9,9,11,255);    // shell surface behind/around the desktop bubble
static bool  g_customAccent=false;                 // config accent overrides the scheme's

// ---------------------------------------------------------------------------------------------
// ANIMATED PALETTE. Every colour in the shell lives in these twelve globals, and both ApplyScheme
// and the wallpaper-derived accent rewrote them in one go - so a scheme change or a new wallpaper
// SNAPPED. They now write as before, then hand the before/after pair to a short cross-fade, and the
// render loop lerps the globals between them. Nothing else in the shell had to change: every widget
// keeps reading COL_* exactly as it did.
struct Pal { ImU32 card,card2,ink,ink2,gold,goldbg,accd,track,panell,bg,tabbg,deskbg; };
static Pal PalNow(){ return {COL_CARD,COL_CARD2,COL_INK,COL_INK2,COL_GOLD,COL_GOLDBG,
                             COL_ACCD,COL_TRACK,COL_PANELL,COL_BG,COL_TABBG,COL_DESKBG}; }
static void PalSet(const Pal& p){
    COL_CARD=p.card; COL_CARD2=p.card2; COL_INK=p.ink; COL_INK2=p.ink2;
    COL_GOLD=p.gold; COL_GOLDBG=p.goldbg; COL_ACCD=p.accd; COL_TRACK=p.track;
    COL_PANELL=p.panell; COL_BG=p.bg; COL_TABBG=p.tabbg; COL_DESKBG=p.deskbg;
}
static Pal   g_palFrom, g_palTo;
static float g_palT=1.0f;
static bool  g_palInit=false;      // the FIRST apply (config load) must snap, not fade in from white
// Called at the end of an apply: `before` is what the palette looked like on the way in.
static void PalCommit(const Pal& before){
    Pal after=PalNow();
    if(!g_palInit){ g_palInit=true; g_palFrom=g_palTo=after; g_palT=1.0f; return; }
    g_palFrom=before; g_palTo=after; g_palT=0.0f;
    PalSet(before);                // rewind, so the cross-fade actually starts from the old colours
}
// returns true while the cross-fade is running, so the caller can mark the static desktop layer
// dirty - g_deskDirty is declared much further down and cannot be touched from here
static bool PalTick(float dt){
    if(g_palT>=1.0f) return false;
    float sp=std::max(0.2f,Cael::g_animSpeed);
    g_palT=std::min(1.0f,g_palT+dt*sp/0.60f);            // ~600ms at 1x
    float e=Cael::eval(Cael::STANDARD,g_palT);
    Pal c;
    c.card  =Mix(g_palFrom.card ,g_palTo.card ,e); c.card2 =Mix(g_palFrom.card2 ,g_palTo.card2 ,e);
    c.ink   =Mix(g_palFrom.ink  ,g_palTo.ink  ,e); c.ink2  =Mix(g_palFrom.ink2  ,g_palTo.ink2  ,e);
    c.gold  =Mix(g_palFrom.gold ,g_palTo.gold ,e); c.goldbg=Mix(g_palFrom.goldbg,g_palTo.goldbg,e);
    c.accd  =Mix(g_palFrom.accd ,g_palTo.accd ,e); c.track =Mix(g_palFrom.track ,g_palTo.track ,e);
    c.panell=Mix(g_palFrom.panell,g_palTo.panell,e); c.bg  =Mix(g_palFrom.bg    ,g_palTo.bg    ,e);
    c.tabbg =Mix(g_palFrom.tabbg,g_palTo.tabbg,e); c.deskbg=Mix(g_palFrom.deskbg,g_palTo.deskbg,e);
    PalSet(c);
    return true;
}

static void ApplyScheme(int idx){
    Pal palBefore=PalNow();
    idx=std::clamp(idx,0,NSCHEMES-1); g_scheme=idx; const Scheme& s=SCHEMES[idx];
    if(!g_customAccent){ COL_GOLD=s.accent;
        int r=s.accent&0xFF,g=(s.accent>>8)&0xFF,b=(s.accent>>16)&0xFF;
        COL_GOLDBG = s.dark ? IM_COL32(r/3+18,g/3+18,b/3+18,255)
                            : IM_COL32(std::min(255,r+150),std::min(255,g+120),std::min(255,b+120),255); }
    COL_PANELL=s.panel; COL_BG=s.panel; COL_CARD=s.card; COL_CARD2=s.card2;
    COL_INK=s.ink; COL_INK2=s.ink2; COL_TRACK=s.track; COL_DESKBG=s.deskbg;
    COL_TABBG=Mix(s.panel,s.card,0.5f); COL_ACCD=COL_GOLD; g_darkUI=s.dark;
    PalCommit(palBefore);
}
