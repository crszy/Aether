// Aether - the launcher.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif
static void StvDrawDim(ImDrawList*,float,float); static void StvIntroStart(); static void StvBannerFire(int,const char*); static void StvSetSolid();   // fwd (Strive.h)

// ================================================================= launcher (hotkey app search)
// Matches the reference: compact rounded panel, results ON TOP, SEARCH BAR AT THE BOTTOM with a
// "Type > for commands" hint, rows showing name + description, and a ">wallpaper" carousel.
static HWND g_launHwnd=nullptr; static IDXGISwapChain1* g_launSc=nullptr; static ID3D11RenderTargetView* g_launRtv=nullptr;
static IDCompositionTarget* g_launTgt=nullptr; static IDCompositionVisual* g_launVis=nullptr; static ImGuiContext* g_ctxLaunch=nullptr;
static float g_launAnim=0.0f; static bool g_launFocus=false; static RECT g_launRect={0,0,0,0};
// A blurred grab of the whole monitor, taken the moment the launcher is summoned. The panel then
// samples the slice of it that sits behind wherever the panel ended up, so the frost lines up with
// the desktop instead of being stretched to the panel's box.
static ID3D11ShaderResourceView* g_launBg=nullptr;
// The same grab at four focus depths: [0] sharp, [3] fully defocused.
static const int  LAUNBG_N=4;
static ID3D11ShaderResourceView* g_launBgL[LAUNBG_N]={};
static ULONGLONG g_launOpenAt=0; static bool g_launArmed=false;
static int g_lmodeAtOpen=0;   // the view the launcher OPENED in (a clipboard/picker shortcut opens straight into one)   // input arming (see DrawLauncher)
// Every "toggle the launcher" trigger routes through here. Because the action is a TOGGLE, anything
// that delivers the trigger twice opens and shuts it in one gesture: a doubled Win key-up from a
// remapper/KVM/RDP stack, the registered hotkey arriving alongside the low-level hook, or simply an
// impatient second tap during the 850ms opening animation - the panel is not even fully on screen
// yet, so the user cannot tell the first tap landed. Refusing a CLOSE that arrives on top of an
// OPEN makes the toggle idempotent for the whole opening beat, whatever delivered it.
static ULONGLONG g_launToggleAt=0;    // when LaunToggle last asked for the launcher to open
static void LaunToggle(){
    if(ShellLocked()) return;
    const ULONGLONG now=GetTickCount64();
    // "Open" means the window is actually ON SCREEN, not just that the flag says so. If the flag goes stale -
    // true while the window is hidden - a toggle would otherwise keep trying to CLOSE something you cannot
    // see, and the launcher became permanently unopenable until a restart. But the window only appears a
    // frame or two AFTER the flag is set, so a quick second tap used to land in that gap, read as "stale",
    // and re-open instead of closing: mashing Super dropped presses and left the launcher up after an even
    // number of taps. Stale now means the flag has claimed "open" for a while with no window.
    const bool stale = g_launShow && g_launHwnd && !IsWindowVisible(g_launHwnd) && now-g_launToggleAt>400;
    if(!g_launShow || stale){ g_launShow=true; g_launToggleAt=now; return; }
    // One physical press can still arrive twice (remappers, the hook and a hotkey both firing): only a
    // repeat inside 40 ms is treated as that. The hook already debounces doubled key-ups at 100 ms.
    if(now-g_launToggleAt < 40) return;
    g_launShow=false;
}
// Hover used to take the selection unconditionally, so opening the launcher UNDER the pointer
// handed the highlight to whatever row happened to land beneath it - the selection visibly jumped
// down the list, and Enter would run that app instead of the top hit. The pointer only gets a say
// once it has actually MOVED; typing or arrowing hands control back to the keyboard.
static bool   g_launMouseLive=false;
static ImVec2 g_launMousePos=ImVec2(-99999.0f,-99999.0f);
// The REAL cursor in the launcher's logical space. io.MousePos is the wrong thing to test for "the
// pointer moved": when the launcher appears it is unset or left over from the last time it was open,
// so the test tripped on the first frame and the row under a motionless pointer took the selection -
// recorded at 60 fps, the selection jumped and the list scrolled while the pop animation played.
static ImVec2 LaunCursorLogical(){
    POINT cp; if(!GetCursorPos(&cp)) return g_launMousePos;
    return ImVec2((float)(cp.x-g_mx)/g_uiScale,(float)(cp.y-g_my)/g_uiScale);
}
// ---- multi-monitor: the pop-up panels live on ONE monitor at a time and follow the cursor ----
// (the desktop layer and the taskbar are different: they span every monitor at once)
// (re)claim the global shortcuts. Windows refuses a combination another app already owns — that is
// recorded per shortcut so Settings can say so instead of the key silently doing nothing.
static void LfxRegisterAliasHotkeys();   // fwd (LauncherFx.h)
static void RegisterHotkeys(){
    for(int i=0;i<HK_COUNT;i++){
        UnregisterHotKey(g_hwnd,i+1);
        g_hk[i].ok=false;
        if(!g_hk[i].vk) continue;
        // A held shortcut must fire ONCE: without MOD_NOREPEAT a held key repeats WM_HOTKEY ~30 times a second,
        // which toggles a panel open and shut over and over. Only the wallpaper cyclers are allowed to repeat.
        const UINT rep=(i==HK_WALLPREV||i==HK_WALLNEXT)? 0u : (UINT)MOD_NOREPEAT;
        g_hk[i].ok = RegisterHotKey(g_hwnd,i+1,g_hk[i].mods|rep,g_hk[i].vk)!=0;
    }
    LfxRegisterAliasHotkeys();             // launcher keywords that carry a hotkey
}
static void MoveOverlay(HWND h,IDXGISwapChain1* sc,ID3D11RenderTargetView** rtv,int x,int y,int w,int ht){
    if(!h) return;
    RECT cur; GetWindowRect(h,&cur);
    bool resized = (cur.right-cur.left)!=w || (cur.bottom-cur.top)!=ht;
    SetWindowPos(h,nullptr,x,y,w,ht,SWP_NOZORDER|SWP_NOACTIVATE);
    if(resized && sc && rtv){          // monitors of different sizes need the swapchain resized
        if(*rtv){ (*rtv)->Release(); *rtv=nullptr; }
        sc->ResizeBuffers(0,w,ht,DXGI_FORMAT_UNKNOWN,0);
        ID3D11Texture2D* bb=nullptr; sc->GetBuffer(0,IID_PPV_ARGS(&bb));
        if(bb){ g_dev->CreateRenderTargetView(bb,nullptr,rtv); bb->Release(); }
    }
}
static void PlaceOverlaysOnActive(){
    int PXs=(int)(g_uiScale+0.0f);(void)PXs;
    auto PXv=[&](float v){ return (int)(v*g_uiScale+0.5f); };
    MoveOverlay(g_hwnd,   g_sc,     &g_rtv,     g_mx,g_my,g_mw,g_mh);   // dashboard drawer
    MoveOverlay(g_sideHwnd,g_sideSc,&g_sideRtv, g_mx,g_my,g_mw,g_mh);   // quick settings
    MoveOverlay(g_sessHwnd,g_sessSc,&g_sessRtv, g_mx,g_my,g_mw,g_mh);
    // The launcher and Settings take focus. A focused window covering the WHOLE monitor is what Wallpaper Engine
    // calls a fullscreen app, and its default rule stops the wallpaper - opening search or Settings froze the
    // live wallpaper. One pixel short of the monitor's width is not fullscreen; both panels are centred, so the
    // missing column is never drawn anyway.
    MoveOverlay(g_launHwnd,g_launSc,&g_launRtv, g_mx,g_my,g_mw-1,g_mh);
    MoveOverlay(g_setHwnd, g_setSc, &g_setRtv,  g_mx,g_my,g_mw-1,g_mh);
    MoveOverlay(g_dimHwnd, g_dimSc, &g_dimRtv,  g_mx,g_my,g_mw,g_mh);
    MoveOverlay(g_notifHwnd,g_notifSc,&g_notifRtv, g_mx+g_mw-PXv(NOTIFWINW),g_my,PXv(NOTIFWINW),g_mh);
    MoveOverlay(g_swHwnd,   g_swSc,   &g_swRtv,    g_mx,g_my,g_mw,g_mh);
    MoveOverlay(g_ovHwnd,   g_ovSc,   &g_ovRtv,    g_mx,g_my,g_mw,g_mh);
    MoveOverlay(g_medHwnd, g_medSc, &g_medRtv,  g_mx+g_mw-PXv(MEDIAWINW),g_my,PXv(MEDIAWINW),PXv(MEDIAWINH));
    MoveOverlay(g_osdHwnd, g_osdSc, &g_osdRtv,  g_mx+g_mw-PXv(OSDW),g_my+(g_mh-PXv(OSDH))/2,PXv(OSDW),PXv(OSDH));
}
static void UpdateActiveMonitor(){
    if(g_mons.size()<2) return;
    POINT cp; if(!GetCursorPos(&cp)) return;
    int mi=MonIndexAt(cp);
    if(mi==g_actMon) return;
    if(mi>=0 && mi<(int)g_mons.size() && !g_mons[mi].cfg.panels) return;  // panels off for that display
    // never teleport a panel that is currently on screen — wait until everything is put away
    if(g_setShow||g_launShow||g_sessShow||g_reveal>0.004f||g_sideReveal>0.004f||
       g_dimAnim>0.004f||g_notifReveal>0.004f) return;
    g_actMon=mi;
    const RECT& r=MonRect(mi);
    g_mx=r.left; g_my=r.top; g_mw=r.right-r.left; g_mh=r.bottom-r.top;
    PlaceOverlaysOnActive();
}

struct LaunchApp{ std::wstring name,nameLower,path; std::string desc; ID3D11ShaderResourceView* icon=nullptr; bool tried=false; };
static std::vector<LaunchApp> g_lapps; static std::vector<int> g_lfilt; static char g_lsearch[256]={0}; static int g_lsel=0;
static int g_lmode=0;   // 0 apps, 1 commands, 2 wallpaper carousel, 3 colour schemes
static int g_schemeSel=0;

// ---- commands (">" mode) ----
struct LCmd{ const char* name; const char* desc; };
static const LCmd LCMDS[]={
    {"clip",      "Clipboard history"},
    {"files",     "Open the file manager"},
    {"wallpaper", "Browse and change the desktop wallpaper"},
    {"settings",  "Open Aether settings"},
    {"utilities", "Keep awake, screen recorder, do not disturb"},
    {"theme",     "Toggle between the light and dark scheme"},
    {"scheme",    "Pick a colour scheme"},
    {"dnd",       "Toggle do not disturb"},
    {"reload",    "Restart the shell"},
    {"lock",      "Lock the session"},
    {"sleep",     "Suspend the PC"},
    {"restart",   "Restart Windows"},
    {"shutdown",  "Shut down Windows"},
    {"quit",      "Exit the shell"},
    {"calculator","Do some maths (just type an expression)"},
    {"variant",   "Change the current scheme variant"},
    {"random",    "Switch to a random wallpaper"},
    {"light",     "Change the scheme to light mode"},
    {"dark",      "Change the scheme to dark mode"},
    {"unhide",    "Show every app you hid from the launcher"},
};
static const char* LCmdIcon(const char* n){
    struct P{ const char* n; const char* i; } m[]={ {"clip","content_paste"},{"files","folder"},{"wallpaper","image"},{"settings","settings"},
        {"utilities","build"},{"theme","contrast"},{"scheme","palette"},{"dnd","notifications_off"},{"reload","refresh"},{"lock","lock"},
        {"sleep","bedtime"},{"restart","restart_alt"},{"shutdown","power_settings_new"},{"quit","logout"},{"calculator","calculate"},
        {"variant","invert_colors"},{"random","shuffle"},{"light","light_mode"},{"dark","dark_mode"},{"unhide","visibility"} };
    for(auto& p:m) if(!strcmp(p.n,n)) return p.i; return "terminal"; }
static const int NLCMDS=(int)(sizeof(LCMDS)/sizeof(LCMDS[0]));
static std::vector<int> g_cfilt;
// Open windows matching the query. Deliberately NOT indices into g_dockRaw: that vector is a
// scratch buffer whose strings the grouping pass std::moves out, so every title in it reads back
// empty. Titles are pulled live from the window instead - they are current that way too, which
// matters for anything whose caption tracks its document.
struct LWin { HWND h=nullptr; std::string title; std::string exe; ID3D11ShaderResourceView* icon=nullptr; };
static std::vector<LWin> g_winHits;

// ---- wallpapers (shared by the carousel and the Settings wallpaper grid) ----
static Spring g_wallSpring;   // cover-flow scroll (damped spring, target always 0)
static std::atomic<bool> g_wallScanned{false}, g_wallReady{false};
// guards g_walls' tex/tried fields against the thumbnail worker below (the vector itself is built
// once by the scan thread before g_wallReady is published, and only WallRescan ever resizes it)

static bool IsImageExt(const std::wstring& n){
    size_t d=n.find_last_of(L'.'); if(d==std::wstring::npos) return false;
    std::wstring e=n.substr(d); for(auto&c:e)c=(wchar_t)towlower(c);
    return e==L".jpg"||e==L".jpeg"||e==L".png"||e==L".bmp"||e==L".webp";
}
static void WallScanDir(std::vector<Wall>& out,const std::wstring& dir,int depth){
    int maxDepth = g_wallRecursive? 6 : 1;
    if(dir.empty()||depth>maxDepth||out.size()>1200) return;
    std::wstring pat=dir+L"\\*"; WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW(pat.c_str(),&fd);
    if(h==INVALID_HANDLE_VALUE) return;
    do{ std::wstring nm=fd.cFileName; if(nm==L"."||nm==L"..") continue;
        std::wstring full=dir+L"\\"+nm;
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){ WallScanDir(out,full,depth+1); continue; }
        if(!IsImageExt(nm)) continue;
        if(out.size()>1200) break;
        bool dup=false; for(auto&w:out) if(w.path==full){dup=true;break;}
        if(!dup){ Wall w; w.path=full; w.name=W2U8(nm); out.push_back(std::move(w)); }
    }while(FindNextFileW(h,&fd));
    FindClose(h);
}
// ---- Wallpaper Engine library -----------------------------------------------------------------------
// Wallpapers live as folders holding a project.json (title, type, preview image): Steam Workshop items in
// <library>\steamapps\workshop\content\431960\, plus the app's own projects\myprojects and
// defaultprojects. Steam lists every library in steamapps\libraryfolders.vdf.
static std::wstring WeFindExe(){
    // the running process knows exactly where it is
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap!=INVALID_HANDLE_VALUE){ PROCESSENTRY32W pe{sizeof(pe)};
        if(Process32FirstW(snap,&pe)) do{
            if(_wcsicmp(pe.szExeFile,L"wallpaper64.exe")==0 || _wcsicmp(pe.szExeFile,L"wallpaper32.exe")==0){
                HANDLE ph=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pe.th32ProcessID);
                if(ph){ wchar_t p[MAX_PATH]; DWORD n=MAX_PATH;
                    if(QueryFullProcessImageNameW(ph,0,p,&n)){ CloseHandle(ph); CloseHandle(snap); return p; }
                    CloseHandle(ph); } }
        } while(Process32NextW(snap,&pe));
        CloseHandle(snap); }
    return L"";
}
static std::vector<std::wstring> SteamLibraries(){
    std::vector<std::wstring> libs;
    std::wstring steam=RegString(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath");
    for(auto& c:steam) if(c==L'/') c=L'\\';
    if(steam.empty()) steam=L"C:\\Program Files (x86)\\Steam";
    libs.push_back(steam);
    std::ifstream f(steam+L"\\steamapps\\libraryfolders.vdf");
    std::string line;
    while(std::getline(f,line)){
        size_t k=line.find("\"path\""); if(k==std::string::npos) continue;
        size_t a=line.find('"',k+6); if(a==std::string::npos) continue;
        size_t b=line.find('"',a+1); if(b==std::string::npos) continue;
        std::string p=line.substr(a+1,b-a-1);
        std::string u; for(size_t i=0;i<p.size();i++){ if(p[i]=='\\' && i+1<p.size() && p[i+1]=='\\') i++; u+=p[i]; }
        std::wstring w=U82W(u); bool dup=false; for(auto& l:libs) if(_wcsicmp(l.c_str(),w.c_str())==0) dup=true;
        if(!dup) libs.push_back(w);
    }
    return libs;
}
// Workshop titles are often Japanese, Chinese or Korean, and the shell's fonts carry no CJK - every such
// character drew as '?', so names read "????-????". Keep only what the atlas can draw (the RANGES table),
// tidy the brackets and separators that are left hollow, and fall back to the workshop id if nothing is.
static std::string FontSafeName(const std::string& in,const std::wstring& folder){
    auto drawable=[](unsigned cp){ for(int i=0;RANGES[i];i+=2) if(cp>=RANGES[i]&&cp<=RANGES[i+1]) return true; return false; };
    std::string out; const unsigned char* p=(const unsigned char*)in.c_str();
    while(*p){
        unsigned cp; int len;
        if(*p<0x80){ cp=*p; len=1; } else if((*p>>5)==6){ cp=((p[0]&31)<<6)|(p[1]&63); len=2; }
        else if((*p>>4)==14){ cp=((p[0]&15)<<12)|((p[1]&63)<<6)|(p[2]&63); len=3; }
        else { cp=0xFFFF; len=((*p>>3)==30)?4:1; }
        bool ok=drawable(cp);
        if(ok) out.append((const char*)p,len); else if(!out.empty() && out.back()!=' ') out+=' ';
        for(int i=0;i<len && *p;i++) p++;
    }
    // hollow pairs and dangling separators left behind by removed characters
    for(const char* hollow : {"[]","()","{}","[ ]","( )","\"\""}){
        size_t k; while((k=out.find(hollow))!=std::string::npos) out.erase(k,strlen(hollow)); }
    std::string t; for(char c:out){ if(c==' ' && (t.empty()||t.back()==' ')) continue; t+=c; }
    while(!t.empty() && strchr(" -_|/~:.",t.back())) t.pop_back();
    size_t st=0; while(st<t.size() && strchr(" -_|/~:.",t[st])) st++;
    t=t.substr(st);
    int alnum=0; for(char c:t) if(isalnum((unsigned char)c)) alnum++;
    if(alnum<2) t="Workshop "+W2U8(folder);
    return t;
}
static void WeScanProjects(std::vector<Wall>& out,const std::wstring& dir){
    WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW((dir+L"\\*").c_str(),&fd);
    if(h==INVALID_HANDLE_VALUE) return;
    do{
        if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0]==L'.') continue;
        std::wstring folder=dir+L"\\"+fd.cFileName, proj=folder+L"\\project.json";
        std::ifstream pf(proj,std::ios::binary); if(!pf) continue;
        std::stringstream ss; ss<<pf.rdbuf(); std::string c=ss.str();
        std::string title=jstr(c,"title"), prev=jstr(c,"preview"), type=jstr(c,"type");
        if(prev.empty()) continue;                            // nothing to show in the picker
        std::wstring pv=folder+L"\\"+U82W(prev);
        if(GetFileAttributesW(pv.c_str())==INVALID_FILE_ATTRIBUTES) continue;
        Wall w; w.path=pv; w.weProj=proj; w.name=FontSafeName(title.empty()? W2U8(fd.cFileName) : title, fd.cFileName);
        out.push_back(std::move(w));
        if(out.size()>4000) break;
    } while(FindNextFileW(h,&fd));
    FindClose(h);
}
static void WeScanInto(std::vector<Wall>& out){
    std::wstring exe=WeFindExe(); std::vector<Wall> we;
    for(const std::wstring& lib:SteamLibraries()){
        WeScanProjects(we,lib+L"\\steamapps\\workshop\\content\\431960");
        std::wstring app=lib+L"\\steamapps\\common\\wallpaper_engine";
        if(exe.empty()){
            if(GetFileAttributesW((app+L"\\wallpaper64.exe").c_str())!=INVALID_FILE_ATTRIBUTES) exe=app+L"\\wallpaper64.exe";
            else if(GetFileAttributesW((app+L"\\wallpaper32.exe").c_str())!=INVALID_FILE_ATTRIBUTES) exe=app+L"\\wallpaper32.exe"; }
        WeScanProjects(we,app+L"\\projects\\myprojects");
        WeScanProjects(we,app+L"\\projects\\defaultprojects");
    }
    if(exe.empty()) return;                                   // wallpapers without the app to play them: skip
    { std::lock_guard<std::mutex> lk(g_weMtx); g_weExe=exe; }
    std::sort(we.begin(),we.end(),[](const Wall& a,const Wall& b){ return _stricmp(a.name.c_str(),b.name.c_str())<0; });
    for(auto& w:we) out.push_back(std::move(w));
}
// Runs on a worker: enumerating Pictures can take seconds, and doing it inline stalled the first
// frame of the Settings app. g_walls is written once, then published via the g_wallReady release.
// Scans the configured folder list (WindowPaper's model). With no folders configured it falls
// back to the current wallpaper's folder plus Pictures, so the picker is never empty out of the box.
static void WallScan(){
    if(g_wallScanned.exchange(true)) return;
    std::thread([]{
        std::vector<Wall> out;
        wchar_t cur[MAX_PATH]={0};
        SystemParametersInfoW(SPI_GETDESKWALLPAPER,MAX_PATH,cur,0);
        if(g_wallSource==WSRC_LIVE){                          // live wallpapers only
            WeScanInto(out);
            g_walls=std::move(out); g_wallSel=0;
            g_wallReady.store(true,std::memory_order_release);
            return;
        }
        for(auto& f:g_wallFolders) WallScanDir(out,U82W(f),0);
        // only when nothing is configured (or you asked for them) - see g_wallBundled
        if(g_wallBundled || g_wallFolders.empty())
            WallScanDir(out,U82W(ExeDir()+"linux\\wallpapers\\cachyos"),0);
        if(g_wallFolders.empty()){
            if(cur[0]){ std::wstring c=cur; size_t s=c.find_last_of(L"\\/");
                if(s!=std::wstring::npos) WallScanDir(out,c.substr(0,s),0); }
            wchar_t pic[MAX_PATH];
            if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_MYPICTURES,nullptr,0,pic))){
                WallScanDir(out,std::wstring(pic)+L"\\Wallpapers",0);
                WallScanDir(out,pic,0); }
        }
        std::sort(out.begin(),out.end(),[](const Wall&a,const Wall&b){ return a.path<b.path; });
        int sel=0;   // open the carousel on the wallpaper that is already set
        if(cur[0]) for(size_t i=0;i<out.size();i++) if(_wcsicmp(out[i].path.c_str(),cur)==0){ sel=(int)i; break; }
        g_walls=std::move(out); g_wallSel=sel;
        g_wallReady.store(true,std::memory_order_release);
    }).detach();
}
// re-run the scan after the folder list changes
static void WallRescan(){
    g_wallReady.store(false,std::memory_order_release);
    { std::lock_guard<std::mutex> lk(g_wallMtx);
      for(auto& w:g_walls) if(w.tex) w.tex->Release();
      g_walls.clear(); }
    g_wallScanned.store(false); WallScan();
}

// apply a specific transition by name (used by the prev/next hotkeys and the slideshow)
static void SetWallpaperWith(const std::wstring& path,const std::string& transName){
    WTrans save=g_transCfg; g_transCfg=ParseTrans(transName);
    SetWallpaperFile(path, DeskPt(0.5f,0.5f));
    g_transCfg=save;
}
// step through the scanned wallpapers (Ctrl+Alt+[ / ]) — swww next/prev
static void CycleWallpaper(int dir){
    if(!g_wallReady.load(std::memory_order_acquire) || g_walls.empty()) return;
    int n=(int)g_walls.size();
    g_wallSel = ((g_wallSel+dir)%n+n)%n;
    SetWallpaperWith(g_walls[g_wallSel].path, dir<0? g_transPrevName : g_transNextName);
}
// The slideshow used the Ctrl+Alt+] KEYBIND transition, so with a slideshow running the transition
// chosen in Settings never showed - "no matter which wallpaper animation I choose it doesn't change".
// The slideshow is not a keybind: it uses the main transition. (The keybinds keep their own settings,
// which are labelled as theirs on the same page.)
static void SlideshowAdvance(int dir=+1){
    if(!g_wallReady.load(std::memory_order_acquire) || g_walls.size()<2) return;
    int n=(int)g_walls.size();
    if(g_slideShuffle && dir>0){ int pick=g_wallSel; while(pick==g_wallSel) pick=rand()%n; g_wallSel=pick; }
    else g_wallSel=((g_wallSel+dir)%n+n)%n;
    SetWallpaperFile(g_walls[g_wallSel].path, DeskPt(0.5f,0.5f));   // g_transCfg: the user's transition
}
// wallust/pywal-style palette export: 8 dominant colours from the applied wallpaper
static void ExportPalette(const std::wstring& path){
    std::vector<uint8_t> px; int w=0,h=0;
    if(!DecodeFirstFrame(path,96,px,w,h)) return;
    struct Bin{ double r=0,g=0,b=0; int n=0; };
    std::unordered_map<int,Bin> bins;
    for(int i=0;i<w*h;i++){
        int b=px[i*4],g=px[i*4+1],r=px[i*4+2];
        int key=((r>>5)<<10)|((g>>5)<<5)|(b>>5);
        Bin& x=bins[key]; x.r+=r; x.g+=g; x.b+=b; x.n++;
    }
    std::vector<Bin> v; for(auto& kv:bins) v.push_back(kv.second);
    std::sort(v.begin(),v.end(),[](const Bin&a,const Bin&b){ return a.n>b.n; });
    auto hex=[](const Bin& x){ char c[16];
        snprintf(c,16,"#%02X%02X%02X",(int)(x.r/x.n),(int)(x.g/x.n),(int)(x.b/x.n)); return std::string(c); };
    wchar_t home[MAX_PATH]; if(FAILED(SHGetFolderPathW(nullptr,CSIDL_PROFILE,nullptr,0,home))) return;
    std::ofstream f(std::wstring(home)+L"\\.caelestia_colors.json");
    if(!f) return;
    f<<"{\n  \"wallpaper\": \""<<jesc(W2U8(path))<<"\",\n";
    f<<"  \"background\": \""<<(v.empty()?"#000000":hex(v[0]))<<"\",\n";
    f<<"  \"foreground\": \""<<(v.size()>1?hex(v[1]):"#FFFFFF")<<"\",\n  \"colors\": [";
    for(int i=0;i<8;i++){ f<<(i?", ":"")<<"\""<<(i<(int)v.size()?hex(v[i]):"#000000")<<"\""; }
    f<<"]\n}\n";
}
// post-apply hook: {path} is replaced with the wallpaper file
static void RunPostApply(const std::wstring& path){
    if(g_postApplyCmd.empty()) return;
    std::string cmd=g_postApplyCmd, tok="{path}";
    for(size_t p=cmd.find(tok); p!=std::string::npos; p=cmd.find(tok,p))
        { cmd.replace(p,tok.size(),W2U8(path)); p+=path.size(); }
    std::wstring args=L"/c "+U82W(cmd);
    AetherShellExec(nullptr,L"open",L"cmd.exe",args.c_str(),nullptr,SW_HIDE);
}
// THUMBNAILS RUN ON A WORKER, AND ONLY FOR WHAT IS ON SCREEN.
// This used to decode 2 wallpapers per FRAME on the render thread, walking the WHOLE library rather
// than the ~20 cards the grid actually draws. On a 101-wallpaper folder of 4K PNGs (some 20 MB) that
// is ~300 MB of WIC decoding on the thread that draws the shell: opening Settings froze everything
// for many seconds, then stayed choppy while the tail drained. Same root cause and same fix as the
// Alt+Tab switcher's PrintWindow stall - get the slow work off the render thread. MakeTextureBGRA
// only touches ID3D11Device (free-threaded for resource creation), never the immediate context, so
// building textures here is legal.
static std::atomic<int>  g_wallThumbWant{0};    // how many LEADING entries the grid needs
static std::atomic<bool> g_wallThumbRun{false};
static void WallThumbWorker(){
    while(g_wallThumbRun.load(std::memory_order_acquire)){
        int want=g_wallThumbWant.load(std::memory_order_acquire);
        bool did=false;
        if(want>0 && g_wallReady.load(std::memory_order_acquire)){
            for(int i=0;i<want;i++){
                if(!g_wallThumbRun.load(std::memory_order_acquire)) break;
                std::wstring path;
                { std::lock_guard<std::mutex> lk(g_wallMtx);
                  if(i>=(int)g_walls.size()) break;
                  if(g_walls[i].tried) continue;
                  g_walls[i].tried=true; path=g_walls[i].path; }
                std::vector<uint8_t> px; int iw=0,ih=0;
                if(DecodeFirstFrame(path,320,px,iw,ih)){
                    ID3D11ShaderResourceView* t=MakeTextureBGRA(px.data(),iw,ih);
                    std::lock_guard<std::mutex> lk(g_wallMtx);
                    // the list can be rebuilt underneath us by WallRescan, so re-check identity
                    if(i<(int)g_walls.size() && g_walls[i].path==path && !g_walls[i].tex){
                        g_walls[i].tex=t; g_walls[i].w=iw; g_walls[i].h=ih; }
                    else if(t) t->Release();
                }
                did=true;
                std::this_thread::sleep_for(std::chrono::milliseconds(4));   // leave the GPU alone
            }
        }
        if(!did) std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
}
// called from the render loop: publish how many thumbnails are visible, start the worker once
static void WallThumbs(int visible){
    g_wallThumbWant.store(std::max(0,visible),std::memory_order_release);
    if(!g_wallThumbRun.exchange(true)) std::thread(WallThumbWorker).detach();
}

static ID3D11ShaderResourceView* LFileIcon(const std::wstring& path){
    SHFILEINFOW sfi={}; if(SHGetFileInfoW(path.c_str(),0,&sfi,sizeof(sfi),SHGFI_ICON|SHGFI_LARGEICON)){
        ID3D11ShaderResourceView* t=IconTex(sfi.hIcon,40); if(sfi.hIcon)DestroyIcon(sfi.hIcon); return t; } return nullptr; }
static void LScanDir(const std::wstring& dir){ std::wstring pat=dir+L"\\*"; WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW(pat.c_str(),&fd); if(h==INVALID_HANDLE_VALUE)return;
    do{ std::wstring nm=fd.cFileName; if(nm==L"."||nm==L"..")continue; std::wstring full=dir+L"\\"+nm;
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)LScanDir(full);
        else{ size_t dot=nm.find_last_of(L'.'); if(dot==std::wstring::npos)continue; std::wstring ext=nm.substr(dot); for(auto&c:ext)c=(wchar_t)towlower(c);
            if(ext==L".lnk"||ext==L".url"){ LaunchApp a; a.name=nm.substr(0,dot); a.nameLower=a.name; for(auto&c:a.nameLower)c=(wchar_t)towlower(c); a.path=full;
                // description = the Start Menu folder it came from (or just "Application")
                size_t s=dir.find_last_of(L"\\/"); std::wstring parent = (s==std::wstring::npos)? L"" : dir.substr(s+1);
                a.desc = (parent.empty()||parent==L"Programs"||parent==L"Start Menu")? "Application" : W2U8(parent);
                g_lapps.push_back(std::move(a)); } }
    }while(FindNextFileW(h,&fd)); FindClose(h); }
// scan a plain folder for executables (Settings > Launcher "app folders"): its .exe files become
// launcher entries, so an app that isn't in the Start Menu (e.g. a project build folder) is reachable
static void LScanExeDir(const std::wstring& dir){
    std::wstring pat=dir+L"\\*.exe"; WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW(pat.c_str(),&fd);
    if(h==INVALID_HANDLE_VALUE)return;
    std::wstring leaf; { std::wstring d=dir; while(!d.empty()&&(d.back()==L'\\'||d.back()==L'/'))d.pop_back();
        size_t s=d.find_last_of(L"\\/"); leaf=(s==std::wstring::npos)?d:d.substr(s+1); }
    do{
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring nm=fd.cFileName; size_t dot=nm.find_last_of(L'.');
        LaunchApp a; a.name=(dot==std::wstring::npos)?nm:nm.substr(0,dot);
        a.nameLower=a.name; for(auto&c:a.nameLower)c=(wchar_t)towlower(c);
        a.path=dir; if(!a.path.empty()&&a.path.back()!=L'\\')a.path+=L'\\'; a.path+=nm;
        a.desc=W2U8(leaf);
        g_lapps.push_back(std::move(a));
    }while(FindNextFileW(h,&fd));
    FindClose(h);
}
// Since we ARE the shell (explorer.exe replacement), surface Windows' own Settings pages + our own
// toggles right in the launcher. path = an ms-settings: URI (ShellExecute opens it) or a cw:// sentinel
// handled specially in LLaunch. Searchable by name like any app.
static void LAddSynthetic(){
    struct SE{ const wchar_t* name; const wchar_t* path; const char* desc; };
    static const SE ITEMS[]={
        {L"Display Settings",        L"ms-settings:display",          "Monitors, resolution, arrangement"},
        {L"Refresh Rate (Hz)",       L"ms-settings:display-advanced", "Check & change your monitor's Hz"},
        {L"Scale & Layout",          L"ms-settings:display",          "Change the UI scale / text size"},
        {L"Night Light",             L"ms-settings:nightlight",       "Warm the display colour at night"},
        {L"Graphics Settings",       L"ms-settings:display-advancedgraphics", "Per-app GPU / HDR options"},
        {L"Windows Settings",        L"ms-settings:",                 "Open all Windows settings"},
        {L"Sound",                   L"ms-settings:sound",            "Output/input devices and volume"},
        {L"Bluetooth & Devices",     L"ms-settings:bluetooth",        "Pair and manage devices"},
        {L"Network & Internet",      L"ms-settings:network-status",   "Wi-Fi, ethernet, VPN"},
        {L"Power & Battery",         L"ms-settings:powersleep",       "Sleep, screen timeout, battery"},
        {L"Personalisation",         L"ms-settings:personalization",  "Windows colours & background"},
        {L"Apps & Features",         L"ms-settings:appsfeatures",     "Uninstall or repair apps"},
        {L"Windows Update",          L"ms-settings:windowsupdate",    "Check for updates"},
        {L"Task Manager",            L"cw://taskmgr",                 "Open Task Manager"},
        {L"Lock screen",             L"cw://lock",                    "Lock the screen"},
        {L"Lock countdown",          L"cw://lockcountdown",           "Preview the pre-lock countdown, then lock"},
        {L"Transparent Windows (Mica)", L"cw://mica",                 "Toggle Mica glass on every app window"},
        {L"Acrylic Windows",         L"cw://acrylic",                 "Toggle blurred acrylic on every app window"},
    };
    for(const SE& s:ITEMS){ LaunchApp a; a.name=s.name; a.nameLower=s.name;
        for(auto&c:a.nameLower)c=(wchar_t)towlower(c); a.path=s.path; a.desc=s.desc; a.tried=true; // no file icon
        g_lapps.push_back(std::move(a)); }
    // colour-scheme switcher (Caelestia's launcher SchemeItem): one searchable entry per scheme
    for(int i=0;i<NSCHEMES;i++){ LaunchApp a;
        std::string nm=std::string("Theme: ")+SCHEMES[i].name;
        a.name=U82W(nm); a.nameLower=a.name; for(auto&c:a.nameLower)c=(wchar_t)towlower(c);
        a.path=U82W(std::string("cw://scheme/")+SCHEMES[i].id);
        a.desc=std::string(SCHEMES[i].family)+(SCHEMES[i].dark?" \xC2\xB7 dark":" \xC2\xB7 light");
        a.tried=true; g_lapps.push_back(std::move(a)); }
}
static void LScanApps(){ g_lapps.clear(); wchar_t buf[MAX_PATH];
    if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_COMMON_PROGRAMS,nullptr,0,buf)))LScanDir(buf);
    if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PROGRAMS,nullptr,0,buf)))LScanDir(buf);
    for(auto& d:g_launchDirs) LScanExeDir(U82W(d));   // extra app folders
    LAddSynthetic();                                  // Windows settings + Mica toggles
    std::sort(g_lapps.begin(),g_lapps.end(),[](const LaunchApp&a,const LaunchApp&b){ if(a.nameLower!=b.nameLower)return a.nameLower<b.nameLower; return a.path<b.path; });
    g_lapps.erase(std::unique(g_lapps.begin(),g_lapps.end(),[](const LaunchApp&a,const LaunchApp&b){return a.nameLower==b.nameLower;}),g_lapps.end()); }
// ---- launcher calculator (Caelestia's CalcItem): type a maths expression, get a live result ----
// Tiny recursive-descent evaluator: + - * / % ^, unary +/-, parentheses, decimals. No dependencies.
static bool g_calcOk=false; static double g_calcVal=0; static std::string g_calcExpr;
struct CalcP{ const char* s; bool ok=true;
    void ws(){ while(*s==' '||*s=='\t')s++; }
    double num(){ ws(); char* e=nullptr; double v=strtod(s,&e); if(e==s){ok=false;return 0;} s=e; return v; }
    double factor(){ ws();
        if(*s=='('){ s++; double v=expr(); ws(); if(*s==')')s++; else ok=false; return v; }
        if(*s=='-'){ s++; return -factor(); }
        if(*s=='+'){ s++; return  factor(); }
        return num(); }
    double power(){ double b=factor(); ws(); if(*s=='^'){ s++; return pow(b,power()); } return b; }
    double term(){ double v=power(); for(;;){ ws();
        if(*s=='*'){ s++; v*=power(); } else if(*s=='/'){ s++; double d=power(); v = d!=0? v/d : (ok=false,0); }
        else if(*s=='%'){ s++; double d=power(); v = d!=0? fmod(v,d) : (ok=false,0); } else break; } return v; }
    double expr(){ double v=term(); for(;;){ ws();
        if(*s=='+'){ s++; v+=term(); } else if(*s=='-'){ s++; v-=term(); } else break; } return v; }
};
static bool EvalExpr(const std::string& in,double& out){
    // must contain a digit and at least one operator/paren, else it's just a search term
    bool hasDigit=false,hasOp=false;
    for(char c:in){ if(c>='0'&&c<='9')hasDigit=true; if(c=='+'||c=='-'||c=='*'||c=='/'||c=='%'||c=='^'||c=='('||c==')')hasOp=true; }
    if(!hasDigit||!hasOp) return false;
    CalcP p{in.c_str()}; double v=p.expr(); p.ws();
    if(!p.ok||*p.s!='\0') return false;
    if(!(v==v)||v==HUGE_VAL||v==-HUGE_VAL) return false;   // NaN / inf guard
    out=v; return true;
}
static void LCalcUpdate(){ g_calcOk = (g_lsearch[0] && g_lsearch[0]!='>') ? EvalExpr(g_lsearch,g_calcVal) : false;
    if(g_calcOk) g_calcExpr=g_lsearch; }
// ---- search scoring, after Flow Launcher's StringMatcher -------------------------------------
// Flow Launcher (github.com/Flow-Launcher/Flow.Launcher) is MIT licensed - see THIRD-PARTY.md for
// the notice. Its matcher is C# on .NET/WPF and cannot be compiled into a native C++ shell, so this
// is a reimplementation of the same algorithm rather than a copy of the file; MIT permits that as
// long as the notice travels with it.
//
// What it buys over the matcher it replaces, which was a plain substring test plus a gap-penalised
// subsequence walk:
//   * ACRONYMS - "vsc" now finds "Visual Studio Code". The old one could only reach it as a
//     scattered subsequence and scored it no better than any other name containing v, s and c.
//   * MULTI-WORD queries - "vis co" matches, each token scored on its own.
//   * COMPACTNESS - Flow's core idea. An early, tight run beats letters scattered down a long
//     name, so typing "code" no longer surfaces "Command Prompt Options Decoder" first.
static bool LIsWordStart(const std::wstring& s,size_t i){
    if(i==0) return true;
    wchar_t p=s[i-1], c=s[i];
    if(iswspace(p)||p==L'-'||p==L'_'||p==L'.'||p==L'/'||p==L'\\') return true;
    if(iswupper(c)&&!iswupper(p)) return true;          // camelCase boundary
    if(iswdigit(c)&&!iswdigit(p)) return true;
    return false;
}
// Flow's acronym pass: what fraction of the name's initials the query consumes, 0..100. Needs the
// ORIGINAL casing, which is why the lowercased copy alone was never enough to do this.
static int LAcronymScore(const std::wstring& disp,const std::wstring& tok){
    if(tok.empty()) return 0;
    int total=0, matched=0; size_t ni=0;
    for(size_t i=0;i<disp.size();++i){
        if(!LIsWordStart(disp,i)) continue;
        total++;
        if(ni<tok.size() && (wchar_t)towlower(disp[i])==tok[ni]){ matched++; ni++; }
    }
    if(total==0 || ni!=tok.size()) return 0;
    return (matched*100)/total;
}
// Flow's fuzzy pass: 100*(len+1) / ((1+first) + (span+1)). Early and tight scores highest.
static bool LFuzzyScore(const std::wstring& lower,const std::wstring& tok,int& out){
    size_t ni=0; int first=-1,last=-1;
    for(size_t i=0;i<lower.size()&&ni<tok.size();++i)
        if(lower[i]==tok[ni]){ if(first<0)first=(int)i; last=(int)i; ++ni; }
    if(ni!=tok.size()) return false;
    int span=last-first;
    int sc=(int)((100.0*((double)tok.size()+1.0))/((1.0+(double)first)+((double)span+1.0)));
    int diff=(int)lower.size()-(int)tok.size();          // a short name that matches beats a long one
    if(diff<5) sc+=20; else if(diff<10) sc+=10;
    out=sc; return true;
}
// one whitespace-separated token
static bool LScore1(const std::wstring& disp,const std::wstring& lower,const std::wstring& tok,int& out){
    size_t p=lower.find(tok);
    if(p!=std::wstring::npos){                            // a contiguous run is the strongest signal
        int sc=1000-(int)p*8-(int)lower.size()/4;
        if(p==0) sc+=400;                                 // starts the name
        else if(LIsWordStart(disp,p)) sc+=200;            // starts a word inside it
        out=sc; return true;
    }
    int a=LAcronymScore(disp,tok);
    if(a>0){ out=500+a*3; return true; }
    int f; if(LFuzzyScore(lower,tok,f)){ out=f; return true; }
    return false;
}
static bool LMatch(const std::wstring& disp,const std::wstring& lower,const std::wstring& needle,int& score){
    if(needle.empty()){ score=0; return true; }
    int total=0; bool any=false;
    size_t i=0;
    while(i<needle.size()){
        while(i<needle.size() && needle[i]==L' ') ++i;
        size_t j=i; while(j<needle.size() && needle[j]!=L' ') ++j;
        if(j>i){
            int sc; if(!LScore1(disp,lower,needle.substr(i,j-i),sc)) return false;   // every token must land
            total+=sc; any=true;
        }
        i=j;
    }
    if(!any){ score=0; return true; }
    score=total-(int)lower.size();                        // shorter names break ties
    return true;
}
static void LRebuild(){
    std::string q=g_lsearch;
    if(!q.empty() && q[0]=='>'){                                  // command mode
        if(g_lmode!=2&&g_lmode!=3&&g_lmode!=4) g_lmode=1;   // those modes own the launcher until Esc
        std::string needle=q.substr(1); for(auto&c:needle)c=(char)tolower(c);
        g_cfilt.clear();
        for(int i=0;i<NLCMDS;i++){ std::string n=LCMDS[i].name;
            if(needle.empty()||n.find(needle)!=std::string::npos) g_cfilt.push_back(i); }
        g_lsel=0; return;
    }
    g_lmode=0;
    LCalcUpdate();                                                // maths preview row (Caelestia CalcItem)
    std::wstring needle=U82W(g_lsearch); for(auto&c:needle)c=(wchar_t)towlower(c);
    struct Hit{int idx,sc;}; std::vector<Hit> hits; for(int i=0;i<(int)g_lapps.size();++i){int sc; if(LMatch(g_lapps[i].name,g_lapps[i].nameLower,needle,sc))hits.push_back({i,sc});}
    std::sort(hits.begin(),hits.end(),[](const Hit&a,const Hit&b){ if(a.sc!=b.sc)return a.sc>b.sc; return g_lapps[a.idx].nameLower<g_lapps[b.idx].nameLower; });
    g_lfilt.clear(); for(auto&h:hits)g_lfilt.push_back(h.idx);
    // hidden apps never show; favourites lead (keeping the match order inside each group)
    { auto has=[](const std::vector<std::string>& v,const std::wstring& p){ std::string u=W2U8(p); for(auto& x:v) if(_stricmp(x.c_str(),u.c_str())==0) return true; return false; };
      if(!g_launHidden.empty()) g_lfilt.erase(std::remove_if(g_lfilt.begin(),g_lfilt.end(),[&](int i){ return has(g_launHidden,g_lapps[i].path); }),g_lfilt.end());
      if(!g_launFavs.empty()) std::stable_partition(g_lfilt.begin(),g_lfilt.end(),[&](int i){ return has(g_launFavs,g_lapps[i].path); }); }

    // Open windows that match, listed ABOVE the apps: if the thing is already running, switching to
    // it is almost always what was meant. Only with a query - an empty launcher stays a clean app
    // list rather than turning into a second Alt+Tab.
    g_winHits.clear();
    if(g_launchWindows && !needle.empty()){
        for(const DockApp& grp : g_dockApps){
            std::wstring e=grp.key; size_t sl=e.find_last_of(L"\\/");
            if(sl!=std::wstring::npos) e=e.substr(sl+1);
            for(auto&ch:e) ch=(wchar_t)towlower(ch);
            for(HWND h : grp.wins){
                if(!h || !IsWindow(h)) continue;
                wchar_t tb[256]={0}; GetWindowTextW(h,tb,255);
                std::wstring t=tb; for(auto&ch:t) ch=(wchar_t)towlower(ch);
                if(t.find(needle)==std::wstring::npos && e.find(needle)==std::wstring::npos) continue;
                LWin w; w.h=h; w.title=W2U8(tb); w.exe=W2U8(e); w.icon=grp.icon;
                // The taskbar fetches its icons per GROUP and some groups never get one, so fall
                // back to the exe's own icon - cached, because this runs on every keystroke.
                if(!w.icon && !grp.key.empty()){
                    static std::map<std::wstring,ID3D11ShaderResourceView*> s_ic;
                    auto it=s_ic.find(grp.key);
                    if(it!=s_ic.end()) w.icon=it->second;
                    else { ID3D11ShaderResourceView* t2=LFileIcon(grp.key); s_ic[grp.key]=t2; w.icon=t2; }
                }
                g_winHits.push_back(std::move(w));
                if(g_winHits.size()>=12) break;      // a launcher, not a window manager
            }
            if(g_winHits.size()>=12) break;
        }
    }
    g_lsel=0; }

// Applying a scheme by hand outranks the auto light/dark follower - without clearing g_themeMode,
// ApplyThemeMode would swap straight back to the sibling of whatever mode was set.
static void ApplySchemeChoice(int idx){
    idx=std::clamp(idx,0,NSCHEMES-1);
    g_themeMode=0;
    ApplyScheme(idx);
    SaveConfig(); g_deskDirty=true;
}
static const ULONG_PTR AETHER_IPC=0xAE01;   // WM_COPYDATA tag for `Aether.exe -s <cmd>`
static void RunCommand(int id);   // fwd
// Named entry point: the launcher indexes LCMDS, IPC arrives as a string. Both end up here.
static void UiDump();   // fwd - diagnostics, defined after the main loop's state
// ---- off-screen snapshots (tests / bug reports): render a panel into its hidden window for a moment, save a PNG ----
static int g_ghostSet=0;                        // frames left to render Settings without showing it
static IDXGISwapChain1* g_snapSc=nullptr;       // RenderL saves this swap chain's frame before presenting
static std::wstring g_snapPath;
static std::atomic<bool> g_videoDumpReq{false};   // -s video_dump: the next decoded video frame -> video_frame.png
static int g_ipcTour=-2, g_ipcSys=-1; static bool g_ipcTourTry=false;           // tour_step= / sysinfo= requests, applied inside DrawSettings
static void OpenSettingsPage(const std::string& name);   // fwd - `-s settings=Shortcuts`
static void ApplySchemeChoice(int idx);   // fwd
static ULONGLONG g_uiDumpBarFrames=0;
static void StartSnip(); // fwd
static void RunShellCommand(const std::string& n){
    if(n=="uidump"){ UiDump(); return; }
    if(n=="video_dump"){ g_videoDumpReq=true; return; }
    if(n=="revive_frozen"){ std::thread([]{ std::vector<HWND> hs;
            EnumWindows([](HWND h,LPARAM lp)->BOOL{ wchar_t c[64]={0}; GetClassNameW(h,c,64);
                if(wcsncmp(c,L"Chrome_WidgetWin",16)==0 && IsWindowVisible(h) && !IsIconic(h)) ((std::vector<HWND>*)lp)->push_back(h); return TRUE; },(LPARAM)&hs);
            int fixed=0; for(HWND h:hs) if(FrozenLooksFlat(h) && ReviveIfFrozen(h,true)) fixed++;
            if(!fixed) ShowAetherMessage("No frozen windows","Every Chromium / Electron window on screen is drawing normally.",2,"check_circle",4000); }).detach(); return; }
    if(n.rfind("slide_trace=",0)==0){ WsTraceEnable(n.substr(12)=="1"); return; }
    if(n.rfind("slide_ms=",0)==0){ g_wsSlideMs=std::clamp(atoi(n.c_str()+9),60,5000); return; }
    if(n=="snap_settings"){ g_snapPath=U82W(ExeDir()+"snap_settings.png"); g_snapSc=g_setSc; return; }   // the open Settings window's next frame
    // ghost_settings=<page>: draw Settings off-screen for ~1.5 s (never shown, never focused) and save snap_settings.png
    if(n.rfind("ghost_settings=",0)==0){ std::string pg=n.substr(15);
        bool was=g_setShow; OpenSettingsPage(pg); g_setShow=was;
        g_snapPath=U82W(ExeDir()+"snap_settings.png"); g_ghostSet=90; return; }
    if(n.rfind("tour_step=",0)==0){ g_ipcTour=atoi(n.c_str()+10); return; }
    if(n=="tour_try"){ g_ipcTourTry=true; return; }
    if(n=="wifi_sidebar"){ g_sideWifiOn=true; g_sideMixerOn=false; g_sideForceUntil=GetTickCount64()+8000; return; }
    if(n=="tour_off"){ g_ipcTour=-1; return; }
    if(n.rfind("sysinfo=",0)==0){ g_ipcSys=atoi(n.c_str()+8); return; }
    if(n=="session"){ g_sessShow=!g_sessShow; return; }                        // power menu
    if(n=="quicksettings"){ g_sideForceUntil=GetTickCount64()+6000; return; }  // quick settings panel
    if(n=="nowplaying"){ g_medUntil=GetTickCount64()+9000; if(g_medWake) PostMessageW(g_medWake,WM_NULL,0,0); return; }
    if(n=="notifications"){ g_notifForceUntil=GetTickCount64()+8000; return; }       // notification panel
    if(n=="osd"){ g_osdMode=0; g_osdVol=GetVolume(); g_osdShowUntil=GetTickCount64()+2200; return; }
    if(n.rfind("settings=",0)==0){ OpenSettingsPage(n.substr(9)); return; }
    if(n=="settings_close"){ g_setShow=false; return; }
    if(n.rfind("widget_add=",0)==0 && g_tab>=0 && g_tab<(int)g_tabs.size()){   // place a custom widget on the current tab
        std::string f=n.substr(11); CwDef* d=CwGet(f); Widget w; w.kind=WK_CUSTOM; w.arg=f;
        w.w=d? std::clamp(d->w,0.05f,1.0f) : 0.3f; w.h=d? std::clamp(d->h,0.05f,1.0f) : 0.4f;
        float x=0; for(auto& o:g_tabs[g_tab].widgets) x=std::max(x,o.x+o.w+0.01f);
        w.x=std::clamp(x,0.0f,1.0f-w.w); w.y=0; g_tabs[g_tab].widgets.push_back(w); SaveConfig(); return; }
    if(n=="dashboard_new_tab"){ DashTab nt; nt.name="Custom"; nt.icon=0; nt.iconName="widgets"; nt.builtin=false;
        g_tabs.push_back(nt); g_tab=(int)g_tabs.size()-1; g_drawerForceUntil=GetTickCount64()+6000; SaveConfig(); return; }
    if(n=="dashboard_terminal_tab"){ bool have=false; for(auto& t:g_tabs) for(auto& w:t.widgets) if(w.kind==WK_TERMINAL) have=true;
        if(!have){ DashTab nt; nt.name="Terminal"; nt.icon=0; nt.iconName="terminal"; nt.builtin=false; Widget w; w.kind=WK_TERMINAL; w.x=0; w.y=0; w.w=1; w.h=1; nt.widgets.push_back(w); g_tabs.push_back(nt); SaveConfig(); }
        for(int i=0;i<(int)g_tabs.size();i++) if(g_tabs[i].name=="Terminal") g_tab=i; g_drawerForceUntil=GetTickCount64()+6000; return; }
    if(n=="dashboard_mixer_tab"){ bool have=false; for(auto& t:g_tabs) for(auto& w:t.widgets) if(w.kind==WK_MIXER) have=true;
        if(!have){ DashTab nt; nt.name="Mixer"; nt.icon=0; nt.iconName="tune"; nt.builtin=false; Widget w; w.kind=WK_MIXER; w.x=0; w.y=0; w.w=1; w.h=1; nt.widgets.push_back(w); g_tabs.push_back(nt); SaveConfig(); }
        for(int i=0;i<(int)g_tabs.size();i++) if(g_tabs[i].name=="Mixer") g_tab=i; g_drawerForceUntil=GetTickCount64()+6000; return; }
    // -s motion_diag : what motion graph is actually in force right now. Settings are read and
    // written through one table, so this also answers "did my edit to motion.toml take?" - which
    // is otherwise invisible, because a curve and a duration only show up as a feeling.
    if(n=="motion_diag"){
        AetherLog("motion: speed_up=%s floor=%.2f speed=%.2f global=%s ms=%d overshoot=%s",
                  Cael::g_speedUp?"on":"off", Cael::g_speedUpFloor, Cael::g_animSpeed,
                  MOTION_NAMES[std::clamp(g_motionStyle,0,MSTY_N-1)], g_motionMs,
                  g_motionOvershoot?"on":"off");
        for(int k=0;k<HK_COUNT;k++){
            if(HK_PANEL[k]<0) continue;
            MotionSpec ms=MotionStyleSpec(std::clamp(g_hkStyle[k]>0?g_hkStyle[k]-1:g_motionStyle,0,MSTY_N-1));
            AetherLog("motion:   key %-13s -> panel %-20s graph=%-16s %4d ms%s",
                      g_hk[k].id, MP_PRETTY[HK_PANEL[k]],
                      g_hkStyle[k]>0? MOTION_PANEL_NAMES[g_hkStyle[k]] : "(follows panel)",
                      g_hkMs[k]>0? g_hkMs[k] : ms.ms,
                      (g_mgKey==k && GetTickCount64()-g_mgKeyAt<2000)? "  <- in force" : "");
        }
        for(int q=0;q<MP_COUNT;q++)
            AetherLog("motion:   panel %-20s graph=%-16s %4d ms",
                      MP_PRETTY[q],
                      g_mpStyle[q]>0? MOTION_PANEL_NAMES[g_mpStyle[q]] : "(follows global)",
                      g_mpMs[q]>0? g_mpMs[q] : MotionResolve(q,true).ms);
        return;
    }
    if(n=="record_toggle"){ V2RecToggle(); return; }
    if(n=="mixer_sidebar"){ g_sideMixerOn=true; g_sideForceUntil=GetTickCount64()+6000; return; }
    if(n.rfind("mixer_view=",0)==0){ g_amView=std::clamp(atoi(n.c_str()+11),0,3); return; }
    if(n=="dashboard_hold"){ g_drawerForceUntil=GetTickCount64()+8000; return; }
    if(n=="baritems"){ BarCustomLoad(); return; }      // re-read configaritems.toml without a restart
    // -s audio_diag : is the visualiser reading REAL audio, or the fallback animation?
    // The fallback reacts to play/pause exactly like the real thing does, so watching the bars can
    // never tell them apart - only g_audioOk can.
    if(n=="audio_diag"){
        float bands[NBANDS];
        { std::lock_guard<std::mutex> lk(g_bandMtx); memcpy(bands,g_bandsRaw,sizeof(bands)); }
        std::string bs; char b[24];
        for(int i=0;i<NBANDS;i++){ snprintf(b,sizeof(b),"%.2f ",bands[i]); bs+=b; }
        std::string dn; { std::lock_guard<std::mutex> lk(g_audioDevMtx); dn=g_audioDevName; }
        AetherLog("audio: loopback=%s dev=\"%s\" %dHz %dch level=%.3f bands=[ %s]",
                  g_audioOk.load()?"OK":"FAILED", dn.c_str(), g_audioRate, g_audioCh,
                  g_audioLevel.load(), bs.c_str());
        return;
    }
    if(n.rfind("bar_preset=",0)==0){ ApplyBarPresetByKey(n.substr(11)); return; }   // -s bar_preset=gnome
    if(n=="overview"){ if(g_ovWant) OvClose(); else OvOpen(); return; }
    if(n=="overview_style=cube"||n=="overview_style=plane"){ g_ovStyle=(n=="overview_style=plane"); SaveConfig(); return; }
    if(n=="wsslide=on"||n=="wsslide=off"){ g_wsSlide.store(n=="wsslide=on"); return; }   // diagnostics: toggle the workspace slide without touching config
    if(n.rfind("dashboard_edit=",0)==0){ std::string t=n.substr(15); for(int i=0;i<(int)g_tabs.size();i++) if(_stricmp(g_tabs[i].name.c_str(),t.c_str())==0) g_tab=i; g_editTab=(t!="off"); g_editSel=-1; g_drawerForceUntil=GetTickCount64()+8000; return; }
    // profile: -s status=<text> | status_clear | status_icon=<symbol> | presence=online|idle|dnd|invisible|none | profile_edit
    if(n.rfind("status=",0)==0){ ProfileSetStatus(n.substr(7),"",g_statusClearMin); return; }
    if(n=="status_clear"){ ProfileSetStatus("","",0); return; }
    if(n.rfind("status_icon=",0)==0){ g_statusIcon=n.substr(12); SaveConfig(); return; }
    if(n.rfind("presence=",0)==0){ std::string p=n.substr(9); for(int i=0;i<5;i++) if(p==PRESENCE_NAMES[i]) ProfileSetPresence(i); return; }
    if(n=="profile_edit"){ ProfileEditorOpen(0); g_drawerForceUntil=GetTickCount64()+8000; return; }
    // -s set=dashboard.home_layout=caelestia : change any registry setting live (scripts, testing, keybinds)
    // open anything through the safe launcher (a missing target gives the popup instead of a stuck shell)
    if(n.rfind("open=",0)==0){ AetherOpen(U82W(n.substr(5))); return; }
    // put text in the launcher's search field (tests / scripting)
    if(n.rfind("launcher_type=",0)==0){ g_launShow=true; g_lmode=0; snprintf(g_lsearch,sizeof(g_lsearch),"%s",n.substr(14).c_str()); LRebuild(); return; }
    // keyword_add=<keyword>|<hotkey or empty>|<path>
    if(n.rfind("keyword_add=",0)==0){ std::string v=n.substr(12); if(std::count(v.begin(),v.end(),'|')>=2){ g_launAliases.push_back(v); SaveConfig(); LfxRegisterAliasHotkeys(); } return; }
    if(n=="keywords_clear"){ g_launAliases.clear(); SaveConfig(); LfxRegisterAliasHotkeys(); return; }
    if(n.rfind("fav_add=",0)==0){ std::string v=n.substr(8); bool have=false; for(auto& x:g_launFavs) if(_stricmp(x.c_str(),v.c_str())==0) have=true; if(!have){ g_launFavs.push_back(v); SaveConfig(); LRebuild(); } return; }
    // -s save_test: a fake one-second slider drag through the real SaveConfig, then two separate clicks.
    // Proves the drag is throttled, the release still saves, and separate clicks are never held back.
    if(n=="save_test"){
        const int w0=g_cfgWrites; const double t0=stall::Now();
        g_cfgFakeHeld=1;
        for(int f=0;f<60;f++){ SaveConfig(); SaveConfigFlush(); Sleep(16); }   // 60 frames of dragging
        const int duringDrag=g_cfgWrites-w0; const bool pendingAtRelease=g_cfgPending;
        g_cfgFakeHeld=0; SaveConfigFlush();                                     // button up
        const int afterRelease=g_cfgWrites-w0; const bool pendingAfter=g_cfgPending;
        const int c0=g_cfgWrites;
        g_cfgFakeHeld=1; SaveConfig(); g_cfgFakeHeld=0; SaveConfigFlush();      // click 1
        g_cfgFakeHeld=1; SaveConfig(); g_cfgFakeHeld=0; SaveConfigFlush();      // click 2, straight after
        const int clicks=g_cfgWrites-c0;
        g_cfgFakeHeld=-1;
        const bool pass = duringDrag>=3 && duringDrag<=6 && afterRelease==duringDrag+(pendingAtRelease?1:0) && !pendingAfter && clicks==2;
        AetherLog("save_test: %s - 60-frame drag wrote %d times (was 60), release wrote the last one (%d total, pending after=%d), two quick clicks wrote %d (want 2), %.0f ms",
                  pass?"PASS":"FAIL",duringDrag,afterRelease,(int)pendingAfter,clicks,stall::Now()-t0);
        return; }
    // -s blur_dump: capture this monitor through the frost path and save blur_src.png / blur_out.png
    if(n=="blur_dump"){ g_blurDump=true; if(auto t=CaptureRegionBlur(g_mx,g_my,g_mw,g_mh)) t->Release(); return; }
    // Strive previews (they play whether or not the option is on): -s strive_intro | strive_banner=<n>
    // -s bar_logo=<key>: default | cachyos | arch | ... | custom (with bar.logo_image set) - for scripts and themes
    if(n.rfind("bar_logo=",0)==0){ std::string k=n.substr(9);
        for(int i=0;i<NBARLOGOS;i++) if(k==BAR_LOGOS[i].key){ g_barLogo=k; SaveConfig(); break; }
        return; }
    // -s bar_hitdump: what the bar window thinks is clickable (its WM_NCHITTEST rects), for click-through bugs
    if(n=="bar_hitdump"){ RECT wr{}; GetWindowRect(g_barHwnd,&wr);
        AetherLog("bar_hitdump: scale=%.3f window=%ld,%ld..%ld,%ld monitors=%d bars=%d",g_uiScale,wr.left,wr.top,wr.right,wr.bottom,(int)g_mons.size(),(int)g_bars.size());
        for(size_t i=0;i<g_bars.size();i++){ const BarState& b=g_bars[i];
            AetherLog("bar_hitdump:   bar %d reveal=%.2f shown=%d rect(logical)=%ld,%ld..%ld,%ld  on=%d",(int)i,b.reveal,(int)b.shown,
                      b.rect.left,b.rect.top,b.rect.right,b.rect.bottom,(int)(i<g_mons.size()? BarOnMon((int)i) : 0)); }
        return; }
    if(n=="strive_intro"){ StvIntroStart(); return; }
    if(n=="strive_intro_solid"){ StvSetSolid(); StvIntroStart(); return; }
    if(n.rfind("strive_banner_solid=",0)==0){ StvSetSolid(); StvBannerFire(atoi(n.substr(20).c_str()),""); return; }
    if(n.rfind("strive_banner=",0)==0){ StvBannerFire(atoi(n.substr(14).c_str()),""); return; }
    if(n=="hang_test"){ Sleep(6500); return; }   // freezes the render loop on purpose: proves the hang watchdog writes its dump + log
    if(n=="alert_test"){ ShowAetherMessage("Aether popups work","This is what an error or notice from the shell looks like.",2,"info",6000); return; }
    if(n.rfind("set=",0)==0){ std::string kv=n.substr(4); size_t eq=kv.find('=');
        if(eq!=std::string::npos){ std::string k=kv.substr(0,eq), v=kv.substr(eq+1);
            for(const Setting& st:SETTINGS){ if(k!=st.path) continue;
                switch(st.kind){
                    case SK_BOOL:  *(bool*)st.ptr = (v=="true"||v=="1"||v=="on"||v=="yes"); break;
                    case SK_INT:   *(int*)st.ptr  = (int)std::clamp(atof(v.c_str()),st.lo,st.hi); break;
                    case SK_FLOAT: *(float*)st.ptr= (float)std::clamp(atof(v.c_str()),st.lo,st.hi); break;
                    case SK_STR:   *(std::string*)st.ptr = v; break;
                    case SK_ENUM:  { for(int i=0;i<st.nnames;i++) if(_stricmp(v.c_str(),st.names[i])==0) *(int*)st.ptr=i; } break;
                    default: break; }
                SaveConfig(); g_deskDirty=true; break; } }
        return; }
    // -s bar_item=workspaces=on : show / hide one bar item (the Settings > Taskbar list)
    if(n.rfind("bar_item=",0)==0){ std::string kv=n.substr(9); size_t eq=kv.find('=');
        std::string id=kv.substr(0,eq); bool on = eq==std::string::npos || kv.substr(eq+1)!="off";
        for(auto& c:g_barItems) if(id==BAR_ITEMS[c.id].key){ c.on=on; SaveConfig(); }
        return; }
    if(n=="widget_pop" && g_tab>=0 && g_tab<(int)g_tabs.size() && !g_tabs[g_tab].widgets.empty()){ g_tabs[g_tab].widgets.pop_back(); SaveConfig(); return; }
    // -s widget_kind=user : place a built-in widget on the current tab (scripts, testing)
    if(n.rfind("widget_kind=",0)==0 && g_tab>=0 && g_tab<(int)g_tabs.size()){ int k=WKindFromId(n.substr(12));
        if(k>=0){ Widget w; w.kind=k; w.w=WREG[k].dw; w.h=WREG[k].dh;
            float x=0; for(auto& o:g_tabs[g_tab].widgets) x=std::max(x,o.x+o.w+0.01f);
            w.x=std::clamp(x,0.0f,1.0f-w.w); w.y=0; g_tabs[g_tab].widgets.push_back(w); SaveConfig(); } return; }
    if(n=="media_view=lyrics"||n=="media_view=player"){ g_mediaView = (n=="media_view=lyrics")? 1 : 0; return; }
    // -s wallapply=<part of a name>: apply the first picker entry whose name contains it, on the screen
    // under the cursor - the same path a click in the picker takes (scripts, testing)
    if(n.rfind("wallapply=",0)==0){ WallScan(); std::string q=n.substr(10); std::wstring pick;
        for(int wait=0; wait<100 && !g_wallReady.load(); wait++) Sleep(50);
        { std::lock_guard<std::mutex> lk(g_wallMtx);
          for(auto& w:g_walls) if(StrStrIA(w.name.c_str(),q.c_str())){ pick=w.path; break; } }
        if(!pick.empty()){ POINT cp; GetCursorPos(&cp); SetWallpaperFile(pick,V((cp.x-g_vs.left)/g_uiScale,(cp.y-g_vs.top)/g_uiScale)); }
        return; }
    if(n.rfind("scheme=",0)==0){ LoadUserSchemes(); std::string id=n.substr(7);   // -s scheme=example-rose
        for(int i=0;i<NSCHEMES;i++) if(id==g_schemes[i].id){ ApplySchemeChoice(i); break; }
        return; }            // diagnostics: popout / hover state -> uidump.txt
    if(n=="snip"){ StartSnip(); return; }           // `Aether.exe -s snip` - region screenshot, like `caelestia screenshot`
    if(n=="wallnext"||n=="wallprev"){ WallScan(); SlideshowAdvance(n=="wallnext"?+1:-1); return; }   // with the main transition
    if(n.rfind("transition=",0)==0){ g_transCfg=ParseTrans(n.substr(11)); SaveConfig(); return; }     // e.g. -s transition=fade
    // while locked only the things a Windows lock screen also allows (plus diagnostics)
    if(ShellLocked() && n!="texdump" && n!="lock" && n!="sleep" && n!="restart" && n!="shutdown") return;
    if(n=="texdump"){ TexDump(); return; }
    if(n=="wallpaper=live"||n=="wallpaper=images"){
        int want=(n=="wallpaper=live")? WSRC_LIVE : WSRC_IMAGES;
        if(want!=g_wallSource){ g_wallSource=want; SaveConfig(); WallRescan(); }
        RunShellCommand("wallpaper"); return; }          // diagnostics: live textures by call site -> texdump.txt
    if(n=="launcher"){ LaunToggle(); return; }
    if(n=="dashboard"){ g_tab=0; g_drawerForceUntil=GetTickCount64()+4000; return; }
    // `-s dashboard=Media` opens the drawer on a named tab and holds it open for a while (screenshots, scripts)
    if(n.rfind("dashboard=",0)==0){ std::string want=n.substr(10);
        for(int i=0;i<(int)g_tabs.size();i++) if(_stricmp(g_tabs[i].name.c_str(),want.c_str())==0){ g_tab=i; break; }
        g_drawerForceUntil=GetTickCount64()+8000; return; }
    // these two keep the launcher OPEN as their own mode, so it has to be showing first - invoked
    // from the command palette it already is, from IPC it is not
    if(n=="wallpaper"||n=="scheme"||n=="clip") g_launShow=true;
    for(int i=0;i<NLCMDS;i++) if(n==LCMDS[i].name){ RunCommand(i); return; }
}
static void RunCommand(int id){
    std::string n=LCMDS[id].name;
    // like the wallpaper carousel, clipboard history keeps the launcher open as its own mode
    if(n=="clip"){ g_lmode=4; g_lsearch[0]=0; ClipRebuild(""); g_lsel=0; return; }
    if(n=="calculator"){ g_lmode=0; g_lsearch[0]=0; LRebuild(); return; }         // stays open: the maths row appears as you type
    if(n=="unhide"){ g_launHidden.clear(); SaveConfig(); g_lmode=0; g_lsearch[0]=0; LRebuild(); return; }
    if(n=="wallpaper"){ WallScan(); g_lmode=2; g_lsearch[0]=0; return; }   // stays open as a carousel
    // like the wallpaper carousel, the scheme picker keeps the launcher open as its own mode
    if(n=="scheme"){ LoadUserSchemes(); ApplyScheme(g_scheme); g_lmode=3; g_lsearch[0]=0; g_schemeSel=std::clamp(g_scheme,0,NSCHEMES-1); return; }
    g_launShow=false;
    if(n=="files") FmStart();
    else if(n=="settings") g_setShow=true;
    else if(n=="utilities"){ g_sideView=3; g_sideForceUntil=GetTickCount64()+5000; }
    else if(n=="theme"){ g_themeMode=g_darkUI?1:2; ApplyThemeMode(); SaveConfig(); g_deskDirty=true; }
    else if(n=="dnd"){ g_dnd=!g_dnd; SaveConfig(); }
    else if(n=="light"){ if(g_darkUI){ g_themeMode=1; ApplyThemeMode(); SaveConfig(); g_deskDirty=true; } }
    else if(n=="dark"){ if(!g_darkUI){ g_themeMode=2; ApplyThemeMode(); SaveConfig(); g_deskDirty=true; } }
    else if(n=="variant"){ ApplySchemeChoice((g_scheme+1)%NSCHEMES); }
    else if(n=="random"){ WallScan(); for(int w=0; w<100 && !g_wallReady.load(); w++) Sleep(50);
        std::wstring pick; { std::lock_guard<std::mutex> lk(g_wallMtx); if(!g_walls.empty()) pick=g_walls[(size_t)((GetTickCount64()*2654435761ULL)>>7)%g_walls.size()].path; }
        if(!pick.empty()) SetWallpaperFile(pick,DeskPt(0.5f,0.5f)); }
    else if(n=="reload"){ wchar_t ex[MAX_PATH]; GetModuleFileNameW(nullptr,ex,MAX_PATH);
        (void)ex; RestartShell(); }
    else if(n=="lock") DoLock();
    else if(n=="sleep") DoSleep();
    else if(n=="restart") AetherShellExec(nullptr,L"open",L"shutdown.exe",L"/r /t 0",nullptr,SW_HIDE);
    else if(n=="shutdown") AetherShellExec(nullptr,L"open",L"shutdown.exe",L"/s /t 0",nullptr,SW_HIDE);
    else if(n=="quit") PostMessageW(g_hwnd,WM_CLOSE,0,0);
}
// ---- live picker: preview while browsing ---------------------------------------------------------------
// As you scroll, the selected wallpaper plays on the screen behind the picker (debounced, so fast
// scrolling does not make Wallpaper Engine load every wallpaper passed). What was on that screen when the
// picker opened is remembered and put back unless Enter commits the choice.
struct LiveBrowse { bool active=false, committed=false; int mon=0; std::wstring orig; int lastSel=-1;
                    ULONGLONG changedAt=0; int previewed=-1; };
static LiveBrowse g_lb;
// ---- animated card previews -----------------------------------------------------------------------------
// 232 of the 376 workshop previews on this machine are animated GIFs. The cards near the centre play them:
// frames are decoded, downscaled to card size, on a worker, and the render thread only swaps textures.
// A handful of wallpapers are kept at once; the furthest from the selection is dropped first.
struct AnimPrev { std::wstring path; std::vector<ID3D11ShaderResourceView*> fr; std::vector<int> delay; int w=0,h=0;
                  bool ready=false, busy=false; ULONGLONG used=0; };
static std::mutex g_apMtx;
static std::vector<std::shared_ptr<AnimPrev>> g_ap;
static void AnimPrevDecode(std::shared_ptr<AnimPrev> ap){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IWICImagingFactory* fac=nullptr;
    std::vector<ID3D11ShaderResourceView*> frames; std::vector<int> delays; int ow=0,oh=0;
    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&fac)))){
        IWICBitmapDecoder* dec=nullptr;
        if(SUCCEEDED(fac->CreateDecoderFromFilename(ap->path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec))){
            UINT n=0; dec->GetFrameCount(&n);
            // GIF frames are often partial rectangles drawn over the previous one: composite onto a canvas
            UINT cw=0,ch=0; std::vector<uint8_t> canvas;
            for(UINT i=0;i<n && i<180;i++){
                IWICBitmapFrameDecode* fr=nullptr; if(FAILED(dec->GetFrame(i,&fr))) continue;
                UINT fw=0,fh=0; fr->GetSize(&fw,&fh);
                int left=0,top=0, delay=80;
                IWICMetadataQueryReader* mq=nullptr;
                if(SUCCEEDED(fr->GetMetadataQueryReader(&mq))){
                    PROPVARIANT v; PropVariantInit(&v);
                    if(SUCCEEDED(mq->GetMetadataByName(L"/grctlext/Delay",&v)) && v.vt==VT_UI2){ delay=v.uiVal*10; if(delay<20) delay=80; } PropVariantClear(&v);
                    if(SUCCEEDED(mq->GetMetadataByName(L"/imgdesc/Left",&v)) && v.vt==VT_UI2) left=v.uiVal; PropVariantClear(&v);
                    if(SUCCEEDED(mq->GetMetadataByName(L"/imgdesc/Top",&v))  && v.vt==VT_UI2) top=v.uiVal;  PropVariantClear(&v);
                    mq->Release(); }
                if(i==0){ IWICMetadataQueryReader* dq=nullptr; cw=fw; ch=fh;
                    if(SUCCEEDED(dec->GetMetadataQueryReader(&dq))){ PROPVARIANT v; PropVariantInit(&v);
                        if(SUCCEEDED(dq->GetMetadataByName(L"/logscrdesc/Width",&v)) && v.vt==VT_UI2) cw=std::max<UINT>(cw,v.uiVal); PropVariantClear(&v);
                        if(SUCCEEDED(dq->GetMetadataByName(L"/logscrdesc/Height",&v)) && v.vt==VT_UI2) ch=std::max<UINT>(ch,v.uiVal); PropVariantClear(&v);
                        dq->Release(); }
                    canvas.assign((size_t)cw*ch*4,0); }
                IWICFormatConverter* conv=nullptr; fac->CreateFormatConverter(&conv);
                if(conv && SUCCEEDED(conv->Initialize(fr,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeMedianCut))){
                    std::vector<uint8_t> buf((size_t)fw*fh*4); conv->CopyPixels(nullptr,fw*4,(UINT)buf.size(),buf.data());
                    for(UINT y=0;y<fh;y++) for(UINT x=0;x<fw;x++){
                        UINT cx=left+x, cy=top+y; if(cx>=cw||cy>=ch) continue;
                        uint8_t* sp=&buf[((size_t)y*fw+x)*4]; if(sp[3]<8) continue;
                        memcpy(&canvas[((size_t)cy*cw+cx)*4],sp,4); }
                    // downscale the canvas to at most 420px wide (nearest is fine at card size and fast)
                    int tw=(int)std::min<UINT>(cw,420), th=(int)((double)ch*tw/std::max<UINT>(cw,1));
                    std::vector<uint8_t> small((size_t)tw*th*4);
                    for(int y=0;y<th;y++){ UINT sy=(UINT)((double)y*ch/th);
                        for(int x=0;x<tw;x++){ UINT sx=(UINT)((double)x*cw/tw);
                            memcpy(&small[((size_t)y*tw+x)*4],&canvas[((size_t)sy*cw+sx)*4],4); } }
                    if(ID3D11ShaderResourceView* t=MakeTextureBGRA(small.data(),tw,th)){ frames.push_back(t); delays.push_back(delay); ow=tw; oh=th; }
                }
                if(conv) conv->Release(); fr->Release();
                if(!g_running) break;
            }
            dec->Release();
        }
        fac->Release();
    }
    std::lock_guard<std::mutex> lk(g_apMtx);
    ap->fr=std::move(frames); ap->delay=std::move(delays); ap->w=ow; ap->h=oh; ap->ready=true; ap->busy=false;
    CoUninitialize();
}
// The texture to draw for a card right now: an animation frame if its preview is a GIF that has been
// decoded, otherwise nullptr (the caller keeps using the still thumbnail).
static ID3D11ShaderResourceView* AnimPrevFrame(const std::wstring& path,bool want,int& w,int& h){
    size_t d=path.find_last_of(L'.'); if(d==std::wstring::npos || _wcsicmp(path.c_str()+d,L".gif")!=0) return nullptr;
    std::lock_guard<std::mutex> lk(g_apMtx);
    std::shared_ptr<AnimPrev> ap;
    for(auto& p:g_ap) if(p->path==path){ ap=p; break; }
    if(!ap){
        if(!want) return nullptr;
        if(g_ap.size()>=7){                                   // evict the least recently used idle one
            size_t victim=0; ULONGLONG oldest=~0ULL;
            for(size_t i=0;i<g_ap.size();i++) if(!g_ap[i]->busy && g_ap[i]->used<oldest){ oldest=g_ap[i]->used; victim=i; }
            if(!g_ap[victim]->busy){ for(auto t:g_ap[victim]->fr) if(t) t->Release(); g_ap.erase(g_ap.begin()+victim); }
        }
        ap=std::make_shared<AnimPrev>(); ap->path=path; ap->busy=true; g_ap.push_back(ap);
        std::thread(AnimPrevDecode,ap).detach();
        return nullptr;
    }
    ap->used=GetTickCount64();
    if(!ap->ready || ap->fr.empty()) return nullptr;
    long long total=0; for(int dl2:ap->delay) total+=dl2; if(total<=0) total=1;
    long long t=(long long)(GetTickCount64()%(ULONGLONG)total); size_t i=0;
    while(i+1<ap->delay.size() && t>=ap->delay[i]){ t-=ap->delay[i]; i++; }
    w=ap->w; h=ap->h; return ap->fr[i];
}
static std::wstring WeCurrentFileFor(int mon){
    std::wstring exe; { std::lock_guard<std::mutex> lk(g_weMtx); exe=g_weExe; }
    if(exe.empty()) exe=WeFindExe();                 // not scanned yet this run (image picker opened first)
    size_t sl=exe.find_last_of(L"\\/"); if(sl==std::wstring::npos) return L"";
    std::ifstream f(exe.substr(0,sl)+L"\\config.json",std::ios::binary); if(!f) return L"";
    std::stringstream ss; ss<<f.rdbuf(); std::string c=ss.str();
    char key[32]; snprintf(key,32,"\"Monitor%d\"",mon);
    // several user profiles can be in there; take the first that has a wallpaper on this monitor
    size_t p=0;
    while((p=c.find(key,p))!=std::string::npos){
        size_t fk=c.find("\"file\"",p), close=c.find('}',p);
        if(fk!=std::string::npos && fk<close){
            size_t a=c.find('"',fk+6); size_t b=(a==std::string::npos)? a : c.find('"',a+1);
            if(a!=std::string::npos && b!=std::string::npos){
                std::wstring w=U82W(c.substr(a+1,b-a-1)); for(auto& ch:w) if(ch==L'/') ch=L'\\';
                // WE records the content file (scene.pkg, a video); openWallpaper is given the project
                size_t sl2=w.find_last_of(L'\\');
                if(sl2!=std::wstring::npos){ std::wstring pj=w.substr(0,sl2)+L"\\project.json";
                    if(GetFileAttributesW(pj.c_str())!=INVALID_FILE_ATTRIBUTES) return pj; }
                return w; } }
        p+=strlen(key);
    }
    return L"";
}
static void LiveBrowseBegin(){
    if(g_lb.active) return;
    g_lb=LiveBrowse(); g_lb.active=true;
    // the monitor the PICKER is on - not the one under the cursor. Opened by keybind or command with the mouse on
    // another screen, the preview used to change that other screen, behind whatever was open there.
    g_lb.mon=std::clamp(g_actMon,0,15);
    g_lb.orig=WeCurrentFileFor(g_lb.mon);
    g_lb.lastSel=g_wallSel;      // opening the picker is not a scroll: do not replace the wallpaper until you move
}
static void LiveBrowseEnd(){
    if(!g_lb.active) return;
    if(!g_lb.committed && g_lb.previewed>=0){                // browsed but not chosen: put it back
        const std::wstring m=std::to_wstring(g_lb.mon);
        // through the same fade as any other live switch, so putting yours back does not pop
        int mi2=std::clamp(g_lb.mon,0,15); WeTrans& T=g_weTr[mi2]; WeTransEnd(T); g_monStill[mi2]=false;
        T.L=WipeLayer(); T.scrim=true; T.toImage=g_lb.orig.empty(); T.sawDrop=false; T.phase=1; T.fade=0.0f; T.t0=GetTickCount64();
        T.cmd = !g_lb.orig.empty()? (L"-control openWallpaper -file \""+g_lb.orig+L"\" -monitor "+m) : (L"-control closeWallpaper -monitor "+m);
        g_deskDirty=true;
    }
    g_lb.active=false;
}
static void LiveBrowsePreview(int sel,bool quick){
    if(sel<0 || sel>=(int)g_walls.size() || g_walls[sel].weProj.empty()) return;
    const RECT& mr=MonRect(g_lb.mon);
    ImVec2 from=V(((mr.left+mr.right)*0.5f-g_vs.left)/g_uiScale,((mr.top+mr.bottom)*0.5f-g_vs.top)/g_uiScale);
    WTrans keepT=g_transCfg; int keepMs=g_transMs; bool keepMouse=g_transPosMouse;
    if(quick){ g_transCfg=WTrans::Fade; g_transMs=260; }    // browsing: a quick cross-fade, not the full show
    g_transPosMouse=false;
    ApplyWeWallpaper(g_walls[sel].weProj,g_walls[sel].path,from);
    g_transCfg=keepT; g_transMs=keepMs; g_transPosMouse=keepMouse;
    g_lb.previewed=sel;
}
// ---- image picker: preview (and recolour) while scrolling ------------------------------------------------
static struct { bool active=false; std::string orig; int lastSel=-1; ULONGLONG chg=0; bool previewed=false; std::wstring liveOrig; int mon=0; bool wasLive=false; } g_ip;
static void ImageQuickApply(const std::wstring& path){
    WTrans keepT=g_transCfg; int keepMs=g_transMs;
    g_transCfg=WTrans::Fade; g_transMs=std::min(keepMs,320);
    SetWallpaperFile(path, DeskPt(0.5f,0.5f));
    g_transCfg=keepT; g_transMs=keepMs;
}
static void ImagePreviewTick(){
    bool on = g_wallPreviewImages && g_launShow && g_lmode==2 && g_wallSource==WSRC_IMAGES && g_wallReady.load();
    ULONGLONG now=GetTickCount64();
    if(on && !g_ip.active){ g_ip.active=true; g_ip.orig=g_lastWall; g_ip.lastSel=g_wallSel; g_ip.chg=0; g_ip.previewed=false;
        // a screen showing a LIVE wallpaper must get that live wallpaper back, not the last still image
        g_ip.mon=std::clamp(g_actMon,0,15); g_ip.wasLive=g_monLive[g_ip.mon] && !g_monStill[g_ip.mon];
        g_ip.liveOrig = g_ip.wasLive? WeCurrentFileFor(g_ip.mon) : std::wstring();
        return; }
    if(!on && g_ip.active){
        g_ip.active=false;
        // left the picker without pressing Enter: put back what was there
        if(g_ip.previewed && !g_wallJustApplied){
            if(g_ip.wasLive){
                // Wallpaper Engine never actually stopped on that screen: lifting the override brings it straight back
                WeTrans& T=g_weTr[g_ip.mon]; WeTransEnd(T); g_monStill[g_ip.mon]=false;
                T.L=WipeLayer(); T.scrim=true; T.toImage=false; T.sawDrop=false; T.phase=1; T.fade=0.0f; T.t0=GetTickCount64();
                T.cmd = g_ip.liveOrig.empty()? std::wstring() : L"-control openWallpaper -file \""+g_ip.liveOrig+L"\" -monitor "+std::to_wstring(g_ip.mon);
                g_deskDirty=true;
                if(!g_ip.orig.empty()){ g_lastWall=g_ip.orig; if(g_dynamicColor) ApplyDynamicAccent(); SaveConfig(); }   // colours back too
            } else if(!g_ip.orig.empty() && _stricmp(g_lastWall.c_str(),g_ip.orig.c_str())!=0)
                ImageQuickApply(U82W(g_ip.orig));
        }
        return; }
    if(!on) return;
    if(g_wallSel!=g_ip.lastSel){ g_ip.lastSel=g_wallSel; g_ip.chg=now; }
    if(g_ip.chg && now-g_ip.chg>=(ULONGLONG)g_wallPreviewDelay){
        g_ip.chg=0;
        if(g_wallSel>=0 && g_wallSel<(int)g_walls.size() && g_walls[g_wallSel].weProj.empty()){
            std::string p=W2U8(g_walls[g_wallSel].path);
            if(_stricmp(p.c_str(),g_lastWall.c_str())!=0){ ImageQuickApply(g_walls[g_wallSel].path); g_ip.previewed=true; } }
    }
}
#include "src/modules/launcher/LauncherFx.h"   // layouts, shapes, glow, 3D, typing fx, tray, keywords
static void LLaunch(){
    if(g_lmode==1){ if(g_lsel>=0&&g_lsel<(int)g_cfilt.size()) RunCommand(g_cfilt[g_lsel]); return; }
    if(g_lmode==2){ if(g_wallSel>=0&&g_wallSel<(int)g_walls.size()){
            if(g_lb.active && !g_walls[g_wallSel].weProj.empty()){
                g_lb.committed=true;
                if(g_lb.previewed!=g_wallSel) LiveBrowsePreview(g_wallSel,false);   // not previewed yet: full transition
                g_launShow=false; g_wallJustApplied=true; return; }
            g_launShow=false; g_wallJustApplied=true;      // uncover the transition immediately
            SetWallpaperFile(g_walls[g_wallSel].path, DeskPt(0.5f,0.5f)); } return; }
    if(g_lmode==3){ ApplySchemeChoice(g_schemeSel); g_launShow=false; return; }
    if(g_lmode==4){
        std::wstring pick;
        { std::lock_guard<std::mutex> lk(g_clipMtx);
          if(g_lsel>=0 && g_lsel<(int)g_clipFilt.size()){
              int i=g_clipFilt[g_lsel];
              if(i>=0 && i<(int)g_clips.size()) pick=g_clips[i].text; } }
        if(!pick.empty()){ ClipPut(pick); g_launShow=false; g_lmode=0;
            if(g_clipPaste) ClipSendPaste(); }
        return; }
    { int ki=LfxAliasMatch(g_lsearch); if(ki>=0){ auto kv2=LfxAliases(); AetherOpen(U82W(kv2[ki].path)); g_launShow=false; return; } }   // a keyword opens its app
    int calcRows=g_calcOk?1:0;
    if(g_calcOk && g_lsel==0){                                    // copy the maths result to the clipboard
        char buf[64]; double v=g_calcVal;
        if(fabs(v-llround(v))<1e-9 && fabs(v)<9e15) snprintf(buf,64,"%lld",(long long)llround(v));
        else snprintf(buf,64,"%.10g",v);
        std::wstring w=U82W(buf);
        if(OpenClipboard(g_hwnd)){ EmptyClipboard();
            HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,(w.size()+1)*sizeof(wchar_t));
            if(h){ memcpy(GlobalLock(h),w.c_str(),(w.size()+1)*sizeof(wchar_t)); GlobalUnlock(h);
                   SetClipboardData(CF_UNICODETEXT,h); } CloseClipboard(); }
        g_launShow=false; return; }
    // an open window was picked: switch to it rather than launching the app again
    int wi=g_lsel-calcRows;
    if(wi>=0 && wi<(int)g_winHits.size()){
        HWND h=g_winHits[wi].h;
        g_launShow=false;
        if(h && IsWindow(h)) ActivateWindow(h);
        return; }
    int ai=g_lsel-calcRows-(int)g_winHits.size();
    if(ai<0||ai>=(int)g_lfilt.size())return;
    std::wstring p=g_lapps[g_lfilt[ai]].path; g_launShow=false;
    // our own actions (not real files) use a cw:// sentinel path
    if(p==L"cw://mica"){    bool was=g_micaMode&&g_micaKind==CWSBT_MICA;    g_micaKind=CWSBT_MICA;    SetMicaMode(!was); SaveConfig(); return; }
    if(p==L"cw://acrylic"){ bool was=g_micaMode&&g_micaKind==CWSBT_ACRYLIC; g_micaKind=CWSBT_ACRYLIC; SetMicaMode(!was); SaveConfig(); return; }
    if(p==L"cw://taskmgr"){ AetherShellExec(nullptr,L"open",L"taskmgr.exe",nullptr,nullptr,SW_SHOWNORMAL); return; }
    if(p==L"cw://lock"){ DoLock(); return; }
    if(p==L"cw://lockcountdown"){ g_preLockForceEnd=GetTickCount64()+(ULONGLONG)g_preLockSecs*1000ULL; return; }
    if(p.rfind(L"cw://scheme/",0)==0){ std::string id=W2U8(p.substr(12)); int idx=SchemeIndex(id);
        g_customAccent=false; g_dynamicColor=false; g_scheme=idx; g_themeMode=0; ApplyScheme(idx); SaveConfig(); g_deskDirty=true; return; }
    AetherShellExec(nullptr,L"open",p.c_str(),nullptr,nullptr,SW_SHOWNORMAL); }

// text entry handled by hand (the panel is fully custom-drawn, so no ImGui window is involved)
static void LTextInput(ImGuiIO& io){
    size_t len=strlen(g_lsearch); bool changed=false;
    for(int i=0;i<io.InputQueueCharacters.Size;i++){ ImWchar c=io.InputQueueCharacters[i];
        if(c>=32&&c<127&&len<sizeof(g_lsearch)-1){ g_lsearch[len++]=(char)c; g_lsearch[len]=0; changed=true; } }
    if(ImGui::IsKeyPressed(ImGuiKey_Backspace)&&len>0){
        if(ImGui::GetIO().KeyCtrl){ while(len>0&&g_lsearch[len-1]==' ')len--; while(len>0&&g_lsearch[len-1]!=' ')len--; }
        else len--;
        g_lsearch[len]=0; changed=true; }
    // The wallpaper picker filters its own list from g_lsearch (see the carousel). Running the app search
    // here dropped it straight back to the app list on the first letter typed.
    if(changed){ if(g_lmode==4){ ClipRebuild(g_lsearch); g_lsel=0; } else if(g_lmode==2||g_lmode==3){} else LRebuild(); }
}

static void DrawLauncher(){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x,H=io.DisplaySize.y;
    float a=std::clamp(std::max(g_launAnim, FrameBornOn()? std::min(g_launReveal,1.0f) : 0.0f),0.0f,1.0f); if(a<0.004f)return;
    // The gesture that OPENED the launcher must not also activate a result. The panel only starts
    // accepting Enter/clicks once the mouse and Enter have been released and a beat has passed —
    // otherwise opening it could launch the first app in the list straight away.
    if(!io.MouseDown[0] && !ImGui::IsKeyDown(ImGuiKey_Enter) && !ImGui::IsKeyDown(ImGuiKey_KeypadEnter)
       && GetTickCount64()-g_launOpenAt>120) g_launArmed=true;
    const bool armed=g_launArmed;
    // Morph-bounce: overshoot on the way in so the panel springs past its resting size and settles,
    // while the whole thing rises from below. Closing stays a plain ease so it snaps away cleanly.
    // Opening springs past its resting size and settles (EaseOutBack). Closing used EaseOutCubic,
    // which DECELERATES - so it crawled out and read as "it just faded". A quadratic ease-IN makes
    // it leave the way it should: gently at first, then away.
    float e = g_launShow ? EaseOutBack(a) : (a*a);
    int al=(int)(std::clamp(EaseOutCubic(a),0.0f,1.0f)*255);
    int vlau=ImGui::GetBackgroundDrawList()->VtxBuffer.Size;   // everything below gets the morph
    bool click=io.MouseClicked[0] && armed;

    // a few px of slack: a stationary mouse still jitters by a pixel on some setups
    { ImVec2 cur=LaunCursorLogical();
      if(fabsf(cur.x-g_launMousePos.x)>3.0f || fabsf(cur.y-g_launMousePos.y)>3.0f){
          g_launMousePos=cur; g_launMouseLive=true; } }

    LTextInput(io);
    LfxIconPump();
    if(ImGui::IsKeyPressed(ImGuiKey_Escape)){
        if(g_lmode==2 && g_lsearch[0]){ g_lsearch[0]=0; }                      // picker: clear the search first
        // A sub-view steps back to the app list only if that is where you came from. Opened straight into it
        // by its own shortcut (Ctrl+Alt+V, Ctrl+Alt+W, `-s clip`) there is nothing to go back to, and the first
        // Escape used to drop you into the app list instead of closing.
        else if((g_lmode==2||g_lmode==3||g_lmode==4) && g_lmode!=g_lmodeAtOpen){ g_lmode=0; LRebuild(); } else g_launShow=false; }

    // ---------------- wallpaper carousel (">wallpaper") ----------------
    if(g_lmode==2){
        bool ready=g_wallReady.load(std::memory_order_acquire);
        // ---- search: typing filters by name (the launcher's text field already collects the keys) ----
        // `fl` is the filtered view; the carousel walks positions in it while g_wallSel stays an index into
        // g_walls, so applying, the slideshow and the keybinds keep working on the real list.
        static std::vector<int> fl; fl.clear();
        if(ready) for(int i=0;i<(int)g_walls.size();i++)
            if(!g_lsearch[0] || StrStrIA(g_walls[i].name.c_str(),g_lsearch)) fl.push_back(i);
        int n=(int)fl.size();
        int fpos=0; for(int i=0;i<n;i++) if(fl[i]==g_wallSel){ fpos=i; break; }
        if(n>0 && (fpos>=n || fl[fpos]!=g_wallSel)){ fpos=0; g_wallSel=fl[0]; }
        { static std::string lastQ; if(lastQ!=g_lsearch){ lastQ=g_lsearch; if(n>0){ fpos=0; g_wallSel=fl[0]; } g_wallSpring.snap(); } }
        auto at=[&](int p)->int{ return n>0? fl[((p%n)+n)%n] : 0; };
        // thumbnails load for the leading entries of the REAL list, so ask for enough to cover the cards shown
        { int need=0; for(int d=-4; d<=14 && n>0; d++) need=std::max(need,at(fpos+d)+1);
          WallThumbs(std::min((int)g_walls.size(),need)); }

        // ================= LIVE WALLPAPERS: the caelestia-aw picker ===========================================
        // Studied frame by frame from the caelestia-aw demo: a frosted panel rising from the bottom edge;
        // Static / Animated / Refresh pills; one flat row of 16:9 cards, the centre one ~25% larger on a lifted
        // tile with a bigger, brighter label; the row slides as you scroll, cards growing as they reach the
        // centre and entering small at the edges; a pill search bar along the bottom; and the wallpaper on
        // screen follows the selection.
        if(g_wallSource==WSRC_LIVE){
            if(g_launShow) LiveBrowseBegin();
            float pw=std::min(W-60.0f,1280.0f), ph=366.0f;
            float px=(W-pw)*0.5f, py=H-ph+(1.0f-e)*(ph+40.0f);               // rises out of the bottom edge
            float rndP=30.0f;
            const bool wBorn=FrameBornOn();
            if(wBorn){
                float bB=FrameInset(EDGE_BOTTOM);
                float rv=std::min(g_launReveal,1.3f);
                ph-=bB*0.5f; py=H-bB-ph*rv;
                FrameEdgePanel(dl,FrameMon(),V(0,0),V(W,H),EDGE_BOTTOM,px,px+pw,bB+ph*rv,bB,rndP,1.0f);
            } else {
            if(g_launBg && W>1 && H>1){
                ImVec2 uv0(px/W,std::clamp(py/H,0.0f,1.0f)), uv1((px+pw)/W,1.0f);
                dl->AddImageRounded((ImTextureID)g_launBg,V(px,py),V(px+pw,H),uv0,uv1,IM_COL32(255,255,255,al),rndP,ImDrawFlags_RoundCornersTop); }
            GlassPanel(dl,V(px,py),V(px+pw,H+rndP),rndP,ImDrawFlags_RoundCornersTop,nullptr,e,
                       (int)(std::clamp(g_wallOpacity,0.10f,1.0f)*215.0f));
            }

            // ---- pills ----
            { const char* lab[3]={"Static","Animated","Refresh"};
              float pwid[3]; float tot=0; for(int i=0;i<3;i++){ pwid[i]=TextW(g_fReg,17,lab[i])+52.0f; tot+=pwid[i]; }
              tot+=20.0f; float x=px+(pw-tot)*0.5f, y=py+20.0f, h=36.0f;
              for(int i=0;i<3;i++){
                  ImVec2 a0=V(x,y), b0=V(x+pwid[i],y+h);
                  bool hov=io.MousePos.x>a0.x&&io.MousePos.x<b0.x&&io.MousePos.y>a0.y&&io.MousePos.y<b0.y;
                  float ha=HoverAnim(9700+i,hov);
                  bool on=(i==1);
                  dl->AddRectFilled(a0,b0,on? AccA((int)(230*e)) : WithA(COL_INK,(int)((18+ha*22)*e)),h*0.5f);
                  ImU32 tc = on? (g_darkUI?IM_COL32(14,18,16,al):IM_COL32(250,252,251,al)) : WithA(COL_INK,(int)(al*0.85f));
                  ImVec2 ic=V(x+20,y+h*0.5f);
                  if(i==0){ dl->AddRect(V(ic.x-6,ic.y-5),V(ic.x+6,ic.y+5),tc,2,0,1.5f);
                            dl->AddTriangleFilled(V(ic.x-4,ic.y+3),V(ic.x,ic.y-1),V(ic.x+3,ic.y+3),tc); }
                  else if(i==1){ dl->AddRect(V(ic.x-6,ic.y-5),V(ic.x+6,ic.y+5),tc,2,0,1.5f);
                            dl->AddLine(V(ic.x-6,ic.y-2),V(ic.x+6,ic.y-2),tc,1.3f);
                            for(int k=-1;k<=1;k++) dl->AddLine(V(ic.x+k*4.0f,ic.y-5),V(ic.x+k*4.0f-1.5f,ic.y-2),tc,1.2f); }
                  else { dl->PathArcTo(ic,5.5f,0.6f,5.4f,16); dl->PathStroke(tc,0,1.6f);
                         dl->AddTriangleFilled(V(ic.x+4,ic.y-6),V(ic.x+7.5f,ic.y-2),V(ic.x+2.5f,ic.y-1.5f),tc); }
                  TextAt(dl,g_fReg,17,V(x+34,y+h*0.5f-11),tc,lab[i]);
                  if(click&&hov){
                      if(i==0){ g_wallSource=WSRC_IMAGES; SaveConfig(); WallRescan(); g_wallSpring.snap(); return; }
                      if(i==2){ WallRescan(); return; } }
                  x+=pwid[i]+10.0f;
              } }

            // ---- search bar ----
            float sbh=46.0f, sbx=px+22.0f, sbw=pw-44.0f, sby=py+ph-sbh-18.0f;
            { dl->AddRectFilled(V(sbx,sby),V(sbx+sbw,sby+sbh),WithA(COL_INK,(int)(16*e)),sbh*0.5f);
              ImVec2 mc=V(sbx+26,sby+sbh*0.5f);
              dl->AddCircle(V(mc.x-1,mc.y-1),6.0f,WithA(COL_INK2,al),0,1.8f);
              dl->AddLine(V(mc.x+3.5f,mc.y+3.5f),V(mc.x+8,mc.y+8),WithA(COL_INK2,al),1.8f);
              float tx=sbx+48;
              if(g_lsearch[0]){
                  TextAt(dl,g_fReg,17,V(tx,sby+sbh*0.5f-11),WithA(COL_INK,al),g_lsearch);
                  float cw2=TextW(g_fReg,17,g_lsearch);
                  if(fmodf((float)GetTickCount64()/530.0f,2.0f)<1.0f)
                      dl->AddRectFilled(V(tx+cw2+2,sby+13),V(tx+cw2+3.6f,sby+sbh-13),WithA(COL_INK,al));
                  ImVec2 xc=V(sbx+sbw-26,sby+sbh*0.5f);
                  bool xh=fabsf(io.MousePos.x-xc.x)<14&&fabsf(io.MousePos.y-xc.y)<14;
                  if(xh) dl->AddCircleFilled(xc,13,WithA(COL_INK,(int)(30*e)));
                  dl->AddLine(V(xc.x-5,xc.y-5),V(xc.x+5,xc.y+5),WithA(COL_INK2,al),1.8f);
                  dl->AddLine(V(xc.x-5,xc.y+5),V(xc.x+5,xc.y-5),WithA(COL_INK2,al),1.8f);
                  if(click&&xh) g_lsearch[0]=0;
              } else {
                  if(fmodf((float)GetTickCount64()/530.0f,2.0f)<1.0f)
                      dl->AddRectFilled(V(tx,sby+13),V(tx+1.6f,sby+sbh-13),WithA(COL_INK,al));
                  TextAt(dl,g_fReg,17,V(tx+6,sby+sbh*0.5f-11),WithA(COL_INK2,(int)(al*0.75f)),"Search live wallpapers");
              } }

            if(n==0){ const char* m=!ready? "Scanning for live wallpapers\xE2\x80\xA6"
                                  : g_lsearch[0]? "Nothing matches that search" : "No Wallpaper Engine wallpapers found";
                TextAt(dl,g_fSml,15,V(px+pw/2-TextW(g_fSml,15,m)/2,py+150),WithA(COL_INK2,al),m);
                if(ImGui::IsKeyPressed(ImGuiKey_Tab)){ g_wallSource=WSRC_IMAGES; SaveConfig(); WallRescan(); }
                g_launRect=RECT{0,0,(LONG)W,(LONG)H}; return; }

            // ---- cards ----
            const float NW=224.0f, NH=126.0f, CW=284.0f, CH=160.0f;   // 16:9, centre 1.27x
            const float STEP=NW+22.0f, EXTRA=(CW-NW)*0.5f;
            static bool s_drag=false, s_dragMoved=false; static float s_dragLastX=0;
            g_wallSpring.k=260.f; g_wallSpring.d=26.f; g_wallSpring.target=0.f;
            if(!s_drag) g_wallSpring.step(std::min(g_frameDt,0.033f));   // the pointer owns the row while dragging
            if(g_wallSpring.settled(0.3f)) g_wallSpring.snap();
            auto moveBy=[&](int d){ fpos=((fpos+d)%n+n)%n; g_wallSel=fl[fpos]; g_wallSpring.pos+=d*STEP; };
            // ---- drag the row with the mouse: it follows the pointer, and every half-card crossed moves the selection
            { bool inRow = io.MousePos.y>py+60 && io.MousePos.y<sby-4 && io.MousePos.x>px && io.MousePos.x<px+pw;
              if(io.MouseClicked[0] && inRow){ s_drag=true; s_dragMoved=false; s_dragLastX=io.MousePos.x; }
              if(s_drag && io.MouseDown[0]){
                  float dx=io.MousePos.x-s_dragLastX; s_dragLastX=io.MousePos.x;
                  if(fabsf(dx)>0.0f){ g_wallSpring.pos+=dx; g_wallSpring.vel=0; if(fabsf(io.MouseDragMaxDistanceSqr[0])>36.0f) s_dragMoved=true; }
                  while(g_wallSpring.pos> STEP*0.5f){ fpos=((fpos-1)%n+n)%n; g_wallSel=fl[fpos]; g_wallSpring.pos-=STEP; }
                  while(g_wallSpring.pos<-STEP*0.5f){ fpos=((fpos+1)%n+n)%n; g_wallSel=fl[fpos]; g_wallSpring.pos+=STEP; }
              }
              if(!io.MouseDown[0]) s_drag=false; }
            if(ImGui::IsKeyPressed(ImGuiKey_Tab)){ g_wallSource=WSRC_IMAGES; SaveConfig(); WallRescan(); g_wallSpring.snap(); return; }
            if(ImGui::IsKeyPressed(ImGuiKey_RightArrow)) moveBy(+1);
            if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  moveBy(-1);
            if(io.MouseWheel!=0) moveBy(io.MouseWheel>0? -1 : +1);

            float cx=px+pw*0.5f, rowY=py+150.0f;
            struct SC{ int di; float t; };
            std::vector<SC> order;
            for(int di=-4;di<=4;di++){
                if(n<9 && (di<0? -di>(n-1)/2 : di>n/2)) continue;
                order.push_back({di, di+g_wallSpring.pos/STEP});
            }
            std::sort(order.begin(),order.end(),[](const SC& a,const SC& b){ return fabsf(a.t)>fabsf(b.t); });
            dl->PushClipRect(V(px+8,py+60),V(px+pw-8,sby-4),true);
            for(const SC& c:order){
                float at2=fabsf(c.t);
                float k=std::max(0.0f,1.0f-at2);                               // 1 at the centre
                float edge=std::clamp((at2-2.0f)/0.9f,0.0f,1.0f);             // leaving/entering at the sides
                float cw=NW+(CW-NW)*k, ch=NH+(CH-NH)*k;
                cw*=1.0f-0.45f*edge; ch*=1.0f-0.45f*edge;
                float xo=c.t*STEP + (c.t>0?1.0f:-1.0f)*EXTRA*std::min(1.0f,at2);
                float ccx=cx+xo, ccy=rowY;
                int idx=at(fpos+c.di); Wall& w=g_walls[idx];
                int ca=(int)(al*(1.0f-edge));
                ImVec2 a0=V(ccx-cw*0.5f,ccy-ch*0.5f), b0=V(ccx+cw*0.5f,ccy+ch*0.5f);
                bool hov=io.MousePos.x>a0.x&&io.MousePos.x<b0.x&&io.MousePos.y>a0.y&&io.MousePos.y<b0.y+26;
                if(k>0.02f){                                                    // the lifted tile behind the centre card
                    float g=10.0f*k;
                    dl->AddRectFilled(V(a0.x-g,a0.y-g),V(b0.x+g,b0.y+34.0f*k),WithA(COL_INK,(int)(20*k*e)),16.0f);
                    for(int sdw=6;sdw>=1;sdw--) dl->AddRectFilled(V(a0.x-sdw,a0.y-sdw+4),V(b0.x+sdw,b0.y+sdw+4),IM_COL32(0,0,0,(int)(8*k*e)),12.0f+sdw);
                }
                { int aw2=0,ah2=0; ID3D11ShaderResourceView* anim=AnimPrevFrame(w.path, abs(c.di)<=2, aw2,ah2);
                  if(anim){ ImVec2 uv0,uv1; CoverUV(aw2,ah2,cw,ch,uv0,uv1);
                      dl->AddImageRounded((ImTextureID)anim,a0,b0,uv0,uv1,IM_COL32(255,255,255,ca),12.0f); }
                  else if(w.tex){ ImVec2 uv0,uv1; CoverUV(w.w,w.h,cw,ch,uv0,uv1);
                      dl->AddImageRounded((ImTextureID)w.tex,a0,b0,uv0,uv1,IM_COL32(255,255,255,ca),12.0f); }
                  else dl->AddRectFilled(a0,b0,WithA(COL_CARD2,ca),12.0f); }
                // label: the centre one larger and brighter
                float fs=15.0f+3.0f*k;
                std::string nm=Clip(g_fReg,fs,w.name,cw+(k>0.5f?30.0f:0.0f));
                float tw=TextW(g_fReg,fs,nm.c_str());
                TextAt(dl,g_fReg,fs,V(ccx-tw*0.5f,b0.y+6.0f+4.0f*k),
                       Mix(WithA(COL_INK2,ca),WithA(COL_INK,ca),k),nm.c_str());
                if(io.MouseReleased[0]&&hov&&armed&&!s_dragMoved){ if(c.di==0) LLaunch(); else moveBy(c.di); }
            }
            dl->PopClipRect();

            // ---- follow the selection on screen ----
            if(g_liveBrowsePreview && g_lb.active){
                if(g_wallSel!=g_lb.lastSel){ g_lb.lastSel=g_wallSel; g_lb.changedAt=GetTickCount64(); }
                if(g_lb.changedAt && GetTickCount64()-g_lb.changedAt>450 && g_lb.previewed!=g_wallSel && g_lb.lastSel>=0
                   && (g_lb.previewed>=0 || GetTickCount64()-g_launOpenAt>900)){   // do not replace it the instant the picker opens
                    LiveBrowsePreview(g_wallSel,true); g_lb.changedAt=0; }
            }
            if(armed&&(ImGui::IsKeyPressed(ImGuiKey_Enter)||ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) LLaunch();
            g_launRect=RECT{0,0,(LONG)W,(LONG)H};
            return;
        }
        float wsc=std::clamp(g_wallScale,0.6f,2.2f);
        float pw=std::min(W-40.0f,1180.0f*wsc), ph=std::min(H-40.0f,380.0f*wsc);
        float edgeGap=(float)g_gap+34.0f;
        float restY = (g_wallPos==LPOS_CENTRE)? (H-ph)*0.5f
                    : (g_wallPos==LPOS_TOP)   ? edgeGap
                    :                           H-ph-edgeGap;
        float travelW=(ph*0.35f+64.0f)*(1.0f-e);
        float dyW = (g_wallAnim==LANIM_RISE)? travelW
                  : (g_wallAnim==LANIM_DROP)? -travelW : 0.0f;
        float px=(W-pw)/2, py=restY+dyW;
        int vwarp2=dl->VtxBuffer.Size;
        // Caelestia: the image picker comes out of the border too - one surface with the frame, the search bar
        // inside it, no screen-wide defocus and no warp (the border reveal is the motion)
        const bool iBorn = FrameBornOn() && g_wallPos!=LPOS_CENTRE;
        const bool iTop  = g_wallPos==LPOS_TOP;
        float iClipB=H;
        if(iBorn){
            int edge = iTop? EDGE_TOP : EDGE_BOTTOM;
            float bd=FrameInset(edge);
            float rv=std::min(g_launReveal,1.3f);
            float phT=ph+62.0f;                               // carousel + the search bar under it
            py = iTop? bd+phT*rv-phT : H-bd-phT*rv;
            FrameEdgePanel(dl,FrameMon(),V(0,0),V(W,H),edge,px,px+pw,bd+phT*rv,bd,g_wallRound>4.0f? g_wallRound : 30.0f,1.0f);
            if(iTop) dl->PushClipRect(V(px,bd),V(px+pw,std::max(bd,py+phT)),true);
            else     dl->PushClipRect(V(px,py),V(px+pw,std::max(py,H-bd)),true);
            iClipB = iTop? py+phT : H-bd;
        }
        // ---- backdrop: the whole screen eases out of focus as the picker arrives ----
        // The blurred grab is already there (taken when the launcher was summoned); ramping its
        // alpha with the same curve the panel travels on is what makes the defocus feel like part
        // of the same motion rather than a separate flash.
        // ...but NOT once a wallpaper has been applied. This backdrop is a SNAPSHOT taken when the
        // picker opened, drawn opaque over the whole screen. Pressing Enter starts a transition on
        // the desktop underneath and closes the picker over close_ms - so the first couple of
        // hundred milliseconds of a multi-second wipe played out behind a stale picture of the old
        // desktop and was then revealed part-way through. That is why applying from the picker
        // looked broken while prev/next, which has no overlay at all, looked smooth.
        if(g_wallBackdrop && !g_wallJustApplied && W>1.0f && H>1.0f && !iBorn){
            // Walk the focus stack rather than fading one blurred copy in: at e=0 the sharp level
            // sits exactly on the real desktop (invisible), and as e rises we cross-fade toward
            // deeper levels, so the screen genuinely pulls out of focus.
            // NOT the panel's curve. The panel uses EaseOutBack, which springs and is ~done in the
            // first fifth of the duration - riding it made the defocus snap to full blur almost
            // instantly and then sit there. Smoothstep on the raw progress spreads the focus pull
            // evenly across the whole open, which is what makes it read as smooth.
            float ba=std::clamp(a,0.0f,1.0f);
            float be=ba*ba*(3.0f-2.0f*ba);
            float t=be*(float)(LAUNBG_N-1);
            int i0=std::clamp((int)t,0,LAUNBG_N-1), i1=std::clamp(i0+1,0,LAUNBG_N-1);
            float f=std::clamp(t-(float)i0,0.0f,1.0f);
            if(g_launBgL[i0]) dl->AddImage((ImTextureID)g_launBgL[i0],V(0,0),V(W,H),
                                           ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,255));
            if(i1!=i0 && g_launBgL[i1]) dl->AddImage((ImTextureID)g_launBgL[i1],V(0,0),V(W,H),
                                           ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(f*255)));
            dl->AddRectFilled(V(0,0),V(W,H),IM_COL32(0,0,0,(int)(be*96)));   // settle it down a touch
        }
        // Immersive drops the panel entirely - no slab, no title, no footer, just the wallpapers.
        if(!g_wallImmersive && !iBorn){
            if(g_wallBlur && g_launBg && W>1.0f && H>1.0f){
                ImVec2 uv0(std::clamp(px/W,0.0f,1.0f),      std::clamp(py/H,0.0f,1.0f));
                ImVec2 uv1(std::clamp((px+pw)/W,0.0f,1.0f), std::clamp((py+ph)/H,0.0f,1.0f));
                dl->AddImageRounded((ImTextureID)g_launBg,V(px,py),V(px+pw,py+ph),uv0,uv1,
                                    IM_COL32(255,255,255,al),g_wallRound,0);
            }
            GlassPanel(dl,V(px,py),V(px+pw,py+ph),g_wallRound,0,nullptr,e,
                       (int)(std::clamp(g_wallOpacity,0.10f,1.0f)*255.0f));
        }
        if(!g_wallImmersive) TextAt(dl,g_fMed,20,V(px+26,py+18),WithA(COL_INK,al),
                                    g_wallSource==WSRC_LIVE? "Live wallpapers" : "Wallpaper");
        // ---- the search bar, under the carousel - shown whenever the picker is open, immersive or not ----
        { float sbw=std::min(pw*0.5f,460.0f), sbh=44.0f;
          float sbx=px+(pw-sbw)*0.5f, sby= iBorn? py+ph+4.0f : std::min(py+ph+14.0f, H-sbh-12.0f);
          dl->AddRectFilled(V(sbx,sby),V(sbx+sbw,sby+sbh),PanelCol((int)(236*e)),sbh*0.5f);
          dl->AddRect(V(sbx,sby),V(sbx+sbw,sby+sbh),WithA(g_lsearch[0]?COL_GOLD:COL_INK2,(int)((g_lsearch[0]?200:60)*e)),sbh*0.5f,0,1.4f);
          { ImVec2 mc=V(sbx+24,sby+sbh*0.5f); dl->AddCircle(mc,7,WithA(COL_INK2,al),0,2.0f); dl->AddLine(V(mc.x+5,mc.y+5),V(mc.x+10,mc.y+10),WithA(COL_INK2,al),2.0f); }
          float tx=sbx+44;
          if(g_lsearch[0]){
              TextAt(dl,g_fReg,17,V(tx,sby+sbh*0.5f-11),WithA(COL_INK,al),g_lsearch);
              float cw2=TextW(g_fReg,17,g_lsearch);
              if(fmodf((float)GetTickCount64()/530.0f,2.0f)<1.0f)
                  dl->AddRectFilled(V(tx+cw2+2,sby+12),V(tx+cw2+4,sby+sbh-12),WithA(COL_GOLD,al));
              char nm[32]; snprintf(nm,32,"%d",n);
              TextAt(dl,g_fSml,13,V(sbx+sbw-18-TextW(g_fSml,13,nm),sby+sbh*0.5f-8),WithA(COL_INK2,al),nm);
          } else {
              dl->AddRectFilled(V(tx,sby+12),V(tx+2,sby+sbh-12),WithA(COL_GOLD,al));
              TextAt(dl,g_fSml,15,V(tx+8,sby+sbh*0.5f-9),WithA(COL_INK2,al),
                     g_wallSource==WSRC_LIVE? "Search live wallpapers\xE2\x80\xA6   Tab for images" : "Search wallpapers\xE2\x80\xA6   Tab for live");
          } }
        if(g_launStyle==1 && g_wallDots && n>0){
            // accent colour dots (the reference's row of swatches): click = use it as the accent, the ring = the scheme's own
            static const ImU32 DOT[6]={ IM_COL32(229,57,53,255),IM_COL32(30,136,229,255),IM_COL32(67,160,71,255),
                                        IM_COL32(253,216,53,255),IM_COL32(142,36,170,255),IM_COL32(251,140,0,255) };
            float r=11.0f, gapD=10.0f, tot=7*(r*2)+6*gapD;
            float sbh2=44.0f, sby2= iBorn? py+ph+4.0f : std::min(py+ph+14.0f, H-sbh2-12.0f);
            float dy2= iBorn? py+ph-r-10.0f : sby2-r-12.0f;
            float dx=px+(pw-tot)*0.5f+r;
            for(int k=0;k<7;k++){
                ImVec2 c=V(dx+k*(r*2+gapD),dy2);
                bool h=(io.MousePos.x-c.x)*(io.MousePos.x-c.x)+(io.MousePos.y-c.y)*(io.MousePos.y-c.y)<(r+3)*(r+3);
                float ha=HoverAnim(9600+k,h);
                if(k<6){ bool on=g_customAccent && (COL_GOLD&0x00FFFFFF)==(DOT[k]&0x00FFFFFF);
                    dl->AddCircleFilled(c,r+ha*1.5f,WithA(DOT[k],al),24);
                    if(on) dl->AddCircle(c,r+4,WithA(COL_INK,al),24,2.0f);
                    if(h&&click){ g_customAccent=true; g_dynamicColor=false; COL_GOLD=DOT[k];
                        int rr=DOT[k]&0xFF, gg=(DOT[k]>>8)&0xFF, bb=(DOT[k]>>16)&0xFF;
                        COL_GOLDBG= g_darkUI? IM_COL32(rr/3+18,gg/3+18,bb/3+18,255) : IM_COL32(std::min(255,rr+150),std::min(255,gg+120),std::min(255,bb+120),255);
                        SaveConfig(); g_deskDirty=true; } }
                else { dl->AddCircle(c,r-1+ha*1.5f,WithA(COL_INK2,al),24,2.0f); MsIcon(dl,"wallpaper",c,14,WithA(COL_INK2,al));
                    if(h&&click){ g_customAccent=false; g_dynamicColor=true; ApplyScheme(g_scheme); SaveConfig(); g_deskDirty=true; } }
            }
        }
        if(n==0 && ImGui::IsKeyPressed(ImGuiKey_Tab)){ g_wallSource=(g_wallSource+1)%WSRC_N; SaveConfig(); WallRescan(); }
        if(n==0 && ready && g_lsearch[0]){ const char* m="Nothing matches that search";
            TextAt(dl,g_fSml,15,V(px+pw/2-TextW(g_fSml,15,m)/2,py+ph/2),WithA(COL_INK2,al),m);
            if(iBorn) dl->PopClipRect();
            g_launRect=RECT{0,0,(LONG)W,(LONG)H}; return; }
        if(n==0){ const char* m=!ready? "Scanning for wallpapers\xE2\x80\xA6"
                              : g_wallSource==WSRC_LIVE? "No Wallpaper Engine wallpapers found \xE2\x80\x94 Tab for images"
                              : "No images found \xE2\x80\x94 add folders in config\\wallpaper.toml \xC2\xB7 Tab for live";
            TextAt(dl,g_fSml,15,V(px+pw/2-TextW(g_fSml,15,m)/2,py+ph/2),WithA(COL_INK2,al),m);
            if(iBorn) dl->PopClipRect();
            g_launRect=RECT{0,0,(LONG)W,(LONG)H}; return; }
        // ---- cover-flow, ported from WindowPaper: cards shrink and fade with distance, are drawn
        // back-to-front, and NAVIGATION MOVES THE INDEX INSTANTLY while offsetting the spring — so
        // the deck always glides toward target 0 instead of snapping. ----
        // The cards scale WITH the panel. Without this, turning the picker up just left a small
        // carousel adrift in a big empty slab.
        // born from the border: the hero card must fit the panel's interior (title row + a little air)
        float cardFit=1.0f;
        if(iBorn){ float room=ph-(g_wallImmersive? 28.0f : 96.0f); if((float)g_carCH*wsc>room) cardFit=room/((float)g_carCH*wsc); }
        const float CW=(float)g_carCW*wsc*cardFit, CH=(float)g_carCH*wsc*cardFit,
                    NW=(float)g_carNW*wsc*cardFit, NH=(float)g_carNH*wsc*cardFit;
        const float GAP=(float)g_carGap*wsc, FALLOFF=4.5f;
        auto cardW=[&](float d){ float t2=1.0f-d/FALLOFF; return NW+(CW-NW)*powf(std::max(t2,0.0f),1.5f); };
        auto cardH=[&](float d){ float t2=1.0f-d/FALLOFF; return NH+(CH-NH)*powf(std::max(t2,0.0f),1.5f); };
        float stepDist=(CW+cardW(1))*0.5f+GAP;
        g_wallSpring.k=300.f; g_wallSpring.d=26.f; g_wallSpring.target=0.f;
        g_wallSpring.step(std::min(g_frameDt,0.033f));
        if(g_wallSpring.settled(0.4f)) g_wallSpring.snap();
        if(ImGui::IsKeyPressed(ImGuiKey_Tab)){ g_wallSource=(g_wallSource+1)%WSRC_N; SaveConfig(); WallRescan(); g_wallSpring.snap();
            if(iBorn) dl->PopClipRect(); return; }
        if(ImGui::IsKeyPressed(ImGuiKey_RightArrow)){ fpos=((fpos+1)%n+n)%n; g_wallSel=fl[fpos]; g_wallSpring.pos+=stepDist; }
        if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) { fpos=((fpos-1)%n+n)%n; g_wallSel=fl[fpos]; g_wallSpring.pos-=stepDist; }
        if(io.MouseWheel!=0){ int d=(int)(io.MouseWheel>0?-1:1);
            fpos=((fpos+d)%n+n)%n; g_wallSel=fl[fpos]; g_wallSpring.pos+=d*stepDist; }

        float centerX=px+pw*0.5f, cy=py+ph*0.46f;
        if(iBorn) cy = g_wallImmersive? py+ph*0.5f+2.0f : py+ph*0.5f+6.0f;
        struct CardDraw{ int di; float x,y,w,h,alpha; };
        std::vector<CardDraw> cards;
        for(int di=-4;di<=4;di++){
            // a short (searched) list must not wrap round and show the same wallpaper on both sides
            if(n<9 && (di<0? -di>(n-1)/2 : di>n/2)) continue;
            float absd=(float)abs(di);
            float cw=cardW(absd), ch=cardH(absd);
            float alpha=0.15f+0.85f*powf(std::max(1.0f-absd/FALLOFF,0.0f),1.2f);
            float xOff=0;                                  // pack by running half-widths + gap
            for(int d2=1;d2<=abs(di);d2++) xOff += (cardW((float)d2)+cardW((float)(d2-1)))*0.5f+GAP;
            if(di<0) xOff=-xOff;
            float cxp=centerX+xOff+g_wallSpring.pos;
            cards.push_back({di,cxp-cw*0.5f,cy-ch*0.5f,cw,ch,alpha});
        }
        std::sort(cards.begin(),cards.end(),[](const CardDraw&a,const CardDraw&b){ return abs(a.di)>abs(b.di); });

        if(iBorn) dl->PushClipRect(V(px+8,py+(g_wallImmersive? 6.0f : 44.0f)),V(px+pw-8,std::min(py+ph+2.0f,iClipB)),true);
        else dl->PushClipRect(V(px+8,py+40),V(px+pw-8,std::min(py+ph-52,iClipB)),true);
        for(auto& cd:cards){
            int idx=at(fpos+cd.di); bool isCenter=(cd.di==0);
            ImVec2 a0=V(cd.x,cd.y), b0=V(cd.x+cd.w,cd.y+cd.h);
            int ia=(int)(al*cd.alpha);
            bool hov=io.MousePos.x>a0.x&&io.MousePos.x<b0.x&&io.MousePos.y>a0.y&&io.MousePos.y<b0.y;
            if(g_carHoverZoom && isCenter){                  // hover zoom on the hero card
                float hz=HoverAnim(9500,hov)*g_carHoverAmt;
                a0=V(a0.x-hz,a0.y-hz); b0=V(b0.x+hz,b0.y+hz); }
            if(isCenter && g_carShadow){                     // stacked shadow lifts the hero card
                for(int s=12;s>=1;s--) dl->AddRectFilled(V(a0.x-s,a0.y-s),V(b0.x+s,b0.y+s),
                    IM_COL32(0,0,0,(int)(s/12.0f*46*e)),14.0f+s*0.4f); }
            Wall& w=g_walls[idx];
            if(w.tex){ ImVec2 uv0,uv1; CoverUV(w.w,w.h,b0.x-a0.x,b0.y-a0.y,uv0,uv1);
                dl->AddImageRounded((ImTextureID)w.tex,a0,b0,uv0,uv1,IM_COL32(255,255,255,ia),12); }
            else dl->AddRectFilled(a0,b0,WithA(COL_CARD2,ia),12);
            if(!isCenter && g_carShadow)                     // depth vignette on the side cards
                dl->AddRectFilled(a0,b0,IM_COL32(0,0,0,(int)(84*(1.0f-cd.alpha)*e)),12);
            // Immersive means immersive: no filename, no badge, nothing but the picture.
            if(isCenter && g_wallShowName && !g_wallImmersive){
                std::string nm=w.name;
                if(!g_wallShowExt){ size_t d=nm.find_last_of('.'); if(d!=std::string::npos) nm=nm.substr(0,d); }
                while(!nm.empty()&&TextW(g_fSml,14,nm.c_str())>(b0.x-a0.x)-20)nm.pop_back();
                dl->AddRectFilled(V(a0.x,b0.y-26),V(b0.x,b0.y),IM_COL32(0,0,0,(int)(al*0.5f)),12,ImDrawFlags_RoundCornersBottom);
                TextAt(dl,g_fSml,14,V(a0.x+10,b0.y-21),IM_COL32(250,252,251,al),nm.c_str());
            }
            if(g_wallShowExt && isCenter && !g_wallImmersive){   // file-type badge, top-right
                size_t d=w.name.find_last_of('.');
                std::string ex = (d==std::string::npos)? "" : w.name.substr(d+1);
                for(auto& ch:ex) ch=(char)toupper(ch);
                if(!ex.empty()){ float bwd=TextW(g_fSml,11,ex.c_str())+14;
                    dl->AddRectFilled(V(b0.x-bwd-8,a0.y+8),V(b0.x-8,a0.y+28),AccA((int)(220*e)),6);
                    TextAt(dl,g_fSml,11,V(b0.x-bwd-1,a0.y+13),
                           g_darkUI?IM_COL32(12,20,14,al):IM_COL32(250,254,252,al),ex.c_str()); } }
            dl->AddRect(a0,b0, isCenter?WithA(COL_GOLD,(int)(210*e)):WithA(COL_INK2,(int)(40*cd.alpha*e)),
                        12,0, isCenter?2.2f:1.0f);
            if(click&&hov){
                if(isCenter) LLaunch();
                else {   // glide: jump the index, then rewind the spring to where the card was
                    float xo=(cd.x+cd.w*0.5f)-centerX-g_wallSpring.pos;
                    fpos=((fpos+cd.di)%n+n)%n; g_wallSel=fl[fpos]; g_wallSpring.pos+=xo; }
            }
        }
        dl->PopClipRect();
        char cnt[140]; snprintf(cnt,140,"%d / %d   \xE2\x86\x90 \xE2\x86\x92 browse \xC2\xB7 Enter apply \xC2\xB7 Tab %s \xC2\xB7 %s",
                               fpos+1,n,g_wallSource==WSRC_LIVE? "images" : "live",WTRANS_NAME[(int)g_transCfg]);
        if(!g_wallImmersive && !iBorn)
            TextAt(dl,g_fSml,14,V(px+pw/2-TextW(g_fSml,14,cnt)/2,py+ph-34),WithA(COL_INK2,al),cnt);
        if(iBorn) dl->PopClipRect();
        else if(g_wallAnim==LANIM_RISE)
            Warp3D(dl,vwarp2,V(px+pw*0.5f,py+ph),-(1.0f-e)*0.60f);
        else if(g_wallAnim==LANIM_POP)
            ScaleVerts(dl,vwarp2,V(px+pw*0.5f,py+ph*0.5f),0.88f+0.12f*std::clamp(e,0.0f,1.0f));
        if(armed&&(ImGui::IsKeyPressed(ImGuiKey_Enter)||ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) LLaunch();
        g_launRect=RECT{0,0,(LONG)W,(LONG)H};
        return;
    }

    // ---------------- colour schemes (">scheme") ----------------
    if(g_lmode==3){
        int n=NSCHEMES;
        const int COLS=3;
        int rows=(n+COLS-1)/COLS;
        float cw=250.0f, chh=76.0f, gapx=12.0f, gapy=12.0f, padp=22.0f;
        float pw=COLS*cw+(COLS-1)*gapx+padp*2;
        float ph=rows*chh+(rows-1)*gapy+padp*2+46.0f;
        float maxH=H-100.0f; if(ph>maxH) ph=maxH;
        float px=(W-pw)/2, py=(H-ph)/2 + (1.0f-e)*(ph*0.22f+40.0f);
        int vwarp3=dl->VtxBuffer.Size;
        const bool sBorn=FrameBornOn();
        float sBorder=0;
        if(sBorn){
            sBorder=FrameInset(EDGE_BOTTOM);
            float rv=std::min(g_launReveal,1.3f);
            py=H-sBorder-ph*rv;
            FrameEdgePanel(dl,FrameMon(),V(0,0),V(W,H),EDGE_BOTTOM,px,px+pw,sBorder+ph*rv,sBorder,22.0f,1.0f);
            dl->PushClipRect(V(px,0),V(px+pw,H-sBorder),true);
        } else
        GlassPanel(dl,V(px,py),V(px+pw,py+ph),22,0,nullptr,e);
        TextAt(dl,g_fMed,20,V(px+padp,py+16),WithA(COL_INK,al),"Colour scheme");

        // arrows move a grid cursor; the row under a resting pointer must not steal it, same rule
        // the app list learned
        { bool l=ImGui::IsKeyPressed(ImGuiKey_LeftArrow), r=ImGui::IsKeyPressed(ImGuiKey_RightArrow);
          bool u=ImGui::IsKeyPressed(ImGuiKey_UpArrow),   d=ImGui::IsKeyPressed(ImGuiKey_DownArrow);
          if(l) g_schemeSel--; if(r) g_schemeSel++;
          if(u) g_schemeSel-=COLS; if(d) g_schemeSel+=COLS;
          if(l||r||u||d){ g_launMouseLive=false; g_launMousePos=LaunCursorLogical(); }
          g_schemeSel=std::clamp(g_schemeSel,0,n-1); }

        float gx=px+padp, gy=py+46.0f;
        dl->PushClipRect(V(px+6,gy-6),V(px+pw-6,py+ph-8),true);
        for(int i=0;i<n;i++){
            int rr=i/COLS, cc=i%COLS;
            float x0=gx+cc*(cw+gapx), y0=gy+rr*(chh+gapy);
            ImVec2 a0=V(x0,y0), b0=V(x0+cw,y0+chh);
            const Scheme& sc=SCHEMES[i];
            bool hov=io.MousePos.x>a0.x&&io.MousePos.x<b0.x&&io.MousePos.y>a0.y&&io.MousePos.y<b0.y;
            if(hov&&g_launMouseLive) g_schemeSel=i;
            bool sel=(i==g_schemeSel), cur=(i==g_scheme);
            // the card paints itself in ITS OWN colours - that is the whole point of a scheme picker
            dl->AddRectFilled(a0,b0,WithA(sc.panel,al),14);
            dl->AddRect(a0,b0, sel? WithA(sc.accent,al) : WithA(sc.ink2,(int)(al*0.45f)),14,0,sel?2.4f:1.0f);
            for(int k=0;k<4;k++){
                ImU32 sw = k==0? sc.accent : k==1? sc.card : k==2? sc.card2 : sc.ink2;
                dl->AddRectFilled(V(a0.x+14+k*26,a0.y+chh-26),V(a0.x+36+k*26,a0.y+chh-8),WithA(sw,al),5); }
            TextAt(dl,g_fMed,16,V(a0.x+14,a0.y+10),WithA(sc.ink,al),sc.name);
            TextAt(dl,g_fSml,11,V(a0.x+14,a0.y+31),WithA(sc.ink2,al),sc.family);
            { const char* tag=sc.dark?"dark":"light";
              TextAt(dl,g_fSml,11,V(b0.x-14-TextW(g_fSml,11,tag),a0.y+10),WithA(sc.ink2,al),tag); }
            if(cur){ ImVec2 kc=V(b0.x-20,a0.y+chh-17);
                dl->AddLine(V(kc.x-6,kc.y),V(kc.x-2,kc.y+4),WithA(sc.accent,al),2.2f);
                dl->AddLine(V(kc.x-2,kc.y+4),V(kc.x+6,kc.y-5),WithA(sc.accent,al),2.2f); }
            if(armed&&click&&hov){ ApplySchemeChoice(i); g_launShow=false; }
        }
        dl->PopClipRect();
        { const char* hint="\xE2\x86\x90 \xE2\x86\x92 \xE2\x86\x91 \xE2\x86\x93 browse \xC2\xB7 Enter apply \xC2\xB7 Esc back";
          TextAt(dl,g_fSml,13,V(px+pw/2-TextW(g_fSml,13,hint)/2,py+ph-26),WithA(COL_INK2,al),hint); }
        if(sBorn) dl->PopClipRect(); else
        Warp3D(dl,vwarp3,V(px+pw*0.5f,py+ph),-(1.0f-e)*0.45f);
        if(armed&&(ImGui::IsKeyPressed(ImGuiKey_Enter)||ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) LLaunch();
        g_launRect=RECT{0,0,(LONG)W,(LONG)H};
        return;
    }

    // ---------------- other layouts (grid / strip / column / ring) ----------------
    if(g_lmode==0 && g_launLayout!=0){ LfxDrawAltLayout(io,dl,W,H,a,e,al,armed,click); return; }
    // ---------------- app / command list ----------------
    bool cmdMode=(g_lmode==1);
    bool clipMode=(g_lmode==4);
    int calcRows = (!cmdMode && !clipMode && g_calcOk)?1:0;       // a maths result row at the top
    int winN  = (cmdMode||clipMode)? 0 : (int)g_winHits.size();
    int total = cmdMode? (int)g_cfilt.size()
              : clipMode? (int)g_clipFilt.size()
              : ((int)g_lfilt.size()+calcRows+winN);
    bool ctrl=ImGui::GetIO().KeyCtrl;   // Vim-style navigation, like Caelestia's launcher
    { bool down=ImGui::IsKeyPressed(ImGuiKey_DownArrow) || (ctrl&&ImGui::IsKeyPressed(ImGuiKey_J)) || (ctrl&&ImGui::IsKeyPressed(ImGuiKey_N));
      bool up  =ImGui::IsKeyPressed(ImGuiKey_UpArrow)   || (ctrl&&ImGui::IsKeyPressed(ImGuiKey_K)) || (ctrl&&ImGui::IsKeyPressed(ImGuiKey_P));
      if(down) g_lsel++;
      if(up)   g_lsel--;
      // arrowing hands the selection back to the keyboard: without this the row under a resting
      // pointer would snatch it straight back on the very next frame
      if(down||up){ g_launMouseLive=false; g_launMousePos=LaunCursorLogical(); } }
    if(total>0)g_lsel=((g_lsel%total)+total)%total; else g_lsel=0;
    if(armed&&(ImGui::IsKeyPressed(ImGuiKey_Enter)||ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))){ LLaunch(); return; }
    if(clipMode && ImGui::IsKeyPressed(ImGuiKey_Delete) && g_lsel>=0 && g_lsel<(int)g_clipFilt.size()){
        { std::lock_guard<std::mutex> lk(g_clipMtx);
          int ci=g_clipFilt[g_lsel];
          if(ci>=0 && ci<(int)g_clips.size()) g_clips.erase(g_clips.begin()+ci); }
        ClipRebuild(g_lsearch);
        if(g_lsel>=(int)g_clipFilt.size()) g_lsel=std::max(0,(int)g_clipFilt.size()-1);
        return; }

    float rowH = g_launchDesc? 54.0f : 42.0f;
    int visRows=std::min(std::max(total,1),g_launchMax);
    float searchH=58, padv=12;
    float pw=std::clamp(g_launWidth,360.0f,std::max(360.0f,W-40.0f)), ph=padv*2+visRows*rowH+searchH+10;
    const float trayH=(!cmdMode&&!clipMode)? LfxTrayH() : 0.0f; ph+=trayH;
    // grow / shrink to fit the results on the launcher's own motion (Caelestia's implicitHeight Behavior)
    { static ULONGLONG s_openSeen=0; float want=ph;
      if(g_launResize && s_openSeen==g_launOpenAt && a>0.98f) ph=MotionAnim(MP_LAUNCHER,830021,want);
      else { Cael::animX(830021,want,1,Cael::LINEAR,0,0,1,1); auto it=Cael::g_st.find(830021);
             if(it!=Cael::g_st.end()){ it->second.cur=it->second.from=it->second.to=want; } if(a>0.98f) s_openSeen=g_launOpenAt; } }
    // the result list changed (typed, or ">" swapped to commands): fade / slide the new rows in
    static float s_sw=1.0f; { static int s_key=-1; static ULONGLONG s_keyOpen=0;
      int key=(cmdMode?1000000:0)+(clipMode?2000000:0)+total*7+(int)strlen(g_lsearch);
      if(s_keyOpen!=g_launOpenAt){ s_keyOpen=g_launOpenAt; s_key=key; s_sw=1.0f; }
      else if(key!=s_key){ s_key=key; if(g_launSwitchAnim!=2 && g_launSwitchMs>0) s_sw=0.0f; }
      s_sw=std::min(1.0f,s_sw+g_frameDt*1000.0f/std::max(1,g_launSwitchMs)); }
    // BOTTOM-ANCHORED, like the reference: the search bar sits near the bottom edge of the desktop
    // and the panel rises up out of it rather than fading in dead centre.
    float bottomGap=(float)g_gap+26.0f;
    float restY = (g_launPos==LPOS_CENTRE)? (H-ph)*0.5f
                : (g_launPos==LPOS_TOP)   ? bottomGap
                :                           H-ph-bottomGap;
    // how the panel travels in. Rise/Drop move it; Pop scales it; Fade and None leave it put.
    float travel = (ph*0.30f+56.0f)*(1.0f-e);
    float dy = (g_launAnimStyle==LANIM_RISE)? travel
             : (g_launAnimStyle==LANIM_DROP)? -travel : 0.0f;
    float px=(W-pw)/2, py=restY+dy;
    // Born from the bottom (or top) border: the panel comes up out of the frame with its content revealed
    // from the border, instead of rising, popping or warping in as a card. Centre stays a floating card.
    const bool lBorn = FrameBornOn() && g_launPos!=LPOS_CENTRE && !LfxWantsFloat();
    float lBorder=0, lReveal=1;
    if(lBorn){
        int ed = (g_launPos==LPOS_TOP)? EDGE_TOP : EDGE_BOTTOM;
        lBorder=FrameInset(ed);
        lReveal=std::min(g_launReveal,1.3f);
        py = (ed==EDGE_BOTTOM)? H-lBorder-ph*lReveal : lBorder-ph*(1.0f-lReveal);
    }
    int vwarp=dl->VtxBuffer.Size;

    // frost first, sampled at the panel's real screen position, then the panel's own tinted fill
    // over the top of it at whatever opacity the user picked.
    if(lBorn){
        int ed = (g_launPos==LPOS_TOP)? EDGE_TOP : EDGE_BOTTOM;
        FrameEdgePanel(dl,FrameMon(),V(0,0),V(W,H),ed,px,px+pw,lBorder+ph*lReveal,lBorder,g_launRound,1.0f);
        if(ed==EDGE_BOTTOM) dl->PushClipRect(V(px,0),V(px+pw,H-lBorder),true);
        else                dl->PushClipRect(V(px,lBorder),V(px+pw,H),true);
    } else if(LfxCustomPanel()){
        float ins=LfxShapeInset(g_launShape,pw,ph);
        LfxPanel(dl,V(px-ins,py),V(px+pw+ins,py+ph),g_launRound,e,W,H);
    } else {
    if(g_launBlur && g_launBg && W>1.0f && H>1.0f){
        ImVec2 uv0(std::clamp(px/W,0.0f,1.0f),           std::clamp(py/H,0.0f,1.0f));
        ImVec2 uv1(std::clamp((px+pw)/W,0.0f,1.0f),      std::clamp((py+ph)/H,0.0f,1.0f));
        dl->AddImageRounded((ImTextureID)g_launBg,V(px,py),V(px+pw,py+ph),uv0,uv1,
                            IM_COL32(255,255,255,al),g_launRound,0);
    }
    GlassPanel(dl,V(px,py),V(px+pw,py+ph),g_launRound,0,nullptr,e,
               (int)(std::clamp(g_launOpacity,0.10f,1.0f)*255.0f));
    }

    // rows (top region)
    float listTop=py+padv, listBot=py+ph-searchH-padv;
    float trayY=0; if(trayH>0){ if(g_launTray==1){ trayY=listTop; listTop+=trayH; } else { listBot-=trayH; trayY=listBot+2; } }
    // Scrolling follows the KEYBOARD only, and only as far as needed. It used to re-centre on the
    // selection every frame - so a hover that changed the selection scrolled the list, which slid a
    // different row under the pointer, which the hover then selected, which scrolled again: the
    // "launcher keeps scrolling if I just move my mouse down a bit" report.
    static int s_first=0, s_lastSel=-1, s_lastTotal=-1; static ULONGLONG s_openStamp=0;
    if(s_openStamp!=g_launOpenAt){ s_openStamp=g_launOpenAt; s_lastTotal=-1; }
    int first=0;
    if(total>visRows){
        if(total!=s_lastTotal) s_first=std::clamp(g_lsel-visRows/2,0,total-visRows);   // a new result set
        else if(g_lsel!=s_lastSel){                                                    // the keyboard moved it
            if(g_lsel<s_first) s_first=g_lsel;
            else if(g_lsel>=s_first+visRows) s_first=g_lsel-visRows+1; }
        s_first=std::clamp(s_first,0,total-visRows); first=s_first;
    } else s_first=0;
    s_lastTotal=total; s_lastSel=g_lsel;
    const char* tipLaun=nullptr;
    dl->PushClipRect(V(px+6,listTop-2),V(px+pw-6,listBot+2),true);
    int alKeep=al; float swE=Cael::eval(Cael::EMPHASIZED_DECEL,s_sw);
    al=(int)(al*(g_launSwitchAnim==2? 1.0f : swE));
    float swOff=(g_launSwitchAnim==1)? (1.0f-swE)*14.0f : 0.0f;
    if(g_launSel==1 && total>0 && g_lsel>=first && g_lsel<first+visRows){ float gry=listTop+(g_lsel-first)*rowH+swOff; LfxGlide(dl,V(px+10,gry),V(px+pw-10,gry+rowH-4),12,e,0); }
    if(total==0){ const char* m = cmdMode? "No matching command"
                                : clipMode? (g_clipEnable? "Clipboard history is empty" : "Clipboard history is off")
                                : "No matching app";
        TextAt(dl,g_fSml,15,V(px+pw/2-TextW(g_fSml,15,m)/2,(listTop+listBot)/2-8),WithA(COL_INK2,al),m); }
    for(int r=0;r<visRows;r++){
        int idx=first+r; if(idx>=total)break;
        // Rows cascade: each starts a beat after the one above and slides up into place. The list
        // is already clipped, so a row that has not started yet is simply still below the edge.
        float rs = 1.0f;
        if(g_launStagger && g_launAnimStyle!=LANIM_NONE && g_launResAnim==0)
            rs = EaseOutCubic(std::clamp((e - (float)r*0.055f)/0.55f, 0.0f, 1.0f));
        float ry=listTop+r*rowH + (1.0f-rs)*26.0f + swOff;
        ImVec2 a0=V(px+10,ry),b0=V(px+pw-10,ry+rowH-4);
        int vRow=dl->VtxBuffer.Size; ID3D11ShaderResourceView* rowIc=nullptr;
        bool hov=io.MousePos.x>a0.x&&io.MousePos.x<b0.x&&io.MousePos.y>a0.y&&io.MousePos.y<b0.y;
        if(hov && g_launMouseLive){ g_lsel=idx; s_lastSel=idx; }   // a hover selects; it never scrolls
        bool sel=(idx==g_lsel);
        const bool cael=(g_launStyle==1);
        const bool selFill=(g_launSel==0);
        if(!selFill) LfxRowSel(dl,a0,b0,sel,hov,e,idx);
        else if(cael){ if(sel) dl->AddRectFilled(a0,b0,WithA(Mix(COL_CARD2,COL_INK2,0.22f),(int)(e*255)),12);
                  else if(hov) dl->AddRectFilled(a0,b0,WithA(Mix(COL_CARD2,COL_INK2,0.10f),(int)(e*200)),12); }
        else if(sel) dl->AddRectFilled(a0,b0,WithA(COL_GOLD,(int)(e*225)),12);
        else if(hov) dl->AddRectFilled(a0,b0,WithA(COL_INK2,(int)(e*40)),12);
        ImU32 nameCol = (sel && !cael && selFill) ? (g_darkUI?IM_COL32(14,20,16,255):IM_COL32(250,254,252,255)) : COL_INK;
        ImU32 descCol = sel ? MulA(nameCol,0.72f) : COL_INK2;
        float tx=a0.x+14;
        if(cmdMode){
            const LCmd& c=LCMDS[g_cfilt[idx]];
            if(cael) MsIcon(dl,LCmdIcon(c.name),V(a0.x+23,ry+(rowH-4)/2),24,WithA(nameCol,al));
            else {
            // ">" glyph badge instead of an icon
            dl->AddRectFilled(V(a0.x+10,ry+(rowH-4)/2-13),V(a0.x+36,ry+(rowH-4)/2+13),WithA(descCol,60),8);
            TextAt(dl,g_fMed,17,V(a0.x+17,ry+(rowH-4)/2-11),WithA(nameCol,al),">"); }
            tx=a0.x+48;
            { std::string cn=c.name; if(cael && !cn.empty()) cn[0]=(char)toupper((unsigned char)cn[0]); TextAt(dl,g_fMed,17,V(tx,ry+(g_launchDesc?8.0f:10.0f)),WithA(nameCol,al),cn.c_str()); }
            if(g_launchDesc) TextAt(dl,g_fSml,13,V(tx,ry+30),WithA(descCol,al),c.desc);
        } else if(clipMode){
            // pinned rows keep a filled marker; the rest get the copy glyph
            int ci = (idx<(int)g_clipFilt.size())? g_clipFilt[idx] : -1;
            std::string prev, ago; bool pinned=false;
            { std::lock_guard<std::mutex> lk(g_clipMtx);
              if(ci>=0 && ci<(int)g_clips.size()){
                  prev=g_clips[ci].preview; ago=ClipAgo(g_clips[ci].at); pinned=g_clips[ci].pinned; } }
            dl->AddRectFilled(V(a0.x+10,ry+(rowH-4)/2-13),V(a0.x+36,ry+(rowH-4)/2+13),WithA(descCol,60),8);
            if(pinned) dl->AddCircleFilled(V(a0.x+23,ry+(rowH-4)/2),5.0f,WithA(nameCol,al));
            else { dl->AddRect(V(a0.x+18,ry+(rowH-4)/2-5),V(a0.x+27,ry+(rowH-4)/2+4),WithA(nameCol,al),2,0,1.4f);
                   dl->AddRect(V(a0.x+20,ry+(rowH-4)/2-7),V(a0.x+29,ry+(rowH-4)/2+2),WithA(nameCol,(int)(al*0.55f)),2,0,1.4f); }
            tx=a0.x+48;
            // the timestamp is right-aligned, so clip the preview to whatever is left
            float agoW = ago.empty()? 0.0f : TextW(g_fSml,13,ago.c_str())+16.0f;
            std::string shown=Clip(g_fMed,17,prev,(b0.x-14-agoW)-tx);
            TextAt(dl,g_fMed,17,V(tx,ry+(g_launchDesc?8.0f:10.0f)),WithA(nameCol,al),shown.c_str());
            if(g_launchDesc) TextAt(dl,g_fSml,13,V(tx,ry+30),WithA(descCol,al),
                                    pinned? "Pinned \xC2\xB7 click the marker to unpin" : "Enter to paste \xC2\xB7 Del to forget");
            if(!ago.empty()) TextAt(dl,g_fSml,13,V(b0.x-14-TextW(g_fSml,13,ago.c_str()),ry+(rowH-4)/2-8),WithA(descCol,al),ago.c_str());
            // clicking the marker pins instead of pasting
            if(click && io.MousePos.x<a0.x+40 && hov && ci>=0){
                { std::lock_guard<std::mutex> lk(g_clipMtx);
                  if(ci<(int)g_clips.size()) g_clips[ci].pinned=!g_clips[ci].pinned; }
                ClipRebuild(g_lsearch); break; }
        } else if(calcRows && idx==0){
            // ---- calculator result row (Caelestia CalcItem) ----
            dl->AddRectFilled(V(a0.x+10,ry+(rowH-4)/2-13),V(a0.x+36,ry+(rowH-4)/2+13),WithA(descCol,60),8);
            TextAt(dl,g_fMed,17,V(a0.x+16,ry+(rowH-4)/2-11),WithA(nameCol,al),"=");
            tx=a0.x+48;
            TextAt(dl,g_fMed,17,V(tx,ry+(g_launchDesc?8.0f:10.0f)),WithA(nameCol,al),g_calcExpr.c_str());
            if(g_launchDesc) TextAt(dl,g_fSml,13,V(tx,ry+30),WithA(descCol,al),"Enter to copy the result");
            char rb[64]; double v=g_calcVal;
            if(fabs(v-llround(v))<1e-9 && fabs(v)<9e15) snprintf(rb,64,"%lld",(long long)llround(v));
            else snprintf(rb,64,"%.10g",v);
            std::string res=std::string("= ")+rb;
            TextAt(dl,g_fMed,21,V(b0.x-14-TextW(g_fMed,21,res.c_str()),ry+(rowH-4)/2-13),WithA(nameCol,al),res.c_str());
        } else if(idx < calcRows+winN){
            // an ALREADY OPEN window - Enter switches to it instead of starting another copy
            const LWin& d=g_winHits[idx-calcRows];
            float isz=g_launchDesc?32.0f:26.0f;
            if(d.icon) dl->AddImage((ImTextureID)d.icon,V(a0.x+12,ry+((rowH-4)-isz)/2),V(a0.x+12+isz,ry+((rowH-4)+isz)/2),
                                    ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,al));
            tx=a0.x+12+isz+14;
            std::string ttl=d.title.empty()? std::string("(untitled window)") : d.title;
            TextAt(dl,g_fMed,17,V(tx,ry+(g_launchDesc?8.0f:10.0f)),WithA(nameCol,al),
                   Clip(g_fMed,17,ttl,(b0.x-90)-tx).c_str());
            if(g_launchDesc){
                std::string sub=std::string("Open window \xC2\xB7 ")+d.exe;
                TextAt(dl,g_fSml,13,V(tx,ry+30),WithA(descCol,al),sub.c_str()); }
            { const char* tag="switch";
              TextAt(dl,g_fSml,13,V(b0.x-14-TextW(g_fSml,13,tag),ry+(rowH-4)/2-8),WithA(descCol,al),tag); }
        } else {
            auto& app=g_lapps[g_lfilt[idx-calcRows-winN]];
            ID3D11ShaderResourceView* appIc = app.icon? app.icon : LfxIcon(app.path,false);   // loads off-thread
            rowIc=appIc;
            float isz=g_launchDesc?32.0f:26.0f;
            { int vi=dl->VtxBuffer.Size;
              if(appIc) dl->AddImage((ImTextureID)appIc,V(a0.x+12,ry+((rowH-4)-isz)/2),V(a0.x+12+isz,ry+((rowH-4)+isz)/2),
                                      ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,al));
              LfxIconPost(dl,vi,V(a0.x+12+isz*0.5f,ry+(rowH-4)*0.5f),HoverAnim(0x7D200+idx,hov)); }
            if(armed && hov && io.MouseClicked[1]){ LfxToggleFav(W2U8(app.path)); break; }   // right-click pins / unpins
            tx=a0.x+12+isz+14;
            std::string nm=W2U8(app.name);
            float textR = cael? b0.x-86 : b0.x-10;
            TextAt(dl,g_fMed,17,V(tx,ry+(g_launchDesc?8.0f:10.0f)),WithA(nameCol,al),Clip(g_fMed,17,nm,textR-tx).c_str());
            if(g_launchDesc) TextAt(dl,g_fSml,13,V(tx,ry+30),WithA(descCol,al),Clip(g_fSml,13,app.desc,textR-tx).c_str());
            if(cael){
                std::string p=W2U8(app.path);
                auto inList=[&](std::vector<std::string>& v)->bool{ for(auto& x:v) if(_stricmp(x.c_str(),p.c_str())==0) return true; return false; };
                bool fav=inList(g_launFavs);
                float cyR=ry+(rowH-4)*0.5f;
                ImVec2 hc=V(b0.x-58,cyR), vc=V(b0.x-24,cyR);
                bool hh=fabsf(io.MousePos.x-hc.x)<15&&fabsf(io.MousePos.y-hc.y)<15;
                bool vh=fabsf(io.MousePos.x-vc.x)<15&&fabsf(io.MousePos.y-vc.y)<15;
                if(hh) dl->AddCircleFilled(hc,15,WithA(COL_INK2,50),20);
                if(vh) dl->AddCircleFilled(vc,15,WithA(COL_INK2,50),20);
                if(fav){ // a filled heart: the outline glyph over a heart-shaped fill
                    dl->AddCircleFilled(V(hc.x-3.4f,hc.y-2.6f),4.6f,WithA(COL_INK,al),14); dl->AddCircleFilled(V(hc.x+3.4f,hc.y-2.6f),4.6f,WithA(COL_INK,al),14);
                    dl->AddTriangleFilled(V(hc.x-8.0f,hc.y-1.4f),V(hc.x+8.0f,hc.y-1.4f),V(hc.x,hc.y+7.6f),WithA(COL_INK,al)); }
                MsIcon(dl,"favorite",hc,20,WithA(fav? COL_INK : descCol,al));
                MsIcon(dl,"visibility",vc,20,WithA(descCol,al));
                if(hh&&vh) {}
                if(click&&hh){ if(fav){ g_launFavs.erase(std::remove_if(g_launFavs.begin(),g_launFavs.end(),[&](const std::string& x){ return _stricmp(x.c_str(),p.c_str())==0; }),g_launFavs.end()); }
                               else g_launFavs.push_back(p);
                               SaveConfig(); LRebuild(); break; }
                if(click&&vh){ if(!inList(g_launHidden)) g_launHidden.push_back(p); SaveConfig(); LRebuild(); break; }
                if(hh) tipLaun = fav? "Remove from favourites" : "Add to favourites";
                if(vh) tipLaun = "Hide from the launcher (>unhide brings it back)";
            }
        }
        LfxRowPost(dl,vRow,a0,b0,r,e);
        if(click&&hov){ LfxLaunchFx(V(a0.x+28,ry+(rowH-4)*0.5f),rowIc); LLaunch(); break; }
    }
    dl->PopClipRect();
    al=alKeep;
    if(trayH>0 && LfxDrawTray(dl,io,px+10,trayY,px+pw-10,al,e,click)) g_launShow=false;

    // ---- search bar AT THE BOTTOM ----
    float sy=py+ph-searchH-6, sx0=px+12, sx1=px+pw-12;
    if(tipLaun){ float tw=TextW(g_fSml,13,tipLaun)+20; ImVec2 t0=V(std::clamp(io.MousePos.x-tw*0.5f,px+8,px+pw-tw-8),io.MousePos.y-38);
        dl->AddRectFilled(t0,V(t0.x+tw,t0.y+26),IM_COL32(28,28,34,240),13); TextAt(dl,g_fSml,13,V(t0.x+10,t0.y+5),IM_COL32(236,236,240,255),tipLaun); }
    if(g_launStyle==1){
        dl->AddRectFilled(V(sx0,sy),V(sx1,sy+searchH-8),WithA(Mix(COL_CARD2,COL_INK2,0.10f),(int)(e*255)),(searchH-8)*0.5f);
    } else {
    dl->AddRectFilled(V(sx0,sy),V(sx1,sy+searchH-8),WithA(COL_CARD,(int)(e*235)),14);
    dl->AddRect(V(sx0,sy),V(sx1,sy+searchH-8),AccA((int)(e*70)),14,0,1.2f); }
    // magnifier
    { ImVec2 mc=V(sx0+24,sy+(searchH-8)/2);
      dl->AddCircle(mc,7,WithA(COL_INK2,al),0,2.0f);
      dl->AddLine(V(mc.x+5,mc.y+5),V(mc.x+10,mc.y+10),WithA(COL_INK2,al),2.0f); }
    float qx=sx0+44;
    // the query: per-letter animation, caret style, smear and sparks (LauncherFx.h) - the defaults draw it as before
    { float chipW=(!cmdMode&&!clipMode)? LfxAliasChip(dl,sx1,sy,searchH-8,al) : 0.0f;
      dl->PushClipRect(V(qx-4,sy),V(sx1-chipW-8,sy+searchH),true);
      LfxSearchText(dl,g_fReg,18,V(qx,sy+(searchH-8)/2-11),22,g_lsearch,
                    clipMode? "Search what you copied\xE2\x80\xA6   Esc goes back"
                            : (g_launStyle==1? "Type \">\" for commands" : "Search apps\xE2\x80\xA6   Type > for commands"),al);
      dl->PopClipRect(); }

    LfxDrawLaunchFx(dl);
    if(lBorn){ dl->PopClipRect(); g_launRect=RECT{0,0,(LONG)W,(LONG)H}; return; }   // no warp/pop/bounce: the border reveal IS the animation
    // stand the panel up out of the desktop plane as it opens (hinge on its bottom edge)
    // Rise keeps the hinge it always had (the panel stands up out of the desktop plane); Pop scales
    // the whole thing about its centre; Fade and Drop just travel, carried by `e` alone.
    if(g_launAnimStyle==LANIM_RISE)
        Warp3D(dl,vwarp,V(px+pw*0.5f,py+ph),-(1.0f-e)*0.62f);
    else if(g_launAnimStyle==LANIM_POP)
        ScaleVerts(dl,vwarp,V(px+pw*0.5f,py+ph*0.5f),0.90f+0.10f*std::clamp(e,0.0f,1.0f));
    LfxTilt(dl,vwarp,V(px+pw*0.5f,py+ph*0.5f),pw,ph,e);   // 3D: face the pointer
    // scale+rise about the panel centre: the bounce lives here so every element morphs together
    // Only the moving styles get it. It used to run for EVERY style, so "none" and "fade" still bounced
    // and "drop" rose - recorded at 60 fps, rise and drop were indistinguishable.
    if(g_launAnimStyle==LANIM_RISE || g_launAnimStyle==LANIM_POP || g_launAnimStyle==LANIM_DROP)
    { ImDrawList* bdl=ImGui::GetBackgroundDrawList();
      float sc = (g_launAnimStyle==LANIM_POP)? 0.86f+0.14f*e : 1.0f;
      float rise=(1.0f-e)*46.0f*(g_launAnimStyle==LANIM_RISE? 1.0f : g_launAnimStyle==LANIM_DROP? -1.0f : 0.0f);
      ImVec2 c=V(W*0.5f,H*0.52f);
      for(int i=vlau;i<bdl->VtxBuffer.Size;i++){
          ImVec2& p2=bdl->VtxBuffer[i].pos;
          p2.x=c.x+(p2.x-c.x)*sc; p2.y=c.y+(p2.y-c.y)*sc+rise; } }
    g_launRect=RECT{0,0,(LONG)W,(LONG)H};
}


// The idle dim layer: a click-through veil that fades the screen down after a spell of no input,
// with a soft clock so the machine still reads as "awake, just resting".
static void DrawDim(){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x,H=io.DisplaySize.y;
    float a=std::clamp(g_dimAnim,0.0f,1.0f);
    float e=EaseOutCubic(a);
    // ---- idle-dim veil + clock (only when actually dimming) ----
    if(a>=0.002f){
        dl->AddRectFilled(V(0,0),V(W,H),IM_COL32(0,0,0,(int)(255*g_dimLevel*e)));
        float ca=std::clamp((a-0.45f)/0.55f,0.0f,1.0f);
        if(ca>0.002f){
            time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
            char clk[8]; snprintf(clk,8,"%d:%02d",g_clock24?lt.tm_hour:((lt.tm_hour%12)==0?12:lt.tm_hour%12),lt.tm_min);
            float cw=g_fHuge->CalcTextSizeA(96,FLT_MAX,0,clk).x;
            dl->AddText(g_fHuge,96,V(W/2-cw/2,H*0.40f-(1.0f-ca)*14.0f),IM_COL32(232,236,242,(int)(210*ca)),clk);
            const char* mn[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
            const char* dn[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
            char date[64]; snprintf(date,64,"%s, %s %d",dn[lt.tm_wday],mn[lt.tm_mon],lt.tm_mday);
            TextAt(dl,g_fMed,21,V(W/2-TextW(g_fMed,21,date)/2,H*0.40f+104),IM_COL32(180,186,196,(int)(170*ca)),date);
        }
    }
    // ---- pre-lock countdown: lock logo + depleting ring + seconds, morphs into the lock screen ----
    float p=std::clamp(g_preLock,0.0f,1.0f);
    if(p>0.002f){
        float T=(float)ImGui::GetTime();
        float pe=EaseOutBack(p);
        dl->AddRectFilled(V(0,0),V(W,H),IM_COL32(6,6,10,(int)(150*p)));    // extra darken so it reads
        ImVec2 c=V(W*0.5f,H*0.42f);
        float rem=(float)g_preLockRemain; int secs=(int)ceilf(rem-0.001f); if(secs<1)secs=1;
        float frac=std::clamp(rem/(float)g_preLockSecs,0.0f,1.0f);
        float tick=rem-floorf(rem);                                        // 1..0 within each second
        float beat=1.0f+0.10f*(1.0f-EaseOutCubic(std::clamp(1.0f-tick,0.0f,1.0f)));  // pulse on each tick
        float R=76*pe*beat;
        // depleting ring (starts full at top, unwinds clockwise)
        dl->PathArcTo(c,R,-1.5708f,-1.5708f+6.2832f,72); dl->PathStroke(WithA(COL_INK2,(int)(110*p)),0,7.0f);
        dl->PathArcTo(c,R,-1.5708f,-1.5708f+6.2832f*frac,72); dl->PathStroke(WithA(COL_GOLD,(int)(255*p)),0,7.0f);
        dl->AddCircleFilled(V(c.x+cosf(-1.5708f+6.2832f*frac)*R,c.y+sinf(-1.5708f+6.2832f*frac)*R),5.0f,WithA(COL_GOLD,(int)(255*p)));
        // padlock glyph in the middle (same shape as the lock screen intro -> seamless hand-off)
        float lr=30*pe*beat; ImU32 lc=WithA(COL_GOLD,(int)(255*p));
        dl->AddRectFilled(V(c.x-lr*0.6f,c.y),V(c.x+lr*0.6f,c.y+lr*0.9f),lc,6);
        dl->PathClear(); for(int k=0;k<=16;k++){ float t=3.1416f*k/16; dl->PathLineTo(V(c.x+(-lr*0.42f)*cosf(t),c.y+(-lr*0.42f)*sinf(t))); }
        dl->PathStroke(lc,0,4.5f);
        dl->AddCircleFilled(V(c.x,c.y+lr*0.42f),3.0f,WithA(g_darkUI?IM_COL32(10,10,14,255):IM_COL32(250,250,254,255),(int)(255*p)));
        // seconds number + hint below
        char sb[8]; snprintf(sb,8,"%d",secs);
        float nw=g_fHuge->CalcTextSizeA(64,FLT_MAX,0,sb).x;
        dl->AddText(g_fHuge,64,V(c.x-nw/2,c.y+R+24),WithA(COL_INK,(int)(255*p)),sb);
        const char* hint="Locking \xE2\x80\x94 move the mouse to cancel";
        TextAt(dl,g_fMed,18,V(c.x-TextW(g_fMed,18,hint)/2,c.y+R+104),WithA(COL_INK2,(int)(220*p)),hint);
    }
    StvDrawDim(dl,W,H);    // Strive workspace banner + intro (opt-in) ride this topmost, click-through layer
}
