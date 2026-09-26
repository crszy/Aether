// src/services/TileAnim2.h  —  Aether shell
// Aether's own animation for komorebi's tiling, done on DWM thumbnails.
//
// WHY THIS EXISTS ALONGSIDE TileAnim.h
//
// komorebi's own animation resizes every window on every frame, and Chromium/Electron apps (Discord, Spotify,
// browsers, VS Code) cannot repaint that fast: they get stuck as blank grey rectangles. So komorebi's animation
// stays off - and with it off, every tiling change is a hard snap.
//
// TileAnim.h was the first answer: let komorebi place the window, then put it back where it was and TRANSLATE it
// to its new spot. That is safe - a window is resized exactly once, by komorebi - but it can only animate
// POSITION. Almost every tiling operation also changes SIZE (toggle a window's layout, move one left or right,
// open or close one, and the whole column re-flows), and those sizes still snapped. Watching it, the result reads
// as "there is no animation": the layout jumps into its new shape and then one window slides a little.
//
// This version animates the real thing - position and size together - without touching a single window. It works
// exactly like the workspace slide (WorkspaceSlide2.h):
//
//   * komorebi retiles. The windows are already at their final rects, instantly, as komorebi intends;
//   * a click-through layer covers that monitor, showing DWM thumbnails - live, GPU-composited previews - of the
//     desktop behind and of every window on the screen;
//   * the previews of the windows that MOVED are drawn at their old rects and animated to their new ones, all four
//     edges at once, on the user's motion curve (motion.tiling_*). DWM scales a thumbnail for free, so a window
//     growing from a third of the screen to half of it is a real, smooth resize;
//   * the layer lifts and the real windows are already exactly where the previews stopped.
//
// Nothing is ever resized per frame, so nothing greys out; nothing is ever moved, so no window can be left
// stranded if the animation is interrupted. If Aether dies mid-animation the layer dies with it and the windows
// are already where komorebi put them.
//
// windows.tile_style picks between this ("preview") and the older translate-only one ("windows").
#pragma once

struct T2Win {
    HWND   h;
    RECT   from, to;          // where the window was, and where komorebi has just put it
    RECT   now;               // the rect the preview is at (so a re-aim can carry on from it)
    bool   placed=false;
};
struct T2Job {
    bool   active=false;
    RECT   mon{};
    double t0=0;
    int    ms=260;
    int    curve=0;
    float  b[4]={0,0,1,1};
    bool   overshoot=true;
    std::vector<T2Win> wins;
    bool   dirty=false;       // T2Add touched this since the worker last looked
    bool   cancel=false;
};
static std::mutex        g_t2Mtx;
static T2Job             g_t2[WS2_MAXMON];
static HWND              g_t2Wnd[WS2_MAXMON]={};
static std::atomic<bool> g_t2Worker[WS2_MAXMON];
static std::atomic<int>  g_t2Running{0};
// g_t2Busy / g_t2EndAt live in WorkspaceSlide.h: the frozen-window check reads them thousands of lines above
// this include, and it must never judge a window through one of these layers.

static void T2Run(int idx);

// Made on the window-event thread with the slide layers (see Ws2MakeLayers). Same class, same properties - a
// click-through, non-activating, topmost popup with a black backdrop.
static void T2MakeLayers(HINSTANCE hi){
    for(int i=0;i<WS2_MAXMON;i++){
        if(g_t2Wnd[i]) continue;
        g_t2Wnd[i]=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT,
                                   L"AetherSlideLayer",L"",WS_POPUP,0,0,1,1,nullptr,nullptr,hi,nullptr);
        // The overview photographs the screen; an animation layer covering it must never end up in
        // one of those pictures, or a workspace is remembered as a frame of its own animation.
        if(g_t2Wnd[i]) SetWindowDisplayAffinity(g_t2Wnd[i],WDA_EXCLUDEFROMCAPTURE);
    }
}
static bool T2IsLayer(HWND h){ for(int i=0;i<WS2_MAXMON;i++) if(h==g_t2Wnd[i]) return true; return false; }

static RECT T2Lerp(const RECT& a,const RECT& b,float e){
    return RECT{ a.left  +(LONG)lroundf((b.left  -a.left  )*e),
                 a.top   +(LONG)lroundf((b.top   -a.top   )*e),
                 a.right +(LONG)lroundf((b.right -a.right )*e),
                 a.bottom+(LONG)lroundf((b.bottom-a.bottom)*e) };
}

// komorebi moved a window from `from` to `to`. Called from the tiling hook (TileAnim.h) on the event thread.
//
// komorebi retiles several windows in one go, a few milliseconds apart, and each arrives as its own event. They
// all join the SAME job and share its clock, so a re-flow moves as one layout rather than as a handful of
// separately-timed slides.
static void T2Add(HWND h,const RECT& from,const RECT& to){
    RECT rmon=Ws2ResolveMon(to);
    if(rmon.right-rmon.left<64 || rmon.bottom-rmon.top<64) return;
    MotionSpec m=MotionResolve(MP_TILE,true);
    if(m.ms<=8) return;                                  // motion.tiling_ms says "no animation"
    double now=WsNowMs();
    std::lock_guard<std::mutex> lk(g_t2Mtx);
    const int idx=MonSlotFor(rmon);
    T2Job& j=g_t2[idx];
    // A job whose clock has already run out is finished - its windows are home. Joining it would inherit an
    // expired t0 and the new window would jump straight to its target with no animation at all (seen in the
    // trace as a job that starts and is "done" 16 ms later). That is a fresh leg, not a late join.
    if(!j.active || j.cancel || (now-j.t0) >= (double)j.ms){
        j=T2Job(); j.active=true; j.mon=rmon; j.t0=now; j.ms=m.ms; j.curve=m.curve;
        for(int i=0;i<4;i++) j.b[i]=m.b[i];
        j.overshoot=g_motionOvershoot;
    }
    for(auto& w:j.wins){
        if(w.h!=h) continue;
        // komorebi re-targeted a window that is still in flight: carry on from where the preview visibly is,
        // rather than snapping back to the rect it started from.
        w.from=w.placed? w.now : w.from; w.to=to; j.dirty=true;
        WsTrace("t2 retarget hwnd=%p  %ld,%ld %ldx%ld -> %ld,%ld %ldx%ld",(void*)h,
                w.from.left,w.from.top,w.from.right-w.from.left,w.from.bottom-w.from.top,
                to.left,to.top,to.right-to.left,to.bottom-to.top);
        return;
    }
    WsTrace("t2 add hwnd=%p  %ld,%ld %ldx%ld -> %ld,%ld %ldx%ld",(void*)h,
            from.left,from.top,from.right-from.left,from.bottom-from.top,
            to.left,to.top,to.right-to.left,to.bottom-to.top);
    T2Win w; w.h=h; w.from=from; w.to=to; w.now=from;
    j.wins.push_back(w);
    j.dirty=true;
    if(!g_t2Worker[idx].exchange(true)) std::thread(T2Run,idx).detach();
}

static void T2CancelAll(){
    std::lock_guard<std::mutex> lk(g_t2Mtx);
    for(int i=0;i<WS2_MAXMON;i++) if(g_t2[i].active) g_t2[i].cancel=true;
}

static void T2Run(int idx){
    struct TimerRes{ TimerRes(){ timeBeginPeriod(1); } ~TimerRes(){ timeEndPeriod(1); } } tr;
    HWND layer=g_t2Wnd[idx];
    std::unordered_map<HWND,HTHUMBNAIL> thumbs;
    std::vector<HTHUMBNAIL> bg, staticThumbs;

    struct Guard {
        int idx; HWND layer;
        std::unordered_map<HWND,HTHUMBNAIL>* thumbs;
        std::vector<HTHUMBNAIL>*bg; std::vector<HTHUMBNAIL>*st;
        bool shown=false;
        ~Guard(){
            if(shown && layer) ShowWindow(layer,SW_HIDE);
            for(auto& kv:*thumbs) if(kv.second) DwmUnregisterThumbnail(kv.second);
            for(auto t:*st) DwmUnregisterThumbnail(t);
            for(auto t:*bg) DwmUnregisterThumbnail(t);
            bool restart=false;
            { std::lock_guard<std::mutex> lk(g_t2Mtx);
              T2Job& j=g_t2[idx];
              restart = j.active && !j.cancel && j.dirty && !j.wins.empty();
              if(!restart) j=T2Job();
              g_t2Worker[idx].store(restart); }
            if(g_t2Running.fetch_sub(1)-1<=0){ g_t2Running.store(0); g_t2Busy.store(false); }
            g_t2EndAt.store(GetTickCount64());
            if(restart) std::thread(T2Run,idx).detach();
        }
    } guard{idx,layer,&thumbs,&bg,&staticThumbs};
    g_t2Running.fetch_add(1); g_t2Busy.store(true);

    if(!layer || !g_ws2Desk) return;
    // the workspace slide owns the screen; it already suppresses tiling, but never fight it for the layer
    if(g_wsSlideBusy.load()) return;

    RECT mon;
    { std::lock_guard<std::mutex> lk(g_t2Mtx);
      if(!g_t2[idx].active || g_t2[idx].cancel) return;
      mon=g_t2[idx].mon; }
    const int MW=mon.right-mon.left, MH=mon.bottom-mon.top;
    if(MW<64||MH<64) return;

    SetWindowPos(layer,HWND_TOPMOST,mon.left,mon.top,MW,MH,SWP_NOACTIVATE);
    if(g_ws2Bar) SetWindowPos(layer,g_ws2Bar,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

    auto thumbOf=[&](HWND src,RECT srcRectScreen,RECT dst)->HTHUMBNAIL{
        HTHUMBNAIL t=nullptr; if(FAILED(DwmRegisterThumbnail(layer,src,&t))) return nullptr;
        RECT wr; GetWindowRect(src,&wr);
        DWM_THUMBNAIL_PROPERTIES p{}; p.dwFlags=DWM_TNP_RECTDESTINATION|DWM_TNP_RECTSOURCE|DWM_TNP_VISIBLE|DWM_TNP_OPACITY|DWM_TNP_SOURCECLIENTAREAONLY;
        p.rcDestination=dst; p.rcSource=RECT{srcRectScreen.left-wr.left,srcRectScreen.top-wr.top,srcRectScreen.right-wr.left,srcRectScreen.bottom-wr.top};
        p.fVisible=TRUE; p.opacity=255; p.fSourceClientAreaOnly=FALSE;
        DwmUpdateThumbnailProperties(t,&p); return t; };

    { HWND wp=Ws2WallpaperWindow(); if(wp){ HTHUMBNAIL t=thumbOf(wp,mon,RECT{0,0,MW,MH}); if(t) bg.push_back(t); } }
    { HTHUMBNAIL t=thumbOf(g_ws2Desk,mon,RECT{0,0,MW,MH}); if(t) bg.push_back(t); }
    if(bg.empty()){
        // no desktop to put behind the previews: covering the screen would only black it out
        g_ws2ThumbFail.fetch_add(1);
        WsTrace("t2[%d]: no background thumbnail - no tiling animation",idx);
        return;
    }

    // Every other window on this monitor is drawn where it is, so the animation happens inside a screen that
    // otherwise looks untouched. Registered back-to-front so the stacking order survives.
    std::vector<HWND> statics;
    { struct C{ RECT mon; std::vector<HWND>* v; int idx; } c{mon,&statics,idx};
      EnumWindows([](HWND h,LPARAM lp)->BOOL{ C* c=(C*)lp;
          if(Ws2IsLayer(h) || T2IsLayer(h) || !WsAnimatable(h)) return TRUE;
          { std::lock_guard<std::mutex> lk(g_t2Mtx);
            for(auto& w:g_t2[c->idx].wins) if(w.h==h) return TRUE; }      // this one is being animated
          if(!WsOnMonitor(h,c->mon)) return TRUE;
          c->v->push_back(h); return TRUE; },(LPARAM)&c); }
    for(auto it=statics.rbegin(); it!=statics.rend(); ++it){
        RECT r; if(!GetWindowRect(*it,&r)) continue;
        HTHUMBNAIL t=thumbOf(*it,r,RECT{r.left-mon.left,r.top-mon.top,r.right-mon.left,r.bottom-mon.top});
        if(t) staticThumbs.push_back(t);
    }

    int badFlush=0;
    const double runStart=WsNowMs();
    { std::lock_guard<std::mutex> lk(g_t2Mtx);
      WsTrace("t2[%d] run: animating=%d statics=%d ms=%d",idx,(int)g_t2[idx].wins.size(),(int)statics.size(),g_t2[idx].ms); }

    for(;;){
        // a window that joined since the last frame needs its thumbnail before it can be placed - registering
        // after the placement pass is what leaves a layer on screen with nothing drawn on it
        {
            std::vector<HWND> fresh;
            { std::lock_guard<std::mutex> lk(g_t2Mtx);
              for(auto& w:g_t2[idx].wins) if(!thumbs.count(w.h)) fresh.push_back(w.h); }
            for(HWND h:fresh){
                HTHUMBNAIL t=nullptr; HRESULT hr=DwmRegisterThumbnail(layer,h,&t);
                thumbs[h]=SUCCEEDED(hr)? t : nullptr;
                // A silent failure here is indistinguishable from "that window did not animate":
                // everything else glides and the one window snaps to its new place.
                WsTrace("t2[%d] thumb hwnd=%p hr=0x%08lX iconic=%d vis=%d",idx,(void*)h,
                        (unsigned long)hr,(int)IsIconic(h),(int)IsWindowVisible(h));
            }
        }

        struct Upd { HTHUMBNAIL t; RECT d; };
        std::vector<Upd> upd;
        bool done=false, cancel=false;
        double now=WsNowMs();
        {
            std::lock_guard<std::mutex> lk(g_t2Mtx);
            T2Job& j=g_t2[idx];
            if(!j.active || j.cancel || j.wins.empty()) cancel=true;
            else {
                j.dirty=false;
                float t=(float)std::clamp((now-j.t0)/std::max(1,j.ms),0.0,1.0);
                float e=Cael::evalC(j.curve,t,j.b[0],j.b[1],j.b[2],j.b[3]);
                if(!j.overshoot) e=std::clamp(e,0.0f,1.0f);
                if(t>=1.0f){ e=1.0f; done=true; }
                for(auto& w:j.wins){
                    w.now=T2Lerp(w.from,w.to,e); w.placed=true;
                    auto it=thumbs.find(w.h);
                    if(it==thumbs.end() || !it->second) continue;
                    upd.push_back({it->second,RECT{w.now.left-mon.left,w.now.top-mon.top,w.now.right-mon.left,w.now.bottom-mon.top}});
                }
            }
        }
        if(cancel) return;

        for(auto& u:upd){
            DWM_THUMBNAIL_PROPERTIES p{}; p.dwFlags=DWM_TNP_RECTDESTINATION|DWM_TNP_VISIBLE|DWM_TNP_SOURCECLIENTAREAONLY;
            p.rcDestination=u.d; p.fVisible=TRUE; p.fSourceClientAreaOnly=FALSE;
            DwmUpdateThumbnailProperties(u.t,&p);
        }
        if(!guard.shown){ ShowWindow(layer,SW_SHOWNOACTIVATE); guard.shown=true; }

        if(done) break;
        if(now-runStart > 3000.0){ WsTrace("t2[%d] deadline",idx); break; }
        if(g_wsSlideBusy.load()) return;              // a workspace switch started: that layer wins

        // same pacing as the workspace slide: the compositor's own frame, with a way out if it stops keeping time
        if(badFlush<3){
            double a=WsNowMs(); bool ok=PaceFlush(100); double dt=WsNowMs()-a;
            if(!ok || dt>40.0) badFlush++;
            else if(badFlush>0) badFlush--;
        } else Sleep(6);
    }
    PaceFlush(100);                                    // the last frame is composited before the layer goes
    WsTrace("t2[%d] done",idx);
}
