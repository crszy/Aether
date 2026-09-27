// src/modules/settings/SettingsFx.h  —  Aether shell
// Flourishes for the Settings window:
//   * the About card - "Aether", the version and "Made by Fora" in slowly cycling RGB, over a drifting nebula at
//     30% opacity (settings.about_card)
//   * the page transition - diagonal stripes sweep across the window, "Loading!!!!!!" bounces in the middle, and
//     the stripes sweep away again to show the new page (settings.page_transition / settings.transition_ms)
#pragma once
#include "src/SettingsIndex.h"
#include "src/components/VideoLoop.h"   // the real nebula footage

static const char* AETHER_VERSION = "1.6.1";
static const char* AETHER_AUTHOR  = "Fora";

static ImU32 FxHue(float h,float s,float v,int a){
    float r,g,b; ImGui::ColorConvertHSVtoRGB(fmodf(h+10.0f,1.0f),s,v,r,g,b);
    return IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),a);
}
// text whose colour flows through the rainbow, glyph by glyph
static float RgbText(ImDrawList* dl,ImFont* f,float sz,ImVec2 p,const char* t,float alpha,float phase,float step=0.045f){
    float x=p.x; int i=0; float tt=(float)ImGui::GetTime();
    for(const char* c=t; *c; ){
        int len=1; unsigned char u=(unsigned char)*c; if(u>=0xF0) len=4; else if(u>=0xE0) len=3; else if(u>=0xC0) len=2;
        std::string g(c,c+len);
        ImU32 col=FxHue(phase+tt*0.10f+i*step,0.55f,1.0f,(int)(255*std::clamp(alpha,0.0f,1.0f)));
        dl->AddText(f,sz*g_textScale,V(x,p.y),col,g.c_str());
        x+=f->CalcTextSizeA(sz*g_textScale,FLT_MAX,0,g.c_str()).x;
        c+=len; i++;
    }
    return x-p.x;
}
// the drawn fallback nebula: coloured gas clouds drifting on their own phases, a sprinkle of twinkling stars
static void DrawNebulaDrawn(ImDrawList* dl,ImVec2 a,ImVec2 b,float opacity){
    float t=(float)ImGui::GetTime();
    float W=b.x-a.x, H=b.y-a.y;
    dl->PushClipRect(a,b,true);
    dl->AddRectFilled(a,b,IM_COL32(8,6,24,(int)(255*opacity)));
    struct Cloud{ float x,y,r,sp,ph; float hue; };
    static const Cloud cl[]={ {0.20f,0.40f,0.55f,0.13f,0.0f,0.78f},{0.65f,0.30f,0.60f,0.10f,1.7f,0.62f},{0.45f,0.75f,0.50f,0.16f,3.1f,0.90f},
                              {0.85f,0.70f,0.45f,0.12f,4.4f,0.52f},{0.05f,0.85f,0.40f,0.09f,5.2f,0.70f},{0.55f,0.50f,0.35f,0.20f,2.3f,0.96f} };
    for(auto& c:cl){
        float cx=a.x+W*(c.x+0.06f*sinf(t*c.sp+c.ph)), cy=a.y+H*(c.y+0.10f*cosf(t*c.sp*0.8f+c.ph));
        float R=std::max(W,H)*c.r*(1.0f+0.06f*sinf(t*0.3f+c.ph));
        float hue=c.hue+0.04f*sinf(t*0.07f+c.ph);
        for(int k=7;k>=1;k--){ float rr=R*k/7.0f; dl->AddCircleFilled(V(cx,cy),rr,FxHue(hue,0.70f,0.95f,(int)(26*opacity)),40); }
    }
    for(int i=0;i<60;i++){
        float hx=fmodf(i*0.6180339f*7.1f,1.0f), hy=fmodf(i*0.4142136f*5.3f,1.0f);
        float tw=0.5f+0.5f*sinf(t*(1.3f+fmodf(i*0.37f,1.0f)*2.0f)+i);
        float r=(i%7==0? 1.6f : 1.0f);
        dl->AddCircleFilled(V(a.x+hx*W,a.y+hy*H),r,IM_COL32(255,255,255,(int)(255*opacity*(0.25f+0.75f*tw))),8);
    }
    dl->PopClipRect();
}
// The nebula is real footage now: a loop cut from NASA / STScI's Hubble "Cosmic Reef" flyby (assets/vfx/nebula.mp4,
// credits in assets/vfx/CREDITS.txt), decoded by VideoLoop.h. The drawn clouds only show while the first frame decodes
// (cross-faded away) or if the video cannot play on this PC.
static void DrawNebula(ImDrawList* dl,ImVec2 a,ImVec2 b,float opacity,float rnd){
    static float vidA=0;
    int vw=0, vh=0; ID3D11ShaderResourceView* t=VideoLoopTex(VfxPath("nebula.mp4"),vw,vh);
    if(t && vw>0) vidA=std::min(1.0f,vidA+g_frameDt*2.5f);
    if(!t || vidA<1.0f){
        dl->PushClipRect(V(a.x+rnd*0.3f,a.y+rnd*0.3f),V(b.x-rnd*0.3f,b.y-rnd*0.3f),true);
        DrawNebulaDrawn(dl,a,b,opacity*(1.0f-vidA)); dl->PopClipRect(); }
    if(t && vw>0){ ImVec2 uv0,uv1; CoverUV(vw,vh,b.x-a.x,b.y-a.y,uv0,uv1);
        dl->AddImageRounded((ImTextureID)t,a,b,uv0,uv1,IM_COL32(255,255,255,(int)(255*std::clamp(opacity*vidA,0.0f,1.0f))),rnd); }
}
// the About card; returns true when clicked
static bool DrawAboutCard(ImDrawList* dl,ImGuiIO& io,ImVec2 a,ImVec2 b,float e,bool click,bool big){
    float rnd=big? 22.0f : 18.0f;
    dl->AddRectFilled(a,b,MulA(Mix(COL_CARD,COL_CARD2,0.35f),e),rnd);
    DrawNebula(dl,a,b,0.30f*e,rnd);                                                              // 70% transparent
    dl->AddRect(a,b,FxHue((float)ImGui::GetTime()*0.08f,0.5f,1.0f,(int)(90*e)),rnd,0,1.4f);
    bool hov=io.MousePos.x>=a.x&&io.MousePos.x<b.x&&io.MousePos.y>=a.y&&io.MousePos.y<b.y;
    float H=b.y-a.y, fs=big? 44.0f : std::clamp(H*0.30f,18.0f,30.0f);
    float x=a.x+(big? 28.0f : 18.0f), y=a.y+(H-fs*1.9f)*0.5f;
    float w=RgbText(dl,g_fMed,fs,V(x,y),"Aether",e,0.0f,0.06f);
    char ver[32]; snprintf(ver,32,"v%s",AETHER_VERSION);
    float vf=fs*0.42f;
    ImVec2 vp=V(x+w+10,y+fs*0.52f);
    float vw=TextW(g_fSml,vf,ver)+14;
    dl->AddRectFilled(V(vp.x,vp.y-2),V(vp.x+vw,vp.y+vf+6),MulA(IM_COL32(255,255,255,40),e),(vf+8)*0.5f);
    TextAt(dl,g_fSml,vf,V(vp.x+7,vp.y),MulA(IM_COL32(255,255,255,230),e),ver);
    std::string by=std::string("Made by ")+AETHER_AUTHOR;
    RgbText(dl,g_fSml,fs*0.50f,V(x+2,y+fs*1.20f),by.c_str(),e,0.35f,0.03f);
    if(big){ const char* sub="A Caelestia-inspired shell for Windows";
        TextAt(dl,g_fSml,15,V(b.x-24-TextW(g_fSml,15,sub),b.y-30),MulA(IM_COL32(255,255,255,170),e),sub); }
    return hov&&click;
}

// ---- page transition ----
static ULONGLONG g_setWipeAt=0;
static void DrawStripeWipe(ImDrawList* dl,ImVec2 a,ImVec2 b,float rnd,float t,float alpha){
    if(t<=0.0f||t>=1.0f) return;
    const int N=9;
    float W=b.x-a.x, H=b.y-a.y, slant=H*0.45f;
    float sw=(W+slant)/N+2.0f;
    ImU32 cols[4]={ COL_GOLD, M3Secondary(), M3Tertiary(), Mix(COL_GOLD,COL_INK,0.30f) };
    dl->PushClipRect(a,b,true);
    for(int i=0;i<N;i++){
        float d=i*0.022f;
        float pin =Cael::eval(Cael::EMPHASIZED_DECEL,std::clamp((t-d)/0.30f,0.0f,1.0f));
        float pout=Cael::eval(Cael::EMPHASIZED_ACCEL,std::clamp((t-0.64f-d)/0.30f,0.0f,1.0f));
        float travel=W+slant*2.0f+sw;
        float off=-(1.0f-pin)*travel + pout*travel;
        float x0=a.x-slant+i*sw+off;
        ImVec2 q[4]={ V(x0,b.y), V(x0+sw,b.y), V(x0+sw+slant,a.y), V(x0+slant,a.y) };
        dl->AddConvexPolyFilled(q,4,MulA(WithA(cols[i%4],255),alpha));
        // a thin highlight edge on each stripe
        dl->AddLine(q[1],q[2],MulA(IM_COL32(255,255,255,70),alpha),2.0f);
    }
    // "Loading!!!!!!" while the stripes hold the screen
    float tx=std::clamp((t-0.24f)/0.12f,0.0f,1.0f)*(1.0f-std::clamp((t-0.64f)/0.10f,0.0f,1.0f));
    if(tx>0.01f){
        const char* msg="Loading!!!!!!";
        float fs=std::min(64.0f,W*0.07f);
        float total=TextW(g_fMed,fs,msg);
        float x=a.x+(W-total)*0.5f+fs*0.45f, cy=a.y+H*0.5f-fs*0.55f;
        float tt=(float)ImGui::GetTime();
        // a spinning M3 cookie in front of it
        M3Shape(dl,V(x-fs*0.75f,cy+fs*0.55f),fs*0.40f,MulA(M3OnPrimary(),tx*alpha),M3_COOKIE9,tt*3.0f);
        int i=0;
        for(const char* c=msg; *c; c++,i++){
            char g[2]={*c,0};
            float bounce=sinf(tt*9.0f-i*0.55f)*fs*0.10f;
            dl->AddText(g_fMed,fs*g_textScale,V(x+2,cy+bounce+3),MulA(IM_COL32(0,0,0,90),tx*alpha),g);
            dl->AddText(g_fMed,fs*g_textScale,V(x,cy+bounce),MulA(IM_COL32(255,255,255,255),tx*alpha),g);
            x+=TextW(g_fMed,fs,g);
        }
    }
    dl->PopClipRect();
}

// =================================================================================================== the Aether page
// Settings' first tab. A big card over the nebula footage (the name in flowing RGB, version, author), a guided tour
// for people who have just installed Aether, and "About this system" (the hardware / OS details), which opens in place.
static bool g_tourOn=false;            // the tour is running
static int  g_tourStep=0;
static bool g_sysInfoOpen=false;       // "About this system" expanded
// A guided tour, not a slideshow. Three kinds of step:
//   TK_INFO    - explained on the Aether page with an animated scene
//   TK_DO      - done FOR REAL: Settings tucks away, a coach card says what to do (and points at the right edge),
//                and the tour notices when you have done it and carries on by itself
//   TK_SETTING - Settings goes to the page that matters and highlights the option, with a small card over the page
enum { TK_INFO=0, TK_DO, TK_SETTING };
enum { TD_NONE=0, TD_BAR, TD_LAUNCHER, TD_DASHBOARD, TD_SIDEBAR, TD_WALLPICK, TD_CLIPBOARD };
enum { TE_CENTER=0, TE_LEFT, TE_RIGHT, TE_TOP, TE_BOTTOM };
struct TourStep { int kind; const char* icon; const char* title; std::string body; std::string doText; int doId; int edge; int keyHk; int page; const char* hilite; int illus; };
static std::string TourKey(int hk){ return (hk>=0 && hk<HK_COUNT && g_hk[hk].vk)? HotkeyName(g_hk[hk].mods&~MOD_NOREPEAT,g_hk[hk].vk) : std::string("(not set)"); }
static const char* TourEdgeName(int edge){ return edge==EDGE_LEFT? "left" : edge==EDGE_RIGHT? "right" : edge==EDGE_TOP? "top" : "bottom"; }
static int TourEdgeFrom(int edge){ return edge==EDGE_LEFT? TE_LEFT : edge==EDGE_RIGHT? TE_RIGHT : edge==EDGE_TOP? TE_TOP : TE_BOTTOM; }
static std::vector<TourStep>& TourSteps(){
    static std::vector<TourStep> v; v.clear();
    int barEdge=g_pn[PN_BAR].edge, dashEdge=g_pn[PN_DRAWER].edge;
    std::string L=TourKey(HK_LAUNCHER);
    v.push_back({TK_INFO,"waving_hand","Welcome to Aether",
        "Aether replaces the Windows taskbar and desktop with a Caelestia-style shell. This tour walks you through it for real: some steps ask you to try things, and it carries on by itself when you have. It takes about three minutes.","",TD_NONE,TE_CENTER,-1,-1,nullptr,0});
    v.push_back({TK_DO,"view_sidebar","The bar",
        "The bar holds your apps, workspaces, the tray, the clock and your status. It hides until you reach its screen edge.",
        std::string("Move your pointer all the way to the ")+TourEdgeName(barEdge)+" edge of the screen.",TD_BAR,TourEdgeFrom(barEdge),-1,-1,nullptr,1});
    v.push_back({TK_DO,"search","The launcher",
        "The launcher finds apps, open windows and settings as you type. Type > for commands, right-click an app to pin it to your favourites.",
        "Press "+L+" (or tap the Windows key), type a few letters, then press Esc to close it.",TD_LAUNCHER,TE_CENTER,HK_LAUNCHER,-1,nullptr,2});
    v.push_back({TK_DO,"dashboard","The dashboard",
        "The dashboard has your media, weather, performance, calendar, a terminal, and any widgets you add.",
        std::string("Push your pointer against the ")+TourEdgeName(dashEdge)+" edge of the screen.",TD_DASHBOARD,TourEdgeFrom(dashEdge),-1,-1,nullptr,3});
    v.push_back({TK_DO,"notifications","The sidebar",
        "The sidebar holds notifications, quick toggles like Wi-Fi and Do Not Disturb, the screen recorder and the audio mixer.",
        "Push your pointer against the right edge of the screen.",TD_SIDEBAR,TE_RIGHT,-1,-1,nullptr,4});
    v.push_back({TK_DO,"wallpaper","Wallpapers",
        "Pick a wallpaper (Wallpaper Engine ones too) and Aether's colours follow it. "+TourKey(HK_WALLPREV)+" and "+TourKey(HK_WALLNEXT)+" flip through them without opening anything.",
        "Press "+TourKey(HK_WALLPICK)+" to open the wallpaper picker, look around, then press Esc.",TD_WALLPICK,TE_CENTER,HK_WALLPICK,-1,nullptr,5});
    v.push_back({TK_DO,"content_paste","Clipboard history",
        "Everything you copy is remembered (in memory only, never saved to disk), so you can paste something from earlier.",
        "Copy any text, press "+TourKey(HK_CLIP)+" to see your history, then press Esc.",TD_CLIPBOARD,TE_CENTER,HK_CLIP,-1,nullptr,6});
    v.push_back({TK_INFO,"keyboard","Handy keys",
        TourKey(HK_SNIP)+" snips a screenshot. "+TourKey(HK_QUIT)+" closes Aether and gives Windows its own taskbar back - useful if anything ever goes wrong.","",TD_NONE,TE_CENTER,-1,-1,nullptr,6});
    v.push_back({TK_SETTING,"palette","Colours and theme",
        "Choose a colour scheme and light or dark mode, or let the colours follow your wallpaper.","",TD_NONE,TE_CENTER,-1,SP_APPEARANCE,"Color scheme",7});
    v.push_back({TK_SETTING,"wallpaper","Your wallpapers",
        "Point Aether at your own wallpaper folders, and choose how wallpapers switch.","",TD_NONE,TE_CENTER,-1,SP_WALLPAPER,"Scan folders recursively",7});
    v.push_back({TK_SETTING,"view_sidebar","The bar, your way",
        "Choose the bar's look and whether it hides until you reach its edge.","",TD_NONE,TE_CENTER,-1,SP_TASKBAR,"Bar style",7});
    v.push_back({TK_SETTING,"search","Launcher looks",
        "Make the launcher a list, a grid, a strip, a side column or a ring, and add shapes, glow, 3D and typing effects further down.","",TD_NONE,TE_CENTER,-1,SP_LAUNCHER,"Layout",7});
    v.push_back({TK_SETTING,"notifications","Sidebar and notifications",
        "Pick the sidebar style and how notifications pop up.","",TD_NONE,TE_CENTER,-1,SP_NOTIF,"Sidebar style",7});
    v.push_back({TK_SETTING,"partly_cloudy_day","Weather",
        "Weather is off until you choose where you are: type a city, or let Aether look up your approximate location from your internet connection.","",TD_NONE,TE_CENTER,-1,SP_DASHBOARD,"Weather location",7});
    v.push_back({TK_SETTING,"select_window","Window extras",
        "Optional extras for your app windows: Linux-style title bars, rounded corners and snapping. They are off until you turn them on.","",TD_NONE,TE_CENTER,-1,SP_WINDOWS,"Linux-style title bars",7});
    v.push_back({TK_SETTING,"grid_view","Tiling with komorebi",
        "If you use the komorebi tiling window manager, Aether can start it for you and slide smoothly between workspaces.","",TD_NONE,TE_CENTER,-1,SP_TASKBAR,"Start komorebi with the shell",7});
    v.push_back({TK_SETTING,"keyboard","Your shortcuts",
        "Click any shortcut to change it to whatever keys you like.","",TD_NONE,TE_CENTER,-1,SP_SHORTCUTS,"Shell shortcuts \xE2\x80\x94 click one to set it to whatever you like",7});
    v.push_back({TK_INFO,"celebration","You're all set",
        "That's the tour. Search at the top left of Settings finds any option by name. The Aether page (the card at the top) can run this tour again whenever you want.","",TD_NONE,TE_CENTER,-1,-1,nullptr,0});
    return v;
}
static int TourCount(){ return (int)TourSteps().size(); }
static TourStep TourGet(int i){ auto& v=TourSteps(); return v[std::clamp(i,0,(int)v.size()-1)]; }
static bool g_tourDo=false;                 // a TK_DO step is live: Settings is tucked away, the coach card is up
static ULONGLONG g_tourDoAt=0, g_tourDoneAt=0;
static bool g_tourSawOpen=false;            // the launcher-type steps: opened, and now waiting for it to close
static bool g_setHiliteHold=false;          // keep the tour's highlighted row pulsing
static RECT g_tourCoachRect={0,0,0,0};
static void TourEnd(){ g_tourOn=false; g_tourDo=false; g_setHiliteHold=false; g_tourStep=0; g_tourCoachRect=RECT{0,0,0,0}; }
// go to a step: pages, highlights and the Settings window follow the kind of step
static void TourGo(int i){
    int n=TourCount();
    if(i>=n){ TourEnd(); return; }
    g_tourStep=std::clamp(i,0,n-1);
    TourStep st=TourGet(g_tourStep);
    g_tourDoneAt=0; g_tourSawOpen=false;
    if(st.kind==TK_SETTING){
        g_tourDo=false; g_setShow=true;
        if(g_setPage!=st.page){ g_setPagePrev=g_setPage; g_setPage=st.page; g_setPageAnim=0; }
        g_setScroll=0; g_setScrollT=0;
        g_setHilite=st.hilite? st.hilite : ""; g_setHiliteAt=GetTickCount64(); g_setHiliteScrolled=false; g_setHiliteHold=true;
    } else {
        g_setHiliteHold=false;
        if(st.kind==TK_INFO || !g_tourDo){ g_tourDo=false; g_setShow=true; if(g_setPage!=SP_ABOUT){ g_setPagePrev=g_setPage; g_setPage=SP_ABOUT; g_setPageAnim=0; g_setScroll=0; g_setScrollT=0; } }
    }
}
static void TourStart(){ g_tourOn=true; TourGo(0); }
// a "try it" step starts: Settings tucks away and the coach card takes over
static void TourBeginDo(){ g_tourDo=true; g_tourDoAt=GetTickCount64(); g_tourDoneAt=0; g_tourSawOpen=false; g_setShow=false; g_setHiliteHold=false; }
static bool TourDoSatisfied(int id){
    // the edge steps: the pointer really has to touch that edge (a pinned bar is revealed before the step even starts),
    // or the panel has to go from hidden to shown while the step is up
    auto atEdge=[](int edge){ POINT p; if(!GetCursorPos(&p)) return false;
        MONITORINFO mi{sizeof(mi)}; if(!GetMonitorInfoW(MonitorFromPoint(p,MONITOR_DEFAULTTONEAREST),&mi)) return false;
        const RECT& r=mi.rcMonitor; const int m=6;
        return edge==TE_LEFT? p.x<r.left+m : edge==TE_RIGHT? p.x>=r.right-m : edge==TE_TOP? p.y<r.top+m : edge==TE_BOTTOM? p.y>=r.bottom-m : false; };
    auto shownFromHidden=[](float rv){ if(rv<0.3f) g_tourSawOpen=true; return g_tourSawOpen && rv>0.85f; };
    switch(id){
    case TD_BAR: { float rv=0; for(auto& b:g_bars) rv=std::max(rv,b.reveal); return atEdge(TourGet(g_tourStep).edge) || shownFromHidden(rv); }
    case TD_DASHBOARD: return atEdge(TourGet(g_tourStep).edge) || shownFromHidden(g_reveal);
    case TD_SIDEBAR:   return atEdge(TE_RIGHT) || shownFromHidden(g_sideReveal);
    case TD_LAUNCHER: case TD_WALLPICK: case TD_CLIPBOARD: {
        int wantMode = id==TD_WALLPICK? 2 : id==TD_CLIPBOARD? 4 : 0;
        bool open = g_launShow && (id==TD_LAUNCHER? (g_lmode==0||g_lmode==1) : g_lmode==wantMode);
        if(open) g_tourSawOpen=true;
        return g_tourSawOpen && !g_launShow; }
    }
    return false;
}
static bool TourCoachActive(){ return g_tourOn && g_tourDo; }

// a little arrow pointer for the illustrations
static void TourCursor(ImDrawList* dl,ImVec2 p,int al){
    ImVec2 pts[7]={ p, V(p.x,p.y+17), V(p.x+4.5f,p.y+13), V(p.x+8,p.y+20), V(p.x+10.5f,p.y+19), V(p.x+7,p.y+12), V(p.x+12,p.y+12) };
    dl->AddConvexPolyFilled(pts,7,IM_COL32(20,20,24,al)); dl->AddPolyline(pts,7,IM_COL32(255,255,255,al),ImDrawFlags_Closed,1.3f); }
static void TourKeycap(ImDrawList* dl,ImVec2 p,const char* lab,float press,int al){
    float w=TextW(g_fMed,14,lab)+22, h=30, dy=press*3.0f;
    dl->AddRectFilled(V(p.x,p.y+4),V(p.x+w,p.y+h+4),WithA(Mix(COL_CARD2,IM_COL32(0,0,0,255),0.5f),al),8);
    dl->AddRectFilled(V(p.x,p.y+dy),V(p.x+w,p.y+h+dy),WithA(Mix(COL_CARD2,COL_GOLD,0.10f+0.35f*press),al),8);
    TextAt(dl,g_fMed,14,V(p.x+11,p.y+6+dy),WithA(COL_INK,al),lab); }
static float TourEase(float x){ x=std::clamp(x,0.0f,1.0f); return x*x*(3.0f-2.0f*x); }
// the animated scene for each step: a small screen where the thing being explained happens on a loop
static void TourIllustration(ImDrawList* dl,int step,ImVec2 a,ImVec2 b,float t,int al){
    float W=b.x-a.x, H=b.y-a.y; const float R=16;
    dl->AddRectFilled(a,b,WithA(IM_COL32(10,10,18,255),al),R);
    DrawNebula(dl,a,b,0.55f*al/255.0f,R);
    dl->AddRectFilledMultiColor(V(a.x,a.y+H*0.45f),b,IM_COL32(0,0,0,0),IM_COL32(0,0,0,0),IM_COL32(0,0,0,(int)(120*al/255)),IM_COL32(0,0,0,(int)(120*al/255)));
    dl->PushClipRect(a,b,true);
    ImU32 panel=WithA(Mix(PanelCol(255),COL_CARD2,0.35f),(int)(235*al/255)), ink=WithA(COL_INK,al), ink2=WithA(COL_INK2,al), acc=WithA(COL_GOLD,al);
    float p=fmodf(t,4.0f)/4.0f;
    auto slot=[&](float from,float to){ return TourEase((p-from)/(to-from)); };
    switch(step){
    case 0: { ImVec2 c=V(a.x+W*0.5f,a.y+H*0.5f);
        for(int i=0;i<3;i++){ float k=fmodf(t*0.45f+i/3.0f,1.0f); dl->AddCircle(c,30+k*W*0.45f,FxHue(t*0.1f+i*0.2f,0.5f,1.0f,(int)(140*(1-k)*al/255)),64,2.0f); }
        float fs=40; float tw=TextW(g_fMed,fs,"Aether"); RgbText(dl,g_fMed,fs,V(c.x-tw*0.5f,c.y-fs*0.62f),"Aether",al/255.0f,0.0f,0.06f); } break;
    case 1: { float s=slot(0.05f,0.30f)*(1.0f-slot(0.88f,1.0f)); float bw=34, bx=a.x+8-(1-s)*50;
        dl->AddRectFilled(V(bx,a.y+8),V(bx+bw,b.y-8),panel,12);
        for(int i=0;i<6;i++){ float iy=a.y+26+i*30; ImU32 c=FxHue(0.08f*i+0.5f,0.45f,0.95f,al); dl->AddRectFilled(V(bx+7,iy),V(bx+27,iy+20),c,6);
            if(i==2) dl->AddRectFilled(V(bx+2,iy+4),V(bx+4,iy+16),acc,1); }
        TextAt(dl,g_fSml,11,V(bx+6,b.y-34),ink,"12:04");
        float mv=slot(0.35f,0.6f); ImVec2 cp=V(a.x+W*0.7f+(bx+17-(a.x+W*0.7f))*mv,a.y+H*0.7f+((a.y+26+60+10)-(a.y+H*0.7f))*mv);
        if(p>0.6f && p<0.85f){ float pk=slot(0.6f,0.85f); dl->AddRectFilled(V(bx+bw+8,a.y+76),V(bx+bw+8+110*TourEase(pk*2),a.y+76+64),panel,10); }
        TourCursor(dl,cp,al); } break;
    case 2: { float k1=slot(0.05f,0.12f)*(1-slot(0.16f,0.22f)), k2=slot(0.10f,0.16f)*(1-slot(0.20f,0.26f));
        TourKeycap(dl,V(a.x+W*0.5f-70,a.y+18),"Alt",k1,al); TourKeycap(dl,V(a.x+W*0.5f-8,a.y+18),"Space",k2,al);
        float rise=slot(0.22f,0.42f)*(1-slot(0.92f,1.0f)); float pw=W*0.66f, ph=H*0.52f, px=a.x+(W-pw)*0.5f, py=b.y-ph*rise-6;
        if(rise>0.01f){ dl->AddRectFilled(V(px,py),V(px+pw,py+ph+20),panel,14);
            const char* q="fire"; int n=std::clamp((int)((p-0.45f)/0.06f),0,4); std::string typed(q,n);
            dl->AddRectFilled(V(px+10,py+ph-34),V(px+pw-10,py+ph-8),WithA(Mix(COL_CARD2,COL_INK2,0.15f),al),13);
            TextAt(dl,g_fSml,13,V(px+26,py+ph-29),ink,typed.c_str());
            float cw=TextW(g_fSml,13,typed.c_str()); if(fmodf(t,1.0f)<0.55f) dl->AddRectFilled(V(px+27+cw,py+ph-28),V(px+28.5f+cw,py+ph-14),acc);
            int rows=std::min(3,n); for(int r=0;r<rows;r++){ float ry=py+10+r*30; if(r==0) dl->AddRectFilled(V(px+8,ry),V(px+pw-8,ry+26),WithA(Mix(COL_CARD2,COL_GOLD,0.25f),al),9);
                dl->AddRectFilled(V(px+16,ry+5),V(px+32,ry+21),FxHue(0.05f+r*0.3f,0.5f,1.0f,al),4); dl->AddRectFilled(V(px+42,ry+10),V(px+42+80-r*16,ry+16),ink2,3); } } } break;
    case 3: { float mv=slot(0.05f,0.3f); ImVec2 cp=V(a.x+W*0.5f,a.y+H*0.7f-(H*0.7f-4)*mv);
        float drop=slot(0.3f,0.5f)*(1-slot(0.9f,1.0f)); float pw=W*0.8f, ph=H*0.55f, px=a.x+(W-pw)*0.5f, py=a.y-ph+ph*drop+6;
        if(drop>0.01f){ dl->AddRectFilled(V(px,py-20),V(px+pw,py+ph),panel,14);
            float cw=(pw-40)/3; for(int i=0;i<3;i++){ float cx=px+10+i*(cw+10); dl->AddRectFilled(V(cx,py+12),V(cx+cw,py+ph-12),WithA(Mix(COL_CARD2,COL_INK2,0.12f),al),10);
                if(i==0){ dl->AddCircleFilled(V(cx+cw*0.5f,py+ph*0.45f),cw*0.25f,FxHue(t*0.05f,0.5f,1.0f,al),24); }
                if(i==1){ for(int k=0;k<5;k++){ float bh=(0.3f+0.6f*fabsf(sinf(t*2+k)))*(ph-50); dl->AddRectFilled(V(cx+10+k*((cw-20)/5),py+ph-20-bh),V(cx+8+(k+1)*((cw-20)/5),py+ph-20),acc,2); } }
                if(i==2){ TextAt(dl,g_fMed,20,V(cx+10,py+20),ink,"21\xC2\xB0"); TextAt(dl,g_fSml,11,V(cx+10,py+48),ink2,"Clear"); } } }
        TourCursor(dl,cp,al); } break;
    case 4: { float mv=slot(0.05f,0.3f); ImVec2 cp=V(a.x+W*0.4f+(W*0.6f-10)*mv,a.y+H*0.5f);
        float sl=slot(0.3f,0.5f)*(1-slot(0.9f,1.0f)); float pw=W*0.42f, px=b.x-pw*sl-6;
        if(sl>0.01f){ dl->AddRectFilled(V(px,a.y+6),V(px+pw+20,b.y-6),panel,14);
            for(int i=0;i<4;i++){ float tx=px+10+i*((pw-20)/4); dl->AddRectFilled(V(tx,a.y+16),V(tx+(pw-20)/4-6,a.y+44),WithA(i<2? Mix(COL_CARD2,COL_GOLD,0.4f) : Mix(COL_CARD2,COL_INK2,0.15f),al),10); }
            for(int i=0;i<3;i++){ float ny=a.y+56+i*44; float ap=slot(0.5f+i*0.08f,0.62f+i*0.08f); dl->AddRectFilled(V(px+10+(1-ap)*30,ny),V(px+pw-10+(1-ap)*30,ny+36),WithA(Mix(COL_CARD2,COL_INK2,0.1f),(int)(al*ap)),10);
                dl->AddCircleFilled(V(px+26+(1-ap)*30,ny+18),8,FxHue(0.6f+i*0.15f,0.5f,1.0f,(int)(al*ap)),16); dl->AddRectFilled(V(px+42+(1-ap)*30,ny+12),V(px+pw-30+(1-ap)*30,ny+18),WithA(COL_INK2,(int)(al*ap)),3); } }
        TourCursor(dl,cp,al); } break;
    case 5: { float sh=fmodf(t*0.5f,3.0f); int cur=(int)sh; float f=TourEase((sh-cur-0.6f)/0.4f);
        float cw=W*0.34f, ch=cw*0.56f, cy=a.y+H*0.5f;
        for(int d=-2;d<=2;d++){ float off=(d-f)*(cw+14); float k=1.0f-std::min(1.0f,fabsf(d-f)*0.35f);
            float ww=cw*(0.75f+0.25f*k), hh=ch*(0.75f+0.25f*k); float cx=a.x+W*0.5f+off;
            int idx=((cur+d)%3+3)%3; ImU32 c1=FxHue(0.55f+idx*0.25f,0.6f,0.9f,(int)(al*k)), c2=FxHue(0.75f+idx*0.25f,0.6f,0.5f,(int)(al*k));
            dl->AddRectFilledMultiColor(V(cx-ww*0.5f,cy-hh*0.5f),V(cx+ww*0.5f,cy+hh*0.5f),c1,c2,c2,c1);
            if(d==0) dl->AddRect(V(cx-ww*0.5f-3,cy-hh*0.5f-3),V(cx+ww*0.5f+3,cy+hh*0.5f+3),acc,6,0,2.0f); } } break;
    case 6: { const int KH[3]={HK_SNIP,HK_CLIP,HK_QUIT}; const char* lab[3]={"Screenshot","Clipboard","Quit Aether"};
        for(int i=0;i<3;i++){ float y=a.y+26+i*((H-40)/3); float on=slot(i*0.3f,i*0.3f+0.08f)*(1-slot(i*0.3f+0.22f,i*0.3f+0.3f));
            std::string k=TourKey(KH[i]); float x=a.x+20;
            size_t st=0; for(;;){ size_t pl=k.find('+',st); std::string part=k.substr(st,pl==std::string::npos? std::string::npos : pl-st);
                TourKeycap(dl,V(x,y),part.c_str(),on,al); x+=TextW(g_fMed,14,part.c_str())+30; if(pl==std::string::npos) break; st=pl+1; }
            TextAt(dl,g_fSml,13,V(x+6,y+7),WithA(on>0.3f? COL_GOLD : COL_INK2,al),lab[i]); } } break;
    case 7: { float sw=W*0.78f, sx=a.x+(W-sw)*0.5f, sy=a.y+24;
        dl->AddRectFilled(V(sx,sy),V(sx+sw,sy+36),panel,18); MsIcon(dl,"search",V(sx+20,sy+18),16,ink2);
        const char* q="glow"; int n=std::clamp((int)((p-0.05f)/0.07f),0,4); std::string typed(q,n);
        TextAt(dl,g_fSml,14,V(sx+36,sy+9),ink,typed.c_str());
        float rs=slot(0.4f,0.55f);
        for(int r=0;r<3;r++){ float ry=sy+50+r*40; float ap=std::clamp(rs*3-r,0.0f,1.0f); if(ap<=0) continue;
            ImU32 bg = r==0? WithA(Mix(COL_CARD2,COL_GOLD,0.18f+0.2f*(0.5f+0.5f*sinf(t*5))),(int)(al*ap)) : WithA(Mix(COL_CARD2,COL_INK2,0.1f),(int)(al*ap));
            dl->AddRectFilled(V(sx,ry),V(sx+sw,ry+32),bg,10);
            const char* tl[3]={"Glow  \xC2\xB7  Launcher","Glow colour  \xC2\xB7  Launcher","Glow strength  \xC2\xB7  Launcher"};
            TextAt(dl,g_fSml,13,V(sx+14,ry+8),WithA(COL_INK,(int)(al*ap)),tl[r]); } } break;
    }
    dl->PopClipRect();
    dl->AddRect(a,b,WithA(COL_INK2,(int)(40*al/255)),R,0,1.0f);
}
// shared little button (right-aligned by the caller walking bx leftwards)
static bool TourBtn(ImDrawList* dl,ImGuiIO& io,float& bx,float by,float bh,const char* icon,const char* lab,bool primary,int id,int al,bool click){
    float bw=TextW(g_fMed,14,lab)+(icon? 46 : 30); ImVec2 ba=V(bx-bw,by), bb=V(bx,by+bh);
    bool h=io.MousePos.x>=ba.x&&io.MousePos.x<bb.x&&io.MousePos.y>=ba.y&&io.MousePos.y<bb.y; float ha=HoverAnim(0x7F100+id,h);
    ImU32 bg= primary? Mix(COL_GOLD,IM_COL32(255,255,255,255),0.12f*ha) : Mix(Mix(COL_CARD2,COL_INK2,0.15f),COL_INK2,0.25f*ha);
    dl->AddRectFilled(ba,bb,WithA(bg,al),bh*0.5f);
    ImU32 fg= primary? M3OnPrimary() : COL_INK;
    float lx=ba.x+15; if(icon){ MsIcon(dl,icon,V(ba.x+20,by+bh*0.5f),18,WithA(fg,al)); lx=ba.x+34; }
    TextAt(dl,g_fMed,14,V(lx,by+8),WithA(fg,al),lab);
    bx-=bw+8; return h&&click; }
// the tour card on the Aether page; returns the height it used
static float DrawTour(ImDrawList* dl,ImGuiIO& io,float x0,float y0,float w,float e,bool click){
    const float H=312;
    int al=(int)(255*e);
    static int shownStep=-1, prevStep=0; static float swT=1; static int dir=1; static double stepAt=0;
    int n=TourCount();
    if(shownStep!=g_tourStep){ if(shownStep>=0){ prevStep=shownStep; dir= g_tourStep>shownStep? 1 : -1; swT=0; } shownStep=g_tourStep; stepAt=ImGui::GetTime(); }
    swT=std::min(1.0f,swT+g_frameDt*3.2f);
    float k=Cael::eval(Cael::EMPHASIZED_DECEL,swT);
    ImVec2 A=V(x0-10,y0), B=V(x0+w,y0+H);
    dl->AddRectFilled(A,B,WithA(Mix(COL_CARD,COL_CARD2,0.45f),al),22);
    dl->AddRect(A,B,FxHue((float)ImGui::GetTime()*0.06f,0.45f,1.0f,(int)(70*e)),22,0,1.2f);
    float ilW=std::min(w*0.44f,380.0f);
    ImVec2 ia=V(A.x+16,A.y+16), ib=V(ia.x+ilW,B.y-58);
    float tx=ib.x+26, tw=B.x-18-tx;
    auto content=[&](int st,float alpha,float dx){
        if(alpha<=0.01f) return;
        int a2=(int)(al*alpha);
        int v0=dl->VtxBuffer.Size;
        TourStep s2=TourGet(st);
        TourIllustration(dl,s2.illus,ia,ib,(float)(ImGui::GetTime()-(st==g_tourStep? stepAt : 0.0)),a2);
        char cnt[48]; snprintf(cnt,48,"Step %d of %d%s",st+1,n,s2.kind==TK_DO? "  \xC2\xB7  try it" : "");
        TextAt(dl,g_fSml,13,V(tx,ia.y+4),WithA(COL_GOLD,a2),cnt);
        MsIcon(dl,s2.icon,V(tx+14,ia.y+44),26,WithA(COL_INK,a2));
        TextAt(dl,g_fBig,26,V(tx+38,ia.y+28),WithA(COL_INK,a2),Clip(g_fBig,26,s2.title,tw-40).c_str());
        std::vector<std::string> lines; WrapLines(g_fSml,15,s2.body,tw,5,lines);
        float ly=ia.y+76; for(auto& l:lines){ TextAt(dl,g_fSml,15,V(tx,ly),WithA(COL_INK2,a2),l.c_str()); ly+=22; }
        if(s2.kind==TK_DO){ ly+=6; MsIcon(dl,"touch_app",V(tx+10,ly+10),18,WithA(COL_GOLD,a2));
            std::vector<std::string> dlines; WrapLines(g_fMed,15,s2.doText,tw-28,3,dlines);
            for(auto& l:dlines){ TextAt(dl,g_fMed,15,V(tx+28,ly),WithA(COL_INK,a2),l.c_str()); ly+=22; } }
        LfxMoveVerts(dl,v0,dx,0);
    };
    dl->PushClipRect(V(A.x+2,A.y+2),V(B.x-2,B.y-52),true);
    if(k<1.0f) content(prevStep,1.0f-k,-dir*50.0f*k);
    content(g_tourStep,k,dir*50.0f*(1.0f-k));
    dl->PopClipRect();
    // progress dots
    float dx=ia.x, dy=B.y-30;
    for(int i=0;i<n;i++){ float on=HoverAnim(0x7F000+i,i==g_tourStep); float dw=6+16*on;
        ImVec2 da=V(dx,dy-4), db=V(dx+dw,dy+4); bool hv=io.MousePos.x>=da.x-3&&io.MousePos.x<db.x+3&&io.MousePos.y>=da.y-6&&io.MousePos.y<db.y+6;
        dl->AddRectFilled(da,db,WithA(i<=g_tourStep? Mix(COL_INK2,COL_GOLD,0.4f+0.6f*on) : Mix(COL_CARD2,COL_INK2,0.35f),al),4);
        if(hv&&click) TourGo(i);
        dx+=dw+5; }
    float bx=B.x-18, by=B.y-46, bh=34;
    TourStep st=TourGet(g_tourStep);
    bool last=g_tourStep>=n-1;
    if(st.kind==TK_DO){
        if(TourBtn(dl,io,bx,by,bh,"play_arrow","Try it",true,1,al,click)) TourBeginDo();
        if(TourBtn(dl,io,bx,by,bh,nullptr,"Skip this",false,4,al,click)) TourGo(g_tourStep+1);
    } else if(st.kind==TK_SETTING){
        if(TourBtn(dl,io,bx,by,bh,"arrow_forward","Show me",true,1,al,click)) TourGo(g_tourStep);
    } else if(TourBtn(dl,io,bx,by,bh,last? "check" : "arrow_forward",last? "Finish" : "Next",true,1,al,click)) TourGo(g_tourStep+1);
    if(g_tourStep>0 && TourBtn(dl,io,bx,by,bh,"arrow_back","Back",false,2,al,click)) TourGo(g_tourStep-1);
    { const char* sk="Skip tour"; float sw=TextW(g_fSml,13,sk); ImVec2 sa=V(B.x-18-sw,A.y+16);
      bool h=io.MousePos.x>=sa.x-6&&io.MousePos.x<sa.x+sw+6&&io.MousePos.y>=sa.y-4&&io.MousePos.y<sa.y+20;
      TextAt(dl,g_fSml,13,sa,WithA(h? COL_INK : COL_INK2,al),sk);
      if(h&&click) TourEnd(); }
    return H;
}
// ---- the small card over any other Settings page ----
static bool TourMiniRect(ImVec2 pa,ImVec2 pb,ImVec2& a,ImVec2& b){
    if(!g_tourOn || g_tourDo || g_setPage==SP_ABOUT) return false;
    const float W=400, H=196;
    b=V(pb.x-24,pb.y-22); a=V(b.x-W,b.y-H); return true; }
static bool TourMiniContains(ImVec2 p,ImVec2 pa,ImVec2 pb){ ImVec2 a,b; return TourMiniRect(pa,pb,a,b) && p.x>=a.x&&p.x<b.x&&p.y>=a.y&&p.y<b.y; }
static void DrawTourMini(ImDrawList* dl,ImGuiIO& io,ImVec2 pa,ImVec2 pb,float e,bool click){
    ImVec2 a,b; if(!TourMiniRect(pa,pb,a,b)) return;
    static int seen=-1; static float in=0;
    if(seen!=g_tourStep){ seen=g_tourStep; in=0; }
    in=std::min(1.0f,in+g_frameDt*4.0f); float k=Cael::eval(Cael::EMPHASIZED_DECEL,in);
    int al=(int)(255*e*k);
    int v0=dl->VtxBuffer.Size;
    for(int i=6;i>0;i--) dl->AddRectFilled(V(a.x-i,a.y-i+5),V(b.x+i,b.y+i+6),IM_COL32(0,0,0,(int)(10*al/255)),20+i);
    dl->AddRectFilled(a,b,WithA(Mix(COL_CARD,COL_CARD2,0.6f),al),20);
    dl->AddRect(a,b,WithA(COL_GOLD,(int)(160*al/255)),20,0,1.6f);
    TourStep st=TourGet(g_tourStep); int n=TourCount();
    char cnt[48]; snprintf(cnt,48,"Tour  \xC2\xB7  step %d of %d",g_tourStep+1,n);
    TextAt(dl,g_fSml,12.5f,V(a.x+18,a.y+14),WithA(COL_GOLD,al),cnt);
    MsIcon(dl,st.icon,V(a.x+30,a.y+50),22,WithA(COL_INK,al));
    TextAt(dl,g_fMed,19,V(a.x+50,a.y+38),WithA(COL_INK,al),Clip(g_fMed,19,st.title,b.x-a.x-70).c_str());
    std::string body = st.kind==TK_SETTING? st.body+"  The highlighted option is the one to look at." : "You are part-way through the tour.";
    std::vector<std::string> lines; WrapLines(g_fSml,14,body,b.x-a.x-36,4,lines);
    float ly=a.y+70; for(auto& l:lines){ TextAt(dl,g_fSml,14,V(a.x+18,ly),WithA(COL_INK2,al),l.c_str()); ly+=20; }
    float bx=b.x-16, by=b.y-46, bh=32;
    bool last=g_tourStep>=n-1;
    if(st.kind==TK_SETTING){
        if(TourBtn(dl,io,bx,by,bh,last? "check" : "arrow_forward",last? "Finish" : "Next",true,11,al,click)) TourGo(g_tourStep+1);
        if(TourBtn(dl,io,bx,by,bh,"arrow_back","Back",false,12,al,click)) TourGo(g_tourStep-1);
    } else if(TourBtn(dl,io,bx,by,bh,"undo","Back to the tour",true,13,al,click)) TourGo(g_tourStep);
    { const char* sk="End tour"; float sw=TextW(g_fSml,12.5f,sk); ImVec2 sa=V(b.x-18-sw,a.y+14);
      bool h=io.MousePos.x>=sa.x-6&&io.MousePos.x<sa.x+sw+6&&io.MousePos.y>=sa.y-4&&io.MousePos.y<sa.y+18;
      TextAt(dl,g_fSml,12.5f,sa,WithA(h? COL_INK : COL_INK2,al),sk);
      if(h&&click) TourEnd(); }
    ScaleVerts(dl,v0,V((a.x+b.x)*0.5f,b.y),0.92f+0.08f*k);
}
// ---- the coach card for "try it" steps, drawn in the bar window (it is up while Settings is away) ----
static void DrawTourCoach(ImDrawList* dl,ImGuiIO& io){
    g_tourCoachRect=RECT{0,0,0,0};
    if(!TourCoachActive()) return;
    TourStep st=TourGet(g_tourStep);
    if(st.kind!=TK_DO){ g_tourDo=false; return; }
    ULONGLONG now=GetTickCount64();
    const float S=1.0f/std::max(0.5f,g_uiScale);
    // the screen under the pointer
    POINT cp; GetCursorPos(&cp); int mi=g_mons.empty()? 0 : MonIndexAt(cp);
    const RECT& mr = g_mons.empty()? RECT{0,0,(LONG)(io.DisplaySize.x*g_uiScale),(LONG)(io.DisplaySize.y*g_uiScale)} : g_mons[mi].rc;
    float L=(mr.left-g_vs.left)/g_uiScale, T=(mr.top-g_vs.top)/g_uiScale, R=(mr.right-g_vs.left)/g_uiScale, B=(mr.bottom-g_vs.top)/g_uiScale;
    ImVec2 mp=ImVec2((cp.x-g_vs.left)/g_uiScale,(cp.y-g_vs.top)/g_uiScale);
    bool click=io.MouseClicked[0];
    // done yet?
    if(!g_tourDoneAt && now-g_tourDoAt>350 && TourDoSatisfied(st.doId)) g_tourDoneAt=now;
    if(g_tourDoneAt && now-g_tourDoneAt>1300){
        int nx=g_tourStep+1;
        if(nx<TourCount() && TourGet(nx).kind==TK_DO){ g_tourStep=nx; g_tourDoAt=now; g_tourDoneAt=0; g_tourSawOpen=false; }
        else { g_tourDo=false; TourGo(nx); }
        return;
    }
    const float CW=460*S, CH=(st.keyHk>=0? 214 : 172)*S, gapE=110*S;
    float cx0, cy0;
    switch(st.edge){
    case TE_LEFT:   cx0=L+gapE;          cy0=(T+B)*0.5f-CH*0.5f; break;
    case TE_RIGHT:  cx0=R-gapE-CW;       cy0=(T+B)*0.5f-CH*0.5f; break;
    case TE_TOP:    cx0=(L+R)*0.5f-CW*0.5f; cy0=T+gapE+20*S;     break;
    case TE_BOTTOM: cx0=(L+R)*0.5f-CW*0.5f; cy0=B-gapE-CH-20*S;  break;
    default:        cx0=(L+R)*0.5f-CW*0.5f; cy0=B-CH-150*S;       break;
    }
    static int seen=-1; static ULONGLONG inAt=0;
    if(seen!=g_tourStep){ seen=g_tourStep; inAt=now; }
    float in=Cael::eval(Cael::EMPHASIZED_DECEL,std::clamp((now-inAt)/420.0f,0.0f,1.0f));
    int al=(int)(255*in);
    int v0=dl->VtxBuffer.Size;
    ImVec2 A=V(cx0,cy0), Bc=V(cx0+CW,cy0+CH);
    for(int i=7;i>0;i--) dl->AddRectFilled(V(A.x-i,A.y-i+6),V(Bc.x+i,Bc.y+i+7),IM_COL32(0,0,0,(int)(11*al/255)),24*S+i);
    dl->AddRectFilled(A,Bc,WithA(Mix(PanelCol(255),COL_CARD2,0.5f),al),24*S);
    dl->AddRect(A,Bc,FxHue((float)ImGui::GetTime()*0.1f,0.5f,1.0f,(int)(200*al/255)),24*S,0,1.8f*S);
    int n=TourCount();
    char cnt[48]; snprintf(cnt,48,"Tour  \xC2\xB7  step %d of %d  \xC2\xB7  try it",g_tourStep+1,n);
    TextAt(dl,g_fSml,13*S,V(A.x+22*S,A.y+16*S),WithA(COL_GOLD,al),cnt);
    float done = g_tourDoneAt? std::clamp((now-g_tourDoneAt)/450.0f,0.0f,1.0f) : 0.0f;
    // icon: the step's own, morphing into a tick when it is done
    ImVec2 ic=V(A.x+44*S,A.y+68*S);
    float pulse=0.5f+0.5f*sinf((float)ImGui::GetTime()*3.0f);
    dl->AddCircleFilled(ic,(22+3*pulse*(1-done))*S,WithA(Mix(Mix(COL_CARD2,COL_GOLD,0.35f),IM_COL32(110,200,130,255),done),al),32);
    if(done<0.5f) MsIcon(dl,st.icon,ic,24*S,WithA(COL_INK,(int)(al*(1-done*2))));
    else          MsIcon(dl,"check",ic,26*S*(0.6f+0.4f*EaseOutBack(done)),WithA(IM_COL32(20,40,24,255),al));
    float tx=A.x+80*S, tw=Bc.x-22*S-tx;
    TextAt(dl,g_fMed,20*S,V(tx,A.y+44*S),WithA(COL_INK,al),done>0? "Nice, that's it!" : st.title);
    std::vector<std::string> lines; WrapLines(g_fSml,15*S,st.doText,tw,3,lines);
    float ly=A.y+74*S; for(auto& l:lines){ TextAt(dl,g_fSml,15*S,V(tx,ly),WithA(done>0? COL_INK2 : COL_INK,al),l.c_str()); ly+=21*S; }
    if(st.keyHk>=0){                                                     // the keys, pressing themselves on a loop
        std::string k=TourKey(st.keyHk); float kx=tx, ky=ly+8*S; float ph=fmodf((float)ImGui::GetTime(),2.0f);
        size_t pos=0; int idx=0;
        for(;;){ size_t pl=k.find('+',pos); std::string part=k.substr(pos,pl==std::string::npos? std::string::npos : pl-pos);
            float press = (ph>0.3f+idx*0.12f && ph<0.9f)? 1.0f : 0.0f;
            TourKeycap(dl,V(kx,ky),part.c_str(),press,al); kx+=TextW(g_fMed,14,part.c_str())+30;
            if(pl==std::string::npos) break; TextAt(dl,g_fSml,14,V(kx-6,ky+6),WithA(COL_INK2,al),"+"); kx+=8; pos=pl+1; idx++; }
    }
    // Skip / End
    float bx=Bc.x-18*S, by=Bc.y-40*S;
    auto link=[&](const char* lab,int id)->bool{ float w=TextW(g_fSml,14*S,lab); ImVec2 la=V(bx-w,by), lb=V(bx,by+22*S);
        bool h=mp.x>=la.x-8&&mp.x<lb.x+8&&mp.y>=la.y-6&&mp.y<lb.y+6; float ha=HoverAnim(0x7F300+id,h);
        if(ha>0.01f) dl->AddRectFilled(V(la.x-10,la.y-5),V(lb.x+10,lb.y+5),WithA(COL_INK2,(int)(40*ha*al/255)),12);
        TextAt(dl,g_fSml,14*S,la,WithA(h? COL_INK : COL_INK2,al),lab); bx-=w+28*S; return h&&click; };
    if(!g_tourDoneAt){
        if(link("End tour",1)){ TourEnd(); g_setShow=false; return; }
        if(link("Skip this step",2)){ g_tourDoneAt=now-1300; }
        if(link("Show Settings",3)){ g_tourDo=false; g_setShow=true; }
    }
    // pointing at the edge to reach
    float bob=(0.5f+0.5f*sinf((float)ImGui::GetTime()*5.0f))*14*S;
    ImU32 ac=WithA(COL_GOLD,(int)(al*(1-done)));
    auto arrow=[&](ImVec2 tip,ImVec2 dir){ ImVec2 n2=V(-dir.y,dir.x); float Ls=36*S, Wd=16*S;
        ImVec2 base=V(tip.x-dir.x*Ls,tip.y-dir.y*Ls);
        dl->AddTriangleFilled(tip,V(base.x+n2.x*Wd,base.y+n2.y*Wd),V(base.x-n2.x*Wd,base.y-n2.y*Wd),ac);
        dl->AddRectFilled(V(std::min(base.x,base.x-dir.x*30*S)-(dir.x==0?5*S:0),std::min(base.y,base.y-dir.y*30*S)-(dir.y==0?5*S:0)),
                          V(std::max(base.x,base.x-dir.x*30*S)+(dir.x==0?5*S:0),std::max(base.y,base.y-dir.y*30*S)+(dir.y==0?5*S:0)),ac,3*S); };
    LONG rl=(LONG)A.x, rt=(LONG)A.y, rr=(LONG)Bc.x, rb=(LONG)Bc.y;
    switch(st.edge){
    case TE_LEFT:  arrow(V(A.x-26*S-bob,(A.y+Bc.y)*0.5f),V(-1,0)); rl=(LONG)(A.x-100*S); break;
    case TE_RIGHT: arrow(V(Bc.x+26*S+bob,(A.y+Bc.y)*0.5f),V(1,0)); rr=(LONG)(Bc.x+100*S); break;
    case TE_TOP:   arrow(V((A.x+Bc.x)*0.5f,A.y-26*S-bob),V(0,-1)); rt=(LONG)(A.y-100*S); break;
    case TE_BOTTOM:arrow(V((A.x+Bc.x)*0.5f,Bc.y+26*S+bob),V(0,1)); rb=(LONG)(Bc.y+100*S); break;
    }
    ScaleVerts(dl,v0,V((A.x+Bc.x)*0.5f,(A.y+Bc.y)*0.5f),0.9f+0.1f*in);
    g_tourCoachRect=RECT{rl-10,rt-10,rr+10,rb+14};
}
// the page's hero card; returns 1 when "Take the tour" is clicked, 2 for "About this system"
static int DrawAetherHero(ImDrawList* dl,ImGuiIO& io,ImVec2 a,ImVec2 b,float e,bool click){
    const float rnd=26; int al=(int)(255*e); int hit=0;
    dl->AddRectFilled(a,b,WithA(IM_COL32(8,8,16,255),al),rnd);
    DrawNebula(dl,a,b,0.80f*e,rnd);
    float H=b.y-a.y;
    dl->AddRectFilledMultiColor(a,V(a.x+(b.x-a.x)*0.62f,b.y),IM_COL32(0,0,0,(int)(150*e)),IM_COL32(0,0,0,0),IM_COL32(0,0,0,0),IM_COL32(0,0,0,(int)(150*e)));
    dl->AddRect(a,b,FxHue((float)ImGui::GetTime()*0.08f,0.5f,1.0f,(int)(110*e)),rnd,0,1.6f);
    float x=a.x+34, fs=64, y=a.y+H*0.5f-fs*0.95f;
    float w=RgbText(dl,g_fMed,fs,V(x,y),"Aether",e,0.0f,0.06f);
    char ver[32]; snprintf(ver,32,"v%s",AETHER_VERSION);
    float vw=TextW(g_fSml,15,ver)+18; ImVec2 vp=V(x+w+14,y+fs*0.5f);
    dl->AddRectFilled(V(vp.x,vp.y-3),V(vp.x+vw,vp.y+23),IM_COL32(255,255,255,(int)(48*e)),13);
    TextAt(dl,g_fSml,15,V(vp.x+9,vp.y),IM_COL32(255,255,255,(int)(235*e)),ver);
    std::string by=std::string("Made by ")+AETHER_AUTHOR;
    RgbText(dl,g_fSml,22,V(x+3,y+fs*1.18f),by.c_str(),e,0.35f,0.03f);
    TextAt(dl,g_fSml,14,V(x+3,y+fs*1.18f+32),IM_COL32(255,255,255,(int)(175*e)),"A Caelestia-inspired shell for Windows");
    // buttons
    float bx=a.x+34, by2=b.y-58, bh=38;
    auto btn=[&](const char* icon,const char* lab,bool primary,int id)->bool{
        float bw=TextW(g_fMed,15,lab)+50; ImVec2 ba=V(bx,by2), bb=V(bx+bw,by2+bh);
        bool h=io.MousePos.x>=ba.x&&io.MousePos.x<bb.x&&io.MousePos.y>=ba.y&&io.MousePos.y<bb.y; float ha=HoverAnim(0x7F200+id,h);
        float sc=1.0f+0.04f*ha; ImVec2 c=V((ba.x+bb.x)*0.5f,(ba.y+bb.y)*0.5f);
        int v0=dl->VtxBuffer.Size;
        dl->AddRectFilled(ba,bb,primary? WithA(Mix(COL_GOLD,IM_COL32(255,255,255,255),0.15f*ha),al) : IM_COL32(255,255,255,(int)((36+30*ha)*e)),bh*0.5f);
        ImU32 fg= primary? WithA(M3OnPrimary(),al) : IM_COL32(255,255,255,al);
        MsIcon(dl,icon,V(ba.x+22,c.y),19,fg); TextAt(dl,g_fMed,15,V(ba.x+38,by2+9),fg,lab);
        ScaleVerts(dl,v0,c,sc);
        bx+=bw+10; return h&&click; };
    if(btn(g_tourOn? "replay" : "school",g_tourOn? "Restart the tour" : "Take the tour",true,1)) hit=1;
    if(btn(g_sysInfoOpen? "expand_less" : "info","About this system",false,2)) hit=2;
    TextAt(dl,g_fSml,11,V(b.x-18-TextW(g_fSml,11,"Nebula footage: NASA / STScI"),b.y-22),IM_COL32(255,255,255,(int)(120*e)),"Nebula footage: NASA / STScI");
    return hit;
}
