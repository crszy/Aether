// src/modules/overview/Overview.h  —  Aether shell
// The workspace overview: a 3-D view of every workspace on the monitor you are looking at.
//
// This is the desktop cube, the thing Compiz made famous and that every Linux desktop has had in
// some form since - workspaces arranged around a prism, spun with the mouse, each face showing what
// is really on that workspace. Windows has never had one. It also does the flat version (GNOME's
// overview, macOS Spaces): the same scene with the faces laid out in a row instead of wrapped
// around an axis. overview.style picks between them.
//
// HOW THE PICTURE IS MADE
//
// Each face is one texture of a whole workspace, and the windows on it are hit-tested from their
// rects rather than drawn separately. That matters for cost: a face is ONE capture, not one capture
// per window, so a workspace with fifteen windows costs exactly as much as one with two.
//
//   * the workspace you are on is captured LIVE, through Windows.Graphics.Capture (Capture.h), at
//     the display's own rate - a video playing on it plays in the overview;
//   * the ones you are not on cannot be captured at all. komorebi hides a workspace by MINIMIZING
//     its windows, so they are not on any screen and no capture API can reach them. Their faces
//     show the last frame taken while they WERE showing, which the render loop keeps refreshed a
//     couple of times a second, so it is never more than half a second stale when you switch away.
//
// WHY THE 3-D IS DONE BY HAND
//
// The whole shell is ImGui - there is not a single vertex shader in it - and adding a D3D pipeline
// for one screen would mean a second way of drawing everything. So the projection is a pinhole done
// on the CPU (the same persp = FOCAL/(FOCAL+z) the coverflow switcher uses) and each face is
// emitted as a grid of textured quads. The grid is the trick: ImGui interpolates texture
// coordinates affinely, so ONE quad for a rotated face would visibly warp across the diagonal;
// subdividing it makes each cell small enough that affine and perspective agree.
//
// The cursor is mapped back onto a face by inverting the bilinear map of its projected corners,
// which is what makes "click that window over there, on the face that is turned away from you"
// work without a depth buffer or a picking pass.
#pragma once

// ---- the scene -------------------------------------------------------------------------------
struct OvWin {                       // one window on a face, in monitor-local pixels
    HWND        h = nullptr;
    RECT        r{};
    std::wstring title;
};
struct OvFace {                      // one workspace of one monitor
    int   mon = 0, ws = 0;
    ID3D11Texture2D*          tex2d = nullptr; // owned; reused across refreshes (see SnapshotInto)
    ID3D11ShaderResourceView* tex = nullptr;   // owned; what this workspace looked like
    int   tw = 0, th = 0;
    std::vector<OvWin> wins;
    ULONGLONG at = 0;                // when that picture was taken
};
struct OvMonitor {                   // per physical screen
    HMONITOR hmon = nullptr;
    RECT     rect{};
    Cap::Session cap;
    ID3D11Texture2D*          liveTex = nullptr;   // newest frame, overwritten in place
    ID3D11ShaderResourceView* liveSrv = nullptr;
    int      lw = 0, lh = 0;
    ULONGLONG lastPull = 0;
    bool     tried = false;                        // we have attempted to start a session
};
static OvMonitor g_ovMon[WS2_MAXMON];
static std::vector<OvFace> g_ovFaces;              // rebuilt when the overview opens
static std::mutex g_ovFaceMtx;                     // guards g_ovFaces' stored textures

// ---- state -----------------------------------------------------------------------------------
static bool   g_ovShow    = false;   // the overlay is up (covers the closing animation too)
static bool   g_ovWant    = false;   // the user wants it open
static float  g_ovAnim    = 0.0f;    // 0 = flat against the screen, 1 = pulled back
static float  g_ovYaw     = 0.0f;    // radians (cube) or face-index (plane)
static float  g_ovYawTo   = 0.0f;
static float  g_ovZoom    = 1.0f;
static int    g_ovHome    = 0;       // the workspace we came from
static int    g_ovKomoMon = 0;       // komorebi's index for the monitor being shown
static int    g_ovPickWs  = -1;      // chosen on the way out
static HWND   g_ovPickWin = nullptr;
static float  g_ovPickT   = 0.0f;    // 0..1 zoom into the chosen window
static bool   g_ovDragging= false;
static float  g_ovDragX   = 0.0f, g_ovDragYaw = 0.0f;
static bool   g_ovDragged = false;   // moved far enough to count as a drag, not a click
static RECT   g_ovRect    = {0,0,0,0};
static std::atomic<int> g_ovReq{0};  // from the keyboard hook: 1 close, 2 left, 3 right, 4 accept

static void OvFreeFaces(){
    std::lock_guard<std::mutex> lk(g_ovFaceMtx);
    for(auto& f:g_ovFaces){ if(f.tex) f.tex->Release(); if(f.tex2d) f.tex2d->Release(); }
    g_ovFaces.clear();
}

// ---- capture ---------------------------------------------------------------------------------
// Called every frame ON THE RENDER THREAD (CopyResource is not free-threaded). Cheap when the
// overview is closed: it pulls a frame about twice a second, just often enough that the workspace
// you are about to leave has a fresh picture waiting.
static void OvCaptureTick(){
    if(!g_ovEnable || !Cap::Supported() || !g_komoLive.load()) return;
    ULONGLONG now=GetTickCount64();
    std::vector<KomoMon> snap; int focusedMon=0;
    { std::lock_guard<std::mutex> lk(g_komoMtx);
      if(g_komo.empty()) return;
      snap=g_komo; focusedMon=std::clamp(g_komoFocusedMon,0,(int)snap.size()-1); }

    for(int m=0;m<(int)snap.size() && m<WS2_MAXMON;m++){
        RECT mr=Ws2ResolveMon(snap[m].rect);
        HMONITOR hm=MonitorFromRect(&mr,MONITOR_DEFAULTTONEAREST);
        OvMonitor& om=g_ovMon[m];
        if(om.hmon!=hm){                                   // first sight, or the screens changed
            Cap::Stop(om.cap);
            if(om.liveSrv){ om.liveSrv->Release(); om.liveSrv=nullptr; }
            if(om.liveTex){ om.liveTex->Release(); om.liveTex=nullptr; }
            om.lw=om.lh=0; om.hmon=hm; om.tried=false;
        }
        om.rect=mr;
        if(!om.cap.started){
            // One attempt per monitor per layout: a machine that refuses WGC (policy, a remote
            // session, a driver that will not) must not be asked sixty times a second.
            if(om.tried) continue;
            om.tried=true;
            if(!Cap::Start(om.cap,hm,g_dev)) continue;
        }
        // While the overview is up, every frame. Otherwise twice a second - enough to keep the
        // picture of the workspace you are on from going stale before you leave it.
        if(!g_ovShow && now-om.lastPull < 500) continue;
        // Mid-switch the screen is a half-minimized mess and worth nothing as a memory of either
        // workspace. (Our own layers are excluded from capture, so they are never the problem.)
        if(g_wsSlideBusy.load() || g_t2Busy.load()) continue;
        om.lastPull=now;
        if(!Cap::Latest(om.cap,g_dev,g_ctx,&om.liveTex,&om.liveSrv,om.lw,om.lh)) continue;

        // Remember this as the face of whichever workspace is currently showing here.
        if(!g_ovShow) {
            int ws=snap[m].focused;
            std::vector<OvWin> wins;
            if(ws>=0 && ws<(int)snap[m].ws.size())
                for(HWND h:snap[m].ws[ws].hwnds){
                    if(!IsWindow(h)||IsIconic(h)) continue;
                    RECT wr; if(!GetWindowRect(h,&wr)) continue;
                    if(!Ws2RectOnMon(wr,mr)) continue;
                    OvWin w; w.h=h;
                    w.r=RECT{wr.left-mr.left,wr.top-mr.top,wr.right-mr.left,wr.bottom-mr.top};
                    wchar_t t[256]={0}; GetWindowTextW(h,t,256); w.title=t;
                    wins.push_back(w);
                }
            std::lock_guard<std::mutex> lk(g_ovFaceMtx);
            OvFace* face=nullptr;
            for(auto& f:g_ovFaces) if(f.mon==m && f.ws==ws){ face=&f; break; }
            if(!face){ g_ovFaces.push_back(OvFace{}); face=&g_ovFaces.back(); face->mon=m; face->ws=ws; }
            // reuses the face's texture when the size is unchanged - no allocation per refresh
            if(!Cap::SnapshotInto(g_dev,g_ctx,om.liveTex,om.lw,om.lh,&face->tex2d,&face->tex,face->tw,face->th))
                continue;
            face->wins=std::move(wins); face->at=now;
            // A stored face is a full-screen texture - about 8 MB each. Keep the newest handful and
            // let the rest go rather than quietly growing a habit.
            while(g_ovFaces.size()>12){
                size_t oldest=0;
                for(size_t i=1;i<g_ovFaces.size();i++) if(g_ovFaces[i].at<g_ovFaces[oldest].at) oldest=i;
                if(g_ovFaces[oldest].tex) g_ovFaces[oldest].tex->Release();
                if(g_ovFaces[oldest].tex2d) g_ovFaces[oldest].tex2d->Release();
                g_ovFaces.erase(g_ovFaces.begin()+oldest);
            }
        }
    }
}

// ---- open / close ----------------------------------------------------------------------------
static void OvOpen(){
    if(!g_ovEnable || g_ovWant) return;
    // each refusal says why in errors.log - a Super+Tab that silently did nothing was undiagnosable
    if(!g_komoLive.load()){ AetherLog("overview: not opened - komorebi is not connected"); return; }
    RECT active{g_mx,g_my,g_mx+g_mw,g_my+g_mh};
    HMONITOR hm=MonitorFromRect(&active,MONITOR_DEFAULTTONEAREST);
    int km=-1;
    { std::lock_guard<std::mutex> lk(g_komoMtx);
      for(int m=0;m<(int)g_komo.size() && m<WS2_MAXMON;m++){
          RECT mr=Ws2ResolveMon(g_komo[m].rect);
          if(MonitorFromRect(&mr,MONITOR_DEFAULTTONEAREST)==hm){ km=m; g_ovHome=g_komo[m].focused; break; } } }
    if(km<0){ int nk=0; { std::lock_guard<std::mutex> lk(g_komoMtx); nk=(int)g_komo.size(); }
        AetherLog("overview: not opened - none of komorebi's %d monitor(s) is the active screen (%d,%d %dx%d)",nk,g_mx,g_my,g_mw,g_mh);
        return; }
    g_ovKomoMon=km;
    { int started=0, stored=0;
      for(int i=0;i<WS2_MAXMON;i++) if(g_ovMon[i].cap.started) started++;
      { std::lock_guard<std::mutex> lk(g_ovFaceMtx); stored=(int)g_ovFaces.size(); }
      WsTrace("overview: open on komorebi monitor %d (ws %d) - capture %s, %d session(s), %d stored face(s)",
                km,g_ovHome,Cap::Supported()?"supported":"UNAVAILABLE",started,stored); }
    g_ovWant=true; g_ovShow=true;
    g_ovPickWs=-1; g_ovPickWin=nullptr; g_ovPickT=0.0f;
    g_ovZoom=1.0f; g_ovDragging=false; g_ovDragged=false;
    g_ovYaw=g_ovYawTo=0.0f;                       // 0 is always "the workspace you are on"
}
static void OvClose(){ g_ovWant=false; g_ovDragging=false; }

// Commit whatever is under the cursor / in front and fly into it.
static void OvPick(int wsRel,HWND win){
    g_ovPickWs=wsRel; g_ovPickWin=win; g_ovWant=false;
}

// ---- geometry --------------------------------------------------------------------------------
struct OvP3 { float x,y,z; };
// Invert the bilinear map of a projected quad, so a point on screen becomes a point on the face.
// Solved by bisection on v: for a candidate v the quad's two horizontal edges give a segment, and
// the point's distance from it is monotonic in v across a convex quad. Twenty steps is well under a
// pixel and costs nothing at this scale.
static bool OvInvQuad(ImVec2 p,ImVec2 a,ImVec2 b,ImVec2 c,ImVec2 d,float& u,float& v){
    auto cross=[](ImVec2 o,ImVec2 p1,ImVec2 p2){ return (p1.x-o.x)*(p2.y-o.y)-(p1.y-o.y)*(p2.x-o.x); };
    // reject points outside the (convex) quad a-b-c-d
    float s1=cross(a,b,p), s2=cross(b,c,p), s3=cross(c,d,p), s4=cross(d,a,p);
    bool allPos = s1>=0&&s2>=0&&s3>=0&&s4>=0, allNeg = s1<=0&&s2<=0&&s3<=0&&s4<=0;
    if(!allPos && !allNeg) return false;
    float lo=0.0f, hi=1.0f;
    for(int i=0;i<20;i++){
        float mid=(lo+hi)*0.5f;
        ImVec2 L{a.x+(d.x-a.x)*mid, a.y+(d.y-a.y)*mid};
        ImVec2 R{b.x+(c.x-b.x)*mid, b.y+(c.y-b.y)*mid};
        if(cross(L,R,p)>=0) lo=mid; else hi=mid;
    }
    v=(lo+hi)*0.5f;
    ImVec2 L{a.x+(d.x-a.x)*v, a.y+(d.y-a.y)*v};
    ImVec2 R{b.x+(c.x-b.x)*v, b.y+(c.y-b.y)*v};
    float dx=R.x-L.x, dy=R.y-L.y;
    float len=dx*dx+dy*dy;
    u = len>0.0001f ? std::clamp(((p.x-L.x)*dx+(p.y-L.y)*dy)/len, 0.0f, 1.0f) : 0.0f;
    return true;
}

// ---- draw ------------------------------------------------------------------------------------
static void DrawOverview(){
    ImGuiIO& io=ImGui::GetIO();
    const float W=io.DisplaySize.x, H=io.DisplaySize.y;
    ImDrawList* dl=ImGui::GetBackgroundDrawList();

    // The opening and closing curve. This used to be Approach() - an exponential smoother, which has
    // no duration, no curve and no idea that a key was pressed: it simply eased toward the target at
    // a fixed rate, so the overview could not be given the springy Caelestia feel, could not be
    // retimed, and could not speed up when the key was hit again. It is a graph like everything else
    // now (motion.overview_style / motion.key_overview_style), which also means hammering the
    // overview key compresses the animation instead of fighting it.
    g_ovAnim = MotionAnim(MP_OVERVIEW, 870001, g_ovWant? 1.0f : 0.0f);
    // Ticked unconditionally, with the CONDITION as the target. Only animating it while a pick
    // was live would leave the animator's stored target at 1 after the pick ended, so the next
    // pick would find it already "there" and snap instead of zooming.
    g_ovPickT = MotionAnim(MP_OVERVIEW, 870002, g_ovPickWs>=0 ? 1.0f : 0.0f);
    if(!g_ovWant && g_ovAnim<0.004f){
        // gone: hand over to whatever was chosen, then let the layer go
        int ws=g_ovPickWs; HWND win=g_ovPickWin;
        g_ovShow=false; g_ovRect=RECT{0,0,0,0};
        g_ovPickWs=-1; g_ovPickWin=nullptr; g_ovPickT=0.0f; g_ovAnim=0.0f;
        if(ws>=0){
            if(ws!=g_ovHome) KomorebiFocus(g_ovKomoMon,ws);
            // komorebi needs a beat to restore the workspace before the window can take focus
            if(win && IsWindow(win)) std::thread([win]{ Sleep(220); if(IsWindow(win)) ActivateWindow(win); }).detach();
        }
        return;
    }
    g_ovRect=RECT{0,0,(LONG)W,(LONG)H};

    // ---- the faces of this monitor ----
    struct Face { int ws; ID3D11ShaderResourceView* tex; int tw,th; std::vector<OvWin> wins; std::string name; };
    std::vector<Face> faces;
    int cur=0;
    { std::lock_guard<std::mutex> lk(g_komoMtx);
      if(g_ovKomoMon<(int)g_komo.size()){
          const KomoMon& km=g_komo[g_ovKomoMon];
          cur=km.focused;
          for(int w=0;w<(int)km.ws.size();w++){
              Face f{}; f.ws=w; f.tex=nullptr; f.tw=f.th=0;
              f.name = km.ws[w].name.empty()? ("Workspace "+std::to_string(w+1)) : km.ws[w].name;
              faces.push_back(std::move(f));
          } } }
    if(faces.empty()){ AetherLog("overview: closed - komorebi reported no workspaces to show"); g_ovShow=false; g_ovWant=false; return; }
    { std::lock_guard<std::mutex> lk(g_ovFaceMtx);
      for(auto& f:faces)
          for(auto& sf:g_ovFaces)
              if(sf.mon==g_ovKomoMon && sf.ws==f.ws){ f.tex=sf.tex; f.tw=sf.tw; f.th=sf.th; f.wins=sf.wins; break; } }
    // the workspace showing right now is live, not a memory
    { OvMonitor& om=g_ovMon[g_ovKomoMon];
      if(om.liveSrv && om.lw>0)
          for(auto& f:faces) if(f.ws==cur){ f.tex=om.liveSrv; f.tw=om.lw; f.th=om.lh; break; } }

    const int N=(int)faces.size();
    // Face `cur` is the one at yaw 0, so "straight ahead" always means "where you already are", and
    // the index is SIGNED: the workspace before this one is at -1, not at N-1. On the cube that is
    // only the shorter way round, but in a row it is the difference between the previous workspace
    // sitting just off the left edge and it sitting four screens off the right.
    auto rel=[&](int i)->int { int r=((i-cur)%N+N)%N; if(r>N/2) r-=N; return r; };

    // ---- camera ----
    const float FW=W, FH=H;
    const bool  cube = (g_ovStyle==0);
    const float R    = cube? (FW*0.5f)/tanf(3.14159265f/(float)std::max(3,N)) : 0.0f;
    const float FOCAL= FW*1.55f;
    const float GAP  = FW*0.12f;
    const float dOpen= cube? (R + FW*0.42f) : (FW*0.62f);
    // openness: 0 puts the front face exactly over the real screen, 1 is the overview
    float open = g_ovAnim;
    if(g_ovPickWs>=0) open = g_ovAnim*(1.0f-g_ovPickT);
    const float d = dOpen*std::clamp(g_ovZoom,0.35f,2.4f)*open;

    // yaw eases toward its target; picking drives it to the chosen face
    if(g_ovPickWs>=0){
        g_ovYawTo = cube? (float)rel(g_ovPickWs)*6.2831853f/(float)N : (float)rel(g_ovPickWs);
    }
    if(!g_ovDragging){
        Approach(g_ovYaw,g_ovYawTo,14.0f);
    }

    const float cx=W*0.5f, cy=H*0.5f;
    auto project=[&](OvP3 p)->ImVec2{
        float persp=FOCAL/std::max(1.0f,FOCAL+p.z);
        return V(cx+p.x*persp, cy+p.y*persp);
    };
    // Zooming into a chosen window: at the end of the flight the face lies exactly over the real
    // screen, so "fill the screen with that window" is a plain 2-D scale about its centre - which is
    // also why the hand-off is invisible, the last frame IS the screen.
    float zs=1.0f; ImVec2 zc{cx,cy};
    if(g_ovPickWin && g_ovPickT>0.001f){
        for(auto& f:faces) if(f.ws==g_ovPickWs)
            for(auto& w:f.wins) if(w.h==g_ovPickWin){
                float ww=(float)(w.r.right-w.r.left), wh=(float)(w.r.bottom-w.r.top);
                if(ww>8&&wh>8){
                    float s=std::min(W/ww,H/wh);
                    zs=1.0f+(s-1.0f)*g_ovPickT;
                    zc=V(cx+((w.r.left+w.r.right)*0.5f/(float)std::max(1,f.tw)-0.5f)*W*g_ovPickT,
                         cy+((w.r.top+w.r.bottom)*0.5f/(float)std::max(1,f.th)-0.5f)*H*g_ovPickT);
                }
            }
    }
    auto post=[&](ImVec2 p)->ImVec2{ return V(cx+(p.x-zc.x)*zs, cy+(p.y-zc.y)*zs); };

    // ---- backdrop ----
    // Dark enough to read the faces against, and it fades in with the pull-back so the first frame
    // of the animation still looks like your desktop.
    dl->AddRectFilled(V(0,0),V(W,H),IM_COL32(6,6,9,(int)(215*std::clamp(open,0.0f,1.0f))));

    // ---- lay the faces out, furthest first ----
    struct Placed { int idx; float z; OvP3 c[4]; };
    std::vector<Placed> order;
    for(int i=0;i<N;i++){
        int r=rel(i);
        OvP3 ctr; float ax,az;                    // centre and the face's local X axis
        if(cube){
            float phi=(float)r*6.2831853f/(float)N - g_ovYaw;
            float sp=sinf(phi), cp=cosf(phi);
            if(cp<=0.06f) continue;               // turned away from us
            ctr=OvP3{ R*sp, 0.0f, d + R - R*cp };
            ax=cp; az=sp;
        } else {
            float off=((float)r - g_ovYaw)*(FW+GAP);
            if(fabsf(off)>(FW+GAP)*2.6f) continue;   // far off the strip: not worth drawing
            ctr=OvP3{ off, 0.0f, d };
            ax=1.0f; az=0.0f;
        }
        Placed pl; pl.idx=i; pl.z=ctr.z;
        const float hx=FW*0.5f, hy=FH*0.5f;
        pl.c[0]=OvP3{ ctr.x-ax*hx, ctr.y-hy, ctr.z-az*hx };   // top-left
        pl.c[1]=OvP3{ ctr.x+ax*hx, ctr.y-hy, ctr.z+az*hx };   // top-right
        pl.c[2]=OvP3{ ctr.x+ax*hx, ctr.y+hy, ctr.z+az*hx };   // bottom-right
        pl.c[3]=OvP3{ ctr.x-ax*hx, ctr.y+hy, ctr.z-az*hx };   // bottom-left
        order.push_back(pl);
    }
    std::sort(order.begin(),order.end(),[](const Placed&a,const Placed&b){ return a.z>b.z; });
    // One line the first time a frame of a given openness is drawn. The overlay is marked
    // WDA_EXCLUDEFROMCAPTURE - it must be, or it would photograph itself - so no screenshot can ever
    // show it and this is the only way to check the scene from outside.
    { static float lastLogged=-1;
      if(open>0.9f && lastLogged<0.9f && !order.empty()){
          // the centre-most face, not order.back(): in plane mode every face shares a z, so the
          // depth sort leaves them in an arbitrary order and the last one can be off screen
          const Placed* pf=&order.back(); float best=1e9f;
          for(const Placed& q:order){ float cxq=fabsf(post(project(q.c[0])).x+post(project(q.c[1])).x-2*cx);
                                      if(cxq<best){ best=cxq; pf=&q; } }
          const Placed& fr=*pf;
          ImVec2 a0=post(project(fr.c[0])), b0=post(project(fr.c[1])), c0=post(project(fr.c[2]));
          WsTrace("overview: %d face(s), %d drawn, front ws=%d at %.0f,%.0f  %.0fx%.0f on a %.0fx%.0f screen (%s)",
                    N,(int)order.size(),faces[fr.idx].ws,a0.x,a0.y,b0.x-a0.x,c0.y-a0.y,W,H,cube?"cube":"plane");
      }
      lastLogged=open; }

    // ---- hit testing, on the faces as drawn ----
    int   hovWs=-1; HWND hovWin=nullptr; ImVec2 hovQuad[4];
    float hovU=0,hovV=0;
    bool  mouseIn = io.MousePos.x>-9000;
    for(int oi=(int)order.size()-1; oi>=0 && mouseIn && hovWs<0; oi--){   // nearest first
        const Placed& pl=order[oi];
        ImVec2 q[4]; for(int k=0;k<4;k++) q[k]=post(project(pl.c[k]));
        float u,v;
        if(OvInvQuad(io.MousePos,q[0],q[1],q[2],q[3],u,v)){
            hovWs=faces[pl.idx].ws; hovU=u; hovV=v;
            for(int k=0;k<4;k++) hovQuad[k]=q[k];
            const Face& f=faces[pl.idx];
            if(f.tw>0&&f.th>0){
                float px=u*(float)f.tw, py=v*(float)f.th;
                for(auto it=f.wins.rbegin(); it!=f.wins.rend(); ++it)
                    if(px>=it->r.left&&px<it->r.right&&py>=it->r.top&&py<it->r.bottom){ hovWin=it->h; break; }
            }
        }
    }

    // ---- draw ----
    const int GX=10, GY=7;                        // subdivision: affine texturing needs small cells
    for(const Placed& pl:order){
        const Face& f=faces[pl.idx];
        bool isCur = (f.ws==cur);
        // the grid of projected points
        ImVec2 pts[(GY+1)*(GX+1)];
        for(int gy=0;gy<=GY;gy++){
            float ty=(float)gy/GY;
            for(int gx=0;gx<=GX;gx++){
                float tx=(float)gx/GX;
                OvP3 top{ pl.c[0].x+(pl.c[1].x-pl.c[0].x)*tx, pl.c[0].y, pl.c[0].z+(pl.c[1].z-pl.c[0].z)*tx };
                OvP3 bot{ pl.c[3].x+(pl.c[2].x-pl.c[3].x)*tx, pl.c[3].y, pl.c[3].z+(pl.c[2].z-pl.c[3].z)*tx };
                OvP3 p{ top.x+(bot.x-top.x)*ty, top.y+(bot.y-top.y)*ty, top.z+(bot.z-top.z)*ty };
                pts[gy*(GX+1)+gx]=post(project(p));
            }
        }
        // a face turned away is dimmed, so the one you are aimed at reads as the front one
        float face=1.0f;
        if(cube){ float phi=(float)rel(pl.idx)*6.2831853f/(float)N - g_ovYaw; face=std::clamp(cosf(phi),0.0f,1.0f); }
        else     { float off=fabsf((float)rel(pl.idx)-g_ovYaw); face=std::clamp(1.0f-off*0.42f,0.25f,1.0f); }
        int tint=(int)(255*(0.42f+0.58f*face));
        ImU32 col=IM_COL32(tint,tint,tint,255);

        if(f.tex){
            for(int gy=0;gy<GY;gy++) for(int gx=0;gx<GX;gx++){
                ImVec2 a=pts[gy*(GX+1)+gx],     b=pts[gy*(GX+1)+gx+1];
                ImVec2 c=pts[(gy+1)*(GX+1)+gx+1], e=pts[(gy+1)*(GX+1)+gx];
                float u0=(float)gx/GX, u1=(float)(gx+1)/GX, v0=(float)gy/GY, v1=(float)(gy+1)/GY;
                dl->AddImageQuad((ImTextureID)f.tex,a,b,c,e,V(u0,v0),V(u1,v0),V(u1,v1),V(u0,v1),col);
            }
        } else {
            // never seen it: a plain slab with its name, so an empty workspace is still a place
            ImVec2 q[4]={pts[0],pts[GX],pts[(GY+1)*(GX+1)-1],pts[GY*(GX+1)]};
            dl->AddConvexPolyFilled(q,4,IM_COL32(20,20,26,(int)(235*face)));
        }
        // edges - a cube reads as a solid only once its seams are visible
        ImVec2 q[4]={pts[0],pts[GX],pts[(GY+1)*(GX+1)-1],pts[GY*(GX+1)]};
        ImU32 edge = isCur? WithA(COL_ACCD,(int)(220*face)) : IM_COL32(150,150,170,(int)(90*face));
        for(int k=0;k<4;k++) dl->AddLine(q[k],q[(k+1)%4],edge,isCur?2.2f:1.4f);

        // the hovered window, outlined on the face it is really on
        if(hovWin && f.ws==hovWs && f.tw>0){
            for(auto& w:f.wins) if(w.h==hovWin){
                float u0=(float)w.r.left/f.tw, u1=(float)w.r.right/f.tw;
                float v0=(float)w.r.top/f.th,  v1=(float)w.r.bottom/f.th;
                auto onFace=[&](float u,float v)->ImVec2{
                    OvP3 top{ pl.c[0].x+(pl.c[1].x-pl.c[0].x)*u, pl.c[0].y, pl.c[0].z+(pl.c[1].z-pl.c[0].z)*u };
                    OvP3 bot{ pl.c[3].x+(pl.c[2].x-pl.c[3].x)*u, pl.c[3].y, pl.c[3].z+(pl.c[2].z-pl.c[3].z)*u };
                    return post(project(OvP3{ top.x+(bot.x-top.x)*v, top.y+(bot.y-top.y)*v, top.z+(bot.z-top.z)*v }));
                };
                ImVec2 o[4]={onFace(u0,v0),onFace(u1,v0),onFace(u1,v1),onFace(u0,v1)};
                for(int k=0;k<4;k++) dl->AddLine(o[k],o[(k+1)%4],WithA(COL_ACCD,235),2.6f);
                break;
            }
        }
        // the label sits under the face, in screen space, so it never shears with the perspective
        if(open>0.35f && g_ovPickWs<0){
            ImVec2 mid=V((q[3].x+q[2].x)*0.5f,(q[3].y+q[2].y)*0.5f);
            std::string nm=f.name;
            float tw=TextW(g_fMed,17,nm.c_str());
            int   la=(int)(255*face*std::clamp((open-0.35f)/0.3f,0.0f,1.0f));
            dl->AddRectFilled(V(mid.x-tw*0.5f-10,mid.y+10),V(mid.x+tw*0.5f+10,mid.y+36),
                              IM_COL32(12,12,16,(int)(la*0.66f)),9.0f);
            TextAt(dl,g_fMed,17,V(mid.x-tw*0.5f,mid.y+15),WithA(isCur?COL_ACCD:COL_INK,la),nm.c_str());
        }
    }

    // ---- input ----
    if(g_ovPickWs>=0) return;                      // flying home: the mouse is no longer ours
    // wheel zooms; the cube can be pulled right back to see every face at once
    if(io.MouseWheel!=0.0f) g_ovZoom=std::clamp(g_ovZoom*(1.0f-io.MouseWheel*0.12f),0.35f,2.4f);

    if(io.MouseDown[0]){
        if(!g_ovDragging){ g_ovDragging=true; g_ovDragged=false; g_ovDragX=io.MousePos.x; g_ovDragYaw=g_ovYaw; }
        float dx=io.MousePos.x-g_ovDragX;
        if(fabsf(dx)>6.0f) g_ovDragged=true;
        // drag right = the faces come toward you from the right, like turning a real cube
        g_ovYaw = g_ovDragYaw - (cube? dx/(W*0.62f)*(6.2831853f/(float)N)*1.9f : dx/(W*0.55f));
        g_ovYawTo=g_ovYaw;
    } else if(g_ovDragging){
        g_ovDragging=false;
        if(g_ovDragged){
            // let go and it settles onto the nearest face
            float step = cube? 6.2831853f/(float)N : 1.0f;
            g_ovYawTo = roundf(g_ovYaw/step)*step;
        } else if(hovWs>=0){
            OvPick(hovWs,hovWin);                  // a click, not a drag: go there
        }
    }
    if(io.MouseClicked[1]) OvClose();              // right-click backs out

    // requests from the keyboard hook
    int req=g_ovReq.exchange(0);
    if(req==1) OvClose();
    else if(req==2 || req==3){
        float step = cube? 6.2831853f/(float)N : 1.0f;
        g_ovYawTo += (req==3? step : -step);
    }
    else if(req==4){
        // Enter takes the face you are aimed at
        float step = cube? 6.2831853f/(float)N : 1.0f;
        int r=((int)lroundf(g_ovYawTo/step))%N; if(r<0) r+=N;
        OvPick((cur+r)%N,nullptr);
    }

    // ---- a line of help, once it has settled ----
    if(open>0.6f){
        const char* tip = cube? "drag to spin  \xC2\xB7  scroll to zoom  \xC2\xB7  click a window to go there  \xC2\xB7  Esc to cancel"
                              : "drag to slide  \xC2\xB7  scroll to zoom  \xC2\xB7  click a window to go there  \xC2\xB7  Esc to cancel";
        float tw=TextW(g_fMed,15,tip);
        int la=(int)(190*std::clamp((open-0.6f)/0.3f,0.0f,1.0f));
        TextAt(dl,g_fMed,15,V(cx-tw*0.5f,H-46),WithA(COL_INK2,la),tip);
    }
}
