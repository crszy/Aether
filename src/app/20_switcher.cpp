// Aether - the window switcher (Alt+Tab).
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =============================================================================================
// WINDOW SWITCHER  —  the GNOME Shell Alt+Tab, in Caelestia's material.
// A row of large app icons on one rounded glass slab, the selection a rounded box that GLIDES
// between them, the selected window's title centred underneath. Deliberately NOT the coverflow
// switcher (Z:\CoverFlowSwitcher) — that one is a 3-D card carousel; this is the flat, calm,
// icon-row switcher every Linux desktop ships, which is what "a linux feel" means here.
//
// Driven entirely from the existing low-level keyboard hook, so it can swallow the real Alt+Tab
// and Windows' own switcher never appears. The hook only ever SETS a request; all the work
// happens on the render loop, because a LL-hook callback must return fast.
// =============================================================================================
static ID3D11ShaderResourceView* CaptureWindowTex(HWND,int&,int&,int maxw);   // fwd (defined with the bar preview)

// A CRISP app icon. WM_GETICON only ever hands back a 32/48px image, which is why the switcher's
// icons looked upscaled and soft. The shell keeps a 256px JUMBO image list — the same source the
// real taskbar and Alt+Tab use — so ask that first and fall back to the window icon.
// Jumbo entries are a 256x256 canvas with a smaller icon centred in it when the app ships nothing
// that big, so the raster is trimmed to its alpha bounding box; the texture is then whatever size
// the artwork actually is and the GPU filters it down to the cell.
static ID3D11ShaderResourceView* IconTexTrim(HICON ico,int raster){
    if(!ico) return nullptr;
    HDC scr=GetDC(nullptr),mem=CreateCompatibleDC(scr);
    BITMAPINFO bi={}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=raster;
    bi.bmiHeader.biHeight=-raster; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr; HBITMAP bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    HGDIOBJ old=SelectObject(mem,bmp);
    ID3D11ShaderResourceView* tex=nullptr;
    if(bits){
        memset(bits,0,(size_t)raster*raster*4);
        DrawIconEx(mem,0,0,ico,raster,raster,0,nullptr,DI_NORMAL);
        uint8_t* px=(uint8_t*)bits;
        bool anyA=false; for(int i=0;i<raster*raster;i++) if(px[i*4+3]){ anyA=true; break; }
        if(!anyA) for(int i=0;i<raster*raster;i++){ uint8_t b=px[i*4],g=px[i*4+1],r=px[i*4+2]; px[i*4+3]=(b|g|r)?255:0; }
        int x0=raster,y0=raster,x1=-1,y1=-1;
        for(int y=0;y<raster;y++) for(int x=0;x<raster;x++)
            if(px[(y*raster+x)*4+3]>8){ if(x<x0)x0=x; if(x>x1)x1=x; if(y<y0)y0=y; if(y>y1)y1=y; }
        if(x1<x0||y1<y0){ x0=y0=0; x1=y1=raster-1; }
        int cw=x1-x0+1, ch=y1-y0+1, cs=std::max(cw,ch);          // keep it square so nothing stretches
        int cx=x0+(cw-cs)/2, cy=y0+(ch-cs)/2;
        cx=std::max(0,std::min(cx,raster-cs)); cy=std::max(0,std::min(cy,raster-cs));
        std::vector<uint8_t> out((size_t)cs*cs*4,0);
        for(int y=0;y<cs;y++) memcpy(&out[(size_t)y*cs*4], px+(((size_t)(cy+y)*raster+cx)*4), (size_t)cs*4);
        tex=MakeTextureBGRA(out.data(),cs,cs);
    }
    SelectObject(mem,old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(nullptr,scr);
    return tex;
}
// The icon Start shows for an app user model id - the one reliable source for a notification's sender.
// Win32 apps (Discord, Flow Launcher, PowerShell...) hand the listener no logo at all; their Start-menu
// shortcut carries the AUMID, and the Apps folder resolves it to the same artwork Start draws.
static ID3D11ShaderResourceView* AumidIconTex(const std::wstring& aumid,int px){
    if(aumid.empty()) return nullptr;
    IShellItem* it=nullptr;
    if(FAILED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder,0,aumid.c_str(),IID_PPV_ARGS(&it))) || !it) return nullptr;
    ID3D11ShaderResourceView* tex=nullptr;
    IShellItemImageFactory* f=nullptr;
    if(SUCCEEDED(it->QueryInterface(IID_PPV_ARGS(&f))) && f){
        HBITMAP hb=nullptr;
        if(SUCCEEDED(f->GetImage(SIZE{px,px},SIIGBF_ICONONLY|SIIGBF_BIGGERSIZEOK,&hb)) && hb){
            BITMAP bm{}; GetObject(hb,sizeof(bm),&bm);
            int w=bm.bmWidth, h=std::abs(bm.bmHeight);
            if(w>0 && h>0 && w<=1024 && h<=1024){
                std::vector<uint8_t> buf((size_t)w*h*4);
                BITMAPINFO bi={}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=w;
                bi.bmiHeader.biHeight=-h; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
                HDC dc=GetDC(nullptr);
                if(GetDIBits(dc,hb,0,(UINT)h,buf.data(),&bi,DIB_RGB_COLORS)==h){
                    bool anyA=false; for(size_t i=0;i<(size_t)w*h;i++) if(buf[i*4+3]){ anyA=true; break; }
                    if(!anyA) for(size_t i=0;i<(size_t)w*h;i++) buf[i*4+3]=255;
                    tex=MakeTextureBGRA(buf.data(),w,h);
                }
                ReleaseDC(nullptr,dc);
            }
            DeleteObject(hb);
        }
        f->Release();
    }
    it->Release();
    return tex;
}
static ID3D11ShaderResourceView* GetAppIconHi(HWND h,const std::wstring& exe){
    // 1. the shell's 256px jumbo list, keyed off the process image
    if(!exe.empty()){
        SHFILEINFOW fi{};
        if(SHGetFileInfoW(exe.c_str(),0,&fi,sizeof(fi),SHGFI_SYSICONINDEX)){
            IImageList* il=nullptr;
            if(SUCCEEDED(SHGetImageList(SHIL_JUMBO,IID_IImageList,(void**)&il)) && il){
                HICON ic=nullptr; il->GetIcon(fi.iIcon,ILD_TRANSPARENT,&ic); il->Release();
                if(ic){ ID3D11ShaderResourceView* t=IconTexTrim(ic,256); DestroyIcon(ic); if(t) return t; }
            }
        }
    }
    // 2. whatever the window itself offers, rasterised generously
    HICON ic=nullptr; SendMessageTimeoutW(h,WM_GETICON,ICON_BIG,0,SMTO_ABORTIFHUNG,120,(PDWORD_PTR)&ic);
    if(!ic) ic=(HICON)GetClassLongPtrW(h,GCLP_HICON);
    if(!ic) ic=LoadIconW(nullptr,IDI_APPLICATION);
    return IconTexTrim(ic,128);
}

struct SwWin { HWND hwnd=nullptr; ID3D11ShaderResourceView* icon=nullptr; std::string title, app; bool minimized=false;
               ID3D11ShaderResourceView* prev=nullptr; int pw=0, ph=0; bool tried=false;
               bool iconTried=false;  // so a window whose icon cannot be resolved is not retried forever
               std::wstring exe; };   // exe = the icon-cache key, resolved once during the enum
static std::vector<SwWin> g_swWins;
static bool  g_swShow=false;          // switcher is up
static int   g_swSel=0;               // highlighted entry
static float g_swAnim=0.0f;           // open/close ease (panel scale + fade)
static float g_swLid=0.0f;            // eyelid sweep, ticked by the render loop (see DrawSwitcher)
// Two switcher LOOKS over one set of plumbing (the LL hook, SwBuild, the thumbnail worker, the
// DComp overlay). Style 1 is the coverflow carousel ported in from Z:\CoverFlowSwitcher - that
// standalone app captured its own windows and ran its own overlay; here it is just another way of
// drawing g_swWins, which is why it costs a draw function rather than a second process.
static float g_swScroll=0.0f;         // smoothed carousel position; GLIDES toward g_swSel
static bool  g_swFpsHud=false;         // --swfps: draw a frame-rate readout in the panel
static RECT  g_swRect={0,0,0,0};
static std::atomic<int> g_swReq{0};   // 1 next, 2 prev, 3 commit, 4 cancel  (set by the hook)
static HWND  g_swWake=nullptr;        // posted to, to wake the idle loop
static HWND  g_ovHwnd=nullptr; static IDXGISwapChain1* g_ovSc=nullptr; static ID3D11RenderTargetView* g_ovRtv=nullptr;
static IDCompositionTarget* g_ovTgt=nullptr; static IDCompositionVisual* g_ovVis=nullptr; static ImGuiContext* g_ctxOv=nullptr;
static HWND g_ovWake=nullptr;               // poke the render thread when the hook wants the overview
static void DrawOverview();                 // fwd (src/modules/overview/Overview.h)
static void OvCaptureTick();                // fwd
static void OvOpen(); static void OvClose();
static HWND  g_swHwnd=nullptr; static IDXGISwapChain1* g_swSc=nullptr; static ID3D11RenderTargetView* g_swRtv=nullptr;
static IDCompositionTarget* g_swTgt=nullptr; static IDCompositionVisual* g_swVis=nullptr; static ImGuiContext* g_ctxSw=nullptr;

// Strip what the UI fonts cannot draw from a window title. The atlas covers Latin/Greek/Cyrillic
// but NOT emoji (thousands of colour glyphs across seven faces), so an emoji comes out as a box or
// a "?". Everything that uses a raw window title goes through here: the switcher, and the bar
// `windowinfo` item, which used to render a Discord title as "@??jeru ???+??? ? - Discord".
static std::wstring CleanTitle(const std::wstring& t){
    std::wstring cl; cl.reserve(t.size());
    for(size_t i=0;i<t.size();i++){
        wchar_t c=t[i];
        if(c>=0xD800 && c<=0xDBFF){ i++; continue; }        // high surrogate + its pair (emoji)
        if(c>=0xDC00 && c<=0xDFFF) continue;                // stray low surrogate
        if(c==0xFE0F || c==0xFE0E || c==0x200D) continue;   // variation selectors / ZWJ
        if(c>=0x2200 && c<=0x25FF) continue;                // maths/box/geometric: not in the atlas
        cl+=c;
    }
    while(!cl.empty() && (cl.back()==L' '||cl.back()==L'\t')) cl.pop_back();
    size_t b=cl.find_first_not_of(L" \t"); if(b==std::wstring::npos) return std::wstring();
    return cl.substr(b);
}
static BOOL CALLBACK SwEnum(HWND h,LPARAM){
    // same alt-tab eligibility test the taskbar uses
    if(!IsWindowVisible(h)||GetWindowTextLengthW(h)==0) return TRUE;
    if(GetWindowLongW(h,GWL_EXSTYLE)&WS_EX_TOOLWINDOW) return TRUE;
    int ck=0; if(SUCCEEDED(DwmGetWindowAttribute(h,DWMWA_CLOAKED,&ck,sizeof(ck)))&&ck) return TRUE;
    HWND root=GetAncestor(h,GA_ROOTOWNER),walk=nullptr,tw=root;
    while(tw!=walk){ walk=tw; tw=GetLastActivePopup(walk); if(IsWindowVisible(tw))break; }
    if(walk!=h) return TRUE;
    { DWORD pid=0; GetWindowThreadProcessId(h,&pid); if(pid==GetCurrentProcessId()) return TRUE; }
    SwWin s; s.hwnd=h; s.minimized=IsIconic(h)!=FALSE;
    { std::wstring t; t.resize(256); int n=GetWindowTextW(h,&t[0],256); t.resize(std::max(0,n));
      // Drop surrogate pairs (emoji): the UI fonts carry no emoji glyphs, so a title like
      // 'A Laugh In A Life "ATL" ✈️' came through as '... ??'. Titles read better without them.
      s.title=W2U8(CleanTitle(t)); }
    { std::wstring full=ProcExe(h); s.exe=full;                      // cache key + icon source
      std::wstring e=full; size_t p=e.find_last_of(L"\\/"); if(p!=std::wstring::npos)e=e.substr(p+1);
      size_t d=e.find_last_of(L'.'); if(d!=std::wstring::npos)e=e.substr(0,d);
      if(!e.empty()) e[0]=towupper(e[0]); s.app=W2U8(e); }
    g_swWins.push_back(std::move(s));
    return TRUE;
}
// Icons are CACHED FOR THE SESSION, keyed by exe. GetAppIconHi is expensive — SHGetFileInfo can
// touch the shell/disk, then a 256x256 raster plus a full alpha-bbox scan plus a texture upload —
// and doing it for every window inline in SwBuild cost ~500ms on the frame the switcher opened.
// That single stalled frame is what made the entrance animation look like it never played.
// Cached entries are owned by the cache and never released, so SwFree must not free them.
static std::unordered_map<std::wstring,ID3D11ShaderResourceView*> g_swIconCache;
static void SwFree(){
    for(auto&s:g_swWins) if(s.prev) s.prev->Release();     // icons belong to g_swIconCache
    g_swWins.clear();
}
static void SwBuild(){
    SwFree();
    EnumWindows(SwEnum,0);                       // EnumWindows walks TOP-DOWN in z-order == MRU
    // only ever take a CACHE HIT here; a miss is left for the worker so the open stays instant
    for(auto&s:g_swWins){
        auto it=g_swIconCache.find(s.exe);
        if(it!=g_swIconCache.end()){ s.icon=it->second; s.iconTried=true; }
    }
}
// ---- preview capture, ON A WORKER THREAD ---------------------------------------------------
// PrintWindow costs tens of milliseconds and it BLOCKS on the target app answering WM_PRINT. Doing
// that on the render thread meant the whole switcher ran at the capture rate — the panel and the
// eyelid animation were being starved by the thumbnails, and a throttle to keep the framerate
// sane capped the previews at ~14fps. Now the render thread only ever reads texture pointers, so
// the animation runs at refresh rate and the previews go as fast as PrintWindow can manage.
// A bonus: a hung app can no longer stall the shell, because nothing waits on it any more.
// ID3D11Device is free-threaded for resource creation, so building textures here is legal; only
// the immediate CONTEXT would need serialising and this never touches it.
static std::mutex g_swMtx;                     // guards g_swWins' preview fields + the list itself
// The dock refreshes every couple of seconds and used to rebuild every icon each time. It now takes
// the same 256px icons the switcher caches, from the same map, under the same lock the switcher's
// worker thread uses - one texture per exe for the whole session, built once.
static ID3D11ShaderResourceView* DockIconHi(HWND h,const std::wstring& exe){
    if(exe.empty()) return nullptr;
    { std::lock_guard<std::mutex> lk(g_swMtx);
      auto it=g_swIconCache.find(exe);
      if(it!=g_swIconCache.end()) return it->second; }     // may be null: the worker found nothing
    ID3D11ShaderResourceView* t=GetAppIconHi(h,exe);         // slow part, outside the lock
    std::lock_guard<std::mutex> lk(g_swMtx);
    auto it=g_swIconCache.find(exe);
    if(it!=g_swIconCache.end()){ if(t) t->Release(); return it->second; }   // the worker beat us to it
    g_swIconCache[exe]=t;
    return t;
}

// ---- dock icons + pin checks, off the render thread ----
// RefreshDock runs twice a second ON the render thread. Three things in it read the disk: the 256px
// jumbo icon of a newly seen app (SHGetFileInfo + the system image list), the icon of a pinned app
// that is not running, and GetFileAttributes on every closed pin, every pass. On this machine C: is a
// failing hard drive and a pin can live on a drive that is asleep or unplugged, so each of those could
// cost hundreds of ms to seconds - stall.txt has RefreshDock at up to 4.9 s. They are done here now,
// and the dock shows the quick WM_GETICON icon (or keeps the pin) until the answer lands.
struct DkJob { int kind; HWND h; std::wstring exe; };   // 0 hi-res running icon, 1 pin icon, 2 pin exists?
static std::mutex g_dkMtx;
static std::condition_variable g_dkCv;
static std::deque<DkJob> g_dkQ;
static std::set<std::wstring> g_dkQueued;               // kind digit + exe, so nothing is queued twice
static std::unordered_map<std::wstring,ID3D11ShaderResourceView*> g_dkPinIcon;   // handed to DockPin.icon once
static std::unordered_map<std::wstring,std::pair<bool,ULONGLONG>> g_dkPinExists; // exe -> {exists, checked at}
static void DkWorker(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);    // SHGetFileInfo / the image list want COM
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    for(;;){
        DkJob j;
        { std::unique_lock<std::mutex> lk(g_dkMtx);
          g_dkCv.wait(lk,[]{ return !g_dkQ.empty(); });
          j=std::move(g_dkQ.front()); g_dkQ.pop_front(); }
        const std::wstring tag=std::to_wstring(j.kind)+j.exe;
        if(j.kind==0){
            ID3D11ShaderResourceView* t=GetAppIconHi(j.h,j.exe);
            std::lock_guard<std::mutex> lk(g_swMtx);
            auto it=g_swIconCache.find(j.exe);
            if(it!=g_swIconCache.end()){ if(t) t->Release(); } else g_swIconCache[j.exe]=t;
        } else if(j.kind==1){
            ID3D11ShaderResourceView* t=LoadExeIcon(j.exe);
            std::lock_guard<std::mutex> lk(g_dkMtx);
            auto& slot=g_dkPinIcon[j.exe]; if(slot) slot->Release(); slot=t;
        } else {
            bool ex=GetFileAttributesW(j.exe.c_str())!=INVALID_FILE_ATTRIBUTES;
            std::lock_guard<std::mutex> lk(g_dkMtx); g_dkPinExists[j.exe]={ex,GetTickCount64()};
        }
        std::lock_guard<std::mutex> lk(g_dkMtx); g_dkQueued.erase(tag);
    }
}
static void DkQueue(int kind,HWND h,const std::wstring& exe){
    static bool started=false;
    std::lock_guard<std::mutex> lk(g_dkMtx);
    if(!started){ started=true; std::thread(DkWorker).detach(); }
    if(!g_dkQueued.insert(std::to_wstring(kind)+exe).second) return;
    g_dkQ.push_back({kind,h,exe}); g_dkCv.notify_one();
}
// The cached hi-res icon, or null while the worker fetches it (or if it found none).
static ID3D11ShaderResourceView* DockIconHiAsync(HWND h,const std::wstring& exe){
    if(exe.empty()) return nullptr;
    { std::lock_guard<std::mutex> lk(g_swMtx);
      auto it=g_swIconCache.find(exe);
      if(it!=g_swIconCache.end()) return it->second; }
    DkQueue(0,h,exe); return nullptr;
}
// A pin's on-disk icon once it has been loaded; ownership passes to the caller.
static ID3D11ShaderResourceView* PinIconAsync(const std::wstring& exe){
    { std::lock_guard<std::mutex> lk(g_dkMtx);
      auto it=g_dkPinIcon.find(exe);
      if(it!=g_dkPinIcon.end()){ auto t=it->second; g_dkPinIcon.erase(it); return t; } }
    DkQueue(1,nullptr,exe); return nullptr;
}
// Whether a pinned exe is still on disk: the last answer (true until the first one lands),
// re-asked in the background every 30 s.
static bool PinExists(const std::wstring& exe){
    bool ex=true, stale=true;
    { std::lock_guard<std::mutex> lk(g_dkMtx);
      auto it=g_dkPinExists.find(exe);
      if(it!=g_dkPinExists.end()){ ex=it->second.first; stale=GetTickCount64()-it->second.second>30000ULL; } }
    if(stale) DkQueue(2,nullptr,exe);
    return ex;
}
static std::atomic<bool> g_swActive{false};     // worker may touch the list
static std::atomic<bool> g_swBusy{false};       // worker is inside the list RIGHT NOW
static void SwWorker(){
    // Below-normal, and it yields between units: PrintWindow is GDI-heavy and texture creation takes
    // the D3D device's internal lock, so at normal priority the worker was stealing frames from the
    // render thread and the entrance animation still stuttered.
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    int rot=0; ULONGLONG lastRot=0;
    while(g_running){
        if(!g_swActive.load()){ Sleep(10); rot=0; continue; }
        g_swBusy.store(true);
        // 1. missing ICONS first — a card with no icon and no thumbnail is an empty box, and this is
        //    the work that used to stall the opening frame when it ran inline in SwBuild.
        { int ii=-1; HWND ih=nullptr; std::wstring iexe;
          { std::lock_guard<std::mutex> lk(g_swMtx);
            for(size_t i=0;i<g_swWins.size();i++) if(!g_swWins[i].icon && !g_swWins[i].iconTried){
                ii=(int)i; ih=g_swWins[i].hwnd; iexe=g_swWins[i].exe; g_swWins[i].iconTried=true; break; } }
          if(ii>=0){
              ID3D11ShaderResourceView* t=GetAppIconHi(ih,iexe);
              { std::lock_guard<std::mutex> lk(g_swMtx);
                if(!iexe.empty()){ auto it=g_swIconCache.find(iexe);          // cache owns it from here
                    if(it!=g_swIconCache.end()){ if(t) t->Release(); t=it->second; }
                    else g_swIconCache[iexe]=t; }
                if(t){ for(auto& s2:g_swWins) if(s2.exe==iexe || s2.hwnd==ih) if(!s2.icon) s2.icon=t; }
                else if(iexe.empty()) { /* nothing to cache and nothing to show; the thumbnail covers it */ } }
              g_swBusy.store(false);
              Sleep(5);                       // yield: never starve the render thread
              continue;                       // one unit of work per pass, so the list stays responsive
          } }
        int idx=-1; HWND hw=nullptr; bool mini=false;
        { std::lock_guard<std::mutex> lk(g_swMtx);
          int n=(int)g_swWins.size();
          if(n>0){
              for(int i=0;i<n;i++) if(!g_swWins[i].tried){ idx=i; break; }   // fill empty cards first
              if(idx<0){
                  int sel=std::clamp(g_swSel,0,n-1);
                  ULONGLONG now=GetTickCount64();
                  // the selection is refreshed as fast as we can; the rest tick over slowly
                  if(n>1 && now-lastRot>=220){ lastRot=now; rot=(rot+1)%n; idx=(rot==sel)?(rot+1)%n:rot; }
                  else idx=sel;
              }
              if(idx>=0){ hw=g_swWins[idx].hwnd; mini=g_swWins[idx].minimized; g_swWins[idx].tried=true; }
          } }
        if(hw && !mini){
            int w=0,h=0; ID3D11ShaderResourceView* t=CaptureWindowTex(hw,w,h,340);
            if(t){
                ID3D11ShaderResourceView* old=nullptr;
                { std::lock_guard<std::mutex> lk(g_swMtx);
                  if(idx<(int)g_swWins.size() && g_swWins[idx].hwnd==hw){       // list may have been rebuilt
                      old=g_swWins[idx].prev; g_swWins[idx].prev=t; g_swWins[idx].pw=w; g_swWins[idx].ph=h; t=nullptr; } }
                if(t)   t->Release();
                if(old) old->Release();     // safe: D3D11 keeps resources alive for queued frames
            }
        }
        g_swBusy.store(false);
        Sleep(hw? 5 : 8);
    }
}
// Draw: one slab, icons in a row (wrapping to more rows when there are a lot), title underneath.

// ---------------------------------------------------------------- coverflow switcher
// Ported from the standalone CoverFlowSwitcher: side cards rotate away about the vertical axis and
// are projected through a simple pinhole (persp = FOCAL/(FOCAL+z)), so they foreshorten instead of
// merely shearing. Drawn far-to-near so the centred card lands on top.
static void DrawSwitcherCover(ImDrawList* dl, ImGuiIO& io, float W, float H, float a){
    int n=(int)g_swWins.size();
    float cx=W*0.5f, cy=H*0.46f;
    const float cardH  = H*0.40f;
    const float ROT    = 1.05f;          // ~60 deg for a fully turned-away card
    const float FOCAL  = cardH*1.6f;
    const float VISIBLE= 3.2f;          // 7 cards on screen; past that they are edge-on anyway
    float gScale=0.86f+0.14f*a, gYoff=(1.0f-a)*40.0f;

    auto xf=[&](ImVec2 pt,float scl,float yo){ return V(cx+(pt.x-cx)*scl, cy+(pt.y-cy)*scl+yo); };

    std::vector<int> order;
    for(int i=0;i<n;i++){ float o=i-g_swScroll; if(fabsf(o)<=VISIBLE) order.push_back(i); }
    std::sort(order.begin(),order.end(),[](int x,int y){
        return fabsf(x-g_swScroll) > fabsf(y-g_swScroll); });

    int hoverIdx=-1;
    for(int idx:order){
        SwWin& c=g_swWins[idx];
        float off=idx-g_swScroll;
        // a window with no thumbnail yet still gets a card, sized to the screen, so the carousel
        // never reflows as captures land
        float aspect = (c.pw>0&&c.ph>0)? (float)c.pw/(float)c.ph : (W>0&&H>0? W/H : 1.6f);
        aspect=std::clamp(aspect,0.4f,3.0f);
        float cardW=cardH*aspect, halfW=cardW*0.5f, halfH=cardH*0.5f;

        float clamped=std::clamp(off,-1.0f,1.0f);
        float angle=-clamped*ROT;
        float sgn = off>0? 1.0f : -1.0f;
        float ax  = std::min(fabsf(off),1.0f);
        float baseGap=cardW*0.62f;
        float ccx = cx + sgn*(ax*baseGap + std::max(fabsf(off)-1.0f,0.0f)*cardW*0.42f);

        float ca=cosf(angle), sa=sinf(angle);
        auto proj=[&](float lx,float ly){
            float xr=lx*ca, zr=lx*sa;
            float persp=FOCAL/(FOCAL+zr);
            return V(ccx+xr*persp, cy+ly*persp); };

        ImVec2 TL=xf(proj(-halfW,-halfH),gScale,gYoff), TR=xf(proj(halfW,-halfH),gScale,gYoff),
               BR=xf(proj( halfW, halfH),gScale,gYoff), BL=xf(proj(-halfW, halfH),gScale,gYoff);

        float bright=1.0f-std::min(fabsf(off),3.0f)*0.16f;
        int v=(int)(bright*255), al2=(int)(255*a);
        ImU32 col=IM_COL32(v,v,v,al2);

        // The shadow belongs to a CARD. Drawing it under a not-yet-captured slot painted a big
        // black trapezoid in the carousel - that is what the empty side cards looked like.
        if(c.prev){
            float shy=cardH*0.03f;
            dl->AddQuadFilled(V(TL.x,TL.y+shy),V(TR.x,TR.y+shy),V(BR.x,BR.y+shy),V(BL.x,BL.y+shy),
                              IM_COL32(0,0,0,(int)(90*a)));
        }
        if(c.prev){
            dl->AddImageQuad((ImTextureID)c.prev,TL,TR,BR,BL,
                             ImVec2(0,0),ImVec2(1,0),ImVec2(1,1),ImVec2(0,1),col);
            if(g_swReflect){
                ImVec2 RTL=V(BL.x,BL.y+(BL.y-TL.y)*0.5f), RTR=V(BR.x,BR.y+(BR.y-TR.y)*0.5f);
                dl->AddImageQuad((ImTextureID)c.prev,BL,BR,RTR,RTL,
                                 ImVec2(0,1),ImVec2(1,1),ImVec2(1,0.5f),ImVec2(0,0.5f),
                                 IM_COL32(v,v,v,(int)(55*a)));
            }
        } else {
            // Not captured yet (the worker does one window per frame). A faint outline plus the app
            // icon holds the slot without shouting; a solid fill reads as a black hole in the
            // carousel, which is exactly how it looked before.
            int ga=(int)(a*(fabsf(off)<0.5f? 190:110));
            dl->AddQuadFilled(TL,TR,BR,BL,IM_COL32(24,24,32,(int)(ga*0.45f)));
            dl->AddQuad(TL,TR,BR,BL,IM_COL32(255,255,255,(int)(ga*0.30f)),1.4f);
            if(c.icon){ ImVec2 mid=V((TL.x+BR.x)*0.5f,(TL.y+BR.y)*0.5f); float is=cardH*0.26f;
                int ia=std::min(255,(int)(ga*1.1f));
                dl->AddImage((ImTextureID)c.icon,V(mid.x-is*0.5f,mid.y-is*0.5f),V(mid.x+is*0.5f,mid.y+is*0.5f),
                             ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,ia)); }
        }
        // app icon badged on the corner of the centred card, like the row style does
        if(fabsf(off)<0.5f && c.icon && c.prev){
            float is=cardH*0.16f; ImVec2 bc=V(BL.x+is*0.55f, BL.y-is*0.55f);
            dl->AddCircleFilled(bc,is*0.62f,IM_COL32(18,18,24,(int)(215*a)));
            dl->AddImage((ImTextureID)c.icon,V(bc.x-is*0.42f,bc.y-is*0.42f),V(bc.x+is*0.42f,bc.y+is*0.42f),
                         ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,al2));
        }
        if(fabsf(off)<0.5f) dl->AddQuad(TL,TR,BR,BL,WithA(COL_GOLD,(int)(220*a)),2.5f);
        // hover test on the card's bounding box (a quad hit test buys nothing at this size)
        { float l=std::min(std::min(TL.x,TR.x),std::min(BL.x,BR.x)), r=std::max(std::max(TL.x,TR.x),std::max(BL.x,BR.x));
          float t=std::min(std::min(TL.y,TR.y),std::min(BL.y,BR.y)), b=std::max(std::max(TL.y,TR.y),std::max(BL.y,BR.y));
          if(io.MousePos.x>l&&io.MousePos.x<r&&io.MousePos.y>t&&io.MousePos.y<b) hoverIdx=idx; }
    }

    // title + position dots, under the carousel
    if(a>0.01f && n>0){
        int cur=std::clamp((int)lroundf(g_swScroll),0,n-1);
        const std::string& t=g_swWins[cur].title;
        std::string show = t.empty()? g_swWins[cur].app : t;
        float fs=22.0f; float tw=TextW(g_fMed,fs,show.c_str());
        float ty=cy+cardH*0.62f+gYoff;
        dl->AddRectFilled(V(cx-tw*0.5f-14,ty-6),V(cx+tw*0.5f+14,ty+fs+6),IM_COL32(0,0,0,(int)(150*a)),8);
        TextAt(dl,g_fMed,fs,V(cx-tw*0.5f,ty),IM_COL32(235,240,238,(int)(255*a)),show.c_str());
        float dotY=ty+fs+22.0f, dw=14.0f, totw=dw*n;
        for(int i=0;i<n;i++){
            ImVec2 dc=V(cx-totw*0.5f+dw*i+dw*0.5f,dotY);
            bool on=(i==cur);
            dl->AddCircleFilled(dc,on?4.5f:3.0f,
                on? WithA(COL_GOLD,(int)(255*a)) : IM_COL32(160,160,160,(int)(140*a)));
        }
    }
    if(io.MouseClicked[0]){ if(hoverIdx>=0){ g_swSel=hoverIdx; g_swReq=3; } else g_swReq=4; }
    else if(hoverIdx>=0 && io.MouseDelta.x*io.MouseDelta.x+io.MouseDelta.y*io.MouseDelta.y>0.5f) g_swSel=hoverIdx;
}
static void DrawSwitcher(){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x, H=io.DisplaySize.y;
    float a=std::clamp(g_swAnim,0.0f,1.0f);
    // held for the whole body: the capture worker swaps preview textures under us
    std::lock_guard<std::mutex> swlk(g_swMtx);
    if(a<0.004f || g_swWins.empty()){ g_swRect=RECT{0,0,0,0}; return; }
    int n=(int)g_swWins.size();
    if(g_swSel<0) g_swSel=0; if(g_swSel>=n) g_swSel=n-1;
    bool cover = (g_swStyle==SWSTYLE_COVER);

    // Cards, not bare icons: each entry is a live thumbnail of the window with its app icon badged
    // on it, which is how KDE's and Hyprland's switchers read. Cards shrink to fit, then wrap.
    const float PAD=26.0f, GAP=14.0f, LABEL=62.0f, ASPECT=0.60f;
    float maxW=W*0.88f, maxH=H*0.80f;
    float cell=300.0f;
    while(cell>170.0f && n*(cell+GAP)-GAP > maxW-PAD*2) cell-=6.0f;
    int perRow=std::max(1,(int)((maxW-PAD*2+GAP)/(cell+GAP)));
    int rows=(n+perRow-1)/perRow;
    float cellH=cell*ASPECT;
    // a lot of windows: shrink again so the wrapped grid still fits vertically
    while(rows>1 && cell>120.0f && rows*(cellH+GAP)-GAP+PAD*2+LABEL > maxH){
        cell-=8.0f; cellH=cell*ASPECT;
        perRow=std::max(1,(int)((maxW-PAD*2+GAP)/(cell+GAP)));
        rows=(n+perRow-1)/perRow;
    }
    int cols=std::min(n,perRow);
    float panelW=cols*(cell+GAP)-GAP+PAD*2;
    float panelH=rows*(cellH+GAP)-GAP+PAD*2+LABEL;
    float px=(W-panelW)*0.5f, py=(H-panelH)*0.5f;

    // ---- "eyelids": the dim sweeps in from the top and bottom edges and meets in the middle, so
    // opening the switcher reads like closing your eyes on the desktop (and re-opens on the way out).
    // Each lid carries a soft gradient on its leading edge so it reads as a lid, not a hard band.
    // g_swLid is advanced by the RENDER LOOP, not here. Cael::anim snaps to its target on the very
    // first call, and this function only runs once the switcher is already showing — so driving the
    // animator from inside the draw meant the lids were fully shut on frame one and the blink never
    // played. Anything animating an overlay's ENTRANCE has to be ticked from the loop.
    { float p=std::clamp(g_swLid,0.0f,1.0f);
      float lid=H*0.5f*p, soft=std::min(90.0f, H*0.16f);
      int base=(int)(64*a), deep=(int)(186*std::max(a,p));
      ImU32 CB=IM_COL32(4,4,8,base), CD=IM_COL32(4,4,8,deep), C0=IM_COL32(4,4,8,0);
      dl->AddRectFilled(V(0,0),V(W,H),CB);                                   // gentle overall dim
      if(lid>1.0f){
          float t1=std::max(0.0f,lid-soft);
          dl->AddRectFilled(V(0,0),V(W,t1),CD);                              // top lid, solid part
          dl->AddRectFilledMultiColor(V(0,t1),V(W,lid),CD,CD,C0,C0);         // its soft edge
          float b1=std::min(H,H-lid+soft);
          dl->AddRectFilled(V(0,b1),V(W,H),CD);                              // bottom lid
          dl->AddRectFilledMultiColor(V(0,H-lid),V(W,b1),C0,C0,CD,CD);
      } }

    // The coverflow style shares the eyelids and the hit rect but nothing below them.
    if(cover){ g_swRect=RECT{0,0,(LONG)W,(LONG)H}; DrawSwitcherCover(dl,io,W,H,a); return; }
    float sc=0.94f+0.06f*EaseOutBack(a);
    ImVec2 p0=V(W*0.5f-(W*0.5f-px)*sc, H*0.5f-(H*0.5f-py)*sc);
    ImVec2 p1=V(W*0.5f+(px+panelW-W*0.5f)*sc, H*0.5f+(py+panelH-H*0.5f)*sc);
    GlassPanel(dl,p0,p1,28.0f,0,g_deskFrost,a);

    auto cellPos=[&](int i){
        int r=i/perRow, c=i%perRow;
        int inRow = std::min(n-r*perRow, perRow);
        float rowW = inRow*(cell+GAP)-GAP;
        float sx = (p0.x+p1.x)*0.5f - rowW*0.5f;
        return V(sx+c*(cell+GAP), p0.y+PAD+r*(cellH+GAP));
    };
    const float CR=14.0f;                                   // card corner radius
    // selection plate glides between entries on the spatial spring, drawn UNDER the cards
    { ImVec2 t=cellPos(g_swSel);
      float sx=Cael::anim(823001,t.x,Cael::DUR_FAST_SPATIAL,Cael::FAST_SPATIAL);
      float sy=Cael::anim(823002,t.y,Cael::DUR_FAST_SPATIAL,Cael::FAST_SPATIAL);
      float e=7.0f;
      dl->AddRectFilled(V(sx-e,sy-e),V(sx+cell+e,sy+cellH+e),
                        MulA(WithA(IM_COL32(255,255,255,255),g_darkUI?34:58),a),CR+e);
      dl->AddRect(V(sx-e,sy-e),V(sx+cell+e,sy+cellH+e),MulA(AccA(170),a),CR+e,0,1.8f); }

    bool click=io.MouseClicked[0];
    int hoverIdx=-1;
    for(int i=0;i<n;i++){
        SwWin& s=g_swWins[i];
        ImVec2 c=cellPos(i);
        ImVec2 c1=V(c.x+cell,c.y+cellH);
        bool hov=io.MousePos.x>c.x&&io.MousePos.x<c1.x&&io.MousePos.y>c.y&&io.MousePos.y<c1.y;
        if(hov) hoverIdx=i;
        int A8=(int)(255*a);
        // the well the thumbnail sits in
        dl->AddRectFilled(c,c1, MulA(g_darkUI?IM_COL32(12,13,18,235):IM_COL32(226,232,236,235),a), CR);
        if(s.prev && s.pw>0 && s.ph>0){
            // CONTAIN-fit: show the whole window letterboxed, the way Linux switchers do — a
            // cover-fit crop mangles tall windows.
            float sc2=std::min(cell/(float)s.pw, cellH/(float)s.ph);
            float w2=s.pw*sc2, h2=s.ph*sc2;
            ImVec2 a0=V(c.x+(cell-w2)*0.5f, c.y+(cellH-h2)*0.5f);
            dl->PushClipRect(c,c1,true);
            dl->AddImageRounded((ImTextureID)s.prev,a0,V(a0.x+w2,a0.y+h2),ImVec2(0,0),ImVec2(1,1),
                                IM_COL32(255,255,255,A8), (w2>=cell-1&&h2>=cellH-1)? CR : CR*0.5f);
            dl->PopClipRect();
        } else {
            // no capture yet (or minimised): the app icon fills the card instead
            float ic=std::min(cell,cellH)*0.46f;
            ImVec2 ip=V(c.x+(cell-ic)*0.5f, c.y+(cellH-ic)*0.5f);
            if(s.icon) dl->AddImage((ImTextureID)s.icon,ip,V(ip.x+ic,ip.y+ic),ImVec2(0,0),ImVec2(1,1),
                                    IM_COL32(255,255,255,(int)(A8*(s.minimized?0.62f:0.9f))));
        }
        // app icon badge, bottom-left, on its own soft plate so it reads over any thumbnail
        if(s.icon && s.prev){
            float bs=std::max(30.0f,cell*0.16f);
            ImVec2 b0=V(c.x+8, c1.y-bs-8);
            dl->AddRectFilled(b0,V(b0.x+bs,b0.y+bs), MulA(IM_COL32(10,11,15,170),a), bs*0.28f);
            dl->AddImage((ImTextureID)s.icon,V(b0.x+3,b0.y+3),V(b0.x+bs-3,b0.y+bs-3),
                         ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,A8));
        }
        dl->AddRect(c,c1, MulA(g_darkUI?IM_COL32(255,255,255,26):IM_COL32(0,0,0,34),a), CR,0,1.0f);
        // minimised: say so, since there is no live thumbnail to show
        if(s.minimized){
            const char* m="Minimised";
            float tw2=TextW(g_fSml,12,m);
            ImVec2 t0=V(c.x+cell*0.5f-tw2*0.5f-8, c1.y-26);
            dl->AddRectFilled(t0,V(t0.x+tw2+16,t0.y+19), MulA(IM_COL32(10,11,15,150),a), 9.0f);
            TextAt(dl,g_fSml,12,V(t0.x+8,t0.y+2),MulA(WithA(COL_INK,200),a),m);
        }
    }
    if(hoverIdx>=0) g_swSel=hoverIdx;
    if(click&&hoverIdx>=0) g_swReq=3;                 // click an icon = commit to it

    // title of the selection, centred under the grid
    { const SwWin& s=g_swWins[g_swSel];
      std::string t = s.title.empty()? s.app : s.title;
      float tw=panelW-PAD*2;
      std::string ct=Clip(g_fMed,17,t,tw);
      TextAt(dl,g_fMed,18,V((p0.x+p1.x)*0.5f-TextW(g_fMed,18,ct.c_str())*0.5f, p1.y-LABEL+10.0f),
             MulA(COL_INK,a),ct.c_str());
      if(!s.app.empty() && !s.title.empty()){
          std::string sub=Clip(g_fSml,14,s.app,tw);
          TextAt(dl,g_fSml,14,V((p0.x+p1.x)*0.5f-TextW(g_fSml,14,sub.c_str())*0.5f, p1.y-LABEL+34.0f),
                 MulA(WithA(COL_INK2,215),a),sub.c_str()); } }

    // --swfps: a rolling frame-rate readout, so "is the switcher smooth" is answerable with a
    // screenshot instead of a guess. The animation timings only read as animation at refresh rate.
    if(g_swFpsHud){
        static float avg=0.0f; if(g_frameDt>0.0001f) avg = avg*0.9f + (1.0f/g_frameDt)*0.1f;
        char fb[64]; snprintf(fb,64,"%.0f fps  |  lid %.2f  anim %.2f",avg,g_swLid,g_swAnim);
        TextAt(dl,g_fSml,14,V(p0.x+14,p0.y+8),IM_COL32(120,255,140,235),fb);
    }
    // Claim the WHOLE screen, not just the panel: ApplyHitRegion clips what is VISIBLE as well as
    // what is clickable, so a panel-sized region silently threw away the dim scrim. Owning the
    // screen also gives us click-outside-to-cancel, which every switcher does.
    if(click && hoverIdx<0 &&
       (io.MousePos.x<p0.x||io.MousePos.x>p1.x||io.MousePos.y<p0.y||io.MousePos.y>p1.y)) g_swReq=4;
    g_swRect = g_swShow? RECT{0,0,(LONG)W,(LONG)H} : RECT{0,0,0,0};
}

// is the foreground window part of this app group?
static bool DockGroupHasFg(const DockApp& a){ HWND fg=GetForegroundWindow(), fr=GetAncestor(fg,GA_ROOTOWNER);
    for(HWND w:a.wins) if(w==fg||w==fr) return true; return false; }
// left-click a taskbar app: 1 window -> toggle; several -> cycle through them, then minimise
static void DockGroupClick(const DockApp& a){
    HWND fg=GetForegroundWindow(), fr=GetAncestor(fg,GA_ROOTOWNER);
    // A pin whose app is closed is a LAUNCHER: it has no window to activate, and the old code fell
    // through to ActivateWindow(nullptr), so pinned icons simply did nothing when clicked.
    if(a.wins.empty() && !a.hwnd){
        if(!a.exe.empty()){
            std::wstring dir=a.exe; size_t sl=dir.find_last_of(L"\\/");
            dir = (sl==std::wstring::npos)? std::wstring() : dir.substr(0,sl);
            AetherShellExec(nullptr,L"open",a.exe.c_str(),nullptr,dir.empty()?nullptr:dir.c_str(),SW_SHOWNORMAL);
        }
        return;
    }
    if(a.wins.size()<=1){ HWND h=a.wins.empty()?a.hwnd:a.wins[0];
        if(h==fg){ ShowWindowAsync(h,SW_MINIMIZE); } else ActivateWindow(h); return; }
    int cur=-1; for(size_t i=0;i<a.wins.size();i++) if(a.wins[i]==fg||a.wins[i]==fr){ cur=(int)i; break; }
    HWND nxt = (cur>=0)? a.wins[(cur+1)%a.wins.size()] : a.wins[0];
    ActivateWindow(nxt);
}
// ---- adopt the windows that were already open ---------------------------------------------------
// komorebi only manages windows it saw appear. Anything already open when it starts is never picked
// up, is therefore never hidden on a workspace switch, and so appears on EVERY workspace - which is
// what "some apps show up on every workspace" actually is.
//
// `komorebic manage` is the only thing that works, and it acts on the FOCUSED window, so each one
// has to be focused in turn. Two gentler routes were tried and rejected: `retile` does nothing for
// unmanaged windows, and a DWM cloak/uncloak pulse (which would have needed no focus change at all)
// is ignored by komorebi's event handling.
//
// So this flashes through the windows and puts focus back where it was. It runs by itself once per
// komorebi run (see the session gate in RefreshWorkspaces) and skips everything komorebi already
// holds, so in the normal case it touches nothing and flashes nothing; the Settings button stays for
// re-running it by hand. "Adopt already-open windows" turns the automatic half off.
static std::atomic<bool> g_adoptBusy{false};
static int g_adoptDone=0;                 // how many the last sweep managed, for the settings label

static void KomoAdoptExisting(){
    if(!g_komoLive.load()) return;
    if(g_adoptBusy.exchange(true)) return;
    std::thread([]{
        // Everything komorebi already holds. Each window this sweep touches gets focused for a
        // moment, so skipping the managed ones is the whole difference between something that can
        // run automatically and something too rude to fire on its own.
        std::vector<HWND> known;
        { std::lock_guard<std::mutex> lk(g_komoMtx);
          for(const KomoMon& m:g_komo)
              for(const KomoWs& w:m.ws)
                  for(HWND h:w.hwnds) known.push_back(h); }

        struct Ctx { std::vector<HWND> v; };
        Ctx c;
        EnumWindows([](HWND h,LPARAM lp)->BOOL{
            Ctx* c=(Ctx*)lp;
            if(!IsWindowVisible(h) || !GetWindowTextLengthW(h)) return TRUE;
            if(GetWindow(h,GW_OWNER)) return TRUE;                       // owned dialogs
            if(GetWindowLongW(h,GWL_EXSTYLE)&WS_EX_TOOLWINDOW) return TRUE;
            DWORD pid=0; GetWindowThreadProcessId(h,&pid);
            if(pid==GetCurrentProcessId()) return TRUE;                  // never our own overlays
            wchar_t cls[128]={0}; GetClassNameW(h,cls,128);
            static const wchar_t* SKIP[]={ L"Shell_TrayWnd",L"Shell_SecondaryTrayWnd",L"Progman",
                L"WorkerW",L"Windows.UI.Core.CoreWindow",L"ApplicationFrameWindow",nullptr };
            for(int i=0;SKIP[i];i++) if(!wcscmp(cls,SKIP[i])) return TRUE;
            c->v.push_back(h); return TRUE;
        },(LPARAM)&c);

        for(auto it=c.v.begin(); it!=c.v.end(); )
            it = (std::find(known.begin(),known.end(),*it)!=known.end()) ? c.v.erase(it) : it+1;
        if(c.v.empty()){ g_adoptDone=0; g_adoptBusy.store(false); return; }

        HWND before=GetForegroundWindow();
        int done=0;
        for(HWND h:c.v){
            if(!IsWindow(h)) continue;
            ActivateWindow(h);
            Sleep(220);
            if(GetForegroundWindow()!=h) continue;     // could not focus it: leave it alone
            KomoRun(L"manage", nullptr);
            Sleep(140);
            done++;
        }
        if(before && IsWindow(before)) ActivateWindow(before);
        g_adoptDone=done;
        g_adoptBusy.store(false);
    }).detach();
}

// ---- taskbar app right-click menu (custom, reliable — TrackPopupMenu from a NOACTIVATE overlay is
//      flaky). Lists the app's windows; each activates, plus New window? / Close all. ----
// ---- dismissing an overlay menu ---------------------------------------------------------------
// THE bug behind "the popup won't disappear": these menus are drawn by WS_EX_NOACTIVATE overlay
// windows whose hit region covers only what they paint. Click anywhere else - another app, the
// desktop, a different monitor - and the click is delivered to THAT window. ImGui here never sees a
// MouseClicked at all, so every "click outside closes it" test silently never fires and the menu
// sits on screen until something else happens to close it.
//
// Reading the physical buttons and the real cursor works no matter who received the click. Each
// menu keeps its own state: `armed` waits for the opening right-click to be released (it is still
// down for the first frames), and `wasDown` makes it edge-triggered per call site, so two menus
// polling in the same frame cannot eat each other's edge.
struct MenuDismiss { bool armed=false, wasDown=false; unsigned long long stamp=0; };
// `cursorInside` MUST be computed by the caller from io.MousePos - i.e. in the same space the menu
// rect is in.
//
// The first version worked this out here, from GetCursorPos, converted with g_vs. That is
// VIRTUAL-SCREEN space, while a menu rect is in its own WINDOW's space; with a second monitor to the
// left of the primary the two differ by its width, so every click - including a click on one of the
// menu's own rows - measured as "outside". And because GetAsyncKeyState sees the button go down a
// frame before ImGui delivers MouseClicked, the menu closed one frame BEFORE the row handler ran.
// Net effect: the menu appeared, and nothing in it could ever be clicked.
static bool MenuOutsideClick(MenuDismiss& st, bool cursorInside, unsigned long long openedAt=0){
    // a fresh open re-arms: otherwise the still-held right-click that opened the menu is itself the
    // outside click that closes it again on the same frame
    if(openedAt && st.stamp!=openedAt){ st.stamp=openedAt; st.armed=false; st.wasDown=true; }
    bool phys=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0 || (GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;
    if(!phys) st.armed=true;
    bool fired = st.armed && phys && !st.wasDown && !cursorInside;
    st.wasDown=phys;
    return fired;
}

static bool g_appMenu=false; static int g_appMenuMon=-1; static ImVec2 g_appMenuAt=V(0,0); static float g_appMenuAnim=0.0f;
static bool g_appMenuJustOpened=false;   // suppress the same-frame outside-click that would instantly close it
static std::vector<HWND> g_appMenuWins; static std::vector<std::string> g_appMenuTitles;
static ID3D11ShaderResourceView* g_appMenuIcon=nullptr; static RECT g_appMenuRect={0,0,0,0}; static int g_appMenuSide=1;
// Which way the menu grows from its anchor. 0 = downward (a top bar, or beside a vertical bar);
// -1 = UPWARD, so on a bottom bar the menu sits ABOVE the strip instead of being drawn over it and
// then shoved to some arbitrary spot by the bottom-of-screen clamp.
static int g_appMenuVDir=0;
static std::wstring g_appMenuExe; static bool g_appMenuPinned=false;
static MenuDismiss g_appMenuDis;
static void OpenAppMenu(const DockApp& a,int mon,ImVec2 at,int side,int vdir=0){
    g_appMenu=true; g_appMenuJustOpened=true; g_appMenuMon=mon; g_appMenuAt=at; g_appMenuSide=side;
    g_appMenuVDir=vdir; g_appMenuIcon=a.icon; g_appMenuDis=MenuDismiss{};
    g_appMenuExe=a.exe;
    g_appMenuPinned=false;
    for(auto& p:g_dockPins) if(!a.exe.empty() && _wcsicmp(p.exe.c_str(),a.exe.c_str())==0){ g_appMenuPinned=true; break; }
    g_appMenuWins.clear(); g_appMenuTitles.clear();
    for(HWND w:a.wins){ g_appMenuWins.push_back(w);
        wchar_t t[256]={0}; GetWindowTextW(w,t,255); std::string s=W2U8(t); if(s.empty())s="(untitled)";
        g_appMenuTitles.push_back(s); }
}
static void DrawAppMenu(ImDrawList* dl,ImGuiIO& io){
    float mt=g_appMenu?1.0f:0.0f;
    g_appMenuAnim = MotionAnim(MP_POPOUT,810002, mt);   // popout spring
    if(g_appMenuAnim<0.004f && mt==0.0f){ g_appMenuRect=RECT{0,0,0,0}; return; }
    bool click=io.MouseClicked[0], rclick=io.MouseClicked[1];
    float a=g_appMenuAnim;   // already the springy expressiveDefaultSpatial value (overshoots slightly)
    float af=std::clamp(g_appMenuAnim,0.0f,1.0f); int al=(int)(af*255);
    int nw=(int)g_appMenuWins.size();
    bool canPin=!g_appMenuExe.empty();
    // rows: the window list, then (Pin), Close, End task
    float mw=248, rowH=30, headH=nw>1?24.0f:0.0f;
    float mh=headH+rowH*nw+rowH*(canPin?3.0f:2.0f)+14;
    float mx0=g_appMenuAt.x - (g_appMenuSide<0? mw:0) - (1.0f-a)*10.0f*g_appMenuSide;
    // grow up or down from the anchor, and let the entrance slide along that same axis
    float my0=(g_appMenuVDir<0)? (g_appMenuAt.y-mh) : g_appMenuAt.y;
    if(g_appMenuVDir!=0) my0 -= (1.0f-a)*10.0f*(float)g_appMenuVDir;
    // Keep it on screen. This clamps in the BAR WINDOW's coordinate space, which is the space `at`
    // arrived in - an earlier version clamped against MonRect, which is in SCREEN space, and mixing
    // the two silently shoved the menu onto the neighbouring monitor where it could never be seen.
    { float L=6.0f, R=io.DisplaySize.x-6.0f, T=6.0f, B=io.DisplaySize.y-6.0f;
      if(R-L>mw) mx0=std::clamp(mx0,L,R-mw);
      if(B-T>mh) my0=std::clamp(my0,T,B-mh); }
    ImVec2 m0=V(mx0,my0),m1=V(mx0+mw,my0+mh);
    GlassPanel(dl,m0,m1,12,0,nullptr,af);
    float ry=m0.y+8;
    if(headH>0){ char h[48]; snprintf(h,48,"%d windows",nw);
        TextAt(dl,g_fSml,12,V(m0.x+16,ry),WithA(COL_INK2,al),h); ry+=headH; }
    for(int i=0;i<nw;i++){
        bool rh=io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>ry&&io.MousePos.y<ry+rowH;
        if(rh) dl->AddRectFilled(V(m0.x+5,ry+1),V(m1.x-5,ry+rowH-1),AccA((int)(af*26)),8);
        bool fgw=(g_appMenuWins[i]==GetForegroundWindow());
        if(fgw) dl->AddCircleFilled(V(m0.x+16,ry+rowH*0.5f),3,AccA(al));
        TextAt(dl,g_fSml,14,V(m0.x+28,ry+7),WithA(COL_INK,al),Clip(g_fSml,14,g_appMenuTitles[i],mw-44).c_str());
        if(click&&rh){ ActivateWindow(g_appMenuWins[i]); g_appMenu=false; }
        ry+=rowH;
    }
    dl->AddLine(V(m0.x+10,ry+2),V(m1.x-10,ry+2),WithA(COL_INK2,(int)(al*0.3f)),1); ry+=6;
    // Pin / unpin - the dock has had this since it was written; the taskbar never exposed it, which
    // is why there was no way to pin anything from the strip people actually use.
    if(canPin){
        bool rh=io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>ry&&io.MousePos.y<ry+rowH;
        if(rh) dl->AddRectFilled(V(m0.x+5,ry+1),V(m1.x-5,ry+rowH-1),AccA((int)(af*30)),8);
        // a pin glyph, same drawing as the tray's promote badge
        { ImVec2 pc=V(m0.x+16,ry+rowH*0.5f); ImU32 pk=WithA(g_appMenuPinned?COL_GOLD:COL_INK2,al);
          dl->AddLine(V(pc.x,pc.y+4.0f),V(pc.x,pc.y-1.0f),pk,1.8f);
          dl->AddLine(V(pc.x-3.6f,pc.y-1.6f),V(pc.x+3.6f,pc.y-1.6f),pk,1.8f);
          dl->AddLine(V(pc.x-2.2f,pc.y-4.4f),V(pc.x+2.2f,pc.y-4.4f),pk,1.8f); }
        TextAt(dl,g_fSml,14,V(m0.x+28,ry+7),WithA(COL_INK,al),
               g_appMenuPinned?"Unpin from taskbar":"Pin to taskbar");
        if(click&&rh){
            if(g_appMenuPinned){
                for(size_t k=0;k<g_dockPins.size();k++)
                    if(_wcsicmp(g_dockPins[k].exe.c_str(),g_appMenuExe.c_str())==0){
                        if(g_dockPins[k].icon) g_dockPins[k].icon->Release();
                        g_dockPins.erase(g_dockPins.begin()+k); break; }
            } else {
                DockPin p; p.exe=g_appMenuExe; p.icon=LoadExeIcon(g_appMenuExe);
                g_dockPins.push_back(std::move(p));
            }
            SaveConfig(); RefreshDock(); g_appMenu=false;
        }
        ry+=rowH;
    }
    // Close — graceful WM_CLOSE (lets the app save/prompt)
    { bool rh=io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>ry&&io.MousePos.y<ry+rowH;
      if(rh) dl->AddRectFilled(V(m0.x+5,ry+1),V(m1.x-5,ry+rowH-1),WithA(COL_INK2,(int)(af*36)),8);
      TextAt(dl,g_fSml,14,V(m0.x+28,ry+7),WithA(COL_INK,al), nw>1?"Close all windows":"Close window");
      if(click&&rh){ for(HWND w:g_appMenuWins) PostMessageW(w,WM_CLOSE,0,0); g_appMenu=false; }
      ry+=rowH; }
    // End task — force-terminate the process(es), like the Windows taskbar
    { bool rh=io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>ry&&io.MousePos.y<ry+rowH;
      if(rh) dl->AddRectFilled(V(m0.x+5,ry+1),V(m1.x-5,ry+rowH-1),WithA(IM_COL32(0xe0,0x1b,0x24,255),(int)(af*34)),8);
      TextAt(dl,g_fSml,14,V(m0.x+28,ry+7),WithA(IM_COL32(0xe8,0x50,0x50,255),al),"End task");
      if(click&&rh){ for(HWND w:g_appMenuWins){ DWORD pid=0; GetWindowThreadProcessId(w,&pid);
              if(pid){ HANDLE h=OpenProcess(PROCESS_TERMINATE,FALSE,pid); if(h){ TerminateProcess(h,1); CloseHandle(h); } } }
          g_appMenu=false; } }
    g_appMenuRect=RECT{(LONG)m0.x,(LONG)m0.y,(LONG)m1.x,(LONG)m1.y};
    bool inMenu=io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>m0.y&&io.MousePos.y<m1.y;
    if(!g_appMenuJustOpened && (click||rclick)&&!inMenu) g_appMenu=false;   // ignore the click that opened it

    // THE reason this menu used to stay up forever: a click that lands on another window never
    // reaches this overlay at all, so io.MouseClicked stays false and none of the tests above ever
    // fire. Watch the physical buttons and the real cursor instead - that works whether or not the
    // click was delivered to us. Escape closes it too, like every other menu on the system.
    if(g_appMenu){
        if(MenuOutsideClick(g_appMenuDis,inMenu)) g_appMenu=false;
        if(ImGui::IsKeyPressed(ImGuiKey_Escape)) g_appMenu=false;
    }
    g_appMenuJustOpened=false;
}
