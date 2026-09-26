// Aether - click-through regions, workspace slide and tiling glue.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =========================================================================================
// Click-through: clip each overlay window to what it actually draws.
//
// HTTRANSPARENT alone is not enough in practice — a full-height 300px strip still sits over the
// apps beside it, and anything that grabs the mouse (drag, capture, a menu) does so across the
// whole window. Giving the window a REGION removes the transparent area from the window itself:
// the desktop below is hit-tested directly, so a game beside the bar takes its own clicks.
// The region is the panel's hit rect grown by `pad` so drop shadows and glows survive.
// =========================================================================================
static std::unordered_map<HWND,RECT> g_rgnCache;
static void ApplyHitRegion(HWND h,const RECT& logical,int pad){
    if(!h) return;
    RECT want;
    if(logical.right<=logical.left || logical.bottom<=logical.top) want=RECT{0,0,0,0};
    else{
        // Hit rects are already in the window's own CLIENT space, in logical px — that is exactly
        // what WM_NCHITTEST compares after ScreenToClient. So scale to physical, do NOT re-origin.
        float s=g_uiScale;
        want.left  =(LONG)(logical.left*s)  - pad;
        want.top   =(LONG)(logical.top*s)   - pad;
        want.right =(LONG)(logical.right*s) + pad;
        want.bottom=(LONG)(logical.bottom*s)+ pad;
    }
    RECT& had=g_rgnCache[h];
    if(memcmp(&had,&want,sizeof(RECT))==0) return;      // regions are expensive; only on change
    had=want;
    if(want.right<=want.left||want.bottom<=want.top){ SetWindowRgn(h,CreateRectRgn(0,0,0,0),FALSE); return; }
    SetWindowRgn(h,CreateRectRgn(want.left,want.top,want.right,want.bottom),FALSE);
}
// The bar window covers every monitor, so its region is the UNION of each monitor's bar rect.
static std::unordered_map<HWND,std::vector<RECT>> g_rgnMultiCache;
static void ApplyHitRegions(HWND h,const std::vector<RECT>& logicals,int pad){
    if(!h) return;
    std::vector<RECT> want;
    for(const RECT& lr:logicals){
        if(lr.right<=lr.left||lr.bottom<=lr.top) continue;
        float s=g_uiScale;
        want.push_back(RECT{ (LONG)(lr.left*s)-pad,(LONG)(lr.top*s)-pad,
                             (LONG)(lr.right*s)+pad,(LONG)(lr.bottom*s)+pad });
    }
    std::vector<RECT>& had=g_rgnMultiCache[h];
    if(had.size()==want.size() && (want.empty()||memcmp(had.data(),want.data(),want.size()*sizeof(RECT))==0)) return;
    had=want;
    if(want.empty()){ SetWindowRgn(h,CreateRectRgn(0,0,0,0),FALSE); return; }
    HRGN rgn=CreateRectRgn(want[0].left,want[0].top,want[0].right,want[0].bottom);
    for(size_t i=1;i<want.size();i++){
        HRGN r2=CreateRectRgn(want[i].left,want[i].top,want[i].right,want[i].bottom);
        CombineRgn(rgn,rgn,r2,RGN_OR); DeleteObject(r2);
    }
    SetWindowRgn(h,rgn,FALSE);
}

static void HideThumb(){
    if(g_thumb){ DwmUnregisterThumbnail(g_thumb); g_thumb=nullptr; }
    g_thumbSrc=nullptr;
    if(g_thumbHwnd && IsWindowVisible(g_thumbHwnd)) ShowWindow(g_thumbHwnd,SW_HIDE);
    if(g_thumbCut){ g_thumbCut=false; g_deskDirty=true; }   // remove the desktop notch
}
// x,y are PHYSICAL screen coords for the preview's top-left.
static void ShowThumb(HWND src,const std::string& title,int x,int y){
    STALL(bar_thumb);                    // DWM thumbnail + SetWindowPos: the prime suspect for bar_layer's stalls
    if(!g_thumbHwnd || !src || !IsWindow(src)) return;
    if(src!=g_thumbSrc){
        if(g_thumb){ DwmUnregisterThumbnail(g_thumb); g_thumb=nullptr; }
        if(FAILED(DwmRegisterThumbnail(g_thumbHwnd,src,&g_thumb))){ g_thumb=nullptr; g_thumbSrc=nullptr; return; }
        g_thumbSrc=src;
    }
    g_thumbTitle=title;
    // keep the preview on screen
    RECT vr={g_mx,g_my,g_mx+g_mw,g_my+g_mh};
    if(x+THUMBW>vr.right)  x=vr.right-THUMBW-4;
    if(y+THUMBH>vr.bottom) y=vr.bottom-THUMBH-4;
    if(x<vr.left) x=vr.left+4;
    if(y<vr.top)  y=vr.top+4;
    SetWindowPos(g_thumbHwnd,HWND_TOPMOST,x,y,THUMBW,THUMBH,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    InvalidateRect(g_thumbHwnd,nullptr,TRUE);
    // publish the preview's rect (logical desk coords) so the DESKTOP layer carves a matching notch =>
    // the wallpaper curves around the preview and it reads as CUT INTO the desktop, part of the bar.
    { float s=g_uiScale; float nl=(x-g_vs.left)/s, nt=(y-g_vs.top)/s;
      bool ch = !g_thumbCut || g_thumbL!=nl || g_thumbT!=nt;
      g_thumbCut=true; g_thumbL=nl; g_thumbT=nt; g_thumbR=nl+THUMBW/s; g_thumbB=nt+THUMBH/s;
      if(ch) g_deskDirty=true; }
    // letterbox the source into the well, preserving aspect (Cairo's DwmThumbnail.Refresh)
    SIZE ss{0,0}; DwmQueryThumbnailSourceSize(g_thumb,&ss);
    RECT dst={THUMBPAD,THUMBPAD+THUMBBAR,THUMBW-THUMBPAD,THUMBH-THUMBPAD};
    int dw=dst.right-dst.left, dh=dst.bottom-dst.top;
    if(ss.cx>0 && ss.cy>0){
        double sa=(double)ss.cx/ss.cy, da=(double)dw/dh;
        if(ss.cx<=dw && ss.cy<=dh){                       // small enough: centre, do not scale
            dst.left+=(dw-ss.cx)/2; dst.top+=(dh-ss.cy)/2;
            dst.right=dst.left+ss.cx; dst.bottom=dst.top+ss.cy;
        } else if(sa>da){                                  // wide
            int hh=(int)(dw/sa); dst.top+=(dh-hh)/2; dst.bottom=dst.top+hh;
        } else if(sa<da){                                  // tall
            int ww=(int)(dh*sa); dst.left+=(dw-ww)/2; dst.right=dst.left+ww;
        }
    }
    DWM_THUMBNAIL_PROPERTIES p{};
    p.dwFlags=DWM_TNP_VISIBLE|DWM_TNP_RECTDESTINATION|DWM_TNP_OPACITY|DWM_TNP_SOURCECLIENTAREAONLY;
    p.fVisible=TRUE; p.opacity=255; p.fSourceClientAreaOnly=FALSE; p.rcDestination=dst;
    DwmUpdateThumbnailProperties(g_thumb,&p);
}

// ---- login items: Explorer normally runs these, so as the shell we must ----
// True when a Run-key command line points back at this very executable. Running the login items
// as the shell would otherwise relaunch us from our own "start at sign-in" entry — two shells.
static bool CommandIsSelf(const std::wstring& cmd){
    wchar_t me[MAX_PATH]; GetModuleFileNameW(nullptr,me,MAX_PATH);
    std::wstring lm=me, lc=cmd;
    for(auto& ch:lm) ch=towlower(ch);
    for(auto& ch:lc) ch=towlower(ch);
    if(lc.find(lm)!=std::wstring::npos) return true;
    // also match on the bare file name, in case the entry used a different path to the same exe
    size_t slash=lm.find_last_of(L'\\');
    std::wstring leaf = (slash==std::wstring::npos)? lm : lm.substr(slash+1);
    return !leaf.empty() && lc.find(leaf)!=std::wstring::npos;
}
static void RunValuesIn(HKEY root,const wchar_t* sub,bool once){
    HKEY k; if(RegOpenKeyExW(root,sub,0,KEY_READ|(once?KEY_SET_VALUE:0),&k)!=ERROR_SUCCESS) return;
    std::vector<std::wstring> names, cmds;
    for(DWORD i=0;;i++){
        wchar_t nm[256]; DWORD nsz=256; BYTE val[2048]; DWORD vsz=sizeof(val), type=0;
        if(RegEnumValueW(k,i,nm,&nsz,nullptr,&type,val,&vsz)!=ERROR_SUCCESS) break;
        if(type!=REG_SZ && type!=REG_EXPAND_SZ) continue;
        if(CommandIsSelf((wchar_t*)val)) continue;          // never launch a second shell
        names.push_back(nm); cmds.push_back((wchar_t*)val);
    }
    for(size_t i=0;i<cmds.size();i++){
        wchar_t exp[4096]; ExpandEnvironmentStringsW(cmds[i].c_str(),exp,4096);
        STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
        std::wstring line=exp;
        if(CreateProcessW(nullptr,&line[0],nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)){
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
        if(once) RegDeleteValueW(k,names[i].c_str());     // RunOnce entries fire exactly once
    }
    RegCloseKey(k);
}
static void RunStartupFolder(int csidl){
    wchar_t dir[MAX_PATH]={0};
    if(FAILED(SHGetFolderPathW(nullptr,csidl,nullptr,0,dir))) return;
    std::wstring pat=std::wstring(dir)+L"\\*";
    WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW(pat.c_str(),&fd);
    if(h==INVALID_HANDLE_VALUE) return;
    do{
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        // desktop.ini and friends live here as hidden system files — Explorer never launches
        // them, and neither must we, or sign-in pops Notepad on desktop.ini.
        if(fd.dwFileAttributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM)) continue;
        if(fd.cFileName[0]==L'.') continue;
        const wchar_t* dot=wcsrchr(fd.cFileName,L'.');
        if(!dot) continue;
        // only things that are actually launchable at sign-in
        static const wchar_t* OKEXT[]={L".lnk",L".exe",L".bat",L".cmd",L".com",L".url",L".vbs",L".ps1",L".pif"};
        bool ok=false; for(const wchar_t* e:OKEXT) if(_wcsicmp(dot,e)==0){ ok=true; break; }
        if(!ok) continue;
        std::wstring full=std::wstring(dir)+L"\\"+fd.cFileName;
        AetherShellExec(nullptr,L"open",full.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    } while(FindNextFileW(h,&fd));
    FindClose(h);
}
// Login items must fire ONCE PER SIGN-IN, not once per shell start. WinLogon relaunches the shell
// after a crash, and we restart it by hand during development — Explorer would never re-run your
// Startup folder for either, and neither must we, or every restart stacks another copy of whatever
// is in there. HKCU\Volatile Environment is wiped by Windows at logoff, so it is exactly the right
// lifetime for the marker: it survives shell restarts and dies with the session.
static const wchar_t* VOLENV=L"Volatile Environment";
static const wchar_t* RANFLAG=L"AetherLoginItemsRan";
static bool LoginItemsAlreadyRan(){
    return !RegString(HKEY_CURRENT_USER,VOLENV,RANFLAG).empty();
}
static void MarkLoginItemsRan(){
    HKEY k;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,VOLENV,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS) return;
    RegSetValueExW(k,RANFLAG,0,REG_SZ,(const BYTE*)L"1",(DWORD)(2*sizeof(wchar_t)));
    RegCloseKey(k);
}
// Runs on a worker thread: launching a dozen login items must not stall the first frame.
static void RunLoginItems(){
    if(LoginItemsAlreadyRan()) return;      // a shell restart is NOT a new sign-in
    MarkLoginItemsRan();                    // set before launching, so a crash mid-run cannot loop
    std::thread([]{
        const wchar_t* RUNW =L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        const wchar_t* ONCEW=L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
        RunValuesIn(HKEY_LOCAL_MACHINE,ONCEW,true);
        RunValuesIn(HKEY_LOCAL_MACHINE,RUNW,false);
        RunValuesIn(HKEY_CURRENT_USER, ONCEW,true);
        RunValuesIn(HKEY_CURRENT_USER, RUNW,false);
        RunStartupFolder(CSIDL_COMMON_STARTUP);
        RunStartupFolder(CSIDL_STARTUP);
    }).detach();
}
// True when a foreground app covers this whole monitor (game / fullscreen video) — the shell must
// get out of the way, exactly like the real taskbar does.
// Is the foreground window covering THIS monitor? Per-monitor, so a fullscreen game on one screen
// no longer hides the bar on the others.
static bool FullscreenOnMon(const RECT& m){
    HWND fg=GetForegroundWindow(); if(!fg) return false;
    DWORD pid=0; GetWindowThreadProcessId(fg,&pid);
    if(pid==GetCurrentProcessId()) return false;              // our own overlays don't count
    wchar_t cls[64]={0}; GetClassNameW(fg,cls,63);
    if(!wcscmp(cls,L"Progman")||!wcscmp(cls,L"WorkerW")||
       !wcscmp(cls,L"Shell_TrayWnd")||!wcscmp(cls,L"Shell_SecondaryTrayWnd")) return false;
    RECT r; if(!GetWindowRect(fg,&r)) return false;
    return r.left<=m.left+1 && r.top<=m.top+1 && r.right>=m.right-1 && r.bottom>=m.bottom-1;
}
static bool FullscreenAppActive(){
    RECT m={g_mx,g_my,g_mx+g_mw,g_my+g_mh};
    return FullscreenOnMon(m);
}

// ---- virtual desktops = "workspaces" ------------------------------------------------
// Deliberately avoids the undocumented IVirtualDesktopManagerInternal COM interfaces that
// VirtualDesktopAccessor uses (they break on nearly every Windows build). Instead: read the
// desktop list from the registry, and switch with the PUBLIC Ctrl+Win+Left/Right shortcut.
static int g_wsCount=1, g_wsCur=0;
// dwExtraInfo stamped on every key the shell synthesises itself, so its own hooks can tell that input
// from anyone else's (rejecting ALL injected input would also ignore remappers, KVM and RDP keyboards).
static const ULONG_PTR AETHER_KEY_TAG=0xAE7E4;
// ---- optimistic workspace switching ---------------------------------------------------------------
// Windows writes CurrentVirtualDesktop about 1.5 s after a switch, so a bar that waits for the
// registry shows the OLD workspace for that long - the "cycling workspaces shows nothing on the bar"
// report. A click now moves the highlight at once and HOLDS it there until Windows agrees, or until
// the hold runs out, at which point whatever Windows says wins.
static int g_wsPendTarget=-1, g_wsPendCount=0; static ULONGLONG g_wsPendUntil=0;
static void WsHoldPending(int& count,int& idx){
    if(g_wsPendTarget<0) return;
    if(GetTickCount64()>g_wsPendUntil){ g_wsPendTarget=-1; return; }            // give up: trust Windows
    if(idx==g_wsPendTarget && count>=g_wsPendCount){ g_wsPendTarget=-1; return; } // confirmed
    count=std::max(count,g_wsPendCount); idx=g_wsPendTarget;                    // not yet: keep the target
}
static inline int WsSlots(){ return std::clamp(std::max(g_wsCount,g_wsShown),1,64); }
static char g_wsName[64][24]={};        // komorebi workspace names; empty on virtual desktops
static int  g_komoMirror=0;             // which komorebi monitor the globals above are mirroring

// komorebi (like Hyprland, which is what Caelestia reads) gives every monitor its OWN workspace
// ring: different count, different focus, different windows. Windows virtual desktops are global,
// so the shell only ever had one set of workspace globals and every bar drew the same dots. Each
// bar now reads its own ring; the virtual-desktop path just fills them all identically.
static const int WS_MAXICONS=5;                     // barconfig.hpp: maxWindowIcons default 5
struct WsRing {
    int  count=1, cur=0, komoMon=-1;
    bool occ[64]={};
    const char* icon[64][WS_MAXICONS]={};
    int  iconN[64]={};
    char name[64][24]={};
};
static WsRing g_wsRing[16];
static const WsRing& WsRingFor(int mi){ return g_wsRing[std::clamp(mi,0,15)]; }
// Caelestia's workspace shapes have THREE states - focused, occupied, empty - and it draws the
// occupied ones larger. That needs to know which desktop each window lives on. The registry already
// hands us the desktop GUIDs IN ORDER (that is what VirtualDesktopIDs is), and
// IVirtualDesktopManager::GetWindowDesktopId is PUBLIC, documented API - so occupancy needs none of
// the undocumented IVirtualDesktopManagerInternal this file deliberately avoids.
static GUID g_wsIds[64]={}; static bool g_wsOccupied[64]={false};
// Workspace.qml draws a category glyph per window under each occupied workspace, capped at
// Config.bar.workspaces.maxWindowIcons (default 5), via Icons.getAppCategoryIcon(class).
// Upstream resolves that through freedesktop .desktop Categories, which Windows has no equivalent
// of - so the exe name is classified directly and mapped onto the same icon families.
static const char* g_wsIcon[64][WS_MAXICONS]={};    // breeze icon names
static int  g_wsIconN[64]={0};

static const char* AppCategoryIcon(const std::wstring& exeIn){
    std::wstring e=exeIn; size_t sl=e.find_last_of(L"\\/"); if(sl!=std::wstring::npos) e=e.substr(sl+1);
    for(auto& c:e) c=towlower(c);
    struct Row{ const wchar_t* exe; const char* icon; };
    // Names are Breeze's, chosen for ones that exist AND tint: "utilities-terminal" is only
    // shipped as layered artwork here, so the terminals borrow Yakuake's symbolic terminal glyph.
    // the same category families as Icons.qml's categoryIcons, in Breeze's naming
    static const Row R[]={
        {L"chrome.exe","plasma-browser-integration-symbolic"},   {L"msedge.exe","plasma-browser-integration-symbolic"},
        {L"firefox.exe","plasma-browser-integration-symbolic"},  {L"brave.exe","plasma-browser-integration-symbolic"},
        {L"opera.exe","plasma-browser-integration-symbolic"},    {L"opera_gx.exe","plasma-browser-integration-symbolic"},
        {L"vivaldi.exe","plasma-browser-integration-symbolic"},  {L"helium.exe","plasma-browser-integration-symbolic"},
        {L"thorium.exe","plasma-browser-integration-symbolic"},  {L"zen.exe","plasma-browser-integration-symbolic"},
        {L"librewolf.exe","plasma-browser-integration-symbolic"},{L"chromium.exe","plasma-browser-integration-symbolic"},
        {L"windowsterminal.exe","yakuake"},  {L"wt.exe","yakuake"},
        {L"alacritty.exe","yakuake"},        {L"cmd.exe","yakuake"},
        {L"powershell.exe","yakuake"},       {L"pwsh.exe","yakuake"},
        {L"discord.exe","dialog-messages-symbolic"},             {L"discordptb.exe","dialog-messages-symbolic"},
        {L"discordcanary.exe","dialog-messages-symbolic"},       {L"vesktop.exe","dialog-messages-symbolic"},
        {L"foracord.exe","dialog-messages-symbolic"},            {L"opticord.exe","dialog-messages-symbolic"},
        {L"slack.exe","dialog-messages-symbolic"},               {L"teams.exe","dialog-messages-symbolic"},
        {L"telegram.exe","dialog-messages-symbolic"},
        {L"code.exe","applications-development-symbolic"},       {L"devenv.exe","applications-development-symbolic"},
        {L"rider64.exe","applications-development-symbolic"},    {L"pycharm64.exe","applications-development-symbolic"},
        {L"idea64.exe","applications-development-symbolic"},     {L"clion64.exe","applications-development-symbolic"},
        {L"sublime_text.exe","applications-development-symbolic"},
        {L"notepad.exe","applications-office-symbolic"},     {L"notepad++.exe","applications-office-symbolic"},
        {L"wordpad.exe","applications-office-symbolic"},     {L"write.exe","applications-office-symbolic"},
        {L"spotify.exe","applications-multimedia-symbolic"},     {L"vlc.exe","applications-multimedia-symbolic"},
        {L"mpv.exe","applications-multimedia-symbolic"},         {L"musicbee.exe","applications-multimedia-symbolic"},
        {L"foobar2000.exe","applications-multimedia-symbolic"},  {L"aimp.exe","applications-multimedia-symbolic"},
        {L"steam.exe","applications-games-symbolic"},            {L"steamwebhelper.exe","applications-games-symbolic"},
        {L"robloxplayerbeta.exe","applications-games-symbolic"}, {L"epicgameslauncher.exe","applications-games-symbolic"},
        {L"explorer.exe","folder-symbolic"},        {L"filepilot.exe","folder-symbolic"},
        {L"systemsettings.exe","configure-symbolic"},            {L"control.exe","configure-symbolic"},
        {L"mmc.exe","applications-utilities-symbolic"},             {L"taskmgr.exe","applications-utilities-symbolic"},
        {L"photoshop.exe","applications-graphics-symbolic"},     {L"gimp.exe","applications-graphics-symbolic"},
        {L"mspaint.exe","applications-graphics-symbolic"},
        {L"winword.exe","applications-office-symbolic"},         {L"excel.exe","applications-office-symbolic"},
    };
    for(auto& r:R) if(e==r.exe) return r.icon;
    return "applications-other-symbolic";                    // the `fallback` argument upstream passes
}
#include "src/services/Komorebi.h"    // <- komorebi as the workspace source (Hypr.qml's role)
#include "src/services/TileAnim.h"    // smooth tiling moves, done by the shell (komorebi's own animation greys Chromium)
#include "src/services/WorkspaceSlide2.h"   // the workspace slide on DWM thumbnails (smooth, never moves real windows per frame)
#include "src/services/TileAnim2.h"    // the tiling animation on DWM thumbnails - position AND size, no window ever moved
#include "src/services/Capture.h"      // live monitor capture (Windows.Graphics.Capture) for the workspace overview
static bool TaEnabled(){ return g_tileAnim && g_komoLive.load(); }
static std::string ProfileWmName(){ return (g_wsSource!=WSSRC_VDESK && g_komoLive.load())? "komorebi" : "Aether"; }

// Mirror komorebi's focused monitor into the bar's workspace globals. Returns false when komorebi
// has nothing for us, so the caller falls through to virtual desktops.
static void KomoFillRing(WsRing& r, const KomoMon& m, int komoIdx){
    r = WsRing{};
    r.komoMon = komoIdx;
    r.count = std::clamp((int)m.ws.size(),1,64);
    r.cur   = std::clamp(m.focused,0,r.count-1);
    for(int i=0;i<r.count;i++){
        const KomoWs& w=m.ws[i];
        strncpy_s(r.name[i],sizeof(r.name[i]),w.name.c_str(),_TRUNCATE);
        r.occ[i]=!w.exes.empty();
        for(auto& exe:w.exes){
            if(r.iconN[i]>=WS_MAXICONS) break;
            r.icon[i][r.iconN[i]++]=AppCategoryIcon(exe); }
    }
    r.occ[r.cur]=true;               // the focused workspace always reads as live, as upstream does
}

static void KomorebiApplyReserve();     // defined below, next to the other komorebi calls
static bool RefreshWorkspacesKomorebi(){
    std::vector<KomoMon> snap; int focusedMon=0;
    { std::lock_guard<std::mutex> lk(g_komoMtx);
      if(g_komo.empty()) return false;
      snap=g_komo; focusedMon=std::clamp(g_komoFocusedMon,0,(int)snap.size()-1); }
    if(snap.empty() || snap[focusedMon].ws.empty()) return false;
    g_komoMirror=focusedMon;

    // Line komorebi's monitors up with ours by rectangle. komorebi enumerates in its own order, so
    // matching by index would put the left screen's workspaces on the right screen's bar.
    int nm=(int)g_mons.size(); if(nm>16) nm=16;
    for(int i=0;i<nm;i++){
        const RECT& mr=MonRect(i);
        int best=-1; LONG bestD=LONG_MAX;
        for(int k=0;k<(int)snap.size();k++){
            const RECT& kr=snap[k].rect;
            LONG d=labs(kr.left-mr.left)+labs(kr.top-mr.top);
            if(d<bestD){ bestD=d; best=k; } }
        if(best<0 || bestD>64) best=focusedMon;     // unmanaged screen: mirror the focused ring
        KomoFillRing(g_wsRing[i], snap[best], best);
    }
    for(int i=nm;i<16;i++) g_wsRing[i]=g_wsRing[0];

    // the legacy globals stay pointed at the focused monitor for the dashboard's workspace chips
    const WsRing& f=g_wsRing[std::clamp(g_actMon,0,std::max(0,nm-1))];
    g_wsCount=f.count; g_wsCur=f.cur;
    for(int i=0;i<64;i++){ g_wsOccupied[i]=f.occ[i]; g_wsIconN[i]=f.iconN[i];
        memcpy(g_wsName[i],f.name[i],sizeof(g_wsName[i]));
        for(int q=0;q<WS_MAXICONS;q++) g_wsIcon[i][q]=f.icon[i][q]; }
    return true;
}

static void KomoAdoptExisting();      // defined with the taskbar code, called from the session gate

// Desktop switches made with the KEYBOARD had no click to show early, so the bar waited for the
// registry: measured 2-3 s behind a Ctrl+Win+Arrow. The shell's keyboard hook sees those keys before
// Windows acts on them, so it predicts the result and shows it at once, held by WsHoldPending exactly
// like a click. Anything it cannot predict (Task View, touchpad gestures) still arrives the slow way.
static void WsPredictKey(DWORD vk){
    if(g_wsSource!=WSSRC_VDESK && g_komoLive.load()) return;      // komorebi's own events drive that ring
    const bool ctrl=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
    const bool win =((GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN))&0x8000)!=0;
    const bool other=((GetAsyncKeyState(VK_MENU)|GetAsyncKeyState(VK_SHIFT))&0x8000)!=0;
    if(!ctrl || !win || other) return;
    int count=std::max(1,g_wsCount), cur=std::clamp(g_wsCur,0,count-1), nc=count, ni=cur;
    if(vk==VK_RIGHT)    { if(cur>=count-1) return; ni=cur+1; }
    else if(vk==VK_LEFT){ if(cur<=0) return; ni=cur-1; }
    else if(vk=='D')    { if(count>=64) return; nc=count+1; ni=count; }       // appended, and switched to
    else if(vk==VK_F4)  { if(count<=1) return; nc=count-1; ni=cur>0? cur-1 : 0; }
    else return;
    g_wsPendTarget=ni; g_wsPendCount=nc; g_wsPendUntil=GetTickCount64()+4000;
    g_wsCount=nc; g_wsCur=ni;
    for(int i=0;i<16;i++){ g_wsRing[i].count=nc; g_wsRing[i].cur=ni; g_wsRing[i].occ[ni]=true; }
    g_deskDirty=true;
}
static void WsQueryPost();    // fwd - virtual-desktop queries run on a worker (see WsQueryWorker)
static void WsApplyAnswer();  // fwd
static void RefreshWorkspaces(){
    // komorebi first when it is the chosen source and actually up
    if(g_wsSource!=WSSRC_VDESK && g_komoLive.load() && RefreshWorkspacesKomorebi()){
        KomorebiApplyReserve();
        // Once per komorebi RUN. komorebi restores a dumped state from its previous instance on
        // start, so anything set through komorebic has to be set again every time it comes up.
        // This used to be keyed on g_komoStamp, which is a per-SNAPSHOT tick - it could not detect a
        // restart, and in testing the re-apply never fired at all. g_komoSession counts down->up
        // transitions, which is the thing actually being asked about.
        { static unsigned doneFor=0;
          unsigned sess=g_komoSession.load();
          if(sess!=0 && sess!=doneFor){
              doneFor=sess;
              if(g_niriMode){
                  KomoSetLayoutAll(L"scrolling");
                  std::thread([]{ Sleep(600); KomoApplyScrollCols(g_niriCols); }).detach();
              }
              // komorebi only manages windows it saw APPEAR, so everything already open when it
              // started is invisible to it: never tiled, never hidden on a workspace switch, and so
              // showing on top of every workspace. That was the "same app shows on every workspace"
              // report, and it came back on every komorebi restart until this ran by itself.
              if(g_komoAutoAdopt)
                  std::thread([]{ Sleep(1500); KomoAdoptExisting(); }).detach();
          } }
        // komorebi takes a column count only for the FOCUSED workspace, so niri mode has to re-send
        // it whenever focus lands somewhere new - otherwise the setting applies to exactly one
        // workspace and looks broken everywhere else.
        if(g_niriMode && g_komoLive.load()){
            // Ask komorebi which monitor is focused. The previous form walked our own monitor list
            // and compared each entry against g_wsRing[0].komoMon - i.e. against monitor ZERO, not
            // against the focused one - so on a two-monitor setup it latched onto the wrong monitor
            // and re-sent the column count for a workspace the user was not looking at.
            static int lastMon=-1, lastWs=-1;
            int fm=-1, fw=-1;
            { std::lock_guard<std::mutex> lk(g_komoMtx);
              if(g_komoFocusedMon>=0 && g_komoFocusedMon<(int)g_komo.size()){
                  fm=g_komoFocusedMon; fw=g_komo[fm].focused; } }
            if(fm>=0 && fw>=0 && (fm!=lastMon || fw!=lastWs)){
                lastMon=fm; lastWs=fw;
                KomoApplyScrollCols(g_niriCols);
            }
        }
        return; }
    for(int i=0;i<64;i++) g_wsName[i][0]=0;
    HKEY k; const wchar_t* P=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";
    if(RegOpenKeyExW(HKEY_CURRENT_USER,P,0,KEY_READ,&k)!=ERROR_SUCCESS) return;
    BYTE ids[16*64]={0}; DWORD sz=sizeof(ids), type=0;
    int count=1;
    if(RegQueryValueExW(k,L"VirtualDesktopIDs",nullptr,&type,ids,&sz)==ERROR_SUCCESS && sz>=16) count=(int)(sz/16);
    BYTE cur[16]={0}; DWORD csz=sizeof(cur); int idx=0;
    if(RegQueryValueExW(k,L"CurrentVirtualDesktop",nullptr,&type,cur,&csz)==ERROR_SUCCESS && csz==16)
        for(int i=0;i<count;i++) if(memcmp(ids+i*16,cur,16)==0){ idx=i; break; }
    RegCloseKey(k);
    WsHoldPending(count,idx);
    g_wsCount=std::max(1,std::min(count,64)); g_wsCur=std::clamp(idx,0,g_wsCount-1);
    memcpy(g_wsIds,ids,sizeof(GUID)*g_wsCount);
    for(int i=0;i<64;i++) g_wsOccupied[i]=false;
    g_wsOccupied[g_wsCur]=true;                     // the one you are on always counts as occupied
    // map the task list onto desktops - on the worker (see WsQueryWorker). Hand it the current
    // snapshot, and use the last answer it produced for this same set of desktops.
    WsQueryPost();
    WsApplyAnswer();

    // Virtual desktops are machine-wide: every bar shows the same ring.
    WsRing r; r.count=g_wsCount; r.cur=g_wsCur; r.komoMon=-1;
    for(int i=0;i<64;i++){ r.occ[i]=g_wsOccupied[i]; r.iconN[i]=g_wsIconN[i];
        for(int q=0;q<WS_MAXICONS;q++) r.icon[i][q]=g_wsIcon[i][q]; }
    for(int i=0;i<16;i++) g_wsRing[i]=r;
}
// Which desktop you are ON is two cheap registry reads. Which windows live on each one costs an
// EnumWindows plus a GetWindowDesktopId per window plus icon lookups, which is why the full refresh
// shares the dock's 2-second tick. Switching desktops therefore took up to two seconds to show on
// the bar - long enough to read as "cycling desktops does not change anything", which is what a
// tester reported. The cheap half now runs on its own fast tick; occupancy and icons still come on
// the slow one, so a switch is visible immediately and costs nothing extra.
// Ask Windows which desktop is current, WITHOUT waiting for the registry.
//
// Measured on this machine: after a desktop switch, CurrentVirtualDesktop is not written for about
// 1500 ms. That is the real reason the bar "does not change when you cycle desktops" - the source
// itself lags, so no poll rate can help and a faster tick just reads a stale value more often.
//
// IsWindowOnCurrentVirtualDesktop answers immediately and is PUBLIC, documented API - the same
// reason this file already prefers GetWindowDesktopId over the undocumented internal interfaces.
// Find any window we already know the desktop of that reports it is on the current desktop, and its
// id IS the current desktop. Usually the first window tried answers, so this is one or two calls.
// Returns -1 when nothing can answer (an empty desktop), and the caller keeps the registry value.
// ---- virtual-desktop queries, OFF the render thread --------------------------------------------------
// IVirtualDesktopManager is a cross-process call into Explorer, and Explorer is busy animating while a
// desktop switch runs. Measured with the stall log: RefreshWorkspaces blocked the render thread for
// 2340 ms and 1905 ms on two switches, so the bar showed nothing new for two seconds - the tester's
// "cycling workspaces shows nothing on the bar". Every such call now happens on this worker; the
// render thread hands it a snapshot and picks up whatever answer is ready, never waiting for one.
struct WsJob { std::vector<std::pair<HWND,std::wstring>> apps; GUID ids[64]; int count=1; int cur=0; };
struct WsAnswer { bool ok=false; int count=0; GUID ids[64]; bool occ[64]; int iconN[64];
                  const char* icon[64][WS_MAXICONS]; };
static std::mutex g_wsqMtx;
static WsJob      g_wsqJob;  static bool g_wsqJobNew=false;
static WsAnswer   g_wsqAns;
static std::atomic<int> g_wsqLive{-1};          // desktop index of a window on the current desktop, -1 unknown
static std::atomic<bool> g_wsqStarted{false};
static void WsQueryWorker(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IVirtualDesktopManager* vdm=nullptr;
    static const GUID NILGUID={};
    while(g_running){
        if(!vdm && FAILED(CoCreateInstance(CLSID_VirtualDesktopManager,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&vdm)))){
            vdm=nullptr; Sleep(1000); continue; }
        WsJob job; bool fresh=false;
        { std::lock_guard<std::mutex> lk(g_wsqMtx); job=g_wsqJob; fresh=g_wsqJobNew; g_wsqJobNew=false; }
        // which desktop are we on: the first window that says it is on the current one
        int live=-1, tried=0;
        for(auto& a:job.apps){
            if(!a.first || !IsWindow(a.first)) continue;
            if(++tried>24) break;
            BOOL on=FALSE;
            if(FAILED(vdm->IsWindowOnCurrentVirtualDesktop(a.first,&on)) || !on) continue;
            GUID g{};
            if(FAILED(vdm->GetWindowDesktopId(a.first,&g)) || memcmp(&g,&NILGUID,sizeof(GUID))==0) continue;
            for(int i=0;i<job.count;i++) if(memcmp(&job.ids[i],&g,sizeof(GUID))==0){ live=i; break; }
            if(live>=0) break;
        }
        g_wsqLive.store(live);
        if(fresh){
            WsAnswer ans; ans.ok=true; ans.count=job.count; memcpy(ans.ids,job.ids,sizeof(ans.ids));
            for(int i=0;i<64;i++){ ans.occ[i]=false; ans.iconN[i]=0; }
            for(auto& a:job.apps){
                // GetWindowDesktopId fails, or answers GUID_NULL, for cloaked UWP hosts and minimised
                // windows; those are attributed to the current desktop, which is where the taskbar shows them.
                GUID g{};
                if(FAILED(vdm->GetWindowDesktopId(a.first,&g)) || memcmp(&g,&NILGUID,sizeof(GUID))==0)
                    g=job.ids[std::clamp(job.cur,0,std::max(0,job.count-1))];
                for(int i=0;i<job.count;i++)
                    if(memcmp(&job.ids[i],&g,sizeof(GUID))==0){
                        ans.occ[i]=true;
                        if(ans.iconN[i]<WS_MAXICONS) ans.icon[i][ans.iconN[i]++]=AppCategoryIcon(a.second);
                        break; }
            }
            std::lock_guard<std::mutex> lk(g_wsqMtx); g_wsqAns=ans;
        }
        Sleep(250);
    }
    if(vdm) vdm->Release();
    CoUninitialize();
}
static void WsQueryPost(){
    if(!g_wsqStarted.exchange(true)) std::thread(WsQueryWorker).detach();
    std::lock_guard<std::mutex> lk(g_wsqMtx);
    g_wsqJob.apps.clear();
    for(auto& a:g_dockApps) if(a.hwnd) g_wsqJob.apps.push_back({a.hwnd,a.exe});
    memcpy(g_wsqJob.ids,g_wsIds,sizeof(g_wsqJob.ids));
    g_wsqJob.count=g_wsCount; g_wsqJob.cur=g_wsCur; g_wsqJobNew=true;
}
static void WsApplyAnswer(){
    std::lock_guard<std::mutex> lk(g_wsqMtx);
    if(g_wsqAns.ok && g_wsqAns.count==g_wsCount && memcmp(g_wsqAns.ids,g_wsIds,sizeof(GUID)*g_wsCount)==0){
        for(int i=0;i<64;i++){ g_wsOccupied[i]=g_wsqAns.occ[i]; g_wsIconN[i]=g_wsqAns.iconN[i];
            for(int q=0;q<WS_MAXICONS;q++) g_wsIcon[i][q]=g_wsqAns.icon[i][q]; }
    } else {
        for(int i=0;i<64;i++) g_wsIconN[i]=0;   // desktops changed: no stale glyphs on the wrong slot
    }
    g_wsOccupied[g_wsCur]=true;
}
static int CurrentDesktopFromWindows(){
    if(!g_wsqStarted.load()) WsQueryPost();
    return g_wsqLive.load();
}
static void RefreshWorkspacesFast(){
    if(g_wsSource!=WSSRC_VDESK && g_komoLive.load()) return;   // komorebi owns the ring in that mode
    HKEY k; const wchar_t* P=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";
    if(RegOpenKeyExW(HKEY_CURRENT_USER,P,0,KEY_READ,&k)!=ERROR_SUCCESS) return;
    BYTE ids[16*64]={0}; DWORD sz=sizeof(ids), type=0; int count=1;
    if(RegQueryValueExW(k,L"VirtualDesktopIDs",nullptr,&type,ids,&sz)==ERROR_SUCCESS && sz>=16) count=(int)(sz/16);
    BYTE cur[16]={0}; DWORD csz=sizeof(cur); int idx=0;
    if(RegQueryValueExW(k,L"CurrentVirtualDesktop",nullptr,&type,cur,&csz)==ERROR_SUCCESS && csz==16)
        for(int i=0;i<count;i++) if(memcmp(ids+i*16,cur,16)==0){ idx=i; break; }
    RegCloseKey(k);
    count=std::max(1,std::min(count,64)); idx=std::clamp(idx,0,count-1);
    // The registry is ~1.5s behind the actual switch, so prefer the live answer when we can get one.
    // A pending click is held against the REGISTRY, the slow source, not the live answer: measured on
    // a 3 -> 1 switch, the registry walked through desktop 2 on its way back, and once the hold was
    // released early the bar sat on workspace 2 for over a second. The live answer is also unreliable
    // right after a switch, because g_dockApps still lists the previous desktop's windows.
    WsHoldPending(count,idx);
    if(g_wsPendTarget<0){ int live=CurrentDesktopFromWindows(); if(live>=0 && live<count) idx=live; }
    count=std::max(1,std::min(count,64)); idx=std::clamp(idx,0,count-1);
    if(count==g_wsCount && idx==g_wsCur) return;               // nothing moved
    g_wsCount=count; g_wsCur=idx;
    memcpy(g_wsIds,ids,sizeof(GUID)*g_wsCount);
    g_wsOccupied[g_wsCur]=true;                                // the one you are on always counts
    for(int i=0;i<16;i++){ g_wsRing[i].count=g_wsCount; g_wsRing[i].cur=g_wsCur;
                           g_wsRing[i].occ[g_wsCur]=true; }
    g_deskDirty=true;
}
// Tell komorebi how much of each monitor the bar owns, so it stops tiling windows underneath it.
// Only re-issued when the number actually changes - this shells out, and komorebi retiles on every
// call, so doing it per frame would be a stutter machine.
static void KomorebiApplyReserve(){
    if(!g_komoReserve || !g_komoLive.load()) return;
    const Panel& P=g_pn[PN_BAR];
    int t = (!P.visible || g_barAutoHide) ? 0
          : (int)((P.size + P.gap*2.0f) * g_uiScale + 0.5f);
    int L=0,T=0,R=0,B=0;
    // komorebi's Rect offsets are {left, top, width-shrink, height-shrink}: shifting the work area
    // right by t also has to take t off the width, which is what its own --help means by
    // "set right to left * 2 to maintain right padding".
    switch(P.edge){
        case EDGE_LEFT:  L=t; R=t; break;
        case EDGE_RIGHT:      R=t; break;
        case EDGE_TOP:   T=t; B=t; break;
        default:              B=t; break;
    }
    static int lastOff[16][4]={};
    static bool lastInit=false;
    if(!lastInit){ for(int i=0;i<16;i++) for(int q=0;q<4;q++) lastOff[i][q]=-1; lastInit=true; }
    int nm=std::min((int)g_mons.size(),16);
    for(int i=0;i<nm;i++){
        int km=g_wsRing[i].komoMon;
        if(km<0) continue;
        int l=BarOnMon(i)?L:0, tp=BarOnMon(i)?T:0, r=BarOnMon(i)?R:0, b=BarOnMon(i)?B:0;
        // komorebi tiles inside Windows' work area, and Aether already takes the bar out of THAT
        // (ApplyWorkAreas). Offsetting by the full bar again reserved it twice - a bar-wide empty strip
        // beside every tiled window. Only ask komorebi for what its work area does not already exclude.
        { std::lock_guard<std::mutex> lk(g_komoMtx);
          if(km<(int)g_komo.size()){
              const KomoMon& K=g_komo[km];
              if(K.work.right>K.work.left && K.rect.right>K.rect.left){
                  int inL=K.work.left-K.rect.left, inT=K.work.top-K.rect.top;
                  int inR=K.rect.right-K.work.right, inB=K.rect.bottom-K.work.bottom;
                  switch(P.edge){
                      case EDGE_LEFT:  l=std::max(0,l-inL); r=l; break;
                      case EDGE_RIGHT: r=std::max(0,r-inR); break;
                      case EDGE_TOP:   tp=std::max(0,tp-inT); b=tp; break;
                      default:         b=std::max(0,b-inB); break;
                  }
              }
          } }
        if(lastOff[i][0]==l && lastOff[i][1]==tp && lastOff[i][2]==r && lastOff[i][3]==b) continue;
        lastOff[i][0]=l; lastOff[i][1]=tp; lastOff[i][2]=r; lastOff[i][3]=b;
        wchar_t a[96];
        _snwprintf_s(a,96,L"monitor-work-area-offset %d %d %d %d %d",km,l,tp,r,b);
        std::thread([cmd=std::wstring(a)]{ KomoRun(cmd,nullptr); }).detach();
    }
}

// ---- explorer.exe, optionally ---------------------------------------------------------------------
// Replacing the Windows shell means WinLogon never starts explorer.exe, and explorer is what
// registers the virtual-desktop COM classes komorebi reaches through.
//
// I first concluded explorer was REQUIRED. That was wrong, and worth writing down: two things were
// changed at once - explorer was started, and komorebi's window_hiding_behaviour was later moved
// from Cloak to Minimize - and explorer got the credit for stability it was not providing. Isolated
// afterwards: with explorer killed and Minimize set, komorebi survived seven consecutive workspace
// switches. The crash (com/mod.rs, REGDB_E_CLASSNOTREG, in a function that cannot unwind) is the
// Cloak path alone.
//
// What explorer does still seem to help with is komorebi ADOPTING windows that were already open
// when it started - with it running, more of them got picked up. So this stays available as an
// opt-in, defaulted off, because a shell replacement should not quietly resurrect the shell it
// replaced.
static void EnsureExplorerForKomorebi(){
    if(!g_keepExplorer) return;
    if(g_wsSource==WSSRC_VDESK) return;          // virtual desktops do not need komorebi at all
    if(!g_isShell) return;                       // explorer is already the shell: nothing to do
    // already up?
    { HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
      if(snap!=INVALID_HANDLE_VALUE){
          PROCESSENTRY32W pe{}; pe.dwSize=sizeof(pe); bool found=false;
          if(Process32FirstW(snap,&pe)) do {
              if(_wcsicmp(pe.szExeFile,L"explorer.exe")==0){ found=true; break; }
          } while(Process32NextW(snap,&pe));
          CloseHandle(snap);
          if(found) return;
      } }
    std::thread([]{
        SHELLEXECUTEINFOW si{}; si.cbSize=sizeof(si); si.lpVerb=L"open";
        si.lpFile=L"explorer.exe"; si.nShow=SW_SHOWNORMAL; si.fMask=SEE_MASK_NOCLOSEPROCESS;
        if(!ShellExecuteExW(&si)) return;
        if(si.hProcess) CloseHandle(si.hProcess);
        // explorer brings its taskbar with it; this shell owns that screen edge
        for(int i=0;i<12;i++){ Sleep(500); if(g_hideTaskbar) SetWindowsTaskbar(true); }
    }).detach();
}

// Win+Ctrl+<key>. Arrows MUST carry KEYEVENTF_EXTENDEDKEY: without it Windows reads them as the
// numeric keypad and the switch silently does nothing - which is what every workspace click here did.
static void SendWinCtrl(WORD key,bool extended){
    INPUT in[6]={}; for(auto&i:in){ i.type=INPUT_KEYBOARD; i.ki.dwExtraInfo=AETHER_KEY_TAG; }
    in[0].ki.wVk=VK_LWIN;    in[0].ki.dwFlags=KEYEVENTF_EXTENDEDKEY;
    in[1].ki.wVk=VK_CONTROL;
    in[2].ki.wVk=key;        in[2].ki.dwFlags=extended?KEYEVENTF_EXTENDEDKEY:0;
    in[3]=in[2];             in[3].ki.dwFlags|=KEYEVENTF_KEYUP;
    in[4].ki.wVk=VK_CONTROL; in[4].ki.dwFlags=KEYEVENTF_KEYUP;
    in[5].ki.wVk=VK_LWIN;    in[5].ki.dwFlags=KEYEVENTF_EXTENDEDKEY|KEYEVENTF_KEYUP;
    SendInput(6,in,sizeof(INPUT));
}
static std::atomic<bool> g_wsKeysBusy{false};   // one gesture sequence at a time
static void SwitchWorkspace(int target){
    target=std::clamp(target,0,g_wsCount-1);
    int steps=target-g_wsCur; if(!steps) return;
    if(g_wsKeysBusy.exchange(true)){ KomoLog("SwitchWorkspace target=%d refused: busy",target); return; }
    KomoLog("SwitchWorkspace target=%d from=%d steps=%d",target,g_wsCur,steps);
    WORD arrow = steps>0 ? VK_RIGHT : VK_LEFT; steps=abs(steps);
    // show it now; Windows confirms later (see WsHoldPending)
    g_wsPendTarget=target; g_wsPendCount=g_wsCount; g_wsPendUntil=GetTickCount64()+4000;
    g_wsCur=target; for(int i=0;i<16;i++){ g_wsRing[i].cur=target; g_wsRing[i].occ[target]=true; }
    g_deskDirty=true;
    // the keystrokes need gaps between them; on the render thread those gaps froze the whole shell
    std::thread([arrow,steps](){ for(int s=0;s<steps;s++){ SendWinCtrl(arrow,true); Sleep(80); }
                                 g_wsKeysBusy.store(false); }).detach();
}

// Win+Ctrl+D: the public "new virtual desktop" gesture. Creating one also switches to it, which is
// exactly what clicking an empty workspace should do.
static void NewWorkspace(){ KomoLog("NewWorkspace (Win+Ctrl+D) wsCount=%d wsCur=%d",g_wsCount,g_wsCur); SendWinCtrl('D',false); }
// Go to workspace `target`, adding desktops first if the user clicked one of the empty slots.
// mi = the monitor whose bar was clicked; komorebi workspaces belong to a monitor.
static void GotoWorkspace(int mi, int target){
    if(target<0) return;
    const WsRing& r=WsRingFor(mi);
    KomoLog("GotoWorkspace mi=%d target=%d | src=%d live=%d komoMon=%d count=%d",
            mi,target,g_wsSource,(int)g_komoLive.load(),r.komoMon,r.count);
    if(g_wsSource!=WSSRC_VDESK && g_komoLive.load() && r.komoMon>=0){
        // komorebi's rings are fixed-size and come from komorebi.json, so there is nothing to
        // create - every slot the bar draws already exists.
        //
        // Snapshot the workspace we are LEAVING before komorebi is told anything. This is the only
        // moment those windows are still on screen: komorebi minimises them the instant it acts, and
        // the event that tells us it acted arrives too late to capture them. Clicking a chip in the
        // bar is therefore the one route where both halves of the slide can animate.
        if(g_wsSlide.load()){
            std::vector<HWND> leaving;
            { std::lock_guard<std::mutex> lk(g_komoMtx);
              if(r.komoMon < (int)g_komo.size()){
                  const KomoMon& km=g_komo[r.komoMon];
                  int cur=std::clamp(r.cur,0,(int)km.ws.size()-1);
                  if(cur>=0 && cur<(int)km.ws.size() && cur!=std::min(target,r.count-1))
                      leaving = km.ws[cur].hwnds;
              } }
            KomoLog("preswitch snapshot: mon=%d ws=%d leaving=%d windows", r.komoMon, r.cur, (int)leaving.size());
            if(!leaving.empty()) WsSlideAdoptOutgoing(leaving,true);
        }
        KomorebiFocus(r.komoMon, std::min(target,r.count-1));
        g_wsRing[std::clamp(mi,0,15)].cur=std::clamp(target,0,r.count-1);   // the event stream confirms it
        return;
    }
    if(target>=g_wsCount){
        // Every new desktop is appended at the END and switched to, so creating the missing ones
        // already lands on the target - no arrows afterwards. The old code did send them, off a
        // registry that had not caught up yet.
        if(g_wsKeysBusy.exchange(true)){ KomoLog("Goto create target=%d refused: busy",target); return; }
        target=std::min(target,g_wsCount+7);
        int need=target+1-g_wsCount;
        g_wsPendTarget=target; g_wsPendCount=target+1; g_wsPendUntil=GetTickCount64()+3000+need*500;
        g_wsCount=target+1; g_wsCur=target;
        for(int i=0;i<16;i++){ g_wsRing[i].count=g_wsCount; g_wsRing[i].cur=target; g_wsRing[i].occ[target]=true; }
        g_deskDirty=true;
        std::thread([need](){ for(int i=0;i<need;i++){ NewWorkspace(); Sleep(300); }
                              g_wsKeysBusy.store(false); }).detach();
        return;
    }
    SwitchWorkspace(target);
}

static void ActivateWindow(HWND h){ if(IsIconic(h))ShowWindowAsync(h,SW_RESTORE);
    HWND fgw=GetForegroundWindow();
    DWORD fg=GetWindowThreadProcessId(fgw,nullptr),me=GetCurrentThreadId();
    // joining input queues with a hung thread makes our focus calls wait on it - just ask politely
    if(!fgw || fg==me || IsHungAppWindow(fgw)){ SetForegroundWindow(h); return; }
    AttachThreadInput(me,fg,TRUE); SetForegroundWindow(h); BringWindowToTop(h); SetActiveWindow(h); AttachThreadInput(me,fg,FALSE); }
