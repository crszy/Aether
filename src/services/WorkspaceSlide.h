// src/services/WorkspaceSlide.h  —  Aether shell
// niri-style workspace slide for komorebi workspace switches.
// Aether is a GPL-3.0 derivative of Caelestia Shell.
//
// Ported from the user's own standalone komorebi_anim ("Workspace Slide Animator",
// Z:\komorebi_anim_v8fix\...\komorebi_anim_final.cpp) so it runs inside the shell instead of as a
// second process fighting for the same event pipe. Same idea as niri's horizontal workspace slide:
// the outgoing workspace's windows travel off one edge while the incoming ones travel in from the
// other, on an expo curve, and everything snaps to its canonical position at the end.
//
// Three things had to change in the port:
//   * The shell's OWN overlays must never be captured. The standalone tool filtered by window
//     class, and every Aether layer is class "A" - it would have slid the bar off the screen.
//   * komorebi's monitor rect comes from the state document we already parse, not from
//     EnumDisplayMonitors, because komorebi's monitor order is its own.
//   * Only real pipe EVENTS animate. The 5-second `komorebic state` heartbeat also observes
//     workspace changes, and animating off a poll would replay a switch seconds after it happened.
//
// Requires komorebi's "window_hiding_behaviour": "Cloak". Under Minimize/Hide there is nothing to
// slide (the windows are already gone), and the no-cloak fallback below just slides the old set out.
#pragma once
// Older SDK headers do not carry these in the DWMWINDOWATTRIBUTE enum.
#ifndef DWMWA_CLOAK
#define DWMWA_CLOAK 13
#endif
#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif
#ifndef DWMWA_TRANSITIONS_FORCEDISABLED
#define DWMWA_TRANSITIONS_FORCEDISABLED 3
#endif

struct WsSnap { HWND hwnd; RECT r; bool wasCloaked; };

// The outgoing workspace, handed over by the komorebi event path.
//
// This used to be discovered with WsCapture(mon, cloaked=true) - find the windows komorebi has
// cloaked, they are the ones we just left. That silently never worked: reading a foreign window's
// cloak state is fine, but nothing in this shell could ever SET it (DWMWA_CLOAK only applies to
// windows the calling process owns), and under komorebi's Minimize mode there is nothing cloaked to
// find in the first place. So the outgoing set was always empty and only the arriving windows ever
// animated.
//
// Being handed the list closes half of that gap - but only when the list is taken at the right
// MOMENT. komorebi has already minimised those windows by the time it tells us the workspace
// changed, so a snapshot taken from the event arrives empty: traced it, and the leaving window's X
// never moved once across an entire switch. The outgoing half has to be captured BEFORE komorebi
// acts.
//
// For a switch the shell itself starts - clicking a workspace chip in the bar - we are the ones who
// call komorebi, so we can snapshot first and animate what is still on screen. That is what the
// `preemptive` flag is for. A switch started from a whkd keybind still goes straight to komorebi
// without passing through here, and for those the outgoing half remains unreachable; the incoming
// half animates either way.
static std::mutex             g_wsOutMtx;
static std::vector<WsSnap>    g_wsOutSnap;          // guarded by g_wsOutMtx
static std::atomic<ULONGLONG> g_wsOutPre{0};        // tick of the last pre-switch snapshot
static const int              WS_PARK = -32000;     // where a hidden window is parked

static void WsSlideAdoptOutgoing(const std::vector<HWND>& hs, bool preemptive=false){
    // Do not let komorebi's post-hoc notification wipe a good pre-switch snapshot with an empty one.
    if(!preemptive){
        ULONGLONG pre=g_wsOutPre.load();
        if(pre && GetTickCount64()-pre < 1500){
            std::lock_guard<std::mutex> lk(g_wsOutMtx);
            if(!g_wsOutSnap.empty()) return;
        }
    }
    std::lock_guard<std::mutex> lk(g_wsOutMtx);
    g_wsOutSnap.clear();
    for(HWND h:hs){
        if(!IsWindow(h) || IsIconic(h)) continue;
        RECT r{}; if(!GetWindowRect(h,&r)) continue;
        if(r.left <= WS_PARK + 1000) continue;       // already parked
        g_wsOutSnap.push_back({h,r,false});
    }
    if(preemptive) g_wsOutPre.store(GetTickCount64());
}

// The shell cloaks the leaving workspace itself rather than waiting on komorebi's minimize (which
// animates, and on Electron windows can leave them on screen for seconds). Used by Komorebi.h.
// OFF: parking windows off-screen to hide them faster wedged Chromium apps (Discord came back as a
// blank grey rectangle and only an app restart fixed it) and was no faster than komorebi's own
// minimize. Kept as a switch rather than deleted so the finding stays visible. See KomoFastSwap.
static std::atomic<bool> g_fastHide{false};       // Settings > Taskbar > Workspaces
static std::atomic<bool> g_wsSlide{true};         // Settings > Taskbar > Workspaces
static std::atomic<int>  g_wsSlideMs{320};        // 100..800
static std::atomic<int>  g_wsSlideFps{144};       // 30..240
static std::atomic<bool> g_wsSlideVert{false};    // horizontal, like niri
static std::atomic<int>  g_wsSlideSettle{80};     // let komorebi finish cloaking before capturing
static std::atomic<bool> g_wsSlideBoth{true};     // uncloak the outgoing set so both halves move

static std::atomic<bool> g_wsSlideBusy{false};
// How short a leg gets when workspaces are switched in a burst (workspaces.slide_min_ms). Flicking through
// workspaces used to drop every switch but one; now each one animates, just faster, down to this floor.
// A plain int, like g_wsSlideStyle beside it: it is a workspaces.* row in the TOML settings table, which writes
// through an int*. Reading it from a slide worker while Settings writes it is an aligned 32-bit access either way.
static int g_wsSlideMinMs = 110;                  // 60..400
// DWM refused to hand out a preview of the desktop this many times in a row. Some laptop drivers will not
// preview a DirectComposition window at all, and on those the "preview" style can never draw anything - so after
// a couple of failures the switch routes itself to the real-window slide instead of going silently dead.
static std::atomic<int>  g_ws2ThumbFail{0};
static RECT Ws2ResolveMon(RECT komo);             // fwd (WorkspaceSlide2.h): komorebi's rect -> the live monitor
static void Ws2NoteRect(HWND h);                  // fwd (WorkspaceSlide2.h): remember where a window is right now
static void TaNoteRect(HWND h);                   // fwd (TileAnim.h): same, for the tiling glide
static void TaOpenAnim(HWND h);                   // fwd (TileAnim.h): a window that just appeared
static bool Ws2IsLayer(HWND h);                   // fwd (WorkspaceSlide2.h): is this one of the slide's own layers?
// ---- the tiling animation (TileAnim.h / TileAnim2.h) ------------------------------------------------------
// "preview": previews of the windows move AND resize on a layer over the screen, the real windows never move
//            (TileAnim2.h). "windows": the older way - komorebi's placement is kept but the window is put back
//            and translated to it, so only its position animates and every resize still snaps (TileAnim.h).
static const char* TILESTYLE_NAMES[] = { "preview", "windows" };
static int  g_tileStyle = 0;                      // windows.tile_style
static std::atomic<bool>      g_t2Busy{false};    // a tiling animation's layer is over a screen right now
static std::atomic<ULONGLONG> g_t2EndAt{0};
static bool T2IsLayer(HWND h);                             // fwd (TileAnim2.h)
static void T2Add(HWND h,const RECT& from,const RECT& to); // fwd (TileAnim2.h)
static void T2CancelAll();                                 // fwd (TileAnim2.h)
static void T2MakeLayers(HINSTANCE hi);                    // fwd (TileAnim2.h)

static const char* WSSLIDESTYLE_NAMES[] = { "preview", "windows" };
static int g_wsSlideStyle=0;                      // workspaces.slide_style: 0 thumbnails (WorkspaceSlide2.h), 1 move the real windows
static void Ws2Fire(int kMon,int oldWs,int newWs,RECT mon);   // fwd (WorkspaceSlide2.h)
static std::atomic<ULONGLONG> g_wsSlideEndAt{0};   // when the last slide finished (tiling animation stays out of its way)
// ---- komorebi's own animations, paused only while OUR workspace slide runs -------------------------------
// komorebi animating window moves at the same moment Aether slides the workspace makes the two fight (windows
// jump to komorebi's in-between positions mid-slide). So: switch starts -> `komorebic animation disable`;
// slide finished (and nothing else queued for a beat) -> `komorebic animation enable`. Everything else - tiling,
// resizing, moving windows - keeps komorebi's animations.
static bool KomoRun(const std::wstring& args, std::string* out);   // fwd (Komorebi.h)
static std::atomic<bool>      g_wsPauseKomoAnim{true};  // workspaces.pause_komorebi_animations
static std::atomic<bool>      g_komoAnimOff{false};     // we turned them off and still owe an enable
static std::atomic<ULONGLONG> g_komoAnimResumeAt{0};
static std::atomic<bool>      g_komoAnimResumer{false};
// Only pause what is actually on. komorebi's movement animation resizes Electron/Chromium windows every frame and
// leaves them stuck grey, so most setups keep it OFF - and blindly sending "animation enable" after a slide turned it
// ON for them. Read the user's komorebi.json (KOMOREBI_CONFIG_HOME or %USERPROFILE%) and leave it alone when disabled.
static bool KomoAnimConfigured(){
    static ULONGLONG at=0; static bool cached=false;
    ULONGLONG now=GetTickCount64(); if(at && now-at<5000) return cached; at=now;
    wchar_t dir[MAX_PATH]={0}; std::wstring path;
    if(GetEnvironmentVariableW(L"KOMOREBI_CONFIG_HOME",dir,MAX_PATH)) path=std::wstring(dir)+L"\\komorebi.json";
    else if(GetEnvironmentVariableW(L"USERPROFILE",dir,MAX_PATH)) path=std::wstring(dir)+L"\\komorebi.json";
    FILE* f=_wfopen(path.c_str(),L"rb"); cached=false; if(!f) return cached;
    std::string c; char buf[8192]; size_t r; while((r=fread(buf,1,sizeof(buf),f))>0) c.append(buf,r); fclose(f);
    size_t a=c.find("\"animation\""); if(a==std::string::npos) return cached;
    size_t e=c.find("\"enabled\"",a), close=c.find('}',a);
    if(e!=std::string::npos && e<close){ size_t v=c.find_first_not_of(" \t\r\n:",e+9); cached = v!=std::string::npos && c.compare(v,4,"true")==0; }
    return cached;
}
static void KomoAnimResumeSoon(int ms);                   // fwd
static void KomoAnimPauseNow(){                           // call on a worker thread (spawns komorebic)
    if(!g_wsPauseKomoAnim.load() || !KomoAnimConfigured()) return;
    g_komoAnimResumeAt.store(0);
    if(!g_komoAnimOff.exchange(true)) KomoRun(L"animation disable", nullptr);
    // A watchdog, because "disable" and "enable" used to be paired by the slide alone: any slide that never
    // started (no windows to animate, DWM refused a thumbnail, the monitor was gone) left komorebi's own
    // animations switched off for the rest of the session. The slide's own resume overrides this with a much
    // shorter delay as soon as it finishes.
    KomoAnimResumeSoon(2500);
}
static void KomoAnimPause(){ if(g_wsPauseKomoAnim.load() && !g_komoAnimOff.load()) std::thread(KomoAnimPauseNow).detach(); }
static void KomoAnimResumeSoon(int ms){
    if(!g_komoAnimOff.load()) return;
    g_komoAnimResumeAt.store(GetTickCount64()+(ULONGLONG)ms);
    if(g_komoAnimResumer.exchange(true)) return;
    std::thread([]{
        for(;;){
            ULONGLONG due=g_komoAnimResumeAt.load(), now=GetTickCount64();
            if(!g_komoAnimOff.load()) break;
            if(due==0 || g_wsSlideBusy.load()){ Sleep(40); if(due==0 && !g_wsSlideBusy.load() && g_komoAnimResumeAt.load()==0){ continue; } continue; }
            if(now>=due){ if(g_komoAnimOff.exchange(false)) KomoRun(L"animation enable", nullptr); break; }
            Sleep((DWORD)std::min<ULONGLONG>(due-now,40));
        }
        g_komoAnimResumer.store(false);
    }).detach();
}
static std::atomic<bool> g_wsSlideAbort{false};
// ---- slide trace (-s slide_trace=1): timestamps of what komorebi and the slide do, to slidetrace.log ----
static std::atomic<bool> g_wsTrace{false};
static std::mutex g_wsTraceMtx;
static double WsNowMs(){ static LARGE_INTEGER f{}; if(!f.QuadPart) QueryPerformanceFrequency(&f); LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart*1000.0/f.QuadPart; }
static void WsTrace(const char* fmt,...){
    if(!g_wsTrace.load()) return;
    char msg[512]; va_list ap; va_start(ap,fmt); vsnprintf(msg,sizeof(msg),fmt,ap); va_end(ap);
    std::lock_guard<std::mutex> lk(g_wsTraceMtx);
    if(FILE* f=fopen((ExeDir()+"slidetrace.log").c_str(),"a")){ fprintf(f,"%10.1f  %s\n",WsNowMs(),msg); fclose(f); }
}
static ULONGLONG         g_wsSlideCooldown[16] = {};

static float WsEaseOutExpo(float t){ return t>=1.0f ? 1.0f : 1.0f-powf(2.0f,-10.0f*t); }
static float WsEaseInExpo (float t){ return t<=0.0f ? 0.0f : powf(2.0f, 10.0f*t-10.0f); }

static bool WsIsCloaked(HWND h){
    BOOL c=FALSE; DwmGetWindowAttribute(h,DWMWA_CLOAKED,&c,sizeof(c)); return c!=FALSE;
}
static void WsSetCloak(HWND h,bool on){
    BOOL v=on?TRUE:FALSE; DwmSetWindowAttribute(h,DWMWA_CLOAK,&v,sizeof(v));
}
static void WsFreezeTransitions(HWND h,bool freeze){
    BOOL off=freeze?TRUE:FALSE;
    DwmSetWindowAttribute(h,DWMWA_TRANSITIONS_FORCEDISABLED,&off,sizeof(off));
}
static void WsMove(HWND h,int x,int y){
    SetWindowPos(h,nullptr,x,y,0,0,
                 SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOSIZE|SWP_NOSENDCHANGING);
}

// Is this a window a workspace switch should move?
static bool WsAnimatable(HWND h){
    if(!IsWindow(h) || IsIconic(h)) return false;
    if(!(GetWindowLong(h,GWL_STYLE)&WS_VISIBLE)) return false;
    // never touch our own overlays - the bar, the desktop layer, the drawer, all of them
    DWORD pid=0; GetWindowThreadProcessId(h,&pid);
    if(pid==GetCurrentProcessId()) return false;
    if(!GetWindowTextLengthW(h)) return false;        // titleless tool windows are not workspaces
    wchar_t cls[128]={0}; GetClassNameW(h,cls,128);
    static const wchar_t* SKIP[]={ L"Shell_TrayWnd",L"Shell_SecondaryTrayWnd",L"Progman",L"WorkerW",
        L"DV2ControlHost",L"Windows.UI.Core.CoreWindow",L"SysShadow",L"Button",
        L"TopLevelWindowForOverflowXamlIsland",L"Xaml_WindowedPopupClass",nullptr };
    for(int i=0;SKIP[i];i++) if(!wcscmp(cls,SKIP[i])) return false;
    return true;
}

static bool WsOnMonitor(HWND h,const RECT& mon){
    RECT r{}; if(!GetWindowRect(h,&r)) return false;
    LONG ix1=std::max(r.left,mon.left),  iy1=std::max(r.top,mon.top);
    LONG ix2=std::min(r.right,mon.right),iy2=std::min(r.bottom,mon.bottom);
    if(ix2<=ix1||iy2<=iy1) return false;
    double area=(double)(r.right-r.left)*(r.bottom-r.top);
    if(area<1) return false;
    return (double)(ix2-ix1)*(iy2-iy1)*2.0 >= area;      // at least half the window is on it
}

struct WsCapCtx { std::vector<WsSnap>* v; const RECT* mon; bool wantCloaked; };
static BOOL CALLBACK WsCapProc(HWND h,LPARAM lp){
    WsCapCtx* c=(WsCapCtx*)lp;
    if(!WsAnimatable(h)) return TRUE;
    if(WsIsCloaked(h)!=c->wantCloaked) return TRUE;
    if(!WsOnMonitor(h,*c->mon)) return TRUE;
    RECT r{}; GetWindowRect(h,&r);
    c->v->push_back({h,r,c->wantCloaked});
    return TRUE;
}
static std::vector<WsSnap> WsCapture(const RECT& mon,bool cloaked){
    std::vector<WsSnap> out; WsCapCtx c{&out,&mon,cloaked};
    EnumWindows(WsCapProc,(LPARAM)&c); return out;
}

// A switch that arrives while a slide is running does not cancel it - it QUEUES, and the worker
// picks it up the instant the current one unwinds. Set by WsSlideFire, consumed at the bottom of
// WsSlideThread.
static std::atomic<bool> g_wsPending{false};
static RECT g_wsPendMon{}; static int g_wsPendDir=1; static int g_wsPendDur=320;

static void WsSlideThread(RECT mon,int dir,int durMs){
    struct Done { ~Done(){ g_wsSlideAbort.store(false); g_wsSlideEndAt.store(GetTickCount64()); g_wsSlideBusy.store(false); KomoAnimResumeSoon(220); } } done;
  for(;;){
    int settle=std::clamp(g_wsSlideSettle.load(),0,300);
    if(settle) Sleep(settle);                    // let komorebi finish cloaking / uncloaking

    // After the settle the cloaked set is the workspace we just left and the visible set is the one
    // we arrived at - komorebi has already done the swap, we are only animating the transition.
    // the handover wins when it has something; WsCapture's cloak probe is the old fallback and only
    // finds anything when komorebi itself is doing the cloaking (its Cloak mode)
    std::vector<WsSnap> outgoing;
    { std::lock_guard<std::mutex> lk(g_wsOutMtx); outgoing.swap(g_wsOutSnap); }
    bool handedOver = !outgoing.empty();
    if(!handedOver) outgoing=WsCapture(mon,true);
    std::vector<WsSnap> incoming=WsCapture(mon,false);
    WsTrace("slide start: outgoing=%d incoming=%d (settle %d)",(int)outgoing.size(),(int)incoming.size(),settle);
    bool noCloak=false;
    if(outgoing.empty() && incoming.empty()){
        if(!g_wsPending.exchange(false)) return;
        mon=g_wsPendMon; dir=g_wsPendDir; durMs=g_wsPendDur; g_wsSlideAbort.store(false); continue;
    }
    if(outgoing.empty()){
        // komorebi is on Minimize/Hide rather than Cloak: nothing to slide out, so just bring the
        // arriving windows in. (Same fallback the standalone tool ended up with.)
        noCloak=true;
    }

    const int   ms  = std::clamp(durMs,60,1200);   // compressed by WsSlideFire during a burst
    const int   fps = std::clamp(g_wsSlideFps.load(),30,240);
    const bool  vert= g_wsSlideVert.load();
    const int   span= vert ? (mon.bottom-mon.top) : (mon.right-mon.left);
    // NEVER fully off-screen. A Chromium/Electron window (Discord, Spotify, browsers) that is moved entirely off
    // the monitor is marked occluded and stops painting - and often never starts again: measured, a window left
    // grey after a workspace move stayed grey through redraw, resize and minimize/restore. Travelling a fraction
    // of the monitor keeps every window mostly on screen, so it keeps rendering; the expo curve still reads as a slide.
    const int   dist= std::max(120,(int)(span*0.28f));
    const int   outD= dir * -dist;
    const int   inD = -dir *  dist;

    // Showing the outgoing windows is what makes this read as a slide rather than a pop-in, so
    // uncloak them for the duration and put the cloak back at the end.
    if(!handedOver && !noCloak && g_wsSlideBoth.load())
        for(auto& s:outgoing){ WsSetCloak(s.hwnd,false); WsFreezeTransitions(s.hwnd,true); }
    else
        for(auto& s:outgoing) WsFreezeTransitions(s.hwnd,true);   // handed over: still on screen

    for(auto& s:incoming){
        if(IsIconic(s.hwnd)) continue;
        WsFreezeTransitions(s.hwnd,true);
        if(vert) WsMove(s.hwnd,s.r.left,s.r.top+inD);
        else     WsMove(s.hwnd,s.r.left+inD,s.r.top);
    }

    const int totalFrames=std::max(1,(ms*fps)/1000);
    const DWORD frameMs=(DWORD)std::max(1,1000/fps);

    for(int f=0;f<=totalFrames;f++){
        if(g_wsSlideAbort.load()) break;          // a newer switch owns the screen now: snap and go
        float t=(float)f/(float)totalFrames;
        float tO=WsEaseInExpo(t), tI=WsEaseOutExpo(t);
        // a window komorebi has minimized (it hides the old workspace that way) must not be moved: SetWindowPos on
        // an iconic window rewrites where it will RESTORE to, which is what left windows badly placed afterwards
        for(auto& s:outgoing){
            if(!IsWindow(s.hwnd) || IsIconic(s.hwnd)) continue;
            if(vert) WsMove(s.hwnd,s.r.left,s.r.top+(int)(outD*tO));
            else     WsMove(s.hwnd,s.r.left+(int)(outD*tO),s.r.top);
        }
        for(auto& s:incoming){
            if(!IsWindow(s.hwnd) || IsIconic(s.hwnd)) continue;
            if(vert) WsMove(s.hwnd,s.r.left,s.r.top+(int)(inD*(1.0f-tI)));
            else     WsMove(s.hwnd,s.r.left+(int)(inD*(1.0f-tI)),s.r.top);
        }
        if(!incoming.empty()){ RECT rr; GetWindowRect(incoming[0].hwnd,&rr); WsTrace("frame %d t=%.3f win0.x=%ld",f,t,rr.left); }
        if(f<totalFrames) Sleep(frameMs);
    }

    // Everything goes home. The outgoing set gets its cloak back so komorebi's own bookkeeping
    // still matches what is on screen.
    for(auto& s:incoming){
        if(!IsWindow(s.hwnd)) continue;
        if(!IsIconic(s.hwnd)) WsMove(s.hwnd,s.r.left,s.r.top);
        WsFreezeTransitions(s.hwnd,false);
    }
    for(auto& s:outgoing){
        if(!IsWindow(s.hwnd)) continue;
        // home, never parked off-screen (-32000 parking is exactly the move that wedges Chromium); komorebi hides them
        if(!IsIconic(s.hwnd)) WsMove(s.hwnd,s.r.left,s.r.top);
        if(!handedOver && !noCloak && g_wsSlideBoth.load()) WsSetCloak(s.hwnd,true);
        WsFreezeTransitions(s.hwnd,false);
    }
    // the rects above were read ~80ms after komorebi acted; if it was still laying the workspace out (or a window was
    // moved to another workspace and the layout re-flowed), ask it to lay everything out again so every window fits
    if(!g_wsPending.load()) std::thread([]{ Sleep(60); KomoRun(L"retile", nullptr); }).detach();

    // A switch queued while that was running starts NOW, at its own (shorter) duration. Looping in
    // this thread rather than spawning another keeps g_wsSlideBusy meaningful and means two slides
    // can never be moving the same window at once.
    if(!g_wsPending.exchange(false)) break;
    mon=g_wsPendMon; dir=g_wsPendDir; durMs=g_wsPendDur;
    g_wsSlideAbort.store(false);
  }
}

// Called from the komorebi event path when a monitor's focused workspace actually changed.
static void WsSlideFire(int komoMon,int oldWs,int newWs,RECT mon){
    WsTrace("FIRE mon=%d ws %d -> %d",komoMon,oldWs,newWs);
    if(!g_wsSlide.load() || oldWs==newWs) return;
    // komorebi's rect is a cached copy of a screen layout that may have changed under it (a laptop being docked,
    // undocked, or having its resolution or scale changed). It says WHICH monitor; Windows says where that monitor is.
    mon = Ws2ResolveMon(mon);
    // the preview style, unless DWM has already proved on this machine that it will not preview the desktop
    if(g_wsSlideStyle==0 && g_ws2ThumbFail.load()<2){ KomoAnimPause(); Ws2Fire(komoMon,oldWs,newWs,mon); return; }
    if(mon.right-mon.left<64 || mon.bottom-mon.top<64) return;
    KomoAnimPause();                                  // a keybind switch: komorebi already moved, stop its animation now
    int mi=std::clamp(komoMon,0,15);
    ULONGLONG now=GetTickCount64();
    int dir = (newWs>oldWs) ? 1 : -1;

    // Switching fast used to abort the running slide and then go quiet for 400ms, so a burst of
    // switches produced one animation and several dead jumps - the "glitch". Instead the animation
    // is COMPRESSED: the closer two switches are, the shorter the slide, down to a floor that keeps
    // it a movement rather than a teleport. Flick through five workspaces and you get five slides,
    // each quicker than the last, instead of one slide and four nothing.
    const int base = std::clamp(g_wsSlideMs.load(),80,1200);
    ULONGLONG gap = g_wsSlideCooldown[mi] ? (now - g_wsSlideCooldown[mi]) : 1000000ULL;
    g_wsSlideCooldown[mi] = now;                    // repurposed: last fire time, not a mute window
    int dur = base;
    if(gap < (ULONGLONG)(base*2)){
        float k = (float)gap / (float)(base*2);     // 0 = instant repeat, 1 = leisurely
        dur = (int)(base * std::clamp(k, 0.25f, 1.0f));
    }
    dur = std::clamp(dur, std::clamp(g_wsSlideMinMs,60,400), base);   // same floor as the preview style

    if(g_wsSlideBusy.load()){
        // hand it to the running worker; it will pick this up the moment it unwinds
        g_wsPendMon=mon; g_wsPendDir=dir; g_wsPendDur=dur;
        g_wsPending.store(true);
        g_wsSlideAbort.store(true);
        return;
    }
    if(g_wsSlideBusy.exchange(true)) return;
    std::thread(WsSlideThread,mon,dir,dur).detach();
}
