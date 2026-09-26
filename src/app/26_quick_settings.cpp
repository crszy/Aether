// Aether - quick settings.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ================================================================= quick settings (bottom-right)
// Anchored to the BOTTOM-RIGHT corner, away from the corners the taskbar occupies. Carries
// everything a proper mini quick-settings should: toggles, master + per-app volume, brightness,
// and the system tray (which used to live in the bar).
static ULONGLONG g_mixNext=0;
static float g_qsRows=0;                 // animated panel height (grows/shrinks between views)

static void DndIcon(ImDrawList* dl,ImVec2 c,ImU32 col){
    dl->AddCircle(c,8.5f,col,0,2.0f); dl->AddLine(V(c.x-4.5f,c.y),V(c.x+4.5f,c.y),col,2.2f);
}
static void CoffeeIcon(ImDrawList* dl,ImVec2 c,ImU32 col){   // keep awake
    dl->AddRect(V(c.x-7,c.y-4),V(c.x+4,c.y+7),col,2.5f,0,1.8f);
    dl->PathArcTo(V(c.x+5,c.y+1),4.0f,-1.5f,1.5f,10); dl->PathStroke(col,0,1.6f);
    dl->AddLine(V(c.x-9,c.y+9),V(c.x+7,c.y+9),col,1.8f);
    dl->AddLine(V(c.x-4,c.y-10),V(c.x-4,c.y-7),col,1.6f);
    dl->AddLine(V(c.x+1,c.y-10),V(c.x+1,c.y-7),col,1.6f);
}
static void TrayGridIcon(ImDrawList* dl,ImVec2 c,ImU32 col){
    for(int gy=0;gy<2;gy++)for(int gx=0;gx<2;gx++)
        dl->AddRectFilled(V(c.x-7+gx*8,c.y-7+gy*8),V(c.x-7+gx*8+5,c.y-7+gy*8+5),col,1.2f);
}

static void DrawSidebarV2(float raw);   // fwd (src/modules/sidebar/SidebarV2.h)
static void DrawSidebar(){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x,H=io.DisplaySize.y;
    float raw=std::clamp(g_sideReveal,0.0f,1.0f);
    if(raw<0.002f){ g_sideRect=RECT{0,0,0,0}; g_qsPanelOn=false; return; }
    if(g_sideStyle==1 && g_sideView==0){ DrawSidebarV2(raw); return; }     // the merged Caelestia sidebar
    // opening overshoots slightly, closing eases straight out
    float slide = (g_sideReveal>0.5f)? EaseOutBack(raw) : EaseOutCubic(raw);
    float af=raw; int al=(int)(af*255);
    bool down=io.MouseDown[0], click=io.MouseClicked[0], rel=io.MouseReleased[0];

    ULONGLONG nowMs=GetTickCount64();
    if(nowMs>g_mixNext){ RefreshMixer(); g_mixNext=nowMs+700; }

    const Panel& P=g_pn[PN_QS];
    // on a left/right edge `size` is the card's WIDTH and its height is the animated span;
    // on a top/bottom edge those swap, so the same card works on any edge
    const float panelW = PanelVert(P)? P.size : (P.span>1.0f? P.span : 384.0f);
    // target height depends on the view; animate between them so switching views is not a jump
    // Whether the two optional sections actually fit; decided with the height, read by the draw.
    bool fitMedia=false, fitGauges=false;
    // height of the main view's fixed content (everything down to the end of the tray strip),
    // measured on the previous frame - see the comment where wantH is worked out
    static float s_qsBase=0.0f;
    float wantH;
    switch(g_sideView){ case 1: case 2: wantH=470; break; case 3: wantH=336; break;
                        case 4: wantH=470; break; case 5: wantH=380; break; case 6: wantH=400; break;
                        default: wantH=0; }
    if(g_sideView==0){
        int mixRows; { std::lock_guard<std::mutex> lk(g_mixerMtx); mixRows=std::min((int)g_mixer.size(),4); }
        // The tray strip used to be the LAST thing in the panel, so its own height cost nothing and
        // a flat 74 covered it. Two sections follow it now, and a sum of constants cannot track what
        // the rows above actually draw (brightness appears and disappears, mixer rows come and go,
        // the tray icon cell scales with the panel width). So the panel measures itself: the main
        // view records where its content really ended last frame and sizes from that. One frame of
        // lag, invisible behind the height easing, and it can never drift again.
        const float trayCell=(panelW-40.0f)/7.0f;
        float base = (s_qsBase>1.0f) ? s_qsBase
                   : (62 + 156 + 44 + (g_brtOk?44.0f:0.0f) + 26 + mixRows*46 + 40
                      + (10.0f + 22.0f + trayCell + 12.0f) + 12);
        // Drop the optional sections rather than run past the screen, gauges first: a quick-settings
        // panel with no gauge row is better than one whose volume sliders are off-screen.
        const float room = H - 2.0f*P.gap - 12.0f;
        fitMedia=g_qsMedia; fitGauges=g_qsGauges;
        if(base + (fitMedia?92.0f:0.0f) + (fitGauges?128.0f:0.0f) > room) fitGauges=false;
        if(base + (fitMedia?92.0f:0.0f) > room) fitMedia=false;
        wantH = std::min(room, base + (fitMedia?92.0f:0.0f) + (fitGauges?128.0f:0.0f));
    }
    if(g_qsRows<=0) g_qsRows=wantH; else Approach(g_qsRows,wantH,14.0f);
    // PanelRect clamps its own span, but the drawing below uses panelH directly - so clamp here too
    // or a tall view paints past the bottom of the screen with the panel's rounded edge off-frame.
    float panelH=std::min(g_qsRows, H-2.0f*P.gap);

    Panel q=P; if(!PanelVert(P)) q.size=panelH;                    // thickness follows the live height
    PRect r=PanelRect(q,W,H, PanelVert(P)? panelH : panelW);
    PanelSlide(q,r,slide,20.0f);                                   // slides in from its own edge
    float px=r.x, py=r.y + (PanelVert(P)? (1.0f-slide)*18.0f : 0.0f);   // and lifts into the corner
    const bool qBorn = FrameBornOn() && PanelVert(P);
    bool qMerge=false;
    if(qBorn){
        float bd=FrameInset(P.edge);
        float rv=std::min(g_sideReveal,1.3f);                 // already eased by the user's motion
        py=r.y;
        px = (P.edge==EDGE_RIGHT)? W-bd-panelW*rv : bd-panelW*(1.0f-rv);
        // MERGE: with the notifications showing, quick settings hangs directly off their bottom, same width,
        // so the two are one column coming out of the right border
        if(P.edge==EDGE_RIGHT && g_nfPanelOn){
            static float s_py=-1; float want=g_nfPanelB;
            if(s_py<0) s_py=want; s_py+= (want-s_py)*std::min(1.0f,g_frameDt*16.0f);
            py=std::min(s_py, H-bd-panelH); qMerge = py>=g_nfPanelB-1.0f;
        }
        g_qsPanelOn=raw>0.02f && P.edge==EDGE_RIGHT; g_qsPanelL=W-bd-panelW; g_qsPanelT=py;
    } else g_qsPanelOn=false;
    ImVec2 pmin=V(px,py),pmax=V(px+panelW,py+panelH);
    if(qBorn){
        float bd=FrameInset(P.edge), rv=std::min(g_sideReveal,1.3f);
        FrameEdgePanel(dl,FrameMon(),V(0,0),V(W,H),P.edge,py,py+panelH,bd+panelW*rv,bd,g_panelRound,1.0f, qMerge? 1e6f : 0.0f);
    } else
    GlassPanel(dl,pmin,pmax,g_panelRound,PanelCorners(q),g_sideAcrylic,af);
    g_sideRect=PanelHitRect(PRect{px,py,panelW,panelH});

    float ix=px+20, rowW=panelW-40;
    auto rowHit=[&](float y,float h){ return io.MousePos.x>ix-8&&io.MousePos.x<ix+rowW+8&&io.MousePos.y>y&&io.MousePos.y<y+h; };
    // back arrow shared by every sub-view
    auto header=[&](const char* title){
        bool bk=io.MousePos.x>px+10&&io.MousePos.x<px+44&&io.MousePos.y>py+8&&io.MousePos.y<py+42;
        float ha=HoverAnim(1950,bk);
        if(ha>0.01f) dl->AddCircleFilled(V(px+27,py+25),16,WithA(COL_INK2,(int)(ha*40*af)));
        dl->AddLine(V(px+32,py+17),V(px+22,py+25),WithA(bk?COL_GOLD:COL_INK,al),2.4f);
        dl->AddLine(V(px+32,py+33),V(px+22,py+25),WithA(bk?COL_GOLD:COL_INK,al),2.4f);
        TextAt(dl,g_fMed,20,V(px+50,py+15),WithA(COL_INK,al),title);
        if(click&&bk) g_sideView=0;
    };
    // a slider row; returns true while being dragged
    auto sliderRow=[&](float y,float& val,int id,bool& changed)->bool{
        float tx0=ix+34, tx1=px+panelW-58;
        dl->AddRectFilled(V(tx0,y+6),V(tx1,y+12),WithA(COL_TRACK,al),3);
        dl->AddRectFilled(V(tx0,y+6),V(tx0+(tx1-tx0)*std::clamp(val,0.0f,1.0f),y+12),WithA(COL_GOLD,al),3);
        bool hov=io.MousePos.y>y-6&&io.MousePos.y<y+26&&io.MousePos.x>tx0-14&&io.MousePos.x<tx1+14;
        float ha=HoverAnim(id,hov);
        dl->AddCircleFilled(V(tx0+(tx1-tx0)*std::clamp(val,0.0f,1.0f),y+9),7.5f+ha*2.0f,WithA(COL_GOLD,al));
        char pc[8]; snprintf(pc,8,"%d",(int)std::round(val*100));
        TextAt(dl,g_fSml,13,V(px+panelW-46,y+1),WithA(COL_INK2,al),pc);
        if(down&&hov){ val=std::clamp((io.MousePos.x-tx0)/(tx1-tx0),0.0f,1.0f); changed=true; return true; }
        return false;
    };

    // ---------------- sub-view: Utilities ----------------
    if(g_sideView==3){
        header("Utilities");
        struct U{const char* label; const char* sub; bool on;} us[3]={
            {"Keep Awake",      g_keepAwake?"Screen and system stay on":"Normal power behaviour", g_keepAwake},
            {"Screen Recorder", g_recording?"Recording\xE2\x80\xA6":"Start a screen capture",      g_recording},
            {"Recordings",      "Open the captures folder",                                        false}};
        float uy=py+62, uh=88;
        for(int i=0;i<3;i++){
            float sa=EaseOutCubic(Stagger(af,i,0.06f,0.5f));
            ImVec2 a=V(ix,uy+(1.0f-sa)*10.0f),b=V(ix+rowW,uy+uh-12+(1.0f-sa)*10.0f);
            bool hov=io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;
            float ha=HoverAnim(7000+i,hov); float ex=ha*2.0f;
            ImVec2 a2=V(a.x-ex,a.y-ex),b2=V(b.x+ex,b.y+ex);
            dl->AddRectFilled(a2,b2, us[i].on?AccA((int)(210*sa*af)):WithA(COL_INK2,(int)((28+ha*40)*sa*af)), 14);
            ImU32 fgc = us[i].on ? (g_darkUI?IM_COL32(12,20,14,255):IM_COL32(250,254,252,255)) : COL_INK;
            TextAt(dl,g_fMed,17,V(a.x+16,a.y+14),WithA(fgc,(int)(al*sa)),us[i].label);
            TextAt(dl,g_fSml,13,V(a.x+16,a.y+38),WithA(us[i].on?MulA(fgc,0.75f):COL_INK2,(int)(al*sa)),us[i].sub);
            if(click&&hov){ if(i==0){ g_keepAwake=!g_keepAwake; ApplyKeepAwake(); }
                            else if(i==1) ToggleRecording(); else OpenRecordings(); }
            uy+=uh;
        }
        return;
    }
    // ---------------- sub-view: full app mixer ----------------
    if(g_sideView==4){
        header("App volume");
        float y=py+62, rh=52;
        std::vector<AppVol> mix; { std::lock_guard<std::mutex> lk(g_mixerMtx); mix=g_mixer; }
        int n=std::min((int)mix.size(),7);
        if(n==0) TextAt(dl,g_fSml,15,V(ix,y+8),WithA(COL_INK2,al),"Nothing is playing audio");
        for(int i=0;i<n;i++){
            auto& a=mix[i]; float sa=EaseOutCubic(Stagger(af,i,0.04f,0.5f));
            float ry=y+i*rh+(1.0f-sa)*10.0f;
            if(a.icon) dl->AddImage((ImTextureID)a.icon,V(ix,ry+4),V(ix+22,ry+26),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(al*sa)));
            std::string nm=a.name; while(!nm.empty()&&TextW(g_fSml,14,nm.c_str())>rowW-140) nm.pop_back();
            TextAt(dl,g_fSml,14,V(ix+30,ry+6),WithA(COL_INK,(int)(al*sa)),nm.c_str());
            // live level meter behind the name
            dl->AddRectFilled(V(ix+30,ry+25),V(ix+30+(rowW-160)*std::clamp(a.peak*1.6f,0.0f,1.0f),ry+27),WithA(COL_GOLD,(int)(120*sa*af)),1);
            bool ch=false; float v=a.vol;
            float tx0=px+panelW-160, tx1=px+panelW-58;
            dl->AddRectFilled(V(tx0,ry+12),V(tx1,ry+18),WithA(COL_TRACK,(int)(al*sa)),3);
            dl->AddRectFilled(V(tx0,ry+12),V(tx0+(tx1-tx0)*v,ry+18),WithA(a.mute?COL_INK2:COL_GOLD,(int)(al*sa)),3);
            bool hov=io.MousePos.y>ry+2&&io.MousePos.y<ry+30&&io.MousePos.x>tx0-12&&io.MousePos.x<tx1+12;
            float ha=HoverAnim(9000+i,hov);
            dl->AddCircleFilled(V(tx0+(tx1-tx0)*v,ry+15),6.5f+ha*2.0f,WithA(a.mute?COL_INK2:COL_GOLD,(int)(al*sa)));
            if(down&&hov){ v=std::clamp((io.MousePos.x-tx0)/(tx1-tx0),0.0f,1.0f); a.vol=v; ch=true; }
            if(ch){ SetAppVolume(a.pid,v,false,false);
                    // the rows are a snapshot of g_mixer, so echo the change into the shared
                    // copy or the slider springs back until the worker next lands
                    { std::lock_guard<std::mutex> lk(g_mixerMtx); for(auto& e:g_mixer) if(e.pid==a.pid){ e.vol=v; break; } } }
            // mute button
            ImVec2 mc=V(px+panelW-36,ry+15); bool mh=fabsf(io.MousePos.x-mc.x)<14&&fabsf(io.MousePos.y-mc.y)<14;
            if(mh) dl->AddCircleFilled(mc,14,WithA(COL_INK2,(int)(40*af)));
            SpeakerIcon(dl,mc,WithA(a.mute?COL_ERR:COL_INK2,(int)(al*sa)));
            if(a.mute) dl->AddLine(V(mc.x-9,mc.y-8),V(mc.x+9,mc.y+8),WithA(COL_ERR,(int)(al*sa)),2.0f);
            if(click&&mh){ a.mute=!a.mute; SetAppVolume(a.pid,0,true,a.mute);
                    { std::lock_guard<std::mutex> lk(g_mixerMtx); for(auto& e:g_mixer) if(e.pid==a.pid){ e.mute=a.mute; break; } } }
        }
        return;
    }
    // ---------------- sub-view: VPN connections ----------------
    if(g_sideView==6){
        header("VPN");
        if(GetTickCount64()-g_vpnChecked > 2000) RefreshVpn();
        std::vector<VpnEntry> vpn; { std::lock_guard<std::mutex> lk(g_vpnMtx); vpn=g_vpn; }
        float y=py+62, rh=52;
        if(vpn.empty()){
            TextAt(dl,g_fSml,15,V(ix,y+8),WithA(COL_INK2,al),"No VPN connections set up");
            ImVec2 a=V(ix,y+44),b=V(ix+rowW,y+78);
            bool hov=io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;
            dl->AddRectFilled(a,b,WithA(COL_INK2,(int)((hov?64:34)*af)),10);
            const char* lb="Add one in Windows settings";
            TextAt(dl,g_fSml,14,V((a.x+b.x)/2-TextW(g_fSml,14,lb)/2,y+52),WithA(COL_INK,al),lb);
            if(click&&hov) AetherShellExec(nullptr,L"open",L"ms-settings:network-vpn",nullptr,nullptr,SW_SHOWNORMAL);
            return;
        }
        int n=std::min((int)vpn.size(),6);
        for(int i=0;i<n;i++){
            auto& v=vpn[i]; float sa=EaseOutCubic(Stagger(af,i,0.04f,0.5f));
            float ry=y+i*rh+(1.0f-sa)*10.0f;
            ImVec2 a=V(ix,ry),b=V(ix+rowW,ry+rh-6);
            bool hov=io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;
            if(v.connected)  dl->AddRectFilled(a,b,AccA((int)(44*sa*af)),10);
            else if(hov)     dl->AddRectFilled(a,b,WithA(COL_INK2,(int)(30*sa*af)),10);
            VpnIcon(dl,V(ix+22,ry+22),WithA(v.connected?COL_GOLD:COL_INK2,(int)(al*sa)));
            std::string nm=W2U8(v.name);
            TextAt(dl,g_fSml,15,V(ix+44,ry+(v.connected?7.0f:13.0f)),WithA(COL_INK,(int)(al*sa)),
                   Clip(g_fSml,15,nm,rowW-140).c_str());
            if(v.connected) TextAt(dl,g_fSml,11,V(ix+44,ry+25),WithA(AccBright(),(int)(al*sa)),"Connected");
            const char* act=v.connected?"Disconnect":"Connect";
            TextAt(dl,g_fSml,13,V(b.x-14-TextW(g_fSml,13,act),ry+15),
                   WithA(hov?COL_GOLD:COL_INK2,(int)(al*sa)),act);
            if(click&&hov){ std::wstring nm=v.name; bool wasUp=v.connected;
                std::thread([nm,wasUp]{ if(wasUp) VpnDisconnect(nm); else VpnConnect(nm); }).detach();
                RefreshVpn(); }
        }
        return;
    }
    // ---------------- sub-view: full system tray ----------------
    if(g_sideView==5){
        header("System tray");
        std::lock_guard<std::mutex> lk(g_systrayMtx);
        std::vector<SysTrayIcon*> items; for(auto&s:g_systray) if(!s.hidden&&s.tex) items.push_back(&s);
        int n=(int)items.size();
        if(n==0) TextAt(dl,g_fSml,15,V(ix,py+70),WithA(COL_INK2,al),"No tray icons");
        const int cols=7; float cell=(rowW)/cols;
        std::string hovTip;
        for(int i=0;i<n && i<28;i++){
            int r=i/cols,c=i%cols; float sa=EaseOutCubic(Stagger(af,i,0.02f,0.5f));
            float cxp=ix+c*cell, cyp=py+62+r*cell+(1.0f-sa)*8.0f;
            bool hov=io.MousePos.x>cxp&&io.MousePos.x<cxp+cell&&io.MousePos.y>cyp&&io.MousePos.y<cyp+cell;
            float ha=HoverAnim(6000+i,hov);
            if(ha>0.01f) dl->AddRectFilled(V(cxp+2,cyp+2),V(cxp+cell-2,cyp+cell-2),WithA(COL_INK2,(int)(ha*46*af)),8);
            float isz=26+ha*3;
            ImVec2 ip=V(cxp+(cell-isz)/2,cyp+(cell-isz)/2);
            dl->AddImage((ImTextureID)items[i]->tex,ip,V(ip.x+isz,ip.y+isz),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(al*sa)));
            if(hov){ const std::wstring& w=items[i]->tip;
                if(!w.empty()){ hovTip=W2U8(w); size_t nl=hovTip.find('\n'); if(nl!=std::string::npos)hovTip=hovTip.substr(0,nl); }
                int sx=(int)((g_mx+ip.x+isz/2)*g_uiScale), sy=(int)((g_my+ip.y+isz/2)*g_uiScale);
                if(click)  TrayForward(*items[i],0x201,0x202,sx,sy);
                if(io.MouseClicked[1]) TrayForward(*items[i],0x204,0x205,sx,sy); }
        }
        if(!hovTip.empty()){ std::string t=hovTip;
            while(!t.empty()&&TextW(g_fSml,14,t.c_str())>rowW) t.pop_back();
            TextAt(dl,g_fSml,14,V(ix,py+panelH-32),WithA(COL_INK2,al),t.c_str()); }
        return;
    }
    // ---------------- sub-views: Wi-Fi / Bluetooth lists ----------------
    if(g_sideView==1||g_sideView==2){
        header(g_sideView==1?"Wi-Fi":"Bluetooth");
        float ry=py+64, rh=48;
        if(g_sideView==1){
            if(!g_wifiScanning && GetTickCount64()-g_wifiLastScan>15000) WifiScanAsync();
            { const char* sl=g_wifiScanning? "Scanning\xE2\x80\xA6" : "Scan"; float sw=TextW(g_fSml,14,sl)+24; ImVec2 sa=V(px+panelW-20-sw,py+22), sb=V(px+panelW-20,py+50);
              bool sh=io.MousePos.x>=sa.x&&io.MousePos.x<sb.x&&io.MousePos.y>=sa.y&&io.MousePos.y<sb.y;
              dl->AddRectFilled(sa,sb,WithA(COL_INK2,(int)((sh?60:30)*af)),14); TextAt(dl,g_fSml,14,V(sa.x+12,sa.y+5),WithA(COL_INK,al),sl);
              if(click&&sh) WifiScanAsync(); }
            DrawWifiList(dl,io,ix,ry,px+panelW-20,1.0f,9,rh+6,click,al,2000);
            return;
        }
        int n = (g_sideView==1)? std::min((int)g_wifi.size(),8) : std::min((int)g_bt.size(),8);
        if(n==0) TextAt(dl,g_fSml,15,V(ix,ry),WithA(COL_INK2,al), g_sideView==1?"No networks found":"No devices found");
        for(int i=0;i<n;i++){
            std::string name; bool conn=false; int sig=0;
            if(g_sideView==1){ name=g_wifi[i].ssid; conn=g_wifi[i].connected; sig=g_wifi[i].signal; }
            else { name=g_bt[i].name; conn=g_bt[i].connected; }
            float sa=EaseOutCubic(Stagger(af,i,0.035f,0.5f));
            ImVec2 a=V(ix,ry+(1.0f-sa)*10.0f),b=V(px+panelW-20,ry+rh-6+(1.0f-sa)*10.0f);
            bool hov=io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;
            float ha=HoverAnim(2000+i,hov);
            if(conn) dl->AddRectFilled(a,b,AccA((int)(46*af)),10);
            else if(ha>0.01f) dl->AddRectFilled(a,b,WithA(COL_INK2,(int)(ha*46*af)),10);
            ImU32 tc=conn?COL_GOLD:COL_INK;
            TextAt(dl,g_fSml,16,V(a.x+12, conn?a.y+rh/2-19.0f:a.y+rh/2-12.0f), WithA(tc,(int)(al*sa)), name.c_str());
            if(conn) TextAt(dl,g_fSml,12,V(a.x+12,a.y+rh/2+1),WithA(COL_INK2,(int)(al*sa)),"Connected");
            if(g_sideView==1){ int bars=sig>75?4:sig>50?3:sig>25?2:1; ImVec2 sg=V(b.x-40,a.y+rh/2+5);
                for(int s=0;s<4;s++){ float bh2=4+s*3.0f; ImU32 bc=(s<bars)?(conn?COL_GOLD:COL_INK):COL_TRACK;
                    dl->AddRectFilled(V(sg.x+s*7,sg.y-bh2),V(sg.x+s*7+4,sg.y),WithA(bc,(int)(al*sa)),1); } }
            else BtIcon(dl,V(b.x-24,a.y+rh/2-4), WithA(conn?COL_GOLD:COL_INK2,(int)(al*sa)));
            if(click&&hov&&g_sideView==1&&!conn) WifiConnect(name);
            ry+=rh;
        }
        return;
    }

    // ---------------- main view ----------------
    TextAt(dl,g_fMed,20,V(ix,py+16),WithA(COL_INK,al),"Quick Settings");
    { ImVec2 gc=V(px+panelW-64,py+26); bool gh=fabsf(io.MousePos.x-gc.x)<16&&fabsf(io.MousePos.y-gc.y)<16;
      float ga=HoverAnim(1900,gh);
      if(ga>0.01f) dl->AddCircleFilled(gc,16,WithA(COL_INK2,(int)(ga*46*af)));
      GearIcon(dl,gc,WithA(gh?COL_GOLD:COL_INK2,al));
      if(click&&gh) g_setShow=true;
      ImVec2 pc2=V(px+panelW-28,py+26); bool ph2=fabsf(io.MousePos.x-pc2.x)<16&&fabsf(io.MousePos.y-pc2.y)<16;
      float pa=HoverAnim(1901,ph2);
      if(pa>0.01f) dl->AddCircleFilled(pc2,16,IM_COL32(224,130,120,(int)(50*af)));
      PowerIcon(dl,pc2,WithA(ph2?IM_COL32(240,120,110,255):IM_COL32(214,110,100,255),al));
      if(click&&ph2) g_sessShow=true; }

    // toggle chips, 4 x 2, cascading in
    float chY=py+56, gap=8, chW=(rowW-3*gap)/4, chH=70;
    // VPN state is polled lazily - RasEnumEntries walks the phonebook and reads every entry's
    // properties, which is far too much to do on a 60Hz panel.
    if(GetTickCount64()-g_vpnChecked > 4000) RefreshVpn();   // returns immediately; worker does the work
    bool vpnOn; { std::lock_guard<std::mutex> lk(g_vpnMtx); vpnOn=g_vpnOn; }
    struct Chip{const char* label; bool on; int icon;};
    const int NCHIP=12;
    Chip chips[NCHIP]={
        {"Wi-Fi",      g_st.online, 0},
        {"Bluetooth",  true,        1},
        {"VPN",        vpnOn,       8},
        {g_darkUI?"Dark":"Light", true, 2},
        {"Do Not Dist",g_dnd,       4},
        {"Keep Awake", g_keepAwake, 5},
        {"Record",     g_recording, 6},
        {"Mic",        !g_micMuted, 7},
        {"Tray",       g_trayOpen,  3},
        {"Ethernet",   false,       9},
        {"Settings",   false,      10},
        {"Lock",       false,      11}};
    for(int i=0;i<NCHIP;i++){
        int r=i/4,c=i%4;
        float sa=EaseOutCubic(Stagger(af,i,0.035f,0.5f));
        float cxp=ix+c*(chW+gap), cyp=chY+r*(chH+gap)+(1.0f-sa)*12.0f;
        bool hov=io.MousePos.x>cxp&&io.MousePos.x<cxp+chW&&io.MousePos.y>cyp&&io.MousePos.y<cyp+chH;
        float ha=HoverAnim(1000+i,hov); float ex=ha*2.5f;
        ImVec2 a=V(cxp-ex,cyp-ex),b=V(cxp+chW+ex,cyp+chH+ex);
        ImU32 chipbg = chips[i].on ? AccA((int)(210*sa*af)) : WithA(COL_INK2,(int)((26+ha*46)*sa*af));
        dl->AddRectFilled(a,b,chipbg,13);
        ImU32 fgc = chips[i].on?(g_darkUI?IM_COL32(12,20,14,255):IM_COL32(250,254,252,255)):COL_INK;
        ImU32 fga = WithA(fgc,(int)(al*sa));
        ImVec2 icc=V((a.x+b.x)/2, a.y+26);
        switch(chips[i].icon){
        case 0: WifiIcon(dl,V(icc.x,icc.y-4),fga); break;
        case 1: BtIcon(dl,icc,fga); break;
        case 2: if(g_darkUI) MoonIcon(dl,icc,fga,chipbg); else SunIcon(dl,icc,fga); break;
        case 3: TrayGridIcon(dl,icc,fga); break;
        case 4: DndIcon(dl,icc,fga); break;
        case 5: CoffeeIcon(dl,icc,fga); break;
        case 6: RecIcon(dl,icc,fga,g_recording); break;
        case 7: MicIcon(dl,icc,fga,g_micMuted); break;
        case 8: VpnIcon(dl,icc,fga); break;
        case 9: EthIcon2(dl,icc,fga); break;
        case 10: GearIcon(dl,icc,fga); break;
        default: LockIcon2(dl,icc,fga); break; }
        std::string lb=chips[i].label; while(!lb.empty()&&TextW(g_fSml,11,lb.c_str())>chW-6) lb.pop_back();
        TextAt(dl,g_fSml,11,V((a.x+b.x)/2 - TextW(g_fSml,11,lb.c_str())/2, b.y-18), fga, lb.c_str());
        if(click&&hov){
            switch(i){
            case 0: g_sideView=1; RefreshWifi(); break;
            case 1: g_sideView=2; RefreshBt(); break;
            case 2: VpnQuickToggle(); break;
            case 3: g_themeMode=g_darkUI?1:2; ApplyThemeMode(); SetLightTheme(!g_darkUI); SaveConfig(); g_deskDirty=true; break;
            case 4: g_dnd=!g_dnd; SaveConfig(); break;
            case 5: g_keepAwake=!g_keepAwake; ApplyKeepAwake(); break;
            case 6: ToggleRecording(); break;
            case 7: g_micMuted=!g_micMuted; SetMicMute(g_micMuted); break;
            case 8: g_sideView=5; break;
            case 9: g_sideView=1; RefreshEth(); RefreshWifi(); break;
            case 10: g_setShow=true; break;
            default: DoLock(); break; }
        }
    }

    float y=chY+((NCHIP+3)/4)*(chH+gap)+6;
    // master volume
    if(g_volCache<0)g_volCache=GetVolume();
    { SpeakerIcon(dl,V(ix+12,y+9),WithA(COL_INK2,al));
      bool ch=false; float v=g_volCache;
      sliderRow(y,v,1500,ch);
      if(ch){ g_volCache=v; SetVolume(v); } }
    y+=44;
    // brightness (DDC/CI; applied on release because it is slow)
    BrightnessProbe();
    if(g_brtOk){ SunIcon(dl,V(ix+12,y+9),WithA(COL_INK2,al));
        bool ch=false; sliderRow(y,g_brtCache,1501,ch);
        if(ch) OsdShowBrightness(g_brtCache);
        if(rel&&ch) SetBrightness((int)(g_brtCache*g_brtMax));
        y+=44; }

    // ---- per-app volume ----
    { TextAt(dl,g_fSml,13,V(ix,y+4),WithA(COL_INK2,al),"App volume");
      std::vector<AppVol> mix; { std::lock_guard<std::mutex> lk(g_mixerMtx); mix=g_mixer; }
      int extra=(int)mix.size()-4;
      if(extra>0){ char mb[24]; snprintf(mb,24,"%d more",extra);
          bool mh=rowHit(y,22)&&io.MousePos.x>px+panelW-100;
          TextAt(dl,g_fSml,13,V(px+panelW-20-TextW(g_fSml,13,mb),y+4),WithA(mh?COL_GOLD:COL_INK2,al),mb);
          if(click&&mh) g_sideView=4; }
      y+=26;
      int n=std::min((int)mix.size(),4);
      if(n==0){ TextAt(dl,g_fSml,13,V(ix+4,y+6),WithA(COL_INK2,(int)(al*0.8f)),"Nothing is playing audio"); }
      for(int i=0;i<n;i++){
          auto& a=mix[i]; float sa=EaseOutCubic(Stagger(af,8+i,0.035f,0.5f));
          float ry=y+i*46+(1.0f-sa)*10.0f;
          if(a.icon) dl->AddImage((ImTextureID)a.icon,V(ix,ry+3),V(ix+22,ry+25),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(al*sa)));
          std::string nm=a.name; while(!nm.empty()&&TextW(g_fSml,13,nm.c_str())>150) nm.pop_back();
          TextAt(dl,g_fSml,13,V(ix+30,ry+2),WithA(COL_INK,(int)(al*sa)),nm.c_str());
          // live level: the "checker" part — you can see which app is actually making noise
          dl->AddRectFilled(V(ix+30,ry+21),V(ix+30+120*std::clamp(a.peak*1.6f,0.0f,1.0f),ry+23),WithA(COL_GOLD,(int)(140*sa*af)),1);
          float tx0=px+panelW-166, tx1=px+panelW-58;
          dl->AddRectFilled(V(tx0,ry+9),V(tx1,ry+15),WithA(COL_TRACK,(int)(al*sa)),3);
          dl->AddRectFilled(V(tx0,ry+9),V(tx0+(tx1-tx0)*a.vol,ry+15),WithA(a.mute?COL_INK2:COL_GOLD,(int)(al*sa)),3);
          bool hov=io.MousePos.y>ry&&io.MousePos.y<ry+26&&io.MousePos.x>tx0-12&&io.MousePos.x<tx1+12;
          float ha=HoverAnim(9100+i,hov);
          dl->AddCircleFilled(V(tx0+(tx1-tx0)*a.vol,ry+12),6.0f+ha*2.0f,WithA(a.mute?COL_INK2:COL_GOLD,(int)(al*sa)));
          if(down&&hov){ a.vol=std::clamp((io.MousePos.x-tx0)/(tx1-tx0),0.0f,1.0f); SetAppVolume(a.pid,a.vol,false,false);
                    { std::lock_guard<std::mutex> lk(g_mixerMtx); for(auto& e:g_mixer) if(e.pid==a.pid){ e.vol=a.vol; break; } } }
          ImVec2 mc=V(px+panelW-36,ry+12); bool mh=fabsf(io.MousePos.x-mc.x)<13&&fabsf(io.MousePos.y-mc.y)<13;
          if(mh) dl->AddCircleFilled(mc,13,WithA(COL_INK2,(int)(40*af)));
          SpeakerIcon(dl,mc,WithA(a.mute?COL_ERR:COL_INK2,(int)(al*sa)));
          if(a.mute) dl->AddLine(V(mc.x-8,mc.y-7),V(mc.x+8,mc.y+7),WithA(COL_ERR,(int)(al*sa)),2.0f);
          if(click&&mh){ a.mute=!a.mute; SetAppVolume(a.pid,0,true,a.mute);
                    { std::lock_guard<std::mutex> lk(g_mixerMtx); for(auto& e:g_mixer) if(e.pid==a.pid){ e.mute=a.mute; break; } } }
      }
      y+=n*46+8;
    }

    // ---- system tray strip (moved here out of the taskbar) ----
    dl->AddLine(V(ix,y),V(px+panelW-20,y),WithA(COL_INK2,(int)(46*af)),1); y+=10;
    { std::lock_guard<std::mutex> lk(g_systrayMtx);
      std::vector<SysTrayIcon*> items; for(auto&s:g_systray) if(!s.hidden&&s.tex) items.push_back(&s);
      int n=(int)items.size();
      TextAt(dl,g_fSml,13,V(ix,y),WithA(COL_INK2,al),"System tray");
      if(n>7){ char mb[24]; snprintf(mb,24,"%d more",n-7);
          bool mh=io.MousePos.x>px+panelW-100&&io.MousePos.x<px+panelW-16&&io.MousePos.y>y-4&&io.MousePos.y<y+20;
          TextAt(dl,g_fSml,13,V(px+panelW-20-TextW(g_fSml,13,mb),y),WithA(mh?COL_GOLD:COL_INK2,al),mb);
          if(click&&mh) g_sideView=5; }
      y+=22;
      float cell=(rowW)/7;
      std::string hovTip;
      for(int i=0;i<n && i<7;i++){
          float sa=EaseOutCubic(Stagger(af,12+i,0.03f,0.5f));
          float cxp=ix+i*cell, cyp=y+(1.0f-sa)*8.0f;
          bool hov=io.MousePos.x>cxp&&io.MousePos.x<cxp+cell&&io.MousePos.y>cyp&&io.MousePos.y<cyp+cell;
          float ha=HoverAnim(6100+i,hov);
          if(ha>0.01f) dl->AddRectFilled(V(cxp+1,cyp+1),V(cxp+cell-1,cyp+cell-1),WithA(COL_INK2,(int)(ha*46*af)),8);
          float isz=24+ha*3; ImVec2 ip=V(cxp+(cell-isz)/2,cyp+(cell-isz)/2);
          dl->AddImage((ImTextureID)items[i]->tex,ip,V(ip.x+isz,ip.y+isz),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(al*sa)));
          if(hov){ if(!items[i]->tip.empty()){ hovTip=W2U8(items[i]->tip);
                       size_t nl=hovTip.find('\n'); if(nl!=std::string::npos)hovTip=hovTip.substr(0,nl); }
              int sx=(int)((g_mx+ip.x+isz/2)*g_uiScale), sy=(int)((g_my+ip.y+isz/2)*g_uiScale);
              if(click) TrayForward(*items[i],0x201,0x202,sx,sy);
              if(io.MouseClicked[1]) TrayForward(*items[i],0x204,0x205,sx,sy); }
      }
      if(n==0) TextAt(dl,g_fSml,13,V(ix+4,y+8),WithA(COL_INK2,(int)(al*0.8f)),"No tray icons");
      if(!hovTip.empty()){ std::string t=hovTip;
          while(!t.empty()&&TextW(g_fSml,12,t.c_str())>rowW) t.pop_back();
          BarTip(dl,ix,y+cell+14,t.c_str()); }
      y+=cell+12;
    }
    // THE measurement the height calculation above reads next frame
    s_qsBase = y - py;

    // ---- now playing (the rice puts the player in the same panel, not a separate flyout) ----
    if(fitMedia){
        dl->AddLine(V(ix,y),V(px+panelW-20,y),WithA(COL_INK2,(int)(46*af)),1); y+=10;
        TextAt(dl,g_fSml,13,V(ix,y),WithA(COL_INK2,al),"Now playing"); y+=20;
        bool hasM=g_md.has && !g_md.title.empty();
        const float art=52;
        if(g_mdArt){
            ImVec2 uv0,uv1; CoverUV(g_mdArtW>0?g_mdArtW:1,g_mdArtH>0?g_mdArtH:1,art,art,uv0,uv1);
            dl->AddImageRounded((ImTextureID)g_mdArt,V(ix,y),V(ix+art,y+art),uv0,uv1,
                                IM_COL32(255,255,255,al),12);
        } else {
            dl->AddRectFilled(V(ix,y),V(ix+art,y+art),WithA(COL_TRACK,al),12);
            dl->AddRectFilled(V(ix+art*0.34f,y+art*0.28f),V(ix+art*0.42f,y+art*0.72f),WithA(COL_INK2,al),2);
            dl->AddCircleFilled(V(ix+art*0.36f,y+art*0.70f),5.0f,WithA(COL_INK2,al));
        }
        // title / artist, clipped so a long track name cannot push the transport off the panel
        float tw=rowW-art-12-3*36;
        std::string t1 = hasM? Clip(g_fMed,15,g_md.title,tw)  : std::string("Nothing playing");
        std::string t2 = hasM? Clip(g_fSml,13,g_md.artist,tw) : std::string();
        TextAt(dl,g_fMed,15,V(ix+art+12,y+8),WithA(COL_INK,al),t1.c_str());
        if(!t2.empty()) TextAt(dl,g_fSml,13,V(ix+art+12,y+29),WithA(COL_INK2,al),t2.c_str());
        // transport - the same media keys the dashboard card sends
        float bty=y+art*0.5f;
        auto tb=[&](float bcx,int kind,BYTE key){
            bool h=fabsf(io.MousePos.x-bcx)<16&&fabsf(io.MousePos.y-bty)<16;
            ImU32 cc=WithA(h?COL_GOLD:COL_INK,al);
            if(h) dl->AddCircleFilled(V(bcx,bty),15,WithA(COL_INK2,(int)(52*af)));
            if(kind==0){ dl->AddRectFilled(V(bcx-6,bty-6),V(bcx-4,bty+6),cc);
                         dl->AddTriangleFilled(V(bcx+6,bty-6),V(bcx+6,bty+6),V(bcx-3,bty),cc); }
            else if(kind==1){ if(g_md.playing){ dl->AddRectFilled(V(bcx-5,bty-7),V(bcx-1,bty+7),cc);
                                                dl->AddRectFilled(V(bcx+1,bty-7),V(bcx+5,bty+7),cc); }
                              else dl->AddTriangleFilled(V(bcx-5,bty-7),V(bcx-5,bty+7),V(bcx+7,bty),cc); }
            else { dl->AddRectFilled(V(bcx+4,bty-6),V(bcx+6,bty+6),cc);
                   dl->AddTriangleFilled(V(bcx-6,bty-6),V(bcx-6,bty+6),V(bcx+3,bty),cc); }
            if(click&&h){ keybd_event(key,0,0,0); keybd_event(key,0,KEYEVENTF_KEYUP,0); } };
        float bx=px+panelW-20-18;
        tb(bx,2,VK_MEDIA_NEXT_TRACK); tb(bx-36,1,VK_MEDIA_PLAY_PAUSE); tb(bx-72,0,VK_MEDIA_PREV_TRACK);
        y+=art+10;
    }

    // ---- resource gauges: the M3 shapes filling up, three across ----
    if(fitGauges){
        dl->AddLine(V(ix,y),V(px+panelW-20,y),WithA(COL_INK2,(int)(46*af)),1); y+=10;
        TextAt(dl,g_fSml,13,V(ix,y),WithA(COL_INK2,al),"Resources"); y+=18;
        const float gw=rowW/3.0f, gh=92.0f;
        struct G{ const char* t; float f; std::string sub; } gs[3]={
            {"CPU",(float)g_st.cpuUsage,TempSub(g_st.cpuTemp)},
            {"GPU",(float)g_st.gpuUsage,TempSub(g_st.gpuTemp)},
            {"RAM",g_st.memTotal?(float)g_st.memUsed/g_st.memTotal:0.0f,GiB(g_st.memUsed)+"G"} };
        for(int i=0;i<3;i++){
            float sa=EaseOutCubic(Stagger(af,14+i,0.035f,0.5f));
            M3Gauge(dl,V(ix+i*gw,y+(1.0f-sa)*10.0f),V(gw,gh),gs[i].t,gs[i].f,gs[i].sub,g_m3Shape,9450+i);
        }
        y+=gh+8;
    }
}

static void DrawOSD(){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x,H=io.DisplaySize.y;
    long long remain=(long long)g_osdShowUntil-(long long)GetTickCount64();
    float alpha=std::clamp((float)remain/280.0f,0.0f,1.0f);
    {   // born from the right border: out on show, back in as it expires
        float rv=std::min(g_osdReveal,1.3f);
        float bd=FrameInset(EDGE_RIGHT), bodyW=64.0f;
        float MWl=(float)g_mw/g_uiScale, MHl=(float)g_mh/g_uiScale, top=-(MHl-H)*0.5f;
        float x0=W-bd-bodyW*rv, y0=18.0f, y1=H-18.0f;
        FrameEdgePanel(dl,FrameMon(),V(W-MWl,top),V(W,top+MHl),EDGE_RIGHT,y0,y1,bd+bodyW*rv,bd,22.0f,1.0f);
        if(rv<0.01f) return;
        float cx=x0+bodyW*0.5f;
        bool brt=(g_osdMode==1);
        float v=brt? g_osdBrt : (g_osdMuted?0.0f:g_osdVol);
        float tt=y0+46.0f, tb=y1-54.0f;
        dl->PushClipRect(V(W-bd-bodyW*rv,0),V(W-bd,H),true);
        dl->AddRectFilled(V(cx-14,tt),V(cx+14,tb),WithA(COL_INK,40),14);
        float fy=tb-(tb-tt)*std::clamp(v,0.0f,1.0f);
        dl->AddRectFilled(V(cx-14,std::min(fy,tb-28.0f)),V(cx+14,tb),COL_GOLD,14);
        char pc[8]; snprintf(pc,8,"%d",(int)std::round(v*100));
        TextAt(dl,g_fMed,18,V(cx-TextW(g_fMed,18,pc)*0.5f,y0+12),COL_INK,pc);
        ImVec2 ic=V(cx,tb+28);
        if(brt) SunIcon(dl,ic,COL_INK); else { SpeakerIcon(dl,ic,COL_INK);
            if(g_osdMuted) dl->AddLine(V(ic.x-10,ic.y-9),V(ic.x+12,ic.y+9),IM_COL32(224,96,86,255),2.5f); }
        dl->PopClipRect();
        return;
    }
    ImVec2 pmin=V(16,10),pmax=V(W-16,H-10);
    dl->AddRectFilled(pmin,pmax,IM_COL32(22,20,28,(int)(238*alpha)),18);
    dl->AddRect(pmin,pmax,IM_COL32(255,255,255,(int)(18*alpha)),18,0,1.5f);
    float cy=H*0.5f;
    bool brt=(g_osdMode==1);
    if(brt) SunIcon(dl,V(44,cy),IM_COL32(230,228,236,(int)(255*alpha)));
    else {
        SpeakerIcon(dl,V(44,cy),IM_COL32(230,228,236,(int)(255*alpha)));
        if(g_osdMuted) dl->AddLine(V(34,cy-9),V(58,cy+9),IM_COL32(224,96,86,(int)(255*alpha)),2.5f);
    }
    float bx0=74,bx1=W-64,by=cy-4;
    dl->AddRectFilled(V(bx0,by),V(bx1,by+8),IM_COL32(70,66,78,(int)(200*alpha)),4);
    float v=brt? g_osdBrt : (g_osdMuted?0.0f:g_osdVol);
    dl->AddRectFilled(V(bx0,by),V(bx0+(bx1-bx0)*v,by+8),(COL_GOLD&0x00FFFFFF)|((unsigned)(255*alpha)<<24),4);
    char pc[8]; snprintf(pc,8,"%d",(int)std::round(v*100));
    TextAt(dl,g_fMed,20,V(W-52,cy-13),IM_COL32(235,232,240,(int)(255*alpha)),pc);
}

static void SessIcon(ImDrawList* dl,int i,ImVec2 ic,ImU32 fg,ImU32 bgc){
    // 0 lock, 1 sleep, 2 sign out, 3 restart, 4 shut down - the same five Plasma's session screen has
    static const char* SESSN[5]={"system-lock-screen","system-suspend","system-log-out","system-reboot","system-shutdown"};
    if(i>=0&&i<5 && ThemedSym(dl,ic,11.0f,fg,SESSN[i])) return;
    if(i==0){ dl->AddRectFilled(V(ic.x-8,ic.y-1),V(ic.x+8,ic.y+11),fg,2); dl->PathArcTo(V(ic.x,ic.y-1),6,3.1416f,6.2832f,12); dl->PathStroke(fg,0,2.4f); }
    else if(i==1){ dl->AddCircleFilled(ic,9,fg); dl->AddCircleFilled(V(ic.x+5,ic.y-4),8,bgc); }
    else if(i==2){ dl->AddRect(V(ic.x-9,ic.y-9),V(ic.x+1,ic.y+9),fg,2,0,2.2f); dl->AddLine(V(ic.x-1,ic.y),V(ic.x+11,ic.y),fg,2.4f); dl->AddLine(V(ic.x+6,ic.y-5),V(ic.x+11,ic.y),fg,2.4f); dl->AddLine(V(ic.x+6,ic.y+5),V(ic.x+11,ic.y),fg,2.4f); }
    else if(i==3){ dl->PathArcTo(ic,8,-1.2f,3.6f,20); dl->PathStroke(fg,0,2.4f); dl->AddTriangleFilled(V(ic.x+6,ic.y-9),V(ic.x+11,ic.y-6),V(ic.x+4,ic.y-4),fg); }
    else { PowerIcon(dl,ic,fg); }
}
// session/power screen — frosted backdrop, big clock, staggered animated button column
static void DrawSession(){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x,H=io.DisplaySize.y;
    float a=std::clamp(g_sessAnim,0.0f,1.0f), ea=1-(1-a)*(1-a);
    auto A=[](ImU32 c,float t){ int al=(int)(((c>>24)&0xFF)*std::clamp(t,0.0f,1.0f)); return (c&0x00FFFFFF)|((ImU32)al<<24); };
    if(g_sessBg) dl->AddImage((ImTextureID)g_sessBg,V(0,0),V(W,H),ImVec2(0,0),ImVec2(1,1),A(IM_COL32(255,255,255,255),ea));
    dl->AddRectFilled(V(0,0),V(W,H),A(IM_COL32(10,9,14,206),ea));

    // clock + date (slide/fade in)
    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
    char clk[8]; snprintf(clk,8,"%d:%02d",g_clock24?lt.tm_hour:((lt.tm_hour%12)==0?12:lt.tm_hour%12),lt.tm_min);
    float ca=std::clamp((a-0.05f)/0.4f,0.0f,1.0f), cyo=(1-ca)*22;
    float cw=g_fHuge->CalcTextSizeA(72,FLT_MAX,0,clk).x;
    dl->AddText(g_fHuge,72,V(W/2-cw/2,H*0.15f-cyo),A(IM_COL32(240,238,246,255),ca),clk);
    const char* mn[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
    const char* dn[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
    char date[64]; snprintf(date,64,"%s, %s %d",dn[lt.tm_wday],mn[lt.tm_mon],lt.tm_mday);
    TextAt(dl,g_fMed,21,V(W/2-TextW(g_fMed,21,date)/2,H*0.15f+66-cyo),A(IM_COL32(184,182,192,255),ca),date);

    // Session lives as a VERTICAL STRIP ON THE RIGHT EDGE (like the reference): a column of large
    // square buttons that slide in from the right and cascade upward.
    const char* labels[5]={"Lock","Sleep","Sign out","Restart","Shut down"};
    int nB=5; float bw=112,bh=112,gp=14; float total=nB*bh+(nB-1)*gp;
    float y0=(H-total)*0.5f;
    float xR=W-40-bw + (1.0f-ea)*(bw+60);            // slides in from off the right edge
    if(FrameBornOn()){                               // the column is a panel coming out of the right border
        float bd=FrameInset(EDGE_RIGHT), padS=18.0f;
        float rv=std::min(g_sessAnim,1.3f);
        FrameEdgePanel(dl,FrameMon(),V(0,0),V(W,H),EDGE_RIGHT,y0-padS,y0+total+padS,bd+(bw+padS*2)*rv,bd,26.0f,1.0f);
        xR=W-bd-(bw+padS)*rv;
    }
    bool click=io.MouseClicked[0] && a>0.75f; bool anyHov=false;
    for(int i=0;i<nB;i++){ float la=std::clamp((a-i*0.05f)/0.5f,0.0f,1.0f); float le=1-(1-la)*(1-la)*(1-la);
        float yoff=(1-le)*38, by=y0+i*(bh+gp)+yoff; ImVec2 A0=V(xR,by),B0=V(xR+bw,by+bh);
        bool hov=a>0.75f && io.MousePos.x>A0.x&&io.MousePos.x<B0.x&&io.MousePos.y>A0.y&&io.MousePos.y<B0.y; if(hov)anyHov=true;
        float ha=HoverAnim(5000+i,hov); float ex=ha*6.0f; ImVec2 a2=V(A0.x-ex,A0.y-ex),b2=V(B0.x+ex,B0.y+ex);
        dl->AddRectFilled(a2,b2,A(IM_COL32(30,28,38,238),le),22);
        if(ha>0.01f){ dl->AddRectFilled(a2,b2,A(WithA(COL_GOLD,(int)(235*ha)),le),22);
                      dl->AddRect(a2,b2,A(WithA(COL_GOLD,(int)(255*ha)),le),22,0,2); }
        ImU32 fg=(ha>0.5f)?IM_COL32(16,22,16,255):IM_COL32(234,232,240,255);
        ImVec2 icc=V((a2.x+b2.x)*0.5f, by+bh*0.40f);
        dl->AddCircleFilled(icc,22,A(WithA(COL_GOLD,(int)(0x30*(1.0f-ha))),le));
        SessIcon(dl,i,icc,A(fg,le), A(ha>0.5f?WithA(COL_GOLD,255):IM_COL32(30,28,38,238),le));
        TextAt(dl,g_fSml,14,V((a2.x+b2.x)*0.5f-TextW(g_fSml,14,labels[i])/2, by+bh-30),A(fg,le),labels[i]);
        if(click&&hov){ g_sessShow=false;
            if(i==0){ DoLock(); }
            else if(i==1)DoSleep();
            else if(i==2)ExitWindowsEx(EWX_LOGOFF,0);
            else if(i==3)AetherShellExec(nullptr,L"open",L"shutdown.exe",L"/r /t 0",nullptr,SW_HIDE);
            else AetherShellExec(nullptr,L"open",L"shutdown.exe",L"/s /t 0",nullptr,SW_HIDE); }
    }
    const char* hint="click anywhere or press Esc to cancel";
    TextAt(dl,g_fSml,15,V(W/2-TextW(g_fSml,15,hint)/2,H*0.15f+104),A(IM_COL32(160,158,168,255),ca),hint);
    if(click&&!anyHov) g_sessShow=false;
}
