// Aether - the layout engine (panel geometry).
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ===================================================================== LAYOUT ENGINE (layer 2)
// Panel geometry used to be hardcoded pixels inside each Draw* function, so a preset could only
// ever change colours — it could not move or resize anything. Every movable panel now reads its
// rect from a descriptor, making position/size/length/anchor DATA. The draw code AND the loop's
// hover-reveal math both go through PanelRect(), so the two can no longer drift apart (they had).
enum { EDGE_LEFT=0, EDGE_RIGHT, EDGE_TOP, EDGE_BOTTOM };
enum { PN_BAR=0, PN_DRAWER, PN_QS, PN_COUNT };
struct Panel {
    int   edge;      // which screen edge the panel docks to
    float size;      // thickness perpendicular to that edge (bar width / drawer height)
    float span;      // extent ALONG the edge: <=1 = fraction of the edge, >1 = absolute logical px
    float spanMax;   // cap applied to a fractional span (0 = uncapped)
    float anchor;    // 0..1 position along the edge: 0 = start, 0.5 = centre, 1 = end
    float gap;       // margin between the panel and the screen edge (also insets the span)
    int   order;     // stacking order along the edge (reserved for layer 3)
    bool  visible;
};
static Panel g_pn[PN_COUNT] = {
    /* bar    */ { EDGE_LEFT,   48, 1.00f,   0, 0.5f, 10, 0, true },
    /* drawer */ { EDGE_TOP,   424, 0.46f, 880, 0.5f,  0, 0, true },
    /* quick  */ { EDGE_RIGHT, 384, 0.00f,   0, 1.0f, 10, 0, true },  // span set live (height varies by view)
};
static const char* PANEL_ID[PN_COUNT] = { "bar", "dashboard", "quickSettings" };
// The rice keeps quick settings, the player and the resource gauges in ONE right-edge panel rather
// than three separate flyouts. Both extra sections are opt-out, because on a short screen the panel
// would otherwise run past the bottom edge.
static bool g_qsMedia  = true;    // now-playing strip under the tray
static bool g_qsGauges = true;    // CPU / GPU / RAM shape gauges under that
static const char* EDGE_NAME[4]       = { "left", "right", "top", "bottom" };
static const char* EDGE_CAP[4]        = { "Left", "Right", "Top", "Bottom" };
static int ParseEdge(const std::string& s){ for(int i=0;i<4;i++) if(s==EDGE_NAME[i]) return i; return -1; }

struct PRect { float x,y,w,h; };
static bool PanelVert(const Panel& p){ return p.edge==EDGE_LEFT || p.edge==EDGE_RIGHT; }
// the resting (fully open) rect in LOGICAL px. spanOverride lets a panel whose length is computed
// per-frame (quick settings' height changes with the view) still go through the same geometry.
static PRect PanelRect(const Panel& p,float W,float H,float spanOverride=0.0f){
    float edgeLen = PanelVert(p)? H : W;
    float avail   = std::max(1.0f, edgeLen - 2*p.gap);
    float s = (spanOverride>0.0f)? spanOverride : (p.span<=1.0f ? avail*p.span : p.span);
    if(spanOverride<=0.0f && p.span<=1.0f && p.spanMax>0) s=std::min(s,p.spanMax);
    s = std::clamp(s,1.0f,avail);
    float pos = p.gap + (avail-s)*std::clamp(p.anchor,0.0f,1.0f);
    PRect r;
    if(PanelVert(p)){ r.w=p.size; r.h=s; r.y=pos; r.x=(p.edge==EDGE_LEFT)? p.gap : W-p.gap-p.size; }
    else            { r.h=p.size; r.w=s; r.x=pos; r.y=(p.edge==EDGE_TOP )? p.gap : H-p.gap-p.size; }
    return r;
}
// slide the rect off its own edge: s=1 fully open, s=0 fully hidden
static void PanelSlide(const Panel& p,PRect& r,float s,float extra=0.0f){
    float d=(1.0f-s)*(p.size+p.gap*2.0f+extra);
    switch(p.edge){ case EDGE_LEFT: r.x-=d; break; case EDGE_RIGHT: r.x+=d; break;
                    case EDGE_TOP:  r.y-=d; break; default:         r.y+=d; }
}
// a panel flush with its edge (gap 0) rounds only the corners facing away from that edge
static ImDrawFlags PanelCorners(const Panel& p){
    // This used to leave the DOCKED edge square whenever a panel sat flush against the screen
    // (gap 0), so it would read as growing out of that edge. That assumes a shell with no margin.
    // The desktop here is an inset bubble, so there is always surround behind a panel, and a flush
    // panel just looked like a rectangle with two corners sliced off - which is exactly what the
    // dashboard drawer was showing along its top edge, gap being 0 in the saved layout.
    // Everything is round now; a panel that wants to bleed into an edge can still say so by
    // setting its corner radius to 0.
    (void)p; return 0;   // 0 == ImDrawFlags_RoundCornersAll
}
static RECT PanelHitRect(const PRect& r){
    return RECT{ (LONG)std::max(0.0f,r.x), (LONG)std::max(0.0f,r.y), (LONG)(r.x+r.w), (LONG)(r.y+r.h) };
}
// cursor at this panel's screen edge, within its span — the hover-reveal trigger
static bool PanelEdgeHot(const Panel& p,float lx,float ly,float W,float H,float slack,float spanOverride=0.0f){
    PRect r=PanelRect(p,W,H,spanOverride);
    float d,a,a0,a1;
    switch(p.edge){ case EDGE_LEFT: d=lx; break; case EDGE_RIGHT: d=W-1-lx; break;
                    case EDGE_TOP:  d=ly; break; default:         d=H-1-ly; }
    if(PanelVert(p)){ a=ly; a0=r.y; a1=r.y+r.h; } else { a=lx; a0=r.x; a1=r.x+r.w; }
    return d<=4.0f && d>=-30.0f && a>a0-slack && a<a1+slack;
}
// cursor inside the panel (padded) — keeps it open once it is out
static bool PanelOver(const PRect& r,float lx,float ly,float pad){
    return lx>r.x-pad && lx<r.x+r.w+pad && ly>r.y-pad && ly<r.y+r.h+pad;
}

// Which monitors carry a taskbar: every one (like Caelestia's per-output bars) or just the primary.
static int  g_barMonMode=0;                     // 0 = all monitors, 1 = primary only
static bool BarOnMon(int mi){
    if(!g_pn[PN_BAR].visible) return false;
    if(mi<0||mi>=(int)g_mons.size()) return g_barMonMode==0;
    if(!g_mons[mi].cfg.bar) return false;             // switched off for this display
    return g_barMonMode==0 || g_mons[mi].primary;
}
static bool DeskOnMon(int mi){
    if(mi<0||mi>=(int)g_mons.size()) return true;
    return g_mons[mi].cfg.desktop;
}
// The bar lives in the MARGIN outside the desktop bubble, so the bubble's inset on the bar's edge is
// derived from the bar panel — one source of truth (D3: the corners must line up with the taskbar).
// A monitor with no bar just gets the plain gap on every side.
static void BubbleInsetsFor(int mi,int& l,int& t,int& r,int& b){
    if(!DeskOnMon(mi) && !BarOnMon(mi)){ l=t=r=b=0; return; }   // shell is off on this display
    int g = (g_bubble && DeskOnMon(mi))? g_gap : 0; l=t=r=b=g;
    if(g_bubble && BarOnMon(mi)){
        int th=(int)(g_pn[PN_BAR].gap*2 + g_pn[PN_BAR].size);
        switch(g_pn[PN_BAR].edge){ case EDGE_LEFT: l=th; break; case EDGE_RIGHT: r=th; break;
                                   case EDGE_TOP:  t=th; break; default:         b=th; break; }
    }
}
static void BubbleInsets(int& l,int& t,int& r,int& b){ BubbleInsetsFor(g_actMon,l,t,r,b); }
static int  LeftGapL(){ int l,t,r,b; BubbleInsets(l,t,r,b); return l; }   // logical px
static RECT BubbleRectFor(int mi){                                       // physical px (SPI_SETWORKAREA)
    int l,t,r,b; BubbleInsetsFor(mi,l,t,r,b);
    l=(int)(l*g_uiScale); t=(int)(t*g_uiScale); r=(int)(r*g_uiScale); b=(int)(b*g_uiScale);
    RECT m = (mi>=0&&mi<(int)g_mons.size())? g_mons[mi].rc : RECT{g_mx,g_my,g_mx+g_mw,g_my+g_mh};
    RECT rc={ m.left+l, m.top+t, m.right-r, m.bottom-b }; return rc;
}
static RECT BubbleRect(){ return BubbleRectFor(g_actMon); }
// reserve the bar's margin on EVERY monitor. SPI_SETWORKAREA applies to the monitor containing the
// rect (verified on this box), so one call per monitor is all it takes.
static void ApplyWorkAreas(){
    for(size_t i=0;i<g_mons.size();i++){ RECT wa=BubbleRectFor((int)i);
        SystemParametersInfoW(SPI_SETWORKAREA,0,&wa,SPIF_SENDCHANGE); }
}
// Setting the work area is not a thing that STAYS set. Windows puts it back to the whole monitor
// behind our back - after its own bookkeeping following an SPI_SETWORKAREA broadcast, on a display
// mode change, and when a full-screen game exits. Measured here: turning the bubble off and
// straight back on left the area full-screen, because the reset from the first call landed after
// the second one. So instead of setting it once and hoping, compare what Windows actually has
// against what the bubble wants, and re-assert only when they differ.
static void EnsureWorkAreas(){
    if(!g_taskbarHidden) return;
    // Re-asserted unconditionally, with NO drift check: GetMonitorInfo's rcWork goes stale after
    // Windows resets the work area, so comparing against it said "already correct" while
    // SPI_GETWORKAREA reported the whole monitor - and the inset was never put back.
    //
    // And NO SPIF_SENDCHANGE: this runs on a timer, and each broadcast is a WM_SETTINGCHANGE to
    // every top-level window on the system, including our own panels. Re-fitting the windows is
    // ReflowWindows' job, so nothing needs telling; without the broadcast this is just a cheap
    // write of a value Windows already holds.
    for(size_t i=0;i<g_mons.size();i++){
        RECT want=BubbleRectFor((int)i);
        SystemParametersInfoW(SPI_SETWORKAREA,0,&want,0);
    }
}
// Windows applies a new work area ONLY to windows that maximize after it changes. Everything
// already on screen keeps the bounds it was given, so moving the bar, changing its edge margin or
// resizing the bubble left every open app straddling the edge - hanging outside the bubble and
// running under the bar. Re-fitting them is a thing we have to do ourselves.
static bool g_confineApps=true;        // desktop.confineApps
static bool g_bubbleGeomDirty=false;   // set while a size slider is being dragged; flushed on release
// Re-fitting apps means restoring, re-maximizing and moving OTHER people's windows, and that churns
// activation and z-order. Doing it underneath our own input-taking panels made the settings window
// lose and regain focus mid-interaction - measured: a toggle flipped itself back about two seconds
// after being clicked. So the sweep waits until nothing of ours is holding the keyboard.
static bool ShellUiBusy();
// Maximized windows already re-fitted once, keyed by window, with the bubble rect they were fitted to.
// A window that CANNOT fill the bubble (a maximum size, or an app that snaps its own maximized frame)
// never passes the "already fills it" test, so it used to be restored and re-maximized every 1.5 s
// forever - reproduced: 12 resizes in 10 s on a size-capped window, which is the "screen shakes and
// flickers black at random" report. One attempt per bubble shape; a new shape (or the window leaving
// the maximized state) allows another.
static std::unordered_map<HWND,RECT> g_refitTried;
// ---- never wait on a window owned by another app ------------------------------------------------
// ShowWindow, SetWindowPos, SetWindowPlacement and SetWindowRgn on a window owned by another thread
// SEND it messages and wait for the answer. If that app has stopped responding, the shell stops with
// it. Reproduced: one unresponsive window hanging off the bubble froze Aether solid - every ping
// timed out for 14 s - because the confinement sweep sat inside SetWindowPos waiting for it. That is
// the "opening Windows Settings freezes Aether" report: with no Explorer, Settings is exactly the
// kind of app that hangs while it starts. Moves and shows now go through the async forms, and calls
// that have no async form first check the window is alive, with a short bounded probe.
// A PENALTY BOX, because "bounded" per call is not bounded per pass. The probe below costs 50 ms
// against a window that has stopped pumping, and the dock asks it of EVERY window, twice a second,
// on the render thread - so one quiet app is not a 50 ms hiccup, it is 50 ms x every window x for
// as long as it stays quiet. Measured over a day of stall.txt: RefreshDock's median is 238 ms but
// its p95 is 3014 ms and its worst 18182 ms, i.e. the shell stood still for eighteen seconds
// because somebody else's window went to sleep. The distribution is the tell - a tight median with
// a tail that long is never "slow code", it is timeouts being paid in full.
//
// So: a window that fails a probe is not probed again for a few seconds. The cost of it going
// quiet is now ONE timeout, not one per window per pass, and the shell notices it came back within
// PROBE_PENALTY_MS. Recorded per window rather than per process on purpose: an app can have one
// wedged window and five healthy ones (Explorer does this constantly).
static const ULONGLONG PROBE_PENALTY_MS = 4000;
static std::unordered_map<HWND,ULONGLONG> g_probeBad;   // hwnd -> tick it may be probed again
static ULONGLONG g_probeBudget = 0;                     // 0 = no budget in force
// Touched only from the thread that owns the message loop: the dock sweep, the confinement sweep,
// and AeLocationEvt (an OUTOFCONTEXT hook, delivered to the registering thread). No lock needed.

static bool ProbeAllowed(HWND h){
    ULONGLONG now=GetTickCount64();
    if(g_probeBudget && now>=g_probeBudget) return false;   // this pass has spent its share
    auto it=g_probeBad.find(h);
    return it==g_probeBad.end() || now>=it->second;
}
static void ProbeFailed(HWND h){ g_probeBad[h]=GetTickCount64()+PROBE_PENALTY_MS; }
static void ProbeOk(HWND h){ if(!g_probeBad.empty()) g_probeBad.erase(h); }
// Caps the blocking probes ONE sweep may do. Scoped, so every other caller keeps probing freely -
// the budget is about not spending a whole frame in the dock, not about never asking.
struct ProbeBudget {
    explicit ProbeBudget(int ms){ g_probeBudget=GetTickCount64()+(ULONGLONG)ms; }
    ~ProbeBudget(){ g_probeBudget=0; }
};
static void ProbePrune(){                                // closed windows must not accumulate
    if(g_probeBad.size()<256) return;
    for(auto it=g_probeBad.begin(); it!=g_probeBad.end(); )
        it = IsWindow(it->first) ? std::next(it) : g_probeBad.erase(it);
}

static bool WindowResponsive(HWND h){
    if(!h || IsHungAppWindow(h)) return false;
    if(!ProbeAllowed(h)) return false;                   // asked recently, it did not answer
    DWORD_PTR r=0;
    if(SendMessageTimeoutW(h,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,50,&r)){ ProbeOk(h); return true; }
    ProbeFailed(h);
    return false;
}
static void ReflowWindows(){
    if(!g_confineApps || !g_bubble) return;
    if(ShellUiBusy()) return;
    for(auto it=g_refitTried.begin(); it!=g_refitTried.end(); )
        it = (IsWindow(it->first) && IsZoomed(it->first)) ? std::next(it) : g_refitTried.erase(it);
    EnumWindows([](HWND h,LPARAM)->BOOL{
        if(!IsWindowVisible(h) || IsIconic(h)) return TRUE;
        LONG ex=GetWindowLongW(h,GWL_EXSTYLE);
        if(ex&WS_EX_TOOLWINDOW) return TRUE;                       // palettes, tooltips
        LONG st=GetWindowLongW(h,GWL_STYLE);
        // Only windows the user can move or resize. A borderless full-screen game has neither, and
        // dragging one into the bubble would be actively wrong.
        if(!(st&WS_CAPTION) && !(st&WS_THICKFRAME)) return TRUE;
        int ck=0; if(SUCCEEDED(DwmGetWindowAttribute(h,DWMWA_CLOAKED,&ck,sizeof(ck))) && ck) return TRUE;
        DWORD pid=0; GetWindowThreadProcessId(h,&pid);
        if(pid==GetCurrentProcessId()) return TRUE;                // never our own overlays
        wchar_t cls[128]={0}; GetClassNameW(h,cls,128);
        static const wchar_t* SKIP[]={ L"Shell_TrayWnd",L"Shell_SecondaryTrayWnd",L"Progman",
                                       L"WorkerW",L"Windows.UI.Core.CoreWindow",nullptr };
        for(int i=0;SKIP[i];i++) if(!wcscmp(cls,SKIP[i])) return TRUE;
        RECT wr{}; if(!GetWindowRect(h,&wr)) return TRUE;
        if(wr.right-wr.left<120 || wr.bottom-wr.top<80) return TRUE;
        if(IsHungAppWindow(h)) return TRUE;                        // it cannot move now; do not queue on it
        int mi=MonIndexAt(POINT{(wr.left+wr.right)/2,(wr.top+wr.bottom)/2});
        RECT wa=BubbleRectFor(mi);
        if(wa.right-wa.left<160 || wa.bottom-wa.top<120) return TRUE;
        if(IsZoomed(h)){
            // A maximized window remembers the work area it was maximized INTO. Restoring and
            // re-maximizing is the only way to make it pick the new one up - but this runs on a
            // timer as well as on demand, so it MUST be a no-op once the window already fills the
            // bubble, or every maximized app would flicker through a restore twice a second.
            RECT fb=wr; DwmGetWindowAttribute(h,DWMWA_EXTENDED_FRAME_BOUNDS,&fb,sizeof(fb));
            auto near2=[](LONG a,LONG b){ return a-b<=2 && b-a<=2; };
            if(near2(fb.left,wa.left) && near2(fb.top,wa.top) &&
               near2(fb.right,wa.right) && near2(fb.bottom,wa.bottom)){ g_refitTried.erase(h); return TRUE; }
            { auto it=g_refitTried.find(h);
              if(it!=g_refitTried.end() && EqualRect(&it->second,&wa)) return TRUE; }   // tried already: it cannot fit
            if(!WindowResponsive(h)) return TRUE;
            g_refitTried[h]=wa;
            ShowWindowAsync(h,SW_RESTORE); ShowWindowAsync(h,SW_MAXIMIZE);
            return TRUE;
        }
        // GetWindowRect includes DWM's invisible resize border, so placing by it leaves a visible
        // gap on three sides. The extended frame bounds are where the window actually LOOKS like
        // it is; the difference is the padding to add back when calling SetWindowPos.
        RECT fb=wr;
        DwmGetWindowAttribute(h,DWMWA_EXTENDED_FRAME_BOUNDS,&fb,sizeof(fb));
        int padL=fb.left-wr.left, padT=fb.top-wr.top, padR=wr.right-fb.right, padB=wr.bottom-fb.bottom;
        int vw=fb.right-fb.left, vh=fb.bottom-fb.top;
        int nw=std::min<int>(vw, wa.right-wa.left), nh=std::min<int>(vh, wa.bottom-wa.top);
        int nx=std::clamp<int>(fb.left, wa.left, wa.right-nw);
        int ny=std::clamp<int>(fb.top,  wa.top,  wa.bottom-nh);
        if(nx==fb.left && ny==fb.top && nw==vw && nh==vh) return TRUE;   // already inside
        SetWindowPos(h,nullptr,nx-padL,ny-padT,nw+padL+padR,nh+padT+padB,
                     SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);
        return TRUE;
    },0);
}
