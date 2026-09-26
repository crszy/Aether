// src/modules/bar/BarV2.h  —  Aether shell
// Caelestia's newer horizontal bar (reference\v2\NOTES.md §1): logo, a pill of workspace dots with a sliding
// target-ring, the active window; then on the right a tray expander, a date pill, a status pill (volume,
// keyboard layout, power profile, notifications) and the power button. The status icons open popouts that
// drop out of the bar: keyboard layouts and power profiles. Switching layouts shows a toast.
// bar.style = "caelestia" picks it (top or bottom edge); the item on/off switches in Settings > Taskbar apply.
#pragma once

// ============================================================================================ system bits
// ---- keyboard layouts ----
struct V2Kbd { HKL hkl=nullptr; std::string code, name; };
static std::vector<V2Kbd> V2KbdList(){
    std::vector<V2Kbd> out; HKL l[32]; int n=GetKeyboardLayoutList(32,l);
    for(int i=0;i<n;i++){ V2Kbd k; k.hkl=l[i]; LANGID lid=LOWORD((UINT_PTR)l[i]);
        wchar_t cc[16]={0}, ln[128]={0}; LCID lc=MAKELCID(lid,SORT_DEFAULT);
        GetLocaleInfoW(lc,LOCALE_SISO3166CTRYNAME,cc,16); GetLocaleInfoW(lc,LOCALE_SLOCALIZEDDISPLAYNAME,ln,128);
        std::string c=W2U8(cc); for(auto& ch:c) ch=(char)tolower((unsigned char)ch);
        k.code=c.empty()? "??" : c; k.name=W2U8(ln); out.push_back(k); }
    return out;
}
static HKL V2KbdCurrent(){ HWND fg=GetForegroundWindow(); DWORD tid=fg? GetWindowThreadProcessId(fg,nullptr) : 0; return GetKeyboardLayout(tid); }
static void V2KbdSet(HKL h){
    HWND fg=GetForegroundWindow();
    if(fg) PostMessageW(fg,WM_INPUTLANGCHANGEREQUEST,0,(LPARAM)h);
    ActivateKeyboardLayout(h,KLF_SETFORPROCESS);
}
// ---- power mode (Windows 10/11 overlay schemes, the "Power mode" dropdown) ----
// not in every SDK's powrprof.lib, so resolved at run time
static DWORD PowerGetEffectiveOverlayScheme(GUID* g){ using F=DWORD(WINAPI*)(GUID*); static F f=(F)GetProcAddress(LoadLibraryW(L"powrprof.dll"),"PowerGetEffectiveOverlayScheme"); return f? f(g) : 1; }
static DWORD PowerSetActiveOverlayScheme(GUID* g){ using F=DWORD(WINAPI*)(GUID*); static F f=(F)GetProcAddress(LoadLibraryW(L"powrprof.dll"),"PowerSetActiveOverlayScheme"); return f? f(g) : 1; }
static const GUID V2_PWR_SAVER = {0x961cc777,0x2547,0x4f9d,{0x81,0x74,0x7d,0x86,0x18,0x1b,0x8a,0x7a}};
static const GUID V2_PWR_BAL   = {0x00000000,0x0000,0x0000,{0,0,0,0,0,0,0,0}};
static const GUID V2_PWR_PERF  = {0xded574b5,0x45a0,0x4f42,{0x87,0x37,0x46,0x34,0x5c,0x09,0xc2,0x38}};
// PowerGetEffectiveOverlayScheme is an RPC to the power service and takes 230-460 ms on a real machine. Asked on the
// render thread every 2 s it froze the whole shell for a quarter of a second every 2 s (stall.txt: "bar_layer 230 ms"),
// so a worker keeps the answer fresh and the bar only ever reads a cached int.
static std::atomic<int> g_v2PwrMode{1};
static std::atomic<bool> g_v2PwrRun{false};
static int V2PowerMode(){                       // 0 saver, 1 balanced, 2 performance
    if(!g_v2PwrRun.exchange(true)) std::thread([]{
        for(;;){ GUID g{}; if(PowerGetEffectiveOverlayScheme(&g)==0) g_v2PwrMode = IsEqualGUID(g,V2_PWR_SAVER)? 0 : IsEqualGUID(g,V2_PWR_PERF)? 2 : 1;
                 Sleep(3000); } }).detach();
    return g_v2PwrMode.load();
}
static void V2PowerSet(int m){ g_v2PwrMode=m; std::thread([m]{ GUID g = m==0? V2_PWR_SAVER : m==2? V2_PWR_PERF : V2_PWR_BAL; PowerSetActiveOverlayScheme(&g); }).detach(); }

// ============================================================================================ state
static RECT  g_barPopRect={0,0,0,0};            // popout + layout toast: kept in the bar window's region
static int   g_v2Pop=0;                         // 0 none, 1 keyboard layouts, 2 power profile
static float g_v2PopAnim=0, g_v2PopX=0;
static int   g_v2PopMon=-1;
static bool  g_v2TrayOpen=false;
static std::string g_v2KbdToast; static ULONGLONG g_v2KbdToastAt=0;

static void LaunToggle();   // fwd
// the caelestia bar has its own component switches (bar.caelestia.*), like the reference Settings > Taskbar > Components
static bool BarItemOn(int id){
    switch(id){ case BIT_LOGO: return g_bv2Logo; case BIT_WORKSPACES: return g_bv2Ws; case BIT_WINDOWINFO: return g_bv2Win;
                case BIT_TRAY: return g_bv2Tray; case BIT_CLOCK: return g_bv2Clock; case BIT_AUDIO: case BIT_NOTIF: return g_bv2Status;
                case BIT_POWER: return g_bv2Power; case BIT_APPS: return g_bv2Apps; }
    return true; }

// ============================================================================================ running apps
// The open apps on this bar's monitor, in a pill: icon, a dot under the ones running (a wider accent bar on the
// focused one), click = focus / minimise / cycle, right-click = the app menu, hover = the live preview.
static void V2BarApps(ImDrawList* dl,ImGuiIO& io,int mi,int HID,float& pos,float cross,float ph,bool vertical,bool edgeFar,float barEdge){
    static std::vector<DockApp*> mine; mine.clear();
    { HMONITOR barMon=MonitorFromPoint(POINT{MonRect(mi).left+2,MonRect(mi).top+2},MONITOR_DEFAULTTONEAREST);
      for(auto& a:g_dockApps) if(a.running && (!g_barSameMonitor || a.mon==barMon)) mine.push_back(&a); }
    if(mine.empty()) return;
    float isz=ph*0.66f, step=ph*0.92f;
    float len=mine.size()*step+ph*0.18f;
    ImU32 pill=Mix(PanelCol(255),COL_CARD2,0.55f);
    if(vertical) dl->AddRectFilled(V(cross-ph*0.5f,pos),V(cross+ph*0.5f,pos+len),pill,ph*0.5f);
    else         dl->AddRectFilled(V(pos,cross-ph*0.5f),V(pos+len,cross+ph*0.5f),pill,ph*0.5f);
    bool click=io.MouseClicked[0], rclick=io.MouseClicked[1];
    bool anyThumb=false;
    for(size_t k=0;k<mine.size();k++){
        DockApp& a=*mine[k];
        float along=pos+ph*0.09f+step*(k+0.5f);
        ImVec2 c = vertical? V(cross,along) : V(along,cross);
        bool hov=fabsf(io.MousePos.x-c.x)<step*0.5f && fabsf(io.MousePos.y-c.y)<ph*0.5f;
        float ha=HoverAnim(HID+400+(int)k,hov);
        if(ha>0.01f) dl->AddCircleFilled(c,ph*0.46f,WithA(COL_INK2,(int)(46*ha)),24);
        float s2=isz*(1.0f+0.08f*ha);
        if(a.icon) dl->AddImage((ImTextureID)a.icon,V(c.x-s2*0.5f,c.y-s2*0.5f),V(c.x+s2*0.5f,c.y+s2*0.5f),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,a.minimized? 170 : 255));
        else MsIcon(dl,"apps",c,s2,COL_INK);
        bool active=DockGroupHasFg(a);
        float d=ph*0.47f;
        if(vertical){ float x= edgeFar? c.x-d : c.x+d; float hl= active? ph*0.30f : ph*0.08f;
            dl->AddRectFilled(V(x-1.4f,c.y-hl),V(x+1.4f,c.y+hl),active? COL_GOLD : WithA(COL_INK2,a.minimized? 110 : 180),2); }
        else { float y= edgeFar? c.y-d : c.y+d; float hl= active? ph*0.30f : ph*0.08f;
            dl->AddRectFilled(V(c.x-hl,y-1.4f),V(c.x+hl,y+1.4f),active? COL_GOLD : WithA(COL_INK2,a.minimized? 110 : 180),2); }
        if(hov && !g_appMenu){
            if(g_barPreviews){
                if(vertical) ShowThumb(a.hwnd,a.title.empty()?"(untitled)":a.title,(int)(g_vs.left+barEdge*g_uiScale)+(edgeFar? -THUMBW-8 : 8),(int)(g_vs.top+c.y*g_uiScale)-THUMBH/2);
                else ShowThumb(a.hwnd,a.title.empty()?"(untitled)":a.title,(int)(g_vs.left+c.x*g_uiScale)-THUMBW/2,edgeFar? (int)(g_vs.top+barEdge*g_uiScale)-THUMBH-8 : (int)(g_vs.top+barEdge*g_uiScale)+8);
                anyThumb=true; } }
        if(click&&hov){ DockGroupClick(a); HideThumb(); anyThumb=false; }
        if(rclick&&hov){ HideThumb(); anyThumb=false;
            if(vertical) OpenAppMenu(a,mi,V(barEdge+(edgeFar? -6.0f : 6.0f),c.y),edgeFar? -1 : 1);
            else OpenAppMenu(a,mi,V(c.x,barEdge+(edgeFar? -6.0f : 6.0f)),1,edgeFar? -1 : 0); }
    }
    if(anyThumb) g_thumbWanted=true;
    pos+=len+(vertical? 14.0f : 18.0f);
}

// ============================================================================================ shared: popouts + toast
// dir: 0 drops down from a top bar, 1 rises from a bottom bar, 2 opens right of a left bar, 3 opens left of a right bar
static void V2BarPopout(ImDrawList* dl,ImGuiIO& io,int mi,int HID,float along,float edge,int dir,
                        float bx,float by,float barW,float bh,bool click){
    if(g_v2PopMon!=mi) return;
    float want = g_v2Pop? 1.0f : 0.0f;
    g_v2PopAnim += (want-g_v2PopAnim)*std::min(1.0f,g_frameDt*14.0f);
    if(fabsf(want-g_v2PopAnim)<0.004f) g_v2PopAnim=want;
    static int shownPop=1; if(g_v2Pop) shownPop=g_v2Pop;
    if(g_v2PopAnim<=0.004f){ if(!g_v2Pop) g_barPopRect=RECT{0,0,0,0}; return; }
    float e=Cael::eval(Cael::DEFAULT_SPATIAL,std::clamp(g_v2PopAnim,0.0f,1.0f));
    std::vector<V2Kbd> kl = shownPop==1? V2KbdList() : std::vector<V2Kbd>();
    float pw = shownPop==1? 300.0f : 250.0f;
    float phh = shownPop==1? 58.0f+kl.size()*40.0f : 150.0f;
    ImVec2 f0,f1;
    if(dir<=1){ float px=std::clamp(along-pw*0.5f,bx+8,std::max(bx+8,bx+barW-pw-8)); float py= dir==0? edge : edge-phh; f0=V(px,py); f1=V(px+pw,py+phh); }
    else      { float py=std::clamp(along-phh*0.5f,by+8,std::max(by+8,by+bh-phh-8)); float px= dir==2? edge : edge-pw; f0=V(px,py); f1=V(px+pw,py+phh); }
    ImVec2 pa=f0, pb=f1;
    switch(dir){ case 0: pb.y=f0.y+phh*e; break; case 1: pa.y=f1.y-phh*e; break; case 2: pb.x=f0.x+pw*e; break; default: pa.x=f1.x-pw*e; }
    ImDrawFlags fl = dir==0? ImDrawFlags_RoundCornersBottom : dir==1? ImDrawFlags_RoundCornersTop : dir==2? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersLeft;
    dl->AddRectFilled(pa,pb,PanelCol((int)(255*std::min(1.0f,g_v2PopAnim*1.5f))),g_panelRound,fl);
    g_barPopRect=RECT{(LONG)pa.x,(LONG)pa.y,(LONG)pb.x,(LONG)pb.y};
    dl->PushClipRect(pa,pb,true);
    int al=(int)(255*std::clamp(g_v2PopAnim*1.4f-0.3f,0.0f,1.0f));
    bool inside=io.MousePos.x>=pa.x&&io.MousePos.x<pb.x&&io.MousePos.y>=pa.y&&io.MousePos.y<pb.y;
    float left=f0.x, top=f0.y;
    if(shownPop==1){
        TextAt(dl,g_fMed,18,V(left+18,top+16),WithA(COL_INK,al),"Keyboard Layouts");
        HKL cur=V2KbdCurrent(); float ry=top+50;
        for(size_t k=0;k<kl.size();k++){
            ImVec2 ra=V(left+10,ry), rb=V(left+pw-10,ry+34);
            bool h=g_v2Pop==1 && io.MousePos.x>=ra.x&&io.MousePos.x<rb.x&&io.MousePos.y>=ra.y&&io.MousePos.y<rb.y;
            float ha=HoverAnim(HID+100+(int)k,h);
            if(kl[k].hkl==cur) dl->AddRectFilled(ra,rb,WithA(COL_CARD2,al),12);
            else if(ha>0.01f) dl->AddRectFilled(ra,rb,WithA(COL_INK2,(int)(36*ha*al/255)),12);
            std::string code=kl[k].code; for(auto& ch:code) ch=(char)toupper((unsigned char)ch);
            std::string line=code+" - "+kl[k].name;
            TextAt(dl,g_fSml,15,V(ra.x+10,ra.y+8),WithA(COL_INK,al),Clip(g_fSml,15,line,pw-40).c_str());
            if(h&&click) V2KbdSet(kl[k].hkl);
            ry+=40;
        }
    } else {
        std::string b1 = g_st.hasBattery? ("Battery "+std::to_string(g_st.battPct)+"%"+(g_st.charging? "  \xC2\xB7  Charging" : "")) : std::string("No battery detected");
        static const char* PN[]={"Power saver","Balanced","Performance"};
        int m=V2PowerMode();
        TextAt(dl,g_fSml,16,V(left+18,top+16),WithA(COL_INK,al),b1.c_str());
        std::string b2=std::string("Power profile: ")+PN[m];
        TextAt(dl,g_fSml,16,V(left+18,top+44),WithA(COL_INK,al),b2.c_str());
        static const char* IC[]={"energy_savings_leaf","balance","rocket_launch"};
        for(int k=0;k<3;k++){
            ImVec2 c=V(left+pw*0.5f+(k-1)*70.0f,top+106);
            bool h=g_v2Pop==2 && (io.MousePos.x-c.x)*(io.MousePos.x-c.x)+(io.MousePos.y-c.y)*(io.MousePos.y-c.y)<26*26;
            float ha=HoverAnim(HID+120+k,h);
            if(k==m) dl->AddCircleFilled(c,26,WithA(Mix(COL_INK,COL_GOLD,0.2f),al),28);
            else if(ha>0.01f) dl->AddCircleFilled(c,26,WithA(COL_INK2,(int)(44*ha*al/255)),28);
            MsIcon(dl,IC[k],c,28,WithA(k==m? M3OnPrimary() : COL_INK,al));
            if(h&&click) V2PowerSet(k);
        }
    }
    dl->PopClipRect();
    bool onBar=io.MousePos.x>=bx&&io.MousePos.x<bx+barW&&io.MousePos.y>=by&&io.MousePos.y<by+bh;
    if(click && !inside && !onBar) g_v2Pop=0;
}
static void V2KbdToastDraw(ImDrawList* dl,int mi){
    if(!g_barV2KbdToast || mi!=FrameMon()) return;
    static HKL last=nullptr; static ULONGLONG chk=0;
    if(GetTickCount64()-chk>250){ chk=GetTickCount64(); HKL now=V2KbdCurrent();
        if(last && now!=last){ for(auto& k:V2KbdList()) if(k.hkl==now) g_v2KbdToast=k.name; g_v2KbdToastAt=GetTickCount64(); }
        last=now; }
    ULONGLONG age=GetTickCount64()-g_v2KbdToastAt;
    if(!g_v2KbdToastAt || age>=3200) return;
    float t = age<250? age/250.0f : age>2800? (3200-age)/400.0f : 1.0f; t=std::clamp(t,0.0f,1.0f);
    float e=Cael::eval(Cael::DEFAULT_SPATIAL,t);
    const RECT& mr=MonRect(mi);
    float mw=(mr.right-mr.left)/g_uiScale, mh=(mr.bottom-mr.top)/g_uiScale, mx=(mr.left-g_vs.left)/g_uiScale, my=(mr.top-g_vs.top)/g_uiScale;
    const float S=1.0f/std::max(0.5f,g_uiScale);
    float tw=400*S, th=84*S;
    float tx=mx+mw-tw-24*S, ty=my+mh-th-24*S+(1.0f-e)*30.0f*S;
    int al=(int)(255*t);
    dl->AddRectFilled(V(tx,ty),V(tx+tw,ty+th),WithA(PanelCol(255),al),16*S);
    dl->AddRect(V(tx,ty),V(tx+tw,ty+th),WithA(COL_INK2,(int)(40*t)),16*S,0,1.0f);
    dl->AddRectFilled(V(tx+12*S,ty+12*S),V(tx+72*S,ty+72*S),WithA(COL_CARD2,al),12*S);
    MsIcon(dl,"keyboard",V(tx+42*S,ty+42*S),34*S,WithA(COL_INK,al));
    TextAt(dl,g_fMed,20*S,V(tx+86*S,ty+16*S),WithA(COL_INK,al),"Keyboard layout changed");
    std::string l2="Layout changed to: "+g_v2KbdToast;
    TextAt(dl,g_fSml,17*S,V(tx+86*S,ty+46*S),WithA(COL_INK2,al),Clip(g_fSml,17*S,l2,tw-100*S).c_str());
    RECT tr{(LONG)tx,(LONG)ty,(LONG)(tx+tw),(LONG)(ty+th)};
    if(g_barPopRect.right<=g_barPopRect.left) g_barPopRect=tr; else UnionRect(&g_barPopRect,&g_barPopRect,&tr);
}

// ============================================================================================ the vertical form
// The same components stacked down a left / right strip (the reference's "Position: Left"): logo, workspace
// dots, the window title turned on its side; tray expander, date pill, status pill and power at the bottom.
static void DrawBarVertV2(int mi,float bx,float by,float barW,float bh,bool onRight,float slide){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    const int HID=mi*20000+950000;
    bool click=io.MouseClicked[0];
    float cx=bx+barW*0.5f;
    float pw=std::clamp(barW-12.0f,24.0f,40.0f);             // pill width
    ImU32 pill=Mix(PanelCol(255),COL_CARD2,0.55f);
    float a=std::clamp(slide,0.0f,1.0f);
    auto A=[&](ImU32 c){ return MulA(c,a); };
    auto inY=[&](float y0,float y1){ return io.MousePos.y>=y0&&io.MousePos.y<y1&&io.MousePos.x>=bx&&io.MousePos.x<bx+barW; };
    const char* tip=nullptr; float tipY=0;
    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
    float popEdge = onRight? bx : bx+barW;

    float y=by+14;
    if(BarItemOn(BIT_LOGO)){
        float box=pw*0.80f; ImVec2 c=V(cx,y+box*0.5f);
        int lw=0,lh=0; bool hov=inY(y-4,y+box+4); float ha=HoverAnim(HID+1,hov);
        if(ha>0.01f) dl->AddCircleFilled(c,pw*0.5f,A(WithA(COL_INK2,(int)(40*ha))),24);
        if(BarLogoTex(lw,lh)) DrawLogoMark(dl,c,box,a); else MsIcon(dl,"blur_on",c,box,A(COL_INK));
        if(hov&&click) LaunToggle();
        y+=box+14;
    }
    if(BarItemOn(BIT_WORKSPACES)){
        const WsRing& WR=WsRingFor(mi);
        int n=std::clamp(WR.count,1,24);
        float step=pw*0.78f, ph=n*step+pw*0.30f;
        dl->AddRectFilled(V(cx-pw*0.5f,y),V(cx+pw*0.5f,y+ph),A(pill),pw*0.5f);
        float y0=y+pw*0.15f+step*0.5f;
        float cur=Cael::anim(HID+10,(float)std::clamp(WR.cur,0,n-1),Cael::DUR_DEFAULT_SPATIAL,Cael::DEFAULT_SPATIAL);
        for(int k=0;k<n;k++){
            ImVec2 c=V(cx,y0+k*step); bool occ=k<64 && WR.occ[k];
            bool hov=fabsf(io.MousePos.y-c.y)<step*0.5f && io.MousePos.x>=bx && io.MousePos.x<bx+barW;
            float ha=HoverAnim(HID+20+k,hov);
            float r=pw*0.24f;
            dl->AddCircle(c,r,A(WithA(COL_INK2,(int)(occ? 230 : 150+60*ha))),20,1.7f);
            if(occ) dl->AddCircleFilled(c,r*0.30f,A(WithA(COL_INK2,200)),12);
            if(hov&&click) GotoWorkspace(mi,k);
        }
        ImVec2 ac=V(cx,y0+cur*step); float R=pw*0.40f;
        dl->AddCircleFilled(ac,R,A(Mix(COL_INK,COL_GOLD,0.2f)),28);
        dl->AddCircle(ac,R*0.66f,A(pill),28,std::max(1.6f,R*0.16f));
        dl->AddCircleFilled(ac,R*0.40f,A(Mix(COL_INK,COL_GOLD,0.2f)),20);
        if(inY(y,y+ph) && io.MouseWheel!=0){ int t=std::clamp(WR.cur+(io.MouseWheel<0? 1 : -1),0,n-1); if(t!=WR.cur) GotoWorkspace(mi,t); }
        y+=ph+18;
    }
    if(BarItemOn(BIT_APPS)) V2BarApps(dl,io,mi,HID,y,cx,pw,true,onRight,onRight? bx : bx+barW);
    float titleTop=y;

    // bottom-up
    float ry=by+bh-14;
    // Your own items (configaritems.toml), just above the bottom cluster. This bar's layout is
    // written out by hand rather than driven by the item list, so they are placed rather than
    // ordered - turning one on in Settings is still what decides whether it appears.
    for(int ci=BAR_CUSTOM_N-1; ci>=0; ci--){
        if(!BarItemOn(BIT_CUSTOM1+ci) || !BarCustomOn(ci)) continue;
        float h=pw; ImVec2 c=V(cx,ry-h*0.5f); bool hov=inY(ry-h,ry);
        BarCustomDrawV(dl,ci,c.x,c.y,pw*0.42f,A(COL_INK),HoverAnim(HID+700+ci,hov),pw*0.5f);
        if(hov&&click) BarCustomClick(ci);
        if(hov){ const char* t=BarCustomTip(ci); if(t){ tip=t; tipY=c.y; } }
        ry-=h+6;
    }
    if(BarItemOn(BIT_POWER)){
        float h=pw; ImVec2 c=V(cx,ry-h*0.5f); bool hov=inY(ry-h,ry); float ha=HoverAnim(HID+40,hov);
        if(ha>0.01f) dl->AddCircleFilled(c,pw*0.5f,A(IM_COL32(232,120,110,(int)(50*ha))),24);
        MsIcon(dl,"power_settings_new",c,pw*0.62f,A(IM_COL32(232,130,120,255)));
        if(hov){ tip="Power"; tipY=c.y; } if(hov&&click) g_sessShow=true;
        ry-=h+10;
    }
    { struct SI{ int kind; float h; }; std::vector<SI> items; std::vector<V2Kbd> kl;
      if(BarItemOn(BIT_AUDIO)) items.push_back({0,pw*0.95f});
      if(g_barV2Kbd){ kl=V2KbdList(); if(!kl.empty()) items.push_back({1,pw*0.95f}); }
      if(g_barV2Power) items.push_back({2,pw*0.95f});
      if(BarItemOn(BIT_NOTIF)) items.push_back({3,pw*0.95f});
      if(!items.empty()){
          float h=pw*0.40f; for(auto& s:items) h+=s.h;
          float y0=ry-h;
          dl->AddRectFilled(V(cx-pw*0.5f,y0),V(cx+pw*0.5f,ry),A(pill),pw*0.5f);
          float iy=y0+pw*0.20f;
          int notifN; { std::lock_guard<std::mutex> lk(g_notifMtx); notifN=(int)g_notifs.size(); }
          HKL curK=V2KbdCurrent();
          for(auto& s:items){
              ImVec2 c=V(cx,iy+s.h*0.5f); bool hov=inY(iy,iy+s.h); float ha=HoverAnim(HID+50+s.kind,hov);
              if(ha>0.01f) dl->AddCircleFilled(c,pw*0.42f,A(WithA(COL_INK2,(int)(46*ha))),20);
              ImU32 ic=A(WithA(COL_INK,225));
              switch(s.kind){
              case 0: { if(g_volCache<0) g_volCache=GetVolume();
                  MsIcon(dl,g_volCache<=0.001f? "volume_off" : g_volCache<0.5f? "volume_down" : "volume_up",c,pw*0.58f,ic);
                  if(hov){ static char vt[24]; snprintf(vt,24,"Volume %d%%",(int)std::round(std::clamp(g_volCache,0.0f,1.0f)*100)); tip=vt; tipY=c.y;
                      if(io.MouseWheel!=0){ g_volCache=std::clamp(g_volCache+io.MouseWheel*0.05f,0.0f,1.0f); SetVolume(g_volCache); } }
                  if(hov&&click){ g_sideView=4; g_sideForceUntil=GetTickCount64()+4000; } } break;
              case 1: { std::string code="??"; for(auto& k:kl) if(k.hkl==curK) code=k.code;
                  float fs=pw*0.50f; TextAt(dl,g_fMed,fs,V(c.x-TextW(g_fMed,fs,code.c_str())*0.5f,c.y-fs*0.62f),ic,code.c_str());
                  if(hov&&click){ g_v2Pop = g_v2Pop==1? 0 : 1; g_v2PopX=c.y; g_v2PopMon=mi; }
                  if(hov && io.MouseClicked[1] && kl.size()>1){ size_t idx=0; for(size_t q=0;q<kl.size();q++) if(kl[q].hkl==curK) idx=q; V2KbdSet(kl[(idx+1)%kl.size()].hkl); } } break;
              case 2: { int m=V2PowerMode(); MsIcon(dl,m==0? "energy_savings_leaf" : m==2? "rocket_launch" : "balance",c,pw*0.58f,ic);
                  if(hov&&click){ g_v2Pop = g_v2Pop==2? 0 : 2; g_v2PopX=c.y; g_v2PopMon=mi; } } break;
              case 3: { MsIcon(dl,g_dnd? "notifications_off" : "notifications",c,pw*0.58f,ic);
                  if(notifN>0 && !g_dnd){ ImVec2 d=V(c.x+pw*0.18f,c.y-pw*0.18f); dl->AddCircleFilled(d,pw*0.12f,A(pill),12); dl->AddCircleFilled(d,pw*0.075f,A(COL_INK),12); }
                  if(hov){ tip=g_dnd? "Do not disturb (right-click to allow)" : "Notifications (right-click for DND)"; tipY=c.y; }
                  if(hov&&click) g_notifForceUntil = GetTickCount64()<g_notifForceUntil? 0 : GetTickCount64()+8000;
                  if(hov&&io.MouseClicked[1]){ g_dnd=!g_dnd; SaveConfig(); } } break;
              }
              iy+=s.h;
          }
          ry=y0-10;
      }
    }
    if(BarItemOn(BIT_CLOCK)){
        float fs=std::clamp(pw*0.48f,11.0f,17.0f);
        static const char* WD[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        int h12=g_clock24? lt.tm_hour : ((lt.tm_hour%12)==0? 12 : lt.tm_hour%12);
        char dd[8], hh[8], mm[8]; snprintf(dd,8,"%d",lt.tm_mday); snprintf(hh,8,"%02d",h12); snprintf(mm,8,"%02d",lt.tm_min);
        const char* ap=g_clock24? nullptr : (lt.tm_hour<12? "am" : "pm");
        float lh=fs*1.15f;
        float h=pw*0.35f+fs*1.3f+lh*2+fs*0.7f+lh*2+(ap? lh*0.9f : 0)+pw*0.30f;
        float y0=ry-h; bool hov=inY(y0,ry); float ha=HoverAnim(HID+60,hov);
        dl->AddRectFilled(V(cx-pw*0.5f,y0),V(cx+pw*0.5f,ry),A(Mix(pill,COL_INK2,0.12f*ha)),pw*0.5f);
        float iy=y0+pw*0.35f;
        auto line=[&](ImFont* f,float sz,const char* t){ TextAt(dl,f,sz,V(cx-TextW(f,sz,t)*0.5f,iy),A(COL_INK),t); iy+=sz*1.15f; };
        MsIcon(dl,"calendar_month",V(cx,iy+fs*0.55f),fs*1.15f,A(COL_INK)); iy+=fs*1.3f;
        line(g_fMed,fs,WD[lt.tm_wday]); line(g_fMed,fs,dd);
        dl->AddLine(V(cx-pw*0.28f,iy+fs*0.3f),V(cx+pw*0.28f,iy+fs*0.3f),A(WithA(COL_INK2,150)),1.2f); iy+=fs*0.7f;
        line(g_fMed,fs,hh); line(g_fMed,fs,mm);
        if(ap){ float s2=fs*0.8f; TextAt(dl,g_fSml,s2,V(cx-TextW(g_fSml,s2,ap)*0.5f,iy),A(COL_INK),ap); }
        if(hov&&click){ g_tab=0; g_drawerForceUntil=GetTickCount64()+4000; }
        ry=y0-10;
    }
    if(BarItemOn(BIT_TRAY)){
        std::vector<SysTrayIcon*> tray;
        { std::lock_guard<std::mutex> lk(g_systrayMtx); for(auto& s:g_systray) if(!s.hidden && s.tex) tray.push_back(&s); }
        float open=Cael::anim(HID+70,g_v2TrayOpen? 1.0f : 0.0f,Cael::DUR_DEFAULT_SPATIAL,Cael::DEFAULT_SPATIAL);
        float step=pw*0.9f;
        float h=pw+std::max(0.0f,open)*(tray.size()*step+pw*0.1f);
        float y0=ry-h;
        dl->AddRectFilled(V(cx-pw*0.5f,y0),V(cx+pw*0.5f,ry),A(pill),pw*0.5f);
        ImVec2 cc=V(cx,ry-pw*0.5f); bool ch=fabsf(io.MousePos.y-cc.y)<pw*0.5f && io.MousePos.x>=bx && io.MousePos.x<bx+barW;
        float hc=HoverAnim(HID+71,ch); if(hc>0.01f) dl->AddCircleFilled(cc,pw*0.46f,A(WithA(COL_INK2,(int)(46*hc))),20);
        MsIcon(dl,open>0.5f? "expand_more" : "expand_less",cc,pw*0.62f,A(COL_INK));
        if(ch&&click) g_v2TrayOpen=!g_v2TrayOpen;
        if(ch){ tip="System tray"; tipY=cc.y; }
        if(open>0.02f){
            dl->PushClipRect(V(bx,y0),V(bx+barW,ry-pw),true);
            for(size_t k=0;k<tray.size();k++){
                ImVec2 c=V(cx,ry-pw-step*(k+0.5f)-pw*0.05f);
                bool th=fabsf(io.MousePos.y-c.y)<step*0.5f && io.MousePos.x>=bx && io.MousePos.x<bx+barW && open>0.9f;
                float ha=HoverAnim(HID+80+(int)k,th);
                if(ha>0.01f) dl->AddCircleFilled(c,pw*0.44f,A(WithA(COL_INK2,(int)(46*ha))),20);
                float ts=pw*0.58f;
                dl->AddImage((ImTextureID)tray[k]->tex,V(c.x-ts*0.5f,c.y-ts*0.5f),V(c.x+ts*0.5f,c.y+ts*0.5f),ImVec2(0,0),ImVec2(1,1),A(IM_COL32(255,255,255,(int)(255*open))));
                if(th){ int sx=(int)(g_vs.left+popEdge*g_uiScale), sy=(int)(g_vs.top+c.y*g_uiScale);
                    if(click) TrayForward(*tray[k],WM_LBUTTONDOWN,WM_LBUTTONUP,sx,sy);
                    if(io.MouseClicked[1]) TrayForward(*tray[k],WM_RBUTTONDOWN,WM_RBUTTONUP,sx,sy);
                    if(!tray[k]->tip.empty()){ static std::string ts2; ts2=W2U8(tray[k]->tip); size_t nl=ts2.find('\n'); if(nl!=std::string::npos) ts2.resize(nl); tip=ts2.c_str(); tipY=c.y; } }
            }
            dl->PopClipRect();
        }
        ry=y0-10;
    }
    // the active window, turned on its side, in whatever room is left
    if(BarItemOn(BIT_WINDOWINFO) && ry-titleTop>80){
        std::string title;
        { HWND fgw=GetForegroundWindow(); wchar_t wt[256]={0}; DWORD pid=0;
          if(fgw){ GetWindowTextW(fgw,wt,255); GetWindowThreadProcessId(fgw,&pid); }
          if(fgw && pid!=GetCurrentProcessId() && wt[0]) title=W2U8(CleanTitle(wt)); }
        bool desk=title.empty(); if(desk) title="Desktop";
        float fs=std::clamp(pw*0.46f,12.0f,16.0f);
        float room=std::min(ry-titleTop-fs*2.5f,bh*0.34f);
        std::string t=Clip(g_fSml,fs,title,room);
        float tw=TextW(g_fSml,fs,t.c_str())+fs*1.6f;
        float y0=titleTop+((ry-titleTop)-tw)*0.5f;
        MsIcon(dl,desk? "desktop_windows" : "select_window",V(cx,y0+fs*0.55f),fs*1.15f,A(COL_INK));
        TextRot(dl,g_fSml,fs,V(cx+fs*0.55f,y0+fs*1.6f),A(COL_INK),t.c_str(),1.5707963f);
    }

    V2BarPopout(dl,io,mi,HID,g_v2PopX,popEdge,onRight? 3 : 2,bx,by,barW,bh,click);
    V2KbdToastDraw(dl,mi);
    if(tip){ static std::string lastTip; static ULONGLONG chg=0; if(lastTip!=tip){ lastTip=tip; chg=GetTickCount64(); }
        float fade=std::clamp((GetTickCount64()-chg)/140.0f,0.0f,1.0f);
        float w=TextW(g_fSml,14,tip)+22, h=28;
        float tx= onRight? bx-8-w : bx+barW+8, ty=tipY-h*0.5f;
        dl->AddRectFilled(V(tx,ty),V(tx+w,ty+h),IM_COL32(30,28,36,(int)(245*fade)),h*0.5f);
        TextAt(dl,g_fSml,14,V(tx+11,ty+6),IM_COL32(232,230,238,(int)(255*fade)),tip);
        g_barTipRect=RECT{(LONG)(tx-6),(LONG)(ty-6),(LONG)(tx+w+6),(LONG)(ty+h+6)}; }
}

// ============================================================================================ the bar
static void DrawBarHorizV2(int mi,float bx,float by,float barW,float bh,bool onTop,float slide){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    const int HID=mi*20000+900000;
    bool click=io.MouseClicked[0];
    float cy=by+bh*0.5f;
    float ph=std::clamp(bh-10.0f,22.0f,40.0f);             // pill height
    ImU32 pill=Mix(PanelCol(255),COL_CARD2,0.55f);
    float a=std::clamp(slide,0.0f,1.0f);
    auto A=[&](ImU32 c){ return MulA(c,a); };
    auto inR=[&](float x0,float x1){ return io.MousePos.x>=x0&&io.MousePos.x<x1&&io.MousePos.y>=by&&io.MousePos.y<by+bh; };
    const char* tip=nullptr; float tipX=0;
    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
    float popAnchorY = onTop? by+bh : by;

    // ---------------------------------------------------------------- left
    float x=bx+14;
    if(BarItemOn(BIT_LOGO)){
        float box=ph*0.80f; ImVec2 c=V(x+box*0.5f,cy);
        int lw=0,lh=0; bool hov=inR(x-4,x+box+4); float ha=HoverAnim(HID+1,hov);
        if(ha>0.01f) dl->AddCircleFilled(c,ph*0.5f,A(WithA(COL_INK2,(int)(40*ha))),24);
        if(BarLogoTex(lw,lh)) DrawLogoMark(dl,c,box,a); else MsIcon(dl,"blur_on",c,box,A(COL_INK));
        if(hov&&click) LaunToggle();
        x+=box+14;
    }
    if(BarItemOn(BIT_WORKSPACES)){
        const WsRing& WR=WsRingFor(mi);
        int n=std::clamp(WR.count,1,24);
        float step=ph*0.78f, pw=n*step+ph*0.30f;
        dl->AddRectFilled(V(x,cy-ph*0.5f),V(x+pw,cy+ph*0.5f),A(pill),ph*0.5f);
        float x0=x+ph*0.15f+step*0.5f;
        float cur=Cael::anim(HID+10,(float)std::clamp(WR.cur,0,n-1),Cael::DUR_DEFAULT_SPATIAL,Cael::DEFAULT_SPATIAL);
        for(int k=0;k<n;k++){
            ImVec2 c=V(x0+k*step,cy); bool occ=k<64 && WR.occ[k];
            bool hov=fabsf(io.MousePos.x-c.x)<step*0.5f && io.MousePos.y>=by && io.MousePos.y<by+bh;
            float ha=HoverAnim(HID+20+k,hov);
            float r=ph*0.24f;
            dl->AddCircle(c,r,A(WithA(COL_INK2,(int)(occ? 230 : 150+60*ha))),20,1.7f);
            if(occ) dl->AddCircleFilled(c,r*0.30f,A(WithA(COL_INK2,200)),12);
            if(hov&&click) GotoWorkspace(mi,k);
        }
        // the sliding target ring
        ImVec2 ac=V(x0+cur*step,cy); float R=ph*0.40f;
        dl->AddCircleFilled(ac,R,A(Mix(COL_INK,COL_GOLD,0.2f)),28);
        dl->AddCircle(ac,R*0.66f,A(pill),28,std::max(1.6f,R*0.16f));
        dl->AddCircleFilled(ac,R*0.40f,A(Mix(COL_INK,COL_GOLD,0.2f)),20);
        if(inR(x,x+pw) && io.MouseWheel!=0){ int t=std::clamp(WR.cur+(io.MouseWheel<0? 1 : -1),0,n-1); if(t!=WR.cur) GotoWorkspace(mi,t); }
        x+=pw+18;
    }
    if(BarItemOn(BIT_APPS)) V2BarApps(dl,io,mi,HID,x,cy,ph,false,!onTop,onTop? by+bh : by);
    if(BarItemOn(BIT_WINDOWINFO)){
        std::string title;
        { HWND fgw=GetForegroundWindow(); wchar_t wt[256]={0}; DWORD pid=0;
          if(fgw){ GetWindowTextW(fgw,wt,255); GetWindowThreadProcessId(fgw,&pid); }
          if(fgw && pid!=GetCurrentProcessId() && wt[0]) title=W2U8(CleanTitle(wt)); }
        bool desk=title.empty(); if(desk) title="Desktop";
        float fs=std::clamp(ph*0.50f,12.0f,17.0f);
        std::string t=Clip(g_fSml,fs,title,std::min(360.0f,barW*0.25f));
        MsIcon(dl,desk? "desktop_windows" : "select_window",V(x+fs*0.6f,cy),fs*1.15f,A(COL_INK));
        TextAt(dl,g_fSml,fs,V(x+fs*1.5f,cy-fs*0.62f),A(COL_INK),t.c_str());
    }

    // ---------------------------------------------------------------- right (built right-to-left)
    float rx=bx+barW-14;
    // Your own items (configaritems.toml). Placed rather than ordered - see the vertical bar.
    { float save=rx;
      for(int ci=BAR_CUSTOM_N-1; ci>=0; ci--){
          if(!BarItemOn(BIT_CUSTOM1+ci) || !BarCustomOn(ci)) continue;
          float fs=std::clamp(ph*0.42f,11.0f,15.0f), w=BarCustomExtH(ci,fs);
          bool hov=inR(rx-w,rx);
          BarCustomDrawH(dl,ci,rx-w,cy,fs,A(COL_INK),HoverAnim(HID+700+ci,hov),ph*0.5f);
          if(hov&&click) BarCustomClick(ci);
          if(hov){ const char* t=BarCustomTip(ci); if(t){ tip=t; tipX=rx-w*0.5f; } }
          rx-=w+8;
      }
      if(rx!=save) rx-=4; }
    if(BarItemOn(BIT_POWER)){
        float w=ph; ImVec2 c=V(rx-w*0.5f,cy); bool hov=inR(rx-w,rx); float ha=HoverAnim(HID+40,hov);
        if(ha>0.01f) dl->AddCircleFilled(c,ph*0.5f,A(IM_COL32(232,120,110,(int)(50*ha))),24);
        MsIcon(dl,"power_settings_new",c,ph*0.62f,A(IM_COL32(232,130,120,255)));
        if(hov){ tip="Power"; tipX=c.x; } if(hov&&click) g_sessShow=true;
        rx-=w+10;
    }
    // status pill
    { struct SI{ int kind; float w; };      // 0 volume 1 kbd 2 power 3 bell
      std::vector<SI> items;
      std::vector<V2Kbd> kl;
      if(BarItemOn(BIT_AUDIO)) items.push_back({0,ph*0.95f});
      if(g_barV2Kbd){ kl=V2KbdList(); if(kl.size()>0) items.push_back({1,ph*1.15f}); }
      if(g_barV2Power) items.push_back({2,ph*0.95f});
      if(BarItemOn(BIT_NOTIF)) items.push_back({3,ph*0.95f});
      if(!items.empty()){
          float w=ph*0.40f; for(auto& s:items) w+=s.w;
          float x0=rx-w;
          dl->AddRectFilled(V(x0,cy-ph*0.5f),V(rx,cy+ph*0.5f),A(pill),ph*0.5f);
          float ix=x0+ph*0.20f;
          int notifN; { std::lock_guard<std::mutex> lk(g_notifMtx); notifN=(int)g_notifs.size(); }
          HKL curK=V2KbdCurrent();
          for(auto& s:items){
              ImVec2 c=V(ix+s.w*0.5f,cy); bool hov=inR(ix,ix+s.w); float ha=HoverAnim(HID+50+s.kind,hov);
              if(ha>0.01f) dl->AddCircleFilled(c,ph*0.42f,A(WithA(COL_INK2,(int)(46*ha))),20);
              ImU32 ic=A(WithA(COL_INK,225));
              switch(s.kind){
              case 0: { if(g_volCache<0) g_volCache=GetVolume();
                  const char* vi = g_volCache<=0.001f? "volume_off" : g_volCache<0.5f? "volume_down" : "volume_up";
                  MsIcon(dl,vi,c,ph*0.58f,ic);
                  if(hov){ static char vt[24]; snprintf(vt,24,"Volume %d%%",(int)std::round(std::clamp(g_volCache,0.0f,1.0f)*100)); tip=vt; tipX=c.x;
                      if(io.MouseWheel!=0){ g_volCache=std::clamp(g_volCache+io.MouseWheel*0.05f,0.0f,1.0f); SetVolume(g_volCache); } }
                  if(hov&&click){ g_sideView=4; g_sideForceUntil=GetTickCount64()+4000; } } break;
              case 1: { std::string code="??"; for(auto& k:kl) if(k.hkl==curK) code=k.code;
                  float fs=ph*0.56f; TextAt(dl,g_fMed,fs,V(c.x-TextW(g_fMed,fs,code.c_str())*0.5f,cy-fs*0.62f),ic,code.c_str());
                  if(hov&&click){ g_v2Pop = g_v2Pop==1? 0 : 1; g_v2PopX=c.x; g_v2PopMon=mi; }
                  if(hov && io.MouseClicked[1] && kl.size()>1){ size_t idx=0; for(size_t q=0;q<kl.size();q++) if(kl[q].hkl==curK) idx=q; V2KbdSet(kl[(idx+1)%kl.size()].hkl); } } break;
              case 2: { int m=V2PowerMode(); MsIcon(dl,m==0? "energy_savings_leaf" : m==2? "rocket_launch" : "balance",c,ph*0.58f,ic);
                  if(hov&&click){ g_v2Pop = g_v2Pop==2? 0 : 2; g_v2PopX=c.x; g_v2PopMon=mi; } } break;
              case 3: { MsIcon(dl,g_dnd? "notifications_off" : "notifications",c,ph*0.58f,ic);
                  if(notifN>0 && !g_dnd){ ImVec2 d=V(c.x+ph*0.18f,c.y-ph*0.18f); dl->AddCircleFilled(d,ph*0.12f,A(pill),12); dl->AddCircleFilled(d,ph*0.075f,A(COL_INK),12); }
                  if(hov){ tip=g_dnd? "Do not disturb (right-click to allow)" : "Notifications (right-click for DND)"; tipX=c.x; }
                  if(hov&&click) g_notifForceUntil = GetTickCount64()<g_notifForceUntil? 0 : GetTickCount64()+8000;
                  if(hov&&io.MouseClicked[1]){ g_dnd=!g_dnd; SaveConfig(); } } break;
              }
              ix+=s.w;
          }
          rx=x0-10;
      }
    }
    // date pill
    if(BarItemOn(BIT_CLOCK)){
        float fs=std::clamp(ph*0.52f,12.0f,18.0f);
        static const char* WD[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        char d1[16]; snprintf(d1,16,"%s %d",WD[lt.tm_wday],lt.tm_mday);
        int h=g_clock24? lt.tm_hour : ((lt.tm_hour%12)==0? 12 : lt.tm_hour%12);
        char t1[16]; snprintf(t1,16,"%02d : %02d",h,lt.tm_min);
        const char* ap=g_clock24? "" : (lt.tm_hour<12? "am" : "pm");
        float w=ph*0.45f+fs*1.3f+TextW(g_fMed,fs,d1)+fs*0.9f+TextW(g_fMed,fs,t1)+(*ap? TextW(g_fSml,fs*0.8f,ap)+4 : 0)+ph*0.45f;
        float x0=rx-w; bool hov=inR(x0,rx); float ha=HoverAnim(HID+60,hov);
        dl->AddRectFilled(V(x0,cy-ph*0.5f),V(rx,cy+ph*0.5f),A(Mix(pill,COL_INK2,0.12f*ha)),ph*0.5f);
        float ix=x0+ph*0.45f;
        MsIcon(dl,"calendar_month",V(ix+fs*0.5f,cy),fs*1.15f,A(COL_INK)); ix+=fs*1.3f;
        TextAt(dl,g_fMed,fs,V(ix,cy-fs*0.62f),A(COL_INK),d1); ix+=TextW(g_fMed,fs,d1)+fs*0.45f;
        dl->AddLine(V(ix,cy-fs*0.6f),V(ix,cy+fs*0.6f),A(WithA(COL_INK2,150)),1.2f); ix+=fs*0.45f;
        TextAt(dl,g_fMed,fs,V(ix,cy-fs*0.62f),A(COL_INK),t1); ix+=TextW(g_fMed,fs,t1)+4;
        if(*ap) TextAt(dl,g_fSml,fs*0.8f,V(ix,cy-fs*0.42f),A(COL_INK),ap);
        if(hov&&click){ g_tab=0; g_drawerForceUntil=GetTickCount64()+4000; }
        rx=x0-10;
    }
    // tray expander
    if(BarItemOn(BIT_TRAY)){
        std::vector<SysTrayIcon*> tray;
        { std::lock_guard<std::mutex> lk(g_systrayMtx); for(auto& s:g_systray) if(!s.hidden && s.tex) tray.push_back(&s); }
        float open=Cael::anim(HID+70,g_v2TrayOpen? 1.0f : 0.0f,Cael::DUR_DEFAULT_SPATIAL,Cael::DEFAULT_SPATIAL);
        float step=ph*0.9f;
        float w=ph + std::max(0.0f,open)*(tray.size()*step+ph*0.1f);
        float x0=rx-w;
        dl->AddRectFilled(V(x0,cy-ph*0.5f),V(rx,cy+ph*0.5f),A(pill),ph*0.5f);
        ImVec2 cc=V(rx-ph*0.5f,cy); bool ch=fabsf(io.MousePos.x-cc.x)<ph*0.5f && io.MousePos.y>=by && io.MousePos.y<by+bh;
        float hc=HoverAnim(HID+71,ch); if(hc>0.01f) dl->AddCircleFilled(cc,ph*0.46f,A(WithA(COL_INK2,(int)(46*hc))),20);
        MsIcon(dl,open>0.5f? "chevron_left" : "chevron_right",cc,ph*0.62f,A(COL_INK));
        if(ch&&click) g_v2TrayOpen=!g_v2TrayOpen;
        if(ch) { tip="System tray"; tipX=cc.x; }
        if(open>0.02f){
            dl->PushClipRect(V(x0,by),V(rx-ph,by+bh),true);
            for(size_t k=0;k<tray.size();k++){
                ImVec2 c=V(rx-ph-step*(k+0.5f)-ph*0.05f,cy);
                bool th=fabsf(io.MousePos.x-c.x)<step*0.5f && io.MousePos.y>=by && io.MousePos.y<by+bh && open>0.9f;
                float ha=HoverAnim(HID+80+(int)k,th);
                if(ha>0.01f) dl->AddCircleFilled(c,ph*0.44f,A(WithA(COL_INK2,(int)(46*ha))),20);
                float ts=ph*0.58f;
                dl->AddImage((ImTextureID)tray[k]->tex,V(c.x-ts*0.5f,c.y-ts*0.5f),V(c.x+ts*0.5f,c.y+ts*0.5f),ImVec2(0,0),ImVec2(1,1),A(IM_COL32(255,255,255,(int)(255*open))));
                if(th){ int sx=(int)(g_vs.left+c.x*g_uiScale), sy=(int)(g_vs.top+popAnchorY*g_uiScale);
                    if(click) TrayForward(*tray[k],WM_LBUTTONDOWN,WM_LBUTTONUP,sx,sy);
                    if(io.MouseClicked[1]) TrayForward(*tray[k],WM_RBUTTONDOWN,WM_RBUTTONUP,sx,sy);
                    if(!tray[k]->tip.empty()){ static std::string ts2; ts2=W2U8(tray[k]->tip); size_t nl=ts2.find('\n'); if(nl!=std::string::npos) ts2.resize(nl); tip=ts2.c_str(); tipX=c.x; } }
            }
            dl->PopClipRect();
        }
    }

    V2BarPopout(dl,io,mi,HID,g_v2PopX,popAnchorY,onTop? 0 : 1,bx,by,barW,bh,click);
    V2KbdToastDraw(dl,mi);
    // tooltip: a pill under (or over) the item, centred on it
    if(tip){ static std::string lastTip; static ULONGLONG chg=0; if(lastTip!=tip){ lastTip=tip; chg=GetTickCount64(); }
        float fade=std::clamp((GetTickCount64()-chg)/140.0f,0.0f,1.0f);
        float w=TextW(g_fSml,14,tip)+22, h=28;
        float tx=std::clamp(tipX-w*0.5f,bx+6,bx+barW-w-6), ty= onTop? by+bh+6 : by-6-h;
        dl->AddRectFilled(V(tx,ty),V(tx+w,ty+h),IM_COL32(30,28,36,(int)(245*fade)),h*0.5f);
        TextAt(dl,g_fSml,14,V(tx+11,ty+6),IM_COL32(232,230,238,(int)(255*fade)),tip);
        g_barTipRect=RECT{(LONG)(tx-6),(LONG)(ty-6),(LONG)(tx+w+6),(LONG)(ty+h+6)}; }
}
