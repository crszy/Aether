// Aether - window decorations.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =============================================================================================
// WINDOW DECORATIONS — a Linux-style titlebar for the focused window. We are the shell, so we
// decorate windows the way a WM does: a bar across the top of the active window with the app icon
// on the left, the title CENTRED, and minimize / maximize / close on the right. Drag it to move
// the window, double-click to (un)maximize. Only the focused window is decorated, so it never
// fights z-order or covers a window that is behind another. Fully reversible (a setting).
// =============================================================================================
static HWND g_decoHwnd=nullptr; static IDXGISwapChain1* g_decoSc=nullptr; static ID3D11RenderTargetView* g_decoRtv=nullptr;
static IDCompositionTarget* g_decoTgt=nullptr; static IDCompositionVisual* g_decoVis=nullptr; static ImGuiContext* g_ctxDeco=nullptr;
static RECT  g_decoRect={0,0,0,0};          // the bar's hit rect (overlay-logical px)
static HWND  g_decoTarget=nullptr;          // the window currently decorated
static bool  g_decoDrag=false; static POINT g_decoGrab={0,0};
// Where the drag is moving the window TO. The bar used to be drawn from DecoFrame(), i.e. where the
// window was when the frame started, while SetWindowPos moved it later in that same frame - so the
// bar trailed the window by a frame or two and the real caption showed through the gap. That is the
// "drags around like it is hovering over something". Now the drag computes the destination first,
// draws the bar there, and moves the window to exactly that rect.
static bool  g_decoDragRectValid=false; static RECT g_decoDragRect={0,0,0,0};
static const float DECOH=34.0f;             // bar height (logical)

static std::wstring DecoExe(HWND h){
    DWORD pid=0; GetWindowThreadProcessId(h,&pid);
    HANDLE pr=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid); std::wstring exe;
    if(pr){ wchar_t path[MAX_PATH]={0}; DWORD n=MAX_PATH;
        if(QueryFullProcessImageNameW(pr,0,path,&n)){ std::wstring w=path;
            size_t s=w.find_last_of(L'\\'); exe=(s==std::wstring::npos)?w:w.substr(s+1);
            for(auto&c:exe)c=towlower(c); }
        CloseHandle(pr); }
    return exe;
}
// Apps whose top strip holds TABS or a MENU you actually use (browsers, the new Notepad, FL Studio,
// VS Code, terminals, Explorer). For these we keep the app's own bar and only restyle its window
// buttons. Everything else (Discord, Steam, plain apps) gets our full centred-title bar.
static bool DecoTabbed(HWND h){
    std::wstring exe=DecoExe(h);
    static const wchar_t* tabbed[]={
        L"chrome.exe",L"msedge.exe",L"firefox.exe",L"brave.exe",L"opera.exe",L"opera_gx.exe",
        L"vivaldi.exe",L"arc.exe",L"helium.exe",L"thorium.exe",L"chromium.exe",L"iron.exe",
        L"librewolf.exe",L"waterfox.exe",L"zen.exe",
        L"notepad.exe",L"fl64.exe",L"fl.exe",L"code.exe",L"code - insiders.exe",
        L"windowsterminal.exe",L"wt.exe",L"explorer.exe",L"notepad++.exe",L"sublime_text.exe"};
    for(auto e:tabbed) if(exe==e) return true;
    return false;
}
static ImU32 SampleScreenPixel(int x,int y){
    HDC dc=GetDC(nullptr); COLORREF c=dc?GetPixel(dc,x,y):CLR_INVALID; if(dc)ReleaseDC(nullptr,dc);
    if(c==CLR_INVALID) return IM_COL32(0x2b,0x2b,0x2b,255);
    return IM_COL32(GetRValue(c),GetGValue(c),GetBValue(c),255);
}
// Per-app fitted decoration profiles (populated for the apps actually on this PC + common ones).
// mode: 0 = our full centred-title bar, 1 = buttons-only over the app's own bar, 2 = skip entirely.
// btnW/btnH/top = the button-cluster geometry to fit that app's native controls; bg = force a bar
// colour (0 = sample the app's own titlebar); glyphLight = glyph colour when bg is forced.
struct DecoProfile { const wchar_t* exe; int mode; float btnW,btnH,top; ImU32 bg; int glyphLight; };
static const DecoProfile DECO_PROFILES[] = {
    // exe                    mode  bW  bH top   forced bg (0=sample)              glyphLight
    { L"steamwebhelper.exe",   1,   30, 24, 2,  IM_COL32(0x17,0x1a,0x21,255),      1 },  // Steam
    { L"steam.exe",            1,   30, 24, 2,  IM_COL32(0x17,0x1a,0x21,255),      1 },
    { L"foracord.exe",         0,   0,0,0,       0,                                 0 },  // your Discord: full bar
    { L"discord.exe",          0,   0,0,0,       0,                                 0 },
    { L"lghub.exe",            1,   30, 26, 2,  0,                                 1 },  // Logitech G HUB
    { L"chrome.exe",           1,   46, 36, 0,  0,                                 0 },  // Helium / Chrome
    { L"msedge.exe",           1,   46, 36, 0,  0,                                 0 },
    { L"firefox.exe",          1,   46, 40, 0,  0,                                 0 },
    { L"brave.exe",            1,   46, 36, 0,  0,                                 0 },
    { L"windowsterminal.exe",  1,   46, 36, 0,  0,                                 1 },  // Windows Terminal
    { L"wt.exe",               1,   46, 36, 0,  0,                                 1 },
    { L"notepad.exe",          1,   46, 40, 0,  0,                                 0 },  // new Notepad
    { L"code.exe",             1,   46, 30, 0,  0,                                 1 },  // VS Code
    { L"spotify.exe",          1,   32, 26, 4,  IM_COL32(0x12,0x12,0x12,255),      1 },  // Spotify
    { L"rustdesk.exe",         2,   0,0,0,      0,                                 0 },  // tray helper: skip
};
static const DecoProfile* DecoProfileFor(const std::wstring& exe){
    for(auto& p:DECO_PROFILES) if(exe==p.exe) return &p;
    return nullptr;
}
static bool DecoManaged(HWND h){
    if(!h||!IsWindow(h)||!IsWindowVisible(h)||IsIconic(h)) return false;
    if(GetWindow(h,GW_OWNER)) return false;                 // tool/owned popups
    LONG ex=GetWindowLongW(h,GWL_EXSTYLE);
    if(ex&WS_EX_TOOLWINDOW) return false;
    LONG st=GetWindowLongW(h,GWL_STYLE);
    if(!(st&WS_CAPTION) && (ex&WS_EX_NOACTIVATE)) return false;
    DWORD pid=0; GetWindowThreadProcessId(h,&pid);
    if(pid==GetCurrentProcessId()) return false;            // our own shell windows
    wchar_t cls[64]={0}; GetClassNameW(h,cls,63);
    if(!wcscmp(cls,L"Progman")||!wcscmp(cls,L"WorkerW")||!wcscmp(cls,L"Shell_TrayWnd")||
       !wcscmp(cls,L"Shell_SecondaryTrayWnd")||!wcscmp(cls,L"Windows.UI.Core.CoreWindow")||
       !wcscmp(cls,L"ForegroundStaging")||!wcscmp(cls,L"XamlExplorerHostIslandWindow")) return false;
    int cloaked=0; DwmGetWindowAttribute(h,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
    if(cloaked) return false;
    return true;
}
// the window's visible frame (excludes the invisible resize border / drop shadow)
static RECT DecoFrame(HWND h){
    RECT r; if(FAILED(DwmGetWindowAttribute(h,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof(r)))) GetWindowRect(h,&r);
    return r;
}
// The height of the window's REAL title bar, in screen px: the gap between the visible frame top
// and the client area. Drawing a fixed 34px band over a caption that is not 34px is most of why the
// old bar read as something laid ON the window rather than being its bar - the app's own caption
// showed as a sliver above or below ours. Client-side-decorated apps (Discord, Steam, browsers)
// have no non-client caption at all and report 0; those keep the nominal height.
static float DecoCaptionH(HWND h){
    RECT fr=DecoFrame(h);
    RECT cr; POINT o{0,0};
    if(!GetClientRect(h,&cr) || !ClientToScreen(h,&o)) return 0.0f;
    float measured=(float)(o.y-fr.top);
    // ClientToScreen(0,0) lands BELOW a menu bar, so the measurement alone would have us cover an
    // app's File/Edit/View row as if it were title bar. AdjustWindowRectEx with bMenu = FALSE says
    // how tall the caption+border SHOULD be for this window's style, with no menu in it; take the
    // smaller of the two so a menu is never swallowed.
    RECT ideal{0,0,100,100};
    LONG st=GetWindowLongW(h,GWL_STYLE), ex=GetWindowLongW(h,GWL_EXSTYLE);
    float styled=0.0f;
    if(AdjustWindowRectEx(&ideal,st,FALSE,ex)) styled=(float)(-ideal.top);
    float capt = (styled>4.0f && styled<measured)? styled : measured;
    if(capt<4.0f || capt>200.0f) return 0.0f;      // 0 = no server-side caption to match
    return capt;
}
// Win11 rounds top-level corners, so a square-cornered band across the top leaves the app's own
// rounded corner peeking out at each end. Ask DWM what this window is actually doing.
static float DecoCornerRadius(HWND h){
    if(IsZoomed(h)) return 0.0f;                   // maximised windows are square
    int pref=0;                                    // DWMWA_WINDOW_CORNER_PREFERENCE = 33
    if(SUCCEEDED(DwmGetWindowAttribute(h,33,&pref,sizeof(pref)))){
        if(pref==1) return 0.0f;                   // DWMWCP_DONOTROUND
        if(pref==3) return 4.0f;                   // DWMWCP_ROUNDSMALL
    }
    return 8.0f;                                   // DWMWCP_DEFAULT / ROUND on Win11
}
struct DecoCache {
    HWND hwnd=nullptr; std::wstring exe; const DecoProfile* prof=nullptr; int mode=0;
    float captH=0.0f, crad=0.0f; bool zoomed=false; ULONGLONG at=0;
};
static DecoCache g_decoCache;
static const DecoCache& DecoResolve(HWND h){
    bool zoom=IsZoomed(h)!=FALSE;
    ULONGLONG now=GetTickCount64();
    // re-resolve on a different window, on maximise/restore, or every 500ms so a style change lands
    if(g_decoCache.hwnd!=h || g_decoCache.zoomed!=zoom || now-g_decoCache.at>500){
        g_decoCache.hwnd=h; g_decoCache.zoomed=zoom; g_decoCache.at=now;
        g_decoCache.exe=DecoExe(h);
        g_decoCache.prof=DecoProfileFor(g_decoCache.exe);
        bool tabbed=false;
        if(!g_decoCache.prof){                      // DecoTabbed re-reads the exe; use the one we have
            static const wchar_t* tabbedExe[]={
                L"chrome.exe",L"msedge.exe",L"firefox.exe",L"brave.exe",L"opera.exe",L"opera_gx.exe",
                L"vivaldi.exe",L"arc.exe",L"helium.exe",L"thorium.exe",L"chromium.exe",L"iron.exe",
                L"librewolf.exe",L"waterfox.exe",L"zen.exe",
                L"notepad.exe",L"fl64.exe",L"fl.exe",L"code.exe",L"code - insiders.exe",
                L"windowsterminal.exe",L"wt.exe",L"explorer.exe",L"notepad++.exe",L"sublime_text.exe"};
            for(auto e:tabbedExe) if(g_decoCache.exe==e){ tabbed=true; break; }
        }
        g_decoCache.mode = g_decoCache.prof? g_decoCache.prof->mode : (tabbed?1:0);
        g_decoCache.captH= DecoCaptionH(h);
        g_decoCache.crad = DecoCornerRadius(h);
    }
    return g_decoCache;
}
static ID3D11ShaderResourceView* DecoIcon(HWND h){
    for(auto& a:g_dockApps) if(a.hwnd==h) return a.icon;
    return nullptr;
}

// ---------------------------------------------------------------- --perflog ----
// Frame accounting. Buckets are accumulated per frame and dumped every 2 seconds with the WORST
// frame seen in that window, because a stutter is a tail-latency problem: a mean of 3ms tells you
// nothing when one frame in fifty takes 90ms.
namespace perf {
    static bool on=false;
    struct Bucket{ const char* name; double ms; int n; double worst; };
    static Bucket B[]={ {"deco",0,0,0},{"bar",0,0,0},{"drawer",0,0,0},{"desk",0,0,0},{"side",0,0,0},
                        {"notif",0,0,0},{"dock",0,0,0},{"switcher",0,0,0},{"settings",0,0,0},
                        {"present",0,0,0},{"frame",0,0,0} };
    enum { DECO=0,BAR,DRAWER,DESK,SIDE,NOTIF,DOCK,SWITCH,SETTINGS,PRESENT,FRAME };
    static LARGE_INTEGER freq{};
    static double Now(){ if(!freq.QuadPart) QueryPerformanceFrequency(&freq);
                         LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart*1000.0/freq.QuadPart; }
    struct Scope{ int i; double t0;
        Scope(int idx):i(idx),t0(on?Now():0){}
        ~Scope(){ if(!on) return; double d=Now()-t0; B[i].ms+=d; B[i].n++; if(d>B[i].worst)B[i].worst=d; } };
    static void Tick(){
        if(!on) return;
        static double last=0; double n=Now();
        if(last==0){ last=n; return; }
        if(n-last < 2000.0) return;
        FILE* f=fopen("perflog.txt","a");
        if(f){ fprintf(f,"---- %.0f ms window ----\n",n-last);
            for(auto& b:B) if(b.n) fprintf(f,"  %-9s calls=%-5d avg=%6.2fms  WORST=%7.2fms  total=%7.1fms\n",
                                           b.name,b.n,b.ms/b.n,b.worst,b.ms);
            fclose(f); }
        for(auto& b:B){ b.ms=0; b.n=0; b.worst=0; }
        last=n;
    }
}
#define PERF(idx) perf::Scope _ps_##idx(perf::idx)
// Frame pacing with a way out: PaceFlush (src/services/Pace.h) is DwmFlush capped at 50 ms. While DWM
// keeps time this is the same pacing as before; when it stops, the shell drops to ~20 fps instead of
// freezing - which is what every HANG in errors.log on 2026-09-26 was (DwmFlush blocked 10-111 s).
namespace pace {
    static void Wait(){
        static int missed=0; static double stuckSince=0;
        if(PaceFlush(50)){
            if(missed>=3) AetherLog("DwmFlush recovered after about %.0f ms (render loop kept running)",perf::Now()-stuckSince);
            missed=0; return; }
        if(++missed==3){ stuckSince=perf::Now()-150.0; AetherLog("DwmFlush stopped returning - pacing on a 50 ms timeout until the compositor comes back"); }
    }
}

static void DrawDeco(){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    HWND fg=GetForegroundWindow();
    // an exclusive-fullscreen app (a game) covering a whole monitor is never decorated
    bool skip=false;
    if(fg){ RECT fr; if(GetWindowRect(fg,&fr)) for(auto& m:g_mons)
        if(fr.left<=m.rc.left&&fr.top<=m.rc.top&&fr.right>=m.rc.right&&fr.bottom>=m.rc.bottom){
            LONG st=GetWindowLongW(fg,GWL_STYLE); if(!(st&WS_CAPTION)){ skip=true; break; } } }
    if(wcsstr(GetCommandLineW(),L"--decolog")){
        static ULONGLONG last=0;
        if(GetTickCount64()-last>1000){ last=GetTickCount64();
            FILE* lf=fopen("decolog.txt","a");
            if(lf){ std::wstring ex=fg?DecoExe(fg):L"";
                fprintf(lf,"on=%d skip=%d managed=%d fg=%p exe=%s captH=%.1f\n",
                        (int)g_decoOn,(int)skip,(int)(fg?DecoManaged(fg):false),(void*)fg,
                        W2U8(ex).c_str(), fg?DecoCaptionH(fg):-1.0f);
                fclose(lf);} }
    }
    if(!g_decoOn || skip || !DecoManaged(fg)){ g_decoTarget=nullptr; g_decoRect=RECT{0,0,0,0}; g_decoDrag=false; return; }
    g_decoTarget=fg;
    RECT R=DecoFrame(fg);
    // While WE are dragging, the authoritative rect is the one we are about to set, not the one the
    // window still has. Drawing from this keeps bar and window in the same composed frame.
    if(g_decoDrag && g_decoDragRectValid) R=g_decoDragRect;
    // map screen (virtual) px -> this overlay's logical px. The deco window spans g_dvs (EVERY
    // monitor), not g_vs, so this must not use g_vs or the bar lands outside the window on any
    // display the user has switched the shell off for.
    auto toL=[&](LONG x,LONG y){ return V((x-g_dvs.left)/g_uiScale,(y-g_dvs.top)/g_uiScale); };
    bool zoomed=IsZoomed(fg);
    bool click=io.MouseClicked[0], down=io.MouseDown[0], rel=io.MouseReleased[0];
    bool dbl=false; { static double lc=0; static ImVec2 lp=V(-9,-9);
        if(click){ double n=ImGui::GetTime(); if(n-lc<0.32&&fabsf(io.MousePos.x-lp.x)<6&&fabsf(io.MousePos.y-lp.y)<6) dbl=true; lc=n; lp=io.MousePos; } }

    // one button renderer, shared by both modes: draws a GNOME min/max/close glyph at c
    // Caption marks follow the shell's icon set, so the headerbar agrees with the bar, the session
    // screen and the tray chevron. Breeze's window decorations do NOT use the Windows _ / box / X:
    // minimise is a chevron DOWN, maximise a chevron UP, restore a diamond, close a cross - straight
    // off breeze/actions/16/window-{minimize,maximize,restore,close}.svg.
    auto drawCtrl=[&](ImVec2 c,float r,int kind,ImU32 glyph,ImU32 hoverBg)->bool{
        bool hov=fabsf(io.MousePos.x-c.x)<r&&fabsf(io.MousePos.y-c.y)<r;
        if(hov) dl->AddCircleFilled(c,r,hoverBg);
        ImU32 gl=(kind==2&&hov)?IM_COL32(255,255,255,255):glyph;
        if(g_iconSet==ICONSET_BREEZE){
            const float e=r*0.42f, t=1.7f;
            if(kind==0){                                   // minimise: chevron down
                dl->AddLine(V(c.x-e,c.y-e*0.42f),V(c.x,c.y+e*0.48f),gl,t);
                dl->AddLine(V(c.x,c.y+e*0.48f),V(c.x+e,c.y-e*0.42f),gl,t);
            } else if(kind==1){
                if(zoomed){                                // restore: diamond outline
                    float d=e*0.95f;
                    dl->AddLine(V(c.x,c.y-d),V(c.x+d,c.y),gl,t); dl->AddLine(V(c.x+d,c.y),V(c.x,c.y+d),gl,t);
                    dl->AddLine(V(c.x,c.y+d),V(c.x-d,c.y),gl,t); dl->AddLine(V(c.x-d,c.y),V(c.x,c.y-d),gl,t);
                } else {                                   // maximise: chevron up
                    dl->AddLine(V(c.x-e,c.y+e*0.42f),V(c.x,c.y-e*0.48f),gl,t);
                    dl->AddLine(V(c.x,c.y-e*0.48f),V(c.x+e,c.y+e*0.42f),gl,t);
                }
            } else {                                       // close: cross
                dl->AddLine(V(c.x-e,c.y-e),V(c.x+e,c.y+e),gl,t);
                dl->AddLine(V(c.x+e,c.y-e),V(c.x-e,c.y+e),gl,t);
            }
            return hov;
        }
        if(kind==0) dl->AddLine(V(c.x-5,c.y+3),V(c.x+5,c.y+3),gl,1.6f);
        else if(kind==1){ if(zoomed){ dl->AddRect(V(c.x-4,c.y-2),V(c.x+3,c.y+5),gl,1,0,1.4f);
                                      dl->AddRect(V(c.x-2,c.y-5),V(c.x+5,c.y+2),gl,1,0,1.4f); }
                          else dl->AddRect(V(c.x-5,c.y-5),V(c.x+5,c.y+5),gl,1,0,1.5f); }
        else { dl->AddLine(V(c.x-4,c.y-4),V(c.x+4,c.y+4),gl,1.7f); dl->AddLine(V(c.x+4,c.y-4),V(c.x-4,c.y+4),gl,1.7f); }
        return hov;
    };
    auto doCtrl=[&](bool overMin,bool overMax,bool overClose){
        if(!click) return false;
        if(overClose){ PostMessageW(fg,WM_SYSCOMMAND,SC_CLOSE,0); g_decoRect=RECT{0,0,0,0}; return true; }
        if(overMax)  PostMessageW(fg,WM_SYSCOMMAND,zoomed?SC_RESTORE:SC_MAXIMIZE,0);
        if(overMin)  PostMessageW(fg,WM_SYSCOMMAND,SC_MINIMIZE,0);
        return false;
    };

    // ---- resolve this app's fitted profile ----
    const DecoCache& dc=DecoResolve(fg);
    const std::wstring& exe=dc.exe;
    const DecoProfile* prof=dc.prof;
    int mode = dc.mode;
    if(wcsstr(GetCommandLineW(),L"--decolog")){ static ULONGLONG lm=0;
        if(GetTickCount64()-lm>1000){ lm=GetTickCount64();
            FILE* lf=fopen("decolog.txt","a"); if(lf){ fprintf(lf,"  mode=%d prof=%d\n",mode,prof?1:0); fclose(lf);} } }
    if(mode==2){ g_decoTarget=nullptr; g_decoRect=RECT{0,0,0,0}; return; }

    // ===== BUTTONS MODE: keep the app's own bar & tabs, just restyle its window buttons, fitted =====
    if(mode==1){
        float BW = (prof&&prof->btnW>0)? prof->btnW : 46;
        float BH = (prof&&prof->btnH>0)? prof->btnH : 34;
        float TOP= prof? prof->top : 0;
        ImU32 bg; ImU32 glyph;
        if(prof && prof->bg){ bg=prof->bg; glyph = prof->glyphLight? IM_COL32(0xff,0xff,0xff,235):IM_COL32(0x30,0x30,0x30,255); }
        else { int spx=std::max((int)R.left+40,(int)R.right-(int)(3*BW)-30), spy=(int)R.top+(int)(TOP+BH*0.35f);
            bg=SampleScreenPixel(spx,spy);
            int br=(bg)&0xFF,bgc=(bg>>8)&0xFF,bb=(bg>>16)&0xFF; float lum=0.299f*br+0.587f*bgc+0.114f*bb;
            glyph = lum<128? IM_COL32(0xff,0xff,0xff,235):IM_COL32(0x30,0x30,0x30,255); }
        int br=(bg)&0xFF,bgc=(bg>>8)&0xFF,bb=(bg>>16)&0xFF; bool bgDark=(0.299f*br+0.587f*bgc+0.114f*bb)<128;
        ImU32 hoverBg = bgDark? IM_COL32(255,255,255,32):IM_COL32(0,0,0,26);
        ImVec2 rc0=toL((LONG)(R.right-3*BW),(LONG)(R.top+TOP)), rc1=toL(R.right,(LONG)(R.top+TOP+BH));
        if(rc1.x-rc0.x<30){ g_decoRect=RECT{0,0,0,0}; return; }
        dl->AddRectFilled(rc0,rc1,bg,0);              // hide the native buttons under a matching patch
        float bwid=(rc1.x-rc0.x)/3.0f, cyb=(rc0.y+rc1.y)/2;
        bool oMin=drawCtrl(V(rc0.x+bwid*0.5f,cyb),11,0,glyph,hoverBg);
        bool oMax=drawCtrl(V(rc0.x+bwid*1.5f,cyb),11,1,glyph,hoverBg);
        bool oClose=drawCtrl(V(rc0.x+bwid*2.5f,cyb),11,2,glyph,IM_COL32(0xe0,0x1b,0x24,255));
        if(doCtrl(oMin,oMax,oClose)) return;
        g_decoRect=RECT{(LONG)rc0.x,(LONG)rc0.y,(LONG)rc1.x,(LONG)rc1.y};
        return;
    }

    // ===== FULL BAR MODE: our centred-title headerbar (apps with no fitted profile) =====
    // Height follows the window's REAL caption when it has one, so our band covers it exactly
    // instead of floating over a taller or shorter one.
    float captH=dc.captH;
    float barH = captH>0.0f ? captH/g_uiScale : DECOH;
    if(barH<24.0f) barH=24.0f; if(barH>64.0f) barH=64.0f;
    ImVec2 b0=toL(R.left,R.top), b1=toL(R.right,R.top);
    b1.y=b0.y+barH;
    float bw=b1.x-b0.x; if(bw<80) { g_decoRect=RECT{0,0,0,0}; return; }
    bool dark=g_darkUI;
    ImU32 barCol = dark? IM_COL32(0x2b,0x2b,0x2b,255) : IM_COL32(0xf6,0xf5,0xf4,255);
    ImU32 ink    = dark? IM_COL32(0xff,0xff,0xff,235) : IM_COL32(0x2e,0x34,0x36,255);
    ImU32 border = dark? IM_COL32(0x1b,0x1b,0x1b,255) : IM_COL32(0xd8,0xd6,0xd4,255);
    // match the window's own corner radius, or the app's rounded corners show past our square ones
    float crad = dc.crad;
    ImDrawFlags rf = crad>0.5f? ImDrawFlags_RoundCornersTop : (ImDrawFlags)0;
    dl->AddRectFilled(b0,b1,barCol,crad,rf);
    dl->AddLine(V(b0.x,b1.y-0.5f),V(b1.x,b1.y-0.5f),border,1);
    float cy=(b0.y+b1.y)/2;

    // app icon (left)
    ID3D11ShaderResourceView* ic=DecoIcon(fg);
    float lx=b0.x+12;
    if(ic){ dl->AddImage((ImTextureID)ic,V(lx,cy-9),V(lx+18,cy+9),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,255)); lx+=26; }

    // window controls (right): minimize, maximize/restore, close  (GNOME order)
    ImU32 hbNorm = dark?IM_COL32(255,255,255,28):IM_COL32(0,0,0,20);
    float bx=b1.x-8;
    auto ctrl=[&](int kind)->bool{ float r=13; bx-=(r*2+6);
        return drawCtrl(V(bx+r,cy),r,kind,ink, kind==2?IM_COL32(0xe0,0x1b,0x24,255):hbNorm); };
    bool overClose=ctrl(2), overMax=ctrl(1), overMin=ctrl(0);
    float ctrlLeft=bx-6;
    if(doCtrl(overMin,overMax,overClose)) return;

    // title, centred in the space between the icon and the controls
    { std::wstring wt; wt.resize(256); int n=GetWindowTextW(fg,&wt[0],256); wt.resize(std::max(0,n));
      std::string t=W2U8(wt); if(t.empty()) t="Window";
      float avail=ctrlLeft-lx-16;
      std::string ct=Clip(g_fMed,15,t,avail);
      float tw=TextW(g_fMed,15,ct.c_str());
      float txc=(b0.x+b1.x)/2 - tw/2;
      txc=std::clamp(txc,lx+8,ctrlLeft-tw-8);
      TextAt(dl,g_fMed,15,V(txc,cy-9),ink,ct.c_str()); }

    // drag to move / double-click to (un)maximize — anywhere on the bar that isn't a control
    bool onBar=io.MousePos.x>b0.x&&io.MousePos.x<ctrlLeft&&io.MousePos.y>b0.y&&io.MousePos.y<b1.y;
    if(dbl&&onBar){ PostMessageW(fg,WM_SYSCOMMAND,zoomed?SC_RESTORE:SC_MAXIMIZE,0); g_decoDrag=false; g_decoDragRectValid=false; }
    else if(click&&onBar){ g_decoDrag=true; g_decoDragRectValid=false; POINT cp; GetCursorPos(&cp);
        RECT wr; GetWindowRect(fg,&wr); g_decoGrab.x=cp.x-wr.left; g_decoGrab.y=cp.y-wr.top; }
    if(g_decoDrag&&down){ POINT cp; GetCursorPos(&cp);
        if(zoomed){ ShowWindowAsync(fg,SW_RESTORE); RECT wr=DecoFrame(fg); (void)wr; }   // pull a maximized window loose
        int nx=cp.x-g_decoGrab.x, ny=cp.y-g_decoGrab.y;
        RECT wr; GetWindowRect(fg,&wr);
        // GetWindowRect includes the invisible resize border; DecoFrame does not. Carry that offset
        // across so the predicted FRAME rect lines up with where the window will actually land.
        RECT fr=DecoFrame(fg);
        g_decoDragRect = RECT{ nx+(fr.left-wr.left), ny+(fr.top-wr.top),
                               nx+(fr.right-wr.left), ny+(fr.bottom-wr.top) };
        g_decoDragRectValid=true;
        SetWindowPos(fg,nullptr,nx,ny,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS); }
    if(rel){ g_decoDrag=false; g_decoDragRectValid=false; }

    g_decoRect=RECT{(LONG)b0.x,(LONG)b0.y,(LONG)b1.x,(LONG)b1.y};
    if(wcsstr(GetCommandLineW(),L"--decolog")){ static ULONGLONG le=0;
        if(GetTickCount64()-le>1000){ le=GetTickCount64();
            FILE* lf=fopen("decolog.txt","a");
            if(lf){ RECT wr{}; GetWindowRect(g_decoHwnd,&wr);
                fprintf(lf,"  FULLBAR rect=(%ld,%ld)-(%ld,%ld) vis=%d decoWnd=(%ld,%ld)-(%ld,%ld) barH=%.1f crad=%.1f\n",
                        g_decoRect.left,g_decoRect.top,g_decoRect.right,g_decoRect.bottom,
                        (int)IsWindowVisible(g_decoHwnd), wr.left,wr.top,wr.right,wr.bottom, barH, crad);
                fclose(lf);} } }
}
