// src/services/TileAnim.h  —  Aether shell
// Smooth window movement for komorebi's tiling, done by the shell instead of komorebi.
//
// komorebi's own "animation" resizes every window on every frame. Chromium/Electron apps (Discord, browsers,
// Spotify) cannot repaint that fast and get stuck as blank grey rectangles - measured: a throwaway Chrome window
// went grey within five workspace moves with it on, and never with it off. So komorebi's animation stays off,
// komorebi places windows instantly, and this module makes that placement glide:
//
//   * a WinEvent hook sees a komorebi-managed window jump to a new rect (a retile, a swap, a move, a window
//     opening or closing in the layout);
//   * the window keeps its NEW size - it is resized exactly once, by komorebi - but is put back where its centre
//     was, then TRANSLATED to its new place on the user's motion curve (motion.tiling_*).
//
// Only position changes per frame, and every window stays on screen the whole way, so apps keep drawing.
// Skipped: the user dragging a window, minimise / restore / maximise, windows the workspace slide is moving,
// and anything komorebi does not manage.
#pragma once

struct TileAnimJob { RECT from{}, to{}; ULONGLONG t0=0; int ms=260; int curve=0; float b[4]={0,0,1,1}; POINT last{}; bool overshoot=true; };
static std::mutex                                g_taMtx;
static std::unordered_map<HWND,TileAnimJob>      g_taJobs;     // guarded by g_taMtx
// Last settled rect per window. Written from the window-event thread AND seeded from the komorebi
// reader thread (TaNoteRect), so it needs the lock.
static std::mutex                                g_taPrevMtx;
static std::unordered_map<HWND,RECT>             g_taPrev;     // guarded by g_taPrevMtx
static std::atomic<bool>                         g_taWorker{false};
static HWINEVENTHOOK                             g_taHook=nullptr, g_taHookMs=nullptr;
static std::atomic<bool>                         g_taMoveSize{false};   // the user is dragging / resizing a window

// Answered from the index, never from g_komo: this runs on every window move on the system, and
// taking the komorebi reader's lock here is what used to freeze the shell (see KomoIndexBuild).
static bool TaManaged(HWND h){ return KomoIndexManaged(h); }
// Remember where a window is NOW, so the next time komorebi moves it there is something to animate
// from. Called for every managed window on each komorebi state ingest.
//
// Without this the FIRST tiling operation on any window after the shell starts cannot animate: the
// hook sees the move, finds no previous rect, records one and returns. Alt+T on a freshly started
// shell therefore snapped the first time and glided every time after - which is exactly what it
// looked like from the outside. The workspace slide had the identical bug (see Ws2NoteRect).
static void TaNoteRect(HWND h){
    if(!h || !IsWindow(h) || IsIconic(h) || IsZoomed(h)) return;
    RECT r; if(!GetWindowRect(h,&r) || r.left<=-20000) return;
    std::lock_guard<std::mutex> lk(g_taPrevMtx);
    auto it=g_taPrev.find(h);
    if(it==g_taPrev.end()) g_taPrev[h]=r;      // seed only; never clobber a rect mid-animation
}
static bool TaEnabled();                         // fwd (main.cpp: windows.tile_animation + komorebi live)
// A window that has just appeared in komorebi's layout. Everything AROUND it already glides as the
// layout re-flows - this is the newcomer itself, which otherwise arrives at full size in one frame.
// It grows into its tile from a slightly smaller rect about the same centre, which reads as the
// window settling into place rather than being pasted on top of the screen.
static void TaOpenAnim(HWND h){
    if(!g_openAnim || !TaEnabled() || g_tileStyle!=0) return;      // preview style only
    if(!h || !IsWindow(h) || IsIconic(h) || IsZoomed(h)) return;
    if(g_wsSlideBusy.load()) return;                               // a workspace switch owns the screen
    RECT to; if(!GetWindowRect(h,&to) || to.left<=-20000) return;
    LONG w=to.right-to.left, hh=to.bottom-to.top;
    if(w<80 || hh<60) return;
    const float k=0.86f;                                           // how small it starts
    LONG dw=(LONG)(w*(1.0f-k)*0.5f), dh=(LONG)(hh*(1.0f-k)*0.5f);
    RECT from{ to.left+dw, to.top+dh, to.right-dw, to.bottom-dh };
    { std::lock_guard<std::mutex> lk(g_taPrevMtx); g_taPrev[h]=to; }   // settled position is the target
    T2Add(h,from,to);
}
static float TaDistToSegment(POINT p,POINT a,POINT b){
    float vx=(float)(b.x-a.x), vy=(float)(b.y-a.y), wx=(float)(p.x-a.x), wy=(float)(p.y-a.y);
    float L=vx*vx+vy*vy; float t= L>0? std::clamp((wx*vx+wy*vy)/L,0.0f,1.0f) : 0.0f;
    float dx=wx-vx*t, dy=wy-vy*t; return sqrtf(dx*dx+dy*dy);
}
static void TaWorker(){
    for(;;){
        std::vector<std::pair<HWND,POINT>> moves; bool any=false;
        ULONGLONG now=GetTickCount64();
        { std::lock_guard<std::mutex> lk(g_taMtx);
          for(auto it=g_taJobs.begin(); it!=g_taJobs.end();){
              TileAnimJob& j=it->second;
              if(!IsWindow(it->first) || IsIconic(it->first)){ it=g_taJobs.erase(it); continue; }
              float t=std::clamp((float)(now-j.t0)/(float)std::max(1,j.ms),0.0f,1.0f);
              float e=Cael::evalC(j.curve,t,j.b[0],j.b[1],j.b[2],j.b[3]);
              if(!j.overshoot) e=std::clamp(e,0.0f,1.0f);
              POINT p{ j.from.left+(LONG)std::lround((j.to.left-j.from.left)*e), j.from.top+(LONG)std::lround((j.to.top-j.from.top)*e) };
              if(t>=1.0f) p=POINT{j.to.left,j.to.top};
              if(p.x!=j.last.x||p.y!=j.last.y){ moves.push_back({it->first,p}); j.last=p; }
              if(t>=1.0f){ it=g_taJobs.erase(it); continue; }
              any=true; ++it;
          } }
        for(auto& m:moves)
            SetWindowPos(m.first,nullptr,m.second.x,m.second.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_ASYNCWINDOWPOS);
        if(!any){
            std::lock_guard<std::mutex> lk(g_taMtx);
            if(g_taJobs.empty()){ g_taWorker.store(false); return; }
        }
        int fps=std::clamp(g_wsSlideFps.load(),30,240);
        Sleep((DWORD)std::max(1,1000/fps));
    }
}
static void TaStart(HWND h,const RECT& from,const RECT& to);
static void CALLBACK TaWinEvent(HWINEVENTHOOK,DWORD ev,HWND h,LONG idObject,LONG idChild,DWORD,DWORD){
    if(ev==EVENT_SYSTEM_MOVESIZESTART){ g_taMoveSize=true; return; }
    if(ev==EVENT_SYSTEM_MOVESIZEEND){ g_taMoveSize=false;
        if(h){ RECT r; if(GetWindowRect(h,&r)){ std::lock_guard<std::mutex> lk(g_taPrevMtx); g_taPrev[h]=r; } } return; }
    if(idObject!=OBJID_WINDOW || idChild!=CHILDID_SELF || !h) return;
    // cheapest tests first: this callback runs for every window move anywhere on the system
    if(!TaEnabled()) return;
    if(!TaManaged(h)){
        // Only interesting when we HAD a rect for it: that is the case where a remembered position
        // is being thrown away, and the next real move then has nothing to animate from.
        size_t gone; { std::lock_guard<std::mutex> lk(g_taPrevMtx); gone=g_taPrev.erase(h); }
        if(gone) WsTrace("ta DROP hwnd=%p (komorebi does not list it)",(void*)h);
        return; }
    if(GetAncestor(h,GA_ROOT)!=h) return;
    RECT cur; if(!GetWindowRect(h,&cur)) return;
    bool usable = !IsIconic(h) && !IsZoomed(h) && cur.left>-20000;
    if(!usable){ std::lock_guard<std::mutex> lk(g_taPrevMtx); g_taPrev.erase(h); return; }
    // the user is dragging, or the workspace slide owns the windows right now: just keep up
    if(g_taMoveSize.load() || (GetAsyncKeyState(VK_LBUTTON)&0x8000) || g_wsSlideBusy.load() || GetTickCount64()-g_wsSlideEndAt.load()<700){
        { std::lock_guard<std::mutex> lk(g_taMtx); g_taJobs.erase(h); }
        { std::lock_guard<std::mutex> lk(g_taPrevMtx); g_taPrev[h]=cur; } return; }
    // an event from one of OUR moves (or komorebi re-asserting the same target)? ignore it
    { std::lock_guard<std::mutex> lk(g_taMtx);
      auto it=g_taJobs.find(h);
      if(it!=g_taJobs.end()){
          TileAnimJob& j=it->second;
          bool sameSize = (cur.right-cur.left)==(j.to.right-j.to.left) && (cur.bottom-cur.top)==(j.to.bottom-j.to.top);
          POINT c{cur.left,cur.top};
          if(sameSize && TaDistToSegment(c,POINT{j.from.left,j.from.top},POINT{j.to.left,j.to.top})<24.0f) return;
          // komorebi re-targeted mid-flight: carry on from where the window visibly is
          RECT from=j.from; OffsetRect(&from,j.last.x-j.from.left,j.last.y-j.from.top);
          from.right=from.left+(j.to.right-j.to.left); from.bottom=from.top+(j.to.bottom-j.to.top);
          g_taJobs.erase(it);
          // fall through to a fresh start below, from the visible spot
          std::lock_guard<std::mutex> lk2(g_taPrevMtx); g_taPrev[h]=from;
      } }
    RECT prev;
    { std::lock_guard<std::mutex> lk(g_taPrevMtx);
      auto pit=g_taPrev.find(h);
      if(pit==g_taPrev.end()){ g_taPrev[h]=cur;                // first sight: nothing to animate from
          WsTrace("ta FIRST-SIGHT hwnd=%p -> no animation this time",(void*)h); return; }
      prev=pit->second;
      g_taPrev[h]=cur; }
    // NEVER animate a window between monitors. Either style has to treat the old rect as real, and on a laptop
    // the old rect is routinely on a screen with a different scale factor: Windows sends the app WM_DPICHANGED
    // with a new suggested size, the app resizes itself mid-flight, and it lands somewhere neither we nor
    // komorebi asked for. (The old guard was a flat 6000 px, which two side-by-side screens never reach.)
    if(MonitorFromRect(&prev,MONITOR_DEFAULTTONEAREST) != MonitorFromRect(&cur,MONITOR_DEFAULTTONEAREST)) return;

    // ---- "preview": animate the real rectangle, old -> new, position and size together ----
    if(g_tileStyle==0){
        // Every edge counts. The translate-only path below can ONLY move a window, so it measures the move and
        // bails when there isn't one - which silently dropped every pure resize, and a pure resize is what most
        // of a tiling re-flow is: close a window and its neighbours keep their corner and just get wider.
        LONG d = std::abs(prev.left-cur.left) + std::abs(prev.top-cur.top)
               + std::abs(prev.right-cur.right) + std::abs(prev.bottom-cur.bottom);
        if(d < 8) return;                                      // nothing worth animating
        if(std::abs(prev.left-cur.left)>6000 || std::abs(prev.top-cur.top)>6000) return;   // back from being parked
        T2Add(h,prev,cur);
        return;
    }

    // ---- "windows": keep the NEW size, centred where the window was, then translate the real window ----
    int w=cur.right-cur.left, hh=cur.bottom-cur.top;
    int pcx=(prev.left+prev.right)/2, pcy=(prev.top+prev.bottom)/2;
    RECT from{ pcx-w/2, pcy-hh/2, pcx-w/2+w, pcy-hh/2+hh };
    int dx=cur.left-from.left, dy=cur.top-from.top;
    if(std::abs(dx)<6 && std::abs(dy)<6) return;             // nothing worth animating
    if(std::abs(dx)>6000 || std::abs(dy)>6000) return;        // came back from being parked / another desktop
    TaStart(h,from,cur);
}
// The screens were rearranged: every remembered rect describes a layout that is gone, and gliding from one of
// those is how a window ends up sailing across a monitor it was never on. Forget all of it and start again from
// wherever komorebi puts things next. Runs on the event thread (g_taPrev belongs to it).
static void TaForget(){
    { std::lock_guard<std::mutex> lk(g_taPrevMtx); g_taPrev.clear(); }
    std::lock_guard<std::mutex> lk(g_taMtx); g_taJobs.clear();
}
static void TaStart(HWND h,const RECT& from,const RECT& to){
    MotionSpec m=MotionResolve(MP_TILE,true);
    if(m.ms<=8) return;
    TileAnimJob j; j.from=from; j.to=to; j.t0=GetTickCount64(); j.ms=m.ms; j.curve=m.curve; for(int i=0;i<4;i++) j.b[i]=m.b[i];
    j.overshoot=g_motionOvershoot; j.last=POINT{from.left,from.top};
    // register first, then jump back SYNCHRONOUSLY: queued, komorebi's placement stayed on screen for a frame and the
    // window visibly flashed at its destination before gliding there (a hung app gets the queued form)
    { std::lock_guard<std::mutex> lk(g_taMtx); g_taJobs[h]=j; }
    SetWindowPos(h,nullptr,from.left,from.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER|(IsHungAppWindow(h)? SWP_ASYNCWINDOWPOS : 0));
    if(!g_taWorker.exchange(true)) std::thread(TaWorker).detach();
}
static HWINEVENTHOOK g_wsTraceHooks[2]={};
static void CALLBACK WsTraceProc(HWINEVENTHOOK,DWORD ev,HWND h,LONG idObject,LONG idChild,DWORD,DWORD){
    if(idObject!=OBJID_WINDOW || idChild!=CHILDID_SELF || !h || !TaManaged(h)) return;
    RECT r{}; GetWindowRect(h,&r);
    const char* n = ev==EVENT_SYSTEM_MINIMIZESTART? "MINSTART" : ev==EVENT_SYSTEM_MINIMIZEEND? "MINEND" : ev==EVENT_OBJECT_LOCATIONCHANGE? "LOC" : ev==EVENT_OBJECT_SHOW? "SHOW" : ev==EVENT_OBJECT_HIDE? "HIDE" : "?";
    WsTrace("%-8s hwnd=%p rect=%ld,%ld %ldx%ld iconic=%d",n,(void*)h,r.left,r.top,r.right-r.left,r.bottom-r.top,(int)IsIconic(h));
}
static void WsTraceEnable(bool on){
    g_wsTrace=on;
    for(auto& hk:g_wsTraceHooks) if(hk){ UnhookWinEvent(hk); hk=nullptr; }
    if(on){ g_wsTraceHooks[0]=SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART,EVENT_SYSTEM_MINIMIZEEND,nullptr,WsTraceProc,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
            g_wsTraceHooks[1]=SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,WsTraceProc,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS); }
}
// Both of these run on the shell's window-event thread (WorkspaceSlide2.h), never on the render
// thread: an out-of-context WinEvent hook is delivered to the message queue of the thread that
// installed it, and UnhookWinEvent has to be called from that same thread. Installing them on the
// render thread meant every window move in the system queued work ahead of the next frame.
static void TaInstall(){
    if(g_taHook) return;
    g_taHook  =SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,TaWinEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    g_taHookMs=SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART,EVENT_SYSTEM_MOVESIZEEND,nullptr,TaWinEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
}
static void TaUninstall(){
    if(g_taHook){ UnhookWinEvent(g_taHook); g_taHook=nullptr; }
    if(g_taHookMs){ UnhookWinEvent(g_taHookMs); g_taHookMs=nullptr; }
    // anything mid-flight goes straight to where komorebi put it
    std::lock_guard<std::mutex> lk(g_taMtx);
    for(auto& kv:g_taJobs) if(IsWindow(kv.first)) SetWindowPos(kv.first,nullptr,kv.second.to.left,kv.second.to.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);
    g_taJobs.clear();
}
