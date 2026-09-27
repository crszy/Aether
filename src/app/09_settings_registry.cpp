// Aether - the settings registry (SETTINGS table, config load/save).
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =================================================================================================
// THE SETTINGS REGISTRY
//
// Every knob in the shell is declared here exactly ONCE: its path, its type, the variable it drives,
// its range, and the sentence that documents it. Everything else is derived from this table -
// the generated config\*.toml files and their comments, reading them back at startup, and writing a
// value back when the GUI changes it. Before this, adding a setting meant editing a JSON writer, a
// JSON reader and a UI row and hoping the three agreed; they frequently did not, which is how
// SaveConfig ended up emitting malformed JSON earlier in this project.
//
// The path's first segment picks the file: "bar.size" lives in config\bar.toml under [bar].
// =================================================================================================
// The bar's item list lives in g_barItems as {id,on} pairs. The registry only speaks plain types,
// so bar.items is carried as the same words config.json uses - "clock", "!audio" - and converted at
// the edges. Without this the layout could only be changed by hand-editing config.json: there is
// no Settings page for it, and an item switched off stayed off with no visible way back.
static std::vector<std::string> g_barItemsToml;
static void BarItemsToToml(){
    g_barItemsToml.clear();
    for(auto& c:g_barItems) g_barItemsToml.push_back(std::string(c.on?"":"!")+BAR_ITEMS[c.id].key);
}
static void BarItemsFromToml(){
    if(g_barItemsToml.empty()) return;             // an empty list is a mistake, not "hide everything"
    g_barItems.clear();
    for(auto& t:g_barItemsToml){
        bool on=true; std::string k=t;
        if(!k.empty()&&k[0]=='!'){ on=false; k.erase(0,1); }
        for(int i=0;i<BIT_COUNT;i++) if(k==BAR_ITEMS[i].key){ g_barItems.push_back({i,on}); break; }
    }
    BarItemsRepair();                              // unknown words dropped, missing items restored
}

enum SKind { SK_BOOL, SK_INT, SK_FLOAT, SK_STR, SK_ENUM, SK_STRLIST };
struct Setting {
    const char* path;
    SKind       kind;
    void*       ptr;
    double      lo, hi;                 // clamp for numbers; ignored otherwise
    const char* const* names; int nnames;   // SK_ENUM: the words written to the file
    const char* doc;
};

static const char* EDGE_NAMES[]   = { "left","right","top","bottom" };
static const char* LPOS_NAMES[]   = { "bottom","centre","top" };
// These tables are indexed by the ENUM value, so their order must be the enum's order. Two were not:
// LANIM_NAMES was written in menu order and ICONSET_NAMES listed icon families that do not exist, so
// "pop" in launcher.toml really selected Drop, "fade" selected None, and so on.
static const char* LANIM_NAMES[]  = { "rise","fade","pop","drop","none" };   // == LANIM_RISE..LANIM_NONE
static const char* ICONSET_NAMES[]= { "breeze","adwaita","aether","material" };   // == ICONSET_KEY order
static const char* THEME_NAMES[]  = { "scheme","dark","light" };
static const char* MDSTYLE_NAMES[]= { "circle","shaped" };
// ---- motion: every panel's open/close curve and duration is the user's ----
static const char* MOTION_NAMES[]      = { "expressive","expressive_fast","expressive_slow","emphasized","smooth",
                                           "standard","linear","bounce","instant","custom","strive" };
static const char* MOTION_PRETTY[]     = { "Expressive (Caelestia)","Expressive fast","Expressive slow","Emphasized",
                                           "Smooth","Standard","Linear","Bounce","Instant","Custom curve","Strive (slam + hit-stop)" };
static const char* MOTION_CLOSE_NAMES[]= { "same","expressive","expressive_fast","expressive_slow","emphasized","smooth",
                                           "standard","linear","bounce","instant","custom","strive" };
static const char* MOTION_PANEL_NAMES[]= { "global","expressive","expressive_fast","expressive_slow","emphasized","smooth",
                                           "standard","linear","bounce","instant","custom","strive" };
enum { MSTY_EXPRESSIVE=0, MSTY_EXP_FAST, MSTY_EXP_SLOW, MSTY_EMPHASIZED, MSTY_SMOOTH, MSTY_STANDARD, MSTY_LINEAR,
       MSTY_BOUNCE, MSTY_INSTANT, MSTY_CUSTOM, MSTY_STRIVE, MSTY_N };
enum { MP_DASHBOARD=0, MP_LAUNCHER, MP_PICKER, MP_NOTIF, MP_QS, MP_OSD, MP_SESSION, MP_POPOUT, MP_TOAST, MP_SETTINGS, MP_TILE, MP_OVERVIEW, MP_COUNT };
static bool g_tileAnim=true;          // windows.tile_animation
static const char* MP_PRETTY[MP_COUNT] = { "Dashboard","Launcher","Wallpaper picker","Notifications","Quick settings",
                                           "Volume / brightness OSD","Power menu","Bar popouts","Now Playing toast","Settings","Window tiling (komorebi)",
                                           "Workspace overview" };
static int   g_motionStyle=MSTY_EXPRESSIVE;     // motion.style
// ---- Guilty Gear Strive extras (src/modules/strive/Strive.h) - every one is opt-in ----
static bool  g_stvImpact=false;                 // strive.impact
static float g_stvImpactStrength=1.0f;          // strive.impact_strength
static bool  g_stvBanner=false;                 // strive.workspace_banner
static std::string g_stvBannerText="WORKSPACE %d";          // strive.banner_text
static bool  g_stvIntroStart=false;             // strive.intro_startup
static bool  g_stvIntroUnlock=false;            // strive.intro_unlock
static std::string g_stvIntroLines="HEAVEN OR HELL|DUEL %n|LET'S ROCK";   // strive.intro_lines
static int   g_motionMs=0;                      // motion.duration_ms   (0 = the style's own)
static int   g_motionCloseStyle=0;              // motion.close_style   (0 = same as opening)
static int   g_motionCloseMs=0;                 // motion.close_duration_ms (0 = same as opening)
static bool  g_motionOvershoot=true;            // motion.overshoot
static std::string g_motionCustom="0.38, 1.21, 0.22, 1.0";   // motion.custom_curve
static const char* NF_DISMISS_NAMES[] = { "slide","fade","shrink" };
static const char* NF_ARRIVE_NAMES[]  = { "slide","fade","pop","none" };
static bool  g_nfFlick=true, g_nfMiddle=true, g_nfExpandable=true, g_nfExpandDefault=false;
static int   g_nfDismissAnim=0, g_nfDismissMs=400, g_nfArriveAnim=0, g_nfArriveMs=450;
static const char* LSWITCH_NAMES[] = { "fade","slide","none" };
static int   g_launSwitchAnim=1, g_launSwitchMs=220; static bool g_launResize=true;
static bool  g_wallPreviewImages=true; static int g_wallPreviewDelay=350;
// ---- profile (src/modules/dashboard/Profile.h) ----
enum { PRES_ONLINE=0, PRES_IDLE, PRES_DND, PRES_INVISIBLE, PRES_NONE };
static const char* PRESENCE_NAMES[] = { "online","idle","dnd","invisible","none" };
static int         g_presence=PRES_ONLINE;       // profile.presence
static std::string g_status;                     // profile.status
static std::string g_statusIcon="mood";          // profile.status_icon
static int         g_statusClearMin=0;           // profile.status_clear_after  (minutes; 0 never, -1 end of day)
static int         g_statusSetAt=0;              // profile.status_set_at       (unix seconds)
static std::string g_avatarImage;                // profile.avatar
static std::string g_avatarShape="circle";       // profile.avatar_shape
static bool        g_avatarRing=true;            // profile.avatar_ring
static bool        g_profileShowName=true;       // profile.show_name
static bool        g_presenceDndLink=true;       // profile.presence_sets_dnd
static std::string g_profileRows="os|{os}; select_window|{wm}; clock_arrow_up|{uptime_long}";   // profile.rows
static ULONGLONG   g_drawerForceUntil=0;
// ---- Caelestia v2 layouts (src/modules/dashboard/CaelV2.h) ----
static const char* V2LAYOUT_NAMES[] = { "classic","caelestia" };
static int         g_homeLayout=0, g_mediaLayout=0, g_perfLayout=0;   // dashboard.home_layout / media_layout / performance_layout
static std::string g_v2Bubble="{wm}";            // profile.bubble
static std::string g_v2Logo="os";                // profile.logo
static std::string g_v2TodayShape="cookie9";     // dashboard.today_shape
// ---- merged Caelestia sidebar (src/modules/sidebar/SidebarV2.h) ----
static const char* SIDESTYLE_NAMES[] = { "classic","caelestia" };
static const char* RECMODE_NAMES[]   = { "fullscreen","all","gamebar" };
static int         g_sideStyle=0;                                                   // sidebar.style
static std::string g_sideToggles="wifi,bluetooth,mic,settings,gamemode,dnd,keepawake,theme";   // sidebar.toggles
static std::string g_sideActions="logout,power,avatar,updates,reload";              // sidebar.actions
static std::string g_sideAvatar;                                                    // sidebar.avatar
static int         g_sideWidth=425;
static float       g_sideOpacity=1.0f;                                              // sidebar.opacity                                                 // sidebar.width
static int         g_recMode=0;                                                     // sidebar.recorder
static const char* TOASTSTYLE_NAMES[] = { "classic","caelestia" };
static int         g_toastStyle=1;                                                  // notifications.toast_style
// ---- Caelestia top bar (src/modules/bar/BarV2.h) ----
static const char* BARSTYLE_NAMES[] = { "classic","caelestia" };
static int  g_barStyle=0;                 // bar.style
static bool g_barV2Kbd=true;              // bar.keyboard_layout
static bool g_barV2Power=true;            // bar.power_profile
static bool g_barV2KbdToast=true;         // bar.layout_toast
// ---- desktop lyrics + background visualiser (src/modules/desktop/DesktopMusic.h) ----
static bool  g_deskLyrics=false, g_deskLyricsAutoHide=true;     // desktop.lyrics / lyrics_autohide
static float g_deskLyricsSize=30.0f, g_deskLyricsY=0.74f;       // desktop.lyrics_size / lyrics_position
static bool  g_deskViz=false, g_deskVizAutoHide=true, g_deskVizMirror=true;   // desktop.visualiser / _autohide / _mirror
static float g_deskVizHeight=0.22f, g_deskVizBar=3.0f, g_deskVizGap=3.0f;     // desktop.visualiser_height / _bar / _gap
static std::string g_deskVizColor;                               // desktop.visualiser_color
// Which render endpoint the spectrum listens on. Empty = follow whatever is actually making noise.
static std::string g_vizDevice;                                  // audio.visualiser_device
static int   g_deskMusicFps=60;                                  // desktop.music_fps
// ---- Caelestia launcher (favourites / hidden apps / command icons / accent dots) ----
static const char* LAUNSTYLE_NAMES[] = { "classic","caelestia" };
static int  g_launStyle=0;                        // launcher.style
static std::vector<std::string> g_launFavs;       // launcher.favourites (app paths)
static std::vector<std::string> g_launHidden;     // launcher.hidden     (app paths)
// ---- launcher customisation (src/modules/launcher/LauncherFx.h) ----
static const char* LLAYOUT_NAMES[]    = { "list","grid","horizontal","vertical","radial" };
static const char* LSHAPE_NAMES[]     = { "rounded","pill","square","chamfer","hexagon","slant" };
static const char* LGLOWCOL_NAMES[]   = { "accent","rainbow","custom" };
static const char* LDEPTH_NAMES[]     = { "flat","tilt","extrude","tilt_extrude" };
static const char* LSEARCHANIM_NAMES[]= { "none","pop","wave","bounce","glitch","rainbow","typewriter" };
static const char* LCARET_NAMES[]     = { "bar","block","underline" };
static const char* LRESANIM_NAMES[]   = { "cascade","fade","scale","slide","flip","none" };
static const char* LSEL_NAMES[]       = { "fill","glide","outline","bar","glow" };
static const char* LICONHOVER_NAMES[] = { "none","bounce","grow","wiggle" };
static const char* LBGFX_NAMES[]      = { "none","nebula","particles","aurora" };
static const char* LTRAY_NAMES[]      = { "off","top","bottom" };
static const char* LLAUNCHFX_NAMES[]  = { "none","ripple","zoom" };
static const char* LSIDE_NAMES[]      = { "left","right" };
static int   g_launLayout=0, g_launShape=0, g_launGlowCol=0, g_launDepth=0, g_launSearchAnim=0, g_launCaret=0, g_launResAnim=0,
             g_launSel=0, g_launIconHover=0, g_launBgFx=0, g_launTray=0, g_launLaunchFx=0, g_launSide=0, g_launGridCols=6;
static bool  g_launGlow=false, g_launGlowPulse=true, g_launSmear=false, g_launSparks=false, g_launTrayLabels=true;
static float g_launGlowStr=0.7f, g_launTilt=0.45f, g_launIconSize=1.0f;
static std::string g_launGlowHex="#9ecbff";
static std::vector<std::string> g_launAliases;    // launcher.keywords: "keyword|hotkey|path"
static bool g_wallDots=true;                      // wallpaper.accent_dots
static const char* LOCKSTYLE_NAMES[] = { "panel","floating","caelestia" };
static const char* LIVETRANS_NAMES[] = { "stripes","fade" };
static int g_liveTransStyle=0;               // wallpaper.live_transition
// ---- recorder + recording checker (src/modules/record/RecordHud.h) ----
static const char* RECHUDPOS_NAMES[] = { "top-left","top-right","bottom-left","bottom-right","left","right","top","bottom" };
static int  g_recFps=60;                     // record.fps
static std::string g_recAudioSys, g_recAudioMic;   // record.audio_system / record.audio_mic (ffmpeg dshow device names)
static bool g_recHud=true;                   // record.hud
static int  g_recHudPos=0;                   // record.hud_position
static bool g_recHudPeekOn=true;             // record.hud_tuck
static int  g_recHudStayMs=4000;             // record.hud_stay_ms
static std::atomic<float>    g_recFpsNow{0};
static std::atomic<uint64_t> g_recBytes{0};
// ---- audio mixer (src/modules/audio/AudioMixer.h) ----
static bool g_mixerInSidebar=true;           // audio.mixer_sidebar
static bool g_mixerInSettings=true;          // audio.mixer_settings
static bool g_sideWifiOn=false;              // the caelestia sidebar is showing the Wi-Fi list
static bool g_reviveFrozen=true;             // windows.revive_frozen_apps
static bool g_openAnim=true;                 // windows.open_animation
// ---- the workspace overview (src/modules/overview/Overview.h) ----
// The CPU/GPU hero cards. "badge" is what Aether has always drawn; "arc" is the reference shell's
// layout, measured off the user's capture: one ~270 deg arc encoding USAGE, the TEMPERATURE large in
// the middle of it with its label beneath, and the usage % outside the arc to the right.
static const char* HEROSTYLE_NAMES[] = { "badge", "arc" };
static int  g_heroStyle=0;                   // dashboard.hero_style
static const char* OVSTYLE_NAMES[] = { "cube", "plane" };
static bool g_ovEnable=true;                 // overview.enabled
static bool g_ovSuperTab=true;               // overview.super_tab: take Super+Tab from Task View
static int  g_ovStyle=0;                     // overview.style: 0 cube, 1 flat plane
static RECT g_recHudRect={0,0,0,0};          // the recording checker's hit rect (bar window, logical px)
static bool g_sideMixerOn=false;             // the sidebar is showing the mixer instead of notifications
static int  g_amView=0;                      // mixer view: 0 outputs 1 inputs 2 apps 3 voicemeeter
static std::mutex g_recDevMtx; static std::vector<std::string> g_recAudioDevs; static std::atomic<int> g_recDevState{0};   // ffmpeg dshow audio devices: 0 unknown 1 listing 2 done
static void V2RecToggle();                   // fwd (SidebarV2.h)
static void RecListAudioDevices();           // fwd (RecordHud.h)
// ---- stripes cover look (src/components/Stripes.h) ----
static const char* STRIPEDIR_NAMES[] = { "right","left" };
static std::string g_stripeText="Loading!!!!!!";                 // stripes.text
static std::string g_stripeColors="primary, secondary, tertiary"; // stripes.colors
static std::string g_stripeTextColor="white";                     // stripes.text_color
static std::string g_stripeShape="cookie9";                       // stripes.shape
static int   g_stripeCount=9;                                     // stripes.count
static float g_stripeSlant=0.45f;                                 // stripes.angle
static float g_stripeTextSize=72.0f;                              // stripes.text_size
static float g_stripeColorSpeed=0.55f;                            // stripes.color_speed
static bool  g_stripeBounce=true, g_stripeEdge=true, g_stripeLiveWallColors=true;   // stripes.bounce / edge / wallpaper_colors
static int   g_stripeDir=0;                                       // stripes.direction
// ---- dashboard terminal (src/modules/dashboard/Terminal.h) ----
static std::string g_termShell;              // terminal.shell
static float g_termFont=15.0f;               // terminal.font_size
static bool  g_termFocus=false;
// ---- Caelestia settings skin ----
static const char* SETSTYLE_NAMES[] = { "classic","caelestia" };
static int  g_setStyle=0;                          // settings.style
static char g_setSearch[64]={0}; static bool g_setSearchFocus=false;
static const char* SETTRANS_NAMES[] = { "stripes","fade","none" };
static int  g_setTransition=0;                     // settings.page_transition
static int  g_setTransMs=1100;                     // settings.transition_ms
static bool g_setAboutCard=true;                   // settings.about_card
static bool g_tourSeen=false;                      // settings.tour_seen: the first Settings open shows the Aether page + tour
static std::string g_setHilite; static ULONGLONG g_setHiliteAt=0; static bool g_setHiliteScrolled=false;   // search jump-to
static int   g_setPagePending=-1;                  // page waiting for the stripes to cover the window
static float g_setScrollKeep=0, g_setScrollTKeep=0; // last frame's scroll (a click resets it before the switch is held)
static bool g_bv2Apps=true;               // bar.caelestia_apps
static bool g_bv2Logo=true, g_bv2Ws=true, g_bv2Win=true, g_bv2Tray=true, g_bv2Clock=true, g_bv2Status=true, g_bv2Power=true;   // bar.caelestia_*         // bar calendar button pops the drawer open for a few seconds
static std::string ProfileOsName(); static std::string ProfileUptimeLong(); static std::string ProfileDisplayName();
static std::string ProfileWmName(); static int ProfilePresenceNow(); static const char* ProfilePresenceLabel(int);
static int   g_mpStyle[MP_COUNT]={0};           // motion.<panel>_style (0 = follow the global style)
static int   g_mpMs[MP_COUNT]={0};              // motion.<panel>_ms    (0 = follow the global duration)
// ---- a graph per KEYBIND --------------------------------------------------------------------
// Panels already carry their own curve and duration. A keybind is the other axis: the same panel
// can be asked to appear by a key, by a click on the bar, or by another panel handing over, and
// those do not have to move the same way - the key is a deliberate, aimed gesture and usually
// wants to be quicker than a mouse wandering onto a hover target. So each hotkey gets its own
// graph, which OVERRIDES the panel's for the motion that key sets off.
// 0 = follow the panel (which itself may follow the global style), exactly like g_mpStyle.
static int   g_hkStyle[HK_COUNT]={0};           // motion.key_<id>_style
static int   g_hkMs[HK_COUNT]={0};              // motion.key_<id>_ms
// Which panel each hotkey drives, so a key's graph is applied ONLY to the motion it causes and
// never to whatever else happens to animate at the same time. -1 = drives no panel motion.
static const int HK_PANEL[HK_COUNT] = {
    /*quit*/        -1,
    /*launcher*/    MP_LAUNCHER,
    /*wallPrev*/    -1,
    /*wallNext*/    -1,
    /*snip*/        -1,
    /*clipboard*/   MP_LAUNCHER,   // the clipboard is the launcher in mode 4
    /*wallPick*/    MP_PICKER,
    /*overview*/    MP_OVERVIEW,
};
// The keybind whose graph is in force, and when it fired. Cleared by time rather than by the
// panel closing: a panel has no single "done" moment (it can be reopened mid-close), and two
// seconds is far longer than any open animation but far shorter than a person's next deliberate
// gesture, so a key can never colour motion it did not cause.
static int       g_mgKey=-1;
static ULONGLONG g_mgKeyAt=0;
static void MgKeyFire(int hk){ g_mgKey=hk; g_mgKeyAt=GetTickCount64(); }
enum { MDSTYLE_CIRCLE=0, MDSTYLE_SHAPED };

#define S_B(p,v,d)              { p, SK_BOOL,  &v, 0,0, nullptr,0, d }
#define S_I(p,v,lo,hi,d)        { p, SK_INT,   &v, lo,hi, nullptr,0, d }
#define S_F(p,v,lo,hi,d)        { p, SK_FLOAT, &v, lo,hi, nullptr,0, d }
#define S_S(p,v,d)              { p, SK_STR,   &v, 0,0, nullptr,0, d }
#define S_E(p,v,n,d)            { p, SK_ENUM,  &v, 0,0, n, (int)(sizeof(n)/sizeof(n[0])), d }
#define S_L(p,v,d)              { p, SK_STRLIST,&v,0,0, nullptr,0, d }

static const Setting SETTINGS[] = {
// ---- motion --------------------------------------------------------------------------------------
S_E("motion.style",             g_motionStyle, MOTION_NAMES, "How panels move when they open. \"expressive\" is Caelestia's: it springs slightly past its resting place and settles. \"smooth\" glides in with no overshoot, \"bounce\" wobbles, \"instant\" skips the animation, \"custom\" uses custom_curve."),
S_I("motion.duration_ms",       g_motionMs, 0,3000, "Length of an open animation in milliseconds. 0 = the style's own (expressive 500, fast 350, slow 650, emphasized 400, smooth 400, standard 300, linear 250, bounce 700)."),
S_E("motion.close_style",       g_motionCloseStyle, MOTION_CLOSE_NAMES, "How panels move when they close. \"same\" plays the opening motion backwards, like Caelestia."),
S_I("motion.close_duration_ms", g_motionCloseMs, 0,3000, "Length of a close animation in milliseconds. 0 = same as opening."),
S_I("motion.strive_fps",        Cael::g_striveFps, 0,60, "Frame rate the \"strive\" style steps at, like Guilty Gear's hand-drawn-looking animation (Arc System Works animates at ~12-20 fps). 0 = smooth."),
S_B("strive.impact",            g_stvImpact, "Guilty Gear-style impact frames when a panel opens: a one-frame flash, a slash across the screen and a short shake."),
S_F("strive.impact_strength",   g_stvImpactStrength, 0.2f,2.0f, "How hard the impact frames hit: flash brightness, slash width and shake distance. 1 = default."),
S_B("strive.workspace_banner",  g_stvBanner, "Switching workspaces slams a huge slanted banner across the screen, like a fighting game's round call."),
S_S("strive.banner_text",       g_stvBannerText, "Banner text. %d = workspace number, %s = workspace name."),
S_B("strive.intro_startup",     g_stvIntroStart, "Play a fight-opening intro when Aether starts. Any key or click skips it."),
S_B("strive.intro_unlock",      g_stvIntroUnlock, "Play the fight-opening intro after you unlock. Any key or click skips it."),
S_S("strive.intro_lines",       g_stvIntroLines, "The intro's beats, separated by |. %n = how many intros this session (DUEL 1, DUEL 2...), %u = your name."),
S_B("motion.overshoot",         g_motionOvershoot, "Let springy styles travel past their resting size before settling. Off keeps every panel inside its final bounds."),
S_S("motion.custom_curve",      g_motionCustom, "Cubic-bezier control points \"x1, y1, x2, y2\" for style = \"custom\" (same numbers as CSS cubic-bezier()). y above 1 overshoots. Caelestia's is 0.38, 1.21, 0.22, 1.0."),
S_B("notifications.flick_dismiss",       g_nfFlick,  "Drag a notification sideways and let go to throw it away (Caelestia's flick)."),
S_B("notifications.middle_click_dismiss", g_nfMiddle, "Middle-click a notification to dismiss it."),
S_E("notifications.dismiss_animation",   g_nfDismissAnim, NF_DISMISS_NAMES, "How a dismissed notification leaves: \"slide\" throws it off the side it was dragged toward, \"fade\" dissolves it, \"shrink\" scales it away."),
S_I("notifications.dismiss_ms",          g_nfDismissMs, 60,1500, "How long the dismiss animation takes, in milliseconds."),
S_E("notifications.arrive_animation",    g_nfArriveAnim, NF_ARRIVE_NAMES, "How a new notification appears: \"slide\" in from the right, \"fade\", \"pop\" (grows in) or \"none\"."),
S_I("notifications.arrive_ms",           g_nfArriveMs, 60,1500, "How long a new notification takes to arrive, in milliseconds."),
S_B("notifications.expandable",          g_nfExpandable, "Show a chevron on each notification that opens it up to the full message (and the older ones from the same app)."),
S_B("notifications.expand_by_default",   g_nfExpandDefault, "Show notifications opened up by default."),
S_E("launcher.switch_animation", g_launSwitchAnim, LSWITCH_NAMES, "What the result list does when it changes (typing, or \">\" switching to commands): \"fade\" fades the new results in, \"slide\" fades them in rising into place, \"none\" swaps instantly."),
S_I("launcher.switch_ms",        g_launSwitchMs, 0,1000, "How long the result-list switch takes, in milliseconds."),
S_B("launcher.resize_animation", g_launResize, "The launcher grows and shrinks smoothly to fit its results (using the launcher's motion) instead of jumping."),
S_B("wallpaper.preview_images",  g_wallPreviewImages, "Scrolling the image wallpaper picker applies each wallpaper as you land on it, and the whole shell recolours to match. Enter keeps it; Esc puts back what you had."),
S_S("profile.name",               g_profileName, "Name on the Profile widget. Empty = your Windows user name."),
S_S("profile.status",             g_status, "Your custom status. Click it on the Profile widget to change it, or: Aether.exe -s status=<text>."),
S_S("profile.status_icon",        g_statusIcon, "Material Symbols icon shown in front of the status (mood, sports_esports, music_note, code, bedtime...)."),
S_E("profile.presence",           g_presence, PRESENCE_NAMES, "The dot on your avatar: \"online\", \"idle\", \"dnd\" (do not disturb), \"invisible\" or \"none\" (no dot)."),
S_I("profile.status_clear_after", g_statusClearMin, -1,10080, "Clear the status by itself after this many minutes. 0 = never, -1 = at the end of the day."),
S_I("profile.status_set_at",      g_statusSetAt, 0,2147483647, "When the status was set (unix seconds) - used by status_clear_after. Written by Aether."),
S_S("profile.avatar",             g_avatarImage, "Picture for the avatar (PNG, JPG or an animated GIF). Empty = your Windows account picture."),
S_S("profile.avatar_shape",       g_avatarShape, "Avatar silhouette: circle, or any Material 3 shape (cookie12, clover4, flower, heart, pixel_circle, sunny, gem...)."),
S_B("profile.avatar_ring",        g_avatarRing, "A softly pulsing accent ring around the avatar."),
S_B("profile.show_name",          g_profileShowName, "Show your name above the status."),
S_B("profile.presence_sets_dnd",  g_presenceDndLink, "Presence and do-not-disturb are one switch: \"dnd\" silences notifications, and turning DND on in the bar shows the red dot."),
S_S("profile.rows",               g_profileRows, "Info rows under the status, Caelestia-style \"icon : value\". Rows are separated by ;  each is icon|text or icon|text|colour. The icon is a Material Symbols name, or a real picture: os (the Windows logo), logo:arch / logo:cachyos / logo:nixos... (assets\\logos), exe:discord (that app's own icon), or a path to a .png/.ico/.gif. Text can use any widget binding: {os} {wm} {uptime_long} {uptime} {host} {user} {cpu%} {mem%} {gpu%} {battery%} {media.title} {media.artist} {weather.temp} {weather.text} {time} {date}. Colours: primary, secondary, tertiary, #hex."),
S_I("wallpaper.preview_delay_ms", g_wallPreviewDelay, 0,3000, "How long to rest on a wallpaper before it is previewed, in milliseconds (so fast scrolling does not apply every one you pass)."),
S_E("motion.dashboard_style",   g_mpStyle[MP_DASHBOARD], MOTION_PANEL_NAMES, "Dashboard motion. \"global\" follows motion.style."),
S_I("motion.dashboard_ms",      g_mpMs[MP_DASHBOARD], 0,3000, "Dashboard duration in ms. 0 = follow motion.duration_ms."),
S_E("motion.launcher_style",    g_mpStyle[MP_LAUNCHER], MOTION_PANEL_NAMES, "Launcher motion (when it comes out of the border)."),
S_I("motion.launcher_ms",       g_mpMs[MP_LAUNCHER], 0,3000, "Launcher duration in ms. 0 = global."),
S_E("motion.picker_style",      g_mpStyle[MP_PICKER], MOTION_PANEL_NAMES, "Wallpaper and colour-scheme picker motion."),
S_I("motion.picker_ms",         g_mpMs[MP_PICKER], 0,3000, "Picker duration in ms. 0 = global."),
S_E("motion.notifications_style", g_mpStyle[MP_NOTIF], MOTION_PANEL_NAMES, "Notification panel motion."),
S_I("motion.notifications_ms",  g_mpMs[MP_NOTIF], 0,3000, "Notification panel duration in ms. 0 = global."),
S_E("motion.quick_settings_style", g_mpStyle[MP_QS], MOTION_PANEL_NAMES, "Quick settings motion."),
S_I("motion.quick_settings_ms", g_mpMs[MP_QS], 0,3000, "Quick settings duration in ms. 0 = global."),
S_E("motion.osd_style",         g_mpStyle[MP_OSD], MOTION_PANEL_NAMES, "Volume / brightness OSD motion."),
S_I("motion.osd_ms",            g_mpMs[MP_OSD], 0,3000, "OSD duration in ms. 0 = global."),
S_E("motion.session_style",     g_mpStyle[MP_SESSION], MOTION_PANEL_NAMES, "Power menu motion."),
S_I("motion.session_ms",        g_mpMs[MP_SESSION], 0,3000, "Power menu duration in ms. 0 = global."),
S_E("motion.popouts_style",     g_mpStyle[MP_POPOUT], MOTION_PANEL_NAMES, "Bar popouts (tray menus, wifi, window previews): how they grow out of the bar and reshape between items."),
S_I("motion.popouts_ms",        g_mpMs[MP_POPOUT], 0,3000, "Bar popout duration in ms. 0 = global."),
S_E("motion.toasts_style",      g_mpStyle[MP_TOAST], MOTION_PANEL_NAMES, "Now Playing toast motion."),
S_I("motion.toasts_ms",         g_mpMs[MP_TOAST], 0,3000, "Now Playing toast duration in ms. 0 = global."),
S_E("motion.settings_style",    g_mpStyle[MP_SETTINGS], MOTION_PANEL_NAMES, "Settings window motion."),
S_I("motion.settings_ms",       g_mpMs[MP_SETTINGS], 0,3000, "Settings window duration in ms. 0 = global."),
S_B("windows.tile_animation",   g_tileAnim, "Glide windows to their new place when komorebi tiles, swaps or moves them. Aether does this instead of komorebi's own animation, which resizes windows every frame and leaves Chromium apps (Discord, browsers, Spotify) stuck grey. Keep komorebi's animation off."),
S_E("motion.tiling_style",      g_mpStyle[MP_TILE], MOTION_PANEL_NAMES, "How windows glide when komorebi re-tiles them. \"global\" follows motion.style."),
S_I("motion.tiling_ms",         g_mpMs[MP_TILE], 0,3000, "How long a window takes to glide to its new tile, in ms. 0 = global."),
S_E("motion.overview_style",    g_mpStyle[MP_OVERVIEW], MOTION_PANEL_NAMES, "Workspace overview motion - how the desktop pulls back and comes home again."),
S_I("motion.overview_ms",       g_mpMs[MP_OVERVIEW], 0,3000, "Overview duration in ms. 0 = global."),
S_B("motion.speed_up",          Cael::g_speedUp, "Press a key again while its animation is still running and the animation SPEEDS UP to finish, instead of being ignored or restarting at full length. Flick through five workspaces and you get five animations, each quicker than the last. Turn this off to give every animation its full duration no matter how fast the input arrives."),
S_F("motion.speed_up_floor",    Cael::g_speedUpFloor, 0.02,1.0, "How far speed_up is allowed to compress an animation, as a fraction of its normal length. 0.25 = a hurried animation still takes a quarter of its usual time, so it stays a movement rather than a teleport. Lower = snappier under fast input."),
// ---- a graph per keybind: overrides the panel's, for the motion that key sets off ------------
S_E("motion.key_launcher_style",  g_hkStyle[HK_LAUNCHER], MOTION_PANEL_NAMES, "Motion for the launcher when it is opened by its KEYBIND, as opposed to by a click. \"global\" follows motion.launcher_style."),
S_I("motion.key_launcher_ms",     g_hkMs[HK_LAUNCHER], 0,3000, "Duration for the launcher's keybind motion in ms. 0 = follow motion.launcher_ms."),
S_E("motion.key_clipboard_style", g_hkStyle[HK_CLIP], MOTION_PANEL_NAMES, "Motion for the clipboard history when opened by its keybind."),
S_I("motion.key_clipboard_ms",    g_hkMs[HK_CLIP], 0,3000, "Duration for the clipboard keybind motion in ms. 0 = global."),
S_E("motion.key_wallpaper_style", g_hkStyle[HK_WALLPICK], MOTION_PANEL_NAMES, "Motion for the wallpaper picker when opened by its keybind."),
S_I("motion.key_wallpaper_ms",    g_hkMs[HK_WALLPICK], 0,3000, "Duration for the wallpaper picker keybind motion in ms. 0 = global."),
S_E("motion.key_overview_style",  g_hkStyle[HK_OVERVIEW], MOTION_PANEL_NAMES, "Motion for the workspace overview when opened by its keybind."),
S_I("motion.key_overview_ms",     g_hkMs[HK_OVERVIEW], 0,3000, "Duration for the overview keybind motion in ms. 0 = global."),
// ---- appearance ------------------------------------------------------------------------------
S_F("appearance.rounding",   g_rounding,   0,1.5,  "Corner-roundness multiplier applied across the shell. 0 = square, 1 = the design's own radii."),
S_F("appearance.ui_scale",   g_uiScale,    0.75,2, "Size of everything the shell draws. 1 = native pixels."),
S_F("appearance.text_scale", g_textScale,  0.7,1.4,"Type size, independent of ui_scale."),
S_B("appearance.dynamic_color", g_dynamicColor, "Pull the accent out of the current wallpaper (Material You)."),
S_B("appearance.custom_accent", g_customAccent, "Use accent = [r,g,b] below instead of the colour scheme's own."),
S_E("appearance.icon_set",   g_iconSet, ICONSET_NAMES, "Which icon family the shell draws with."),
S_B("appearance.mica",       g_micaMode,   "Give ordinary app windows a Mica/Acrylic backdrop."),
// ---- windows -----------------------------------------------------------------------------------
S_B("windows.round_corners", g_roundWindows, "Round app-window corners using DWM. Smooth, antialiased, and it never clips the window."),
S_B("windows.deep_corners",  g_deepCorners,  "Deeper corners than DWM allows, by clipping each window to a rounded region. A region has no antialiasing and clips what it covers, so a window can show cut-off edges while it is being resized."),
S_I("appearance.mica_kind",  g_micaKind, 0,4, "2 = Mica, 3 = Acrylic, 4 = Tabbed."),
// ---- desktop ---------------------------------------------------------------------------------
S_B("desktop.bubble",        g_bubble,     "Draw the wallpaper as a rounded pane inset from the screen edges."),
S_I("desktop.gap",           g_gap, 0,40,  "Margin between the screen edge and the bubble, in px."),
S_F("desktop.radius",        g_bubbleRound,0,44,"Corner radius of the bubble."),
S_B("desktop.live_wallpaper",g_deskLive,   "Show the real animated wallpaper through the bubble instead of a still snapshot."),
S_B("appearance.frame_born_panels", g_frameBorn, "Panels on the top edge grow out of the screen border like Caelestia - same material, concave shoulders, content revealed from the border - instead of floating in as cards."),
S_F("desktop.live_frame_opacity", g_liveFrameAlpha, 0,1, "How solid the bubble frame is over a live wallpaper (Wallpaper Engine). Lower lets the animation move in the frame too; 1 = solid."),
S_B("desktop.confine_apps",  g_confineApps,"Keep app windows inside the bubble: maximized windows fill it, strays are pulled back in."),
S_B("desktop.host_icons",    g_hostDesktop,"Run Explorer as a desktop host underneath (experimental). On Windows 11 24H2 its desktop draws black with no icons when Aether is the shell, so leave this off."),
S_S("desktop.widget_tab",    g_deskTabName,"Name of the dashboard tab mirrored onto the wallpaper. Empty = none."),
// ---- bar -------------------------------------------------------------------------------------
S_B("bar.visible",           g_pn[PN_BAR].visible, "Draw the bar at all."),
S_E("bar.edge",              g_pn[PN_BAR].edge, EDGE_NAMES, "Which screen edge the bar sits on."),
S_F("bar.size",              g_pn[PN_BAR].size, 28,160, "Thickness of the bar in px (its width when vertical, height when horizontal)."),
S_F("bar.margin",            g_pn[PN_BAR].gap,  0,60,   "Gap between the bar and the screen edge. The bubble's inset on this edge is derived from it."),
S_F("bar.anchor",            g_pn[PN_BAR].anchor,0,1,   "Position along the edge: 0 = start, 0.5 = centre, 1 = end."),
S_S("bar.logo_image",        g_barLogoImage, "An image of your own for the bar logo (PNG, JPG, GIF, BMP or ICO). Used when the logo is set to \"Your own image\" in Settings > Taskbar."),
S_B("bar.custom_colours",    g_barTheme, "Give the bar its own colours instead of the ones the colour scheme picks. The three keys below take effect only while this is on; anything left empty still follows the scheme."),
S_S("bar.colour_panel",      g_barColPanel,  "The bar's background, as #rrggbb. Its card and track shades are derived from it. Empty = follow the scheme."),
S_S("bar.colour_ink",        g_barColInk,    "The bar's text and icon colour, as #rrggbb. Empty = follow the scheme."),
S_S("bar.colour_accent",     g_barColAccent, "The bar's accent (the active workspace, highlights), as #rrggbb. Empty = follow the scheme."),
S_F("bar.span",              g_pn[PN_BAR].span, 0.1,1,  "Length as a fraction of the edge."),
S_B("bar.autohide",          g_barAutoHide,"Hide the bar until the pointer touches its edge."),
S_B("bar.hide_taskbar",      g_hideTaskbar,"Hide Explorer's taskbar and take over the work area."),
S_L("bar.items",             g_barItemsToml,"What the bar shows, top to bottom (left to right on a horizontal bar). Reorder the words to move things; put ! in front to hide one.   Words: logo files workspaces windowinfo apps spacer1 spacer2 spacer3 plugins tray calendar clock notif ethernet wifi bluetooth theme mic audio battery power"),
// ---- launcher --------------------------------------------------------------------------------
S_E("launcher.position",     g_launPos, LPOS_NAMES, "Where the launcher panel sits on screen."),
S_E("launcher.style",        g_launStyle, LAUNSTYLE_NAMES, "\"classic\": Aether's result rows. \"caelestia\": the newer rice - quiet selection, a heart (favourite, pinned to the top) and an eye (hide) on every app, icons on the > commands, a pill search bar."),
S_L("launcher.favourites",   g_launFavs, "Apps pinned to the top of the launcher (their paths). The heart button adds and removes them."),
S_L("launcher.hidden",       g_launHidden, "Apps the launcher never lists (their paths). The eye button adds them; the >unhide command clears the list."),
S_B("wallpaper.accent_dots", g_wallDots, "Caelestia launcher: a row of colour dots under the wallpaper picker - click one to use it as the accent colour (the ring = back to the wallpaper's own)."),
S_E("launcher.layout",       g_launLayout, LLAYOUT_NAMES, "\"list\" (rows), \"grid\" (icon tiles), \"horizontal\" (a strip of tiles), \"vertical\" (a tall column on a screen edge) or \"radial\" (apps on a ring around the search)."),
S_I("launcher.grid_columns", g_launGridCols, 3,10, "Columns in the grid layout."),
S_E("launcher.vertical_side",g_launSide, LSIDE_NAMES, "Which screen edge the vertical layout sits on."),
S_F("launcher.icon_size",    g_launIconSize, 0.6,1.8, "Icon / tile size for the grid, horizontal, vertical and radial layouts."),
S_E("launcher.shape",        g_launShape, LSHAPE_NAMES, "Panel outline: rounded, pill, square, chamfer (cut corners), hexagon or slant (a parallelogram)."),
S_B("launcher.glow",         g_launGlow, "A soft glow around the panel."),
S_E("launcher.glow_color",   g_launGlowCol, LGLOWCOL_NAMES, "Glow colour: the accent, an animated rainbow, or launcher.glow_hex."),
S_S("launcher.glow_hex",     g_launGlowHex, "Custom glow colour, #rrggbb."),
S_F("launcher.glow_strength",g_launGlowStr, 0.1,1.5, "How strong the glow is."),
S_B("launcher.glow_pulse",   g_launGlowPulse, "The glow slowly breathes."),
S_E("launcher.depth",        g_launDepth, LDEPTH_NAMES, "3D: \"tilt\" turns the panel towards the pointer and tumbles it in, \"extrude\" gives it a solid slab underneath."),
S_F("launcher.tilt_amount",  g_launTilt, 0,1, "How far the tilt turns."),
S_E("launcher.background_fx",g_launBgFx, LBGFX_NAMES, "An effect drawn inside the panel: nebula, drifting particles or aurora."),
S_E("launcher.letter_animation", g_launSearchAnim, LSEARCHANIM_NAMES, "How each typed letter appears: pop, wave, bounce, glitch, rainbow or typewriter."),
S_E("launcher.caret",        g_launCaret, LCARET_NAMES, "Search caret: bar, block or underline."),
S_B("launcher.caret_smear",  g_launSmear, "The caret stretches and leaves a smear trail when it moves."),
S_B("launcher.type_sparks",  g_launSparks, "Sparks fly off each new letter."),
S_E("launcher.result_animation", g_launResAnim, LRESANIM_NAMES, "How results appear: cascade, fade, scale, slide, flip or none."),
S_E("launcher.selection",    g_launSel, LSEL_NAMES, "Selection look: fill, glide (a highlight that slides between results), outline, accent bar or glow."),
S_E("launcher.icon_hover",   g_launIconHover, LICONHOVER_NAMES, "What an app icon does under the pointer."),
S_E("launcher.launch_effect",g_launLaunchFx, LLAUNCHFX_NAMES, "An effect when something is opened: ripple or zoom."),
S_E("launcher.tray",         g_launTray, LTRAY_NAMES, "The favourites tray: off, above or below the results."),
S_B("launcher.tray_labels",  g_launTrayLabels, "Names under the icons in the favourites tray."),
S_L("launcher.keywords",     g_launAliases, "Keywords (and optional global hotkeys) that open an app: \"keyword|Ctrl+Alt+F|C:\\\\path\\\\app.exe\"."),
S_F("launcher.width",        g_launWidth, 360,1400, "Panel width in px."),
S_F("launcher.radius",       g_launRound, 0,48,     "Panel corner radius."),
S_F("launcher.opacity",      g_launOpacity, 0.1,1,  "Panel fill opacity. Lower lets more of the frosted backdrop through."),
S_B("launcher.blur",         g_launBlur,   "Frost whatever is behind the panel."),
S_E("launcher.animation",    g_launAnimStyle, LANIM_NAMES, "How the panel arrives and leaves."),
S_I("launcher.open_ms",      g_launOpenMs, 40,2000, "Opening duration in milliseconds."),
S_I("launcher.close_ms",     g_launCloseMs,40,2000, "Closing duration in milliseconds."),
S_B("launcher.stagger",      g_launStagger,"Cascade the result rows in one after another."),
S_B("launcher.super_key",    g_winKeyLauncher, "Tapping Super opens the launcher."),
// ---- wallpaper -------------------------------------------------------------------------------
S_L("wallpaper.folders",     g_wallFolders,"Folders the picker scans. Add your own; order does not matter."),
S_B("wallpaper.recursive",   g_wallRecursive, "Include sub-folders."),
S_B("wallpaper.live_preview", g_liveBrowsePreview, "Scrolling the live-wallpaper picker plays each wallpaper on your screen as you pass it. Enter keeps it, Esc puts back what you had."),
S_E("wallpaper.picker_source", g_wallSource, WSRC_NAMES, "What the picker lists: \"images\" (your wallpaper folders) or \"live\" (your Wallpaper Engine wallpapers). Tab switches while the picker is open. Picking a live one plays the transition on that screen, then the live wallpaper takes over."),
S_B("wallpaper.bundled",     g_wallBundled,   "Include the three CachyOS wallpapers that ship with Aether. Off, they only appear when no folders are set."),
S_B("wallpaper.show_name",   g_wallShowName,  "Print the filename under the centred wallpaper."),
S_B("wallpaper.show_ext",    g_wallShowExt,   "Show the file-type badge."),
S_E("wallpaper.picker_position", g_wallPos, LPOS_NAMES, "Where the picker sits on screen."),
S_F("wallpaper.picker_scale",g_wallScale, 0.6,2.2, "Size of the picker and its cards together. Turn it up to fill a big screen."),
S_F("wallpaper.picker_radius",g_wallRound, 0,48,   "Picker corner radius."),
S_F("wallpaper.picker_opacity",g_wallOpacity,0.1,1,"Picker background opacity."),
S_B("wallpaper.picker_blur", g_wallBlur,      "Frost behind the picker panel."),
S_E("wallpaper.picker_animation", g_wallAnim, LANIM_NAMES, "How the picker arrives."),
S_B("wallpaper.picker_immersive", g_wallImmersive, "Hide the panel, title and footer - only the wallpapers on a blurred desktop."),
S_B("wallpaper.picker_backdrop",  g_wallBackdrop,  "Ease the whole screen out of focus as the picker opens."),
S_I("wallpaper.card_width",  g_carCW, 120,600, "Width of the centred card."),
S_I("wallpaper.card_height", g_carCH, 80,400,  "Height of the centred card."),
S_I("wallpaper.near_width",  g_carNW, 80,500,  "Width of the cards either side."),
S_I("wallpaper.near_height", g_carNH, 60,340,  "Height of the cards either side."),
S_I("wallpaper.card_gap",    g_carGap, 0,90,   "Space between cards."),
S_I("wallpaper.slideshow_seconds", g_slideSec, 0,86400, "Change wallpaper every N seconds. 0 = never."),
S_B("wallpaper.slideshow_shuffle", g_slideShuffle, "Pick the next wallpaper at random rather than in order."),
S_E("wallpaper.transition",  g_transCfg, WTRANS_NAME, "How a new wallpaper comes in - from the picker AND from the slideshow. any = a different one each time, none = instant."),
S_I("wallpaper.transition_ms", g_transMs, 80,5000, "How long the transition takes, in milliseconds."),
// ---- dashboard -------------------------------------------------------------------------------
S_B("dashboard.clock_24h",   g_clock24,    "24-hour clock everywhere the shell prints a time."),
S_B("media.lyrics",          g_mediaLyrics, "Show synced lyrics beside the player in the dashboard's Media tab (from lrclib.net). The line being sung glows as it plays."),
S_B("media.toast_lyrics",    g_lyricsToast, "The Now Playing toast shows the current lyric line too."),
S_F("media.lyrics_offset",   g_lyricsOffset, -5,5, "Seconds to shift lyric timing by for EVERY song (e.g. audio output latency). One song's own nudge is set with the +/- beside its lyrics."),
S_B("desktop.lyrics",             g_deskLyrics, "Show the current lyric line on the wallpaper while music plays (synced lyrics, same source as the Media tab)."),
S_B("desktop.lyrics_autohide",    g_deskLyricsAutoHide, "Hide the desktop lyrics while a window covers the screen."),
S_F("desktop.lyrics_size",        g_deskLyricsSize, 12,96, "Size of the current lyric line."),
S_F("desktop.lyrics_position",    g_deskLyricsY, 0.05,0.95, "How far down the screen the lyrics sit (0 top, 1 bottom)."),
S_B("desktop.visualiser",         g_deskViz, "Audio bars along the bottom of the wallpaper while music plays."),
S_B("desktop.visualiser_autohide",g_deskVizAutoHide, "Hide the visualiser while a window covers the screen."),
S_B("desktop.visualiser_mirror",  g_deskVizMirror, "Mirror the spectrum so the bass sits at both edges (the reference look). Off: bass on the left."),
S_F("desktop.visualiser_height",  g_deskVizHeight, 0.02,0.9, "Tallest bar, as a fraction of the screen height."),
S_F("desktop.visualiser_bar",     g_deskVizBar, 1,20, "Width of one bar in px."),
S_F("desktop.visualiser_gap",     g_deskVizGap, 0,20, "Space between bars in px."),
S_S("desktop.visualiser_color",   g_deskVizColor, "Bar colour: empty = the text colour, or primary, secondary, tertiary, #rrggbb, #rrggbbaa."),
S_I("desktop.music_fps",          g_deskMusicFps, 10,120, "How often the wallpaper layer redraws for the lyrics / visualiser."),
S_S("terminal.shell",       g_termShell, "Command line the dashboard Terminal runs. Empty = PowerShell 7 if installed, else Windows PowerShell. e.g. \"cmd.exe\", \"wsl.exe\", \"nu.exe\"."),
S_F("terminal.font_size",   g_termFont, 9,28, "Text size in the dashboard Terminal."),
S_S("stripes.text",        g_stripeText, "Text in the middle of the stripes transition (Settings pages and live wallpaper switches). Empty = no text."),
S_S("stripes.colors",      g_stripeColors, "Stripe colours, comma separated, cycled through over time: primary, secondary, tertiary, ink, #rrggbb... "),
S_B("stripes.wallpaper_colors", g_stripeLiveWallColors, "On a live wallpaper switch, colour the stripes from the NEW wallpaper instead of stripes.colors."),
S_S("stripes.text_color",  g_stripeTextColor, "Colour of the stripes text: white, black, ink, primary, #rrggbb..."),
S_F("stripes.text_size",   g_stripeTextSize, 16,160, "Size of the stripes text."),
S_S("stripes.shape",       g_stripeShape, "The spinning shape beside the text: any Material 3 shape name (cookie9, flower, heart, sunny...) or none."),
S_I("stripes.count",       g_stripeCount, 3,30, "How many stripes."),
S_F("stripes.angle",       g_stripeSlant, 0,1.5, "How slanted the stripes are (0 = straight up)."),
S_F("stripes.color_speed", g_stripeColorSpeed, 0,4, "How fast the stripe colours flow through the palette (0 = still)."),
S_B("stripes.bounce",      g_stripeBounce, "Letters of the text bounce in a wave."),
S_B("stripes.edge",        g_stripeEdge, "A thin light edge on every stripe."),
S_E("stripes.direction",   g_stripeDir, STRIPEDIR_NAMES, "Which way the stripes sweep: right or left."),
S_I("record.fps",           g_recFps, 10,240, "Frame rate the screen recorder captures at (ffmpeg modes)."),
S_S("record.audio_system",  g_recAudioSys, "Device to record what you hear from (an ffmpeg dshow audio device: \"Stereo Mix\", a Voicemeeter output...). Empty = no system audio."),
S_S("record.audio_mic",     g_recAudioMic, "Microphone to record (dshow device name). Empty = no microphone."),
S_B("record.hud",           g_recHud, "Show the recording checker: a card that flies in while recording with the time, fps, size, pause / stop and settings."),
S_E("record.hud_position",  g_recHudPos, RECHUDPOS_NAMES, "Where the recording checker flies in from."),
S_B("record.hud_tuck",      g_recHudPeekOn, "After a few seconds the checker tucks into the screen edge as a small tab; touch that edge to bring it back."),
S_I("record.hud_stay_ms",   g_recHudStayMs, 800,60000, "How long the checker stays out before tucking away."),
S_S("audio.visualiser_device", g_vizDevice, "Which playback device the audio visualiser listens to. Empty = automatic: it follows whichever output is actually making noise, which is what you want on a PC with Voicemeeter or a virtual cable, where the \"default\" device is only one of several and is often silent. Otherwise, part of a device name to lock onto (for example \"Voicemeeter Input\" or \"Headset\")."),
S_B("audio.mixer_sidebar",  g_mixerInSidebar, "A mixer button in the caelestia sidebar (the notifications area turns into the audio mixer)."),
S_B("audio.mixer_settings", g_mixerInSettings, "Show the audio mixer at the top of Settings > Audio."),
S_E("dashboard.hero_style", g_heroStyle, HEROSTYLE_NAMES, "Layout of the CPU and GPU cards on the Performance tab. \"badge\": the usage percentage in a cookie-shaped badge with the temperature on a bar underneath. \"arc\": the reference shell's layout - one wide arc showing usage, the temperature large in the middle of it, and the usage percentage outside the arc to the right."),
S_B("overview.enabled",     g_ovEnable, "The workspace overview: a 3-D view of every workspace on the screen you are on, opened with its hotkey. Needs komorebi, and a Windows build that allows screen capture."),
S_B("overview.super_tab",   g_ovSuperTab, "Open the overview with Super+Tab, taking the gesture from Windows' own Task View. Windows will not hand Super+Tab over as an ordinary hotkey, so the shell takes it in the keyboard hook - the same way it takes Alt+Tab for the switcher. Turn this off to leave Task View alone and use the overview's hotkey instead."),
S_E("overview.style",       g_ovStyle, OVSTYLE_NAMES, "How the overview arranges the workspaces. \"cube\": wrapped around a vertical axis like the Compiz desktop cube - drag to spin it. \"plane\": laid out flat in a row - drag to slide along them."),
S_E("windows.tile_style",   g_tileStyle, TILESTYLE_NAMES, "How windows animate when komorebi tiles, swaps or moves them (needs windows.tile_animation). \"preview\": live previews of the windows move and RESIZE on a layer over the screen while the real windows stay put - the same machinery as the workspace slide, and the only one that can animate a resize. \"windows\": the older way, which puts the real window back and slides it to its new place, so its size still snaps."),
S_B("windows.open_animation", g_openAnim, "A new window grows into its tile instead of appearing at full size. Its neighbours already glide as the layout re-flows; this is the window itself. Uses the same previews and the same motion curve as the tiling animation, so it needs windows.tile_animation with the \"preview\" style."),
S_B("windows.revive_frozen_apps", g_reviveFrozen, "When a Chromium / Electron app (Spotify, Discord, browsers, VS Code) comes back from minimized as a flat grey rectangle and stays that way, restart just its graphics helper so it draws again. The app, its tabs and its audio keep running."),
S_E("weather.location",       g_wxSource, WXSRC_NAMES, "Where the weather is for: \"off\" (nothing is looked up), \"auto\" (your approximate location from your IP address, via ip-api.com) or \"city\" (weather.city)."),
S_S("weather.city",           g_wxCity, "The city to show the weather for when weather.location is \"city\"."),
S_I("workspaces.slide_min_ms", g_wsSlideMinMs, 60,400, "How short a workspace slide is allowed to get when you switch quickly. Switches that arrive in a burst shorten the animation down to this instead of being dropped, so flicking through workspaces stays a movement rather than a series of jumps."),
S_E("workspaces.slide_style",  g_wsSlideStyle, WSSLIDESTYLE_NAMES, "How a komorebi workspace switch slides: \"preview\" moves live previews of the windows on a layer over the screen (smooth, never touches the windows); \"windows\" moves the real windows (the older way)."),
S_E("wallpaper.live_transition", g_liveTransStyle, LIVETRANS_NAMES, "Switching a live (Wallpaper Engine) wallpaper: \"stripes\" - stripes in the new wallpaper's colours sweep over the screen with Loading!!!!!! while it loads, then sweep away; \"fade\" - the screen fades to the surround colour and back."),
S_E("lock.style",          g_lockStyle, LOCKSTYLE_NAMES, "Lock screen layout: \"panel\" (one container, three columns), \"floating\" (shape widgets on the blur) or \"caelestia\" (the newer rice: weather + fetch + player | big clock, avatar, password | resource shapes + notifications)."),
S_E("settings.page_transition", g_setTransition, SETTRANS_NAMES, "What switching Settings pages looks like: \"stripes\" (coloured stripes sweep over, Loading!!!!!!, sweep away), \"fade\" or \"none\"."),
S_I("settings.transition_ms",   g_setTransMs, 300,3000, "Length of the stripes transition in milliseconds."),
S_B("settings.tour_seen",       g_tourSeen, "The getting-started tour has been offered. Set false to see it again the next time Settings opens."),
S_B("settings.about_card",      g_setAboutCard, "The Aether / version / Made by Fora card with the nebula background at the top of the Settings sidebar."),
S_E("settings.style",      g_setStyle, SETSTYLE_NAMES, "Settings window look. \"classic\": the rail + a wallpaper card. \"caelestia\": one card, a searchable sidebar of category cards (icon, title, subtitle) and grouped rows with M3 switches."),
S_E("bar.style",           g_barStyle, BARSTYLE_NAMES, "Bar look, on any edge. \"classic\": the s11 Caelestia strip (workspace pill with app icons, tray, stacked clock, tinted status pill). \"caelestia\": the newer rice - hollow workspace dots with a sliding ring, the active window, a tray expander, date pill, status pill (volume, keyboard layout, power profile, notifications) and power."),
S_B("bar.keyboard_layout", g_barV2Kbd,   "Caelestia bar: show the keyboard layout (\"gb\", \"us\") in the status pill. Click for the list, right-click to cycle."),
S_B("bar.power_profile",   g_barV2Power, "Caelestia bar: show the Windows power mode in the status pill. Click for power saver / balanced / performance."),
S_B("bar.caelestia_logo",       g_bv2Logo,   "Caelestia bar component: the logo (click opens the launcher)."),
S_B("bar.caelestia_workspaces", g_bv2Ws,     "Caelestia bar component: workspace dots (click to switch, scroll to step)."),
S_B("bar.caelestia_apps",       g_bv2Apps,   "Caelestia bar component: the running apps (click to focus, right-click for the menu, hover for a preview)."),
S_B("bar.caelestia_window",     g_bv2Win,    "Caelestia bar component: the active window title."),
S_B("bar.caelestia_tray",       g_bv2Tray,   "Caelestia bar component: the tray expander."),
S_B("bar.caelestia_clock",      g_bv2Clock,  "Caelestia bar component: the date / time pill."),
S_B("bar.caelestia_status",     g_bv2Status, "Caelestia bar component: the status pill (volume, layout, power profile, notifications)."),
S_B("bar.caelestia_power",      g_bv2Power,  "Caelestia bar component: the power button."),
S_B("bar.layout_toast",    g_barV2KbdToast, "Show a \"Keyboard layout changed\" toast in the corner when the layout switches."),
S_E("notifications.toast_style", g_toastStyle, TOASTSTYLE_NAMES, "How a new notification pops up while the caelestia sidebar is in use: \"caelestia\" (a card under the bar: icon, title and time, the message, a chevron) or \"classic\" (the notification panel).") ,
S_E("sidebar.style",    g_sideStyle, SIDESTYLE_NAMES, "\"classic\": the notification panel and quick settings are separate panels. \"caelestia\": ONE sidebar on the right - notifications, Keep Awake, Screen Recorder and Quick Toggles, with a column of action buttons."),
S_S("sidebar.toggles",  g_sideToggles, "Quick Toggles in the caelestia sidebar, in order: wifi, bluetooth, mic, settings, gamemode (DND + keep awake), dnd, keepawake, theme, record, lock, vpn, volume. Right-click wifi / bluetooth / mic for the detailed list."),
S_S("sidebar.actions",  g_sideActions, "Buttons down the left of the caelestia sidebar: logout (click twice), power, avatar, updates, reload, lock, settings, sleep."),
S_S("sidebar.avatar",   g_sideAvatar, "Picture or animated GIF for the avatar button in the sidebar. Empty = your profile picture."),
S_F("sidebar.opacity",  g_sideOpacity, 0,1, "How solid the caelestia sidebar is over your windows (1 = opaque, like the reference)."),
S_I("sidebar.width",    g_sideWidth, 300,900, "Width of the caelestia sidebar in screen pixels (425 = the reference)."),
S_E("sidebar.recorder", g_recMode, RECMODE_NAMES, "Screen Recorder: \"fullscreen\" (the screen under the cursor) and \"all\" (every screen) record with ffmpeg when installed - pausable, saved to Videos\\Captures; \"gamebar\" uses Windows' own capture of the focused window."),
S_E("dashboard.home_layout",        g_homeLayout,  V2LAYOUT_NAMES, "The Dashboard tab: \"classic\" (Aether's page) or \"caelestia\" (weather, user card with gem logo + bubble + uptime, vertical clock, calendar, resource rings, media card)."),
S_E("dashboard.media_layout",       g_mediaLayout, V2LAYOUT_NAMES, "The Media tab: \"classic\" or \"caelestia\" (dotted visualiser ring, wavy slider, five controls, lyrics card with a player picker)."),
S_E("dashboard.performance_layout", g_perfLayout,  V2LAYOUT_NAMES, "The Performance tab: \"classic\" or \"caelestia\" (CPU/GPU cards with cookie usage badges, storage ring with a drive picker, network graph, memory ring)."),
S_S("dashboard.today_shape",        g_v2TodayShape, "Shape behind today's date on the caelestia calendar: any Material 3 shape (cookie9, sunny, circle, clover4, flower...)."),
S_S("profile.bubble",               g_v2Bubble, "What the speech bubble on the caelestia user card says. Any binding: {wm}, {status}, {os}, {media.title}... Empty = your status."),
S_S("profile.logo",                 g_v2Logo, "Logo in the gem on the caelestia user card: os (Windows), logo:arch / logo:cachyos ..., an image path, or a Material Symbols name."),
S_E("dashboard.tab_style",   g_tabStyle, TABSTYLE_NAMES, "Tab bar look. \"caelestia\": icon over label with a sliding indicator. \"pills\": a row of pill buttons. \"segmented\": one connected segmented button. \"minimal\": labels only."),
S_E("dashboard.tab_indicator", g_tabIndicator, TABIND_NAMES, "The mark under the current tab (caelestia and minimal styles): underline, pill, dot or none."),
S_B("dashboard.tab_icons",   g_tabIcons,   "Show an icon on each tab. Set a tab's icon with icon_name (any Material Symbols name) in Settings > Dashboard."),
S_B("dashboard.tab_labels",  g_tabLabels,  "Show each tab's name."),
S_B("dashboard.tab_separator", g_tabSeparator, "A thin line under the tab bar."),
S_B("dashboard.tab_fill",    g_tabFill,    "Tabs share the full width equally (Caelestia). Off: tabs are only as wide as they need, centred."),
S_B("dashboard.tab_scroll",  g_tabWheel,   "Scroll the mouse wheel over the tab bar to change tab."),
S_E("media.lyrics_layout",   g_lyricsLayout, LYRLAYOUT_NAMES, "Where the lyrics go in the dashboard's Media tab: \"beside\" the player, or \"tab\" - Player and Lyrics become two sections you switch between."),
S_S("widgets.python",        g_cwPython, "python.exe used for Python dashboard widgets. Empty = find it on PATH."),
S_B("dashboard.media_rays",  g_mdRays,     "The 64 audio bars around the album art. Off leaves the cover and the progress arc, as in the reference."),
S_E("dashboard.media_style", g_mdStyle, MDSTYLE_NAMES, "Media card look. \"circle\" is the screen recording: a round cover ringed by reactive bars, no arc. \"shaped\" fills the cover into a Material 3 silhouette with a wavy progress arc over it."),
S_I("dashboard.media_shape", g_mdMediaShape, 0,19, "Silhouette the cover is filled into when media_style = \"shaped\". 16 = Cookie 12."),
S_I("dashboard.gauge_shape", g_m3Shape, 0,19,      "Silhouette the shape gauges fill up."),
S_S("dashboard.media_image", g_catPath,    "GIF or PNG drawn on the media card in place of the drawn cat. Animated GIFs play while music does."),
};
static const int SETTINGS_N = (int)(sizeof(SETTINGS)/sizeof(SETTINGS[0]));

// ---- the loaded config: one Tml::File per source, in the order they take effect ----------------
// Later files win, and whoever won a key OWNS it: that is the file a GUI change is written back to,
// which is what makes "drop in someone's bar.toml" and "move a slider" coexist.
static std::vector<Tml::File>              g_cfgFiles;
static std::map<std::string,int>           g_cfgOwner;   // setting path -> index into g_cfgFiles
static std::string CfgDir(){ return ExeDir()+"config\\"; }

// ---- custom colour schemes: config\schemes\<id>.toml ---------------------------------------------
// Drop a file in, pick it in Settings > Appearance or the launcher's ">scheme" - share it by sharing the
// file. Any key left out is taken from `base` (a built-in or another user scheme), so a scheme can be
// as small as a single accent colour. A file whose name matches a built-in replaces that built-in.
static bool ParseColour(const Tml::File& f,const std::string& key,ImU32& out){
    if(!f.has(key)) return false;
    std::string v=f.str(key,"");
    if(!v.empty() && v[0]=='#' && (v.size()==7 || v.size()==9)){
        unsigned long x=strtoul(v.c_str()+1,nullptr,16);
        int r,g,b,a=255;
        if(v.size()==7){ r=(x>>16)&255; g=(x>>8)&255; b=x&255; }
        else           { r=(x>>24)&255; g=(x>>16)&255; b=(x>>8)&255; a=x&255; }
        out=IM_COL32(r,g,b,a); return true; }
    auto arr=f.array(key);                                   // [r, g, b] also works
    if(arr.size()>=3){ out=IM_COL32(std::clamp(atoi(arr[0].c_str()),0,255),std::clamp(atoi(arr[1].c_str()),0,255),
                                    std::clamp(atoi(arr[2].c_str()),0,255),255); return true; }
    return false;
}
static void WriteExampleScheme(){
    std::string dir=CfgDir()+"schemes\\";
    CreateDirectoryA(CfgDir().c_str(),nullptr); CreateDirectoryA(dir.c_str(),nullptr);
    std::string p=dir+"example-rose.toml";
    if(GetFileAttributesA(p.c_str())!=INVALID_FILE_ATTRIBUTES) return;
    std::ofstream f(p,std::ios::binary); if(!f) return;
    f<<"# ==============================================================================\n"
     <<"# Aether colour scheme. The file name (without .toml) is the scheme's id.\n"
     <<"#\n"
     <<"# Copy this file, rename it, change the colours, then pick it in Settings >\n"
     <<"# Appearance or type >scheme in the launcher. Share a scheme by sharing the file.\n"
     <<"# Colours are \"#rrggbb\" (or \"#rrggbbaa\", or [r, g, b]). Leave a key out and it\n"
     <<"# comes from `base`, so a scheme can be as small as one accent colour.\n"
     <<"# ==============================================================================\n\n"
     <<"[scheme]\n"
     <<"name    = \"Rose\"             # shown in the pickers\n"
     <<"family  = \"rose\"             # light/dark variants of one theme share a family\n"
     <<"dark    = true\n"
     <<"base    = \"catppuccin-mocha\" # any scheme id: fills in whatever is not set here\n\n"
     <<"accent  = \"#f5a0c0\"          # highlights, selection, active workspace\n"
     <<"panel   = \"#1f1a22\"          # panel and drawer background\n"
     <<"card    = \"#18141b\"          # cards inside panels\n"
     <<"card2   = \"#2c2530\"          # raised / secondary cards\n"
     <<"ink     = \"#f3e6ee\"          # main text\n"
     <<"ink2    = \"#b49fb0\"          # secondary text\n"
     <<"track   = \"#3a3140\"          # slider and progress tracks\n"
     <<"desktop = \"#0f0c11\"          # the surround outside the desktop bubble\n";
}
static void LoadUserSchemes(){
    std::string curId = (g_scheme>=0 && g_scheme<NSCHEMES)? g_schemes[g_scheme].id : std::string();
    g_schemes.assign(std::begin(BUILTIN_SCHEMES), std::end(BUILTIN_SCHEMES));
    WriteExampleScheme();
    std::string dir=CfgDir()+"schemes\\";
    WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA((dir+"*.toml").c_str(),&fd);
    if(h==INVALID_HANDLE_VALUE) return;
    std::vector<std::string> files;
    do{ if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) files.push_back(fd.cFileName); } while(FindNextFileA(h,&fd));
    FindClose(h);
    std::sort(files.begin(),files.end());
    for(const std::string& fn:files){
        Tml::File f; if(!f.load(dir+fn)) continue;
        std::string id=fn.substr(0,fn.size()-5);
        // start from base (or the first built-in), then apply what the file sets
        Scheme sc=BUILTIN_SCHEMES[0];
        std::string base=f.str("scheme.base","");
        for(const Scheme& b:g_schemes) if(!base.empty() && base==b.id){ sc=b; break; }
        auto keep=[&](const std::string& v)->const char*{ g_schemeText.push_back(v); return g_schemeText.back().c_str(); };
        sc.id=keep(id);
        sc.name=keep(f.str("scheme.name",id));
        sc.family=keep(f.str("scheme.family",id));
        if(f.has("scheme.dark")) sc.dark=f.boolean("scheme.dark");
        ParseColour(f,"scheme.accent",sc.accent); ParseColour(f,"scheme.panel",sc.panel);
        ParseColour(f,"scheme.card",sc.card);     ParseColour(f,"scheme.card2",sc.card2);
        ParseColour(f,"scheme.ink",sc.ink);       ParseColour(f,"scheme.ink2",sc.ink2);
        ParseColour(f,"scheme.track",sc.track);   ParseColour(f,"scheme.desktop",sc.deskbg);
        bool replaced=false;
        for(Scheme& b:g_schemes) if(id==b.id){ b=sc; replaced=true; break; }
        if(!replaced) g_schemes.push_back(sc);
    }
    // keep the SAME scheme selected across a rescan: indices move when files come and go
    if(!curId.empty()) for(int i=0;i<NSCHEMES;i++) if(curId==g_schemes[i].id){ g_scheme=i; break; }
    if(g_scheme>=NSCHEMES) g_scheme=0;
}
static std::string CfgComponent(const char* path){
    const char* dot=strchr(path,'.');
    return dot? std::string(path,dot-path) : std::string(path);
}
static std::string CfgFileFor(const char* path){ return CfgDir()+CfgComponent(path)+".toml"; }

// format one setting's CURRENT value as TOML text
static std::string CfgFormat(const Setting& s){
    switch(s.kind){
    case SK_BOOL:  return Tml::File::Bool(*(bool*)s.ptr);
    case SK_INT:   return Tml::File::Num(*(int*)s.ptr);
    case SK_FLOAT: return Tml::File::Num(*(float*)s.ptr);
    case SK_STR:   return Tml::File::Quote(*(std::string*)s.ptr);
    case SK_ENUM:  { int v=*(int*)s.ptr;
                     return Tml::File::Quote((v>=0&&v<s.nnames)? s.names[v] : "0"); }
    case SK_STRLIST: return Tml::File::StrArray(*(std::vector<std::string>*)s.ptr);
    }
    return "0";
}
// pull one setting OUT of a parsed file into its variable
static void CfgApply(const Setting& s, const Tml::File& f){
    switch(s.kind){
    case SK_BOOL:  *(bool*)s.ptr = f.boolean(s.path, *(bool*)s.ptr); break;
    case SK_INT:   { double v=f.num(s.path, *(int*)s.ptr);
                     *(int*)s.ptr = (int)std::clamp(v, s.lo, s.hi); } break;
    case SK_FLOAT: { double v=f.num(s.path, *(float*)s.ptr);
                     *(float*)s.ptr = (float)std::clamp(v, s.lo, s.hi); } break;
    case SK_STR:   *(std::string*)s.ptr = f.str(s.path, *(std::string*)s.ptr); break;
    case SK_ENUM:  { std::string w=f.str(s.path,"");
                     if(w.empty()) break;
                     for(int i=0;i<s.nnames;i++) if(_stricmp(w.c_str(),s.names[i])==0){ *(int*)s.ptr=i; return; }
                     // a number is accepted too, so an old value or a quick edit still works
                     char* e=nullptr; long n=strtol(w.c_str(),&e,10);
                     if(e && *e==0 && n>=0 && n<s.nnames) *(int*)s.ptr=(int)n; } break;
    case SK_STRLIST: { if(f.has(s.path)) *(std::vector<std::string>*)s.ptr = f.strArray(s.path); } break;
    }
}

// ---- generate the files from the values the shell is running with -------------------------------
// Written once, when config\ does not exist yet: everything you already had carries over, and from
// then on the files are the record. Each key gets its documentation as a comment above it, so the
// file explains itself without a manual.
static void CfgGenerate(){
    BarItemsToToml();
    CreateDirectoryA(CfgDir().c_str(), nullptr);
    // group the registry by component, preserving declaration order within each
    std::vector<std::string> comps;
    for(int i=0;i<SETTINGS_N;i++){
        std::string c=CfgComponent(SETTINGS[i].path);
        if(std::find(comps.begin(),comps.end(),c)==comps.end()) comps.push_back(c);
    }
    for(const std::string& c:comps){
        // Never REWRITE a file that exists - it is the user's, and running this on every startup is
        // what lets a component added to SETTINGS[] later still get a file.
        //
        // But "skip it entirely" had its own hole: a key added to an EXISTING component never
        // appeared in TOML at all. The file opens by saying "This file IS the setting", so a
        // setting missing from it is a setting nobody finds - motion.speed_up and the per-keybind
        // graphs would have shipped invisible. So an existing file gets the registered keys it is
        // MISSING appended, with their documentation, and everything already in it - values,
        // comments, spacing, ordering - is left exactly alone.
        std::string cp=CfgDir()+c+".toml";
        bool exists = GetFileAttributesA(cp.c_str())!=INVALID_FILE_ATTRIBUTES;
        if(exists){
            std::string txt;
            { std::ifstream in(cp, std::ios::binary);
              if(!in) continue;
              txt.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); }
            // Which keys does it already mention? A key counts as present when a non-comment line
            // starts with it followed by optional spaces and '='. A commented-out key counts as
            // ABSENT on purpose: the header tells people to delete a key to take the default, and
            // commenting one out is the same gesture, so neither comes back on the next run.
            auto present=[&](const char* key)->bool{
                size_t pos=0, klen=strlen(key);
                while(pos<txt.size()){
                    size_t eol=txt.find('\n',pos); if(eol==std::string::npos) eol=txt.size();
                    size_t a=pos; while(a<eol && (txt[a]==' '||txt[a]=='\t')) a++;
                    if(a<eol && txt[a]!='#' && eol-a>=klen && txt.compare(a,klen,key)==0){
                        size_t e=a+klen; while(e<eol && (txt[e]==' '||txt[e]=='\t')) e++;
                        if(e<eol && txt[e]=='=') return true;
                    }
                    pos=eol+1;
                }
                return false;
            };
            std::vector<int> missing;
            for(int i=0;i<SETTINGS_N;i++){
                const Setting& s=SETTINGS[i];
                if(CfgComponent(s.path)!=c) continue;
                const char* key=strchr(s.path,'.'); key = key? key+1 : s.path;
                if(!present(key)) missing.push_back(i);
            }
            if(missing.empty()) continue;
            std::ofstream f(cp, std::ios::binary|std::ios::app);
            if(!f) continue;
            if(!txt.empty() && txt.back()!='\n') f<<"\n";
            f<<"\n# ---- added by a newer build of Aether -----------------------------------\n";
            for(int i:missing){
                const Setting& s=SETTINGS[i];
                const char* key=strchr(s.path,'.'); key = key? key+1 : s.path;
                f<<"\n# "<<s.doc<<"\n";
                if(s.kind==SK_ENUM){
                    f<<"#   one of:";
                    for(int n=0;n<s.nnames;n++) f<<" \""<<s.names[n]<<"\"";
                    f<<"\n";
                } else if((s.kind==SK_INT||s.kind==SK_FLOAT) && s.hi>s.lo){
                    f<<"#   range: "<<Tml::File::Num(s.lo)<<" .. "<<Tml::File::Num(s.hi)<<"\n";
                }
                f<<key<<" = "<<CfgFormat(s)<<"\n";
            }
            continue;
        }
        std::ofstream f(cp, std::ios::binary);
        if(!f) continue;
        f<<"# ==============================================================================\n"
         <<"# Aether - "<<c<<"\n"
         <<"#\n"
         <<"# This file IS the setting. Edit it and the shell picks it up; move a slider in\n"
         <<"# Settings and only the value on that line changes - your comments, spacing and\n"
         <<"# ordering are left alone. Delete a key to fall back to the built-in default.\n"
         <<"# ==============================================================================\n\n"
         <<"["<<c<<"]\n";
        for(int i=0;i<SETTINGS_N;i++){
            const Setting& s=SETTINGS[i];
            if(CfgComponent(s.path)!=c) continue;
            const char* key=strchr(s.path,'.'); key = key? key+1 : s.path;
            f<<"\n# "<<s.doc<<"\n";
            if(s.kind==SK_ENUM){
                f<<"#   one of:";
                for(int n=0;n<s.nnames;n++) f<<" \""<<s.names[n]<<"\"";
                f<<"\n";
            } else if((s.kind==SK_INT||s.kind==SK_FLOAT) && s.hi>s.lo){
                f<<"#   range: "<<Tml::File::Num(s.lo)<<" .. "<<Tml::File::Num(s.hi)<<"\n";
            }
            f<<key<<" = "<<CfgFormat(s)<<"\n";
        }
    }
    // the top-level file: includes and overrides
    if(GetFileAttributesA((ExeDir()+"aether.toml").c_str())==INVALID_FILE_ATTRIBUTES)
    { std::ofstream f(ExeDir()+"aether.toml", std::ios::binary);
      if(f){
        f<<"# ==============================================================================\n"
         <<"# Aether - the top of the config.\n"
         <<"#\n"
         <<"# `include` lists the component files, in the order they take effect. Drop someone\n"
         <<"# else's bar.toml into config\\ and it just works; reorder the list to change who\n"
         <<"# wins. Anything written directly in THIS file overrides the files it included,\n"
         <<"# so you can keep a shared set of components and still bend one value locally.\n"
         <<"# ==============================================================================\n\n"
         <<"include = [\n";
        for(size_t i=0;i<comps.size();i++)
            f<<"  \"config/"<<comps[i]<<".toml\""<<(i+1<comps.size()?",":"")<<"\n";
        f<<"]\n\n"
         <<"# Example - uncomment to override whatever config/bar.toml says:\n"
         <<"# [bar]\n"
         <<"# size = 52\n";
      } }
}

// ---- read everything back ----------------------------------------------------------------------
static void CfgLoad(){
    g_cfgFiles.clear(); g_cfgOwner.clear();
    std::vector<std::string> order;
    // aether.toml decides the order; without it, every component file in registry order
    Tml::File top;
    bool haveTop = top.load(ExeDir()+"aether.toml");
    if(haveTop){
        for(const std::string& inc : top.strArray("include")){
            std::string p=inc;
            for(char& ch:p) if(ch=='/') ch='\\';
            if(p.size()>1 && p[1]!=':') p=ExeDir()+p;     // relative to the exe
            order.push_back(p);
        }
    }
    if(order.empty()){
        std::vector<std::string> comps;
        for(int i=0;i<SETTINGS_N;i++){
            std::string c=CfgComponent(SETTINGS[i].path);
            if(std::find(comps.begin(),comps.end(),c)==comps.end()) comps.push_back(c);
        }
        for(const std::string& c:comps) order.push_back(CfgDir()+c+".toml");
    }
    // Any config\*.toml the include list does not mention is still loaded, after the listed ones.
    // That covers a component added since aether.toml was written, and means dropping somebody
    // else's file into config\ works without editing the include array by hand.
    { WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA((CfgDir()+"*.toml").c_str(),&fd);
      if(h!=INVALID_HANDLE_VALUE){
          do{ if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
              std::string full=CfgDir()+fd.cFileName;
              bool listed=false;
              for(const std::string& p:order) if(_stricmp(p.c_str(),full.c_str())==0){ listed=true; break; }
              if(!listed) order.push_back(full);
          } while(FindNextFileA(h,&fd));
          FindClose(h); } }
    for(const std::string& p:order){
        Tml::File f;
        if(f.load(p)) g_cfgFiles.push_back(std::move(f));
    }
    if(haveTop) g_cfgFiles.push_back(std::move(top));   // the top file overrides its own includes

    for(int i=0;i<SETTINGS_N;i++){
        const Setting& s=SETTINGS[i];
        for(int fi=0; fi<(int)g_cfgFiles.size(); fi++){
            if(!g_cfgFiles[fi].has(s.path)) continue;
            CfgApply(s, g_cfgFiles[fi]);
            g_cfgOwner[s.path]=fi;                       // last writer wins, and owns it
        }
    }
    if(g_cfgOwner.count("bar.items")) BarItemsFromToml();
}

// ---- write the live values back, in place --------------------------------------------------------
static void CfgSave(){
    if(g_cfgFiles.empty()) return;
    BarItemsToToml();
    std::vector<bool> touched(g_cfgFiles.size(), false);
    for(int i=0;i<SETTINGS_N;i++){
        const Setting& s=SETTINGS[i];
        std::string text=CfgFormat(s);
        auto it=g_cfgOwner.find(s.path);
        int fi;
        bool fresh=false;
        if(it!=g_cfgOwner.end()) fi=it->second;
        else {
            // never seen in any file: put it in its component's file, creating it if need be
            std::string want=CfgFileFor(s.path);
            fi=-1;
            for(int k=0;k<(int)g_cfgFiles.size();k++)
                if(_stricmp(g_cfgFiles[k].path.c_str(),want.c_str())==0){ fi=k; break; }
            if(fi<0){ Tml::File nf; nf.path=want; nf.parse(""); g_cfgFiles.push_back(std::move(nf));
                      fi=(int)g_cfgFiles.size()-1; }
            g_cfgOwner[s.path]=fi;
            fresh=!g_cfgFiles[fi].has(s.path);
        }
        if(g_cfgFiles[fi].rawOf(s.path)==text) continue;    // unchanged: do not touch the file
        // A list the user spread over several lines reads back identical but is not the same TEXT;
        // comparing the words keeps their layout instead of collapsing it on every save.
        if(s.kind==SK_STRLIST && g_cfgFiles[fi].has(s.path) &&
           g_cfgFiles[fi].strArray(s.path)==*(std::vector<std::string>*)s.ptr) continue;
        g_cfgFiles[fi].set(s.path,text);
        if(fresh){
            // A key appended to an existing file used to land as a bare `key = value`. Give it the
            // same comment a generated file would have, so the file still explains itself.
            const char* key=strchr(s.path,'.'); key = key? key+1 : s.path;
            auto& L=g_cfgFiles[fi].lines;
            std::string tbl(s.path, key-1-s.path);
            int hdr=g_cfgFiles[fi].tableLineOf(tbl);        // search this table only - "items" may exist elsewhere
            for(size_t li=(hdr<0?0:(size_t)hdr+1); li<L.size(); li++){
                if(L[li].rfind(std::string(key)+" = ",0)!=0) continue;
                L.insert(L.begin()+li, std::string("# ")+s.doc);
                L.insert(L.begin()+li, std::string());
                g_cfgFiles[fi].reindex();
                break;
            }
        }
        touched[fi]=true;
    }
    CreateDirectoryA(CfgDir().c_str(), nullptr);
    for(size_t i=0;i<g_cfgFiles.size();i++)
        if(i<touched.size() && touched[i]) g_cfgFiles[i].save();
}

// ---- bar presets: the shapes real Linux panels have -------------------------------------------
//
// The bar is already an ordered list of items with flexible spacers, alignment and grouping pills -
// which is the same model waybar and polybar are built on, so most desktops' panels are not a
// different BAR, they are a different ORDER. A preset is therefore just a layout: an edge, a
// thickness, an alignment and a sequence. Everything it sets stays editable afterwards; picking one
// is a starting point, not a mode.
//
// A preset is applied as an ACTION, never restored from config on startup. If it were a setting,
// every launch would overwrite whatever you had rearranged since.
struct BarPreset {
    const char* key;
    const char* name;
    const char* blurb;
    int   edge;              // EDGE_*
    float size;              // thickness in px
    float gap;               // margin from the screen edge
    int   align;             // BALIGN_*
    bool  pills;
    int   style;             // 0 classic, 1 caelestia
    int   items[14];         // in order, terminated by -1; everything omitted is turned off
};
static const BarPreset BAR_PRESETS[] = {
  { "gnome", "GNOME", "A thin bar across the top: Activities on the left, the clock alone in the centre, the status cluster on the right.",
    EDGE_TOP, 34, 0, BALIGN_START, false, 0,
    { BIT_LOGO, BIT_SPACER1, BIT_CLOCK, BIT_SPACER2, BIT_TRAY, BIT_WIFI, BIT_BT, BIT_AUDIO, BIT_BATT, BIT_POWER, -1 } },
  { "plasma", "KDE Plasma", "A panel along the bottom: launcher, task buttons, then the tray, clock and power at the right end.",
    EDGE_BOTTOM, 44, 0, BALIGN_START, true, 0,
    { BIT_LOGO, BIT_WORKSPACES, BIT_APPS, BIT_SPACER1, BIT_TRAY, BIT_NOTIF, BIT_AUDIO, BIT_WIFI, BIT_BT, BIT_BATT, BIT_CLOCK, BIT_POWER, -1 } },
  { "xfce", "XFCE", "The same idea as Plasma but thinner, and with the window title where the task buttons end.",
    EDGE_BOTTOM, 34, 0, BALIGN_START, false, 0,
    { BIT_LOGO, BIT_WORKSPACES, BIT_APPS, BIT_SPACER1, BIT_WINDOWINFO, BIT_SPACER2, BIT_TRAY, BIT_AUDIO, BIT_BATT, BIT_CLOCK, -1 } },
  { "polybar", "polybar", "Workspaces hard left, the focused window in the middle, modules hard right. No capsules - polybar draws its modules bare.",
    EDGE_TOP, 32, 6, BALIGN_START, false, 0,
    { BIT_WORKSPACES, BIT_SPACER1, BIT_WINDOWINFO, BIT_SPACER2, BIT_MIC, BIT_AUDIO, BIT_WIFI, BIT_BATT, BIT_CLOCK, BIT_POWER, -1 } },
  { "waybar", "waybar", "polybar's layout with Wayland manners: a floating bar off the edge, and the right-hand modules grouped onto one capsule.",
    EDGE_TOP, 36, 8, BALIGN_START, true, 0,
    { BIT_WORKSPACES, BIT_SPACER1, BIT_WINDOWINFO, BIT_SPACER2, BIT_TRAY, BIT_AUDIO, BIT_WIFI, BIT_BT, BIT_BATT, BIT_CLOCK, BIT_POWER, -1 } },
  { "unity", "Ubuntu Unity", "The launcher down the left: logo, then every running app as a tile, with power at the bottom.",
    EDGE_LEFT, 56, 0, BALIGN_START, false, 0,
    { BIT_LOGO, BIT_WORKSPACES, BIT_APPS, BIT_SPACER1, BIT_TRAY, BIT_POWER, -1 } },
  { "elementary", "elementary / Pantheon", "A floating top bar with nothing but the essentials, and the clock centred.",
    EDGE_TOP, 32, 8, BALIGN_START, true, 0,
    { BIT_LOGO, BIT_SPACER1, BIT_CLOCK, BIT_SPACER2, BIT_WIFI, BIT_AUDIO, BIT_BATT, BIT_POWER, -1 } },
  { "i3status", "i3 / dwm", "The tiling-WM classic: workspace numbers at the left edge, one run of status text at the right, nothing in between.",
    EDGE_TOP, 28, 0, BALIGN_START, false, 0,
    { BIT_WORKSPACES, BIT_SPACER1, BIT_MIC, BIT_AUDIO, BIT_ETH, BIT_WIFI, BIT_BATT, BIT_CLOCK, -1 } },
  { "windows11", "Windows 11", "Centred: the launcher and task buttons in the middle of the bar, tray and clock in the corner.",
    EDGE_BOTTOM, 48, 0, BALIGN_START, false, 0,
    { BIT_SPACER1, BIT_LOGO, BIT_WORKSPACES, BIT_APPS, BIT_SPACER2, BIT_TRAY, BIT_NOTIF, BIT_WIFI, BIT_AUDIO, BIT_BATT, BIT_CLOCK, -1 } },
};
static const int BAR_PRESET_N = (int)(sizeof(BAR_PRESETS)/sizeof(BAR_PRESETS[0]));

static void ApplyBarPreset(const BarPreset& pr){
    g_barStyle = pr.style;
    g_pn[PN_BAR].edge    = pr.edge;
    g_pn[PN_BAR].size    = pr.size;
    g_pn[PN_BAR].gap     = pr.gap;
    g_pn[PN_BAR].visible = true;
    g_barAlign = pr.align;
    g_barPills = pr.pills;
    std::vector<BarItemCfg> out; bool used[BIT_COUNT]={false};
    for(int k=0;k<14 && pr.items[k]>=0;k++){
        int id=pr.items[k];
        if(id<0||id>=BIT_COUNT||used[id]) continue;
        out.push_back({id,true}); used[id]=true;
    }
    // Everything the preset did not name is kept, in factory order, turned OFF - so switching
    // presets never loses an item, and switching back finds it where it has always been.
    for(int i=0;i<BIT_COUNT;i++) if(!used[i]) out.push_back({i,false});
    g_barItems=std::move(out);
    SaveConfig();
    g_monsDirty=true;                 // the bar changed edge/thickness: redo the work areas
}
static bool ApplyBarPresetByKey(const std::string& key){
    for(int i=0;i<BAR_PRESET_N;i++) if(key==BAR_PRESETS[i].key){ ApplyBarPreset(BAR_PRESETS[i]); return true; }
    return false;
}

// ---- the bar's own colours --------------------------------------------------------------------
// The scheme drives every surface in the shell at once, which is right for a theme and wrong when
// all you want is a taskbar that does not match. These override the palette FOR THE DURATION OF
// DrawBar and put it back afterwards - the bar is drawn in one pass into its own layer, so a swap
// around that pass reaches every pixel of it and nothing else.
static bool BarHexCol(const std::string& s,ImU32& out){
    unsigned r=0,g=0,b=0;
    if(s.size()>=7 && s[0]=='#' && sscanf(s.c_str()+1,"%02x%02x%02x",&r,&g,&b)==3){ out=IM_COL32(r,g,b,255); return true; }
    return false;
}
struct BarThemeSwap {
    bool on=false, glow=false; ImU32 panell,card,card2,ink,ink2,gold,track,deskbg;
    BarThemeSwap(){
        if(!g_barTheme) return;
        ImU32 c;
        on=true;
        panell=COL_PANELL; card=COL_CARD; card2=COL_CARD2; deskbg=COL_DESKBG;
        ink=COL_INK; ink2=COL_INK2; gold=COL_GOLD; track=COL_TRACK; glow=g_deskGlow;
        if(BarHexCol(g_barColPanel,c)){
            COL_PANELL=c;
            // The bar's own surface is NOT COL_PANELL. In the frame-born look its body is painted by
            // FrameMaterialShape, which fills with COL_DESKBG and then lays the desktop frost over
            // it - so overriding only the panel colour changed the pills and left the background
            // exactly as it was. Take the surround colour too, and drop the frost for this pass:
            // someone who has typed in a colour wants that colour, not that colour under a blur.
            COL_DESKBG=c;
            g_frameTint=c;
            g_deskGlow=false;
            COL_CARD=Mix(c,COL_INK,0.06f); COL_CARD2=Mix(c,COL_INK,0.12f);
            COL_TRACK=Mix(c,COL_INK,0.18f);
        }
        if(BarHexCol(g_barColInk,c)){ COL_INK=c; COL_INK2=WithA(c,165); }
        if(BarHexCol(g_barColAccent,c)) COL_GOLD=c;
    }
    ~BarThemeSwap(){
        if(!on) return;
        COL_PANELL=panell; COL_CARD=card; COL_CARD2=card2; COL_DESKBG=deskbg;
        COL_INK=ink; COL_INK2=ink2; COL_GOLD=gold; COL_TRACK=track; g_deskGlow=glow;
        g_frameTint=0;
    }
};

static void ApplyCaelestiaBarPreset(){
    const int caelPad = std::max((int)Tok::padding::small, 10);       // Config.border.thickness
    g_pn[PN_BAR].size = (float)(Tok::bar::innerWidth + caelPad*2);    // = 60
    if(g_pn[PN_BAR].edge!=EDGE_LEFT && g_pn[PN_BAR].edge!=EDGE_RIGHT)
        g_pn[PN_BAR].edge = EDGE_LEFT;                                // it is a vertical bar
    // Upstream's entry list is logo / workspaces / spacer / activeWindow / spacer / tray / clock /
    // statusIcons / power - there is no task list, because Hyprland has no taskbar. Windows does,
    // and dropping the task buttons takes away the one thing the strip is load-bearing for, so
    // `apps` stays in, directly under the workspaces where a vertical task list belongs.
    static const int CAEL[]={ BIT_LOGO, BIT_WORKSPACES, BIT_APPS, BIT_SPACER1, BIT_WINDOWINFO,
                              BIT_SPACER2, BIT_TRAY, BIT_CLOCK, BIT_WIFI, BIT_BT, BIT_BATT,
                              BIT_POWER };
    std::vector<BarItemCfg> out; bool used[BIT_COUNT]={false};
    for(int id:CAEL){ out.push_back({id,true}); used[id]=true; }
    for(int i=0;i<BIT_COUNT;i++) if(!used[i]) out.push_back({i,false});   // everything else off
    g_barItems=std::move(out);
    g_barPills=true;          // statusIcons carries a background upstream; tray and clock do not
    g_barAlign=BALIGN_START;
    SaveConfig();
}

// rewrite config.json from the live globals (bar menu + Settings app both call this)
static int g_cfgWrites=0;                // real writes, for -s save_test
static void SaveConfigNow(){
    STALL(SaveConfig);
    g_cfgWrites++;
    int ar=(COL_GOLD)&0xFF, ag=(COL_GOLD>>8)&0xFF, ab=(COL_GOLD>>16)&0xFF;
    const char* tm = g_themeMode==1?"light":g_themeMode==2?"dark":g_themeMode==3?"auto":"scheme";
    std::ofstream f(ExeDir()+"config.json"); if(!f)return;
    f<<"{\n"
     <<"  \"appearance\": { \"scheme\": \""<<SCHEMES[g_scheme].id<<"\", \"themeMode\": \""<<tm
     <<"\", \"customAccent\": "<<(g_customAccent?"true":"false")<<", \"accent\": ["<<ar<<","<<ag<<","<<ab<<"],\n"
     <<"                   \"dynamicColor\": "<<(g_dynamicColor?"true":"false")
     <<", \"hideTaskbar\": "<<(g_hideTaskbar?"true":"false")<<", \"rounding\": "<<g_rounding<<", \"opacity\": "<<(g_drawerAlpha/255.0f)
     <<", \"iconSet\": \""<<ICONSET_KEY[std::clamp(g_iconSet,0,ICONSET_N-1)]<<"\""<<", \"uiScale\": "<<g_uiScale<<", \"textScale\": "<<g_textScale<<", \"micaWindows\": "<<(g_micaMode?"true":"false")<<", \"micaKind\": "<<g_micaKind<<" },\n"
     <<"  \"background\": { \"mode\": \""<<(g_bgMode==1?"image":g_bgMode==2?"solid":"acrylic")<<"\", \"image\": \""<<g_bgPath
     <<"\", \"blur\": "<<(g_bgBlur?"true":"false")<<", \"opacity\": "<<g_bgOpacity<<", \"wallpaperDir\": \""<<g_wallDir<<"\",\n"
     <<"                  \"transition\": \""<<WTRANS_NAME[(int)g_transCfg]<<"\", \"transitionMs\": "<<g_transMs
     <<", \"transitionPos\": \""<<(g_transPosMouse?"mouse":"center")<<"\" },\n"
     <<"  \"desktop\": { \"hostIcons\": "<<(g_hostDesktop?"true":"false")
     <<", \"confineApps\": "<<(g_confineApps?"true":"false")
     <<", \"bubble\": "<<(g_bubble?"true":"false")<<", \"live\": "<<(g_deskLive?"true":"false")
     <<", \"clock\": "<<(g_deskClock?"true":"false")
     <<", \"widgetTab\": \""<<jesc(g_deskTabName)<<"\""
     <<", \"gap\": "<<g_gap
     <<", \"radius\": "<<(int)g_bubbleRound<<" },\n"
     <<"  \"dock\": { \"on\": "<<(g_dockOn?"true":"false")<<", \"autohide\": "<<(g_dockAutohide?"true":"false")
     <<", \"icon\": "<<(int)g_dockIcon<<", \"mag\": "<<g_dockMag<<", \"magRange\": "<<g_dockMagRange
     <<", \"gap\": "<<(int)g_dockGap<<", \"round\": "<<(int)g_dockRound<<", \"opacity\": "<<g_dockOpacity
     <<", \"pinned\": ["; { bool first=true; for(auto&p:g_dockPins){ f<<(first?"":", ")<<"\""<<jesc(W2U8(p.exe))<<"\""; first=false; } } f<<"] },\n"
     <<"  \"bar\": { \"autoHide\": "<<(g_barAutoHide?"true":"false")<<", \"width\": "<<(int)g_pn[PN_BAR].size
     <<", \"hideOnFullscreen\": "<<(g_hideOnFullscreen?"true":"false")
     <<", \"sameMonitor\": "<<(g_barSameMonitor?"true":"false")
     <<", \"previews\": "<<(g_barPreviews?"true":"false")
     <<", \"monitors\": \""<<(g_barMonMode==1?"primary":"all")<<"\""
     // item layout: ONE ordered token list; a leading '!' means the user hid that item.
     // Order and visibility live in the same array on purpose - two parallel lists drift apart
     // the moment an item is added, and then a stale index silently hides the wrong control.
     <<", \"logo\": \""<<jesc(g_barLogo)<<"\""
     <<", \"logoTint\": "<<(g_barLogoTint?"true":"false")
     <<", \"workspacesShown\": "<<g_wsShown
     <<", \"workspaceSource\": \""<<WSSRC_KEY[std::clamp(g_wsSource,0,2)]<<"\""
     <<", \"komorebiReserve\": "<<(g_komoReserve?"true":"false")
     <<", \"komorebiAutoStart\": "<<(g_komoAutoStart?"true":"false")
     <<", \"komorebiMasir\": "<<(g_komoMasir?"true":"false")
     <<", \"komorebiWhkd\": "<<(g_komoWhkd?"true":"false")
     <<", \"keepExplorer\": "<<(g_keepExplorer?"true":"false")
     <<", \"niriMode\": "<<(g_niriMode?"true":"false")
     <<", \"niriColumns\": "<<g_niriCols
     <<", \"komoAutoAdopt\": "<<(g_komoAutoAdopt?"true":"false")
     <<", \"komoLog\": "<<(g_komoLog?"true":"false")
     <<", \"fastHide\": "<<(g_fastHide.load()?"true":"false")
     <<", \"wsSlide\": "<<(g_wsSlide.load()?"true":"false")
     <<", \"wsSlideMs\": "<<g_wsSlideMs.load()
     <<", \"wsSlideFps\": "<<g_wsSlideFps.load()
     <<", \"wsSlideVertical\": "<<(g_wsSlideVert.load()?"true":"false")
     <<", \"pauseKomoAnim\": "<<(g_wsPauseKomoAnim.load()?"true":"false")
     <<", \"align\": "<<g_barAlign<<", \"pills\": "<<(g_barPills?"true":"false")
     <<", \"trayCollapse\": "<<(g_trayCollapse?"true":"false")
     <<", \"trayShown\": ["; { bool first=true; for(auto&k:g_trayShown){ f<<(first?"":", ")<<"\""<<jesc(k)<<"\""; first=false; } } f<<"]"
     <<", \"items\": ["; { bool first=true; for(auto&c:g_barItems){
            if(c.id<0||c.id>=BIT_COUNT) continue;
            f<<(first?"":", ")<<"\""<<(c.on?"":"!")<<BAR_ITEMS[c.id].key<<"\""; first=false; } }
     f<<"] },\n"
     <<"  \"layout\": {\n";
    for(int i=0;i<PN_COUNT;i++){ const Panel& p=g_pn[i];
        f<<"    \""<<PANEL_ID[i]<<"\": { \"edge\": \""<<EDGE_NAME[p.edge]<<"\", \"size\": "<<p.size
         <<", \"span\": "<<p.span<<", \"spanMax\": "<<p.spanMax<<", \"anchor\": "<<p.anchor
         <<", \"gap\": "<<p.gap<<", \"order\": "<<p.order
         <<", \"visible\": "<<(p.visible?"true":"false")<<" }"<<(i+1<PN_COUNT?",":"")<<"\n"; }
    // "layout" is opened above and was never closed, and no comma was written before "macros" - so
    // every config the shell has ever saved has been malformed JSON from this point down. Our own
    // loader is a lenient substring scanner and never cared; jq, an editor's validator, or any
    // stricter parser does.
    f<<"  },\n";
    // macros: the whole schema, so a hand-edited config can re-bind or rewrite steps.
    // g_macroArmed is deliberately NOT saved - the shell must never come up already able to
    // synthesise keyboard and mouse input.
    f<<"  \"macros\": [";
    for(size_t mi=0; mi<g_macros.size(); mi++){
        const Macro& m=g_macros[mi];
        f<<(mi?",":"")<<"\n    { \"name\": \""<<jesc(m.name)<<"\", \"desc\": \""<<jesc(m.desc)
         <<"\", \"trigger\": "<<m.trigger<<", \"mode\": "<<m.mode
         <<", \"suppress\": "<<(m.suppress?"true":"false")<<", \"interval\": "<<m.interval
         <<", \"enabled\": "<<(m.enabled?"true":"false")<<", \"steps\": [";
        for(size_t k=0;k<m.steps.size();k++){ const MacroStep& st=m.steps[k];
            f<<(k?", ":"")<<"["<<st.kind<<","<<st.code<<","<<st.arg<<"]"; }
        f<<"] }";
    }
    f<<"\n  ],\n";
    // There used to be a "}," here. It was the close for "layout" - written so late that every
    // macro ended up NESTED INSIDE the layout object, and the only thing keeping the braces
    // balanced was that the opening "{" was never terminated where it should have been. Now that
    // layout closes itself above, this brace would be one too many.
    f<<"  \"sidebar\": { \"media\": "<<(g_qsMedia?"true":"false")
     <<", \"gauges\": "<<(g_qsGauges?"true":"false")<<" },\n"
     <<"  \"motion\": { \"decorations\": "<<(g_decoOn?"true":"false")<<", \"speed\": "<<g_animMul<<", \"reduce\": "<<(g_reduceMotion?"true":"false")
     <<", \"idle\": "<<(g_idleMotion?"true":"false")<<", \"idleRate\": "<<g_idleRate
     <<", \"tilt\": "<<g_tiltAmount<<" },\n"
     <<"  \"region\": { \"clock24\": "<<(g_clock24?"true":"false")<<", \"firstDay\": "<<g_firstDay<<" },\n"
     <<"  \"notifications\": { \"suppressNative\": "<<(g_suppressToasts?"true":"false")<<", \"doNotDisturb\": "<<(g_dnd?"true":"false")<<", \"nowPlaying\": "<<(g_nowPlaying?"true":"false")<<" },\n"
     <<"  \"windowSearch\": "<<(g_launchWindows?"true":"false")<<",\n"
     <<"  \"outlines\": "<<(g_panelOutline?"true":"false")
     <<", \"glow\": "<<(g_deskGlow?"true":"false")
     <<", \"panelShadow\": "<<(g_panelShadow?"true":"false")<<",\n"
     <<"  \"windows\": { \"roundCorners\": "<<(g_roundWindows?"true":"false")
     <<", \"deepCorners\": "<<(g_deepCorners?"true":"false")
     <<", \"cornerRadius\": "<<g_winRoundPx<<" },\n"
     <<"  \"clipboard\": { \"history\": "<<(g_clipEnable?"true":"false")<<", \"maxEntries\": "<<g_clipMax
     <<", \"pasteOnPick\": "<<(g_clipPaste?"true":"false")<<" },\n"
     <<"  \"launcher\": { \"descriptions\": "<<(g_launchDesc?"true":"false")<<", \"maxResults\": "<<g_launchMax
     <<", \"position\": "<<g_launPos<<", \"width\": "<<g_launWidth<<", \"radius\": "<<g_launRound
     <<", \"animation\": "<<g_launAnimStyle<<", \"openMs\": "<<g_launOpenMs<<", \"closeMs\": "<<g_launCloseMs
     <<", \"stagger\": "<<(g_launStagger?"true":"false")
     <<", \"opacity\": "<<g_launOpacity<<", \"blur\": "<<(g_launBlur?"true":"false")
     <<", \"superKey\": "<<(g_winKeyLauncher?"true":"false")<<", \"appFolders\": [";
    for(size_t i=0;i<g_launchDirs.size();i++) f<<(i?", ":"")<<"\""<<jesc(g_launchDirs[i])<<"\"";
    f<<"] },\n"
     <<"  \"dashboard\": { \"mediaRays\": "<<(g_mdRays?"true":"false")
     <<", \"mediaShape\": "<<g_mdMediaShape
     <<", \"mediaImage\": \""<<jesc(g_catPath)<<"\", \"gaugeShape\": "<<g_m3Shape
     <<", \"tabs\": [";
    for(size_t ti=0;ti<g_tabs.size();ti++){ const DashTab& t=g_tabs[ti];
        f<<(ti?",":"")<<"\n    { \"name\": \""<<jesc(t.name)<<"\", \"icon\": "<<t.icon<<", \"iconName\": \""<<jesc(t.iconName)<<"\""
         <<", \"builtin\": "<<(t.builtin?"true":"false")<<", \"widgets\": [";
        for(size_t wi=0;wi<t.widgets.size();wi++){ const Widget& w=t.widgets[wi];
            f<<(wi?", ":"")<<"{ \"k\": \""<<(w.kind>=0&&w.kind<WK_COUNT?WREG[w.kind].id:"text")
             <<"\", \"x\": "<<w.x<<", \"y\": "<<w.y<<", \"w\": "<<w.w<<", \"h\": "<<w.h;
            if(!w.arg.empty()) f<<", \"arg\": \""<<jesc(w.arg)<<"\"";
            if(!w.style.empty()) f<<", \"style\": \""<<jesc(w.style)<<"\"";
            f<<" }"; }
        f<<"] }"; }
    f<<"\n  ], \"calMarks\": [";
    for(size_t i=0;i<g_calMarks.size();i++) f<<(i?",":"")<<g_calMarks[i];
    f<<"], \"calNotes\": [";
    { bool first=true; for(auto& kv:g_calNotes){ if(kv.second.empty()) continue;
        char kb[16]; snprintf(kb,16,"%d:",kv.first);
        f<<(first?"":", ")<<"\""<<kb<<jesc(kv.second)<<"\""; first=false; } }
    f<<"] },\n"
     <<"  \"sensors\": { \"lhmUrl\": \""<<g_lhmUrl<<"\", \"sidecar\": "<<(g_sensorSidecar?"true":"false")<<" },\n"
     <<"  \"power\": { \"autoDim\": "<<(g_autoDim?"true":"false")<<", \"dimAfter\": "<<g_dimAfter
     <<", \"dimLevel\": "<<g_dimLevel<<", \"idleLock\": "<<(g_idleLock?"true":"false")<<", \"idleLockAfter\": "<<g_idleLockAfter
     <<", \"displayOff\": "<<(g_idleDisplayOff?"true":"false")<<", \"displayOffAfter\": "<<g_idleDisplayAfter<<", \"hibernate\": "<<(g_idleHibernate?"true":"false")<<", \"hibernateAfter\": "<<g_idleHibernateAfter<<", \"lockScreen\": "<<(g_useLockScreen?"true":"false")<<", \"lockStyle\": "<<g_lockStyle<<", \"lockCode\": \""<<jesc(g_lockCode)<<"\", \"hello\": "<<(g_helloOn?"true":"false")<<" },\n"
     <<"  \"wallpaper\": { \"folders\": [";
    for(size_t i=0;i<g_wallFolders.size();i++){ f<<(i?", ":"")<<"\""<<jesc(g_wallFolders[i])<<"\""; }
    f<<"], \"recursive\": "<<(g_wallRecursive?"true":"false")<<", \"bundled\": "<<(g_wallBundled?"true":"false")
     <<", \"showName\": "<<(g_wallShowName?"true":"false")<<", \"showExt\": "<<(g_wallShowExt?"true":"false")<<",\n"
     <<"                  \"cardW\": "<<g_carCW<<", \"cardH\": "<<g_carCH<<", \"nearW\": "<<g_carNW
     <<", \"nearH\": "<<g_carNH<<", \"cardGap\": "<<g_carGap
     <<", \"pickerPos\": "<<g_wallPos<<", \"pickerScale\": "<<g_wallScale
     <<", \"pickerRadius\": "<<g_wallRound<<", \"pickerAnim\": "<<g_wallAnim
     <<", \"pickerOpacity\": "<<g_wallOpacity<<", \"pickerBlur\": "<<(g_wallBlur?"true":"false")
     <<", \"pickerImmersive\": "<<(g_wallImmersive?"true":"false")
     <<", \"pickerBackdrop\": "<<(g_wallBackdrop?"true":"false")<<",\n"
     <<"                  \"depthShadow\": "<<(g_carShadow?"true":"false")<<", \"hoverZoom\": "<<(g_carHoverZoom?"true":"false")
     <<", \"hoverAmount\": "<<g_carHoverAmt<<",\n"
     <<"                  \"slideshowSec\": "<<g_slideSec<<", \"shuffle\": "<<(g_slideShuffle?"true":"false")
     <<", \"paletteExport\": "<<(g_paletteExport?"true":"false")<<",\n"
     <<"                  \"restoreOnLaunch\": "<<(g_restoreOnLaunch?"true":"false")
     <<", \"postApply\": \""<<jesc(g_postApplyCmd)<<"\",\n"
     <<"                  \"prevTransition\": \""<<g_transPrevName<<"\", \"nextTransition\": \""<<g_transNextName
     <<"\", \"last\": \""<<jesc(g_lastWall)<<"\" },\n"
     <<"  \"shortcuts\": { ";
    for(int i=0;i<HK_COUNT;i++)
        f<<(i?", ":"")<<"\""<<g_hk[i].id<<"\": \""<<jesc(HotkeyName(g_hk[i].mods,g_hk[i].vk))<<"\"";
    f<<", \"altTab\": "<<(g_swEnable?"true":"false")
     <<", \"switcherStyle\": "<<g_swStyle
     <<", \"switcherReflect\": "<<(g_swReflect?"true":"false")<<" },\n"
     <<"  \"displays\": {";
    { bool first=true;
      // remember every display we have seen, not just the attached ones, so unplugging and
      // replugging a screen keeps its switches
      for(auto& m:g_mons) g_monCfg[m.dev]=m.cfg;
      for(auto& kv2:g_monCfg){ const MonCfg& c=kv2.second;
          f<<(first?"\n":",\n")<<"    \""<<jesc(kv2.first)<<"\": { \"desktop\": "<<(c.desktop?"true":"false")
           <<", \"bar\": "<<(c.bar?"true":"false")<<", \"panels\": "<<(c.panels?"true":"false")<<" }";
          first=false; }
      f<<(first?" },\n":"\n  },\n"); }
    f<<"  \"plugins\": { \"enabled\": "<<(g_pluginsOn?"true":"false")<<", \"disabled\": [";
    for(size_t i=0;i<g_plDisabled.size();i++) f<<(i?", ":"")<<"\""<<jesc(g_plDisabled[i])<<"\"";
    f<<"] },\n"
     <<"  \"profile\": { \"name\": \""<<g_profileName<<"\" }\n"
     <<"}\n";
    f.close();
    // The TOML files are the record now. config.json is still written alongside while the registry
    // grows to cover every key, so nothing that has not been migrated yet can be lost.
    CfgSave();
}
// Every Settings control calls SaveConfig, and a handful of sliders call it on EVERY frame of a drag -
// that was config.json plus every TOML file rewritten 60 times a second, on the render thread. A click
// still saves at once; while the mouse button is held, saves are spaced 250 ms apart and the last one
// lands when the button comes up (SaveConfigFlush, from the render loop and on the way out).
// Only saves within ONE continuous press are spaced out: two separate quick clicks (a toggle, then "Save
// preset") still save immediately each time, because the button went up in between.
static bool      g_cfgPending=false;
static bool      g_cfgSavedThisPress=false;
static ULONGLONG g_cfgLastSave=0;
static int       g_cfgFakeHeld=-1;       // -s save_test: pretend the button is held (1) / up (0); -1 = real mouse
static bool CfgButtonHeld(){ return g_cfgFakeHeld>=0 ? g_cfgFakeHeld==1 : (GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0; }
static void SaveConfig(){
    const ULONGLONG now=GetTickCount64();
    const bool held=CfgButtonHeld();
    if(held && g_cfgSavedThisPress && now-g_cfgLastSave<250){ g_cfgPending=true; return; }
    g_cfgPending=false; g_cfgLastSave=now; g_cfgSavedThisPress=held;
    SaveConfigNow();
}
static void SaveConfigFlush(bool force=false){
    const bool held=CfgButtonHeld();
    if(!held) g_cfgSavedThisPress=false;
    if(g_cfgPending && (force || !held)){ g_cfgPending=false; g_cfgLastSave=GetTickCount64(); SaveConfigNow(); }
}
static bool IsLightTheme();   // fwd (registry AppsUseLightTheme)
// pick the scheme's light/dark sibling when themeMode forces one
static void ApplyThemeMode(){
    bool wantDark = (g_themeMode==2) || (g_themeMode==3 && !IsLightTheme());
    if(g_themeMode==1) wantDark=false;
    if(g_themeMode==0){ ApplyScheme(g_scheme); return; }
    if(SCHEMES[g_scheme].dark==wantDark){ ApplyScheme(g_scheme); return; }
    const char* fam=SCHEMES[g_scheme].family;
    for(int i=0;i<NSCHEMES;i++) if(!strcmp(SCHEMES[i].family,fam) && SCHEMES[i].dark==wantDark){ ApplyScheme(i); return; }
    for(int i=0;i<NSCHEMES;i++) if(SCHEMES[i].dark==wantDark){ ApplyScheme(i); return; }
    ApplyScheme(g_scheme);
}
static void LoadConfig(){
    LoadUserSchemes();                           // before the scheme id below is resolved
    std::string path=ExeDir()+"config.json";
    std::ifstream in(path);
    if(!in){ g_freshConfig=true; WriteDefaultConfig(path); in.open(path); }
    std::stringstream ss; ss<<in.rdbuf(); std::string c=ss.str();
    std::string app=jscope1(c,"appearance"), bg=jscope1(c,"background"), prof=jscope1(c,"profile"), desk=jscope1(c,"desktop");
    // colour scheme first — the accent/dynamic-colour overrides layer on top of it
    { std::string sc=jstr(app,"scheme"); g_scheme = sc.empty()?0:SchemeIndex(sc);
      std::string tm=jstr(app,"themeMode");
      g_themeMode = (tm=="light")?1:(tm=="dark")?2:(tm=="auto")?3:0; }
    g_customAccent = jbool(app,"customAccent");
    { std::string ik=jstr(app,"iconSet"); if(!ik.empty()) for(int i=0;i<ICONSET_N;i++) if(ik==ICONSET_KEY[i]){ g_iconSet=i; break; } }
    ApplyThemeMode();
    auto acc=jarr(jscope(app,"accent"));
    if(g_customAccent && acc.size()>=3){ int r=(int)acc[0],g=(int)acc[1],b=(int)acc[2];
        COL_GOLD=IM_COL32(r,g,b,255);
        COL_GOLDBG= g_darkUI ? IM_COL32(r/3+18,g/3+18,b/3+18,255)
                             : IM_COL32(std::min(255,r+150),std::min(255,g+120),std::min(255,b+120),255); }
    g_dynamicColor = (jkey(app,"dynamicColor")==std::string::npos) ? false : jbool(app,"dynamicColor");
    if(g_dynamicColor) ApplyDynamicAccent();   // wallpaper-derived accent overrides everything
    g_hideTaskbar  = (jkey(app,"hideTaskbar")==std::string::npos) ? true : jbool(app,"hideTaskbar");
    g_micaMode = jbool(app,"micaWindows");                       // reapplied to all windows by RefreshDock's tick
    { double mk=jnum(app,"micaKind"); if(mk>=2&&mk<=4) g_micaKind=(int)mk; }
    { double ts=jnum(app,"textScale"); if(ts>=0.7&&ts<=1.3) g_textScale=(float)ts; }
    float rnd=(float)jnum(app,"rounding"); if(rnd>0.2f&&rnd<3){ g_rounding=rnd; g_cardRound=16*rnd; g_panelRound=22*rnd; }
    float op=(float)jnum(app,"opacity"); if(op>0.1f&&op<=1.0f) g_drawerAlpha=(int)(op*255);
    std::string mode=jstr(bg,"mode"); g_bgMode = (mode=="image")?1:(mode=="solid")?2:0;
    g_bgPath=jstr(bg,"image"); g_bgBlur=jbool(bg,"blur"); g_wallDir=jstr(bg,"wallpaperDir");
    { std::string tr=jstr(bg,"transition"); if(!tr.empty()) g_transCfg=ParseTrans(tr);
      double v=jnum(bg,"transitionMs"); if(v>=80&&v<=6000) g_transMs=(int)v;
      std::string tp=jstr(bg,"transitionPos"); if(!tp.empty()) g_transPosMouse=(tp=="mouse"); }
    float bo=(float)jnum(bg,"opacity"); if(bo>=0&&bo<=1) g_bgOpacity=bo;
    g_profileName=jstr(prof,"name");
    std::string barc=jscope1(c,"bar");
    g_barAutoHide = (jkey(barc,"autoHide")==std::string::npos) ? true : jbool(barc,"autoHide");
    { double v=jnum(barc,"width"); if(v>=28&&v<=140) g_pn[PN_BAR].size=(float)v; }
    g_hideOnFullscreen = (jkey(barc,"hideOnFullscreen")==std::string::npos) ? true : jbool(barc,"hideOnFullscreen");
    g_barSameMonitor   = (jkey(barc,"sameMonitor")==std::string::npos) ? true : jbool(barc,"sameMonitor");
    g_barPreviews      = (jkey(barc,"previews")==std::string::npos) ? true : jbool(barc,"previews");
    { std::string bm=jstr(barc,"monitors"); if(!bm.empty()) g_barMonMode=(bm=="primary")?1:0; }
    // ---- bar item layout: order + visibility, keyed by token so the enum can be reordered ----
    { std::string lg=jstr(barc,"logo");
      if(!lg.empty()) for(int i=0;i<NBARLOGOS;i++) if(lg==BAR_LOGOS[i].key){ g_barLogo=lg; break; }
      int al2=(int)jnum(barc,"align"); if(al2>=0&&al2<=BALIGN_SPREAD) g_barAlign=al2;
      // absent key => keep the default (true); jbool alone would read a missing key as false and
      // silently un-collapse the tray for everyone upgrading from a pre-chevron config.
      if(jkey(barc,"trayCollapse")!=std::string::npos) g_trayCollapse=jbool(barc,"trayCollapse");
      if(jkey(barc,"pills")!=std::string::npos) g_barPills=jbool(barc,"pills");   // absent => default on
      if(jkey(barc,"logoTint")!=std::string::npos) g_barLogoTint=jbool(barc,"logoTint");
      if(jkey(barc,"workspacesShown")!=std::string::npos)
          g_wsShown=std::clamp((int)jnum(barc,"workspacesShown"),1,16);
      { std::string ws=jstr(barc,"workspaceSource");
        for(int i=0;i<3;i++) if(ws==WSSRC_KEY[i]) g_wsSource=i; }
      if(jkey(barc,"komorebiReserve")!=std::string::npos) g_komoReserve=jbool(barc,"komorebiReserve");
      if(jkey(barc,"komorebiAutoStart")!=std::string::npos) g_komoAutoStart=jbool(barc,"komorebiAutoStart");
      if(jkey(barc,"komorebiMasir")!=std::string::npos)     g_komoMasir=jbool(barc,"komorebiMasir");
      if(jkey(barc,"komorebiWhkd")!=std::string::npos)      g_komoWhkd=jbool(barc,"komorebiWhkd");
      if(jkey(barc,"keepExplorer")!=std::string::npos)      g_keepExplorer=jbool(barc,"keepExplorer");
      if(jkey(barc,"niriMode")!=std::string::npos)          g_niriMode=jbool(barc,"niriMode");
      { double nc=jnum(barc,"niriColumns"); if(nc>=1&&nc<=8) g_niriCols=(int)nc; }
      if(jkey(barc,"komoAutoAdopt")!=std::string::npos)      g_komoAutoAdopt=jbool(barc,"komoAutoAdopt");
      if(jkey(barc,"komoLog")!=std::string::npos)             g_komoLog=jbool(barc,"komoLog");
      if(jkey(barc,"fastHide")!=std::string::npos)  g_fastHide.store(jbool(barc,"fastHide"));
      if(jkey(barc,"wsSlide")!=std::string::npos)   g_wsSlide.store(jbool(barc,"wsSlide"));
      if(jkey(barc,"wsSlideMs")!=std::string::npos)
          g_wsSlideMs.store(std::clamp((int)jnum(barc,"wsSlideMs"),80,1200));
      if(jkey(barc,"wsSlideFps")!=std::string::npos)
          g_wsSlideFps.store(std::clamp((int)jnum(barc,"wsSlideFps"),30,240));
      if(jkey(barc,"wsSlideVertical")!=std::string::npos)
          g_wsSlideVert.store(jbool(barc,"wsSlideVertical")); 
      if(jkey(barc,"pauseKomoAnim")!=std::string::npos) g_wsPauseKomoAnim.store(jbool(barc,"pauseKomoAnim"));
      g_trayShown=jsarr(jscope(barc,"trayShown"));
      std::vector<std::string> toks=jsarr(jscope(barc,"items"));
      if(toks.empty()) BarItemsDefault();          // absent (first run / older config) -> factory
      else{
        g_barItems.clear();
        for(auto& t:toks){
            bool on=true; std::string k=t;
            if(!k.empty()&&k[0]=='!'){ on=false; k.erase(0,1); }
            for(int i=0;i<BIT_COUNT;i++) if(k==BAR_ITEMS[i].key){ g_barItems.push_back({i,on}); break; }
        }
        BarItemsRepair();                          // add ids this build knows but the file did not
      } }
    // ---- layout engine: per-panel geometry (read AFTER bar.width so layout wins) ----
    bool gapSet[PN_COUNT]={false,false,false};
    { std::string lay=jscope1(c,"layout");
      if(!lay.empty()) for(int i=0;i<PN_COUNT;i++){
          std::string ps=jscope(lay,PANEL_ID[i]); if(ps.empty()) continue;
          Panel& p=g_pn[i];
          int e=ParseEdge(jstr(ps,"edge"));
          if(e>=0){
              // the bar's contents are laid out vertically and the drawer's horizontally, so each
              // only accepts the edges it can actually render on (see Settings > Taskbar/Dashboard)
              if(i==PN_BAR) p.edge=e;                                    // taskbar: any of the 4 edges
              else if(i==PN_DRAWER && (e==EDGE_TOP||e==EDGE_BOTTOM)) p.edge=e;
              else if(i==PN_QS) p.edge=e;
          }
          double v;
          v=jnum(ps,"size");    if(v>=24&&v<=1400) p.size=(float)v;
          v=jnum(ps,"span");    if(v>=0.05&&v<=4000) p.span=(float)v;
          v=jnum(ps,"spanMax"); if(v>=0&&v<=4000) p.spanMax=(float)v;
          v=jnum(ps,"anchor");  if(v>=0&&v<=1) p.anchor=(float)v;
          v=jnum(ps,"gap");     if(v>=0&&v<=200){ p.gap=(float)v; gapSet[i]=true; }
          v=jnum(ps,"order");   if(v>=0&&v<=32) p.order=(int)v;
          if(jkey(ps,"visible")!=std::string::npos) p.visible=jbool(ps,"visible");
      } }
    { std::string mo=jscope1(c,"motion");
      double v=jnum(mo,"speed"); if(v>=0.2&&v<=12) g_animMul=(float)v;
      g_reduceMotion=jbool(mo,"reduce"); if(g_reduceMotion) g_animMul=9.0f;
      double t=jnum(mo,"tilt"); if(t>=0&&t<=0.8) g_tiltAmount=(float)t;
      g_decoOn = (jkey(mo,"decorations")==std::string::npos) ? false : jbool(mo,"decorations");   // opt-in for new installs
      if(jkey(mo,"idle")!=std::string::npos) g_idleMotion=jbool(mo,"idle");
      { double ir=jnum(mo,"idleRate"); if(ir>=0.25&&ir<=2.0) g_idleRate=(float)ir; }
      // Reduce motion means reduce motion: idling shapes are the first thing that should stop.
      if(g_reduceMotion) g_idleMotion=false; }
    { std::string rg=jscope1(c,"region");
      g_clock24=jbool(rg,"clock24");
      double v=jnum(rg,"firstDay"); if(v>=0&&v<=1) g_firstDay=(int)v; }
    std::string nf=jscope1(c,"notifications");
    g_suppressToasts = (jkey(nf,"suppressNative")==std::string::npos) ? true : jbool(nf,"suppressNative");
    g_dnd = jbool(nf,"doNotDisturb");
    g_nowPlaying = (jkey(nf,"nowPlaying")==std::string::npos) ? true : jbool(nf,"nowPlaying");   // default on
    std::string lc=jscope1(c,"launcher");
    { std::string cb=jscope1(c,"clipboard");
      if(!cb.empty()){
          if(jkey(cb,"history")!=std::string::npos)     g_clipEnable=jbool(cb,"history");
          if(jkey(cb,"pasteOnPick")!=std::string::npos) g_clipPaste =jbool(cb,"pasteOnPick");
          double cm=jnum(cb,"maxEntries"); if(cm>=10&&cm<=1000) g_clipMax=(int)cm; } }
    if(jkey(c,"windowSearch")!=std::string::npos) g_launchWindows=jbool(c,"windowSearch");
    if(jkey(c,"outlines")!=std::string::npos)     g_panelOutline=jbool(c,"outlines");
    if(jkey(c,"glow")!=std::string::npos)         g_deskGlow=jbool(c,"glow");
    if(jkey(c,"panelShadow")!=std::string::npos)  g_panelShadow=jbool(c,"panelShadow");
    { std::string wn=jscope1(c,"windows");
      if(!wn.empty() && jkey(wn,"roundCorners")!=std::string::npos) g_roundWindows=jbool(wn,"roundCorners");
      if(!wn.empty() && jkey(wn,"deepCorners")!=std::string::npos)  g_deepCorners=jbool(wn,"deepCorners");
      if(!wn.empty()){ double cr=jnum(wn,"cornerRadius"); if(cr>=0&&cr<=64) g_winRoundPx=(int)cr; } }
    g_launchDesc = (jkey(lc,"descriptions")==std::string::npos) ? true : jbool(lc,"descriptions");
    { double v=jnum(lc,"maxResults"); if(v>=3&&v<=20) g_launchMax=(int)v; }
    { double v=jnum(lc,"position");  if(v>=0&&v<LPOS_N)  g_launPos=(int)v; }
    { double v=jnum(lc,"width");     if(v>=360&&v<=1400) g_launWidth=(float)v; }
    { double v=jnum(lc,"radius");    if(v>=0&&v<=48)     g_launRound=(float)v; }
    { double v=jnum(lc,"animation"); if(v>=0&&v<LANIM_N) g_launAnimStyle=(int)v; }
    { double v=jnum(lc,"openMs");    if(v>=0&&v<=1200)   g_launOpenMs=(int)v; }
    { double v=jnum(lc,"closeMs");   if(v>=0&&v<=1200)   g_launCloseMs=(int)v; }
    if(jkey(lc,"stagger")!=std::string::npos) g_launStagger=jbool(lc,"stagger");
    if(jkey(lc,"blur")!=std::string::npos)    g_launBlur=jbool(lc,"blur");
    { double v=jnum(lc,"opacity"); if(v>=0.10&&v<=1.0) g_launOpacity=(float)v; }
    g_winKeyLauncher = (jkey(lc,"superKey")==std::string::npos) ? true : jbool(lc,"superKey");
    g_launchDirs = jsarr(jscope(lc,"appFolders"));   // extra folders scanned for .exe
    { std::string sb=jscope1(c,"sidebar");
      if(!sb.empty()){
          if(jkey(sb,"media") !=std::string::npos) g_qsMedia =jbool(sb,"media");
          if(jkey(sb,"gauges")!=std::string::npos) g_qsGauges=jbool(sb,"gauges"); } }
    { std::string dsh=jscope1(c,"dashboard");
      { std::string ci=jstr(dsh,"mediaImage"); if(ci!=g_catPath){ g_catPath=ci; g_catDirty=true; } }
      if(jkey(dsh,"mediaRays")!=std::string::npos) g_mdRays=jbool(dsh,"mediaRays");
      if(jkey(dsh,"mediaShape")!=std::string::npos)
          g_mdMediaShape=std::clamp((int)jnum(dsh,"mediaShape"),0,34);
      if(jkey(dsh,"gaugeShape")!=std::string::npos)
          g_m3Shape=std::clamp((int)jnum(dsh,"gaugeShape"),0,34);
      std::string ta=jscope(dsh,"tabs");
      g_tabs.clear();
      if(!ta.empty()){
          // walk the array of { name, icon, builtin, widgets:[ {k,x,y,w,h,arg}, ... ] }
          size_t p=1;
          while(p<ta.size()){
              size_t ob=ta.find('{',p); if(ob==std::string::npos) break;
              // find this tab object's matching close brace
              int d=0; size_t e=ob; for(;e<ta.size();e++){ if(ta[e]=='{')d++; else if(ta[e]=='}'){ d--; if(!d) break; } }
              if(e>=ta.size()) break;
              std::string obj=ta.substr(ob,e-ob+1);
              DashTab dt; dt.name=jstr(obj,"name"); if(dt.name.empty()) dt.name="Tab";
              dt.icon=(int)jnum(obj,"icon"); if(dt.icon<0||dt.icon>3) dt.icon=0;
              dt.builtin=jbool(obj,"builtin");
              { size_t kp=jkey(obj,"iconName"); size_t wk=jkey(obj,"widgets");
                if(kp!=std::string::npos && (wk==std::string::npos || kp<wk)) dt.iconName=jstr(obj,"iconName"); }
              std::string ws=jscope(obj,"widgets");
              size_t q=0;
              while(q<ws.size()){
                  size_t wb=ws.find('{',q); if(wb==std::string::npos) break;
                  int wd2=0; size_t we=wb; for(;we<ws.size();we++){ if(ws[we]=='{')wd2++; else if(ws[we]=='}'){ wd2--; if(!wd2) break; } }
                  if(we>=ws.size()) break;
                  std::string wo=ws.substr(wb,we-wb+1);
                  Widget w; w.kind=WKindFromId(jstr(wo,"k"));
                  if(w.kind<0) w.kind=WK_TEXT;
                  w.x=(float)jnum(wo,"x"); w.y=(float)jnum(wo,"y");
                  w.w=(float)jnum(wo,"w"); w.h=(float)jnum(wo,"h");
                  if(!(w.w>0)) w.w=0.3f; if(!(w.h>0)) w.h=0.3f;
                  w.arg=jstr(wo,"arg");
                  w.style=jstr(wo,"style");
                  dt.widgets.push_back(w);
                  q=we+1;
              }
              g_tabs.push_back(dt);
              p=e+1;
          }
      }
      if(g_tabs.empty()) DefaultTabs();
      if(g_tab>=(int)g_tabs.size()) g_tab=0;
      g_calMarks.clear();
      { auto mk=jarr(jscope(dsh,"calMarks")); for(double d:mk) if(d>0) g_calMarks.push_back((int)d); }
      // notes ride as "YYYYMMDD:text" strings - the FIRST colon splits, so a note may contain colons
      g_calNotes.clear();
      for(auto& e:jsarr(jscope(dsh,"calNotes"))){
          size_t c=e.find(':'); if(c==std::string::npos||c==0) continue;
          int d=atoi(e.substr(0,c).c_str()); if(d>0) g_calNotes[d]=e.substr(c+1); } }
    // macros. A config with none (or a broken list) falls back to the preset library rather than
    // leaving the page empty, which is what the Python build did too.
    { std::string ms=jscope1(c,"macros");
      g_macros.clear();
      size_t q=0;
      while(true){
          size_t ob=ms.find('{',q); if(ob==std::string::npos) break;
          size_t cb=ms.find('}',ob); if(cb==std::string::npos) break;
          // steps live in a [ ... ] that may sit past the object's first '}', so take the wider span
          size_t sb=ms.find("\"steps\"",ob);
          size_t end=cb;
          if(sb!=std::string::npos && sb<cb+2){ size_t se=ms.find(']',sb);
              if(se!=std::string::npos){ size_t ce=ms.find('}',se); if(ce!=std::string::npos) end=ce; } }
          std::string obj=ms.substr(ob,end-ob+1);
          Macro m;
          m.name=jstr(obj,"name"); m.desc=jstr(obj,"desc");
          m.trigger=(int)jnum(obj,"trigger"); m.mode=std::clamp((int)jnum(obj,"mode"),0,2);
          m.suppress=jbool(obj,"suppress"); m.interval=std::clamp((int)jnum(obj,"interval"),1,10000);
          m.enabled=jbool(obj,"enabled");
          { size_t p2=obj.find("\"steps\"");
            if(p2!=std::string::npos){ size_t lb=obj.find('[',p2); size_t le=obj.find(']',lb==std::string::npos?p2:lb);
              // each step is its own [k,c,a]
              size_t t=lb;
              while(t!=std::string::npos){ size_t o2=obj.find('[',t+1); if(o2==std::string::npos||o2>le) break;
                  size_t c2=obj.find(']',o2); if(c2==std::string::npos) break;
                  MacroStep st; int k2=0,c3=0,a2=0;
                  if(sscanf(obj.c_str()+o2+1,"%d,%d,%d",&k2,&c3,&a2)>=1){
                      st.kind=std::clamp(k2,0,8); st.code=c3; st.arg=a2; m.steps.push_back(st); }
                  t=c2; if(c2>=le) break; } } }
          if(!m.name.empty()) g_macros.push_back(std::move(m));
          q=end+1;
      }
      if(g_macros.empty()) MacroLoadPresets();
      g_macroHeld.assign(g_macros.size(),0); g_macroToggled.assign(g_macros.size(),0);
      g_macroArmed=false; }   // always disarmed at startup
    { std::string sn=jscope1(c,"sensors"); std::string u=jstr(sn,"lhmUrl"); if(!u.empty()) g_lhmUrl=u;
      g_sensorSidecar=jbool(sn,"sidecar"); }
    { std::string wp2=jscope1(c,"wallpaper");
      g_wallFolders=jsarr(jscope(wp2,"folders"));
      if(jkey(wp2,"bundled")!=std::string::npos) g_wallBundled=jbool(wp2,"bundled");
      if(g_wallFolders.empty() && !g_wallDir.empty()) g_wallFolders.push_back(g_wallDir);   // migrate
      g_wallRecursive=jbool(wp2,"recursive");
      g_wallShowName=(jkey(wp2,"showName")==std::string::npos)?true:jbool(wp2,"showName");
      g_wallShowExt =(jkey(wp2,"showExt") ==std::string::npos)?true:jbool(wp2,"showExt");
      double v;
      v=jnum(wp2,"cardW");   if(v>=140&&v<=640) g_carCW=(int)v;
      v=jnum(wp2,"cardH");   if(v>=90 &&v<=420) g_carCH=(int)v;
      v=jnum(wp2,"nearW");   if(v>=80 &&v<=520) g_carNW=(int)v;
      v=jnum(wp2,"nearH");   if(v>=50 &&v<=360) g_carNH=(int)v;
      v=jnum(wp2,"cardGap"); if(v>=0  &&v<=90)  g_carGap=(int)v;
      v=jnum(wp2,"pickerPos");     if(v>=0&&v<3)        g_wallPos=(int)v;
      v=jnum(wp2,"pickerScale");   if(v>=0.6&&v<=2.2)   g_wallScale=(float)v;
      v=jnum(wp2,"pickerRadius");  if(v>=0&&v<=48)      g_wallRound=(float)v;
      v=jnum(wp2,"pickerAnim");    if(v>=0&&v<5)        g_wallAnim=(int)v;
      v=jnum(wp2,"pickerOpacity"); if(v>=0.10&&v<=1.0)  g_wallOpacity=(float)v;
      if(jkey(wp2,"pickerBlur")!=std::string::npos)      g_wallBlur=jbool(wp2,"pickerBlur");
      if(jkey(wp2,"pickerImmersive")!=std::string::npos) g_wallImmersive=jbool(wp2,"pickerImmersive");
      if(jkey(wp2,"pickerBackdrop")!=std::string::npos)  g_wallBackdrop=jbool(wp2,"pickerBackdrop");
      g_carShadow=(jkey(wp2,"depthShadow")==std::string::npos)?true:jbool(wp2,"depthShadow");
      g_carHoverZoom=jbool(wp2,"hoverZoom");
      v=jnum(wp2,"hoverAmount"); if(v>=0&&v<=40) g_carHoverAmt=(int)v;
      v=jnum(wp2,"slideshowSec"); if(v>=0&&v<=7200) g_slideSec=(int)v;
      g_slideShuffle=(jkey(wp2,"shuffle")==std::string::npos)?true:jbool(wp2,"shuffle");
      g_paletteExport=jbool(wp2,"paletteExport");
      g_restoreOnLaunch=jbool(wp2,"restoreOnLaunch");
      g_postApplyCmd=jstr(wp2,"postApply");
      { std::string s2=jstr(wp2,"prevTransition"); if(!s2.empty()) g_transPrevName=s2;
        s2=jstr(wp2,"nextTransition"); if(!s2.empty()) g_transNextName=s2; }
      g_lastWall=jstr(wp2,"last"); }
    { std::string sc2=jscope1(c,"shortcuts");
      if(!sc2.empty()) for(int i=0;i<HK_COUNT;i++){
          std::string v=jstr(sc2,g_hk[i].id); if(v.empty()) continue;
          UINT m=0,k=0; ParseHotkey(v,m,k);
          if(k){ g_hk[i].mods=m|(i==HK_LAUNCHER?MOD_NOREPEAT:0u); g_hk[i].vk=k; }
          else if(v=="Not set"||v=="none"||v=="None"){ g_hk[i].vk=0; }
      }
      g_swEnable = (jkey(sc2,"altTab")==std::string::npos) ? true : jbool(sc2,"altTab");
      { int st=(int)jnum(sc2,"switcherStyle"); if(st>=0&&st<=1) g_swStyle=st; }
      if(jkey(sc2,"switcherReflect")!=std::string::npos) g_swReflect=jbool(sc2,"switcherReflect"); }
    // per-display switches: { "DISPLAY2": { desktop, bar, panels }, ... }
    { std::string ms=jscope1(c,"displays");
      if(ms.empty()) ms=jscope1(c,"monitors");      // read the old name once, then it is rewritten
      if(!ms.empty()){
          g_monCfg.clear();
          size_t p=1;
          while(true){
              size_t q=ms.find('"',p); if(q==std::string::npos) break;
              size_t e=ms.find('"',q+1); if(e==std::string::npos) break;
              std::string name=ms.substr(q+1,e-q-1);
              size_t ob=ms.find('{',e); if(ob==std::string::npos) break;
              size_t cb=ms.find('}',ob); if(cb==std::string::npos) break;
              std::string body=ms.substr(ob,cb-ob+1);
              MonCfg mc;
              mc.desktop=(jkey(body,"desktop")==std::string::npos)?true:jbool(body,"desktop");
              mc.bar    =(jkey(body,"bar")    ==std::string::npos)?true:jbool(body,"bar");
              mc.panels =(jkey(body,"panels") ==std::string::npos)?true:jbool(body,"panels");
              g_monCfg[name]=mc;
              p=cb+1;
          }
          for(auto& m:g_mons){ auto it=g_monCfg.find(m.dev); if(it!=g_monCfg.end()) m.cfg=it->second; }
          if(!g_mons.empty()) g_monsDirty=true;   // a preset can change these: rebuild the layers
      } }
    { std::string pl=jscope1(c,"plugins");
      g_pluginsOn = (jkey(pl,"enabled")==std::string::npos) ? true : jbool(pl,"enabled");
      g_plDisabled = jsarr(jscope(pl,"disabled")); }
    { std::string pw2=jscope1(c,"power");
      g_autoDim = (jkey(pw2,"autoDim")==std::string::npos) ? false : jbool(pw2,"autoDim");      // opt-in for new installs
      double v=jnum(pw2,"dimAfter"); if(v>=10&&v<=3600) g_dimAfter=(int)v;
      g_idleLock = jbool(pw2,"idleLock");
      double vl=jnum(pw2,"idleLockAfter"); if(vl>=30&&vl<=7200) g_idleLockAfter=(int)vl;
      g_idleDisplayOff=jbool(pw2,"displayOff");
      { double vd=jnum(pw2,"displayOffAfter"); if(vd>=60&&vd<=10800) g_idleDisplayAfter=(int)vd; }
      g_idleHibernate=jbool(pw2,"hibernate");
      { double vh=jnum(pw2,"hibernateAfter"); if(vh>=300&&vh<=43200) g_idleHibernateAfter=(int)vh; }
      g_useLockScreen = jbool(pw2,"lockScreen");
      if(jkey(pw2,"lockStyle")!=std::string::npos) g_lockStyle=std::clamp((int)jnum(pw2,"lockStyle"),0,2);
      { std::string lc=jstr(pw2,"lockCode"); strncpy(g_lockCode,lc.c_str(),sizeof(g_lockCode)-1); g_lockCode[sizeof(g_lockCode)-1]=0; }
      if(jkey(pw2,"hello")!=std::string::npos) g_helloOn=jbool(pw2,"hello");   // absent => keep the default (on)
      v=jnum(pw2,"dimLevel"); if(v>=0.1&&v<=0.98) g_dimLevel=(float)v; }
    g_bubble = (jkey(desk,"bubble")==std::string::npos) ? true : jbool(desk,"bubble");
    if(jkey(desk,"confineApps")!=std::string::npos) g_confineApps=jbool(desk,"confineApps");
    if(jkey(desk,"hostIcons")!=std::string::npos)   g_hostDesktop=jbool(desk,"hostIcons");
    g_deskLive = (jkey(desk,"live")==std::string::npos) ? true : jbool(desk,"live");   // live wallpaper passthrough (default on)
    g_deskClock= (jkey(desk,"clock")==std::string::npos) ? true : jbool(desk,"clock");  // wallpaper clock (default on)
    g_deskTabName = jstr(desk,"widgetTab");
    { std::string dock=jscope1(c,"dock");
      if(!dock.empty()){
        if(jkey(dock,"on")!=std::string::npos)       g_dockOn=jbool(dock,"on");
        if(jkey(dock,"autohide")!=std::string::npos) g_dockAutohide=jbool(dock,"autohide");
        double v;
        v=jnum(dock,"icon");     if(v>=24&&v<=96)    g_dockIcon=(float)v;
        v=jnum(dock,"mag");      if(v>=1.0&&v<=3.0)  g_dockMag=(float)v;
        v=jnum(dock,"magRange"); if(v>=1.0&&v<=6.0)  g_dockMagRange=(float)v;
        v=jnum(dock,"gap");      if(v>=0&&v<=40)     g_dockGap=(float)v;
        v=jnum(dock,"round");    if(v>=0&&v<=48)     g_dockRound=(float)v;
        v=jnum(dock,"opacity");  if(v>=0.2&&v<=1.0)  g_dockOpacity=(float)v;
        for(auto&p:g_dockPins) if(p.icon) p.icon->Release(); g_dockPins.clear();
        for(auto& s:jsarr(jscope(dock,"pinned"))){ DockPin p; p.exe=U82W(s); p.icon=nullptr; g_dockPins.push_back(std::move(p)); }
      } }
    { double v=jnum(desk,"gap");     if(v>=0&&v<=200) g_gap=(int)v;
      v=jnum(desk,"radius");  if(v>=0&&v<=80)  g_bubbleRound=(float)v;
      v=jnum(app,"uiScale");  if(v>=0.5&&v<=3.0) g_uiScale=(float)v; }
    // panels that didn't state a gap of their own float in the desktop gap; the drawer hangs from
    // the true screen top (gap 0) so it reads as being outside the bubble — leave it alone
    if(!gapSet[PN_BAR]) g_pn[PN_BAR].gap=(float)g_gap;
    if(!gapSet[PN_QS])  g_pn[PN_QS ].gap=(float)g_gap;

    // ---- TOML takes over from here -------------------------------------------------------------
    // First run: config\ does not exist, so it is generated from whatever the JSON just produced -
    // your current setup becomes the starting file rather than a wall of defaults. After that the
    // files are read LAST, so they are what the shell actually runs on.
    CfgGenerate();   // writes only the component files that do not exist yet
    CfgLoad();
}

// ---- desktop layer: shell surface + the wallpaper drawn as a rounded inset "bubble" ----
static ID3D11ShaderResourceView* g_deskWall=nullptr; static int g_deskWallW=0,g_deskWallH=0;
// ---- stacked wallpaper transitions ------------------------------------------------------------
// There used to be exactly ONE transition: a base image plus a single reveal on top. Changing
// wallpaper again while that reveal was still running overwrote it, so the animation you were
// watching was CUT OFF part-way and replaced. Now each apply pushes its own layer and the ones
// underneath keep running, so several transitions can be on screen at once and each finishes.
//
// Layers composite oldest-first. `under` is whatever was on top when this layer was pushed, which
// is what the Outer transition shrinks away to reveal the new image.
struct WipeLayer {
    ID3D11ShaderResourceView* tex=nullptr;   int tw=0, th=0;   // the image this layer brings in
    ID3D11ShaderResourceView* under=nullptr; int uw=0, uh=0;   // what it covers
    WTrans kind=WTrans::Fade;
    ImVec2 at=ImVec2(0,0);
    float  prog=0.0f;                                          // 0..1
    float  ms=900.0f;                                          // its own duration
};
static std::vector<WipeLayer>     g_wipeStack;
static ID3D11ShaderResourceView*  g_deskBase=nullptr; static int g_deskBaseW=0,g_deskBaseH=0;
// whatever is currently topmost - the image a new layer will be revealed over
static ID3D11ShaderResourceView* WipeTop(int& w,int& h){
    if(!g_wipeStack.empty()){ const WipeLayer& L=g_wipeStack.back(); w=L.tw; h=L.th; return L.tex; }
    if(g_deskBase){ w=g_deskBaseW; h=g_deskBaseH; return g_deskBase; }
    w=g_deskWallW; h=g_deskWallH; return g_deskWall;
}
static void PushWipe(ID3D11ShaderResourceView* tex,int tw,int th,WTrans kind,ImVec2 at,int ms){
    if(!tex) return;
    if(!g_deskBase && g_deskWall){                       // first ever transition: settle a base
        g_deskBase=g_deskWall; g_deskBase->AddRef(); g_deskBaseW=g_deskWallW; g_deskBaseH=g_deskWallH; }
    WipeLayer L;
    L.tex=tex; L.tex->AddRef(); L.tw=tw; L.th=th;
    L.under=WipeTop(L.uw,L.uh); if(L.under) L.under->AddRef();
    L.kind=kind; L.at=at; L.prog=0.0f; L.ms=(float)std::max(80,ms);
    if(kind==WTrans::None) L.prog=1.0f;                  // instant: retires on the next tick
    // A runaway stack would mean unbounded textures held alive; past a handful the older ones are
    // invisible anyway, so retire the oldest immediately rather than let it grow.
    while(g_wipeStack.size()>=6){
        WipeLayer& f=g_wipeStack.front();
        if(g_deskBase) g_deskBase->Release();
        g_deskBase=f.tex; g_deskBaseW=f.tw; g_deskBaseH=f.th;   // ref transfers
        if(f.under) f.under->Release();
        g_wipeStack.erase(g_wipeStack.begin());
    }
    g_wipeStack.push_back(L);
}
// ---- per-monitor Wallpaper Engine transitions --------------------------------------------------------
// A live wallpaper is drawn by Wallpaper Engine, not by us, so there is nothing of ours to animate. The
// hand-over is staged per screen instead: (1) the chosen transition plays from what that screen shows into
// the new wallpaper's preview, (2) the preview holds until Wallpaper Engine is rendering, (3) it fades
// out and the live wallpaper is underneath. Picking a plain image on a live screen runs (1) the other way
// round, from the live wallpaper's preview.
// phases (to a live wallpaper): 1 hold the current frame while Wallpaper Engine loads, 2 play the transition
// into a captured frame of the new wallpaper, 3 fade that frame off the live wallpaper underneath.
// REVISED: a live wallpaper's own frames cannot be read back - PrintWindow returns a stale surface for
// Wallpaper Engine's flip-model swapchain (an hour-old wallpaper flashed up during a switch), and Windows
// Graphics Capture refuses the desktop window. Animating from/into the project's small preview image is what
// looked "low quality and huge". So a switch that involves a LIVE screen fades THROUGH the bubble colour:
//   scrim: 1 fade the bubble interior out (the old wallpaper keeps playing under it), then give the command;
//          2 hold while Wallpaper Engine loads; 3 fade back in over the new wallpaper (or the new image).
// From a still image to a live one the real still image is held instead, then faded off the live wallpaper.
struct WeTrans { int phase=0; WipeLayer L; ULONGLONG t0=0, liveAt=0, lastTry=0; float fade=0; bool toImage=false; bool wasLive=false;
                 ID3D11ShaderResourceView* preview=nullptr; int pw=0, ph=0;
                 bool scrim=false; std::wstring cmd; bool sawDrop=false;
                 ImU32 pal[6]={0}; int npal=0; };          // the incoming wallpaper's colours, for the stripes
static WeTrans g_weTr[16];
#include "src/components/Stripes.h"   // stripes + Loading!!!!!! cover (live wallpaper switch, Settings)
static bool g_weToImageSnap=false;
static ID3D11ShaderResourceView* g_weShown[16]={}; static int g_weShownW[16]={}, g_weShownH[16]={};   // preview of what WE shows per screen
static void WeTransEnd(WeTrans& T){
    if(T.preview) { T.preview->Release(); T.preview=nullptr; }
    if(T.L.tex)   { T.L.tex->Release();   T.L.tex=nullptr; }
    if(T.L.under) { T.L.under->Release(); T.L.under=nullptr; }
    T.phase=0;
}
static void ClearWipes(){
    for(auto& L:g_wipeStack){ if(L.tex)L.tex->Release(); if(L.under)L.under->Release(); }
    g_wipeStack.clear();
}
static ID3D11ShaderResourceView* g_deskWallOld=nullptr; static int g_deskWallOldW=0,g_deskWallOldH=0;
// heavily blurred copy of the wallpaper: the margin AROUND the bubble is painted with it so the
// area outside the "display" reads as frosted glass of the same image, not a flat backdrop.
static ID3D11ShaderResourceView* g_deskFrost=nullptr; static int g_deskFrostW=0,g_deskFrostH=0;

static bool  g_deskDirty=true;                                  // desktop layer is static: redraw on demand
static float EaseOutCubic(float t);                             // fwd
// ---- wallpaper watcher -------------------------------------------------------------------------
// Windows sends no reliable notification when the desktop wallpaper changes (WM_SETTINGCHANGE with
// SPI_SETDESKWALLPAPER only fires for callers that ask for it, and slideshow/third-party tools often
// rewrite the SAME path), so this polls the path AND its last-write time. The expensive part - the
// decode - happens HERE, off the render thread, exactly like the wallpaper thumbnails: a 4K PNG is
// far too slow to open on the thread that draws the shell.
static std::atomic<bool>  g_wallSeedReady{false};
static std::atomic<ImU32> g_wallSeedVal{0};
static std::atomic<bool>  g_wallChanged{false};
static void WallWatchThread(){
    std::wstring last; FILETIME lastW{};
    bool first=true;
    while(g_running){
        wchar_t wp[MAX_PATH]={0};
        if(SystemParametersInfoW(SPI_GETDESKWALLPAPER,MAX_PATH,wp,0) && wp[0]){
            FILETIME wt{}; WIN32_FILE_ATTRIBUTE_DATA fad{};
            if(GetFileAttributesExW(wp,GetFileExInfoStandard,&fad)) wt=fad.ftLastWriteTime;
            if(last!=wp || CompareFileTime(&wt,&lastW)!=0){
                bool changed=!first;          // the first poll is just baselining, not a change
                last=wp; lastW=wt; first=false;
                if(changed){
                    ImU32 seed;
                    if(WallpaperSeed(seed)){ g_wallSeedVal.store(seed); g_wallSeedReady.store(true); }
                    g_wallChanged.store(true);
                }
            }
        }
        for(int i=0;i<15 && g_running;i++) Sleep(100);   // ~1.5s, but exits promptly on shutdown
    }
}

// `want` is the file to load. Without it this asks Windows what the wallpaper currently IS, which
// is right at startup and wrong everywhere else: SetWallpaperFile hands the new path to Windows on
// a BACKGROUND thread and starts the transition immediately, so at the moment the transition needs
// the new image, SPI_GETDESKWALLPAPER still reports the OLD one. The wipe therefore animated from
// the old wallpaper TO the old wallpaper, and the picture you actually chose only turned up on the
// NEXT apply - the transition running permanently one wallpaper behind.
static void LoadDeskWallpaper(bool keepOld=false, const std::wstring& want=std::wstring()){
    wchar_t wp[MAX_PATH]={0};
    if(!want.empty()) wcsncpy_s(wp,want.c_str(),_TRUNCATE);
    else if(!SystemParametersInfoW(SPI_GETDESKWALLPAPER,MAX_PATH,wp,0)||!wp[0]) return;
    if(!wp[0]) return;
    // Wallpaper Engine replaces the Windows wallpaper with a still snapshot of its scenes (spanned
    // across every screen, black where it has nothing). That image is not the user's wallpaper: the
    // screens WE draws on show WE itself, and the others should keep the wallpaper chosen in Aether.
    bool substituted=false;
    if(want.empty() && wcsstr(wp,L"WallpaperEngineOverride") && !g_lastWall.empty()){
        std::wstring lw=U82W(g_lastWall);
        if(GetFileAttributesW(lw.c_str())!=INVALID_FILE_ATTRIBUTES){ wcsncpy_s(wp,lw.c_str(),_TRUNCATE); substituted=true; }
    }
    std::vector<uint8_t> px; int w=0,h=0;
    ReadWallStyle();
    if(substituted) g_wallSpan=false;                   // the span style belongs to WE's image, not ours
    if(!DecodeFirstFrame(wp,g_wallSpan?5120:2560,px,w,h)) return;   // a spanned image covers every screen
    ID3D11ShaderResourceView* t=MakeTextureBGRA(px.data(),w,h);
    if(!t) return;
    if(keepOld){ if(g_deskWallOld)g_deskWallOld->Release();
        g_deskWallOld=g_deskWall; g_deskWallOldW=g_deskWallW; g_deskWallOldH=g_deskWallH; }
    else if(g_deskWall) g_deskWall->Release();
    g_deskWall=t; g_deskWallW=w; g_deskWallH=h;
    if(!keepOld){                       // not a transition: this IS the settled image
        ClearWipes();
        if(g_deskBase) g_deskBase->Release();
        g_deskBase=t; g_deskBase->AddRef(); g_deskBaseW=w; g_deskBaseH=h;
    }
    // frosted surround: decode small, blur hard, stretch over the whole screen
    { std::vector<uint8_t> fp; int fw=0,fh=0;
      if(DecodeFirstFrame(wp,220,fp,fw,fh)){
          BoxBlur(fp.data(),fw,fh,7,3);
          ID3D11ShaderResourceView* ft=MakeTextureBGRA(fp.data(),fw,fh);
          if(ft){ if(g_deskFrost)g_deskFrost->Release(); g_deskFrost=ft; g_deskFrostW=fw; g_deskFrostH=fh; } } }
}
// cover-fit UVs for a source image drawn into a destination box (centre crop)
static void CoverUV(int sw,int sh,float bw,float bh,ImVec2& uv0,ImVec2& uv1){
    uv0=ImVec2(0,0); uv1=ImVec2(1,1); if(sw<=0||sh<=0||bw<=0||bh<=0) return;
    float sa=(float)sw/(float)sh, da=bw/bh;
    if(sa>da){ float f=da/sa; uv0.x=(1-f)*0.5f; uv1.x=1-uv0.x; }
    else     { float f=sa/da; uv0.y=(1-f)*0.5f; uv1.y=1-uv0.y; }
}
// ---- Lua plugins (layer 4): defined further down, next to the draw helpers they bind ----
enum { PSURF_DESKTOP=0, PSURF_BAR=1 };
static void PluginsDraw(ImDrawList* dl,int surface,float x,float y,float w,float h);   // fwd
static bool PluginsActive(int surface);                                                // fwd

// The bar-popout's LIVE rect (logical, shared coord space) + radius. The DESKTOP layer reads these to
// carve a rounded NOTCH out of the wallpaper where the popout is, so the wallpaper curves around it and
// the shell looks like it's cutting INTO the desktop (Caelestia) — not a card floating on top.
static bool g_flyCut=false; static float g_flyL=0,g_flyT=0,g_flyR=0,g_flyB=0,g_flyRad=16.0f;
// second notch channel: the app-hover window PREVIEW (DWM thumbnail) also cuts into the desktop so it
// reads as part of the bar (not a floating card). Separate from g_flyCut because the sys-flyout clears
// that every frame it's idle.
static bool g_thumbCut=false; static float g_thumbL=0,g_thumbT=0,g_thumbR=0,g_thumbB=0,g_thumbRad=14.0f;
// third notch channel: the NEW in-bar app preview — a captured-window texture drawn DIRECTLY into the bar
// draw list (like the wifi/bt flyout), so it genuinely SWELLS out of the bar. Own channel so it's never
// wiped by HideThumb() (which owns g_thumbCut for the legacy horizontal-bar DWM path).
static bool g_prevCut=false; static float g_prevL=0,g_prevT=0,g_prevR=0,g_prevB=0,g_prevRad=16.0f;
// Live wallpapers (Wallpaper Engine, etc.) render into explorer's desktop window tree (Progman/WorkerW).
// With no explorer running, that host is gone and WE can't paint — so the transparent live bubble would
// show BLACK. Detect the host; when it's absent, DrawDeskMon falls back to blitting the static wallpaper
// image so the desktop is never black. Flips to live automatically the moment explorer/WE comes up.
static bool DesktopHostPresent(){ return FindWindowW(L"Progman",nullptr)!=nullptr; }
static HWND g_shellHwnd=nullptr;   // the invisible window registered with SetShellWindow (see ClaimShellRoles site)
static bool g_deskTop=false;       // --deskshot: float the desk layer on top to screenshot it
// Keep the desktop layer DIRECTLY above Explorer's desktop and below every app. Windows pins the
// registered shell window to the very bottom, which is why that role now belongs to a separate
// invisible window: the visible desk layer has to be free to sit above Progman, or a live wallpaper
// inside Progman covers it (and a non-shell Explorer's black desktop covered it completely).
static int g_deskReorders=0;
static void PlaceDeskLayer(HWND desk){
    if(!desk) return;
    if(g_deskTop){ SetWindowPos(desk,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE); return; }
    HWND pm=FindWindowW(L"Progman",nullptr);
    if(pm && pm!=GetShellWindow() && !IsHungAppWindow(pm)){
        // Already right where it belongs? Skip INVISIBLE windows in between: Windows parks its IME windows
        // there, so comparing with the immediate neighbour never matched and this re-ordered the desktop on
        // every redraw - measured 25 times a second, each one a single-frame grey flash of the wallpaper.
        { HWND up=GetWindow(pm,GW_HWNDPREV);
          while(up && up!=desk && !IsWindowVisible(up)) up=GetWindow(up,GW_HWNDPREV);
          if(up==desk) return; }
        g_deskReorders++;
        SetWindowPos(desk,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetWindowPos(pm,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);   // Progman under it
        return;
    }
    SetWindowPos(desk,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}
// True when some OTHER program is rendering into Explorer's desktop: Wallpaper Engine parents its
// WPEDesktopDX11Window into the WorkerW that 24H2 creates inside Progman (older builds use a top-level
// WorkerW). Anything in there besides Explorer's own icon view counts.
// Which monitors a live wallpaper is actually drawing on. Read from the wallpaper program's OWN
// windows rather than its settings file, so whatever was picked in Wallpaper Engine's "Choose display"
// - one screen, each screen separately, cloned, or one wallpaper stretched across all of them - is
// followed exactly: a monitor counts as live when a foreign window inside Explorer's desktop covers
// most of it.
static bool g_monLive[16]={};
// A still image picked for a screen Wallpaper Engine is drawing: WE ignores "closeWallpaper -monitor N" (measured:
// its window stays and keeps animating), so Aether stops treating that screen as live and paints the image over it.
static bool g_monStill[16]={};
static HWND g_monLiveHwnd[16]={};      // the foreign window drawing each live monitor (for real-frame captures)
static bool UpdateLiveMonitors(){
    bool now[16]={}; HWND hw[16]={}; long long best[16]={};
    HWND pm=FindWindowW(L"Progman",nullptr);
    if(pm && !g_monsDirty){
        DWORD exPid=0; GetWindowThreadProcessId(pm,&exPid);
        struct C{ DWORD ex; bool* out; HWND* hw; long long* best; } c{exPid,now,hw,best};
        auto scan=[](HWND parent,C& cc){
            EnumChildWindows(parent,[](HWND h,LPARAM lp)->BOOL{
                C* p=(C*)lp; DWORD pid=0; GetWindowThreadProcessId(h,&pid);
                if(pid==p->ex || pid==GetCurrentProcessId() || !IsWindowVisible(h)) return TRUE;
                RECT wr; if(!GetWindowRect(h,&wr)) return TRUE;
                for(size_t i=0;i<g_mons.size() && i<16;i++){
                    const RECT& mr=g_mons[i].rc; RECT in;
                    if(!IntersectRect(&in,&wr,&mr)) continue;
                    long long a=(long long)(in.right-in.left)*(in.bottom-in.top);
                    long long m=(long long)(mr.right-mr.left)*(mr.bottom-mr.top);
                    if(m>0 && a*2>=m){ p->out[i]=true; if(a>p->best[i]){ p->best[i]=a; p->hw[i]=h; } } }
                return TRUE; },(LPARAM)&cc); };
        scan(pm,c);
        HWND w=nullptr;                                   // pre-24H2: the wallpaper WorkerW is top-level
        while((w=FindWindowExW(nullptr,w,L"WorkerW",nullptr))!=nullptr) scan(w,c);
    }
    for(int i=0;i<16;i++) if(g_monStill[i]) now[i]=false;
    bool changed=memcmp(now,g_monLive,sizeof(now))!=0;
    memcpy(g_monLive,now,sizeof(now));
    memcpy(g_monLiveHwnd,hw,sizeof(hw));
    return changed;
}
static bool AnyMonLive(){ for(bool b:g_monLive) if(b) return true; return false; }
static bool LiveWallpaperPresent(){
    HWND pm=FindWindowW(L"Progman",nullptr); if(!pm) return false;
    DWORD exPid=0; GetWindowThreadProcessId(pm,&exPid);
    struct C{ DWORD ex; bool found; } c{exPid,false};
    auto scan=[](HWND parent,C& cc){
        EnumChildWindows(parent,[](HWND h,LPARAM lp)->BOOL{
            C* p=(C*)lp; DWORD pid=0; GetWindowThreadProcessId(h,&pid);
            if(pid!=p->ex && pid!=GetCurrentProcessId() && IsWindow(h)){ p->found=true; return FALSE; }
            return TRUE; },(LPARAM)&cc); };
    scan(pm,c);
    if(!c.found){                                   // pre-24H2: the wallpaper WorkerW is top-level
        HWND w=nullptr;
        while(!c.found && (w=FindWindowExW(nullptr,w,L"WorkerW",nullptr))!=nullptr) scan(w,c);
    }
    return c.found;
}
// Start Explorer purely as the desktop host. Rate-limited because a failed launch would otherwise
// spawn a process every poll, and skipped entirely when a host already exists.
// ---- built-in desktop host: the part of Explorer that live wallpapers actually use -----------------
// Wallpaper Engine (and Lively) never talk to Explorer's icons or taskbar. All they do is:
//   1. FindWindow("Progman")
//   2. send it message 0x052C, which makes Explorer create a "WorkerW" window behind the icons
//   3. find that WorkerW (on Windows 11 24H2 it is a CHILD of Progman) and SetParent their own
//      render window into it.
// Observed on this machine with real Explorer: Progman > { SHELLDLL_DefView, WorkerW > WPEDesktopDX11Window }.
// So Aether provides exactly that window tree itself when it is the shell. No explorer.exe, no black
// Explorer desktop covering the wallpaper, and the host is ours to size and z-order.
static HWND g_hostPm=nullptr, g_hostWorker=nullptr, g_hostDefView=nullptr;
static void HostLayout(){
    if(!g_hostPm) return;
    int w=g_dvs.right-g_dvs.left, h=g_dvs.bottom-g_dvs.top;
    SetWindowPos(g_hostPm,nullptr,g_dvs.left,g_dvs.top,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
    if(g_hostWorker) SetWindowPos(g_hostWorker,HWND_BOTTOM,0,0,w,h,SWP_NOACTIVATE);
}
static LRESULT CALLBACK HostProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_ERASEBKGND){ RECT r; GetClientRect(h,&r); FillRect((HDC)w,&r,(HBRUSH)GetStockObject(BLACK_BRUSH)); return 1; }
    if(m==WM_PAINT){ PAINTSTRUCT ps; BeginPaint(h,&ps); EndPaint(h,&ps); return 0; }
    if(h==g_hostPm && m==0x052C){                      // "spawn the wallpaper WorkerW" - what WE sends
        if(!g_hostWorker || !IsWindow(g_hostWorker)){
            RECT r; GetClientRect(h,&r);
            g_hostWorker=CreateWindowExW(0,L"WorkerW",L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
                                         0,0,r.right,r.bottom,h,nullptr,GetModuleHandleW(nullptr),nullptr);
            if(g_hostWorker) SetWindowPos(g_hostWorker,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        }
        return 0;
    }
    if(h==g_hostPm && m==WM_DISPLAYCHANGE){ HostLayout(); return 0; }
    return DefWindowProcW(h,m,w,l);
}
static void HostAdoptOrphans();   // fwd
static void CreateBuiltInDesktopHost(){
    if(g_hostPm && IsWindow(g_hostPm)) return;
    if(FindWindowW(L"Progman",nullptr)) return;        // a real Explorer desktop exists: never fight it
    HINSTANCE hi=GetModuleHandleW(nullptr);
    for(const wchar_t* cls : { L"Progman", L"WorkerW", L"SHELLDLL_DefView" }){
        WNDCLASSEXW wc={sizeof(wc)}; wc.lpfnWndProc=HostProc; wc.hInstance=hi; wc.lpszClassName=cls;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        RegisterClassExW(&wc);                         // re-registering an existing class just fails harmlessly
    }
    int w=g_dvs.right-g_dvs.left, h=g_dvs.bottom-g_dvs.top;
    g_hostPm=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"Progman",L"Program Manager",
                             WS_POPUP|WS_VISIBLE|WS_CLIPCHILDREN,g_dvs.left,g_dvs.top,w,h,nullptr,nullptr,hi,nullptr);
    if(!g_hostPm) return;
    // Explorer's icon view sits above the WorkerW. Wallpaper programs look for it by class only, so it is
    // present but zero-sized: it must never paint over the wallpaper.
    g_hostDefView=CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,g_hostPm,nullptr,hi,nullptr);
    SendMessageW(g_hostPm,0x052C,0,0);                  // have the WorkerW ready before anyone asks
    // (No adopting from our side: SetParent on Wallpaper Engine's window from this thread deadlocked the
    //  shell - it waits on WE's thread while WE waits on our WorkerW. The TaskbarCreated broadcast below
    //  makes Wallpaper Engine re-attach itself, from its own thread.)
    // Wallpaper programs re-scan for the desktop when the shell announces itself.
    SendNotifyMessageW(HWND_BROADCAST,RegisterWindowMessageW(L"TaskbarCreated"),0,0);
}
// ---- leaving and re-joining the built-in host -------------------------------------------------------
// A window owned by ANOTHER process that is a child of ours is destroyed along with ours. Wallpaper
// Engine's renderer does not survive that: measured, restarting Aether crashed it inside the AMD driver
// (amdxx64.dll, access violation) and WE fell back to its saved wallpaper. So on the way out its window
// is hidden and handed back to the desktop as a top-level window, and on the way in any such orphan is
// adopted back into the new host before Wallpaper Engine even notices the shell went away.
static void HostDetachForeign(){
    if(!g_hostWorker || !IsWindow(g_hostWorker)) return;
    // Done on a helper thread while THIS thread keeps pumping messages: SetParent on another process's
    // window waits for that process, which may itself be waiting on our WorkerW. Measured as a hard hang
    // when done inline. Bounded at 2 s either way, so exit can never stick here.
    auto donePtr=std::make_shared<std::atomic<bool>>(false);
    std::thread([donePtr]{ std::atomic<bool>& done=*donePtr;
    std::vector<HWND> kids;
    EnumChildWindows(g_hostWorker,[](HWND h,LPARAM lp)->BOOL{
        DWORD pid=0; GetWindowThreadProcessId(h,&pid);
        if(pid!=GetCurrentProcessId() && GetParent(h)==g_hostWorker) ((std::vector<HWND>*)lp)->push_back(h);
        return TRUE; },(LPARAM)&kids);
    for(HWND k:kids){
        ShowWindowAsync(k,SW_HIDE);
        LONG st=GetWindowLongW(k,GWL_STYLE);
        SetWindowLongW(k,GWL_STYLE,(st&~WS_CHILD)|WS_POPUP);
        SetParent(k,nullptr);
    }
    done=true; }).detach();
    ULONGLONG t0=GetTickCount64();
    while(!*donePtr && GetTickCount64()-t0<2000){
        MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);
        MSG m; while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){ TranslateMessage(&m); DispatchMessageW(&m); }
    }
}
static void HostAdoptOrphans(){
    if(!g_hostWorker) return;
    struct C{ std::vector<HWND> v; } c;
    EnumWindows([](HWND h,LPARAM lp)->BOOL{
        wchar_t cls[64]={0}; GetClassNameW(h,cls,63);
        if(wcsncmp(cls,L"WPE",3)!=0) return TRUE;             // Wallpaper Engine's render windows
        DWORD pid=0; GetWindowThreadProcessId(h,&pid);
        if(pid!=GetCurrentProcessId()) ((C*)lp)->v.push_back(h);
        return TRUE; },(LPARAM)&c);
    RECT r; GetClientRect(g_hostWorker,&r);
    for(HWND h:c.v){
        LONG st=GetWindowLongW(h,GWL_STYLE);
        SetWindowLongW(h,GWL_STYLE,(st&~WS_POPUP)|WS_CHILD);
        SetParent(h,g_hostWorker);
        RECT wr; GetWindowRect(h,&wr);                         // keep the monitor it was drawing on
        POINT p{wr.left,wr.top}; ScreenToClient(g_hostWorker,&p);
        SetWindowPos(h,nullptr,p.x,p.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
}
static void EnsureDesktopHost(){
    if(!g_isShell) return;
    if(DesktopHostPresent()) return;
    if(!g_hostDesktop){ CreateBuiltInDesktopHost(); return; }   // the default: Aether is its own desktop host
    static ULONGLONG lastTry=0; ULONGLONG now=GetTickCount64();
    if(lastTry && now-lastTry<10000) return;          // one attempt per 10s, no tighter
    lastTry=now;
    AetherShellExec(nullptr,L"open",L"explorer.exe",nullptr,nullptr,SW_HIDE);
}
// One monitor's desktop: the frosted surround, the recessed well and the wallpaper bubble.
// (L,T,MW,MH) is that monitor's rect inside the desk window, in logical px.
// Paint ONLY the bubble frame (the opaque margin ring + rounded interior corners + recessed bevel),
// leaving the interior transparent so the LIVE wallpaper composites through. (x0,y0)-(x1,y1) is the
// interior opening; L,T,R,B the monitor rect. The four corner notches are filled with triangle fans so
// the opening reads as a rounded inset rather than a plain rectangle.
static void DrawLiveBubbleFrame(ImDrawList* dl,float L,float T,float R,float B,
                                float x0,float y0,float x1,float y1,float rnd,int frameA=255){
    // frameA < 255: the frame is tinted glass, so a live wallpaper keeps moving underneath it too
    ImU32 frame = g_darkUI? IM_COL32(9,10,13,frameA) : IM_COL32(226,230,234,frameA);
    dl->AddRectFilled(V(L,T),V(R,y0),frame);      // top margin
    dl->AddRectFilled(V(L,y1),V(R,B),frame);      // bottom margin
    dl->AddRectFilled(V(L,y0),V(x0,y1),frame);    // left margin
    dl->AddRectFilled(V(x1,y0),V(R,y1),frame);    // right margin
    if(rnd>0.5f){    // round the interior corners: fill each square-minus-quarter-disc notch with a fan
        const float PI=3.14159265f;
        // The fan is drawn WITHOUT per-triangle antialiasing: every triangle's fringe overlapped its
        // neighbours at the shared corner point, which is invisible on a solid frame but left a dark star
        // at each corner once the frame became translucent over a live wallpaper. One antialiased
        // stroke along the arc gives the edge back.
        auto notch=[&](float sqx,float sqy,float cxp,float cyp,float a0,float a1){
            const int N=18; ImVec2 prev;
            const ImDrawListFlags fl=dl->Flags; dl->Flags&=~ImDrawListFlags_AntiAliasedFill;
            for(int i=0;i<=N;i++){ float a=a0+(a1-a0)*(i/(float)N);
                ImVec2 p=V(cxp+cosf(a)*rnd, cyp+sinf(a)*rnd);
                if(i>0) dl->AddTriangleFilled(V(sqx,sqy),prev,p,frame);
                prev=p; }
            dl->Flags=fl;
            dl->PathArcTo(V(cxp,cyp),rnd+0.5f,a0,a1,N); dl->PathStroke(WithA(frame,(int)(((frame>>IM_COL32_A_SHIFT)&0xFF)*0.5f)),0,1.0f); };
        notch(x0,y0, x0+rnd,y0+rnd, PI,       1.5f*PI);   // top-left
        notch(x1,y0, x1-rnd,y0+rnd, 0.0f,     -0.5f*PI);  // top-right
        notch(x1,y1, x1-rnd,y1-rnd, 0.0f,      0.5f*PI);  // bottom-right
        notch(x0,y1, x0+rnd,y1-rnd, PI,        0.5f*PI);  // bottom-left
    }
    // recessed bevel: a soft inner shadow on the frame around the opening + a crisp edge + bright lip,
    // so the live wallpaper looks set INTO the frame (the "well" that sells the bubble).
    for(int i=8;i>0;i--){ float e=i*2.0f;
        dl->AddRect(V(x0-e,y0-e),V(x1+e,y1+e),IM_COL32(0,0,0,10),rnd+e,0,e*0.9f); }
    dl->AddRect(V(x0-0.5f,y0-0.5f),V(x1+0.5f,y1+0.5f),IM_COL32(0,0,0,120),rnd,0,1.4f);
    dl->AddRect(V(x0-2.0f,y0-2.0f),V(x1+2.0f,y1+2.0f),g_darkUI?IM_COL32(255,255,255,20):IM_COL32(255,255,255,90),rnd+2.0f,0,1.6f);
}
