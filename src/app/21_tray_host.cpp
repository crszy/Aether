// Aether - the system-tray host, audio devices, mixer.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ================================================================= system-tray HOST (Cairo/ManagedShell technique)
// We register our OWN "Shell_TrayWnd" and receive the Shell_NotifyIcon WM_COPYDATA that apps
// broadcast — the classic protocol still works on Win11 (MS only changed the visual host).
// Wire structs — verified from a live hex dump. The tray protocol uses 32-bit handles even on
// x64 (a compatibility layout), 4-byte packed, total nid size 956 (0x3BC).
#define TRAY_SIGNATURE 0x34753423u
struct WireNID {
    uint32_t cbSize; uint32_t hWnd; uint32_t uID; uint32_t uFlags; uint32_t uCallbackMessage; uint32_t hIcon;
    wchar_t szTip[128]; uint32_t dwState; uint32_t dwStateMask; wchar_t szInfo[256];
    uint32_t uVersion; wchar_t szInfoTitle[64]; uint32_t dwInfoFlags; GUID guidItem; uint32_t hBalloonIcon;
};
struct WireTrayData { uint32_t dwSignature; uint32_t dwMessage; WireNID nid; };
static_assert(offsetof(WireNID,hWnd)==4,"WireNID.hWnd offset");
static_assert(offsetof(WireNID,uCallbackMessage)==16,"WireNID.uCallbackMessage offset");
static_assert(offsetof(WireNID,hIcon)==20,"WireNID.hIcon offset");
static_assert(offsetof(WireNID,szTip)==24,"WireNID.szTip offset");
static_assert(sizeof(WireNID)==956,"WireNID size");
static_assert(offsetof(WireTrayData,nid)==8,"WireTrayData.nid offset");

struct SysTrayIcon { HWND hwnd=nullptr; UINT id=0; UINT cbMsg=0; UINT ver=0; bool hidden=false;
    GUID guid{}; bool hasGuid=false; std::wstring tip; ID3D11ShaderResourceView* tex=nullptr;
    mutable std::string key; mutable bool keyed=false; };   // TrayKey() result, computed once
static std::vector<SysTrayIcon> g_systray; static std::mutex g_systrayMtx;
static HWND g_trayHwnd=nullptr, g_trayNotifyHwnd=nullptr; static UINT g_msgTaskbarCreated=0;
static bool g_trayLogged=false;   // one-shot debug log for --traytest

static ID3D11ShaderResourceView* IconTex(HICON,int,bool);   // fwd
static bool GuidEq(const GUID&a,const GUID&b){ return memcmp(&a,&b,sizeof(GUID))==0; }

static void HandleTrayData(WireTrayData* td){
    if(td->dwSignature!=TRAY_SIGNATURE) return;
    UINT msg=td->dwMessage; WireNID* n=&td->nid;
    HWND  nhwnd=(HWND)(ULONG_PTR)n->hWnd;
    HICON nicon=(HICON)(ULONG_PTR)n->hIcon;
    bool hasGuid=(n->uFlags & 0x20)!=0;   // NIF_GUID
    std::lock_guard<std::mutex> lk(g_systrayMtx);
    auto match=[&](SysTrayIcon& s){ return hasGuid? (s.hasGuid&&GuidEq(s.guid,n->guidItem)) : (s.hwnd==nhwnd && s.id==n->uID); };
    if(msg==2){   // NIM_DELETE
        for(size_t i=0;i<g_systray.size();i++) if(match(g_systray[i])){ if(g_systray[i].tex)g_systray[i].tex->Release(); g_systray.erase(g_systray.begin()+i); break; }
        return;
    }
    if(msg==4){ for(auto&s:g_systray) if(match(s)) s.ver=n->uVersion; return; }   // NIM_SETVERSION
    // NIM_ADD(0) / NIM_MODIFY(1): find or create
    SysTrayIcon* rec=nullptr; for(auto&s:g_systray) if(match(s)){ rec=&s; break; }
    if(!rec){ SysTrayIcon s; s.hwnd=nhwnd; s.id=n->uID; s.hasGuid=hasGuid; s.guid=n->guidItem; g_systray.push_back(s); rec=&g_systray.back(); }
    if(n->uFlags & 0x01) rec->cbMsg=n->uCallbackMessage;                 // NIF_MESSAGE
    if(n->uFlags & 0x04){ rec->tip.assign(n->szTip, wcsnlen(n->szTip,128)); }  // NIF_TIP
    if(n->uFlags & 0x08) rec->hidden=(n->dwState & n->dwStateMask & 0x01)!=0;   // NIF_STATE / NIS_HIDDEN
    if((n->uFlags & 0x02) && nicon){                                    // NIF_ICON
        HICON cp=CopyIcon(nicon);
        if(cp){ ID3D11ShaderResourceView* t = g_dev? IconTex(cp,32):nullptr;
            if(t){ if(rec->tex)rec->tex->Release(); rec->tex=t; } DestroyIcon(cp); }
    }
}

static bool g_trayRaw=false; static std::vector<std::string> g_trayRawDump;   // --traytest hex capture
static LRESULT CALLBACK TrayWndProcImpl(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_COPYDATA){
        COPYDATASTRUCT* cds=(COPYDATASTRUCT*)l;
        if(cds && cds->dwData==1 && cds->lpData && cds->cbData>=sizeof(int32_t)*2){
            if(g_trayRaw && g_trayRawDump.size()<6){
                const uint8_t* p=(const uint8_t*)cds->lpData; int n=(int)std::min<DWORD>(cds->cbData,96);
                char buf[512]; int o=snprintf(buf,sizeof(buf),"cb=%lu bytes: ",cds->cbData);
                for(int i=0;i<n;i++) o+=snprintf(buf+o,sizeof(buf)-o,"%02X ",p[i]);
                g_trayRawDump.push_back(buf);
            }
            HandleTrayData((WireTrayData*)cds->lpData);
            return TRUE;   // report success so the app is satisfied
        }
        return TRUE;
    }
    return DefWindowProcW(h,m,w,l);
}
// Every message to these windows is timed HERE, not around DispatchMessage: messages SENT from other
// processes (Shell_NotifyIcon's WM_COPYDATA from every tray app, `Aether.exe -s ...`) are delivered
// inside PeekMessage and never pass through DispatchMessage, so the pump-level timer could not see them.
static LRESULT CALLBACK TrayWndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    const double t0=stall::Now(); LRESULT r=TrayWndProcImpl(h,m,w,l);
    MSG msg{}; msg.hwnd=h; msg.message=m; msg.wParam=w; msg.lParam=l; stall::Msg(msg,stall::Now()-t0);
    return r; }

static void InitTrayHost(){
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    HINSTANCE hi=GetModuleHandleW(nullptr);
    WNDCLASSEXW wc={sizeof(wc)}; wc.lpfnWndProc=TrayWndProc; wc.hInstance=hi; wc.lpszClassName=L"Shell_TrayWnd";
    RegisterClassExW(&wc);   // per-process class; explorer has its own in its process
    // topmost + created last -> FindWindow("Shell_TrayWnd") returns OURS first, so apps route icons to us
    g_trayHwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW, L"Shell_TrayWnd", L"", WS_POPUP, 0,0,0,0, nullptr,nullptr,hi,nullptr);
    WNDCLASSEXW wc2={sizeof(wc2)}; wc2.lpfnWndProc=DefWindowProcW; wc2.hInstance=hi; wc2.lpszClassName=L"TrayNotifyWnd";
    RegisterClassExW(&wc2);
    g_trayNotifyHwnd=CreateWindowExW(0,L"TrayNotifyWnd",L"",WS_CHILD,0,0,0,0,g_trayHwnd,nullptr,hi,nullptr);
    SetWindowPos(g_trayHwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    // announce ourselves so every tray app re-adds its icon to us
    SendNotifyMessageW(HWND_BROADCAST, g_msgTaskbarCreated, 0, 0);
}

// forward a click to a tray icon's owner using the classic contract (or v4 packed form)
static void TrayForward(const SysTrayIcon& s, UINT downMsg, UINT upMsg, int sx, int sy){
    if(!s.hwnd||!IsWindow(s.hwnd)) return;
    SetForegroundWindow(s.hwnd);   // so context menus don't immediately dismiss
    if(s.ver>=4){ WPARAM wp=MAKEWPARAM(sx,sy); LPARAM lp=MAKELPARAM(downMsg, s.id);
        PostMessageW(s.hwnd,s.cbMsg,wp,lp); lp=MAKELPARAM(upMsg,s.id); PostMessageW(s.hwnd,s.cbMsg,wp,lp); }
    else { PostMessageW(s.hwnd,s.cbMsg,(WPARAM)s.id,(LPARAM)downMsg); PostMessageW(s.hwnd,s.cbMsg,(WPARAM)s.id,(LPARAM)upMsg); }
}

// ---- tray-menu SCRAPE: read a tray app's real menu items so we can render them ourselves (in the bar) ----
// Windows has no clean API for this, so: make the app pop its menu OFF-SCREEN, grab the popup window's
// HMENU (MN_GETHMENU), enumerate the items, then dismiss it. Runs on a worker thread (it briefly blocks).
struct TrayMenuItem { std::wstring text; UINT id=0; bool sep=false; bool disabled=false; HMENU sub=nullptr; };
struct TrayMenu { std::vector<TrayMenuItem> items; HWND owner=nullptr; bool ready=false;
                  ULONGLONG at=0; };   // when it was read: an EMPTY read is retried after a while (see the bar)
static std::mutex g_trayMenuMtx; static std::unordered_map<void*,TrayMenu> g_trayMenuCache;
static std::atomic<bool> g_trayScraping{false};
#ifndef MN_GETHMENU
#define MN_GETHMENU 0x01E1
#endif
static void ReadHMenu(HMENU hm, std::vector<TrayMenuItem>& out){
    if(!hm) return; int n=GetMenuItemCount(hm); if(n<0) return;
    for(int i=0;i<n && (int)out.size()<48;i++){
        TrayMenuItem it; wchar_t buf[256]={0};
        MENUITEMINFOW mii={sizeof(mii)}; mii.fMask=MIIM_STRING|MIIM_ID|MIIM_FTYPE|MIIM_STATE|MIIM_SUBMENU;
        mii.dwTypeData=buf; mii.cch=255;
        if(!GetMenuItemInfoW(hm,i,TRUE,&mii)) continue;
        it.sep=(mii.fType&MFT_SEPARATOR)!=0; it.id=mii.wID; it.sub=mii.hSubMenu;
        it.disabled=(mii.fState&(MFS_DISABLED|MFS_GRAYED))!=0; it.text=buf;
        out.push_back(std::move(it));
    }
}
static void ScrapeTrayMenu(void* key, SysTrayIcon s){
    TrayMenu tm; tm.owner=s.hwnd;
    DWORD ownerPid=0; const ULONGLONG t0=GetTickCount64(); bool sawMenu=false;
    if(s.hwnd && IsWindow(s.hwnd)){
        SetForegroundWindow(s.hwnd);
        const int ox=-32000, oy=-32000;   // off-screen so the native menu never flashes
        // Exactly what Explorer sends for a right-click: DOWN, UP, and (version 4 icons) CONTEXTMENU. The DOWN
        // was missing, and apps that open their menu on a full press (Voicemeeter) never opened it - so the
        // scrape logged "menu window never appeared" and the icon fell back to the native menu.
        if(s.ver>=4){ WPARAM wp=MAKEWPARAM(ox&0xFFFF,oy&0xFFFF);
            PostMessageW(s.hwnd,s.cbMsg,wp,MAKELPARAM(WM_RBUTTONDOWN,s.id));
            PostMessageW(s.hwnd,s.cbMsg,wp,MAKELPARAM(WM_RBUTTONUP,s.id));
            PostMessageW(s.hwnd,s.cbMsg,wp,MAKELPARAM(WM_CONTEXTMENU,s.id)); }
        else { PostMessageW(s.hwnd,s.cbMsg,(WPARAM)s.id,(LPARAM)WM_RBUTTONDOWN);
               PostMessageW(s.hwnd,s.cbMsg,(WPARAM)s.id,(LPARAM)WM_RBUTTONUP); }
        HWND menuWnd=nullptr; HMENU hmenu=nullptr;
        GetWindowThreadProcessId(s.hwnd,&ownerPid);
        // up to ~1 s: 490 ms was tight for an app on a busy (or thermally throttled) CPU
        for(int i=0;i<140 && !hmenu;i++){ Sleep(7);
            // FindWindow("#32768") is ANY open popup menu - only the tray app's own will do, or another
            // app's menu (a browser's, say) was read and shown as this icon's
            menuWnd=nullptr;
            for(HWND m=FindWindowExW(nullptr,nullptr,L"#32768",nullptr); m; m=FindWindowExW(nullptr,m,L"#32768",nullptr)){
                DWORD mp=0; GetWindowThreadProcessId(m,&mp);
                if(mp==ownerPid && IsWindowVisible(m)){ menuWnd=m; break; } }
            // the menu belongs to the tray app: a plain SendMessage waited forever on a hung one, which left
            // g_trayScraping stuck and every tray menu dead until a restart
            DWORD_PTR r=0;
            if(menuWnd && SendMessageTimeoutW(menuWnd,MN_GETHMENU,0,0,SMTO_ABORTIFHUNG,200,&r)) hmenu=(HMENU)r;
        }
        sawMenu=menuWnd!=nullptr;
        if(hmenu) ReadHMenu(hmenu,tm.items);
        if(menuWnd){ PostMessageW(menuWnd,WM_KEYDOWN,VK_ESCAPE,0); PostMessageW(menuWnd,WM_KEYUP,VK_ESCAPE,0); }
        PostMessageW(s.hwnd,WM_CANCELMODE,0,0);
    }
    tm.ready=true; tm.at=GetTickCount64();
    if(tm.items.empty()) AetherLog("tray menu: nothing read for \"%s\" (pid %lu) after %llu ms, menu window %s - the native menu is used on right-click; retried later",
                                   W2U8(s.tip).substr(0,60).c_str(),(unsigned long)ownerPid,GetTickCount64()-t0,sawMenu?"seen":"never appeared");
    { std::lock_guard<std::mutex> lk(g_trayMenuMtx); g_trayMenuCache[key]=std::move(tm); }
    g_trayScraping=false;
}
// send a scraped menu item's command back to the owning app
static void TrayInvoke(HWND owner, UINT id){
    if(owner && IsWindow(owner) && id){ SetForegroundWindow(owner); PostMessageW(owner,WM_COMMAND,MAKEWPARAM(id,0),0); }
}

// Modern macOS-style magnifying dock: a floating rounded bar at the bottom whose icons swell smoothly
// toward the cursor (cosine falloff), re-flow apart to make room, sit on the container baseline, show a
// running/active indicator dot, a hover label, and a small launch bounce. Fully data-driven off the
// dock-appearance globals so the customization panel can retune it live.
static float DockFalloff(float d,float R){ if(d>=R) return 0.0f; float t=d/R; return 0.5f*(1.0f+cosf(3.14159265f*t)); }
static void DrawDock(){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x,H=io.DisplaySize.y;
    float slide=EaseOutCubic(std::clamp(g_dockReveal,0.0f,1.0f));
    if(slide<0.002f){ g_dockRect=RECT{0,0,0,0}; return; }
    int n=(int)g_dockApps.size();
    if(n<=0){ g_dockRect=RECT{0,0,0,0}; return; }
    const float base=g_dockIcon, gap=g_dockGap, pad=14.0f, mag=std::max(1.0f,g_dockMag);
    const float step=base+gap;
    const float R=step*std::max(0.5f,g_dockMagRange);
    const float margin=14.0f;                                   // gap from the screen's bottom edge
    float mouseX=io.MousePos.x, mouseY=io.MousePos.y;
    // resting (unmagnified) geometry, centred on the monitor
    float totalRest=n*base+(n-1)*gap;
    float restLeft=(W-totalRest)*0.5f;
    // magnify only when the cursor is within reach of the dock (near the bottom)
    float reachTop=H - (base*mag + 74.0f);
    bool nearDock=(mouseY>reachTop);
    static std::vector<float> sc; sc.assign(n,1.0f);
    for(int i=0;i<n;i++){ float rc=restLeft+base*0.5f+i*step; float d=fabsf(mouseX-rc);
        sc[i]= nearDock? 1.0f+(mag-1.0f)*DockFalloff(d,R) : 1.0f; }
    // re-flow: total magnified width, then place icons left→right so magnified ones push neighbours apart
    float totalMag=0; for(int i=0;i<n;i++) totalMag+=base*sc[i]; totalMag+=(n-1)*gap;
    float left=(W-totalMag)*0.5f;
    // container: fixed resting height, sitting at the bottom; magnified icons overflow ABOVE it (macOS)
    float Hc=base+2*pad;
    float contBottom = H - margin + (1.0f-slide)*(Hc+margin+24.0f);   // slides up from below the edge
    float contTop=contBottom-Hc;
    float baseline=contBottom-pad;                                    // icons' bottom edge rests here
    float contL=left-pad, contR=left+totalMag+pad;
    ImVec2 pmin=V(contL,contTop),pmax=V(contR,contBottom);
    // soft drop shadow
    for(int s=8;s>0;s--){ float e=s*2.2f; dl->AddRect(V(pmin.x-e,pmin.y-e+3),V(pmax.x+e,pmax.y+e+3),IM_COL32(0,0,0,(int)(10*slide)),g_dockRound+e,0,e*0.9f); }
    // acrylic-of-wallpaper or solid material
    int op=(int)(g_dockOpacity*255*slide);
    ID3D11ShaderResourceView* bg=(g_bgMode==0?g_dockAcrylic:nullptr);
    if(bg){ dl->AddImageRounded((ImTextureID)bg,pmin,pmax,ImVec2(0,0),ImVec2(1,1),WithA(IM_COL32(255,255,255,255),op),g_dockRound);
            dl->AddRectFilled(pmin,pmax, g_darkUI?IM_COL32(18,18,22,(int)(op*0.55f)):PanelCol((int)(op*0.55f)),g_dockRound); }
    else    dl->AddRectFilled(pmin,pmax, g_darkUI?IM_COL32(22,22,27,op):PanelCol(op),g_dockRound);
    TopWash(dl,pmin,pmax,(int)((g_darkUI?14:30)*slide),g_dockRound,ImDrawFlags_RoundCornersAll,0.65f);   // fading, not a hard half
    dl->AddRect(V(pmin.x+0.5f,pmin.y+0.5f),V(pmax.x-0.5f,pmax.y-0.5f),g_darkUI?IM_COL32(255,255,255,(int)(28*slide)):IM_COL32(255,255,255,(int)(120*slide)),g_dockRound,0,1.2f);
    g_dockRect=RECT{(LONG)(contL),(LONG)std::max(0.0f,contTop-(base*(mag-1.0f))),(LONG)(contR),(LONG)contBottom};
    // per-icon left edge from the re-flow, so drag/hit-testing and drawing agree
    static std::vector<float> xs; xs.assign(n,0.0f);
    { float cx=left; for(int i=0;i<n;i++){ xs[i]=cx; cx+=base*sc[i]+gap; } }
    int pinCount=0; for(int i=0;i<n;i++) if(g_dockApps[i].pinned) pinCount++;   // pinned occupy the front
    // which icon is the cursor over?
    int overIdx=-1;
    for(int i=0;i<n;i++){ float w=base*sc[i];
        if(mouseX>xs[i]-gap*0.5f && mouseX<xs[i]+w+gap*0.5f && mouseY>contTop-(w-base) && mouseY<contBottom) overIdx=i; }

    // ---- pointer: a short click activates/launches; a drag (pinned only) reorders ----
    bool menuActive=g_dockMenuOpen;   // when the context menu is up it owns the pointer this frame
    bool refreshAfter=false;          // structural changes rebuild g_dockApps — deferred to end of frame
    if(!menuActive){
        if(io.MouseClicked[0] && overIdx>=0){ g_dockPress=overIdx; g_dockPressMX=mouseX; g_dockDragging=false; }
        if(io.MouseDown[0] && g_dockPress>=0 && !g_dockDragging && g_dockPress<n
           && g_dockApps[g_dockPress].pinned && fabsf(mouseX-g_dockPressMX)>7.0f){
            g_dockDragging=true; g_dockDragExe=g_dockApps[g_dockPress].exe; }
        if(io.MouseClicked[1] && overIdx>=0){ auto&a=g_dockApps[overIdx]; float w=base*sc[overIdx];
            g_dockMenuOpen=true; g_dockMenuExe=a.exe; g_dockMenuPinned=a.pinned; g_dockMenuRunning=a.running;
            g_dockMenuWins=a.wins; g_dockMenuPos=V(xs[overIdx]+w*0.5f, baseline-w-10.0f);
            g_dockPress=-1; g_dockDragging=false; }
    }

    int hoverIdx=-1;
    for(int i=0;i<n;i++){ auto&a=g_dockApps[i]; float w=base*sc[i];
        bool isDrag = g_dockDragging && a.exe==g_dockDragExe;
        float bounce=0; if(g_dockBounce==i){ float bt=g_dockBounceT; bounce = -fabsf(sinf(bt*3.14159f))*18.0f*(1.0f-bt); }
        float ix = isDrag? (mouseX-w*0.5f) : xs[i];
        ImVec2 a0=V(ix, baseline-w+bounce), a1=V(ix+w, baseline+bounce);
        if(overIdx==i && !g_dockDragging) hoverIdx=i;
        int alpha=(int)(255*slide);
        if(!a.running)      alpha=(int)(alpha*0.55f);   // pinned-but-closed launcher reads as dimmed
        else if(a.minimized)alpha=(int)(alpha*0.72f);   // minimised: a touch dimmer than active
        if(isDrag)          alpha=(int)(alpha*0.85f);
        if(a.icon) dl->AddImage((ImTextureID)a.icon,a0,a1,ImVec2(0,0),ImVec2(1,1),WithA(IM_COL32(255,255,255,255),alpha));
        else dl->AddRectFilled(a0,a1,WithA(COL_INK2,(int)(120*slide)),8);
        // indicator dot — only for running apps; accent when this app is the foreground one
        if(a.running){ bool active=DockGroupHasFg(a); float dotx=ix+w*0.5f;
            dl->AddCircleFilled(V(dotx,contBottom-4.5f), active?3.2f:2.4f,
                active?AccA((int)(255*slide)):WithA(IM_COL32(170,170,175,255),(int)(200*slide))); }
    }
    // divider between the pinned block and the running-but-unpinned apps
    if(pinCount>0 && pinCount<n){ float sx=xs[pinCount]-gap*0.5f;
        dl->AddLine(V(sx,baseline-base*0.62f),V(sx,baseline+2.0f),
            g_darkUI?IM_COL32(255,255,255,(int)(42*slide)):IM_COL32(0,0,0,(int)(48*slide)),1.2f); }
    // drop caret while dragging a pin
    if(g_dockDragging && pinCount>0){ int j=0; for(int i=0;i<pinCount;i++){ float c=xs[i]+base*sc[i]*0.5f; if(mouseX>c) j++; }
        j=std::min(j,pinCount); float caretX=(j<pinCount)? xs[j]-gap*0.5f : xs[pinCount-1]+base*sc[pinCount-1]+gap*0.5f;
        dl->AddLine(V(caretX,baseline-base-6),V(caretX,baseline+4), AccA((int)(255*slide)),2.4f); }

    // hover label above the magnified icon
    if(hoverIdx>=0){ auto&a=g_dockApps[hoverIdx];
        std::string t=a.title.empty()?"":a.title; size_t nl=t.find('\n'); if(nl!=std::string::npos)t=t.substr(0,nl);
        if(!t.empty()){ float w=base*sc[hoverIdx]; float lx=xs[hoverIdx]+w*0.5f;
            float tw=TextW(g_fSml,14,t.c_str())+18, th=26; float ly=baseline-w-14-th;
            ImVec2 l0=V(lx-tw*0.5f,ly),l1=V(lx+tw*0.5f,ly+th);
            dl->AddRectFilled(l0,l1,WithA(IM_COL32(28,28,34,247),(int)(255*slide)),9);
            dl->AddRect(l0,l1,WithA(IM_COL32(255,255,255,26),(int)(255*slide)),9,0,1.0f);
            TextAt(dl,g_fSml,14,V(l0.x+9,ly+6),WithA(COL_INK,(int)(255*slide)),t.c_str()); } }

    // ---- right-click context menu (drawn last so it sits on top) ----
    if(g_dockMenuOpen){
        struct MI{ const char* label; int act; };   // act 1=pin 2=unpin 3=quit
        std::vector<MI> items;
        if(g_dockMenuRunning && !g_dockMenuPinned) items.push_back({"Keep in Dock",1});
        if(g_dockMenuPinned)                        items.push_back({"Remove from Dock",2});
        if(g_dockMenuRunning)                       items.push_back({"Quit",3});
        if(items.empty()){ g_dockMenuOpen=false; }
        else{
            float mw=178, ih=30, mh=ih*items.size()+8;
            float mx=std::clamp(g_dockMenuPos.x-mw*0.5f,6.0f,W-mw-6.0f), my=std::max(4.0f,g_dockMenuPos.y-mh);
            ImVec2 m0=V(mx,my),m1=V(mx+mw,my+mh);
            for(int s=6;s>0;s--){ float e=s*2.0f; dl->AddRect(V(m0.x-e,m0.y-e+2),V(m1.x+e,m1.y+e+2),IM_COL32(0,0,0,12),12+e,0,e); }
            dl->AddRectFilled(m0,m1,g_darkUI?IM_COL32(30,30,36,252):IM_COL32(248,248,250,252),12);
            dl->AddRect(m0,m1,WithA(IM_COL32(255,255,255,g_darkUI?30:120),255),12,0,1.0f);
            int hovItem=-1;
            for(size_t i=0;i<items.size();i++){ float iy=my+4+i*ih; ImVec2 r0=V(mx+4,iy),r1=V(mx+mw-4,iy+ih);
                bool hit=mouseX>=r0.x&&mouseX<=r1.x&&mouseY>=r0.y&&mouseY<=r1.y; if(hit)hovItem=(int)i;
                if(hit) dl->AddRectFilled(r0,r1,AccA(42),7);
                ImU32 tc=(items[i].act==3)?IM_COL32(232,120,120,255):(g_darkUI?COL_INK:IM_COL32(30,30,34,255));
                TextAt(dl,g_fSml,14,V(mx+14,iy+ih*0.5f-8),tc,items[i].label); }
            // only act on clicks from the SECOND frame on, so the very right-click that opened the
            // menu doesn't also trip the click-outside-to-close test on the same frame
            if(menuActive && io.MouseClicked[0]){
                if(hovItem>=0){ int act=items[hovItem].act;
                    if(act==1){ DockPin p; p.exe=g_dockMenuExe; p.icon=LoadExeIcon(g_dockMenuExe); g_dockPins.push_back(std::move(p)); SaveConfig(); refreshAfter=true; }
                    else if(act==2){ for(size_t k=0;k<g_dockPins.size();k++) if(_wcsicmp(g_dockPins[k].exe.c_str(),g_dockMenuExe.c_str())==0){ if(g_dockPins[k].icon)g_dockPins[k].icon->Release(); g_dockPins.erase(g_dockPins.begin()+k); break; } SaveConfig(); refreshAfter=true; }
                    else if(act==3){ for(HWND wnd:g_dockMenuWins) if(IsWindow(wnd)) PostMessageW(wnd,WM_CLOSE,0,0); refreshAfter=true; } }
                g_dockMenuOpen=false; g_dockPress=-1; g_dockDragging=false;
            } else if(menuActive && io.MouseClicked[1] && hovItem<0){ g_dockMenuOpen=false; }
            // extend the hit region so the menu (above the container) is actually clickable
            g_dockRect.top=std::min(g_dockRect.top,(LONG)my);
            g_dockRect.left=std::min(g_dockRect.left,(LONG)mx);
            g_dockRect.right=std::max(g_dockRect.right,(LONG)(mx+mw));
        }
    }

    // ---- release: commit a click (activate/launch) or a drag (reorder pin) ----
    if(!menuActive && io.MouseReleased[0]){
        if(g_dockDragging){
            int j=0; for(int i=0;i<pinCount;i++){ float c=xs[i]+base*sc[i]*0.5f; if(mouseX>c) j++; }
            int cur=-1; for(size_t k=0;k<g_dockPins.size();k++) if(_wcsicmp(g_dockPins[k].exe.c_str(),g_dockDragExe.c_str())==0){ cur=(int)k; break; }
            if(cur>=0){ int tgt=std::min(j,(int)g_dockPins.size()); if(tgt>cur) tgt--; tgt=std::clamp(tgt,0,(int)g_dockPins.size()-1);
                if(tgt!=cur){ DockPin mv=std::move(g_dockPins[cur]); g_dockPins.erase(g_dockPins.begin()+cur); g_dockPins.insert(g_dockPins.begin()+tgt,std::move(mv)); SaveConfig(); refreshAfter=true; } }
        } else if(g_dockPress>=0 && g_dockPress==overIdx && g_dockPress<n){
            auto&a=g_dockApps[g_dockPress];
            if(a.running) DockGroupClick(a); else DockLaunch(a.exe);
            g_dockBounce=g_dockPress; g_dockBounceT=0;
        }
        g_dockPress=-1; g_dockDragging=false;
    }
    if(g_dockBounce>=0){ g_dockBounceT+=0.10f; if(g_dockBounceT>=1.0f){ g_dockBounce=-1; g_dockBounceT=0; } }
    if(refreshAfter) RefreshDock();   // rebuild AFTER all drawing so g_dockApps stays valid this frame
}
