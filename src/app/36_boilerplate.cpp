// Aether - window plumbing: render helpers, WndProc, keyboard hook, IPC.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif
#include "src/modules/strive/Strive.h"   // Guilty Gear Strive extras: impact frames, workspace banner, intro

// ================================================================= boilerplate
static void CreateRTV(){ ID3D11Texture2D* bb; g_sc->GetBuffer(0,IID_PPV_ARGS(&bb)); if(bb){g_dev->CreateRenderTargetView(bb,nullptr,&g_rtv);bb->Release();} }

// Start a frame in LOGICAL pixels (see ScaleDrawData): shrink the display size, then rescale the
// mouse afterwards so hit-testing in the Draw* functions matches what is drawn.
static void NewFrameL(){
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame();
    ImGuiIO& io=ImGui::GetIO();
    if(g_uiScale!=1.0f){ io.DisplaySize.x/=g_uiScale; io.DisplaySize.y/=g_uiScale; }
    ImGui::NewFrame();
    // Every rounded surface in the shell lands in these two draw lists, and ImGui resets the fringe
    // to 1px on each list at the start of every frame - which is what left high-contrast arcs
    // looking stepped. This is the single place that covers the bar, drawer, cards, menus, flyouts
    // and the desktop bubble alike.
    ImGui::GetBackgroundDrawList()->_FringeScale = 2.4f;
    ImGui::GetForegroundDrawList()->_FringeScale = 2.4f;
    // Rescaling io.MousePos IN PLACE is only correct if ImGui refreshed it from the backend this
    // frame. It does not: the Win32 backend only feeds a position when Windows actually sends one,
    // so on every frame without a mouse event the ALREADY-DIVIDED value got divided again. The
    // error compounds geometrically - measured at scale 0.838, a cursor really at (901,22) had
    // rotted to (518550,11410) inside a second, i.e. divided ~36 times - so nothing could be
    // hovered or clicked at any scale except exactly 1.0, where the branch is skipped entirely.
    //
    // Remembering what we last wrote tells the two apart: if the value still equals our own output,
    // the backend did not refresh it and it is already in logical px.
    if(g_uiScale!=1.0f){
        // The stored value is FLOORED because NewFrame floors io.MousePos itself - comparing against
        // an unrounded one never matched, so the guard never fired and the division kept compounding.
        static std::unordered_map<ImGuiContext*,ImVec2> s_lastOut;
        ImVec2& last = s_lastOut[ImGui::GetCurrentContext()];
        if(io.MousePos.x!=last.x || io.MousePos.y!=last.y){
            io.MousePos.x=floorf(io.MousePos.x/g_uiScale);
            io.MousePos.y=floorf(io.MousePos.y/g_uiScale);
            last=io.MousePos;
        }
        // NewFrame latched these off the raw position a moment ago, so restate them in logical px.
        for(int i=0;i<5;i++) if(io.MouseClicked[i]) io.MouseClickedPos[i]=io.MousePos;
    }
}
static void RenderL(ID3D11RenderTargetView* rtv, IDXGISwapChain1* sc){
    StvImpactPre(sc);                                   // Strive impact frames (opt-in): flash + slash, before Render
    ImGui::Render(); ScaleDrawData(ImGui::GetDrawData(),g_uiScale);
    StvImpactShake(ImGui::GetDrawData());               // ...and the shake, in physical pixels
    const float clr[4]={0,0,0,0};
    g_ctx->OMSetRenderTargets(1,&rtv,nullptr); g_ctx->ClearRenderTargetView(rtv,clr);
    // Present WITHOUT vsync-blocking. This is called once PER VISIBLE LAYER per frame; a blocking
    // Present(1,0) here stalled a full vsync for EACH layer, so N layers (e.g. Settings + bar) ran at
    // 60/N fps — that was the "lag". DirectComposition still composites at vsync, so no tearing. The
    // whole loop is paced once per frame by a single DwmFlush() instead (see the render loop).
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if(g_snapSc==sc){ g_snapSc=nullptr;                 // off-screen snapshot: read the frame back before presenting
        ID3D11Texture2D* bb=nullptr;
        if(SUCCEEDED(sc->GetBuffer(0,IID_PPV_ARGS(&bb))) && bb){
            D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d); d.Usage=D3D11_USAGE_STAGING; d.BindFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.MiscFlags=0;
            ID3D11Texture2D* st=nullptr;
            if(SUCCEEDED(g_dev->CreateTexture2D(&d,nullptr,&st)) && st){
                g_ctx->CopyResource(st,bb);
                D3D11_MAPPED_SUBRESOURCE m;
                if(SUCCEEDED(g_ctx->Map(st,0,D3D11_MAP_READ,0,&m))){
                    std::vector<uint8_t> px((size_t)d.Width*d.Height*4);
                    for(UINT y=0;y<d.Height;y++){ const uint8_t* src=(uint8_t*)m.pData+(size_t)y*m.RowPitch; uint8_t* dst=&px[(size_t)y*d.Width*4];
                        for(UINT x=0;x<d.Width;x++){ uint8_t a=src[x*4+3]; const int bg=22;   // premultiplied over a dark ground
                            dst[x*4]  =(uint8_t)std::min(255,src[x*4]  +bg*(255-a)/255);
                            dst[x*4+1]=(uint8_t)std::min(255,src[x*4+1]+bg*(255-a)/255);
                            dst[x*4+2]=(uint8_t)std::min(255,src[x*4+2]+(bg+6)*(255-a)/255); dst[x*4+3]=255; } }
                    g_ctx->Unmap(st,0);
                    SavePng(g_snapPath,px.data(),(int)d.Width,(int)d.Height); }
                st->Release(); }
            bb->Release(); } }
    // DO_NOT_WAIT: with three frames already queued and DWM not taking them, a plain Present blocks
    // the render thread until it does - that was bar_layer's ~260 ms stalls (the bar is the one layer
    // that is always up, so it caught every compositor hiccup). A frame DWM cannot take is dropped;
    // every layer redraws in full next frame anyway.
    // ...but never forever. DO_NOT_WAIT also refuses a frame whenever the GPU is still busy with this layer's
    // last one, and a heavy layer (the overview composites live captures of every workspace) could have
    // EVERY frame refused: its window was up and drawing and nothing ever reached the screen. After three
    // refusals in a row a layer gets one ordinary Present, which waits for at most a frame or two.
    { STALL(present);
      static std::unordered_map<IDXGISwapChain1*,int> refused;
      int& n=refused[sc];
      if(n>=3){ sc->Present(0,0); n=0; }
      else if(sc->Present(0,DXGI_PRESENT_DO_NOT_WAIT)==DXGI_ERROR_WAS_STILL_DRAWING){
          if(++n==3){ static int logged=0; if(logged++<20) AetherLog("present: a layer refused 3 frames in a row - presenting it normally once"); } }
      else n=0; }
}
// capture a LOGICAL screen region for the acrylic frost (screen coords are physical)
static ID3D11ShaderResourceView* CaptureRegionBlur(int sx,int sy,int iw,int ih);   // fwd
static ID3D11ShaderResourceView* CaptureL(float x,float y,float w,float h){
    return CaptureRegionBlur((int)(g_mx+x*g_uiScale),(int)(g_my+y*g_uiScale),(int)(w*g_uiScale),(int)(h*g_uiScale));
}

static LRESULT WINAPI WndProcImpl(HWND h,UINT m,WPARAM w,LPARAM l){
    // route input to the ImGui context that owns this window
    ImGuiContext* c = (h==g_dockHwnd)? g_ctxDock : (h==g_barHwnd)? g_ctxBar : (h==g_sideHwnd)? g_ctxSide : (h==g_osdHwnd)? g_ctxOsd : (h==g_sessHwnd)? g_ctxSess : (h==g_lockHwnd)? g_ctxLock : (h==g_launHwnd)? g_ctxLaunch : (h==g_swHwnd)? g_ctxSw : (h==g_ovHwnd)? g_ctxOv : (h==g_notifHwnd)? g_ctxNotif : (h==g_medHwnd)? g_ctxMed : (h==g_deskHwnd)? g_ctxDesk : (h==g_setHwnd)? g_ctxSet : (h==g_dimHwnd)? g_ctxDim : (h==g_snipHwnd)? g_ctxSnip : (h==g_fmHwnd)? g_ctxFm : (h==g_decoHwnd)? g_ctxDeco : g_ctxDrawer;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    if (c) ImGui::SetCurrentContext(c);
    bool handled = ImGui_ImplWin32_WndProcHandler(h,m,w,l)!=0;
    if (c && prev) ImGui::SetCurrentContext(prev);
    if (handled) return true;

    // ---- shell hook: the live window-list feed a real taskbar runs on ----
    if(g_shellHookMsg && m==g_shellHookMsg){
        switch(LOWORD(w)){
        case HSHELL_WINDOWCREATED: case HSHELL_WINDOWDESTROYED:
        case HSHELL_WINDOWACTIVATED: case HSHELL_RUDEAPPACTIVATED:
        case HSHELL_WINDOWREPLACED: case HSHELL_REDRAW: case HSHELL_FLASH:
            g_taskListDirty=true; break;
        case HSHELL_TASKMAN:
            // A bare Win tap ALSO arrives here, so forcing the drawer open made the Super key pop
            // the dashboard as well as the launcher. The launcher owns that gesture; ignore it.
            break;
        }
        return 0;
    }

    static const RECT g_zeroRect={0,0,0,0};
    const RECT& hit = (h==g_dockHwnd)? g_dockRect : (h==g_barHwnd)? g_zeroRect : (h==g_sideHwnd)? g_sideRect : (h==g_osdHwnd)? g_osdRect : (h==g_sessHwnd)? g_sessRect : (h==g_lockHwnd)? g_lockRect : (h==g_launHwnd)? g_launRect : (h==g_swHwnd)? g_swRect : (h==g_ovHwnd)? g_ovRect : (h==g_notifHwnd)? g_notifRect : (h==g_setHwnd)? g_setRect : (h==g_medHwnd)? g_medRect : (h==g_fmHwnd)? g_fmRect : (h==g_decoHwnd)? g_decoRect : (h==g_deskHwnd||h==g_dimHwnd)? g_zeroRect : g_drawerRect;
    switch(m){
    // the launcher and settings take focus (they read the keyboard)
    // The drawer is normally NOACTIVATE so it never steals focus from what you are working in.
    // A note editor you cannot type into is useless though, so it becomes activatable for exactly
    // as long as one is open (see the GWL_EXSTYLE flip in the render loop).
    case WM_MOUSEACTIVATE: return (h==g_launHwnd||h==g_setHwnd||h==g_snipHwnd||h==g_fmHwnd||h==g_lockHwnd
                                   ||(h==g_hwnd&&DrawerTyping()))? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_NCHITTEST: {                     // only the visible panel is clickable; rest passes through
        if (h==g_snipHwnd) return g_snipActive? HTCLIENT : HTTRANSPARENT;   // whole overlay grabs the drag
        POINT p={GET_X_LPARAM(l),GET_Y_LPARAM(l)}; ScreenToClient(h,&p);
        // hit rects are authored in logical px; the window is physical
        float s=g_uiScale;
        if (h==g_barHwnd){                   // the bar window holds one strip PER MONITOR
            for(auto& b:g_bars)
                if(p.x>=b.rect.left*s && p.x<b.rect.right*s && p.y>=b.rect.top*s && p.y<b.rect.bottom*s)
                    return HTCLIENT;
            // The task-button right-click menu is drawn by this window but lives OUTSIDE every strip
            // rect, so without this it fell through to HTTRANSPARENT: the window then received no
            // WM_MOUSEMOVE over its own menu, ImGui's MousePos froze at the last point on the strip,
            // and no row could ever be hovered or clicked - which is why "Pin to taskbar" did
            // nothing. SetWindowRgn already covers the menu (it draws, and WindowFromPoint reports
            // this window), but NCHITTEST is the gate that decides whether input arrives at all.
            // The bar's own strip menu never hit this because its rect is folded into BS.rect.
            { const RECT& am=g_appMenuRect;
              if(am.right>am.left &&
                 p.x>=am.left*s && p.x<am.right*s && p.y>=am.top*s && p.y<am.bottom*s)
                  return HTCLIENT; }
            // the recording checker (RecordHud.h) and the caelestia bar popouts live outside the strips too
            for(const RECT* rr : { &g_recHudRect, &g_barPopRect, &g_alertRect, &g_tourCoachRect })
                if(rr->right>rr->left && p.x>=rr->left*s && p.x<rr->right*s && p.y>=rr->top*s && p.y<rr->bottom*s)
                    return HTCLIENT;
            return HTTRANSPARENT;
        }
        if (p.x>=hit.left*s && p.x<hit.right*s && p.y>=hit.top*s && p.y<hit.bottom*s) return HTCLIENT;
        return HTTRANSPARENT;
    }
    case WM_COPYDATA: {                                                    // `Aether.exe -s <command>`
        COPYDATASTRUCT* cds=(COPYDATASTRUCT*)l;
        if(cds && cds->dwData==AETHER_IPC && cds->lpData && cds->cbData>1){
            std::string n((const char*)cds->lpData, cds->cbData-1);
            RunShellCommand(n);
        }
        return TRUE; }
    // somebody - anybody - put something on the clipboard
    case WM_CLIPBOARDUPDATE: if(h==g_hwnd) ClipCapture(); return 0;
    case WM_SYSCOMMAND: if((w&0xFFF0)==SC_KEYMENU) return 0; break;         // block Alt menu (Alt+Space)
    case WM_HOTKEY:
        if(w==1) PostQuitMessage(0);                                       // Ctrl+Alt+Q -> graceful quit
        else if(w==2){ MgKeyFire(HK_LAUNCHER); LaunToggle(); }             // Alt+Space -> toggle launcher
        else if(w==3){ WallScan(); CycleWallpaper(-1); }                   // Ctrl+Alt+[ -> previous wallpaper
        else if(w==4){ WallScan(); CycleWallpaper(+1); }                   // Ctrl+Alt+] -> next wallpaper
        else if(w==1+HK_SNIP) StartSnip();                                 // snip a region screenshot
        else if(w==1+HK_CLIP){ MgKeyFire(HK_CLIP); g_launShow=true; g_lmode=4; g_lsearch[0]=0; ClipRebuild(""); g_lsel=0; }
        else if(w==1+HK_WALLPICK){ MgKeyFire(HK_WALLPICK); WallScan(); g_launShow=true; g_lmode=2; g_lsearch[0]=0; }
        else if(w==1+HK_OVERVIEW){ MgKeyFire(HK_OVERVIEW); if(g_ovWant) OvClose(); else OvOpen(); }   // Super+Tab -> workspace overview
        else if(w>=1000 && w<1064) LfxAliasHotkey((int)w-1000);          // a launcher keyword's hotkey
        return 0;
    case WM_CLOSE:  PostQuitMessage(0); return 0;                          // quit cleanly (restores taskbar)
    case WM_ENDSESSION: if(g_taskbarHidden) SetWindowsTaskbar(false); return 0;  // restore on logoff/shutdown
    // a monitor was plugged in, unplugged or rearranged: rebuild the whole layout next frame
    case WM_DISPLAYCHANGE: g_monsDirty=true; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
// Every message to these windows is timed HERE, not around DispatchMessage: messages SENT from other
// processes (Shell_NotifyIcon's WM_COPYDATA from every tray app, `Aether.exe -s ...`) are delivered
// inside PeekMessage and never pass through DispatchMessage, so the pump-level timer could not see them.
static LRESULT WINAPI WndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    const double t0=stall::Now(); LRESULT r=WndProcImpl(h,m,w,l);
    MSG msg{}; msg.hwnd=h; msg.message=m; msg.wParam=w; msg.lParam=l; stall::Msg(msg,stall::Now()-t0);
    return r; }

static void AddFonts(){
    ImGuiIO& io=ImGui::GetIO();
    // Real GNOME/CachyOS typeface pulled from the ISO. AdwaitaSans ships Regular+Italic only,
    // so every weight slot maps to Regular; Segoe UI is the fallback if the file is missing.
    auto exists=[](const char* p){ FILE* f=fopen(p,"rb"); if(f){fclose(f);return true;} return false; };
    // Caelestia's ACTUAL typeface — Google Sans Flex (assets/google-sans-flex, SIL OFL). Use their real
    // font so Aether's typography IS Caelestia's, not a Segoe/Adwaita stand-in.
    // held in statics so the const char* below stay valid for the whole call
    static std::string s_gsf=ExeDir()+"assets\\GoogleSansFlex.ttf";
    static std::string s_adw=ExeDir()+"linux\\fonts\\AdwaitaSans-Regular.ttf";
    static std::string s_adm=ExeDir()+"linux\\fonts\\AdwaitaMono-Regular.ttf";
    const char* gsf=s_gsf.c_str();
    const char* adw=s_adw.c_str();
    const char* adm=s_adm.c_str();
    const char* sui="C:\\Windows\\Fonts\\segoeui.ttf";
    const char* ssb="C:\\Windows\\Fonts\\seguisb.ttf";
    const char* slt="C:\\Windows\\Fonts\\segoeuil.ttf";
    bool haveGsf=exists(gsf), haveAdw=exists(adw);
    const char* reg = haveGsf?gsf:(haveAdw?adw:sui);
    const char* med = haveGsf?gsf:(haveAdw?adw:ssb);   // Google Sans Flex is variable -> same file for all weights
    const char* lt  = haveGsf?gsf:(haveAdw?adw:slt);
    // Google Sans Flex has no arrows or symbols, so every "↓ 12 GiB" / "← → browse" in the UI came
    // out as "?". Merging a symbol face over the same font fixes the lot in one place instead of
    // rewriting each string: ImGui falls back to the merged glyphs only for codepoints the primary
    // font is missing.
    static const ImWchar SYMR[] = {0x2010,0x206F, 0x2190,0x21FF, 0x2600,0x26FF, 0};
    const char* sym = exists("C:\\Windows\\Fonts\\seguisym.ttf") ? "C:\\Windows\\Fonts\\seguisym.ttf" : sui;
    // Google Sans Flex / Adwaita Sans do not carry Greek or Cyrillic either, so Segoe UI is merged
    // over those ranges as well. ImGui keeps the FIRST font's glyph for a codepoint, so this only
    // fills gaps - the primary face still wins everywhere it has coverage.
    static const ImWchar EXTR[] = {0x0100,0x024F, 0x0370,0x03FF, 0x0400,0x04FF, 0};
    auto withSymbols=[&](ImFont* f,float px){
        if(!f) return f;
        ImFontConfig mc; mc.MergeMode=true; mc.PixelSnapH=true;
        io.Fonts->AddFontFromFileTTF(sym,px,&mc,SYMR);
        ImFontConfig ec; ec.MergeMode=true; ec.PixelSnapH=true;
        io.Fonts->AddFontFromFileTTF(sui,px,&ec,EXTR);
        return f; };
    g_fReg=withSymbols(io.Fonts->AddFontFromFileTTF(reg,18,nullptr,RANGES),18);
    g_fSml=withSymbols(io.Fonts->AddFontFromFileTTF(reg,15,nullptr,RANGES),15);
    g_fMed=io.Fonts->AddFontFromFileTTF(med,21,nullptr,RANGES); if(!g_fMed)g_fMed=io.Fonts->AddFontFromFileTTF(reg,21,nullptr,RANGES);
    withSymbols(g_fMed,21);
    g_fBig=withSymbols(io.Fonts->AddFontFromFileTTF(reg,34,nullptr,RANGES),34);
    g_fHuge=io.Fonts->AddFontFromFileTTF(lt,68,nullptr,RANGES); if(!g_fHuge)g_fHuge=io.Fonts->AddFontFromFileTTF(reg,68,nullptr,RANGES);
    withSymbols(g_fHuge,68);
    g_fMono=withSymbols(io.Fonts->AddFontFromFileTTF(exists(adm)?adm:"C:\\Windows\\Fonts\\consola.ttf",17,nullptr,RANGES),17);
    // Desktop clock: the reference rice sets it in Rubik Bold. Nothing that heavy ships with
    // Windows in a geometric cut, so Segoe UI Black is the closest stand-in; loaded big and
    // scaled DOWN for the date lines, which stays crisp (scaling a 21px face up would not).
    { static const ImWchar DIG[] = {0x0020,0x007F, 0};
      const char* blk = exists("C:\\Windows\\Fonts\\seguibl.ttf") ? "C:\\Windows\\Fonts\\seguibl.ttf"
                      : (exists("C:\\Windows\\Fonts\\segoeuib.ttf") ? "C:\\Windows\\Fonts\\segoeuib.ttf" : reg);
      g_fClock=io.Fonts->AddFontFromFileTTF(blk,120,nullptr,DIG);
      if(exists("C:\\Windows\\Fonts\\bahnschrift.ttf")){
          g_fCond=io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\bahnschrift.ttf",64,nullptr,DIG);
          // The lock clock draws its hour at ~300px: from the 64px bake that was a 5x stretch, visibly soft and
          // stair-stepped. A 256px bake of ONLY the characters a clock uses keeps the atlas cost small.
          static const ImWchar CLK[] = { 0x0020,0x0020, 0x0030,0x003A, 0x0041,0x0041, 0x004D,0x004D, 0x0050,0x0050, 0 };
          g_fCondBig=io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\bahnschrift.ttf",256,nullptr,CLK); } }
    // Material Symbols, rasterised once at a size big enough that every on-screen use is a
    // DOWNscale - icons run 10-28px, so 48 stays crisp everywhere without a second atlas page.
    { static std::string s_mi=ExeDir()+"assets\\MaterialSymbols.ttf";
      static const ImWchar MR[]={ MICON_LO, MICON_HI, 0 };
      if(exists(s_mi.c_str())){
          ImFontConfig ic; ic.PixelSnapH=true; ic.OversampleH=2; ic.OversampleV=2;
          g_fIcon=io.Fonts->AddFontFromFileTTF(s_mi.c_str(),48.0f,&ic,MR); } }
    io.Fonts->AddFontDefault();
}

// ---- Super/Windows key opens the launcher -------------------------------------------------
// RegisterHotKey cannot claim a bare modifier, so this is a low-level keyboard hook (the same
// approach Cairo's MenuBarHotKeyService uses for its EnableWinKey option). We only act on the
// key-UP, and only if no other key was pressed while Win was held — so Win+E, Win+R, Win+Shift+S
// and friends all still work untouched. The key-up is swallowed so nothing else sees a bare Win.
static bool g_winDown=false, g_winCombo=false;
// Physical Win state as THIS hook last saw it, tracked whether or not "Super opens the launcher" is
// on. Used only to notice Windows disagreeing with it - see the stuck-Win repair in WinKeyHook.
static bool g_winSeenL=false, g_winSeenR=false;
// dwExtraInfo stamped on keys the repair below replays, so it can tell its own output from anyone
// else's. Rejecting ALL injected input instead would ignore remappers, KVM and RDP keyboards.

static HHOOK g_kbHook=nullptr;
// ---- the keyboard hook runs on its OWN thread ----
// A low-level hook is called on the thread that installed it, from inside that thread's message pump.
// It used to be the render thread, so every render stall held up every keypress on the PC - the
// ~260 ms bar_layer stalls were 260 ms of typing lag in every app, and the 111 s DwmFlush stall on
// 2026-09-26 froze the keyboard outright. Worse, Windows silently REMOVES a low-level hook that keeps
// missing LowLevelHooksTimeout, after which Super / Alt+Tab / macros stop working until a restart.
// The hook thread only decides whether to swallow a key; anything that touches render-thread state
// (the launcher, the workspace prediction, the macro engine) is queued and run by the render loop.
struct HookEv { int kind; DWORD vk; bool down; };      // 0 launcher toggle, 1 workspace prediction, 2 macro key, 3 close bar menus, 4 skip the intro
static std::mutex g_hookEvMtx;
static std::vector<HookEv> g_hookEvs;
static void HookPost(int kind,DWORD vk=0,bool down=false){
    { std::lock_guard<std::mutex> lk(g_hookEvMtx); g_hookEvs.push_back({kind,vk,down}); }
    if(g_swWake) PostMessageW(g_swWake,WM_NULL,0,0);
}
// What the hook needs to know about macros to decide a swallow, republished by the render thread.
static std::mutex g_macroSnapMtx;
static bool g_macroSnapArmed=false;
static std::vector<int> g_macroSnapSuppress;             // trigger vks of enabled, suppressing macros
static bool MacroWouldSwallow(DWORD vk){
    if(vk==VK_F8) return false;
    std::lock_guard<std::mutex> lk(g_macroSnapMtx);
    if(!g_macroSnapArmed) return false;
    for(int t:g_macroSnapSuppress) if((DWORD)t==vk) return true;
    return false;
}
// Render thread, once per loop pass: run what the hook queued, then refresh the macro snapshot.
static void HookDrain(){
    std::vector<HookEv> evs;
    { std::lock_guard<std::mutex> lk(g_hookEvMtx); evs.swap(g_hookEvs); }
    for(auto& e:evs){
        if(e.kind==0) LaunToggle();
        else if(e.kind==1) WsPredictKey(e.vk);
        else if(e.kind==3){ g_barMenu=false; g_trayOpen=false; g_sessShow=false; }
        else if(e.kind==4){ StvIntroSkip(); }
        else MacroOnKey(e.vk,e.down);
    }
    std::vector<int> sup;
    for(auto& m:g_macros) if(m.enabled && m.trigger!=0 && m.suppress) sup.push_back(m.trigger);
    std::lock_guard<std::mutex> lk(g_macroSnapMtx);
    g_macroSnapArmed=g_macroArmed; g_macroSnapSuppress.swap(sup);
}
static LRESULT CALLBACK WinKeyHook(int code,WPARAM w,LPARAM l);
static DWORD WINAPI KbHookThread(void*){
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_TIME_CRITICAL);   // it only ever answers the hook
    g_kbHook=SetWindowsHookExW(WH_KEYBOARD_LL,WinKeyHook,GetModuleHandleW(nullptr),0);
    MSG m; while(GetMessageW(&m,nullptr,0,0)>0){ TranslateMessage(&m); DispatchMessageW(&m); }
    if(g_kbHook) UnhookWindowsHookEx(g_kbHook);
    return 0;
}
static LRESULT CALLBACK WinKeyHook(int code,WPARAM w,LPARAM l){
    // ---- any other key while Win is held makes it a combo - decided FIRST ---------------------
    // Several branches below swallow their key and return early (Super+Tab does). The combo mark used
    // to sit at the very end, so after Super+Tab the Win release still counted as a bare tap and the
    // launcher opened on top of the overview.
    if(code==HC_ACTION && g_winDown && (w==WM_KEYDOWN||w==WM_SYSKEYDOWN)){
        DWORD vk=((KBDLLHOOKSTRUCT*)l)->vkCode;
        if(vk!=VK_LWIN && vk!=VK_RWIN) g_winCombo=true; }
    // ---- Alt+Tab -> our switcher ------------------------------------------------------------
    // Swallowing the Tab KEYDOWN is what stops Windows' own switcher from appearing. The Alt keyup
    // is never swallowed: eating a modifier's release leaves the OS believing it is still held and
    // every later keystroke becomes an Alt-combo (the same trap the Win key handling below hit).
    if(code==HC_ACTION && g_swEnable){
        KBDLLHOOKSTRUCT* k=(KBDLLHOOKSTRUCT*)l;
        bool down = (w==WM_KEYDOWN || w==WM_SYSKEYDOWN);
        bool up   = (w==WM_KEYUP   || w==WM_SYSKEYUP);
        bool altHeld = (GetAsyncKeyState(VK_MENU)&0x8000)!=0;
        auto wake=[](){ if(g_swWake) PostMessageW(g_swWake,WM_NULL,0,0); };
        // INJECTED input is accepted here on purpose: Remote Desktop, KVM software and on-screen
        // keyboards all deliver Alt+Tab that way, and we never synthesise Tab ourselves, so there
        // is no feedback loop to guard against.
        if(k->vkCode==VK_TAB && down && altHeld){
            g_swReq = (GetAsyncKeyState(VK_SHIFT)&0x8000)? 2 : 1;
            wake(); return 1;                                  // never let the OS switcher through
        }
        if(g_swShow){
            if((k->vkCode==VK_LMENU||k->vkCode==VK_RMENU) && up){ g_swReq=3; wake(); }   // let it pass
            else if(down && k->vkCode==VK_ESCAPE){ g_swReq=4; wake(); return 1; }
            else if(down && (k->vkCode==VK_RIGHT||k->vkCode==VK_DOWN)){ g_swReq=1; wake(); return 1; }
            else if(down && (k->vkCode==VK_LEFT ||k->vkCode==VK_UP)){   g_swReq=2; wake(); return 1; }
            else if(down && k->vkCode==VK_RETURN){ g_swReq=3; wake(); return 1; }
        }
    }
    // ---- Super+Tab -> the overview -----------------------------------------------------------
    // Windows reserves Win+Tab for Task View and refuses to register it, so it is taken here, where
    // the hook sees the key before the shell does. Only Tab is swallowed: eating the Win key's
    // release would leave Windows believing it is still held, which is the trap documented below.
    // g_komoLive matters: without komorebi there are no workspaces to show, and swallowing the key
    // would leave the user with neither the overview NOR Task View.
    if(code==HC_ACTION && g_ovEnable && g_ovSuperTab && g_komoLive.load() && (w==WM_KEYDOWN||w==WM_SYSKEYDOWN)){
        KBDLLHOOKSTRUCT* k=(KBDLLHOOKSTRUCT*)l;
        if(k->vkCode==VK_TAB && ((GetAsyncKeyState(VK_LWIN)&0x8000)||(GetAsyncKeyState(VK_RWIN)&0x8000))){
            g_ovReq.store(g_ovWant? 1 : 5);
            if(g_ovWake) PostMessageW(g_ovWake,WM_NULL,0,0);
            return 1;
        }
    }
    // ---- the overview owns the keyboard while it is up --------------------------------------
    // It is a WS_EX_NOACTIVATE layer, so it never receives keys the normal way. The switcher solves
    // that with this same hook; the requests are handed to the render thread rather than acted on
    // here, because everything the overview touches belongs to that thread.
    if(code==HC_ACTION && g_ovWant){
        KBDLLHOOKSTRUCT* k=(KBDLLHOOKSTRUCT*)l;
        if(w==WM_KEYDOWN || w==WM_SYSKEYDOWN){
            if(k->vkCode==VK_ESCAPE){ g_ovReq.store(1); return 1; }
            if(k->vkCode==VK_LEFT) { g_ovReq.store(2); return 1; }
            if(k->vkCode==VK_RIGHT){ g_ovReq.store(3); return 1; }
            if(k->vkCode==VK_RETURN||k->vkCode==VK_SPACE){ g_ovReq.store(4); return 1; }
        }
    }
    // ---- Escape closes the bar's right-click menu, the tray panel and the power menu ----------
    // The bar is WS_EX_NOACTIVATE, so it never has the keyboard and its own Escape check never fired:
    // the menu stayed up until something was clicked. The key is NOT swallowed - it still reaches the app.
    if(code==HC_ACTION && (w==WM_KEYDOWN||w==WM_SYSKEYDOWN) && ((KBDLLHOOKSTRUCT*)l)->vkCode==VK_ESCAPE && (g_barMenu||g_trayOpen||g_sessShow))
        HookPost(3);
    // ---- any key skips the Strive intro (not swallowed) --------------------------------------
    if(code==HC_ACTION && (w==WM_KEYDOWN||w==WM_SYSKEYDOWN) && g_stvIntroAt && !g_stvIntroSkipAt) HookPost(4);
    // ---- keyboard desktop switches show on the bar at once (see WsPredictKey) ----------------
    if(code==HC_ACTION){
        KBDLLHOOKSTRUCT* k=(KBDLLHOOKSTRUCT*)l;
        if((w==WM_KEYDOWN||w==WM_SYSKEYDOWN) && k->dwExtraInfo!=AETHER_KEY_TAG) HookPost(1,k->vkCode);
    }
    // ---- macros -----------------------------------------------------------------------------
    // Rides the shell's existing hook rather than installing a second global one. INJECTED events
    // are ignored here (unlike the Alt+Tab branch above) because this engine DOES synthesise keys -
    // without that guard a macro whose steps include its own trigger would retrigger itself forever.
    if(code==HC_ACTION){
        KBDLLHOOKSTRUCT* k=(KBDLLHOOKSTRUCT*)l;
        if(!(k->flags & LLKHF_INJECTED) || g_macroAllowInjected){
            bool down = (w==WM_KEYDOWN || w==WM_SYSKEYDOWN);
            bool up   = (w==WM_KEYUP   || w==WM_SYSKEYUP);
            if(down||up){ HookPost(2,k->vkCode,down); if(MacroWouldSwallow(k->vkCode)) return 1; }
        }
    }
    // ---- stuck-Win repair ----------------------------------------------------------------------
    // Tester report: after Super+S opened Flow Launcher and an app was launched from it, every plain
    // key behaved as Super+<key> ("Q quits the app") until they went back to an empty desktop. That
    // is Windows' key state believing Win is still down after it was released - some hook further
    // along the chain ate the key-up. We saw the release, so when a normal key arrives while Windows
    // still reports Win held, give Windows the key-up it missed and replay the key after it, so the
    // key lands as itself instead of as a Win-combo. It only acts when the two disagree: a genuinely
    // held Win (which we saw go down) is never touched.
    if(code==HC_ACTION){
        KBDLLHOOKSTRUCT* k=(KBDLLHOOKSTRUCT*)l;
        bool down = (w==WM_KEYDOWN || w==WM_SYSKEYDOWN);
        bool up   = (w==WM_KEYUP   || w==WM_SYSKEYUP);
        if(k->vkCode==VK_LWIN){ if(down) g_winSeenL=true; else if(up) g_winSeenL=false; }
        else if(k->vkCode==VK_RWIN){ if(down) g_winSeenR=true; else if(up) g_winSeenR=false; }
        else if(down && k->dwExtraInfo!=AETHER_KEY_TAG){
            bool staleL = !g_winSeenL && (GetAsyncKeyState(VK_LWIN)&0x8000);
            bool staleR = !g_winSeenR && (GetAsyncKeyState(VK_RWIN)&0x8000);
            if(staleL || staleR){
                INPUT in[3]={}; int n=0;
                if(staleL){ in[n].type=INPUT_KEYBOARD; in[n].ki.wVk=VK_LWIN; in[n].ki.dwFlags=KEYEVENTF_KEYUP|KEYEVENTF_EXTENDEDKEY; in[n].ki.dwExtraInfo=AETHER_KEY_TAG; n++; }
                if(staleR){ in[n].type=INPUT_KEYBOARD; in[n].ki.wVk=VK_RWIN; in[n].ki.dwFlags=KEYEVENTF_KEYUP|KEYEVENTF_EXTENDEDKEY; in[n].ki.dwExtraInfo=AETHER_KEY_TAG; n++; }
                in[n].type=INPUT_KEYBOARD; in[n].ki.wVk=(WORD)k->vkCode; in[n].ki.wScan=(WORD)k->scanCode;
                in[n].ki.dwFlags=(k->flags&LLKHF_EXTENDED)?KEYEVENTF_EXTENDEDKEY:0; in[n].ki.dwExtraInfo=AETHER_KEY_TAG; n++;
                SendInput(n,in,sizeof(INPUT));
                return 1;                                   // the replay above is this key
            }
        }
    }
    if(code==HC_ACTION && g_winKeyLauncher){
        KBDLLHOOKSTRUCT* k=(KBDLLHOOKSTRUCT*)l;
        bool isWin = (k->vkCode==VK_LWIN || k->vkCode==VK_RWIN);
        bool down  = (w==WM_KEYDOWN || w==WM_SYSKEYDOWN);
        bool up    = (w==WM_KEYUP   || w==WM_SYSKEYUP);
        if(isWin){
            if(down){ if(!g_winDown){ g_winDown=true; g_winCombo=false; } }
            else if(up){
                bool solo = g_winDown && !g_winCombo;
                g_winDown=false;
                // Do NOT swallow. Swallowing only the key-up leaves Windows believing Win is still
                // held, which turns every later keystroke into a Win+combo.
                // Instead, cancel the Start menu the way Windows itself decides it: it only opens on
                // a BARE Win tap, so we inject a harmless Ctrl tap while Win is still down and the
                // tap stops being bare. Without this, running alongside Explorer opened our launcher
                // AND the Start menu together (reported on another machine — this box is the shell,
                // so there was no Start menu here to reveal it).
                if(solo){
                    // The action is a TOGGLE, so anything that delivers the Win key-up twice makes
                    // the launcher open and shut in the same tap - which is exactly what a bare
                    // Super press looked like. Key-ups can arrive doubled from remappers, KVM and
                    // RDP stacks, and from the injected Ctrl below re-entering this hook. A short
                    // debounce makes the tap idempotent no matter which of those is responsible.
                    static ULONGLONG s_lastTap=0;
                    ULONGLONG nowTap=GetTickCount64();
                    // Masking the tap. The Ctrl tap used to be sent from here and let the real release
                    // through - but input sent from inside a hook is queued BEHIND the event being
                    // hooked, so Windows saw a bare Win press-release first and opened Start/Search
                    // anyway. Measured: after one Super tap the foreground window was "Search", so the
                    // launcher was on screen while typing went to an invisible search box. Now the real
                    // release is held back and re-sent AFTER an unassigned mask key (vk 0xE8, the key
                    // AutoHotkey uses for exactly this), so Windows sees Win+<nothing> - never a bare
                    // tap - and the key state stays consistent because the release is always delivered.
                    INPUT in[3]={};
                    in[0].type=INPUT_KEYBOARD; in[0].ki.wVk=0xE8; in[0].ki.dwExtraInfo=AETHER_KEY_TAG;
                    in[1]=in[0]; in[1].ki.dwFlags=KEYEVENTF_KEYUP;
                    in[2].type=INPUT_KEYBOARD; in[2].ki.wVk=(WORD)k->vkCode;
                    in[2].ki.dwFlags=KEYEVENTF_KEYUP|KEYEVENTF_EXTENDEDKEY; in[2].ki.dwExtraInfo=AETHER_KEY_TAG;
                    SendInput(3,in,sizeof(INPUT));
                    // 100 ms still absorbs a doubled key-up from a remapper (those land within a few ms)
                    // without eating a person's quick second tap, which the old 250 ms did.
                    if(nowTap-s_lastTap>=100){ s_lastTap=nowTap; HookPost(0); }
                    return 1;                              // the release was re-sent above, after the mask
                }
            }
        } else if(down && g_winDown){
            // INJECTED input counts here, same as the Alt+Tab branch above accepts it. The guard
            // that used to reject it was there to stop our OWN injected Ctrl from registering as a
            // combo - but g_winDown is already cleared before that SendInput runs, so it never
            // could. All the guard actually did was make every injected Win+<key> look like a bare
            // Win tap: Windows performed the combo AND Aether toggled the launcher on top of it.
            // That hits remappers, KVM switches, RDP, on-screen keyboards and macro tools - and it
            // is how a scripted Ctrl+Win+D became "new virtual desktop plus a launcher toggle".
            g_winCombo=true;                                   // Win+<something> stays the OS's
        }
    }
    return CallNextHookEx(g_kbHook,code,w,l);
}

// ---- IPC: hand a command to an ALREADY-RUNNING shell, then exit ------------------------------
// `Aether.exe -s lock`, `Aether.exe --cmd scheme`, etc. This is the "caelestia shell -s ..." nicety.
// The window class is not CS_GLOBALCLASS, so FindWindow by class returns 0 from another process
// (see the taskbar notes) - enumerate instead and confirm the owner really is an Aether.exe.
static HWND FindRunningShell(){
    struct Ctx{ HWND h=nullptr; } ctx;
    EnumWindows([](HWND h,LPARAM lp)->BOOL{
        wchar_t t[64]={0}; GetWindowTextW(h,t,63);
        if(wcscmp(t,L"Aether")!=0) return TRUE;
        DWORD pid=0; GetWindowThreadProcessId(h,&pid);
        if(pid==GetCurrentProcessId()) return TRUE;               // never talk to ourselves
        HANDLE ph=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!ph) return TRUE;
        wchar_t path[MAX_PATH]={0}; DWORD n=MAX_PATH;
        BOOL ok=QueryFullProcessImageNameW(ph,0,path,&n); CloseHandle(ph);
        if(ok){ const wchar_t* b=wcsrchr(path,L'\\');
            if(b && _wcsicmp(b+1,L"Aether.exe")==0){ ((Ctx*)lp)->h=h; return FALSE; } }
        return TRUE;
    },(LPARAM)&ctx);
    return ctx.h;
}
static bool SendShellCommand(const std::string& name){
    HWND h=FindRunningShell(); if(!h) return false;
    COPYDATASTRUCT cds{}; cds.dwData=AETHER_IPC;
    cds.cbData=(DWORD)(name.size()+1); cds.lpData=(void*)name.c_str();
    DWORD_PTR res=0;
    return SendMessageTimeoutW(h,WM_COPYDATA,0,(LPARAM)&cds,SMTO_ABORTIFHUNG,4000,&res)!=0;
}

// The virtual-screen layers (bar, desktop, snipper, decorations) are placed ONLY when the monitor
// layout changes. If that path is missed - or runs while the window and its swapchain disagree -
// the layers stay mis-sized until the shell is restarted. Seen for real: both monitors' bubbles
// squeezed into the top-left quarter of one screen with the bar torn into two pieces, and a restart
// was the only cure. Verifying costs a GetWindowRect; the repair only runs when it is actually
// wrong, so this is free in the normal case and the stuck state can no longer persist.
static void EnsureOverlayGeometry(){
    const int vw=g_vs.right-g_vs.left, vh=g_vs.bottom-g_vs.top;
    if(vw<16||vh<16) return;
    auto chk=[&](HWND h,IDXGISwapChain1* sc,ID3D11RenderTargetView** rtv,int x,int y,int w,int ht){
        if(!h||w<16||ht<16) return;
        RECT c{}; if(!GetWindowRect(h,&c)) return;
        if(c.left==x && c.top==y && (c.right-c.left)==w && (c.bottom-c.top)==ht) return;
        MoveOverlay(h,sc,rtv,x,y,w,ht);
    };
    chk(g_barHwnd, g_barSc, &g_barRtv, g_vs.left,g_vs.top,vw,vh);
    chk(g_deskHwnd,g_deskSc,&g_deskRtv,g_vs.left,g_vs.top,vw,vh);
    if(!g_snipActive) chk(g_snipHwnd,g_snipSc,&g_snipRtv,g_vs.left,g_vs.top,vw,vh);
    chk(g_decoHwnd,g_decoSc,&g_decoRtv,g_dvs.left,g_dvs.top,
        g_dvs.right-g_dvs.left,g_dvs.bottom-g_dvs.top);
}
static void UiDump(){
    FILE* f=fopen((ExeDir()+"uidump.txt").c_str(),"a"); if(!f) return;
    ULONGLONG now=GetTickCount64();
    ImVec2 mp(-1,-1);
    if(g_ctxBar){ ImGuiContext* was=ImGui::GetCurrentContext(); ImGui::SetCurrentContext(g_ctxBar);
                  mp=ImGui::GetIO().MousePos; ImGui::SetCurrentContext(was); }
    POINT cp; GetCursorPos(&cp);
    fprintf(f,"barFly=%d barFlyMon=%d barFlyPin=%d hoverAge=%llums trayMenuKey=%p | bar io.MousePos=(%.0f,%.0f) cursor=(%ld,%ld) uiScale=%.3f\n",
            g_barFly,g_barFlyMon,g_barFlyPin,(unsigned long long)(now-g_barFlyHoverTick),g_trayMenuKey,
            mp.x,mp.y,cp.x,cp.y,g_uiScale);
    { static ULONGLONG t0=GetTickCount64();
      fprintf(f,"  ws t=%llu: wsCur=%d wsCount=%d ring0.cur=%d ring1.cur=%d pendTarget=%d pendIn=%lldms keysBusy=%d barFrames=%llu\n",
              (unsigned long long)(now-t0),g_wsCur,g_wsCount,g_wsRing[0].cur,g_wsRing[1].cur,g_wsPendTarget,
              (long long)g_wsPendUntil-(long long)now,(int)g_wsKeysBusy.load(),(unsigned long long)g_uiDumpBarFrames); }
    { std::lock_guard<std::mutex> lk(g_wallMtx); int we=0; for(auto& w:g_walls) if(!w.weProj.empty()) we++;
      std::wstring exe; { std::lock_guard<std::mutex> lk2(g_weMtx); exe=g_weExe; }
      fprintf(f,"  deskReorders=%d%c",g_deskReorders,10);
      fprintf(f,"  walls: total=%d wallpaperEngine=%d sel=%d weExe='%s' monLive=[%d,%d] weTr phase=[%d,%d]\n",(int)g_walls.size(),we,g_wallSel,
              W2U8(exe).c_str(),(int)g_monLive[0],(int)g_monLive[1],g_weTr[0].phase,g_weTr[1].phase); }
    fprintf(f,"  snip: active=%d why='%s' window visible=%d locked=%d vs=(%ld,%ld)-(%ld,%ld)\n",
            (int)g_snipActive,g_snipWhy,(int)IsWindowVisible(g_snipHwnd),(int)ShellLocked(),
            g_vs.left,g_vs.top,g_vs.right,g_vs.bottom);
    fclose(f);
}
// --dumptest: raise a harmless exception inside a guard and hand it to the real dump writer, so the
// "send me the .dmp" instruction to testers is backed by a file that was actually produced.
static int DumpSelfTest(){
    __try { RaiseException(0xE0AE7E57,0,0,nullptr); }
    __except(WriteCrashDump(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
