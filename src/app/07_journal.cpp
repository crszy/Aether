// Aether - crash-recovery journal, window probes.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =============================================================================================
// CRASH-RECOVERY JOURNAL
// Hiding Explorer's taskbar and re-insetting every monitor's work area are the two changes that
// OUTLIVE this process. If the shell is force-killed (Task Manager "End task", a GPU driver reset,
// a hard power-off) none of our cleanup runs, and the user is left with no taskbar and a dead strip
// down the side of every maximised window — with no idea what did it and no way to undo it. That is
// the "it broke my PC" failure, and the only real fix is to write down what the machine looked like
// BEFORE we touch it, so somebody can always put it back:
//   * we crash in-process        -> the unhandled-exception / terminate / atexit hooks
//   * Aether is launched again   -> RecoveryHeal() runs at startup, before we touch anything
//   * user never launches it again -> a RunOnce entry runs "Aether.exe --recover" at next sign-in
// The journal is deleted the moment everything is back, so a clean run leaves nothing behind.
// =============================================================================================
static const wchar_t* RECKEY  = L"Software\\Aether\\Recovery";
static const wchar_t* RUNONCEK= L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";

static void RecoveryWrite(const std::vector<RECT>& orig){
    HKEY k;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,RECKEY,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)==ERROR_SUCCESS){
        DWORD one=1; RegSetValueExW(k,L"Dirty",0,REG_DWORD,(const BYTE*)&one,sizeof(one));
        if(!orig.empty()) RegSetValueExW(k,L"WorkAreas",0,REG_BINARY,(const BYTE*)orig.data(),
                                         (DWORD)(orig.size()*sizeof(RECT)));
        else RegDeleteValueW(k,L"WorkAreas");
        RegCloseKey(k);
    }
    wchar_t me[MAX_PATH]; GetModuleFileNameW(nullptr,me,MAX_PATH);
    std::wstring c=L"\""; c+=me; c+=L"\" --recover";
    if(RegCreateKeyExW(HKEY_CURRENT_USER,RUNONCEK,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)==ERROR_SUCCESS){
        RegSetValueExW(k,L"AetherRecover",0,REG_SZ,(const BYTE*)c.c_str(),(DWORD)((c.size()+1)*sizeof(wchar_t)));
        RegCloseKey(k); }
}
static void RecoveryClear(){
    HKEY k;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,RECKEY,0,KEY_SET_VALUE,&k)==ERROR_SUCCESS){
        RegDeleteValueW(k,L"Dirty"); RegDeleteValueW(k,L"WorkAreas"); RegCloseKey(k); }
    if(RegOpenKeyExW(HKEY_CURRENT_USER,RUNONCEK,0,KEY_SET_VALUE,&k)==ERROR_SUCCESS){
        RegDeleteValueW(k,L"AetherRecover"); RegCloseKey(k); }
}
// Show every taskbar we did not create ourselves. Ours squats on the Shell_TrayWnd class name to be
// the tray host, so a plain FindWindow would just find us.
// The counterpart to ShowRealTaskbars. Needed because hosting Explorer for the desktop ALSO brings
// its taskbar back, and it can reappear at any time (Explorer restarts itself). Skips our own
// process for the same reason ShowRealTaskbars does: the tray host creates a Shell_TrayWnd of ours.
static void HideExplorerTaskbars(){
    DWORD me=GetCurrentProcessId();
    for(HWND t=nullptr; (t=FindWindowExW(nullptr,t,L"Shell_TrayWnd",nullptr))!=nullptr; ){
        DWORD pid=0; GetWindowThreadProcessId(t,&pid);
        if(pid!=me && IsWindowVisible(t)) ShowWindowAsync(t,SW_HIDE); }
    for(HWND s2=nullptr; (s2=FindWindowExW(nullptr,s2,L"Shell_SecondaryTrayWnd",nullptr))!=nullptr; ){
        DWORD pid=0; GetWindowThreadProcessId(s2,&pid);
        if(pid!=me && IsWindowVisible(s2)) ShowWindowAsync(s2,SW_HIDE); }
}
static void ShowRealTaskbars(){
    DWORD me=GetCurrentProcessId();
    for(HWND t=nullptr; (t=FindWindowExW(nullptr,t,L"Shell_TrayWnd",nullptr))!=nullptr; ){
        DWORD pid=0; GetWindowThreadProcessId(t,&pid);
        if(pid!=me) ShowWindowAsync(t,SW_SHOW); }
    for(HWND s=nullptr; (s=FindWindowExW(nullptr,s,L"Shell_SecondaryTrayWnd",nullptr))!=nullptr; )
        ShowWindowAsync(s,SW_SHOW);
}
// Is a real Aether shell already up? The named mutex is the only reliable cross-process answer:
// FindWindow(L"AetherClass") does NOT work from another process, because an application class is
// registered per-process and the caller cannot resolve the name to an atom (shell classes like
// Shell_TrayWnd only work because Explorer registers them CS_GLOBALCLASS). Getting this wrong meant
// the guard always passed and --recover would have healed the desktop out from under a LIVE shell.
static bool AetherShellRunning(){
    HANDLE h=OpenMutexW(SYNCHRONIZE,FALSE,L"Local\\AetherShell");
    if(!h) return false;
    CloseHandle(h); return true;
}
// Put the machine back the way the journal describes. No-op when there is no journal; safe twice.
static bool RecoveryHeal(){
    DWORD flag=0, sz=sizeof(flag);
    if(RegGetValueW(HKEY_CURRENT_USER,RECKEY,L"Dirty",RRF_RT_REG_DWORD,nullptr,&flag,&sz)!=ERROR_SUCCESS || !flag){
        RecoveryClear(); return false; }
    ShowRealTaskbars();
    std::vector<RECT> orig; DWORD bytes=0;
    if(RegGetValueW(HKEY_CURRENT_USER,RECKEY,L"WorkAreas",RRF_RT_REG_BINARY,nullptr,nullptr,&bytes)==ERROR_SUCCESS
       && bytes>0 && bytes%sizeof(RECT)==0){
        orig.resize(bytes/sizeof(RECT));
        if(RegGetValueW(HKEY_CURRENT_USER,RECKEY,L"WorkAreas",RRF_RT_REG_BINARY,nullptr,orig.data(),&bytes)!=ERROR_SUCCESS)
            orig.clear(); }
    if(!orig.empty()) for(auto& wa:orig) SystemParametersInfoW(SPI_SETWORKAREA,0,&wa,SPIF_SENDCHANGE);
    else {   // journal lost its rects: give every monitor its FULL area back and let the taskbar re-reserve
        EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR h,HDC,LPRECT,LPARAM)->BOOL{
            MONITORINFO mi={sizeof(mi)};
            if(GetMonitorInfoW(h,&mi)) SystemParametersInfoW(SPI_SETWORKAREA,0,&mi.rcMonitor,SPIF_SENDCHANGE);
            return TRUE; },0); }
    RecoveryClear();
    return true;
}

static void SetWindowsTaskbar(bool hide){
    if(hide){
        // Write the journal BEFORE anything is touched, so a crash one instruction later is still
        // recoverable. (Order matters: measure, journal, then change.)
        if(!g_taskbarHidden){
            SystemParametersInfoW(SPI_GETWORKAREA,0,&g_savedWorkArea,0);
            g_savedWorkAreas.clear();
            for(auto& m:g_mons){ MONITORINFO mi={sizeof(mi)};
                HMONITOR h=MonitorFromPoint(POINT{m.rc.left+2,m.rc.top+2},MONITOR_DEFAULTTONEAREST);
                g_savedWorkAreas.push_back(GetMonitorInfoW(h,&mi)? mi.rcWork : m.rc); }
            RecoveryWrite(g_savedWorkAreas);
        }
        HWND tray=FindWindowW(L"Shell_TrayWnd",nullptr);
        if(tray) ShowWindowAsync(tray,SW_HIDE);
        HWND sec=nullptr; while((sec=FindWindowExW(nullptr,sec,L"Shell_SecondaryTrayWnd",nullptr))!=nullptr) ShowWindowAsync(sec,SW_HIDE);
        ApplyWorkAreas();       // maximized/snapped windows land inside each monitor's bubble
    } else {
        ShowRealTaskbars();
        if(g_taskbarHidden){
            if(g_savedWorkAreas.size()==g_mons.size())
                for(auto& wa:g_savedWorkAreas) SystemParametersInfoW(SPI_SETWORKAREA,0,&wa,SPIF_SENDCHANGE);
            else SystemParametersInfoW(SPI_SETWORKAREA,0,&g_savedWorkArea,SPIF_SENDCHANGE);
        }
        RecoveryClear();        // nothing left to undo
    }
    g_taskbarHidden=hide;
}

static std::string ExeDir(){ wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr,p,MAX_PATH); std::wstring w(p); size_t s=w.find_last_of(L"\\/"); std::wstring d=(s==std::wstring::npos)?L"":w.substr(0,s+1); return std::string(d.begin(),d.end()); }
static bool jbool(const std::string& s,const std::string& k){ size_t p=jkey(s,k); if(p==std::string::npos)return false; p=s.find(':',p)+1; while(p<s.size()&&s[p]==' ')p++; return p<s.size()&&s[p]=='t'; }
// string array: ["a","b"] — jarr only handles numbers
static std::vector<std::string> jsarr(const std::string& scope){
    std::vector<std::string> v; size_t p=scope.find('['); if(p==std::string::npos) return v;
    for(++p; p<scope.size() && scope[p]!=']';){
        size_t q=scope.find('"',p); if(q==std::string::npos||q>scope.find(']',p)) break;
        std::string out; ++q;
        for(; q<scope.size() && scope[q]!='"'; ++q){
            if(scope[q]=='\\' && q+1<scope.size()){ ++q; out+=scope[q]; }   // \\ and \" survive
            else out+=scope[q];
        }
        if(!out.empty()) v.push_back(out);
        p=q+1;
    }
    return v;
}
static std::string jesc(const std::string& s){ std::string o; for(char c:s){ if(c=='\\'||c=='"') o+='\\'; o+=c; } return o; }

// ---- module settings owned by the Settings app ----
static bool  g_suppressToasts=true;              // hide Windows' own toast popups (ours replace them)
static bool  g_dnd=false;                        // do not disturb: never auto-pop notifications
static bool  g_nowPlaying=true;                  // pop the "Now Playing" toast on a track change
static bool  g_launchDesc=true;                  // launcher rows show a description line
static int   g_launchMax=8;                      // launcher visible rows
// ---- clipboard history ----------------------------------------------------------------------
// Aether only ever WROTE to the clipboard (calculator result, settings copy, snip) - nothing ever
// recorded it, so the shell had no history at all while every Linux rice ships one (cliphist).
// Windows' own Win+V exists but is a separate surface that looks nothing like the shell.
// Fluent Search's signature trick: the launcher searches what is ALREADY OPEN, not just what can
// be started. Typing a title switches to that window instead of launching a second copy of the app.
// The window list is the taskbar's own (g_dockRaw, one entry per window), so this costs nothing.
static bool  g_launchWindows=true;               // launcher.searchWindows
// ---- launcher look & motion -------------------------------------------------------------------
// Walker (the GTK4 launcher) is themed with CSS and has no transitions at all; the shape of the
// panel and the way it arrives are the two things people actually re-theme, so they are knobs here.
enum { LPOS_BOTTOM=0, LPOS_CENTRE, LPOS_TOP, LPOS_N };
static const char* LPOS_LABEL[LPOS_N]={"Bottom (Caelestia)","Centre","Top"};
enum { LANIM_RISE=0, LANIM_FADE, LANIM_POP, LANIM_DROP, LANIM_NONE, LANIM_N };
static const char* LANIM_LABEL[LANIM_N]={"Rise + hinge","Fade","Pop (scale)","Drop from top","None"};
static int   g_launPos=LPOS_BOTTOM;              // launcher.position
static float g_launWidth=620.0f;                 // launcher.width
static float g_launRound=20.0f;                  // launcher.radius
static int   g_launAnimStyle=LANIM_RISE;         // launcher.animation
static int   g_launOpenMs=230;                   // launcher.openMs
static int   g_launCloseMs=150;                  // launcher.closeMs
static bool  g_launStagger=true;                 // launcher.stagger - rows cascade in
// The panel used to be whatever GlassPanel's global drawer alpha happened to be, so it was always
// the same half-transparent slab. These make it its own surface.
static float g_launOpacity=0.86f;                // launcher.opacity  - panel fill 0..1
static bool  g_launBlur=true;                    // launcher.blur     - frost what is behind it
// Ask DWM to round app windows that opted out of it (see ApplyWindowRound, far below - declared
// here because the config loader runs long before that point).
static bool  g_roundWindows=true;                // windows.roundCorners
// Clipping a window to a rounded REGION is what gave app windows deeper corners than DWM allows -
// and it is also what cut their edges off. A region clips RENDERING, is cached against the frame
// rect, and only refreshed on a periodic sweep, so between a resize and the next sweep the window
// was clipped to the size it USED to be. Testers saw that as "some window edges get cut off".
// DWM's own rounding never clips, so that is the default now and this is opt-in.
static bool  g_deepCorners=false;                // windows.deepCorners
// Desktop icons and Wallpaper Engine are the SAME missing piece: both live in Explorer's desktop
// window tree (Progman/WorkerW). When Aether is the shell nothing ever creates that tree, so there
// are no icons to click and WE has no host to render into. Launching explorer.exe while Progman is
// absent makes it build the shell desktop - the technique Cairo Shell uses - and the taskbar
// takeover that already runs hides the tray it brings with it.
// OFF by default. Tested for real with Aether as the session shell on Windows 11 24H2: Explorer does
// build Progman, but a non-shell Explorer paints its desktop solid black, keeps its icon list hidden,
// and nothing parented into its wallpaper WorkerW shows through - so turning this on just blacked out
// the wallpaper. Kept as an opt-in for builds where Explorer behaves differently.
static bool  g_hostDesktop=false;                // desktop.hostIcons
static float g_liveFrameAlpha=0.72f;             // desktop.live_frame_opacity
static bool g_frameBorn=true;                    // appearance.frame_born_panels
// How deep to round app windows, in pixels. DWM's own rounding is a fixed ~8px and Windows exposes
// no way to change it, so anything deeper has to be a window REGION - and a region is a 1-bit mask,
// so its edge is hard. 0 = leave DWM's rounding alone (smooth but shallow).
static int   g_winRoundPx=36;                    // windows.cornerRadius - matches the bubble
static bool  g_clipEnable=true;                  // clipboard.history
static int   g_clipMax=100;                      // clipboard.maxEntries (pinned rows never evict)
static bool  g_clipPaste=true;                   // clipboard.pasteOnPick - send Ctrl+V after picking
static std::vector<std::string> g_launchDirs;    // extra folders scanned for .exe (Settings/config)
static std::string g_wallDir;                    // legacy single folder (migrated into g_wallFolders)
// ---- wallpaper source + picker settings, ported from the WindowPaper engine ----
static std::vector<std::string> g_wallFolders;   // folders the picker scans
// wallpaper.picker_source: the picker lists EITHER image files OR live wallpapers - never both mixed.
enum { WSRC_IMAGES=0, WSRC_LIVE, WSRC_N };
static int g_wallSource=WSRC_IMAGES;
static bool g_liveBrowsePreview=true;   // wallpaper.live_preview: scrolling the live picker previews on screen
static const char* WSRC_NAMES[]={ "images","live" };
// The three bundled CachyOS wallpapers used to be scanned unconditionally, so they sat in the
// picker forever alongside the folders you actually chose - wallpapers you never put anywhere.
// They are a fallback for a fresh install with nothing configured, so that is when they show.
static bool g_wallBundled=false;                 // wallpaper.bundled - force them in anyway
// The 64 audio bars ringing the album art are ours, not Caelestia's: upstream's media card is a
// clean shape with a single arc on it. So they are a choice rather than a fixture.
static bool g_mdRays=true;                       // dashboard.mediaRays
// Which M3 silhouette the album art is filled into. 16 is M3_COOKIE12 - the scalloped cover in the
// reference. (The literal is unavoidable: this is declared thousands of lines above the M3_* enum,
// and there is a static_assert down there keeping the two honest.)
static int  g_mdMediaShape=16;                   // dashboard.mediaShape
// Two media-card looks, because the two references disagree and both are Caelestia:
//   0 "circle" - the screen recording: a plain circular cover ringed by reactive bars, and no arc.
//                The bumpy edge in the still was those bars, not a shape.
//   1 "shaped" - the still: the cover filled into an M3 silhouette with an M3 Expressive wavy
//                progress arc over the top of it.
static int  g_mdStyle=0;                         // dashboard.mediaStyle
static bool  g_wallRecursive=false;              // waypaper-style recursive scan
static bool  g_wallShowName=true, g_wallShowExt=true;
static int   g_carCW=308, g_carCH=182, g_carNW=196, g_carNH=116, g_carGap=22;   // cover-flow card sizes
// ---- wallpaper picker: same knobs the launcher got --------------------------------------------
// The panel was a fixed 1180x380 slab welded to the bottom edge. Scale multiplies the panel AND the
// cover-flow cards together, so turning it up genuinely fills a big screen rather than stretching a
// small carousel across it.
static int   g_wallPos=0;                        // wallpaper.pickerPos   (LPOS_*)
static float g_wallScale=1.0f;                   // wallpaper.pickerScale 0.6 .. 2.2
static float g_wallRound=24.0f;                  // wallpaper.pickerRadius
static int   g_wallAnim=0;                       // wallpaper.pickerAnim  (LANIM_*)
static float g_wallOpacity=0.86f;                // wallpaper.pickerOpacity
static bool  g_wallBlur=true;                    // wallpaper.pickerBlur
// Immersive: drop the panel, the title and the footer so nothing but the wallpapers is on screen.
// Backdrop: fade a blurred grab of the whole desktop in behind it as the picker opens, so the
// screen visibly settles out of focus instead of a slab just appearing over a sharp desktop.
static bool  g_wallImmersive=false;              // wallpaper.pickerImmersive
static bool  g_wallBackdrop=true;                // wallpaper.pickerBackdrop
// Set the moment a wallpaper is applied FROM the picker, cleared once the picker has finished
// closing. While it is set the full-screen blurred backdrop is not drawn, so the desktop transition
// is visible from its very first frame instead of from behind a snapshot of the old desktop.
static bool  g_wallJustApplied=false;
static bool  g_carShadow=true, g_carHoverZoom=false; static int g_carHoverAmt=8;
static int   g_slideSec=0; static bool g_slideShuffle=true;                     // slideshow
static bool  g_paletteExport=false, g_restoreOnLaunch=false;
static std::string g_postApplyCmd;                                              // {path} hook
static std::string g_transPrevName="slide_right", g_transNextName="slide_left"; // Ctrl+Alt+[ / ]
static std::string g_lastWall;                                                  // last applied wallpaper
// ---- regional / clock (Settings > Region) ----
static bool  g_clock24=false;                    // 24-hour clock everywhere the shell prints a time
static int   g_firstDay=0;                       // 0 = Sunday, 1 = Monday (calendar card)
// ---- wallpaper transition engine (swww parity, ported from WindowPaper) ----
enum class WTrans { Fade, Grow, Outer, WipeLeft, WipeRight, WipeUp, WipeDown,
                    SlideLeft, SlideRight, SlideUp, SlideDown, Zoom,
                    Diagonal, Ripple, Stripes, Any, Random, None, COUNT };
static const char* WTRANS_NAME[]={ "fade","grow","outer","wipe_left","wipe_right","wipe_up","wipe_down",
                                   "slide_left","slide_right","slide_up","slide_down","zoom",
                                   "diagonal","ripple","stripes","any","random","none" };
static WTrans ParseTrans(const std::string& s){
    // accept the swww aliases too
    if(s=="simple") return WTrans::Fade;
    if(s=="center") return WTrans::Grow;
    if(s=="circle") return WTrans::Grow;
    if(s=="circle_inverted") return WTrans::Outer;
    if(s=="wipe")  return WTrans::WipeLeft;
    if(s=="top")   return WTrans::WipeUp;
    if(s=="bottom")return WTrans::WipeDown;
    if(s=="left")  return WTrans::SlideLeft;
    if(s=="right") return WTrans::SlideRight;
    for(int i=0;i<(int)WTrans::COUNT;i++) if(s==WTRANS_NAME[i]) return (WTrans)i;
    return WTrans::Grow;
}
static WTrans g_transCfg=WTrans::Grow;      // what the user picked
static WTrans g_transNow=WTrans::Grow;      // resolved for the running transition
static int    g_transMs=900;                // swww --transition-duration
static bool   g_transPosMouse=true;         // swww --transition-pos mouse|center
static float g_wipe=1.0f; static ImVec2 g_wipeAt=ImVec2(0,0);   // 0..1 progress + origin
// The desk layer spans every monitor, so a wipe origin taken from a panel (which lives on the
// ACTIVE monitor) has to be shifted into the desk window's virtual-screen space.
static ImVec2 DeskFromActive(ImVec2 p){
    return ImVec2(p.x+(g_mx-g_vs.left)/g_uiScale, p.y+(g_my-g_vs.top)/g_uiScale); }
static ImVec2 DeskPt(float fx,float fy){            // fraction of the active monitor
    return DeskFromActive(ImVec2(fx*g_mw/g_uiScale, fy*g_mh/g_uiScale)); }
static ImVec2 DeskCursor(){
    POINT cp; if(!GetCursorPos(&cp)) return DeskPt(0.5f,0.5f);
    return ImVec2((cp.x-g_vs.left)/g_uiScale,(cp.y-g_vs.top)/g_uiScale); }
// resolve the meta types (any / random) to something concrete, swww-style
static WTrans ResolveTrans(WTrans t,bool& randomPos){
    randomPos=false;
    if(t==WTrans::Any){ randomPos=true; return (rand()&1)? WTrans::Grow : WTrans::Outer; }
    if(t==WTrans::Random){
        static const WTrans all[]={WTrans::Fade,WTrans::Grow,WTrans::Outer,
            WTrans::WipeLeft,WTrans::WipeRight,WTrans::WipeUp,WTrans::WipeDown,
            WTrans::SlideLeft,WTrans::SlideRight,WTrans::SlideUp,WTrans::SlideDown,
            WTrans::Zoom,WTrans::Diagonal,WTrans::Ripple,WTrans::Stripes};
        WTrans a=all[rand()%(int)(sizeof(all)/sizeof(all[0]))];
        if(a==WTrans::Grow||a==WTrans::Outer) randomPos=true;
        return a;
    }
    return t;
}
static bool  g_autoDim=false;                    // fade the screen down when there is no input (opt-in: surprised new people)
static int   g_dimAfter=120;                     // seconds of idle before the veil appears
static bool  g_idleLock=false;                   // lock the workstation after a longer idle (opt-in)
static int   g_idleLockAfter=600;                // seconds of idle before locking
static bool  g_idleDisplayOff=false;             // blank the displays after a longer idle (opt-in)
static int   g_idleDisplayAfter=900;             // seconds of idle before the displays go off
static bool  g_idleHibernate=false;              // hibernate after a VERY long idle (opt-in)
static int   g_idleHibernateAfter=3600;          // seconds of idle before hibernating
static bool  g_useLockScreen=false;              // Lock uses our overlay instead of native LockWorkStation
// 0 = Panel (Caelestia's: one container, three columns)
// 1 = Floating (the rice's: M3 blob widgets straight on the blurred wallpaper)
static int   g_lockStyle=0;
static const char* LOCKSTYLE_LABEL[3] = { "Panel", "Floating", "Caelestia (new)" };
static char  g_lockCode[64]={0};                 // optional shell unlock code (works for PIN/MS accounts that LogonUser can't check)
static bool  g_helloOn=true;                     // offer Windows Hello on the lock screen (power.hello)
static float g_dimLevel=0.60f;                   // how far down it fades (0..1)
static float g_rounding=1.0f;

static int SchemeIndex(const std::string& id){ for(int i=0;i<NSCHEMES;i++) if(id==SCHEMES[i].id) return i; return 0; }

static void WriteDefaultConfig(const std::string& path){
    std::ofstream f(path); if(!f)return;
    f<<"{\n"
     <<"  \"appearance\": { \"scheme\": \"caelestia\", \"themeMode\": \"scheme\", \"customAccent\": false, \"accent\": [32,106,96],\n"
     <<"                   \"dynamicColor\": false, \"hideTaskbar\": true, \"rounding\": 1.0, \"opacity\": 0.96, \"uiScale\": 1.0 },\n"
     <<"  \"background\": { \"mode\": \"acrylic\", \"image\": \"\", \"blur\": true, \"opacity\": 0.55, \"wallpaperDir\": \"\",\n"
     <<"                  \"transition\": \"grow\", \"transitionMs\": 900, \"transitionPos\": \"mouse\" },\n"
     <<"  \"desktop\": { \"bubble\": true, \"gap\": 10, \"radius\": 22 },\n"
     <<"  \"bar\": { \"autoHide\": true, \"width\": 48, \"monitors\": \"all\" },\n"
     <<"  \"layout\": {\n"
     <<"    \"bar\":           { \"edge\": \"left\",  \"size\": 48,  \"span\": 1.0,  \"spanMax\": 0,   \"anchor\": 0.5, \"gap\": 10, \"order\": 0, \"visible\": true },\n"
     <<"    \"dashboard\":     { \"edge\": \"top\",   \"size\": 424, \"span\": 0.46, \"spanMax\": 880, \"anchor\": 0.5, \"gap\": 0,  \"order\": 0, \"visible\": true },\n"
     <<"    \"quickSettings\": { \"edge\": \"right\", \"size\": 384, \"span\": 0,    \"spanMax\": 0,   \"anchor\": 1.0, \"gap\": 10, \"order\": 0, \"visible\": true }\n"
     <<"  },\n"
     <<"  \"notifications\": { \"suppressNative\": true, \"doNotDisturb\": false },\n"
     <<"  \"launcher\": { \"descriptions\": true, \"maxResults\": 8 },\n"
     <<"  \"dashboard\": { \"tabs\": [true,true,true,true] },\n"
     <<"  \"sensors\": { \"lhmUrl\": \"http://localhost:8085/data.json\" },\n"
     <<"  \"power\": { \"autoDim\": true, \"dimAfter\": 120, \"dimLevel\": 0.6 },\n"
     <<"  \"displays\": { },\n"
     <<"  \"plugins\": { \"enabled\": true, \"disabled\": [] },\n"
     <<"  \"wallpaper\": { \"folders\": [], \"recursive\": false, \"showName\": true, \"showExt\": true,\n"
     <<"                  \"cardW\": 308, \"cardH\": 182, \"nearW\": 196, \"nearH\": 116, \"cardGap\": 22,\n"
     <<"                  \"depthShadow\": true, \"hoverZoom\": false, \"hoverAmount\": 8,\n"
     <<"                  \"slideshowSec\": 0, \"shuffle\": true, \"paletteExport\": false,\n"
     <<"                  \"restoreOnLaunch\": false, \"postApply\": \"\",\n"
     <<"                  \"prevTransition\": \"slide_right\", \"nextTransition\": \"slide_left\", \"last\": \"\" },\n"
     <<"  \"profile\": { \"name\": \"\" }\n"
     <<"}\n";
}
static std::vector<int> g_calMarks;   // calendar: marked days as YYYYMMDD (persisted)
// Per-day notes, keyed the same way as the marks. A plain click opens the editor for that day; a
// DRAG across cells marks the whole run, which is the "select 3 days and block them out" gesture.
static std::map<int,std::string> g_calNotes;
static int   g_calNoteDay=0;          // ymd being edited, 0 = editor closed
static float g_calNoteAnim=0.0f;
static char  g_calNoteBuf[192]={0};
static bool  g_calDragging=false, g_calDragMoved=false;
static int   g_calDragI0=-1, g_calDragI1=-1;   // cell indices, not dates: a range must follow the
                                               // grid, and ymd is not contiguous across a month end
static std::string CalNote(int ymd){ auto it=g_calNotes.find(ymd); return it==g_calNotes.end()?std::string():it->second; }
// ---- user-authorable dashboard config: config.toml (export a template, hand-edit, import) ----
// The visual tab creator writes config.json; config.toml is a human-friendly layer you can write
// yourself. Export snapshots the current layout (with docs); Import replaces the tabs from the file.
static void SaveTomlLayout(){
    std::ofstream f(ExeDir()+"config.toml"); if(!f) return;
    f<<"# ============================================================================\n"
     <<"# Aether dashboard layout.  Edit this file, then Settings > Dashboard > \"Import\n"
     <<"# config.toml\" (or delete it to keep using the visual editor).\n"
     <<"#\n"
     <<"# Each [[tab]] is one dashboard tab. Under it, each [[tab.widget]] places a widget.\n"
     <<"# Positions are FRACTIONS of the content area: x,y = top-left, w,h = size (0.0-1.0).\n"
     <<"#\n"
     <<"# tab keys:    name (text), icon (0=grid 1=media 2=gauge 3=cloud)\n"
     <<"# widget keys: type, x, y, w, h, and arg (a file path for image/image_fill, or the\n"
     <<"#              text for a text widget)\n"
     <<"#\n"
     <<"# widget types:\n"
     <<"#   pages:        page_dash  page_media  page_perf  page_weather   (full-bleed 1x1)\n"
     <<"#   widgets:      clock  calendar  weather  profile  volume  media\n"
     <<"#   performance:  cpu  gpu  memory  storage  network\n"
     <<"#   custom:       image  image_fill  text        (set arg)\n"
     <<"# ============================================================================\n\n";
    for(const DashTab& t:g_tabs){
        f<<"[[tab]]\n";
        f<<"name = \""<<jesc(t.name)<<"\"\n";
        f<<"icon = "<<t.icon<<"\n";
        if(!t.iconName.empty()) f<<"icon_name = \""<<jesc(t.iconName)<<"\"\n";
        for(const Widget& w:t.widgets){
            f<<"[[tab.widget]]\n";
            f<<"type = \""<<(w.kind>=0&&w.kind<WK_COUNT?WREG[w.kind].id:"text")<<"\"\n";
            f<<"x = "<<w.x<<"\ny = "<<w.y<<"\nw = "<<w.w<<"\nh = "<<w.h<<"\n";
            if(!w.arg.empty()) f<<"arg = \""<<jesc(w.arg)<<"\"\n";
            if(!w.style.empty()) f<<"style = \""<<jesc(w.style)<<"\"\n";
        }
        f<<"\n";
    }
}
static bool LoadTomlLayout(){
    std::ifstream f(ExeDir()+"config.toml"); if(!f) return false;
    auto trim=[](std::string s){ size_t a=s.find_first_not_of(" \t\r\n"); if(a==std::string::npos) return std::string();
        size_t b=s.find_last_not_of(" \t\r\n"); return s.substr(a,b-a+1); };
    auto unq=[&](std::string v){ v=trim(v);
        if(v.size()>=2&&v.front()=='"'&&v.back()=='"'){ std::string o; for(size_t i=1;i+1<v.size();i++){
            if(v[i]=='\\'&&i+2<v.size()){ o+=v[i+1]; i++; } else o+=v[i]; } return o; } return v; };
    std::vector<DashTab> tabs; DashTab* ct=nullptr; Widget* cw=nullptr; std::string line;
    while(std::getline(f,line)){
        std::string s=trim(line); if(s.empty()||s[0]=='#') continue;
        if(s=="[[tab]]"){ tabs.push_back(DashTab{}); ct=&tabs.back(); ct->icon=0; ct->builtin=false; cw=nullptr; continue; }
        if(s=="[[tab.widget]]"||s=="[[widget]]"){ if(!ct) continue; ct->widgets.push_back(Widget{}); cw=&ct->widgets.back();
            cw->kind=WK_TEXT; cw->x=cw->y=0.0f; cw->w=cw->h=0.3f; continue; }
        size_t eq=s.find('='); if(eq==std::string::npos) continue;
        std::string k=trim(s.substr(0,eq)), v=trim(s.substr(eq+1));
        if(cw){
            if(k=="type"){ int ki=WKindFromId(unq(v)); if(ki>=0) cw->kind=ki; }
            else if(k=="x") cw->x=(float)atof(v.c_str());
            else if(k=="y") cw->y=(float)atof(v.c_str());
            else if(k=="w") cw->w=(float)atof(v.c_str());
            else if(k=="h") cw->h=(float)atof(v.c_str());
            else if(k=="arg") cw->arg=unq(v);
            else if(k=="style") cw->style=unq(v);
        } else if(ct){
            if(k=="name") ct->name=unq(v);
            else if(k=="icon") ct->icon=atoi(v.c_str());
            else if(k=="icon_name") ct->iconName=unq(v);
            else if(k=="builtin") ct->builtin=(unq(v)=="true");
        }
    }
    if(tabs.empty()) return false;
    for(auto& t:tabs) if(t.widgets.empty()) t.widgets.push_back({WK_PAGE_DASH,0,0,1,1,""});
    g_tabs=std::move(tabs); if(g_tab>=(int)g_tabs.size()) g_tab=0;
    return true;
}
// ---------------------------------------------------------------------------------------------
// BAR ITEM REGISTRY — the bar used to be a hardcoded sequential procedure: DrawBarOn walked a
// fixed order of icons at absolute offsets grown from the top edge (logo, workspaces, apps) and
// from the bottom edge (power, cluster, clock, calendar, tray) at once, and `item++` existed only
// to stagger the entrance animation. That made "hide the mic", "put the clock first" or "centre
// the strip" impossible without editing the draw code. Now the bar is a LIST: an ordered, visible-
// flagged registry the user owns, laid out by a measure-then-place pass (BarLayout below).
//
// SPACER is the piece that makes this reproduce the old look exactly. The historic layout is not
// "spread" — it is one FLEXIBLE GAP sitting between the app list and the tray strip, with
// everything above packed to the top and everything below packed to the bottom. Modelling that gap
// as a real, movable item (waybar/polybar do the same) means the default order renders pixel-wise
// like the old hardcoded bar, while the user can move, delete or add gaps to get any arrangement.
enum BarItemId {
    BIT_LOGO=0, BIT_FILES, BIT_WORKSPACES, BIT_WINDOWINFO, BIT_APPS, BIT_SPACER1, BIT_PLUGINS,
    BIT_TRAY, BIT_CALENDAR, BIT_CLOCK, BIT_NOTIF, BIT_ETH, BIT_WIFI, BIT_BT, BIT_THEME, BIT_MIC,
    BIT_AUDIO, BIT_BATT, BIT_POWER, BIT_SPACER2, BIT_SPACER3,
    // Appended, never inserted: a saved layout stores each item's KEY, but BarItemsRepair places a
    // missing one at its FACTORY POSITION, so moving an existing id would shuffle everyone's bar.
    BIT_CUSTOM1, BIT_CUSTOM2, BIT_CUSTOM3, BIT_CUSTOM4, BIT_CUSTOM5, BIT_CUSTOM6,
    BIT_COUNT
};
#define BIT_IS_CUSTOM(id) ((id)>=BIT_CUSTOM1 && (id)<=BIT_CUSTOM6)
// pill: 0 = bare (no capsule). Non-zero groups CONSECUTIVE visible items onto one rounded capsule,
// which is how the two historic pills (workspaces+apps, and the system cluster) are reproduced —
// and it means a user who drags the clock into the cluster gets it inside that pill automatically.
enum { PILL_NONE=0, PILL_TOP=1, PILL_SYS=2 };
struct BarItemDef {
    const char*  key;      // stable config token — persisted instead of the enum, so reordering
                           // the enum can never silently scramble a saved layout
    const char*  label;    // Settings row
    const char*  desc;     // Settings sub-label
    int          pill;
    bool         vert;     // offered on a left/right bar
    bool         horiz;    // offered on a top/bottom bar
    bool         defOn;    // in the factory layout
};
// Declaration order here IS the factory order.
static const BarItemDef BAR_ITEMS[BIT_COUNT]={
/*LOGO      */ {"logo",        "Logo / launcher",   "Opens the app launcher",                                  PILL_NONE,true ,true ,true },
/*FILES     */ {"files",       "Files",             "Opens the file manager",                                  PILL_NONE,false,true ,true },
/*WORKSPACES*/ {"workspaces",  "Workspaces",        "Virtual desktop dots (hidden when there is only one)",    PILL_TOP ,true ,true ,true },
/*WINDOWINFO*/ {"windowinfo",  "Focused window",    "Title of the window in front (runs down a side bar)",     PILL_NONE,true ,true ,true },
/*APPS      */ {"apps",        "Running apps",      "Task buttons for open windows",                           PILL_NONE,true ,true ,true },
/*SPACER1   */ {"spacer1",     "Flexible space",    "Soaks up the leftover room; move it to push items apart", PILL_NONE,true ,true ,true },
/*PLUGINS   */ {"plugins",     "Plugins",           "Lua plugins that draw on the bar surface",                PILL_NONE,true ,false,true },
/*TRAY      */ {"tray",        "System tray",       "Real Windows tray icons",                                 PILL_NONE,true ,true ,true },
/*CALENDAR  */ {"calendar",    "Calendar",          "Opens the dashboard on the Calendar tab",                 PILL_NONE,true ,true ,true },
/*CLOCK     */ {"clock",       "Clock",             "Time and date",                                           PILL_NONE,true ,true ,true },
/*NOTIF     */ {"notif",       "Notifications",     "Bell with an unread dot (hidden when there is nothing)",  PILL_SYS ,true ,true ,true },
/*ETH       */ {"ethernet",    "Ethernet",          "Wired network status and flyout",                         PILL_SYS ,true ,true ,true },
/*WIFI      */ {"wifi",        "Wi-Fi",             "Wireless status and network flyout",                      PILL_SYS ,true ,true ,true },
/*BT        */ {"bluetooth",   "Bluetooth",         "Bluetooth status and device flyout",                      PILL_SYS ,true ,true ,true },
/*THEME     */ {"theme",       "Light / dark",      "Toggles the colour scheme",                               PILL_SYS ,true ,true ,true },
/*MIC       */ {"mic",         "Microphone",        "Mute toggle (hidden when there is no mic)",               PILL_SYS ,true ,true ,true },
/*AUDIO     */ {"audio",       "Volume",            "Opens the volume mixer",                                  PILL_SYS ,true ,true ,true },
/*BATT      */ {"battery",     "Battery",           "Charge level (hidden on a desktop)",                      PILL_SYS ,true ,true ,true },
/*POWER     */ {"power",       "Power",             "Session menu: lock, sleep, restart",                      PILL_NONE,true ,true ,true },
/*SPACER2   */ {"spacer2",     "Flexible space 2",  "A second gap, off by default",                            PILL_NONE,true ,true ,false},
/*SPACER3   */ {"spacer3",     "Flexible space 3",  "A third gap, off by default",                             PILL_NONE,true ,true ,false},
/*CUSTOM1   */ {"custom1",     "Custom 1",          "Yours - see config\baritems.toml",                        PILL_NONE,true ,true ,false},
/*CUSTOM2   */ {"custom2",     "Custom 2",          "Yours - see config\baritems.toml",                        PILL_NONE,true ,true ,false},
/*CUSTOM3   */ {"custom3",     "Custom 3",          "Yours - see config\baritems.toml",                        PILL_NONE,true ,true ,false},
/*CUSTOM4   */ {"custom4",     "Custom 4",          "Yours - see config\baritems.toml",                        PILL_NONE,true ,true ,false},
/*CUSTOM5   */ {"custom5",     "Custom 5",          "Yours - see config\baritems.toml",                        PILL_NONE,true ,true ,false},
/*CUSTOM6   */ {"custom6",     "Custom 6",          "Yours - see config\baritems.toml",                        PILL_NONE,true ,true ,false},
};
static inline bool BarIsSpacer(int id){ return id==BIT_SPACER1||id==BIT_SPACER2||id==BIT_SPACER3; }

// content alignment along the bar, used only when NO spacer is visible (a spacer already decides
// where the slack goes, and honouring both at once produces layouts nobody asked for)
enum { BALIGN_START=0, BALIGN_CENTER, BALIGN_END, BALIGN_SPREAD };
struct BarItemCfg{ int id; bool on; };
static std::vector<BarItemCfg> g_barItems;      // user's order + visibility (vertical AND horizontal)
static int  g_barAlign = BALIGN_START;
// Caelestia groups related bar items behind a lighter rounded panel (workspaces+apps, and the
// system cluster). Only the VERTICAL bar draws them, which is why they appear the moment the bar
// moves to a side edge. They are a grouping device, so a group of ONE is drawn without one - a box
// around a single icon reads as a smudge, not as grouping.
static bool g_barPills = true;
// ---- the bar's own colours (bar.custom_colours + bar.colour_*) ----
// The scheme drives every surface in the shell at once, which is right for a theme and wrong when
// all you want is a taskbar that does not match the rest. Empty means "follow the scheme".
static bool        g_barTheme = false;
static std::string g_barColPanel, g_barColInk, g_barColAccent;
// Set only while the bar is being drawn with its own colours. The frame material paints a panel's
// BODY, and over a live wallpaper it uses a fixed near-black tint rather than anything from the
// palette - so without this the override recoloured the bar's pills and left its background alone.
static ImU32       g_frameTint = 0;               // 0 = the frame's usual colour

// ---- icon set: hand-drawn Aether marks, or a real Linux icon theme -----------------------------
// The user runs CachyOS (KDE/Breeze) on the other side of the dual boot and wants the shell to use
// the SAME marks - "their power icon, wifi icon, lock icon etc". Every hand-drawn glyph below now
// asks this layer first and only draws itself if the theme has no answer, so switching sets is one
// setting and no call site changes.
// Appended rather than inserted: the saved config stores the INDEX, so renumbering the existing
// three would silently change everyone's icon set on upgrade.
enum { ICONSET_BREEZE=0, ICONSET_ADWAITA, ICONSET_AETHER, ICONSET_MATERIAL, ICONSET_N };
static int g_iconSet = ICONSET_MATERIAL;
static const char* ICONSET_KEY[ICONSET_N]={"breeze","adwaita","aether","material"};
static const char* ICONSET_LABEL[ICONSET_N]={"Breeze (CachyOS / KDE)","Adwaita (GNOME)","Aether (hand-drawn)","Material Symbols (Caelestia)"};


// ---- system tray: collapsed behind a chevron (Windows' own overflow model) --------------------
// The bar used to lay every tray icon out inline, so the tray was not a TRAY at all - just a row of
// icons taking bar space. With collapse on, the bar shows a chevron plus whatever the user promoted
// to "always show"; everything else lives in a flyout that the chevron opens.
static bool g_trayCollapse = true;              // false restores the old always-inline row
static std::vector<std::string> g_trayShown;    // TrayKey()s the user promoted into the bar itself

// Rebuild the factory list. Also used to repair a config that has drifted.
static void BarItemsDefault(){
    g_barItems.clear();
    for(int i=0;i<BIT_COUNT;i++) g_barItems.push_back({i,BAR_ITEMS[i].defOn});
}
// Any id missing from the loaded config (a build that adds a new bar item, or a hand-edited
// config.json) is appended at its FACTORY position rather than dumped at the end, so an upgrade
// puts a new control where it was designed to go instead of after the power button.
static void BarItemsRepair(){
    std::vector<bool> seen(BIT_COUNT,false);
    std::vector<BarItemCfg> out;
    for(auto& c:g_barItems){                       // drop junk + duplicates
        if(c.id<0||c.id>=BIT_COUNT||seen[c.id]) continue;
        seen[c.id]=true; out.push_back(c); }
    for(int i=0;i<BIT_COUNT;i++){
        if(seen[i]) continue;
        size_t at=out.size();                      // find the first later factory id we already have
        for(size_t k=0;k<out.size();k++) if(out[k].id>i){ at=k; break; }
        out.insert(out.begin()+at,{i,BAR_ITEMS[i].defOn}); }
    g_barItems=std::move(out);
}
