// Aether - the dock / running-app list, window rounding, dock icons.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ================================================================= dock (bottom taskbar overlay)
static IDXGIFactory2* g_fac=nullptr;
static ImFontAtlas*   g_atlas=nullptr;
static ImGuiContext*  g_ctxDrawer=nullptr;
static HWND g_dockHwnd=nullptr; static IDXGISwapChain1* g_dockSc=nullptr; static ID3D11RenderTargetView* g_dockRtv=nullptr;
static IDCompositionTarget* g_dockTgt=nullptr; static IDCompositionVisual* g_dockVis=nullptr; static ImGuiContext* g_ctxDock=nullptr;
static float g_dockReveal=0.0f; static RECT g_dockRect={0,0,0,0}; static ID3D11ShaderResourceView* g_dockAcrylic=nullptr;
static const int DOCKWINH=220;   // tall enough for magnified icons + hover labels above them
struct DockApp{ HWND hwnd; ID3D11ShaderResourceView* icon; ID3D11ShaderResourceView* gray=nullptr; HMONITOR mon; bool minimized; std::string title;
                unsigned long long seq; std::vector<HWND> wins; std::wstring key;
                bool pinned=false; bool running=false; bool ownsIcon=true; std::wstring exe; };
static std::vector<DockApp> g_dockApps;
// drag-to-reorder + right-click context menu state (see DrawDock)
static bool         g_dockDragging=false;   static std::wstring g_dockDragExe;   static int g_dockPress=-1; static float g_dockPressMX=0;
static bool         g_dockMenuOpen=false;   static ImVec2 g_dockMenuPos;
static std::wstring g_dockMenuExe;          static bool g_dockMenuPinned=false, g_dockMenuRunning=false;
static std::vector<HWND> g_dockMenuWins;
// stable taskbar order: each window keeps the slot it FIRST appeared in, so activating one (which
// changes its z-order) no longer makes the icons reshuffle/"teleport" on the next refresh.
static std::unordered_map<HWND,unsigned long long> g_dockSeen;
static unsigned long long g_dockSeq=0;
static std::vector<DockApp> g_dockRaw;              // one entry per window, before grouping
static std::wstring ProcExe(HWND h){
    DWORD pid=0; GetWindowThreadProcessId(h,&pid);
    HANDLE pr=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid); std::wstring exe;
    if(pr){ wchar_t path[MAX_PATH]={0}; DWORD n=MAX_PATH;
        if(QueryFullProcessImageNameW(pr,0,path,&n)) exe=path;
        CloseHandle(pr); }
    for(auto&c:exe)c=towlower(c); return exe;
}

static ID3D11ShaderResourceView* IconTex(HICON ico,int sz,bool gray=false){
    if(!ico)return nullptr;
    HDC scr=GetDC(nullptr),mem=CreateCompatibleDC(scr);
    BITMAPINFO bi={}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=sz; bi.bmiHeader.biHeight=-sz; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr; HBITMAP bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0); HGDIOBJ old=SelectObject(mem,bmp);
    memset(bits,0,(size_t)sz*sz*4); DrawIconEx(mem,0,0,ico,sz,sz,0,nullptr,DI_NORMAL);
    uint8_t* px=(uint8_t*)bits; bool anyA=false; for(int i=0;i<sz*sz;i++)if(px[i*4+3]){anyA=true;break;}
    if(!anyA)for(int i=0;i<sz*sz;i++){uint8_t b=px[i*4],g=px[i*4+1],r=px[i*4+2];px[i*4+3]=(b|g|r)?255:0;}
    if(gray)for(int i=0;i<sz*sz;i++){ uint8_t b=px[i*4],g=px[i*4+1],r=px[i*4+2];   // Caelestia-style monochrome bar glyphs
        int l=(r*77+g*151+b*28)>>8; l=190+(l*65>>8);                                // luminance, lifted to a light grey
        px[i*4]=px[i*4+1]=px[i*4+2]=(uint8_t)std::min(255,l); }
    ID3D11ShaderResourceView* t=MakeTextureBGRA(bits,sz,sz);
    SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,scr); return t;
}
// pull an app icon straight from an exe on disk (used for pinned apps that aren't running)
static ID3D11ShaderResourceView* IconTexTrim(HICON,int);   // fwd - defined with the switcher icons
static ID3D11ShaderResourceView* LoadExeIcon(const std::wstring& exe){
    if(exe.empty()) return nullptr;
    // PINNED apps came through SHGFI_LARGEICON, which is 32px - blown up to the 46px default icon
    // size that is visibly pixellated, and it is why the bar looked soft out of the box while
    // RUNNING apps (which already go through GetAppIconHi) looked sharp. Ask the shell's 256px
    // jumbo list first, exactly as the running-app path does, and keep 32px only as a fallback.
    { SHFILEINFOW ji{};
      if(SHGetFileInfoW(exe.c_str(),0,&ji,sizeof(ji),SHGFI_SYSICONINDEX)){
          IImageList* il=nullptr;
          if(SUCCEEDED(SHGetImageList(SHIL_JUMBO,IID_IImageList,(void**)&il)) && il){
              HICON ic=nullptr; il->GetIcon(ji.iIcon,ILD_TRANSPARENT,&ic); il->Release();
              if(ic){ ID3D11ShaderResourceView* t=IconTexTrim(ic,256); DestroyIcon(ic); if(t) return t; }
          } } }
    SHFILEINFOW fi{};
    if(SHGetFileInfoW(exe.c_str(),0,&fi,sizeof(fi),SHGFI_ICON|SHGFI_LARGEICON) && fi.hIcon){
        ID3D11ShaderResourceView* t=IconTex(fi.hIcon,48,false); DestroyIcon(fi.hIcon); return t; }
    HICON ic=ExtractIconW(GetModuleHandleW(nullptr),exe.c_str(),0);
    if(ic && ic!=(HICON)1){ ID3D11ShaderResourceView* t=IconTex(ic,48,false); DestroyIcon(ic); return t; }
    return nullptr;
}
static ID3D11ShaderResourceView* GetAppIcon(HWND h, ID3D11ShaderResourceView** grayOut=nullptr){
    // WM_GETICON is a SEND: it runs in the target app's message loop, so an app that is not pumping
    // costs the full timeout, here twice over. The class icon below needs no round trip at all and
    // is right for almost every app, so a window in the penalty box simply skips to it.
    HICON ic=nullptr;
    const bool ask = ProbeAllowed(h) && !IsHungAppWindow(h);
    if(ask && !SendMessageTimeoutW(h,WM_GETICON,ICON_BIG,0,SMTO_ABORTIFHUNG,60,(PDWORD_PTR)&ic)) ProbeFailed(h);
    if(!ic) ic=(HICON)GetClassLongPtrW(h,GCLP_HICON);
    if(!ic && ask && ProbeAllowed(h) &&
       !SendMessageTimeoutW(h,WM_GETICON,ICON_SMALL2,0,SMTO_ABORTIFHUNG,60,(PDWORD_PTR)&ic)) ProbeFailed(h);
    if(!ic) ic=(HICON)GetClassLongPtrW(h,GCLP_HICONSM);
    if(!ic) ic=LoadIconW(nullptr,IDI_APPLICATION);
    if(grayOut) *grayOut=IconTex(ic,48,true);          // monochrome variant for the bar's top pill
    return IconTex(ic,48,false);
}
static BOOL CALLBACK DockEnum(HWND h,LPARAM){
    if(!IsWindowVisible(h)||GetWindowTextLengthW(h)==0)return TRUE;
    if(GetWindowLongW(h,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)return TRUE;
    int ck=0; if(SUCCEEDED(DwmGetWindowAttribute(h,DWMWA_CLOAKED,&ck,sizeof(ck)))&&ck)return TRUE;
    HWND root=GetAncestor(h,GA_ROOTOWNER),walk=nullptr,tw=root; while(tw!=walk){walk=tw;tw=GetLastActivePopup(walk);if(IsWindowVisible(tw))break;} if(walk!=h)return TRUE;
    if(h==g_hwnd||h==g_dockHwnd)return TRUE;
    { DWORD pid=0; GetWindowThreadProcessId(h,&pid); if(pid==GetCurrentProcessId()) return TRUE; }
    DockApp a; a.hwnd=h; a.icon=nullptr;   // icon fetched once per app group in RefreshDock
    a.mon=MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST);
    a.minimized=IsIconic(h)!=FALSE;
    // Explorer parks minimized windows off-screen; with no Explorer, Windows falls back to the
    // 3.1-era behaviour of tiling their title bars at the bottom-left of the desktop. As the shell
    // that job is ours: push the minimized position off-screen so they actually disappear.
    if(a.minimized && g_isShell && WindowResponsive(h)){
        WINDOWPLACEMENT wp{sizeof(wp)};
        if(GetWindowPlacement(h,&wp) && (wp.ptMinPosition.x!=-32000 || wp.ptMinPosition.y!=-32000)){
            wp.ptMinPosition.x=-32000; wp.ptMinPosition.y=-32000;
            wp.flags|=WPF_SETMINPOSITION;
            wp.showCmd=SW_SHOWMINIMIZED;          // keep it minimized; only move where that is
            SetWindowPlacement(h,&wp);
        }
        // SetWindowPlacement only decides where it minimises NEXT time, so anything already
        // parked on the desktop has to be shoved off-screen by hand.
        RECT wr;
        if(GetWindowRect(h,&wr) && wr.left>-30000){
            SetWindowPos(h,nullptr,-32000,-32000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);
        }
    }
    { wchar_t t[256]={0}; GetWindowTextW(h,t,255); a.title=W2U8(t); }
    // keep the slot this window first appeared in (stable taskbar order)
    auto it=g_dockSeen.find(h);
    a.seq = (it!=g_dockSeen.end())? it->second : (g_dockSeen[h]=g_dockSeq++);
    a.key = ProcExe(h);                              // group key = the app's exe
    a.wins.push_back(h);
    g_dockRaw.push_back(std::move(a)); return TRUE;
}
// ---- "Mica for everyone": apply a DWM system backdrop (Mica/Acrylic/Tabbed) to every app window ----
// This is exactly what MicaForEveryone does — DwmSetWindowAttribute(SYSTEMBACKDROP_TYPE) + dark titlebar.
// Works on Win11 22621+ (this box is 24H2). Some apps that paint an opaque background won't show it;
// that's the same limitation MicaForEveryone has. Reversible: kind 0 (auto) restores each app's default.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
// ---- rounded corners on APP windows -----------------------------------------------------------
// Windows 11 rounds top-level windows itself, but only ones that let it: anything drawing a custom
// frame - Roblox, a lot of Electron apps, terminals - comes out with hard square corners, which is
// what reads as "boxy" sitting inside the shell's rounded bubble.
// DWMWA_WINDOW_CORNER_PREFERENCE asks DWM to round it regardless, and because DWM composites the
// result the edge is properly antialiased. SetWindowRgn would also round any window and to ANY
// radius, but a region is a 1-bit mask with no antialiasing at all - it would put back exactly the
// blocky corners the fringe work just removed - so this deliberately takes DWM's radius instead.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
enum { AE_WCP_DEFAULT=0, AE_WCP_DONOTROUND=1, AE_WCP_ROUND=2, AE_WCP_ROUNDSMALL=3 };

static std::unordered_map<HWND,DWORD> g_roundPrefSent;   // what DWM was last told, per window (+1)
static void ApplyWindowRound(HWND h){
    if(!h||!IsWindow(h)) return;
    bool round=g_roundWindows;
    if(round){
        // Only real app frames get forced round. A borderless popup is a game, a video player gone
        // full-screen, a splash screen or another app's overlay - Windows leaves those square, and
        // forcing ROUND on them made DWM draw its 1px border along every screen edge over full-screen
        // games (tester report; reproduced - 18/18 edge samples showed the line, 0/18 once the window
        // was handed back to DWM's default). A framed window that fills its monitor is left alone too.
        LONG st=GetWindowLongW(h,GWL_STYLE);
        if(!(st&WS_CAPTION) && !(st&WS_THICKFRAME)) round=false;
        else {
            RECT fr{}; if(FAILED(DwmGetWindowAttribute(h,DWMWA_EXTENDED_FRAME_BOUNDS,&fr,sizeof(fr)))) GetWindowRect(h,&fr);
            MONITORINFO mi{sizeof(mi)};
            if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&mi) &&
               fr.left<=mi.rcMonitor.left && fr.top<=mi.rcMonitor.top &&
               fr.right>=mi.rcMonitor.right && fr.bottom>=mi.rcMonitor.bottom) round=false;
        }
    }
    DWORD pref = round ? AE_WCP_ROUND : AE_WCP_DEFAULT;   // DEFAULT hands the choice back
    // Re-decided on every dock refresh, because a window's shape changes after it appears (a game
    // going borderless full-screen kept its forced corners and border line). DWM is only told when
    // the answer actually changes, so the steady state costs a style read per window.
    auto& last=g_roundPrefSent[h];
    if(last==pref+1) return;                                // +1 so a fresh entry (0) never matches
    last=pref+1;
    DwmSetWindowAttribute(h,DWMWA_WINDOW_CORNER_PREFERENCE,&pref,sizeof(pref));
}

// Deeper corners than DWM will give, by clipping the window to a rounded region.
// Caveats, all of them deliberate:
//  * a region is 1-bit, so this edge is NOT antialiased - it is crisper than the shell's own arcs.
//    That is the trade for matching the bubble's radius; DWM is the smooth-but-shallow alternative.
//  * region coordinates are relative to the WINDOW rect, but the visible frame is inset by the
//    invisible resize border, so rounding the window rect would put the arc out in dead space.
//    The extended frame bounds are what the user actually sees.
//  * maximised and full-screen windows are left square, exactly as Windows itself does.
static std::unordered_map<HWND,RECT> g_rgnApplied;   // last frame we shaped, per window

static void ApplyWindowRegion(HWND h){
    if(!h||!IsWindow(h)) return;
    if(IsHungAppWindow(h)) return;                         // SetWindowRgn waits on the window
    RECT wr{}; if(!GetWindowRect(h,&wr)) return;
    const bool off = !g_deepCorners || (g_winRoundPx<=0) || IsZoomed(h) || IsIconic(h);
    if(off){
        auto it=g_rgnApplied.find(h);
        if(it!=g_rgnApplied.end() && WindowResponsive(h)){ SetWindowRgn(h,nullptr,TRUE); g_rgnApplied.erase(it); }
        return;
    }
    RECT fr{}; if(FAILED(DwmGetWindowAttribute(h,DWMWA_EXTENDED_FRAME_BOUNDS,&fr,sizeof(fr)))) fr=wr;
    // a window filling its whole monitor is effectively full-screen: leave it alone
    if(HMONITOR mon=MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST)){
        MONITORINFO mi{sizeof(mi)};
        if(GetMonitorInfoW(mon,&mi) &&
           fr.left<=mi.rcMonitor.left && fr.top<=mi.rcMonitor.top &&
           fr.right>=mi.rcMonitor.right && fr.bottom>=mi.rcMonitor.bottom){
            auto it=g_rgnApplied.find(h);
            if(it!=g_rgnApplied.end() && WindowResponsive(h)){ SetWindowRgn(h,nullptr,TRUE); g_rgnApplied.erase(it); }
            return; } }

    auto it=g_rgnApplied.find(h);
    if(it!=g_rgnApplied.end() && EqualRect(&it->second,&fr)) return;   // unchanged: nothing to redo

    int l=fr.left-wr.left, t=fr.top-wr.top, r=fr.right-wr.left, b=fr.bottom-wr.top;
    if(r-l<8 || b-t<8) return;
    int d=std::clamp(g_winRoundPx,2,64)*2;                 // CreateRoundRectRgn takes ellipse w/h
    if(!WindowResponsive(h)) return;
    HRGN rgn=CreateRoundRectRgn(l,t,r+1,b+1,d,d);
    if(!rgn) return;
    SetWindowRgn(h,rgn,TRUE);                              // the window owns it now - do not delete
    g_rgnApplied[h]=fr;
}
// A region that only refreshes on a 2s sweep is a window clipped to its old size for up to 2s.
// EVENT_OBJECT_LOCATIONCHANGE fires on every move and resize, so the shape follows the drag.
static HWINEVENTHOOK g_locHook=nullptr;
static void CALLBACK AeLocationEvt(HWINEVENTHOOK,DWORD ev,HWND h,LONG idObj,LONG idChild,DWORD,DWORD){
    if(ev!=EVENT_OBJECT_LOCATIONCHANGE || idObj!=OBJID_WINDOW || idChild!=CHILDID_SELF) return;
    if(!g_deepCorners || !h) return;
    ApplyWindowRegion(h);
}
static void SyncDeepCornerHook(){
    if(g_deepCorners && !g_locHook)
        g_locHook=SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE,
                                  nullptr,AeLocationEvt,0,0,
                                  WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    else if(!g_deepCorners && g_locHook){ UnhookWinEvent(g_locHook); g_locHook=nullptr; }
}
// The taskbar list is not the whole desktop: it deliberately drops overlays, owned windows and UWP
// frames, and those are exactly the ones that were still showing square corners (Discord's game
// overlay sitting on top of Roblox, the Settings frame). Rounding is a purely visual thing, so it
// wants every visible top-level window, not just the ones worth a taskbar button.
static BOOL CALLBACK RgnEnum(HWND h,LPARAM){
    if(!IsWindowVisible(h) || IsIconic(h)) return TRUE;
    if(GetWindowLongW(h,GWL_EXSTYLE)&WS_EX_TOOLWINDOW) return TRUE;   // palettes, tooltips
    int ck=0; if(SUCCEEDED(DwmGetWindowAttribute(h,DWMWA_CLOAKED,&ck,sizeof(ck))) && ck) return TRUE;
    DWORD pid=0; GetWindowThreadProcessId(h,&pid);
    if(pid==GetCurrentProcessId()) return TRUE;                       // never our own overlays
    wchar_t cls[128]={0}; GetClassNameW(h,cls,128);
    // the desktop host and the shell furniture are not "windows" in the sense meant here
    static const wchar_t* SKIP[]={ L"Shell_TrayWnd",L"Shell_SecondaryTrayWnd",L"Progman",
                                   L"WorkerW",L"Windows.UI.Core.CoreWindow",nullptr };
    for(int i=0;SKIP[i];i++) if(!wcscmp(cls,SKIP[i])) return TRUE;
    RECT r{}; if(!GetWindowRect(h,&r)) return TRUE;
    if(r.right-r.left<120 || r.bottom-r.top<80) return TRUE;          // menus, tiny helper windows
    ApplyWindowRegion(h);
    return TRUE;
}
static void SweepWindowRegions(){
    EnumWindows(RgnEnum,0);
    for(auto it=g_rgnApplied.begin(); it!=g_rgnApplied.end(); )
        it = IsWindow(it->first) ? std::next(it) : g_rgnApplied.erase(it);
}

// Hand every window its square corners back. Called when the feature is switched off and on the way
// out - a region outlives the process that set it, so skipping this would leave every window on the
// desktop clipped with no shell around to explain why.
static void ClearAllWindowRegions(){
    for(auto& kv : g_rgnApplied) if(IsWindow(kv.first) && WindowResponsive(kv.first)) SetWindowRgn(kv.first,nullptr,TRUE);
    g_rgnApplied.clear();
}
// DWM rounding is normally applied ONCE per window as it appears (see the s_rounded set in the dock
// refresh), so flipping the setting would otherwise not reach anything already on screen.
static BOOL CALLBACK RoundEnum(HWND h,LPARAM){
    if(IsWindowVisible(h) && !(GetWindowLongW(h,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)) ApplyWindowRound(h);
    return TRUE;
}
static void SweepWindowRounds(){ g_roundPrefSent.clear(); EnumWindows(RoundEnum,0); }   // the setting flipped: tell every window again

static void ApplyWindowBackdrop(HWND h){
    if(!h||!IsWindow(h)) return;
    int kind = g_micaMode ? g_micaKind : CWSBT_AUTO;   // AUTO restores the app's own default when off
    DwmSetWindowAttribute(h,DWMWA_SYSTEMBACKDROP_TYPE,&kind,sizeof(kind));
    BOOL dark = g_darkUI?TRUE:FALSE;                    // match the shell theme's titlebar
    DwmSetWindowAttribute(h,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
}
static ID3D11ShaderResourceView* DockIconHi(HWND,const std::wstring&);   // fwd - shares the switcher's cache
static ID3D11ShaderResourceView* DockIconHiAsync(HWND,const std::wstring&);   // fwd - off-thread versions, below DockIconHi
static ID3D11ShaderResourceView* PinIconAsync(const std::wstring&);
static bool PinExists(const std::wstring&);
static void RefreshDock(){
    // Everything below asks other processes questions - WM_GETICON, WM_NULL, SetWindowRgn, DWM
    // attributes - once per window, on the render thread, twice a second. Healthy apps answer in
    // microseconds and this costs nothing; the budget only bites when they do not, and then it
    // hands the rest of the sweep to the next pass instead of to the frame timer.
    ProbeBudget _pb(120);
    ProbePrune();
    for(auto&a:g_dockApps){ if(a.ownsIcon){ if(a.icon)a.icon->Release(); if(a.gray)a.gray->Release(); } } g_dockApps.clear();
    g_dockRaw.clear();
    EnumWindows(DockEnum,0);
    std::sort(g_dockRaw.begin(),g_dockRaw.end(),[](const DockApp&a,const DockApp&b){return a.seq<b.seq;});
    // GROUP windows of the same app into one taskbar entry (a real taskbar shows one icon per app,
    // not a new button for every window). Group key = exe; representative = the earliest window.
    std::vector<DockApp> groups;
    for(auto& r:g_dockRaw){
        DockApp* g=nullptr;
        if(!r.key.empty()) for(auto& e:groups) if(e.key==r.key){ g=&e; break; }
        if(g){ g->wins.push_back(r.hwnd);            // fold this window into its app group
               if(!r.minimized) g->minimized=false;  // group is "minimised" only if ALL are
        } else {
            // RUNNING apps were built from WM_GETICON - normally a 32px image drawn into a 48px
            // texture - and the dock shows them at 46px and magnifies to ~80px under the cursor, so
            // every running app in the dock was visibly upscaled. Pinned-but-closed apps already came
            // from the 256px jumbo list, which is why only SOME icons looked pixellated.
            r.icon=DockIconHiAsync(r.hwnd,r.key);   // from the cache; fetched off-thread the first time
            if(r.icon) r.ownsIcon=false;             // the session cache owns it
            else       r.icon=GetAppIcon(r.hwnd);    // no exe / no jumbo image: the old path
            r.running=true; r.exe=r.key; groups.push_back(std::move(r)); }
    }
    // present running apps in the order they first opened, not the volatile z-order EnumWindows gives
    std::sort(groups.begin(),groups.end(),[](const DockApp&a,const DockApp&b){return a.seq<b.seq;});
    // Corner preference is re-decided every refresh (ApplyWindowRound only calls DWM when it changes);
    // the record is pruned when it grows so closed windows do not accumulate.
    { for(const DockApp& r : g_dockRaw) if(r.hwnd) ApplyWindowRound(r.hwnd);
      if(g_roundPrefSent.size()>512)
          for(auto it=g_roundPrefSent.begin(); it!=g_roundPrefSent.end(); )
              it = IsWindow(it->first) ? std::next(it) : g_roundPrefSent.erase(it); }
    SweepWindowRegions();   // every visible window, not just the ones with taskbar buttons
    // MERGE pinned + running: pinned apps come first in their saved order (a running one adopts its
    // pin slot); then the still-open apps that aren't pinned, in launch order. Pinned-but-closed apps
    // stay as launchers (their on-disk icon), so the dock is stable whether or not they're running.
    std::vector<bool> used(groups.size(),false);
    // Just the file name, lowercased - what a pin should really be matched on.
    auto baseName=[](const std::wstring& p){
        size_t s=p.find_last_of(L"\\/");
        std::wstring b = (s==std::wstring::npos)? p : p.substr(s+1);
        for(auto& c:b) c=(wchar_t)towlower(c);
        return b;
    };
    for(auto& p:g_dockPins){
        // Match the FULL path first, then fall back to the file name. Discord and Roblox live in a
        // version-numbered folder (app-1.0.1209\, version-<hash>\) that changes on every auto-update,
        // so a pin stored as a full path stops matching the running app the moment it updates - and
        // then shows up as a second, dead entry beside the live one.
        int gi=-1;
        for(size_t i=0;i<groups.size();i++)
            if(!used[i] && _wcsicmp(groups[i].key.c_str(),p.exe.c_str())==0){ gi=(int)i; break; }
        if(gi<0){
            std::wstring want=baseName(p.exe);
            for(size_t i=0;i<groups.size();i++)
                if(!used[i] && baseName(groups[i].key)==want){ gi=(int)i; break; }
        }
        if(gi>=0){
            used[gi]=true; groups[gi].pinned=true;
            // adopt the live path so the pin survives the next update too
            if(_wcsicmp(groups[gi].key.c_str(),p.exe.c_str())!=0 && !groups[gi].key.empty())
                p.exe=groups[gi].key;
            g_dockApps.push_back(std::move(groups[gi]));
            continue;
        }
        // Not running. A launcher is only worth a slot if it can actually be launched: an exe that
        // is no longer on disk (the updated-away version folder again) would otherwise sit in the
        // bar as an icon-less button that does nothing when clicked.
        if(!PinExists(p.exe)) continue;             // cached; the disk is asked off-thread
        if(!p.icon) p.icon=PinIconAsync(p.exe);     // lazy, off-thread: the on-disk icon once
        DockApp a{}; a.hwnd=nullptr; a.icon=p.icon; a.ownsIcon=false; a.pinned=true; a.running=false;
        a.mon=nullptr; a.minimized=false; a.exe=p.exe; a.key=p.exe; a.seq=0;
        std::wstring e=p.exe; size_t s=e.find_last_of(L"\\/"); if(s!=std::wstring::npos)e=e.substr(s+1);
        size_t d=e.find_last_of(L'.'); if(d!=std::wstring::npos)e=e.substr(0,d);
        if(!e.empty())e[0]=towupper(e[0]); a.title=W2U8(e);
        g_dockApps.push_back(std::move(a));
    }
    for(size_t i=0;i<groups.size();i++) if(!used[i]) g_dockApps.push_back(std::move(groups[i]));
    // forget windows that have closed, so the sequence doesn't grow forever
    for(auto it=g_dockSeen.begin();it!=g_dockSeen.end();){
        bool live=false; for(auto&e:g_dockApps) for(auto w:e.wins) if(w==it->first){ live=true; break; }
        it = live? std::next(it) : g_dockSeen.erase(it);
    }
    if(g_micaMode) for(auto&e:g_dockApps) for(auto w:e.wins) ApplyWindowBackdrop(w);  // Mica-for-everyone upkeep
}
// ---- pin helpers ----------------------------------------------------------------------
static bool DockIsPinned(const std::wstring& exe){ for(auto&p:g_dockPins) if(_wcsicmp(p.exe.c_str(),exe.c_str())==0) return true; return false; }
static void DockPinAdd(const std::wstring& exe){
    if(exe.empty()||DockIsPinned(exe)) return;
    DockPin p; p.exe=exe; p.icon=LoadExeIcon(exe); g_dockPins.push_back(std::move(p));
    SaveConfig(); RefreshDock();
}
static void DockPinRemove(const std::wstring& exe){
    for(size_t i=0;i<g_dockPins.size();i++) if(_wcsicmp(g_dockPins[i].exe.c_str(),exe.c_str())==0){
        if(g_dockPins[i].icon) g_dockPins[i].icon->Release(); g_dockPins.erase(g_dockPins.begin()+i); break; }
    SaveConfig(); RefreshDock();
}
static void DockLaunch(const std::wstring& exe){ if(!exe.empty()) AetherShellExec(nullptr,L"open",exe.c_str(),nullptr,nullptr,SW_SHOWNORMAL); }
// Toggle the backdrop on/off and apply (or revert) to every currently-open window right away.
static void SetMicaMode(bool on){
    g_micaMode=on;
    RefreshDock();                                   // make sure g_dockApps is current
    for(auto&e:g_dockApps) for(auto w:e.wins) ApplyWindowBackdrop(w);   // applies, or reverts to AUTO when off
}
