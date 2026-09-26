// Aether - in-bar app preview and the bar draw.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =========================================================================================
// IN-BAR APP PREVIEW — the fix for "make it INTO THE BAR so it looks and actually is coming
// out the bar". The old preview was a SEPARATE DWM window (g_thumbHwnd) painted to look like
// the bar but genuinely floating over it. This instead CAPTURES the hovered window into a
// texture (PrintWindow) and draws the whole preview — frame, title, shoulder fillets, video —
// straight into the BAR's own draw list, exactly like the wifi/bt SysFlyout. So it morphs out
// of the strip, gets the desktop notch, and is one continuous surface with the bar.
// =========================================================================================
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
static int    g_appPrev=0;                 // 0 none; 1 showing
static int    g_appPrevMon=-1;             // which monitor's bar owns it
static ImVec2 g_appPrevAt(0,0);            // strip-edge anchor (x = strip edge, y = icon centre)
static HWND   g_appPrevSrc=nullptr;        // the window being previewed
static std::string g_appPrevTitle;
static ULONGLONG g_appPrevHoverTick=0;     // grace timer (kept alive while cursor on icon OR panel)
static ID3D11ShaderResourceView* g_appPrevTex=nullptr;   // last captured frame
static int    g_appPrevTexW=0, g_appPrevTexH=0;
static HWND   g_appPrevCapSrc=nullptr;     // src the current texture was captured from
static ULONGLONG g_appPrevCapTick=0;       // last capture time (throttle ~9fps)

// Grab a window into a downscaled BGRA texture. PW_RENDERFULLCONTENT captures GPU-composited
// (Chrome/UWP/etc.) content that a plain BitBlt would miss.
static ID3D11ShaderResourceView* CaptureWindowTex(HWND src,int& outW,int& outH,int maxw=600){
    outW=outH=0; if(!src||!IsWindow(src)||IsIconic(src)) return nullptr;
    RECT wr; if(!GetWindowRect(src,&wr)) return nullptr;
    int sw=wr.right-wr.left, sh=wr.bottom-wr.top;
    if(sw<=1||sh<=1) return nullptr;
    const int MAXW=std::max(80,maxw); float sc=(sw>MAXW)?(float)MAXW/sw:1.0f;
    int dw=std::max(1,(int)(sw*sc)), dh=std::max(1,(int)(sh*sc));
    HDC scr=GetDC(nullptr);
    HDC full=CreateCompatibleDC(scr);
    BITMAPINFO fbi={}; fbi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); fbi.bmiHeader.biWidth=sw; fbi.bmiHeader.biHeight=-sh; fbi.bmiHeader.biPlanes=1; fbi.bmiHeader.biBitCount=32; fbi.bmiHeader.biCompression=BI_RGB;
    void* fbits=nullptr; HBITMAP fbmp=CreateDIBSection(full,&fbi,DIB_RGB_COLORS,&fbits,nullptr,0); HGDIOBJ fo=SelectObject(full,fbmp);
    BOOL ok=fbmp? PrintWindow(src,full,PW_RENDERFULLCONTENT) : FALSE;
    ID3D11ShaderResourceView* tex=nullptr;
    if(ok && fbits){
        HDC dst=CreateCompatibleDC(scr);
        BITMAPINFO dbi={}; dbi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); dbi.bmiHeader.biWidth=dw; dbi.bmiHeader.biHeight=-dh; dbi.bmiHeader.biPlanes=1; dbi.bmiHeader.biBitCount=32; dbi.bmiHeader.biCompression=BI_RGB;
        void* dbits=nullptr; HBITMAP dbmp=CreateDIBSection(dst,&dbi,DIB_RGB_COLORS,&dbits,nullptr,0); HGDIOBJ do_=SelectObject(dst,dbmp);
        SetStretchBltMode(dst,HALFTONE); StretchBlt(dst,0,0,dw,dh,full,0,0,sw,sh,SRCCOPY);
        if(dbits){ for(int i=0;i<dw*dh;i++)((uint8_t*)dbits)[i*4+3]=255; tex=MakeTextureBGRA(dbits,dw,dh); outW=dw; outH=dh; }
        SelectObject(dst,do_); DeleteObject(dbmp); DeleteDC(dst);
    }
    SelectObject(full,fo); DeleteObject(fbmp); DeleteDC(full); ReleaseDC(nullptr,scr);
    return tex;
}

// Draws the in-bar preview panel in the BAR's own draw list. Mirrors SysFlyout: morph spring,
// shoulder fillets, desktop notch, hover-grace close. `hit` is BS.rect (expanded so the panel is
// visible + click-safe, same as the wifi flyout).
static void AppPreviewFlyout(ImDrawList* dl, ImGuiIO& io, int mi, RECT& hit){
    if(g_appPrevMon!=mi) return;
    static HWND shownSrc=nullptr; static std::string shownTitle;
    if(g_appPrev){ shownSrc=g_appPrevSrc; shownTitle=g_appPrevTitle; }
    bool open=(g_appPrev!=0);
    // (re)capture the source, throttled, while open — also on a source change
    if(open && shownSrc){
        ULONGLONG now=GetTickCount64();
        if(shownSrc!=g_appPrevCapSrc || now-g_appPrevCapTick>110){
            int w,h; ID3D11ShaderResourceView* t=CaptureWindowTex(shownSrc,w,h);
            if(t){ if(g_appPrevTex) g_appPrevTex->Release(); g_appPrevTex=t; g_appPrevTexW=w; g_appPrevTexH=h; g_appPrevCapSrc=shownSrc; }
            g_appPrevCapTick=now;
        }
    }
    // content size — width fixed, height follows the captured aspect (clamped so it stays a sane pill)
    const float PW_=300.0f, TITLEH=30.0f, VPAD=8.0f;
    float wellW=PW_-VPAD*2;
    float aspH = (g_appPrevTexW>0 && g_appPrevTexH>0) ? wellW*g_appPrevTexH/(float)g_appPrevTexW : 165.0f;
    aspH=std::clamp(aspH,90.0f,215.0f);
    float mw=PW_, mh=TITLEH+aspH+VPAD*2;
    // MORPH exactly like SysFlyout: height/width spring toward the content size, opacity crossfades.
    float panelH=MotionAnim(MP_POPOUT,810030, open? mh:0.0f);
    float panelW=MotionAnim(MP_POPOUT,810031, open? mw:0.0f);
    float anchorY=Cael::anim(810033, g_appPrevAt.y, Cael::DUR_FAST_SPATIAL, Cael::FAST_SPATIAL);
    float af=std::clamp(Cael::anim(810032, open?1.0f:0.0f, Cael::DUR_DEFAULT_EFFECTS, Cael::DEFAULT_EFFECTS),0.0f,1.0f);
    int al=(int)(af*255);
    if(panelH<2.0f && !open){ shownSrc=nullptr; if(g_prevCut){ g_prevCut=false; g_deskDirty=true; } return; }
    // Same reference-correct geometry as SysFlyout: the preview emerges from the strip's RIGHT edge and
    // grows OUT to the right, CENTRED vertically on the icon. The app icon column stays visible in the
    // strip to the left; a small tuck (stripMerge) under the strip's right edge swallows BarGlass's
    // border+shadow so strip+panel are ONE continuous surface. Only right corners rounded.
    const float mergeOverlap=18.0f, stripMerge=6.0f;
    bool leftBar = (g_barStripR>g_barStripL && g_appPrevAt.x >= g_barStripL);
    float attX = leftBar ? (g_barStripR - stripMerge) : (g_appPrevAt.x - mergeOverlap);
    float originX = leftBar ? g_barStripR : g_appPrevAt.x;
    float top=anchorY - panelH*0.5f;
    if(top < hit.top+6)               top=(float)hit.top+6;
    if(top+panelH > hit.bottom-6)     top=(float)hit.bottom-6-panelH;
    ImVec2 m0=V(attX, top), m1=V(originX + panelW, top+panelH);
    // publish the notch so the desktop wallpaper curves around the preview
    { bool ch = !g_prevCut || g_prevL!=m0.x || g_prevT!=m0.y || g_prevR!=m1.x || g_prevB!=m1.y;
      g_prevCut=true; g_prevL=m0.x; g_prevT=m0.y; g_prevR=m1.x; g_prevB=m1.y; g_prevRad=16.0f;
      if(ch) g_deskDirty=true; }
    // same solid material as the bar strip — clean rounded-right rectangle stepping OUT of the strip. NO
    // shoulder fillets (they drew a rounded nub + wallpaper cove = "floating card" look). The left edge is
    // tucked stripMerge px into the opaque strip so strip+panel are one continuous surface; only the
    // desktop-facing right corners are rounded, the strip-facing left corners are square and merge in.
    ImU32 barFill = g_darkUI ? IM_COL32(18,18,22,255) : PanelCol(255);
    dl->AddRectFilled(m0,m1,barFill,16,ImDrawFlags_RoundCornersRight);
    dl->PushClipRect(V(m0.x-1,m0.y-1),V(m1.x+1,m1.y+1),true);
    // title (indented just past the strip's right edge, clear of the icon column)
    TextAt(dl,g_fMed,16,V(m0.x+stripMerge+8,m0.y+7),WithA(COL_INK,al),Clip(g_fMed,16,shownTitle,mw-24).c_str());
    // video well + letterboxed capture
    ImVec2 w0=V(m0.x+stripMerge+VPAD, m0.y+TITLEH), w1=V(m1.x-VPAD, m1.y-VPAD);
    dl->AddRectFilled(w0,w1,WithA(IM_COL32(10,10,13,255),al),8);
    if(g_appPrevTex && g_appPrevTexW>0){
        float ww=w1.x-w0.x, wh=w1.y-w0.y, ta=g_appPrevTexW/(float)g_appPrevTexH, wa=ww/wh;
        float iw=ww, ih=wh;
        if(ta>wa) ih=ww/ta; else iw=wh*ta;         // letterbox
        ImVec2 i0=V(w0.x+(ww-iw)*0.5f, w0.y+(wh-ih)*0.5f), i1=V(i0.x+iw,i0.y+ih);
        dl->AddImageRounded((ImTextureID)g_appPrevTex,i0,i1,ImVec2(0,0),ImVec2(1,1),WithA(IM_COL32(255,255,255,255),al),6);
    } else {
        const char* ld="…";
        TextAt(dl,g_fMed,18,V((w0.x+w1.x)*0.5f-TextW(g_fMed,18,ld)/2,(w0.y+w1.y)*0.5f-9),WithA(COL_INK2,al),ld);
    }
    dl->PopClipRect();
    // expand the bar hit rect so the panel is visible + click-safe (same as SysFlyout)
    hit.left=(LONG)std::min((float)hit.left,m0.x); hit.top=(LONG)std::min((float)hit.top,m0.y);
    hit.right=(LONG)std::max((float)hit.right,m1.x); hit.bottom=(LONG)std::max((float)hit.bottom,m1.y);
    // keep-alive while cursor is over the panel; click it -> activate the window
    bool inPanel=io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>m0.y&&io.MousePos.y<m1.y;
    if(inPanel){ g_appPrevHoverTick=GetTickCount64();
        if(io.MouseClicked[0] && shownSrc && IsWindow(shownSrc)){
            if(IsIconic(shownSrc)) ShowWindowAsync(shownSrc,SW_RESTORE);
            SetForegroundWindow(shownSrc); g_appPrev=0; } }
    if(g_appPrev && GetTickCount64()-g_appPrevHoverTick>190) g_appPrev=0;   // grace close after leaving icon+panel
}
// ---- bar components, each ported from Caelestia's modules/bar/components/*.qml into its own file ----
#include "src/modules/bar/components/OsIcon.h"      // <- OsIcon.qml
#include "src/modules/bar/components/Clock.h"       // <- Clock.qml
#include "src/modules/bar/components/Power.h"       // <- Power.qml
// Draws the bar for ONE monitor. (MX,MY,MW,MH) is that monitor's rect inside the bar window
// (which spans every monitor), in logical px.
static void DrawBarOn(int mi,float MX,float MY,float MW,float MH){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    BarState& BS=Bar(mi);
    float H=MH;
    float slide=EaseOutCubic(std::clamp(BS.reveal,0.0f,1.0f));
    if(slide<0.002f){ BS.rect=RECT{0,0,0,0};
        if(g_barMenuMon==mi && !g_forceBar){ g_barMenu=false; g_barMenuAnim=0.0f; }
        if(g_barFlyMon==mi){ g_barFly=0; }
        if(g_trayFlyMon==mi){ g_trayOpen=false; }
        if(g_appPrevMon==mi){ g_appPrev=0; if(g_prevCut){ g_prevCut=false; g_deskDirty=true; } }
        if(g_powerMenuMon==mi) g_powerMenu=false;
        return; }
    if(g_pn[PN_BAR].edge==EDGE_TOP||g_pn[PN_BAR].edge==EDGE_BOTTOM){ DrawBarHoriz(mi,MX,MY,MW,MH,BS,slide); return; }
    // geometry is DATA now: edge / width / length / anchor all come from the Panel descriptor
    const Panel& P=g_pn[PN_BAR];
    PRect pr=PanelRect(P,MW,MH);
    pr.x+=MX; pr.y+=MY;                                    // into the bar window's space
    float barW=pr.w, by=pr.y, bh=pr.h;
    PanelSlide(P,pr,slide);                                // slides in from its own edge
    float bx=pr.x;
    const bool onRight=(P.edge==EDGE_RIGHT);
    const float side = onRight? -1.0f : 1.0f;              // which way tips/menus/previews open
    ImVec2 pmin=V(bx,by),pmax=V(bx+barW,by+bh);
    g_barStripL=bx; g_barStripR=bx+barW;                   // so popouts can start flush at the strip edge
    float rnd=g_bubble?g_bubbleRound:g_panelRound;         // same radius as the desktop bubble
    BarGlass(dl,pmin,pmax,MX,MY,MW,MH,rnd,PanelCorners(P),1.0f);   // frosted-wallpaper acrylic
    BS.rect=PanelHitRect(PRect{bx,by,barW,bh});
    if(g_barStyle==1){ DrawBarVertV2(mi,bx,by,barW,bh,onRight,slide); return; }   // the Caelestia (new) strip
    const int HID = mi*20000;      // hover-animation ids must not collide between monitors
    auto dist=[](ImVec2 a,ImVec2 b){ return sqrtf((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)); };
    bool click=io.MouseClicked[0], rclick=io.MouseClicked[1];
    float cx=bx+barW*0.5f, tipX=onRight? bx-12 : bx+barW+12;
    float inner=onRight? bx+barW : bx;                     // the strip edge facing the desktop
    const char* tip=nullptr; float tipY=0; std::string tipStore;
    float half=barW*0.5f-3;
    // per-item entrance: each control eases in with a stagger, so the bar "unfolds"
    auto IE=[&](int i){ return EaseOutCubic(Stagger(slide,i,0.035f,0.55f)); };
    auto IX=[&](int i){ return (1.0f-IE(i))*-18.0f*side; };
    auto IA=[&](int i,ImU32 c){ return MulA(c,IE(i)); };
    auto hit=[&](float y,float r){ return io.MousePos.x>cx-r&&io.MousePos.x<cx+r&&io.MousePos.y>y-r&&io.MousePos.y<y+r; };
    // (no group separators: the reference rice divides the strip with SPACE only — every hairline
    //  I tried read as clutter next to it. The pill surfaces do the grouping.)
    // ===== REGISTRY-DRIVEN LAYOUT =============================================================
    // This used to be a hardcoded cascade: logo/workspaces/apps grown down from the top edge and
    // power/cluster/clock/calendar/tray grown UP from the bottom at the same time, meeting at a gap
    // in the middle. That is why nothing could be hidden, moved or centred. Now every item reports
    // an extent, the spacer absorbs the leftover, and BarPlace assigns the positions.
    if(g_barItems.empty()) BarItemsDefault();

    // ---- live data the extents depend on (all measured BEFORE placing anything) ----
    HWND fg=GetForegroundWindow();
    int notifN; { std::lock_guard<std::mutex> lk(g_notifMtx); notifN=(int)g_notifs.size(); }
    float plugH=PluginsBarHeight();
    // only the windows on THIS bar's monitor, like a real taskbar
    static std::vector<DockApp*> mine; mine.clear();
    { HMONITOR barMon=MonitorFromPoint(POINT{MonRect(mi).left+2,MonRect(mi).top+2},MONITOR_DEFAULTTONEAREST);
      // a.mon is null for a pin whose app is closed: it belongs on every bar, not none of them
      for(auto& a:g_dockApps)
          if(!g_barSameMonitor || a.mon==barMon || (a.pinned && !a.running)) mine.push_back(&a); }
    // Focused-window title, drawn ROTATED down the strip. Caelestia's bar carries
    // `activeWindow` as a core vertical entry - the demo frames show "Desktop" and
    // "(Paused) (24 / 53) Res..." running down the bar - but this port had it flagged
    // horizontal-only in BAR_ITEMS and simply never drew it on a side bar.
    std::string wiTitleV;
    { HWND fgw=GetForegroundWindow();
      wchar_t wt[256]={0}; if(fgw) GetWindowTextW(fgw,wt,255);
      DWORD wpid=0; if(fgw) GetWindowThreadProcessId(fgw,&wpid);
      if(fgw && wpid!=GetCurrentProcessId() && wt[0]) wiTitleV=W2U8(CleanTitle(wt)); }

    // real Windows system-tray icons — barTray is the PROMOTED set (see DrawBarHoriz), the rest
    // live behind the chevron at the head of the strip.
    static std::vector<SysTrayIcon*> barTray; barTray.clear();
    int trayTotal=0, trayHidden=0;
    { std::lock_guard<std::mutex> lk(g_systrayMtx);
      for(auto&s:g_systray){ if(s.hidden||!s.tex) continue; trayTotal++;
          if(TrayIsShown(s)) barTray.push_back(&s); else trayHidden++; } }

    const float ai=44.0f, isz=30.0f;                  // app row pitch / icon size
    // This bar's own workspace ring. Under komorebi each monitor has a different one; under virtual
    // desktops they are all the same ring, so nothing changes there.
    const WsRing& WR=WsRingFor(mi);
    const float WSPITCH=(float)(Tok::bar::innerWidth-Tok::padding::small);   // 32, per Workspace.qml
    const float WSICON=20.0f;                       // one window-category glyph row (MaterialIcon)
    // Workspace.qml: size = implicitHeight + (hasWindows ? Tokens.padding.extraSmall : 0)
    auto wsSlotH=[&](int k)->float{
        int n = (k>=0&&k<64)? WR.iconN[k] : 0;
        return WSPITCH + (n>0 ? n*WSICON + (float)Tok::padding::extraSmall : 0.0f); };
    // Workspaces.qml: ColumnLayout { spacing: Math.floor(Tokens.spacing.extraSmall) } - 4px between
    // workspaces, which we were not leaving at all, so tall (windowed) slots ran into each other.
    const float WSGAP=(float)Tok::spacing::extraSmall;
    auto wsSlotTop=[&](int k)->float{
        float y=0; for(int j=0;j<k;j++) y+=wsSlotH(j)+WSGAP; return y; };
    auto wsSlotY=[&](int k)->float{ return wsSlotTop(k)+WSPITCH*0.5f; };   // centre of the SHAPE
    const int WSN=WsSlots();
    auto wsRunH=[&]()->float{ float t=0; for(int k=0;k<WSN;k++) t+=wsSlotH(k);
        return t + (WSN>1 ? WSGAP*(WSN-1) : 0.0f); };
    const float BSTEP=34.0f, trayStep=33.0f;          // cluster + tray row pitch
    int nApps=(int)mine.size();
    int trayShown=std::min((int)barTray.size(),6);
    bool trayChev=g_trayCollapse;

    // An item can be switched off by the user OR be irrelevant right now (no mic, no battery, one
    // workspace). Both mean "not laid out" — but only the first is a setting, so they stay separate.
    auto liveOK=[&](int id)->bool{
        switch(id){
        // Workspaces.qml has no "hide when there is only one" rule - the pill with its dots is a
        // permanent landmark, and hiding it made the whole strip reflow whenever a desktop appeared.
        case BIT_WORKSPACES: return true;
        case BIT_APPS:       return nApps>0;
        case BIT_PLUGINS:    return plugH>0;
        case BIT_TRAY:       return trayTotal>0;
        case BIT_NOTIF:      return notifN>0||g_dnd;
        case BIT_MIC:        return g_micPresent;
        case BIT_BATT:       return g_st.hasBattery;
        case BIT_WINDOWINFO: return !wiTitleV.empty();       // rotated down the strip, like ActiveWindow.qml
        case BIT_FILES: return false;                        // horizontal-bar item only
        default: return BIT_IS_CUSTOM(id)? BarCustomOn(id-BIT_CUSTOM1) : true; }
    };
    // These used to be "content PLUS the space after it", which meant no two entries could share a
    // spacing rule and the pills could never sit tight against what they contain. They are the
    // entry's own implicitHeight now - exactly what Bar.qml's EntryWrapper reports - and the space
    // between entries is a real layout gap (Bar.qml: spacing: Tokens.spacing.medium).
    const float CELL=(float)Tok::bar::innerWidth-(float)Tok::padding::small;   // 32: one icon cell
    // Which entries the user has enabled at all - the clock needs this before placement, because
    // Clock.qml carries its own calendar_month icon and we only want one of those in the strip.
    bool vis0[BIT_COUNT]; for(int k2=0;k2<BIT_COUNT;k2++) vis0[k2]=false;
    for(auto& c0:g_barItems) if(c0.on && BAR_ITEMS[c0.id].vert) vis0[c0.id]=true;
    auto extOf=[&](int id)->float{
        if(BIT_IS_CUSTOM(id)) return CELL;                 // a side strip shows the icon, not a label
        switch(id){
        case BIT_LOGO:       return 40.0f;                                    // centre at pos+20
        // Workspaces.qml: implicitHeight = layout.implicitHeight + Tokens.padding.small
        case BIT_WORKSPACES: return wsRunH()+(float)Tok::padding::small;
        case BIT_APPS:       return nApps>0? (nApps-1)*ai+isz : 0.0f;         // first icon top at pos
        case BIT_PLUGINS:    return plugH;
        case BIT_TRAY:       return (trayShown+(trayChev?1:0)-1)*trayStep+CELL;// first cell centre pos+16
        case BIT_WINDOWINFO: return std::min(TextW(g_fMed,15,wiTitleV.c_str())+22.0f, bh*0.34f);
        case BIT_CALENDAR:   return CELL;                                     // centre at pos+16
        case BIT_CLOCK:      return BarClockH(!vis0[BIT_CALENDAR]);
        case BIT_POWER:      return CELL;                                     // centre at pos+16
        default:             return BarIsSpacer(id)? 0.0f : CELL; }           // cluster icons
    };

    // ---- build the ordered slot list, then shrink the elastic items until it fits --------------
    std::vector<BarSlot> slots;
    auto rebuild=[&](){
        slots.clear();
        for(auto& c:g_barItems){
            if(!c.on || !BAR_ITEMS[c.id].vert || !liveOK(c.id)) continue;
            slots.push_back({c.id,0.0f,extOf(c.id),0.0f}); }
        // Bar.qml puts spacing.medium between entries. StatusIcons.qml is ONE entry upstream whose
        // icons sit spacing.medium/2 apart inside the pill, and those icons are separate slots here,
        // so two adjacent cluster items get the inner spacing and everything else the entry spacing.
        for(size_t q=0;q+1<slots.size();q++){
            bool inSys = BAR_ITEMS[slots[q].id].pill==PILL_SYS &&
                         BAR_ITEMS[slots[q+1].id].pill==PILL_SYS;
            slots[q].gapAfter = inSys ? (float)Tok::spacing::medium*0.5f
                                      : (float)Tok::spacing::medium;
        }
        float t=0; for(size_t q=0;q<slots.size();q++){ t+=slots[q].ext;
            if(q+1<slots.size()) t+=slots[q].gapAfter; }
        return t;
    };
    float need=rebuild();
    // Overflow policy: drop task buttons first (the list is the elastic thing and it already had a
    // maxA cap), then tray icons. Without this a machine with 40 windows open would run the strip
    // straight off the bottom of the screen instead of just showing fewer buttons.
    while(need>bh && nApps>0){ nApps--; need=rebuild(); }
    while(need>bh && trayShown>0){ trayShown--; need=rebuild(); }

    // Bar.qml: EntryWrapper.Layout.topMargin/bottomMargin = root.vPadding on the FIRST and LAST
    // entry only, where vPadding = Tokens.padding.large. The run itself was starting flush against
    // the strip end.
    { const float vPad=(float)Tok::padding::large;   // 16
      BarPlace(slots,by+vPad,bh-vPad*2.0f,g_barAlign); }

    float posOf[BIT_COUNT]; float extB[BIT_COUNT]; bool vis[BIT_COUNT];
    for(int k=0;k<BIT_COUNT;k++){ posOf[k]=0; extB[k]=0; vis[k]=false; }
    for(auto& sl:slots){ posOf[sl.id]=sl.pos; extB[sl.id]=sl.ext; vis[sl.id]=true; }

    // named anchors so the drawing code below reads the way it always did
    float logoY  = posOf[BIT_LOGO]+20.0f;
    float wsTop  = posOf[BIT_WORKSPACES]+(float)Tok::padding::small*0.5f;   // inside its own pill
    float appTop = posOf[BIT_APPS];
    float plugTop= posOf[BIT_PLUGINS];
    float trayTop= posOf[BIT_TRAY]+CELL*0.5f;
    float calY   = posOf[BIT_CALENDAR]+CELL*0.5f;
    float ckT    = posOf[BIT_CLOCK];
    float powY   = posOf[BIT_POWER]+CELL*0.5f;
    int   n      = vis[BIT_APPS]? nApps : 0;
    if(!vis[BIT_TRAY]){ trayShown=0; trayChev=false; }

    // ---- entrance stagger: index items by their PLACE in the strip, so the unfold still runs
    //      top-to-bottom however the user has reordered things ----
    int staggerOf[BIT_COUNT]; for(int k=0;k<BIT_COUNT;k++) staggerOf[k]=0;
    { int q=0; for(auto& sl:slots) staggerOf[sl.id]=q++; }
    // Caelestia groups related bar icons onto rounded "pill" surfaces. Draw those capsules BEHIND
    // the icons via a 2-channel split (channel 0 = pills, channel 1 = icons), merged before menus.
    dl->ChannelsSplit(2); dl->ChannelsSetCurrent(1);

    // ---- pill capsules (drawn behind the icons in channel 0) ----------------------------------
    // Derived from the layout now: any RUN of consecutive visible items sharing a pill group gets
    // one capsule. That reproduces the two historic pills (workspaces+apps, system cluster) and
    // means dragging the clock into the cluster puts it inside that capsule automatically.
    { dl->ChannelsSetCurrent(0);
      // Upstream geometry, verbatim:
      //   StatusIcons.qml / Tray.qml : implicitWidth = Tokens.sizes.bar.innerWidth        (= 40)
      //   BarWrapper.qml             : contentWidth  = innerWidth + padding*2
      //                                padding       = max(Tokens.padding.small, border.thickness)
      //   borderconfig.hpp           : thickness default 10   ->  padding 10, strip 40+20 = 60
      // So a Caelestia bar is a 60px strip around a 40px inner column, and the pills ARE that inner
      // column. Ours was a hardcoded (barW-14)/2 = 25.7px in their 39.7px bar - narrower than both
      // the 30px app icons and the 33.7px system cells, so every icon overhung its own panel.
      // Take innerWidth when the strip is wide enough for it, shrink to fit when it is not, and
      // never go narrower than the widest thing being grouped.
      float innerW=std::min((float)Tok::bar::innerWidth, barW-4.0f);
      float ph=std::max(innerW*0.5f, isz*0.5f+3.0f);
      // StatusIcons.qml, verbatim:  color: Colours.tPalette.m3surfaceContainer
      //                              radius: Tokens.rounding.full
      // A flat scheme surface, fully rounded - no gradient, no border. We had been drawing a white
      // 26-alpha wash PLUS a top-down white gradient PLUS a white hairline, which is a different
      // thing entirely and is why it read as a grey smear instead of a surface. COL_CARD is this
      // port's m3surfaceContainer.
      auto groupPill=[&](float y0,float y1){
          if(y1-y0<6.0f) return;
          ImVec2 pa=V(cx-ph,y0), pb=V(cx+ph,y1);
          dl->AddRectFilled(pa,pb, COL_CARD, ph);
      };
      for(size_t a2=0;a2<slots.size();){
          int grp=BAR_ITEMS[slots[a2].id].pill;
          if(grp==PILL_NONE){ a2++; continue; }
          size_t b2=a2; while(b2+1<slots.size() && BAR_ITEMS[slots[b2+1].id].pill==grp) b2++;
          // A pill says "these belong together"; around ONE icon it says nothing and just reads as a
          // stray grey panel - which is what the system cluster becomes once every item but Wi-Fi is
          // switched off. Judge that by the pill's LENGTH, not by the slot count: `apps` is a single
          // slot that draws every task button, so counting slots would wrongly drop that pill too.
          float pt=BarPillTop(slots[a2].id,slots[a2].pos);
          float pb2=BarPillBot(slots[b2].id,slots[b2].pos,slots[b2].ext);
          if(g_barPills && pb2-pt > 24.0f) groupPill(pt,pb2);
          a2=b2+1; }
      dl->ChannelsSetCurrent(1); }

    // ---- logo / launcher (OsIcon.qml) — drawn by the ported component ----
    if(vis[BIT_LOGO]){ int i=staggerOf[BIT_LOGO]; const char* t=DrawBarLogo(dl,io,cx,IX(i),logoY,half,IE(i),click,HID);
        if(t){tip=t;tipY=logoY;} }

    // ---- workspaces (virtual desktops) ----
    // The active marker is one capsule that GLIDES between the dots on the spatial spring instead of
    // teleporting on every switch.
    // Workspaces, as Caelestia's "Shapes" display type actually draws them (Workspace.qml):
    //   MaterialShape, implicitSize = innerWidth - padding.small (32)
    //   scale   focused 2/3 | occupied 1/3 | empty 1/4
    //   colour  occupied or focused -> m3onSurface, else a dimmed outline variant
    // The old version was a fixed 2.6px dot per workspace plus a gold capsule for the active one,
    // which is neither the right size ladder nor the right shape.
    if(vis[BIT_WORKSPACES]){ int i0=staggerOf[BIT_WORKSPACES]; float ox0=IX(i0);
      // ActiveIndicator.qml: a fully-rounded capsule of innerWidth - padding.small, filled
      // m3primary, whose y ANIMATES between workspaces - and the focused shape is drawn on top of
      // it in m3onPrimary. Drawn first so the shapes land over it.
      // ActiveIndicator.qml: implicitHeight = the focused Workspace's `size`, i.e. it GROWS to cover
      // that workspace's window glyphs too - not a fixed cell. y and height are both animated.
      int wsc=std::clamp(WR.cur,0,WSN-1);
      float indT = Cael::anim(HID+3900, wsTop+wsSlotTop(wsc), Cael::DUR_DEFAULT_SPATIAL, Cael::DEFAULT_SPATIAL);
      float indH = Cael::anim(HID+3901, wsSlotH(wsc),         Cael::DUR_DEFAULT_SPATIAL, Cael::DEFAULT_SPATIAL);
      float ir   = WSPITCH*0.5f;
      dl->AddRectFilled(V(cx+ox0-ir,indT),V(cx+ox0+ir,indT+indH),IA(i0,COL_GOLD),ir);
      for(int k=0;k<WSN;k++){
        int i=i0; float ox=IX(i);
        float wy=wsTop+wsSlotY(k); bool act=(k==WR.cur);
        bool occ=(k<WR.count)&&WR.occ[k];      // slots past the last real desktop are empty
        bool wh=io.MousePos.x>cx-WSPITCH*0.5f&&io.MousePos.x<cx+WSPITCH*0.5f&&
                io.MousePos.y>wy-WSPITCH*0.5f&&io.MousePos.y<wy+WSPITCH*0.5f;
        float wa=HoverAnim(HID+3300+k,wh);
        // the scale ladder is animated so switching desktops grows/shrinks the shapes
        float want = act? (2.0f/3.0f) : occ? (1.0f/3.0f) : (1.0f/4.0f);
        float sc   = Cael::anim(HID+3910+k, want, Cael::DUR_DEFAULT_SPATIAL, Cael::DEFAULT_SPATIAL);
        float r    = WSPITCH*0.5f*sc*(1.0f+wa*0.10f);
        // on the indicator the shape must read as m3onPrimary, not m3onSurface
        ImU32 col  = act? M3OnPrimary() : occ? COL_INK : WithA(COL_INK2,110);
        // upstream re-rolls the focused shape on every focus change; keep the roll per workspace so
        // it only changes when that workspace is actually (re)entered, not on every frame
        static int  s_wsShape[64]={0}; static bool s_wsWasAct[64]={false};
        if(act && !s_wsWasAct[k]) s_wsShape[k]=M3_FOCUSED[rand()%18];
        s_wsWasAct[k]=act;
        int shp = act? s_wsShape[k] : (occ? M3_SQUARE : M3_CIRCLE);
        // each workspace turns at its own slightly different rate, so the strip does not pulse as
        // one block; the focused one turns a touch faster
        float wspin=ShellPhase()*(0.10f+0.013f*(float)(k%5))*(act?1.7f:1.0f);
        if(act){
            // the focused one flows through the expressive set while it turns
            int mA,mB; float mT;
            const int cyc[5]={ s_wsShape[k], M3_COOKIE6, M3_CLOVER4, M3_SUNNY, M3_GEM };
            M3MorphPick(7000+k, cyc, 5, 4.0f, mA, mB, mT);
            M3ShapeMorph(dl,V(cx+ox,wy), r, IA(i,col), mA, mB, mT, wspin);
        } else {
            // occupied breathes square <-> cookie, empty stays a circle so "nothing here" still reads
            int mA,mB; float mT;
            const int occCyc[3]={ M3_SQUARE, M3_COOKIE4, M3_SQUARE };
            const int empCyc[1]={ M3_CIRCLE };
            if(occ) M3MorphPick(7100+k, occCyc, 3, 6.0f, mA, mB, mT);
            else    M3MorphPick(7100+k, empCyc, 1, 6.0f, mA, mB, mT);
            M3ShapeMorph(dl,V(cx+ox,wy), r, IA(i,col), mA, mB, mT, wspin);
        }
        // the window glyphs for this workspace, stacked under its shape
        for(int q=0;q<WR.iconN[k];q++){
            float gy = wy + WSPITCH*0.5f + (float)Tok::padding::extraSmall*0.5f + q*WSICON + WSICON*0.5f;
            // Colours.palette.m3onSurfaceVariant, but ActiveIndicator's Colouriser repaints
            // everything inside the focused capsule as m3onPrimary
            ImU32 gc = IA(i, act? M3OnPrimary() : WithA(COL_INK2,225));
            if(!ThemedSym(dl,V(cx+ox,gy),9.0f,gc,WR.icon[k][q]))
                dl->AddCircleFilled(V(cx+ox,gy),2.4f,gc);
        }
        if(wh){ static char wtip[64];
            // komorebi workspace names are free text - this config uses kana - and the bar atlas is
            // built for Latin/Greek/Cyrillic only, so a name outside it would draw as blanks.
            bool ascii=WR.name[k][0]!=0;
            for(const char* q=WR.name[k]; *q; q++) if((unsigned char)*q>0x7E){ ascii=false; break; }
            if(ascii) snprintf(wtip,64,"Workspace %s",WR.name[k]);
            else      snprintf(wtip,64,"Workspace %d",k+1);
            tip=wtip; tipY=wy; }
        if(click&&wh) GotoWorkspace(mi,k);
      } }

    bool anyThumb=false;
    bool appTookRclick=false;   // an app icon consumed the right-click -> don't also open the bar menu
    for(int k=0;k<n;k++){ auto&a=*mine[k]; int i=staggerOf[BIT_APPS]; float ox=IX(i);
        float iy=appTop+k*ai; ImVec2 ip=V(cx+ox-isz/2,iy);
        bool hov=io.MousePos.x>ip.x-5&&io.MousePos.x<ip.x+isz+5&&io.MousePos.y>iy-4&&io.MousePos.y<iy+isz+4;
        float ha=HoverAnim(HID+3000+k,hov);
        bool active=DockGroupHasFg(a);
        // ActiveIndicator.qml marks the focused workspace with an m3primary capsule; the task list is
        // the same idea one level down, so the focused window gets an accent capsule rather than the
        // white wash it used to get - which on a dark strip just read as a grey disc behind the icon.
        { float e=std::min(isz*0.5f+5.0f, half-0.5f);
          float actA=Cael::anim(HID+3800+k, active?1.0f:0.0f, Cael::DUR_DEFAULT_SPATIAL, Cael::DEFAULT_SPATIAL);
          ImVec2 ac=V(cx+ox,iy+isz*0.5f);
          if(actA>0.01f){ float r=e*(0.95f+0.05f*actA);
              dl->AddRectFilled(V(ac.x-r,ac.y-r),V(ac.x+r,ac.y+r),
                                IA(i,AccA((int)((g_darkUI?120:96)*actA))), r); }
          if(ha>0.01f){ float r=e*(0.92f+0.08f*ha);
              dl->AddRectFilled(V(ac.x-r,ac.y-r),V(ac.x+r,ac.y+r),
                                IA(i,AccA((int)(ha*(g_darkUI?46:56)))), r); } }
        // minimised windows read as dimmed rather than getting their own marker, which keeps the
        // three taskbar states legible without adding chrome the rice does not have
        if(a.icon){ float g=1.0f+ha*0.08f, s=isz*g*0.5f;
            int ia2 = a.minimized? 150 : 255;
            dl->AddImage((ImTextureID)a.icon,V(cx+ox-s,iy+isz*0.5f-s),V(cx+ox+s,iy+isz*0.5f+s),
                         ImVec2(0,0),ImVec2(1,1),IA(i,IM_COL32(255,255,255,ia2))); }   // normal COLOUR app icons
        // more than one window in the group: a small dot under the icon (the rice's occupancy dot)
        if(a.wins.size()>1)
            dl->AddCircleFilled(V(cx+ox,iy+isz+3.0f),1.9f,
                                IA(i, active? AccA(235) : WithA(M3Secondary(),170)));
        if(hov && !g_appMenu){   // no preview while the right-click menu is open (they'd overlap)
            // IN-BAR preview: set the state; AppPreviewFlyout() draws it into the bar's own draw list
            // (captured window texture) so it SWELLS out of the strip instead of floating over it.
            if(g_barPreviews){ g_appPrev=1; g_appPrevMon=mi; g_appPrevSrc=a.hwnd;
                g_appPrevTitle=a.title.empty()?"(untitled)":a.title;
                g_appPrevAt=V(onRight? bx-8-300.0f : bx+barW-8, iy+isz*0.5f);
                g_appPrevHoverTick=GetTickCount64(); }
        }
        if(click&&hov){ DockGroupClick(a); g_appPrev=0; }
        if(rclick&&hov){ g_appPrev=0; appTookRclick=true;
            OpenAppMenu(a,mi,V(onRight? bx-8 : bx+barW+8, iy), side); }
    }
    (void)anyThumb;   // vertical bar no longer uses the DWM thumbnail window

    // ---- bar-surface plugins (drawn between the app list and the calendar) ----
    if(vis[BIT_PLUGINS] && plugH>0) PluginsDraw(dl,PSURF_BAR,bx+4,plugTop,barW-8,plugH);

    // (the rotated "Desktop" label + monitor glyph used to float in the gap here — dropped at the
    //  user's request; the empty run between the app cluster and the tray strip is the breathing
    //  room, which is how the reference shell reads anyway)

    // ---- system-tray strip (REAL Windows tray icons: Vesktop / Spotify / Steam / ...) ----
    // Dwell/cache are keyed by the app's HWND (the SysTrayIcon* isn't stable, and the strip is drawn on
    // every monitor's bar, so a pointer key + per-monitor reset never dwelled).
    // The chevron leads the strip: it is the tray's handle and keeps its slot whatever is promoted.
    if(vis[BIT_TRAY] && trayChev){ int i=staggerOf[BIT_TRAY]; float ox=IX(i);
        float ty=trayTop; ImVec2 ic=V(cx+ox,ty);
        if(g_trayFlyMon==mi){ g_trayFlyDir=onRight?3:2; g_trayFlyAt=V(onRight? bx : bx+barW, ty); }
        bool ch=io.MousePos.x>cx-half&&io.MousePos.x<cx+half&&io.MousePos.y>ty-13&&io.MousePos.y<ty+13;
        float ha=HoverAnim(HID+3601,ch||g_trayOpen);
        if(ha>0.01f) dl->AddRectFilled(V(ic.x-12,ty-12),V(ic.x+12,ty+12),
                                       IA(i,g_trayOpen?AccA((int)(60+ha*40)):WithA(COL_INK2,(int)(ha*52))),8);
        ChevronGlyph(dl,ic,5.5f,IA(i,WithA(g_trayOpen?COL_INK:COL_INK2,235)),g_trayAnim,onRight?3:2);
        if(trayHidden>0 && !g_trayOpen) dl->AddCircleFilled(V(ic.x+9,ty-8),2.4f,IA(i,AccA(230)));
        if(ch && !g_trayOpen){ tip="System tray"; tipY=ty; }
        if(click&&ch){ if(g_trayOpen) g_trayOpen=false;
            else { g_trayOpen=true; g_trayFlyMon=mi; g_trayFlyDir=onRight?3:2;
                   g_trayFlyAt=V(onRight? bx : bx+barW, ty); } } }
    for(int k=0;k<trayShown;k++){ auto* s=barTray[k]; int i=staggerOf[BIT_TRAY]; float ox=IX(i);
        float ty=trayTop+(k+(trayChev?1:0))*trayStep; ImVec2 ic=V(cx+ox,ty);
        bool th=io.MousePos.x>cx-half&&io.MousePos.x<cx+half&&io.MousePos.y>ty-14&&io.MousePos.y<ty+14;
        float ha=HoverAnim(HID+3500+k,th);
        if(ha>0.01f){ float s=13.0f*(0.9f+0.1f*ha);
            dl->AddRectFilled(V(ic.x-s,ty-s),V(ic.x+s,ty+s),AccA((int)(ha*46*IE(i))),s*0.56f); }
        float isz=22+ha*2;
        dl->AddImage((ImTextureID)s->tex,V(ic.x-isz/2,ty-isz/2),V(ic.x+isz/2,ty+isz/2),ImVec2(0,0),ImVec2(1,1),IA(i,IM_COL32(255,255,255,255)));
        if(th){ int sx=(int)(g_vs.left+(inner+6*side)*g_uiScale), sy=(int)(g_vs.top+ty*g_uiScale);
            void* key=(void*)s->hwnd;
            if(!s->tip.empty()){ tipStore=W2U8(s->tip); size_t nl=tipStore.find('\n'); if(nl!=std::string::npos)tipStore=tipStore.substr(0,nl); tip=tipStore.c_str(); tipY=ty; }
            // HOVER-DWELL -> SCRAPE the app's real menu items on a worker thread (off-screen).
            if(g_trayDwellKey!=key){ g_trayDwellKey=key; g_trayDwellSince=GetTickCount64(); g_trayDwellFired=false; }
            if(!g_trayDwellFired && GetTickCount64()-g_trayDwellSince>250){ g_trayDwellFired=true;
                // an EMPTY read is only trusted for 20 s: one unlucky first try used to leave that app without
                // our menu for the rest of the session
                bool have=false; { std::lock_guard<std::mutex> lk(g_trayMenuMtx); auto hit=g_trayMenuCache.find(key);
                    have = hit!=g_trayMenuCache.end() && (!hit->second.items.empty() || GetTickCount64()-hit->second.at<20000ULL); }
                if(!have && !g_trayScraping.exchange(true)){ SysTrayIcon cp=*s;
                    std::thread([key,cp](){ ScrapeTrayMenu(key,cp); }).detach(); } }
            // scrape ready WITH items -> MORPH our own menu out of the bar (type 5); no items (custom-UI
            // app) -> fall back to the app's native menu on right-click.
            bool ready=false,hasItems=false;
            { std::lock_guard<std::mutex> lk(g_trayMenuMtx); auto mit=g_trayMenuCache.find(key);
              ready=(mit!=g_trayMenuCache.end()&&mit->second.ready); if(ready) hasItems=!mit->second.items.empty(); }
            if(ready && hasItems){ g_barFlyHoverTick=GetTickCount64();
                if(g_barFly!=5 || g_trayMenuKey!=key){ g_barFly=5; g_barFlyMon=mi; g_trayMenuKey=key;
                    g_barFlyAt=V(onRight? bx-8-260 : bx+barW-8, ty+18.0f); } }
            if(g_barFly==5 && g_trayMenuKey==key) tip=nullptr;             // its menu is out: the tooltip would sit on top of it
            if(io.MouseClicked[2]) TrayToggleShown(*s);                    // middle-click demotes it
            else if(click) TrayForward(*s,0x201,0x202,sx,sy);              // left-click activates the app
            // a right-click on a tray icon is the icon's, never the bar's: without this the bar's own menu
            // opened on top of the app's (the vertical bar in 23_plugins.cpp already did this)
            if(rclick){ appTookRclick=true; if(!hasItems) TrayForward(*s,0x204,0x205,sx,sy); } } }  // native fallback for custom-UI apps
    // (tray strip needs no rule above it — the gap does the work)

    // ---- calendar (pops the dashboard drawer open on the Dashboard tab) ----
    // ---- focused window title, running DOWN the strip (Caelestia's ActiveWindow entry) ----
    // TextRot has been sitting unused in this file since the old rotated "Desktop" label was
    // dropped; this is what it was for. +90 degrees spins the glyphs so the line reads top-to-bottom
    // with their baseline against the strip's inner edge, which is how the reference renders it.
    if(vis[BIT_WINDOWINFO] && !wiTitleV.empty()){
        int i=staggerOf[BIT_WINDOWINFO]; float ox=IX(i);
        float run   = extB[BIT_WINDOWINFO]-22.0f;                  // room the layout gave the text
        std::string t=Clip(g_fMed,15,wiTitleV,run);
        float tw=TextW(g_fMed,15,t.c_str());
        float top=posOf[BIT_WINDOWINFO]+11.0f+(run-tw)*0.5f;       // centre it in its own run
        // rotating about the anchor sends the text down and its glyph-up toward +x, so nudge the
        // anchor left by half the cap height to land the line on the strip's centre line
        TextRot(dl,g_fMed,15.0f*g_textScale,V(cx+ox-7.0f,top),IA(i,WithA(COL_INK,235)),t.c_str(),1.5707963f);
    }
    if(vis[BIT_CALENDAR]){ int i=staggerOf[BIT_CALENDAR]; float ox=IX(i); bool hov=hit(calY,half); float ha=HoverAnim(HID+3104,hov);
        if(ha>0.01f){ float s=half*(0.92f+0.08f*ha);
            dl->AddRectFilled(V(cx+ox-s,calY-s),V(cx+ox+s,calY+s),AccA((int)(ha*44)),s*0.56f); }
        CalIcon(dl,V(cx+ox,calY),IA(i,ha>0.5f?COL_INK:COL_INK2));
        if(click&&hov){ g_tab=0; g_drawerForceUntil=GetTickCount64()+4000; }
        if(hov){ tip="Calendar"; tipY=calY; } }

    // ---- stacked clock (Clock.qml) — drawn by the ported component below the calendar ----
    if(vis[BIT_CLOCK]){ int i=staggerOf[BIT_CLOCK]; DrawBarClock(dl, cx, IX(i), ckT, IE(i), !vis[BIT_CALENDAR]); }
    // (no rule above the system cluster either; its pill already separates it)
    // ---- the single-icon controls (ethernet, wifi, bluetooth, theme, mic, audio, battery,
    //      notifications). These used to be a fixed bItem[] list pinned to the bottom edge; they are
    //      ordinary registry entries now, so each one can be hidden or dragged anywhere in the strip
    //      - including out of the system pill entirely. ----
    // one shared state layer for every cluster control: a springy rounded squircle
    auto sysCell=[&](float yy,float ha){
        if(ha<=0.01f) return;
        float s=half*(0.92f+0.08f*ha);
        dl->AddRectFilled(V(cx-s,yy-s),V(cx+s,yy+s),AccA((int)(ha*46)),s*0.56f);
    };
    // StatusIcons.qml: `property color colour: Colours.palette.m3secondary` - the whole cluster is
    // one role, not plain ink. SIH is the hovered reading of it (upstream's StateLayer sits over the
    // icon rather than recolouring it, but on a 40px strip a colour lift is what actually registers).
    const ImU32 SI  = M3Secondary();
    const ImU32 SIH = Mix(SI, COL_INK, 0.45f);
    for(auto& sl:slots){
        int id=sl.id;
        if(id!=BIT_NOTIF&&id!=BIT_ETH&&id!=BIT_WIFI&&id!=BIT_BT&&
           id!=BIT_THEME&&id!=BIT_MIC&&id!=BIT_AUDIO&&id!=BIT_BATT) continue;
        int i=staggerOf[id]; float ox=IX(i); float baseY=sl.pos+CELL*0.5f;
        ImVec2 c=V(cx+ox,baseY);
        bool hov=dist(io.MousePos,V(cx,baseY))<half;
        switch(id){
        case BIT_NOTIF: {
            // Notifications were only reachable by throwing the cursor into the screen corner, which
            // is why the summon zone had been widened to a third of the screen in the first place.
            // A bell with an unread badge makes them discoverable without any hover trap at all.
            sysCell(baseY,HoverAnim(HID+3106,hov));
            ImU32 col=IA(i, g_dnd? WithA(SI,110) : (hov?SIH:SI));
            dl->PathArcTo(V(c.x,c.y+1.5f),6.5f,3.1416f,6.2832f,14);           // bell dome
            dl->PathLineTo(V(c.x+8.0f,c.y+4.0f)); dl->PathLineTo(V(c.x-8.0f,c.y+4.0f));
            dl->PathStroke(col,ImDrawFlags_Closed,1.7f);
            dl->AddLine(V(c.x-2.2f,c.y+6.4f),V(c.x+2.2f,c.y+6.4f),col,1.7f);  // clapper
            if(g_dnd) dl->AddLine(V(c.x-8,c.y+8),V(c.x+8,c.y-8),IA(i,COL_ERR),1.8f);
            else if(notifN>0){
                // A dot, not a number: on a ~39px strip a digit is unreadable and just muddies the
                // glyph. The ring in the bar's own fill punches it clear of the bell outline.
                ImVec2 bp=V(c.x+6.5f,c.y-6.0f);
                dl->AddCircleFilled(bp,5.0f, g_darkUI?IM_COL32(18,18,22,255):PanelCol(255));
                dl->AddCircleFilled(bp,3.4f, IA(i,COL_GOLD)); }
            if(hov){ tip = g_dnd? "Do not disturb (right-click to allow)" : "Notifications (right-click for DND)"; tipY=baseY; }
            if(click&&hov){ g_notifForceUntil = (GetTickCount64()<g_notifForceUntil)? 0
                                                                                    : GetTickCount64()+8000; }
            if(rclick&&hov){ g_dnd=!g_dnd; SaveConfig(); appTookRclick=true; }
        } break;
        case BIT_ETH: {
            sysCell(baseY,HoverAnim(HID+3110,hov));
            if(hov && g_barFly!=3){ tip="Ethernet"; tipY=baseY; }   // no tooltip while its flyout is open
            // RJ45 plug: body + latch tab + contact pins. The old glyph was a rectangle on a stand,
            // which read as a second MONITOR icon right under the "Desktop" monitor glyph.
            ImU32 col=IA(i,hov?SIH:SI);
            dl->AddRect(V(c.x-7,c.y-7),V(c.x+7,c.y+4),col,2.0f,0,1.7f);       // plug body
            dl->AddRectFilled(V(c.x-2.5f,c.y+4),V(c.x+2.5f,c.y+8),col,1.0f);  // latch tab
            for(int p=-2;p<=2;p++) dl->AddRectFilled(V(c.x+p*3.0f-0.6f,c.y-5),V(c.x+p*3.0f+0.6f,c.y-1),col,0.5f);
            if(hov){ g_barFlyHoverTick=GetTickCount64();
                if(g_barFly!=3){ g_barFly=3; g_barFlyMon=mi;
                    g_barFlyAt=V(onRight? bx-8-274 : bx+barW-8, baseY+20.0f); RefreshEth(); }
                if(click) g_barFlyPin=(g_barFlyPin==3?0:3); }
        } break;
        case BIT_WIFI: {
            sysCell(baseY,HoverAnim(HID+3111,hov));
            if(hov && g_barFly!=1){ tip=g_st.online?"Network":"Offline"; tipY=baseY; }
            WifiIcon(dl, V(c.x,c.y-4), IA(i,g_st.online?SIH:WithA(SI,150)));
            if(hov){ g_barFlyHoverTick=GetTickCount64();
                if(g_barFly!=1){ g_barFly=1; g_barFlyMon=mi;
                    g_barFlyAt=V(onRight? bx-8-274 : bx+barW-8, baseY+20.0f); RefreshWifi(); }
                if(click) g_barFlyPin=(g_barFlyPin==1?0:1); }
        } break;
        case BIT_BT: {
            sysCell(baseY,HoverAnim(HID+3112,hov));
            if(hov && g_barFly!=2){ tip="Bluetooth"; tipY=baseY; }
            BtIcon(dl, c, IA(i,hov?SIH:SI));
            if(hov){ g_barFlyHoverTick=GetTickCount64();
                if(g_barFly!=2){ g_barFly=2; g_barFlyMon=mi;
                    g_barFlyAt=V(onRight? bx-8-274 : bx+barW-8, baseY+20.0f); RefreshBt(); }
                if(click) g_barFlyPin=(g_barFlyPin==2?0:2); }
        } break;
        case BIT_CUSTOM1: case BIT_CUSTOM2: case BIT_CUSTOM3:
        case BIT_CUSTOM4: case BIT_CUSTOM5: case BIT_CUSTOM6: {
            int ci=sl.id-BIT_CUSTOM1; float ha=HoverAnim(HID+3200+ci,hov);
            BarCustomDrawV(dl,ci,c.x,c.y,13.0f,IA(i,hov?SIH:SI),ha,CELL*0.5f);
            if(click&&hov) BarCustomClick(ci);
            if(hov){ const char* t=BarCustomTip(ci); if(t){ tip=t; tipY=baseY; } }
        } break;
        case BIT_THEME: {
            sysCell(baseY,HoverAnim(HID+3101,hov));
            if(g_darkUI) MoonIcon(dl,c,IA(i,hov?SIH:SI),COL_PANELL);
            else         SunIcon (dl,c,IA(i,hov?SIH:SI));
            if(click&&hov){ g_themeMode = g_darkUI?1:2; ApplyThemeMode(); SaveConfig(); g_deskDirty=true; }
            if(hov){ tip=g_darkUI?"Switch to light":"Switch to dark"; tipY=baseY; }
        } break;
        case BIT_MIC: {
            sysCell(baseY,HoverAnim(HID+3102,hov));
            MicIcon(dl,c,IA(i,g_micMuted?COL_ERR:(hov?SIH:SI)),g_micMuted);
            if(click&&hov){ g_micMuted=!g_micMuted; SetMicMute(g_micMuted); }
            if(hov){ tip=g_micMuted?"Microphone muted":"Microphone on"; tipY=baseY; }
        } break;
        case BIT_AUDIO: {
            sysCell(baseY,HoverAnim(HID+3103,hov));
            SpeakerIcon(dl,c,IA(i,hov?SIH:SI));
            if(hov){ static char vt[24]; snprintf(vt,24,"Volume %d%%",(int)(std::clamp(g_volCache,0.0f,1.0f)*100));
                     tip = g_volCache>=0.0f? vt : "Volume mixer"; tipY=baseY; }
            if(click&&hov){ g_sideView=4; g_sideForceUntil=GetTickCount64()+4000; }
        } break;
        case BIT_BATT: {
            // a real battery glyph with a fill level, not a bare percentage
            sysCell(baseY,HoverAnim(HID+3105,hov));
            float pct=std::clamp(g_st.battPct/100.0f,0.0f,1.0f);
            ImU32 shell=IA(i,WithA(SI,190));
            ImU32 fill =IA(i, g_st.charging? COL_GOLD : (pct<0.15f? COL_ERR : SIH));
            ImVec2 b0=V(c.x-8,c.y-5), b1=V(c.x+7,c.y+5);
            dl->AddRect(b0,b1,shell,2.5f,0,1.4f);
            dl->AddRectFilled(V(b1.x+1,c.y-2),V(b1.x+3,c.y+2),shell,1.0f);            // terminal
            dl->AddRectFilled(V(b0.x+2,b0.y+2),V(b0.x+2+(b1.x-b0.x-4)*pct,b1.y-2),fill,1.5f);
            if(g_st.charging){ ImU32 bolt=IA(i,COL_GOLD);                              // charging bolt
                dl->AddLine(V(c.x+1,c.y-4),V(c.x-2,c.y),bolt,1.6f);
                dl->AddLine(V(c.x-2,c.y),V(c.x+1,c.y+4),bolt,1.6f); }
            if(hov){ static char bt2[24]; snprintf(bt2,24,"Battery %d%%%s",g_st.battPct,g_st.charging?" (charging)":"");
                     tip=bt2; tipY=baseY; }
        } break;
        }
    }
    // power (very bottom, outside the pill) — Power.qml, drawn by the ported component
    if(vis[BIT_POWER]){ int i=staggerOf[BIT_POWER]; const char* t=DrawBarPower(dl,io,cx,IX(i),powY,half,IE(i),click); if(t){tip=t;tipY=powY;} }

    dl->ChannelsMerge();   // flatten pill(0)+icons(1) before menus/flyouts/tooltips draw on top

    // ---- right-click context menu on the taskbar ----
    { bool onStrip = io.MousePos.x>bx && io.MousePos.x<bx+barW && io.MousePos.y>by && io.MousePos.y<by+bh;
      if(rclick && onStrip && !appTookRclick){ g_barMenu=true; g_barMenuMon=mi; g_barMenuOpenedAt=GetTickCount64();
          // the menu is 6 rows x 34 + 12 = 216 tall; clamping to 200 let it hang 16 px off the bottom of the
          // screen when opened low on the bar, cutting "Exit shell" in half
          g_barMenuAt=V(onRight? bx-8-200 : bx+barW+8, std::max((float)by,std::min(io.MousePos.y,(float)(by+bh)-(34.0f*6+12)))); }
      // the menu belongs to the monitor whose bar was right-clicked; the others skip it
      bool mine2 = (g_barMenuMon==mi);
      float mt=(g_barMenu&&mine2)?1.0f:0.0f;
      if(mine2) g_barMenuAnim = MotionAnim(MP_POPOUT,810003, mt);   // popout spring
      if(mine2 && g_barMenuAnim>0.004f){
          float a=g_barMenuAnim;
          float af=std::clamp(g_barMenuAnim,0.0f,1.0f); int al=(int)(af*255);
          const char* items[6]={"Snip a screenshot","Auto-hide taskbar","Keep awake","Settings","Restart shell","Exit shell"};
          const int NM=6;
          float mw=200, rowH=34, mh=rowH*NM+12;
          float mx0=g_barMenuAt.x-(1.0f-a)*14.0f*side, my0=g_barMenuAt.y+(1.0f-a)*6.0f;
          ImVec2 m0=V(mx0,my0), m1=V(mx0+mw,my0+mh);
          GlassPanel(dl,m0,m1,12,0,nullptr,af);
          for(int i=0;i<NM;i++){
              float ra=EaseOutCubic(Stagger(af,i,0.05f,0.5f));
              float ry=m0.y+6+i*rowH+(1.0f-ra)*8.0f;
              bool rh2=g_barMenu&&io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>ry&&io.MousePos.y<ry+rowH;
              if(rh2) dl->AddRectFilled(V(m0.x+5,ry+1),V(m1.x-5,ry+rowH-1),AccA((int)(af*26)),8);
              TextAt(dl,g_fSml,15,V(m0.x+34,ry+9),WithA(COL_INK,(int)(al*ra)),items[i]);
              bool checked=(i==1&&g_barAutoHide)||(i==2&&g_keepAwake);
              if(checked){ ImVec2 k=V(m0.x+18,ry+rowH*0.5f);
                  dl->AddLine(V(k.x-5,k.y),V(k.x-1,k.y+4),AccA((int)(al*ra)),2.0f);
                  dl->AddLine(V(k.x-1,k.y+4),V(k.x+6,k.y-4),AccA((int)(al*ra)),2.0f); }
              if(click&&rh2){
                  if(i==0){ g_barMenu=false; g_snipPending=8; }   // fire after the bar/menu hide (~8 frames)
                  else if(i==1){ g_barAutoHide=!g_barAutoHide; SaveConfig(); }
                  else if(i==2){ g_keepAwake=!g_keepAwake; ApplyKeepAwake(); }
                  else if(i==3){ g_setShow=true; }
                  else if(i==4){ RestartShell(); }
                  else PostMessageW(g_hwnd,WM_CLOSE,0,0);
                  g_barMenu=false; }
          }
          if(g_barMenu){   // menu must be clickable (it sits outside the strip)
              BS.rect.left  =(LONG)std::min((float)BS.rect.left,m0.x);
              BS.rect.top   =(LONG)std::min((float)BS.rect.top,m0.y);
              BS.rect.right =(LONG)std::max((float)BS.rect.right,m1.x);
              BS.rect.bottom=(LONG)std::max((float)BS.rect.bottom,m1.y);
              bool inMenu=io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>m0.y&&io.MousePos.y<m1.y;
              if((click||rclick)&&!inMenu&&!onStrip) g_barMenu=false;
              static MenuDismiss dis;
              if(MenuOutsideClick(dis,inMenu,g_barMenuOpenedAt)) g_barMenu=false;
              if(ImGui::IsKeyPressed(ImGuiKey_Escape)) g_barMenu=false; }
      } }

    SysFlyout(dl,io,mi,BS.rect);              // morphing Wi-Fi/Bluetooth flyout
    TrayFlyout(dl,io,mi,BS.rect);             // the tray chevron's panel
    AppPreviewFlyout(dl,io,mi,BS.rect);       // in-bar app hover preview (swells out of the strip)
    if(tip) BarTip(dl,tipX,tipY,tip,side);   // draw last, on top of everything
}
// the bar window spans every monitor: draw a strip on each one that is meant to have it
#include "src/components/AlertCard.h"       // "Couldn't open X" popups (SafeLaunch.h)
static void DrawRecordHud(ImDrawList* dl,ImGuiIO& io);   // fwd (RecordHud.h)
static void DrawTourCoach(ImDrawList* dl,ImGuiIO& io);   // fwd (SettingsFx.h: the guided tour's "try it" card)
static bool RecHudActive();                              // fwd
static void DrawBar(){
    BarThemeSwap barTheme;               // bar.custom_colours, for this pass only (see BarThemeSwap)
    { STALL(bar_custom); BarCustomTick(); }   // refresh any custom item whose interval has come round
    g_thumbWanted=false;
    g_barTipRect=RECT{0,0,0,0};   // BarTip refills this if anything is hovered this frame
    if(g_mons.empty()){ ImGuiIO& io=ImGui::GetIO();
        DrawBarOn(0,0,0,io.DisplaySize.x,io.DisplaySize.y); }
    else {
        if((int)g_bars.size()<(int)g_mons.size()) g_bars.resize(g_mons.size());
        for(size_t i=0;i<g_mons.size();i++){
            if(!BarOnMon((int)i)){ Bar((int)i).rect=RECT{0,0,0,0}; continue; }
            const RECT& r=g_mons[i].rc;
            STALL(bar_strip);
            DrawBarOn((int)i,(r.left-g_vs.left)/g_uiScale,(r.top-g_vs.top)/g_uiScale,
                      (r.right-r.left)/g_uiScale,(r.bottom-r.top)/g_uiScale);
        }
    }
    { ImGuiIO& io=ImGui::GetIO(); DrawRecordHud(ImGui::GetBackgroundDrawList(),io); }
    { ImGuiIO& io=ImGui::GetIO(); DrawAlerts(ImGui::GetForegroundDrawList(),io); }   // error / progress popups
    { ImGuiIO& io=ImGui::GetIO(); DrawTourCoach(ImGui::GetForegroundDrawList(),io); }   // guided tour: "try it" steps   // the recording checker lives in the bar window
    { STALL(bar_overlays); ImGuiIO& io=ImGui::GetIO(); DrawAppMenu(ImGui::GetBackgroundDrawList(),io); }   // app right-click menu (drawn once, above the strips)
    if(!g_thumbWanted){ STALL(bar_hidethumb); HideThumb(); }
}

static void SpeakerIcon(ImDrawList* dl,ImVec2 c,ImU32 col){
    { float v=g_volCache; const char* n = v<0.0f? "audio-volume-high" : v<0.005f? "audio-volume-muted"
                                       : v<0.34f? "audio-volume-low" : v<0.67f? "audio-volume-medium" : "audio-volume-high";
      if(ThemedSym(dl,c,10.0f,col,n)) return; }
    dl->AddRectFilled(V(c.x-9,c.y-3),V(c.x-3,c.y+3),col);
    dl->AddTriangleFilled(V(c.x-3,c.y-7),V(c.x-3,c.y+7),V(c.x+3,c.y),col);
    dl->PathArcTo(V(c.x+2,c.y),6,-0.7f,0.7f,8); dl->PathStroke(col,0,1.6f);
    dl->PathArcTo(V(c.x+2,c.y),10,-0.7f,0.7f,10); dl->PathStroke(col,0,1.6f);
}
static void SunIcon(ImDrawList* dl,ImVec2 c,ImU32 col){
    // the themed SVG cannot animate, so the drawn glyph is used whenever idle motion is on
    if(!g_idleMotion && ThemedSym(dl,c,10.0f,col,"weather-clear")) return;
    SunGlyph(dl,c,10.6f,col);
}
// GetBrightness talks DDC/CI to the panel over the video link, which is slow enough to be felt
// (tens to hundreds of ms) and was done inline the first time any panel showing a brightness slider
// drew. Probed once on a worker instead; until it lands the slider simply is not offered.
static float g_brtCache=-1; static int g_brtMax=100;
static std::atomic<bool> g_brtOkA{false};
static std::atomic<int>  g_brtState{0};    // 0 unprobed, 1 probing, 2 done
#define g_brtOk (g_brtOkA.load())
static void BrightnessProbe(){
    int expect=0;
    if(!g_brtState.compare_exchange_strong(expect,1)) return;   // already probing or done
    std::thread([]{
        int c=0,m=0; bool ok=GetBrightness(c,m);
        if(ok){ g_brtMax=m?m:100; g_brtCache=(float)c/(m?m:100); }
        g_brtOkA.store(ok); g_brtState.store(2);
    }).detach();
}
