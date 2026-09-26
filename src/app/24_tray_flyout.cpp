// Aether - the system-tray flyout.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ============================ SYSTEM-TRAY FLYOUT (the chevron's panel) =========================
// Windows' overflow tray, in our material: a grid of every icon the user has NOT promoted into the
// bar. Left-click activates the app, right-click forwards to the app's own menu, and the pin badge
// (or a middle-click) promotes an icon so it lives in the bar permanently.
//
// It grows OUT of the strip in g_trayFlyDir and tucks a few px UNDER the bar edge, so the strip and
// the panel are one continuous surface - the same merge SysFlyout uses, just on the other axis for
// a horizontal bar. Only the desktop-facing corners are rounded.
static void TrayFlyout(ImDrawList* dl, ImGuiIO& io, int mi, RECT& hit){
    if(g_trayFlyMon!=mi) return;
    bool click=io.MouseClicked[0], rclick=io.MouseClicked[1], mclick=io.MouseClicked[2];
    float open=g_trayOpen?1.0f:0.0f;

    // ---- the hidden set, measured before any geometry ----
    static std::vector<SysTrayIcon> items; items.clear();
    { std::lock_guard<std::mutex> lk(g_systrayMtx);
      for(auto&s:g_systray) if(!s.hidden&&s.tex&&!TrayIsShown(s)) items.push_back(s); }
    int n=(int)items.size();
    const float cell=44.0f, pad=12.0f, headH=30.0f;
    int cols = std::min(std::max(n,1), 5);
    int rows = (n+cols-1)/cols; if(rows<1) rows=1;
    float mw = std::max(pad*2+cols*cell, 168.0f);
    float mh = pad+headH+rows*cell+pad;

    // ---- morph: width/height spring to the content, opacity is a separate quick fade ----
    float panelH=MotionAnim(MP_POPOUT,810040, open>0.5f? mh:0.0f);
    float panelW=MotionAnim(MP_POPOUT,810041, open>0.5f? mw:0.0f);
    float af=std::clamp(Cael::anim(810042, open, Cael::DUR_DEFAULT_EFFECTS, Cael::DEFAULT_EFFECTS),0.0f,1.0f);
    g_trayAnim=af;
    if(panelH<2.0f && open<0.5f) return;
    int al=(int)(af*255);

    // The bar is a FLOATING pill, not a full-length strip, so the popout cannot merge into it the way
    // SysFlyout merges into a vertical bar: over most of its width there is no bar underneath, and a
    // square bottom edge there reads as "a card overlapping the bar" (the exact defect the shoulder
    // fillets were dropped for). It floats a hair off the bar instead, fully rounded, like the pill.
    const float lift=8.0f;
    ImVec2 m0,m1; ImDrawFlags rc=ImDrawFlags_RoundCornersAll;
    switch(g_trayFlyDir){
    case 1:  m0=V(g_trayFlyAt.x-panelW*0.5f, g_trayFlyAt.y+lift);   m1=V(m0.x+panelW, m0.y+panelH); break;
    case 2:  m0=V(g_trayFlyAt.x+lift, g_trayFlyAt.y-panelH*0.5f);   m1=V(m0.x+panelW, m0.y+panelH); break;
    case 3:  m1=V(g_trayFlyAt.x-lift, g_trayFlyAt.y+panelH*0.5f);   m0=V(m1.x-panelW, m1.y-panelH); break;
    default: m1=V(g_trayFlyAt.x+panelW*0.5f, g_trayFlyAt.y-lift);   m0=V(m1.x-panelW, m1.y-panelH); break; }
    // keep it on screen: a chevron near a monitor edge would otherwise push half the grid off it
    { RECT r=MonRect(mi); float L=(r.left-g_vs.left)/g_uiScale+6, R=(r.right-g_vs.left)/g_uiScale-6;
      float dx=0; if(m1.x>R) dx=R-m1.x; if(m0.x+dx<L) dx=L-m0.x;
      m0.x+=dx; m1.x+=dx; }

    // No desktop notch here: a notch makes the wallpaper curve around the panel so it reads as CUT
    // INTO the desktop, which is right for a popout welded to a full-length strip. This one floats,
    // so it gets a drop shadow instead - a cut AND a shadow would be two contradictory depths.
    ImU32 barFill = g_darkUI ? IM_COL32(18,18,22,255) : PanelCol(255);
    for(int sh=5;sh>=1;sh--)   // soft drop shadow so it lifts off the wallpaper like the bar pill does
        dl->AddRect(V(m0.x-sh,m0.y-sh+1),V(m1.x+sh,m1.y+sh+1),IM_COL32(0,0,0,(int)(13*af)),16+sh,0,(float)sh*2);
    dl->AddRectFilled(m0,m1,barFill,16,rc);
    dl->AddRect(m0,m1,g_darkUI?WithA(IM_COL32(255,255,255,255),(int)(20*af)):WithA(IM_COL32(0,0,0,255),(int)(26*af)),16,0,1.0f);
    dl->PushClipRect(V(m0.x-1,m0.y-1),V(m1.x+1,m1.y+1),true);

    char hdr[40]; snprintf(hdr,sizeof(hdr),"System tray");
    TextAt(dl,g_fMed,15,V(m0.x+pad+2,m0.y+pad-1),WithA(COL_INK,al),hdr);
    { char cnt[16]; snprintf(cnt,sizeof(cnt),"%d",n);
      TextAt(dl,g_fSml,13,V(m1.x-pad-2-TextW(g_fSml,13,cnt),m0.y+pad+1),WithA(COL_INK2,al),cnt); }

    const char* tip=nullptr; std::string tipStore; float tipCX=0, tipCY=0;
    if(n==0) TextAt(dl,g_fSml,14,V(m0.x+pad+2,m0.y+pad+headH+6),WithA(COL_INK2,al),
                    g_trayCollapse?"Every icon is in the taskbar":"No tray icons");
    for(int i=0;i<n;i++){
        int r=i/cols, c=i%cols;
        float sa=EaseOutCubic(Stagger(af,i,0.03f,0.5f));
        float cx=m0.x+pad+c*cell+cell*0.5f, cy=m0.y+pad+headH+r*cell+cell*0.5f+(1.0f-sa)*7.0f;
        bool hov=io.MousePos.x>cx-cell*0.5f&&io.MousePos.x<cx+cell*0.5f&&
                 io.MousePos.y>cy-cell*0.5f&&io.MousePos.y<cy+cell*0.5f;
        float ha=HoverAnim(8200+i,hov);
        if(ha>0.01f) dl->AddRectFilled(V(cx-cell*0.5f+3,cy-cell*0.5f+3),V(cx+cell*0.5f-3,cy+cell*0.5f-3),
                                       WithA(COL_INK2,(int)(ha*52*af)),10);
        float isz=26+ha*3;
        dl->AddImage((ImTextureID)items[i].tex,V(cx-isz*0.5f,cy-isz*0.5f),V(cx+isz*0.5f,cy+isz*0.5f),
                     ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(al*sa)));
        // pin badge — the only way to promote an icon that does not steal the app's right-click
        ImVec2 pc=V(cx+cell*0.5f-10,cy-cell*0.5f+10); float pr=8.0f;
        bool ph=hov && (io.MousePos.x-pc.x)*(io.MousePos.x-pc.x)+(io.MousePos.y-pc.y)*(io.MousePos.y-pc.y) < pr*pr*1.6f;
        if(ha>0.01f){
            dl->AddCircleFilled(pc,pr,WithA(ph?COL_GOLD:IM_COL32(40,38,48,255),(int)(ha*235*af)));
            ImU32 pk=WithA(ph?IM_COL32(18,18,22,255):COL_INK,(int)(ha*255*af));
            dl->AddLine(V(pc.x,pc.y+3.5f),V(pc.x,pc.y-1.0f),pk,1.7f);      // a pin: shaft + head
            dl->AddLine(V(pc.x-3.2f,pc.y-1.5f),V(pc.x+3.2f,pc.y-1.5f),pk,1.7f);
            dl->AddLine(V(pc.x-2.0f,pc.y-4.0f),V(pc.x+2.0f,pc.y-4.0f),pk,1.7f);
        }
        if(hov){
            if(ph) tip="Keep in the taskbar";
            else if(!items[i].tip.empty()){ tipStore=W2U8(items[i].tip);
                size_t nl=tipStore.find(0x0A); if(nl!=std::string::npos) tipStore=tipStore.substr(0,nl);
                tip=tipStore.c_str(); }
            tipCX=cx; tipCY=cy;
            int sx=(int)(g_vs.left+cx*g_uiScale), sy=(int)(g_vs.top+cy*g_uiScale);
            if(mclick || (click&&ph)){ TrayToggleShown(items[i]); }
            else if(click){ TrayForward(items[i],WM_LBUTTONDOWN,WM_LBUTTONUP,sx,sy); g_trayOpen=false; }
            else if(rclick){ TrayForward(items[i],WM_RBUTTONDOWN,WM_RBUTTONUP,sx,sy); g_trayOpen=false; }
        }
    }
    dl->PopClipRect();
    if(tip){ float tw=TextW(g_fSml,14,tip)+20,th=28;
        float tx=std::clamp(tipCX-tw*0.5f,m0.x,m1.x-tw), ty=tipCY-cell*0.5f-th-4;
        if(ty<m0.y+2) ty=tipCY+cell*0.5f+4;
        dl->AddRectFilled(V(tx,ty),V(tx+tw,ty+th),IM_COL32(30,28,36,247),8);
        dl->AddRect(V(tx,ty),V(tx+tw,ty+th),IM_COL32(255,255,255,22),8,0,1.0f);
        TextAt(dl,g_fSml,14,V(tx+10,ty+6),IM_COL32(232,230,238,255),tip); }

    // The panel must be inside the window's hit region or it gets no mouse messages at all -
    // the same closed loop that made the notification stack permanently click-through.
    hit.left  =(LONG)std::min((float)hit.left,  m0.x); hit.top   =(LONG)std::min((float)hit.top,   m0.y);
    hit.right =(LONG)std::max((float)hit.right, m1.x); hit.bottom=(LONG)std::max((float)hit.bottom,m1.y);
    bool inFly=io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>m0.y&&io.MousePos.y<m1.y;
    bool onStrip=io.MousePos.x>=hit.left&&io.MousePos.x<=hit.right&&io.MousePos.y>=hit.top&&io.MousePos.y<=hit.bottom&&!inFly;
    // Click-outside closes. The chevron itself is on the strip and does its own toggle, so a strip
    // click is left alone here - otherwise the click that opens the panel would also close it.
    if(g_trayOpen && (click||rclick) && !inFly && !onStrip) g_trayOpen=false;
}

// Small morphing Wi-Fi / Bluetooth flyout that pops from a dock system icon (Caelestia-style).
// Grows open (height eases up so it "morphs to fit"), lists networks/devices, closes on outside click.
static void SysFlyout(ImDrawList* dl, ImGuiIO& io, int mi, RECT& hit){
    if(g_barFlyMon!=mi) return;
    bool click=io.MouseClicked[0];
    float open=(g_barFly!=0)?1.0f:0.0f;
    static int shownType=0; if(g_barFly) shownType=g_barFly;   // keep drawing the last popout while it morphs closed
    int ty3=shownType;                                 // 1 wifi, 2 bluetooth, 3 ethernet, 5 tray menu
    bool wifi=(ty3==1), bt=(ty3==2), eth=(ty3==3), trayM=(ty3==5);
    // tray menu (type 5): items scraped from the app; measure to fit the longest item + count.
    std::vector<TrayMenuItem> tItems; HWND tOwner=nullptr; const float trH=30, trPad=8;
    if(trayM){ std::lock_guard<std::mutex> lk(g_trayMenuMtx); auto mit=g_trayMenuCache.find(g_trayMenuKey);
        if(mit!=g_trayMenuCache.end()){ tItems=mit->second.items; tOwner=mit->second.owner; } }
    int nr = wifi? (int)std::min(g_wifi.size(),(size_t)6) : bt? (int)std::min(g_bt.size(),(size_t)6) : (int)std::min(g_eth.size(),(size_t)6);
    float titleH=44, togH=bt?66:(eth?24:0), listH= wifi? (nr>0? nr*48.0f+36.0f : 70.0f) : (nr>0?nr*40.0f:30.0f), btnH=48;
    float mw, mh;
    if(trayM){ float wmax=150; for(auto&it:tItems) if(!it.sep){ float w=TextW(g_fSml,15,W2U8(it.text).c_str())+52; if(w>wmax)wmax=w; }
        mw=std::min(380.0f,wmax); mh=trPad*2; for(auto&it:tItems) mh+= it.sep?9.0f:trH; if(mh<40)mh=40; }
    else { mw=274; mh=titleH+togH+listH+btnH+12; }
    // === MORPH, exactly like Caelestia's popout Wrapper.qml ===
    //   implicitHeight: <active popout content height>;  Behavior on implicitHeight { Anim { expressiveDefaultSpatial } }
    // The panel HEIGHT springs toward the ACTIVE popout's content height. Hovering wifi->bt->eth re-targets
    // this, so the panel RESIZES (springs) between the two sizes instead of the old fade+grow-up. The anchor
    // Y springs too so it glides between the icons; opacity is a SEPARATE quick effects fade (crossfade feel).
    float panelH = MotionAnim(MP_POPOUT,810020, open>0.5f? mh : 0.0f);
    float panelW = MotionAnim(MP_POPOUT,810021, open>0.5f? mw : 0.0f);  // width grows OUT of the bar
    float anchorY= Cael::anim(810023, g_barFlyAt.y,          Cael::DUR_FAST_SPATIAL,    Cael::FAST_SPATIAL);
    float af=std::clamp(Cael::anim(810022, open, Cael::DUR_DEFAULT_EFFECTS, Cael::DEFAULT_EFFECTS),0.0f,1.0f);
    int al=(int)(af*255); g_barFlyAnim=af;
    if(panelH<2.0f && open<0.5f){ shownType=0; g_flyCut=false; return; }
    // Reference-correct geometry (see 1.png/2.png): the popout emerges from the strip's RIGHT edge and
    // grows OUT to the right. The bar's icon COLUMN stays fully visible in the thin strip to the LEFT of
    // the panel — the panel never covers it. To read as ONE continuous dark surface (no floating-card
    // seam), the panel tucks a few px UNDER the strip's right edge (stripMerge): that sliver of overlap
    // swallows BarGlass's inner/outer border + edge shadow exactly in the panel's vertical span, so the
    // strip fill and the panel fill butt together with nothing between them. Above/below the panel the
    // strip stays its normal thin self; the SHOULDER FILLETS below blend that junction so the strip
    // visibly SWELLS into the panel. Only the desktop-facing (right) corners are rounded.
    // Morph: at panelW=0 the panel is a thin sliver at the strip edge; width+height spring outward => the
    // bar appears to bulge/widen to the right. originX is the fixed right-edge origin at the strip.
    const float mergeOverlap = 18.0f, stripMerge = 6.0f;
    bool leftBar = (g_barStripR>g_barStripL && g_barFlyAt.x >= g_barStripL);
    float attX   = leftBar ? (g_barStripR - stripMerge) : (g_barFlyAt.x - mergeOverlap);
    float originX= leftBar ?  g_barStripR               :  g_barFlyAt.x;
    ImVec2 m0=V(attX, anchorY-panelH), m1=V(originX + panelW, anchorY);
    // Publish the rect so the DESKTOP layer carves a matching notch out of the wallpaper (wallpaper curves
    // around the popout => it looks CUT INTO the desktop, not floating on top).
    g_flyCut=true; g_flyL=m0.x; g_flyT=m0.y; g_flyR=m1.x; g_flyB=m1.y; g_flyRad=16.0f;
    // Draw it in the SAME solid material as the bar (not a frosted floating card). NO left/top hairline —
    // a border there is exactly what made it read as a detached card. The wallpaper-facing edge is defined
    // by the desktop NOTCH lip (drawn on the desk layer); the bar-facing edge is seamless (same fill).
    // Reference look (1.png/2.png): the popout is a clean rounded-right rectangle stepping straight OUT of
    // the strip — the strip stays thin above and below it. NO shoulder fillets: the old convex coves drew a
    // rounded nub above the panel top with wallpaper showing in the cove, which read exactly as a "floating
    // card with rounded left corner + gap". The panel's left edge is tucked stripMerge px into the OPAQUE
    // strip, so across the panel's whole vertical span the strip fill and panel fill are one continuous dark
    // surface with nothing between them. Left corners square (merge), only the desktop-facing right rounded.
    ImU32 barFill = g_darkUI ? IM_COL32(18,18,22,255) : PanelCol(255);
    dl->AddRectFilled(m0,m1,barFill,16,ImDrawFlags_RoundCornersRight);
    dl->PushClipRect(V(m0.x-1,m0.y-1),V(m1.x+1,m1.y+1),true);
    auto toggle=[&](float tx,float ty,bool on){ float tw=40,th=22;
        dl->AddRectFilled(V(tx,ty),V(tx+tw,ty+th),on?AccA(al):WithA(COL_TRACK,al),th*0.5f);
        float kx=on? tx+tw-th*0.5f : tx+th*0.5f; dl->AddCircleFilled(V(kx,ty+th*0.5f),th*0.5f-3,WithA(IM_COL32(252,254,253,255),al)); };
    if(trayM){
        // ---- the app's REAL menu items (scraped), rendered in OUR style, morphing out of the bar ----
        float ry=m0.y+trPad;
        for(size_t i=0;i<tItems.size();i++){ auto&it=tItems[i];
            if(it.sep){ dl->AddLine(V(m0.x+12,ry+4),V(m1.x-12,ry+4),WithA(COL_INK2,(int)(al*0.35f)),1); ry+=9; continue; }
            std::string t=W2U8(it.text); std::string disp;                 // strip &-accelerators and \t shortcuts
            for(size_t c=0;c<t.size();c++){ if(t[c]=='\t')break; if(t[c]=='&'&&c+1<t.size())continue; disp+=t[c]; }
            bool rh=io.MousePos.x>m0.x+6&&io.MousePos.x<m1.x-6&&io.MousePos.y>ry&&io.MousePos.y<ry+trH;
            if(rh && !it.disabled) dl->AddRectFilled(V(m0.x+6,ry+1),V(m1.x-6,ry+trH-1),AccA((int)(af*30)),8);
            TextAt(dl,g_fSml,15,V(m0.x+18,ry+7),WithA(it.disabled?COL_INK2:COL_INK,al),Clip(g_fSml,15,disp,mw-28).c_str());
            if(it.sub) TextAt(dl,g_fSml,15,V(m1.x-18,ry+7),WithA(COL_INK2,al),">");   // submenu arrow
            if(click&&rh&&!it.disabled&&it.id){ TrayInvoke(tOwner,it.id); g_barFly=0; }
            ry+=trH;
        }
        dl->PopClipRect();
    } else {
    float y=m0.y+13;
    TextAt(dl,g_fMed,18,V(m0.x+16,y),WithA(COL_INK,al),wifi?"Wireless":bt?"Bluetooth":"Ethernet");
    if(!eth) toggle(m1.x-56,y-1, wifi? g_st.online : true);
    if(wifi && click && io.MousePos.x>=m1.x-56 && io.MousePos.x<m1.x-16 && io.MousePos.y>=y-1 && io.MousePos.y<y+21) RadioToggleAsync(0);   // the switch turns Wi-Fi on / off
    y+=titleH-6;
    if(bt){ TextAt(dl,g_fSml,15,V(m0.x+16,y),WithA(COL_INK,al),"Enabled");     toggle(m1.x-56,y-2,true);  y+=32;
            TextAt(dl,g_fSml,15,V(m0.x+16,y),WithA(COL_INK,al),"Discovering"); toggle(m1.x-56,y-2,false); y+=34; }
    if(eth){ char dv[32]; snprintf(dv,32,"%d device%s available",nr,nr==1?"":"s");
             TextAt(dl,g_fSml,14,V(m0.x+16,y),WithA(COL_INK2,al),dv); y+=24; }
    if(wifi){ DrawWifiList(dl,io,m0.x+8,y-4,m1.x-8,1.0f,6,48,click,al,0x7F400); } else {
    if(nr==0) TextAt(dl,g_fSml,14,V(m0.x+16,y+4),WithA(COL_INK2,al),wifi?"No networks found":bt?"0 devices available":"No wired devices");
    for(int i=0;i<nr;i++){ std::string nm=wifi?g_wifi[i].ssid:bt?g_bt[i].name:g_eth[i].name; bool cn=wifi?g_wifi[i].connected:bt?g_bt[i].connected:g_eth[i].up;
        float ry=y+i*40; bool rh=io.MousePos.x>m0.x+8&&io.MousePos.x<m1.x-8&&io.MousePos.y>ry-3&&io.MousePos.y<ry+31;
        if(cn) dl->AddRectFilled(V(m0.x+8,ry-3),V(m1.x-8,ry+31),AccA((int)(44*af)),10);
        else if(rh) dl->AddRectFilled(V(m0.x+8,ry-3),V(m1.x-8,ry+31),WithA(COL_INK2,(int)(30*af)),10);
        TextAt(dl,g_fSml,15,V(m0.x+18,ry+ (cn&&!eth?1.0f:6.0f)),WithA(COL_INK,al),Clip(g_fSml,15,nm,mw-96).c_str());  // always white for contrast on the accent wash
        if(cn&&!eth) TextAt(dl,g_fSml,11,V(m0.x+18,ry+18),WithA(AccBright(),al),"Connected");
        if(wifi){ int bars=g_wifi[i].signal>75?4:g_wifi[i].signal>50?3:g_wifi[i].signal>25?2:1;
            for(int s=0;s<4;s++){ float bh2=4+s*3.0f; dl->AddRectFilled(V(m1.x-42+s*7,ry+16-bh2),V(m1.x-42+s*7+4,ry+16),WithA(s<bars?(cn?COL_GOLD:COL_INK):COL_TRACK,al),1); } }
        if(eth){ ImVec2 lc=V(m1.x-30,ry+14); ImU32 lk=WithA(cn?COL_GOLD:COL_INK2,al);   // link / broken-link glyph
            dl->AddLine(V(lc.x-6,lc.y+4),V(lc.x+1,lc.y-3),lk,2); dl->AddLine(V(lc.x-1,lc.y+3),V(lc.x+6,lc.y-4),lk,2);
            if(!cn) dl->AddLine(V(lc.x-7,lc.y-7),V(lc.x+7,lc.y+7),WithA(COL_INK2,al),1.6f); }
        if(click&&rh&&wifi&&!cn) WifiConnect(nm); }
    }
    float by=m0.y+mh-btnH+8; ImVec2 b0=V(m0.x+12,by),b1=V(m1.x-12,by+34);
    bool bh=io.MousePos.x>b0.x&&io.MousePos.x<b1.x&&io.MousePos.y>b0.y&&io.MousePos.y<b1.y;
    dl->AddRectFilled(b0,b1,WithA(COL_INK2,(int)((bh?64:34)*af)),10);
    const char* bl=wifi?(g_wifiScanning?"Scanning\xE2\x80\xA6":"Scan for networks"):"Open settings";
    TextAt(dl,g_fSml,14,V((b0.x+b1.x)*0.5f-TextW(g_fSml,14,bl)/2,by+8),WithA(COL_INK,al),bl);
    if(click&&bh){ if(wifi) RefreshWifi(); else AetherShellExec(nullptr,L"open",bt?L"ms-settings:bluetooth":L"ms-settings:network-ethernet",nullptr,nullptr,SW_SHOWNORMAL); }
    dl->PopClipRect();
    }
    // the icon that opened us lives in the bar column — a click there must NOT count as an
    // "outside" click, or the very frame that opens the flyout would immediately close it.
    RECT bar=hit;
    bool inBar=io.MousePos.x>=bar.left&&io.MousePos.x<=bar.right&&io.MousePos.y>=bar.top&&io.MousePos.y<=bar.bottom;
    hit.left=(LONG)std::min((float)hit.left,m0.x); hit.top=(LONG)std::min((float)hit.top,m0.y);
    hit.right=(LONG)std::max((float)hit.right,m1.x); hit.bottom=(LONG)std::max((float)hit.bottom,m1.y);
    bool inFly=io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>m0.y&&io.MousePos.y<m1.y;
    if(inFly) g_barFlyHoverTick=GetTickCount64();          // cursor in the panel keeps it alive
    if(g_barFlyPin!=0 && g_barFlyPin!=g_barFly) g_barFlyPin=0;   // pin only applies to the panel that's showing
    bool pinned = (g_barFlyPin!=0 && g_barFlyPin==g_barFly);
    if(g_barFly){
        if(pinned){   // pinned by a click: only an outside click dismisses it
            if((click||io.MouseClicked[1]) && !inFly && !inBar){ g_barFly=0; g_barFlyPin=0; }
        } else {      // hover-open: fade out shortly after the cursor leaves both icon and panel
            if(GetTickCount64()-g_barFlyHoverTick > 190) g_barFly=0;
        }
    }
}
