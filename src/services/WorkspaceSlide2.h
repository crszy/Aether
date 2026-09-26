// src/services/WorkspaceSlide2.h  —  Aether shell
// The workspace slide, done with DWM thumbnails instead of moving real windows.
//
// Why the first version (WorkspaceSlide.h, still selectable as workspaces.slide_style = "windows") was choppy -
// measured with a 60 fps capture and a trace of every window event:
//   * komorebi restores the arriving windows at their final place ~30 ms BEFORE it tells us the workspace changed,
//     and the slide then waited another 80 ms "to settle" - so the windows sat there for ~110 ms, then jumped
//     536 px sideways and slid back: the pop at the start;
//   * every frame moved each window with a synchronous SetWindowPos to ANOTHER process. A window busy restoring or
//     minimizing makes that call wait: frames landed 8-40 ms apart instead of 7;
//   * frames were paced with Sleep(6), which really sleeps ~15.6 ms without a raised timer resolution, and progress
//     counted frames rather than time - so the 320 ms slide took 1.1 s and stuttered on the way;
//   * on a switch to an empty workspace it "slid in" the windows that were leaving (they were still on screen when
//     it captured), and komorebi's "Minimize" hiding meant the leaving half could never move at all.
//
// This version never moves a real window per frame. When a switch starts, a click-through layer covers that monitor
// and shows DWM thumbnails - live, GPU-composited previews - of the desktop behind, of the windows that are leaving
// and of the windows arriving. Only the thumbnails move, paced by DwmFlush (the compositor's own frame), on the
// user's slide duration. Underneath, komorebi minimizes and restores the real windows however slowly it likes; when
// the slide ends the layer goes away and the real windows are already exactly where the thumbnails stopped.
//
// Timing is taken from the earliest signal: a managed window leaving the minimized state while it belongs to a
// workspace that is NOT the focused one is the first sign of a switch (it arrives before komorebi's event), and
// komorebi's event covers the leaving side (it arrives before komorebi minimizes them).
//
// ---------------------------------------------------------------------------------------------------------------
// 2026-09-19 rework. What was wrong, and what each fix is for:
//
//  1. ONE job and ONE layer for the whole machine. Two monitors switching (komorebi fires both in the same event,
//     and a laptop plus an external screen does it constantly) meant the second switch stole the layer from the
//     first: it was moved to the other monitor mid-flight, the window list was thrown away, and the previews of
//     the first monitor were left at whatever offset they had reached. That is the "it doesn't know where to go
//     and the app just spazzes out". Now every komorebi monitor has its OWN job, its own layer window and its own
//     worker; they never touch each other.
//
//  2. A switch arriving mid-slide RESTARTED the slide: it cleared the window list, unregistered every thumbnail,
//     dropped the layer's contents and began again from zero. Flicking through workspaces therefore produced one
//     animation and a series of black flashes and jumps. Now a new switch RE-AIMS the strip instead: each preview
//     keeps the offset it has reached (off0 = where it is now), the windows that were arriving become the ones
//     leaving, the new workspace comes in from the far side, and the layer and its thumbnails stay up the whole
//     time. Nothing is ever thrown away mid-flight.
//
//  3. Rapid switches now SPEED THE ANIMATION UP rather than being skipped: the leg's duration shrinks with the gap
//     between switches, down to workspaces.slide_min_ms. Five quick switches give five visible slides, each faster
//     than the last, instead of one slide and four teleports.
//
//  4. Pacing had no escape hatch. DwmFlush blocks until the compositor's next frame - and on a laptop (hybrid
//     graphics, panel self-refresh, power saving) it can block for hundreds of milliseconds, which stall.txt shows
//     happening on this machine too. A slide that takes one 300 ms "frame" is a slide that visibly does not
//     finish. Now flushes are timed, and after three bad ones the worker paces on the timer instead, with a hard
//     wall-clock deadline behind that.
//
//  5. Every early return used to leak: the layer stayed up or the thumbnails stayed registered, and komorebi's own
//     animations - which are disabled for the duration of a slide - were never re-enabled if the slide failed to
//     start. One guard now owns all of it.
//
//  6. komorebi's monitor rect was trusted blindly. It is a cached copy, and on a laptop it is wrong exactly when it
//     matters: after docking, undocking, a resolution change or a scale change. Sliding a monitor-wide strip using
//     a stale rect is precisely the "the animation happens somewhere else and the app ends up in the wrong place".
//     Every rect is now resolved against the live monitor it actually lands on.
//
//  7. If DWM would not give us a thumbnail of the desktop the slide simply did nothing - forever, on every switch.
//     Some laptop drivers will not hand out a thumbnail of a DirectComposition window. After two failures the
//     slide falls back to the real-window style by itself rather than going quiet.
#pragma once
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")    // timeBeginPeriod
// g_wsSlideStyle / WSSLIDESTYLE_NAMES live in WorkspaceSlide.h (settings are declared ahead of the SETTINGS table)

#define WS2_MAXMON 8                 // komorebi monitors we can slide independently

// One preview in a slide. `off` is the distance along the slide axis, in pixels, from where the real
// window is: off0 at the start of the current leg, off1 at its end, offNow where the worker last put it.
struct Ws2Win { HWND h; RECT r; float off0=0, off1=0, offNow=0; bool incoming=false; };
struct Ws2Job {
    bool   active=false;
    int    kMon=-1;
    RECT   mon{};
    int    dir=1;
    double t0=0;              // start of the current leg
    int    ms=320;            // length of the current leg
    std::vector<Ws2Win> wins;
    bool   relegged=false;    // a new switch re-aimed the strip; the worker syncs its thumbnails
    bool   dirty=false;       // Ws2Add touched this job since the worker last looked at it
    bool   cancel=false;      // tear down now (monitors changed, komorebi went away)
    bool   speculative=false; // started by a window restoring, not yet confirmed by a komorebi event
};
static std::mutex        g_ws2Mtx;                  // guards all of g_ws2[]
static Ws2Job            g_ws2[WS2_MAXMON];
static HWND              g_ws2Wnd[WS2_MAXMON]={};   // one layer per monitor, made on the event thread
static std::atomic<bool> g_ws2Worker[WS2_MAXMON];
static std::atomic<int>  g_ws2Running{0};           // how many slides are in flight -> g_wsSlideBusy
static double            g_ws2LastFire[WS2_MAXMON]={};
// g_ws2ThumbFail / g_wsSlideMinMs live in WorkspaceSlide.h - the settings table is compiled thousands of lines
// before this header is included, and a setting has to be declared before the code that persists it.

static HWND g_ws2Desk=nullptr, g_ws2Bar=nullptr, g_ws2Ctl=nullptr;
static HWINEVENTHOOK g_ws2Hook=nullptr, g_ws2HookLoc=nullptr;
static std::mutex g_ws2RectMtx;
static std::unordered_map<HWND,RECT> g_ws2LastRect;      // last on-screen rect of each managed window

// messages handled on the window-event thread (it owns the layers and every WinEvent hook)
#define WS2_MSG_LAYERS   (WM_APP+41)
#define WS2_MSG_TAHOOK   (WM_APP+42)
#define WS2_MSG_TAUNHOOK (WM_APP+43)
#define WS2_MSG_TACLEAR  (WM_APP+44)

static LRESULT CALLBACK Ws2WndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h,m,w,l);
}
static void Ws2MakeLayers(HINSTANCE hi){
    for(int i=0;i<WS2_MAXMON;i++){
        if(g_ws2Wnd[i]) continue;
        // a normal (redirected) window: DWM thumbnails do not show in a WS_EX_NOREDIRECTIONBITMAP window
        g_ws2Wnd[i]=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT,
                                    L"AetherSlideLayer",L"",WS_POPUP,0,0,1,1,nullptr,nullptr,hi,nullptr);
        // The overview photographs the screen; an animation layer covering it must never end up in
        // one of those pictures, or a workspace is remembered as a frame of its own animation.
        if(g_ws2Wnd[i]) SetWindowDisplayAffinity(g_ws2Wnd[i],WDA_EXCLUDEFROMCAPTURE);
    }
}
static LRESULT CALLBACK Ws2CtlProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WS2_MSG_LAYERS){ HINSTANCE hi=(HINSTANCE)GetWindowLongPtrW(h,GWLP_HINSTANCE); Ws2MakeLayers(hi); T2MakeLayers(hi); return 0; }
    if(m==WS2_MSG_TAHOOK){ TaInstall();   return 0; }
    if(m==WS2_MSG_TAUNHOOK){ TaUninstall(); return 0; }
    if(m==WS2_MSG_TACLEAR){ TaForget(); return 0; }
    return DefWindowProcW(h,m,w,l);
}
static void CALLBACK Ws2WinEvent(HWINEVENTHOOK,DWORD ev,HWND h,LONG idObj,LONG idChild,DWORD,DWORD);   // fwd

// The layer windows and every window-event hook live on their OWN thread with its own message loop.
//
// On the render thread they waited behind every drawn frame: a restore event reached us ~90 ms late and moving /
// showing the layer from the slide thread blocked until the render loop pumped - the layer came up ~165 ms after the
// switch. Worse in the other direction: an out-of-context WinEvent hook fires for every window move anywhere on the
// system, so the render thread was doing that work between frames, and one slow call inside a hook (or one lock held
// by the komorebi reader) stalled the whole shell. Everything hook-shaped now lives here.
static void Ws2CreateWindow(HINSTANCE hi,HWND desk,HWND bar){
    g_ws2Desk=desk; g_ws2Bar=bar;
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread([hi,ready]{
        WNDCLASSW wc{}; wc.lpfnWndProc=Ws2WndProc; wc.hInstance=hi; wc.lpszClassName=L"AetherSlideLayer";
        wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc);
        WNDCLASSW wc2{}; wc2.lpfnWndProc=Ws2CtlProc; wc2.hInstance=hi; wc2.lpszClassName=L"AetherSlideCtl";
        RegisterClassW(&wc2);
        g_ws2Ctl=CreateWindowExW(0,L"AetherSlideCtl",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,hi,nullptr);
        Ws2MakeLayers(hi);
        T2MakeLayers(hi);            // the tiling animation's layers are the same kind of window

        g_ws2Hook=SetWinEventHook(EVENT_SYSTEM_MINIMIZEEND,EVENT_SYSTEM_MINIMIZEEND,nullptr,Ws2WinEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        g_ws2HookLoc=SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,Ws2WinEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        SetEvent(ready);
        MSG m; while(GetMessageW(&m,nullptr,0,0)>0){ TranslateMessage(&m); DispatchMessageW(&m); }
    }).detach();
    WaitForSingleObject(ready,3000); CloseHandle(ready);
}
// The tiling glide's hooks belong on the event thread too - an out-of-context WinEvent hook is delivered to the
// thread that installed it, and UnhookWinEvent must come from that same thread. Taking them down is synchronous
// (with a timeout, so a wedged event thread can never hold up shutdown): TaUninstall also puts any window still
// mid-glide at its final place, and a posted message would lose that race with process exit.
static void Ws2TileHooks(bool on){
    if(!g_ws2Ctl){ if(on) TaInstall(); else TaUninstall(); return; }   // no event thread: better than no hooks
    if(on){ PostMessageW(g_ws2Ctl,WS2_MSG_TAHOOK,0,0); return; }
    DWORD_PTR r=0; SendMessageTimeoutW(g_ws2Ctl,WS2_MSG_TAUNHOOK,0,0,SMTO_ABORTIFHUNG,800,&r);
}

static bool Ws2IsLayer(HWND h){ for(int i=0;i<WS2_MAXMON;i++) if(h==g_ws2Wnd[i]) return true; return false; }

// komorebi's monitor / workspace of a window, and that monitor's focused workspace. Index only -
// never g_komoMtx: this is called from a hook.
static bool Ws2Locate(HWND h,int& mon,int& ws,int& focused){ return KomoIndexFind(h,mon,ws,focused); }

// komorebi's monitor rect is a CACHED copy of what Windows told it, and on a laptop it is stale exactly when it
// matters - after docking, undocking, a resolution change, a scale change, or when the external screen komorebi
// remembers is not plugged in any more. Sliding a monitor-wide strip on a rect that is not where that monitor is
// puts the whole animation in the wrong place. So komorebi's rect only ever picks WHICH monitor; the geometry comes
// from Windows.
static RECT Ws2ResolveMon(RECT komo){
    HMONITOR hm=nullptr;
    if(komo.right-komo.left>=64 && komo.bottom-komo.top>=64)
        hm=MonitorFromRect(&komo,MONITOR_DEFAULTTONULL);            // the monitor it overlaps most
    if(!hm){
        POINT c{ (komo.left+komo.right)/2, (komo.top+komo.bottom)/2 };
        hm=MonitorFromPoint(c,MONITOR_DEFAULTTONEAREST);            // that monitor is gone: the nearest live one
    }
    MONITORINFO mi{}; mi.cbSize=sizeof(mi);
    if(hm && GetMonitorInfoW(hm,&mi)) return mi.rcMonitor;
    return komo;
}
static bool Ws2RectOnMon(const RECT& r,const RECT& mon){
    LONG ix1=std::max(r.left,mon.left),  iy1=std::max(r.top,mon.top);
    LONG ix2=std::min(r.right,mon.right),iy2=std::min(r.bottom,mon.bottom);
    if(ix2<=ix1||iy2<=iy1) return false;
    double area=(double)(r.right-r.left)*(r.bottom-r.top);
    if(area<1) return false;
    return (double)(ix2-ix1)*(iy2-iy1)*2.0 >= area;                 // at least half of it is on this monitor
}
// Remember where a window is, so that when komorebi minimizes it for a workspace switch there is still something
// to preview. The hook below does this as windows move, but a window that has not moved since the shell started
// has no entry at all - and the first switch after a restart then had nothing to slide out. komorebi's state
// ingest calls this for everything it manages, so the cache is warm from the first switch.
static void Ws2NoteRect(HWND h){
    if(!h || !IsWindow(h) || IsIconic(h)) return;
    RECT r; if(!GetWindowRect(h,&r) || r.left<=-20000) return;
    std::lock_guard<std::mutex> lk(g_ws2RectMtx); g_ws2LastRect[h]=r;
}
static bool Ws2RectFor(HWND h,RECT& r){
    if(IsWindow(h) && !IsIconic(h) && GetWindowRect(h,&r) && r.left>-20000) return true;
    std::lock_guard<std::mutex> lk(g_ws2RectMtx);
    auto it=g_ws2LastRect.find(h); if(it==g_ws2LastRect.end()) return false;
    r=it->second; return true;
}
static void Ws2Run(int idx);

// Which animation slot belongs to a display.
//
// This used to be komorebi's monitor INDEX, and komorebi reorders its monitor array - so the same physical screen
// could be slot 0 for one switch and slot 1 for the next. Two slots then ran two layers over one monitor while the
// other screen got none. Slots are claimed by HMONITOR instead, which is what "the same screen" actually means.
// Shared by the workspace slide and the tiling animation, so both call the same screen by the same name.
static std::mutex g_monSlotMtx;
static HMONITOR   g_monSlot[WS2_MAXMON]={};
static int MonSlotFor(const RECT& mon){
    HMONITOR hm=MonitorFromRect(&mon,MONITOR_DEFAULTTONEAREST);
    if(!hm) return 0;
    std::lock_guard<std::mutex> lk(g_monSlotMtx);
    for(int i=0;i<WS2_MAXMON;i++) if(g_monSlot[i]==hm) return i;
    for(int i=0;i<WS2_MAXMON;i++) if(!g_monSlot[i]){ g_monSlot[i]=hm; return i; }
    return 0;                                     // more than WS2_MAXMON screens: they share slot 0
}
static void MonSlotsForget(){ std::lock_guard<std::mutex> lk(g_monSlotMtx); for(auto& m:g_monSlot) m=nullptr; }

// How long this leg should take. Switches that come fast get a SHORTER slide, not a skipped one - the whole point
// is that flicking through workspaces stays an animation instead of degenerating into jumps.
static int Ws2LegMs(int idx,double now){
    int base=std::clamp(g_wsSlideMs.load(),100,800);
    int floorMs=std::clamp(g_wsSlideMinMs,60,400);
    double last=g_ws2LastFire[idx];
    g_ws2LastFire[idx]=now;
    int dur=base;
    if(last>0.0){
        double gap=now-last, window=base*1.5;
        if(gap<window) dur=(int)(base*std::clamp(gap/window,0.25,1.0));
    }
    return std::max(floorMs,std::min(dur,base));
}

// Add windows to monitor `idx`'s slide, starting or re-aiming it.
//   newLeg = this is a workspace SWITCH (the strip gets a new target), not a window joining one already running.
static void Ws2Add(int kMon,RECT mon,int dir,const std::vector<std::pair<HWND,bool>>& hs,bool newLeg,bool speculative=false){
    const RECT rmon=Ws2ResolveMon(mon);
    if(rmon.right-rmon.left<64 || rmon.bottom-rmon.top<64) return;
    double now=WsNowMs();
    std::lock_guard<std::mutex> lk(g_ws2Mtx);
    const int idx=MonSlotFor(rmon);
    Ws2Job& j=g_ws2[idx];

    if(!j.active){
        j=Ws2Job(); j.active=true; j.kMon=kMon; j.mon=rmon; j.dir=dir; j.t0=now;
        j.ms=Ws2LegMs(idx,now); j.speculative=speculative;
    } else if(newLeg){
        // A new switch while one is running. Do NOT start over: re-aim the strip from wherever it is.
        if(!speculative) j.speculative=false;
        // ... but first, is it really a new switch? The restore hook sees komorebi un-minimize the arriving
        // workspace ~30 ms BEFORE komorebi's own event for the same switch, and komorebi can repeat an event.
        // Re-aiming for those would restart the leg - and because the two arrive milliseconds apart, the burst
        // compression would shrink it to the floor as though the user had flicked. Within a beat of the leg
        // starting it is the same switch: merge the windows and leave the timing alone.
        const bool sameSwitch = (now-j.t0) < 150.0;
        if(!(sameSwitch && dir==j.dir)){
            j.mon=rmon; j.kMon=kMon;
            j.dir=dir;                       // komorebi's event is the authority on which way the strip travels
            j.t0=now;
            // a direction correction on a leg that just began is still that one switch, so it keeps its duration
            j.ms = sameSwitch ? j.ms : Ws2LegMs(idx,now);
            const float span=(float)(g_wsSlideVert.load()? (j.mon.bottom-j.mon.top) : (j.mon.right-j.mon.left));
            // everything that was on the strip is now leaving, continuing outward in the new direction
            for(auto it=j.wins.begin(); it!=j.wins.end();){
                bool keep=true;
                bool nowIncoming=false;
                for(auto& p:hs) if(p.first==it->h && p.second){ nowIncoming=true; break; }
                if(nowIncoming){ it->off0=it->offNow; it->off1=0; it->incoming=true; }
                else {
                    // already all the way off screen and not part of this switch: let it go
                    if(fabsf(it->offNow)>=span*0.98f) keep=false;
                    else { it->off0=it->offNow; it->off1=-dir*span; it->incoming=false; }
                }
                if(keep) ++it; else it=j.wins.erase(it);
            }
            j.relegged=true;
        }
    } else if(speculative==false){
        j.speculative=false;
    }

    const float span=(float)(g_wsSlideVert.load()? (j.mon.bottom-j.mon.top) : (j.mon.right-j.mon.left));
    for(auto& p:hs){
        bool have=false;
        for(auto& w:j.wins) if(w.h==p.first){ have=true; break; }
        if(have) continue;
        RECT r; if(!Ws2RectFor(p.first,r)) continue;
        // a remembered rect that is not on this monitor is stale (the window was moved, or the screens were
        // rearranged). Previewing it would draw the window somewhere it has never been - skip it and let the
        // real window simply appear.
        if(!Ws2RectOnMon(r,j.mon)) continue;
        Ws2Win w; w.h=p.first; w.r=r; w.incoming=p.second;
        // A window that joins late (komorebi restored it after the leg started) takes its workspace-mates' start
        // and end offsets, so the leg's shared t0/ms put it at exactly their phase - no catching up, no drifting.
        // Their off0 is only the screen edge on a leg that began there; after a re-aim it is wherever they were.
        const Ws2Win* mate=nullptr;
        for(auto& x:j.wins) if(x.incoming==p.second){ mate=&x; break; }
        if(mate){ w.off0=mate->off0; w.off1=mate->off1; }
        else {
            w.off0 = p.second? (float)(j.dir*span) : 0.0f;
            w.off1 = p.second? 0.0f : (float)(-j.dir*span);
        }
        w.offNow = w.off0;
        j.wins.push_back(w);
    }
    // The worker clears this every frame. It is the ONLY signal that says "there is new work here", and the
    // worker's teardown uses it to decide whether a switch slipped in behind its back - a timing heuristic could
    // not tell that apart from a slide that had simply run its course, and got the answer catastrophically wrong:
    // a cancelled job looked "still within its duration" and was restarted, forever, four times a second.
    j.dirty=true;
    if(!g_ws2Worker[idx].exchange(true)) std::thread(Ws2Run,idx).detach();
}

// komorebi's event: the workspace on monitor kMon changed from oldWs to newWs
static void Ws2Fire(int kMon,int oldWs,int newWs,RECT mon){
    if(kMon<0 || kMon>=WS2_MAXMON) return;
    std::vector<std::pair<HWND,bool>> hs;
    { std::lock_guard<std::mutex> lk(g_komoMtx);
      if(kMon>=(int)g_komo.size()) return;
      auto& km=g_komo[kMon];
      if(oldWs>=0 && oldWs<(int)km.ws.size()) for(HWND h:km.ws[oldWs].hwnds) hs.push_back({h,false});
      if(newWs>=0 && newWs<(int)km.ws.size()) for(HWND h:km.ws[newWs].hwnds) hs.push_back({h,true}); }
    // a leaving window must still be on screen (or remembered); an arriving one that is still minimized joins
    // when it restores (the hook below)
    std::vector<std::pair<HWND,bool>> use;
    for(auto& p:hs){ if(p.second && IsIconic(p.first)) continue; use.push_back(p); }
    WsTrace("ws2 FIRE mon=%d %d->%d windows=%d",kMon,oldWs,newWs,(int)use.size());
    Ws2Add(kMon,mon,newWs>oldWs? 1 : -1,use,true,false);
}
static void CALLBACK Ws2WinEvent(HWINEVENTHOOK,DWORD ev,HWND h,LONG idObj,LONG idChild,DWORD,DWORD){
    if(idObj!=OBJID_WINDOW || idChild!=CHILDID_SELF || !h) return;
    if(ev==EVENT_OBJECT_LOCATIONCHANGE){
        // the hot one: every window move on the system lands here. One hash lookup, then out.
        if(!KomoIndexManaged(h)) return;
        if(GetAncestor(h,GA_ROOT)!=h || IsIconic(h)) return;
        RECT r; if(!GetWindowRect(h,&r) || r.left<=-20000) return;
        std::lock_guard<std::mutex> lk(g_ws2RectMtx); g_ws2LastRect[h]=r; return;
    }
    if(ev==EVENT_SYSTEM_MINIMIZEEND){
        if(g_wsSlideStyle!=0 || !g_wsSlide.load()) return;
        int m,w,f; if(!Ws2Locate(h,m,w,f)) return;
        if(m<0 || m>=WS2_MAXMON) return;
        RECT mon; if(!KomoIndexMonRect(m,mon)) return;
        int jobDir=0;
        { RECT rmon=Ws2ResolveMon(mon);
          std::lock_guard<std::mutex> lk(g_ws2Mtx);
          int slot=MonSlotFor(rmon);
          if(g_ws2[slot].active && WsNowMs()-g_ws2[slot].t0<400.0) jobDir=g_ws2[slot].dir; }
        // a restore on a workspace that is not showing = komorebi is switching (this arrives before its event);
        // a restore on the one showing right after a switch started = an arriving window that was still minimized
        if(w==f && !jobDir) return;
        WsTrace("ws2 restore mon=%d ws=%d (focused %d) job=%d",m,w,f,jobDir);
        // Joining a running slide is not a new leg. Starting one off a bare restore IS speculative: if komorebi's
        // event never follows, the user just un-minimized something by hand and there is no switch to animate.
        Ws2Add(m,mon,jobDir? jobDir : (w>f? 1 : -1),{{h,true}},false,jobDir==0);
    }
}
static void Ws2InstallHooks(){ /* installed by the layer thread in Ws2CreateWindow */ }

// Monitors were plugged, unplugged or rearranged. Every cached rect and every running slide describes a screen
// layout that no longer exists, so drop all of it rather than animate into a monitor that is not there.
static void Ws2MonitorsChanged(){
    { std::lock_guard<std::mutex> lk(g_ws2Mtx);
      for(int i=0;i<WS2_MAXMON;i++) if(g_ws2[i].active) g_ws2[i].cancel=true; }
    MonSlotsForget();
    { std::lock_guard<std::mutex> lk(g_ws2RectMtx); g_ws2LastRect.clear(); }
    g_ws2ThumbFail.store(0);                       // a new adapter / a new panel deserves a fresh try
    if(g_ws2Ctl){ PostMessageW(g_ws2Ctl,WS2_MSG_LAYERS,0,0);
                  PostMessageW(g_ws2Ctl,WS2_MSG_TACLEAR,0,0); }   // the tiling glide's remembered rects too
}

// the live wallpaper surface (Wallpaper Engine draws into a WorkerW behind the desktop)
static HWND Ws2WallpaperWindow(){
    // Windows 11 24H2 (and Aether's own desktop host) put the wallpaper WorkerW INSIDE Progman, with Wallpaper Engine's
    // WPEDesktopDX11Window inside that - so the top-level window to preview is Progman itself (a thumbnail of a top-level
    // window includes its children). The old layout had a separate top-level WorkerW behind Progman; kept as a fallback.
    HWND pm=nullptr;
    EnumWindows([](HWND h,LPARAM lp)->BOOL{
        wchar_t cls[32]={0}; GetClassNameW(h,cls,32);
        if(wcscmp(cls,L"Progman")==0 && IsWindowVisible(h)){
            HWND w=FindWindowExW(h,nullptr,L"WorkerW",nullptr);
            if(w && FindWindowExW(w,nullptr,nullptr,nullptr)){ *(HWND*)lp=h; return FALSE; } }
        return TRUE; },(LPARAM)&pm);
    if(pm) return pm;
    HWND found=nullptr;
    EnumWindows([](HWND h,LPARAM lp)->BOOL{
        wchar_t cls[32]={0}; GetClassNameW(h,cls,32);
        if(wcscmp(cls,L"WorkerW")==0 && IsWindowVisible(h) && !FindWindowExW(h,nullptr,L"SHELLDLL_DefView",nullptr)){ *(HWND*)lp=h; return FALSE; }
        return TRUE; },(LPARAM)&found);
    return found;
}

// niri's workspace switch is a critically damped spring; this is that shape stretched over the duration:
// a soft start, most of the travel in the first half, and a long settle with no overshoot.
static float Ws2Ease(float t){
    if(t>=1.0f) return 1.0f;
    float e=1.0f-(1.0f+7.0f*t)*expf(-7.0f*t);
    if(t>0.97f) e=e+(1.0f-e)*((t-0.97f)/0.03f);      // land exactly on 1
    return e;
}

static void Ws2Run(int idx){
    struct TimerRes{ TimerRes(){ timeBeginPeriod(1); } ~TimerRes(){ timeEndPeriod(1); } } tr;
    HWND layer=g_ws2Wnd[idx];
    std::unordered_map<HWND,HTHUMBNAIL> thumbs;
    std::vector<HTHUMBNAIL> bg, staticThumbs;
    std::vector<HWND>       statics;

    // Everything below can fail or bail; this is the one place any of it is undone. Before it existed, a slide
    // that gave up left the layer covering the screen, thumbnails registered, and komorebi's animations disabled
    // for the rest of the session.
    struct Guard {
        int idx; HWND layer;
        std::unordered_map<HWND,HTHUMBNAIL>* thumbs;
        std::vector<HTHUMBNAIL>*bg;  std::vector<HTHUMBNAIL>*st;
        bool shown=false;
        ~Guard(){
            if(shown && layer) ShowWindow(layer,SW_HIDE);
            for(auto& kv:*thumbs) if(kv.second) DwmUnregisterThumbnail(kv.second);
            for(auto t:*st) DwmUnregisterThumbnail(t);
            for(auto t:*bg) DwmUnregisterThumbnail(t);
            // A switch can land in the sliver between the last frame and here. Dropping it would be the old
            // "sometimes it just doesn't animate", and handing it back without keeping the worker flag would leave
            // an active job nobody drives - so both decisions are made under the one lock Ws2Add also holds.
            bool restart=false;
            { std::lock_guard<std::mutex> lk(g_ws2Mtx);
              Ws2Job& j=g_ws2[idx];
              restart = j.active && !j.cancel && j.dirty && !j.wins.empty();
              if(!restart) j=Ws2Job();
              g_ws2Worker[idx].store(restart); }
            if(g_ws2Running.fetch_sub(1)-1<=0){ g_ws2Running.store(0); g_wsSlideBusy.store(false); }
            g_wsSlideEndAt.store(GetTickCount64());
            KomoAnimResumeSoon(220);
            if(restart) std::thread(Ws2Run,idx).detach();
        }
    } guard{idx,layer,&thumbs,&bg,&staticThumbs};
    g_ws2Running.fetch_add(1); g_wsSlideBusy.store(true);

    if(!layer || !g_ws2Desk){ WsTrace("ws2[%d]: no layer",idx); return; }

    RECT mon; int dir;
    { std::lock_guard<std::mutex> lk(g_ws2Mtx);
      if(!g_ws2[idx].active || g_ws2[idx].cancel) return;
      mon=g_ws2[idx].mon; dir=g_ws2[idx].dir; }
    const int MW=mon.right-mon.left, MH=mon.bottom-mon.top;
    if(MW<64||MH<64){ WsTrace("ws2[%d]: monitor %ldx%ld - nothing to slide",idx,(long)MW,(long)MH); return; }

    // ---- the layer: over the monitor, just under the bar ----
    SetWindowPos(layer,HWND_TOPMOST,mon.left,mon.top,MW,MH,SWP_NOACTIVATE);
    if(g_ws2Bar) SetWindowPos(layer,g_ws2Bar,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

    auto thumbOf=[&](HWND src,RECT srcRectScreen,RECT dst,BYTE op)->HTHUMBNAIL{
        HTHUMBNAIL t=nullptr; if(FAILED(DwmRegisterThumbnail(layer,src,&t))) return nullptr;
        RECT wr; GetWindowRect(src,&wr);
        DWM_THUMBNAIL_PROPERTIES p{}; p.dwFlags=DWM_TNP_RECTDESTINATION|DWM_TNP_RECTSOURCE|DWM_TNP_VISIBLE|DWM_TNP_OPACITY|DWM_TNP_SOURCECLIENTAREAONLY;
        p.rcDestination=dst; p.rcSource=RECT{srcRectScreen.left-wr.left,srcRectScreen.top-wr.top,srcRectScreen.right-wr.left,srcRectScreen.bottom-wr.top};
        p.fVisible=TRUE; p.opacity=op; p.fSourceClientAreaOnly=FALSE;
        DwmUpdateThumbnailProperties(t,&p); return t; };

    { HWND wp=Ws2WallpaperWindow(); if(wp){ HTHUMBNAIL t=thumbOf(wp,mon,RECT{0,0,MW,MH},255); if(t) bg.push_back(t); } }
    { HTHUMBNAIL t=thumbOf(g_ws2Desk,mon,RECT{0,0,MW,MH},255); if(t) bg.push_back(t);
      WsTrace("ws2[%d] desk thumb ok=%d",idx,(int)(t!=nullptr)); }
    if(bg.empty()){
        // Nothing to put behind the previews, so covering the screen would only black it out. Some laptop drivers
        // will not preview a DirectComposition window at all; after a couple of tries stop asking and let the
        // real-window style take over (WsSlideFire reads this).
        int n=g_ws2ThumbFail.fetch_add(1)+1;
        WsTrace("ws2[%d]: no background thumbnail (%d) - no slide",idx,n);
        return;
    }
    g_ws2ThumbFail.store(0);

    // windows on this monitor that are part of neither workspace (unmanaged, floating elsewhere) stay put, on top
    { struct C{ RECT mon; std::vector<HWND>* v; } c{mon,&statics};
      EnumWindows([](HWND h,LPARAM lp)->BOOL{ C* c=(C*)lp;
          if(Ws2IsLayer(h) || !WsAnimatable(h)) return TRUE;
          if(KomoIndexManaged(h)) return TRUE;                     // komorebi's: only the two workspaces show
          if(!WsOnMonitor(h,c->mon)) return TRUE;
          c->v->push_back(h); return TRUE; },(LPARAM)&c); }

    // The layer is NOT shown yet. It used to go up here, before a single window thumbnail had been placed, so the
    // first frame on screen was the bare desktop with every window missing - a flash of an empty workspace at the
    // start of every slide. It is shown at the bottom of the first pass instead, once its contents are in place.
    bool staticsDone=false;
    int  badFlush=0;                 // DwmFlush is not pacing us any more
    double runStart=WsNowMs();
    WsTrace("ws2[%d] run: dir=%d statics=%d",idx,dir,(int)statics.size());

    // One turn of this loop is one leg: the strip travels and comes to rest. A switch that arrives while the strip
    // is MOVING is picked up inside the frame loop below with no break in the animation; one that arrives while it
    // is settling comes back round to here. Either way the layer, the thumbnails and the background stay up - the
    // teardown only happens when the slide is really over.
  for(;;){
    for(;;){
        // ---- anything that joined since the last frame needs a thumbnail before it can be placed ----
        {
            std::vector<HWND> fresh;
            { std::lock_guard<std::mutex> lk(g_ws2Mtx);
              for(auto& w:g_ws2[idx].wins) if(!thumbs.count(w.h)) fresh.push_back(w.h); }
            for(HWND h:fresh){
                HTHUMBNAIL t=nullptr; HRESULT hr=DwmRegisterThumbnail(layer,h,&t);
                thumbs[h]=SUCCEEDED(hr)? t : nullptr;
                WsTrace("ws2[%d] thumb hwnd=%p hr=0x%08lX iconic=%d",idx,(void*)h,(unsigned long)hr,(int)IsIconic(h));
            }
        }
        if(!staticsDone){
            for(auto it=statics.rbegin(); it!=statics.rend(); ++it){
                RECT r; if(!GetWindowRect(*it,&r)) continue;
                HTHUMBNAIL t=thumbOf(*it,r,RECT{r.left-mon.left,r.top-mon.top,r.right-mon.left,r.bottom-mon.top},255);
                if(t) staticThumbs.push_back(t);
            }
            staticsDone=true;
        }
        // ---- read the job, compute every destination, all under one short lock ----
        struct Upd { HTHUMBNAIL t; RECT d; };
        std::vector<Upd> upd;
        std::vector<HWND> drop;
        bool done=false, cancel=false, empty=false;
        double now=WsNowMs();
        {
            std::lock_guard<std::mutex> lk(g_ws2Mtx);
            Ws2Job& j=g_ws2[idx];
            if(!j.active || j.cancel){ cancel=true; }
            else {
                // A slide that began on a bare window restore and was never confirmed by komorebi is not a
                // workspace switch at all - do not sit on top of the screen for it. komorebi's event follows the
                // restore by 30-90 ms in every trace taken here, so this only ever catches the real strays.
                if(j.speculative && now-j.t0>250.0){ cancel=true; j.cancel=true; }
                j.dirty=false;
                if(j.relegged){
                    j.relegged=false;
                    for(auto& kv:thumbs){ bool still=false; for(auto& w:j.wins) if(w.h==kv.first){ still=true; break; } if(!still) drop.push_back(kv.first); }
                }
                float t=(float)std::clamp((now-j.t0)/std::max(1,j.ms),0.0,1.0);
                float e=Ws2Ease(t);
                for(auto& w:j.wins){
                    // komorebi may still be placing an arriving window: follow it rather than the rect we captured
                    if(w.incoming && IsWindow(w.h) && !IsIconic(w.h)){
                        RECT live; if(GetWindowRect(w.h,&live) && live.left>-20000 && Ws2RectOnMon(live,mon)) w.r=live;
                    }
                    w.offNow = w.off0 + (w.off1-w.off0)*e;
                    auto it=thumbs.find(w.h);
                    if(it==thumbs.end() || !it->second) continue;
                    int off=(int)lroundf(w.offNow);
                    RECT d{w.r.left-mon.left,w.r.top-mon.top,w.r.right-mon.left,w.r.bottom-mon.top};
                    if(g_wsSlideVert.load()){ d.top+=off; d.bottom+=off; } else { d.left+=off; d.right+=off; }
                    upd.push_back({it->second,d});
                }
                if(t>=1.0f) done=true;
                // A leg with nothing on it: komorebi told us a workspace changed but no window of either side was
                // reachable. Covering the screen with a frozen desktop for the whole duration is worse than not
                // animating, so give it a moment for late joiners and then get out of the way.
                if(j.wins.empty() && now-j.t0>140.0) empty=true;
            }
        }
        if(cancel||empty){ WsTrace("ws2[%d] %s",idx,cancel?"cancelled":"nothing to slide"); return; }

        for(HWND h:drop){ auto it=thumbs.find(h); if(it!=thumbs.end()){ if(it->second) DwmUnregisterThumbnail(it->second); thumbs.erase(it); } }

        for(auto& u:upd){
            DWM_THUMBNAIL_PROPERTIES p{}; p.dwFlags=DWM_TNP_RECTDESTINATION|DWM_TNP_VISIBLE|DWM_TNP_SOURCECLIENTAREAONLY;
            p.rcDestination=u.d; p.fVisible=TRUE; p.fSourceClientAreaOnly=FALSE;
            DwmUpdateThumbnailProperties(u.t,&p);
        }
        if(!guard.shown){
            ShowWindow(layer,SW_SHOWNOACTIVATE); guard.shown=true;
            // the clock starts at the first frame that is actually on screen, so no part of the slide is skipped
            double t0=WsNowMs(); std::lock_guard<std::mutex> lk(g_ws2Mtx); if(g_ws2[idx].t0<t0) g_ws2[idx].t0=t0;
        }

        if(done) break;
        // A slide that runs long past its duration is a slide the compositor is not letting us drive. Stop rather
        // than hold the screen hostage.
        if(now-runStart > 3000.0){ WsTrace("ws2[%d] deadline",idx); break; }

        // ---- pacing ----
        // DwmFlush waits for the compositor's next frame, which is exactly right when the compositor is keeping
        // time. On a laptop (hybrid graphics, panel self-refresh, a power plan throttling the GPU) it can block for
        // hundreds of milliseconds, and stall.txt shows it doing 150-900 ms here too. Three bad flushes and we pace
        // ourselves instead - the animation is driven by the wall clock either way, so it stays the right length.
        if(badFlush<3){
            double a=WsNowMs(); bool ok=PaceFlush(100); double dt=WsNowMs()-a;
            if(!ok || dt>40.0){ badFlush++; WsTrace("ws2[%d] slow flush %.1f ms (%d)",idx,dt,badFlush); }
            else if(badFlush>0) badFlush--;
        } else {
            int fps=std::clamp(g_wsSlideFps.load(),30,240);
            Sleep((DWORD)std::max(1,1000/fps));
        }
    }

    // ---- the landing ----
    // The previews stop exactly where the real windows belong - but komorebi may not have restored them yet, and
    // lifting the layer before it has is what made a window appear out of nowhere, or flashed the workspace
    // underneath for a frame. Hold the last frame, tracking wherever komorebi actually puts each window, until
    // they are all really there (or until it has clearly given up).
    {
        const double until=WsNowMs()+250.0;
        for(;;){
            bool waiting=false;
            struct Upd { HTHUMBNAIL t; RECT d; };
            std::vector<Upd> upd;
            { std::lock_guard<std::mutex> lk(g_ws2Mtx);
              Ws2Job& j=g_ws2[idx];
              if(!j.active || j.cancel || j.dirty) break;         // cancelled, or new work arrived
              for(auto& w:j.wins){
                  if(!w.incoming) continue;
                  if(!IsWindow(w.h)) continue;
                  if(IsIconic(w.h)){ if(!IsHungAppWindow(w.h)) waiting=true; continue; }
                  RECT live; if(!GetWindowRect(w.h,&live) || !Ws2RectOnMon(live,mon)) continue;
                  if(live.left!=w.r.left || live.top!=w.r.top || live.right!=w.r.right || live.bottom!=w.r.bottom){
                      w.r=live; waiting=true;
                      auto it=thumbs.find(w.h);
                      if(it!=thumbs.end() && it->second)
                          upd.push_back({it->second,RECT{live.left-mon.left,live.top-mon.top,live.right-mon.left,live.bottom-mon.top}});
                  }
              } }
            for(auto& u:upd){
                DWM_THUMBNAIL_PROPERTIES p{}; p.dwFlags=DWM_TNP_RECTDESTINATION|DWM_TNP_VISIBLE|DWM_TNP_SOURCECLIENTAREAONLY;
                p.rcDestination=u.d; p.fVisible=TRUE; p.fSourceClientAreaOnly=FALSE;
                DwmUpdateThumbnailProperties(u.t,&p);
            }
            if(!waiting || WsNowMs()>until) break;
            if(badFlush<3) PaceFlush(100); else Sleep(8);
        }
        PaceFlush(100); // the held frame is composited before the layer goes
    }

    // A switch that landed during the settle: run another leg on the same layer rather than tearing everything
    // down and building it again (which is what the black flash between rapid switches used to be).
    bool again=false;
    { std::lock_guard<std::mutex> lk(g_ws2Mtx);
      Ws2Job& j=g_ws2[idx];
      again = j.active && !j.cancel && j.dirty && !j.wins.empty(); }
    if(!again) break;
    runStart=WsNowMs();            // a new leg gets its own deadline
    WsTrace("ws2[%d] another leg",idx);
  }
    WsTrace("ws2[%d] done",idx);
    // The guard hides the layer, drops every thumbnail, clears the job and lets komorebi animate again. If another
    // switch arrived in the last instant it simply starts a fresh job (and a fresh worker) from a clean slate.
}
