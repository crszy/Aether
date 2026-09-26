// Aether - notifications.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =============================================================================================
// NOTIFICATIONS — the stack of cards on the right.
// Every card owns a spring for its slot, so dismissing one makes the rest glide up to close the
// gap instead of teleporting; the panel itself rides the Material spatial curve from the loop.
// =============================================================================================

// stable per-app accent: the app name hashes to a hue, so Discord / Steam / mail each keep their
// own recognisable colour instead of every badge being the same accent disc.
static ImU32 NotifHue(const std::string& app,float sat,float val,int alpha){
    uint32_t h=2166136261u; for(unsigned char c:app){ h^=(uint32_t)tolower(c); h*=16777619u; }
    float H=(h%360)/60.0f, S=sat, Vv=val;
    int i=(int)floorf(H); float f=H-i, p=Vv*(1-S), q=Vv*(1-S*f), t=Vv*(1-S*(1-f));
    float r,g,b;
    switch(i%6){ case 0:r=Vv;g=t;b=p;break; case 1:r=q;g=Vv;b=p;break; case 2:r=p;g=Vv;b=t;break;
                 case 3:r=p;g=q;b=Vv;break; case 4:r=t;g=p;b=Vv;break; default:r=Vv;g=p;b=q; }
    return IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),alpha);
}
// "now" / "4m" / "2h" — age since we first saw it (0 = it predates this shell session)
static std::string NotifAge(ULONGLONG at){
    if(!at) return "";
    ULONGLONG s=(GetTickCount64()-at)/1000;
    char b[24];
    if(s<45) return "now";
    if(s<3600){ snprintf(b,24,"%llum",s/60); return b; }
    snprintf(b,24,"%lluh",s/3600); return b;
}
// greedy word wrap to `w` pixels, at most `maxLines` lines, ellipsis on the last one
static void WrapLines(ImFont* f,float sz,const std::string& in,float w,int maxLines,std::vector<std::string>& out){
    std::string src=in; for(auto& c:src) if(c=='\n'||c=='\r') c=' ';
    size_t i=0;
    while(i<src.size() && (int)out.size()<maxLines){
        while(i<src.size()&&src[i]==' ') i++;
        if(i>=src.size()) break;
        size_t brk=std::string::npos, j=i;
        std::string line;
        while(j<src.size()){
            size_t sp=src.find(' ',j+1); if(sp==std::string::npos) sp=src.size();
            std::string cand=src.substr(i,sp-i);
            if(TextW(f,sz,cand.c_str())>w){ break; }
            line=cand; brk=sp; j=sp;
        }
        if(line.empty()){                                  // one word wider than the card: hard-cut it
            size_t k=i+1; while(k<src.size() && TextW(f,sz,src.substr(i,k-i).c_str())<=w) k++;
            line=src.substr(i,std::max<size_t>(1,k-i-1)); brk=i+line.size();
        }
        bool last=((int)out.size()==maxLines-1);
        if(last && brk<src.size()) line=Clip(f,sz,src.substr(i),w);
        out.push_back(line);
        i=(brk==std::string::npos)? src.size() : brk;
    }
}

#include "src/modules/sidebar/SidebarV2.h"   // the merged Caelestia sidebar
#include "src/modules/audio/AudioMixer.h"     // outputs / inputs / apps / Voicemeeter mixer
#include "src/modules/record/RecordHud.h"     // the flying recording checker
static void DrawNotifications(){
    // Rebuilt from the caelestia-aw demo. What changed and why:
    //  * grouped by app, newest first - 35 identical "Call Check" cards read as "dismiss does nothing"
    //  * the app's REAL icon on a soft chip, "App  - now" on top, the message under it; no letter avatars,
    //    colour stripes, breathing rims or glow - the old card was noise
    //  * dismiss animates out (fade + slide right + collapse) and the rest glide up; the panel's height
    //    follows its content smoothly instead of snapping
    //  * one surface with the border, merging with quick settings below and the dashboard beside it
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x, H=io.DisplaySize.y;
    float dt=std::min(g_frameDt,0.05f);
    float rvRaw=std::clamp(g_notifReveal,0.0f,1.0f);
    g_nfPanelVis=rvRaw;
    if(rvRaw<0.002f){ g_notifRect=RECT{0,0,0,0}; g_notifStackBot=0; g_nfPanelOn=false; return; }
    if(g_sideStyle==1 && g_toastStyle==1){ DrawToastsV2(rvRaw); return; }     // caelestia toasts
    std::vector<Notif> snap; { std::lock_guard<std::mutex> lk(g_notifMtx); snap=g_notifs; }
    { std::vector<std::pair<std::string,std::vector<uint8_t>>> pend;
      { std::lock_guard<std::mutex> lk(g_nIconMtx); pend.swap(g_nIconBytes); }
      int aw=g_mdArtW, ah=g_mdArtH;                         // the decoder records media-art aspect; keep it
      for(auto& p:pend){ ID3D11ShaderResourceView* t=DecodeBufferToTexture(p.second.data(),p.second.size());
          std::lock_guard<std::mutex> lk(g_nIconMtx); g_nIcons[p.first]=t; }
      g_mdArtW=aw; g_mdArtH=ah; }
    { static std::unordered_map<std::string,bool> shellTried;              // no logo from the listener: ask Start's Apps folder
      for(auto& n:snap){ std::string key=n.aumid.empty()? n.app : n.aumid;
          if(n.aumid.empty() || shellTried.count(key)) continue;
          bool none; { std::lock_guard<std::mutex> lk(g_nIconMtx); auto it=g_nIcons.find(key); none = it!=g_nIcons.end() && !it->second; }
          if(!none) continue;
          shellTried[key]=true;
          ID3D11ShaderResourceView* t=AumidIconTex(U82W(n.aumid),96);
          if(!t){
              // Not in Start either (Flow Launcher registers "Flow.Launcher" with no shortcut). The sender is
              // almost always running: match its process by the id or the display name and use the exe's icon.
              auto norm=[](std::wstring w){ std::wstring o; for(wchar_t c:w) if(c!=L' '&&c!=L'-'&&c!=L'_') o+=(wchar_t)towlower(c);
                                           if(o.size()>4 && o.compare(o.size()-4,4,L".exe")==0) o.resize(o.size()-4); return o; };
              std::vector<std::wstring> want={ norm(U82W(n.aumid)), norm(U82W(n.app)) };
              { std::wstring a=U82W(n.aumid); size_t d=a.find_last_of(L".!\\"); if(d!=std::wstring::npos && d+1<a.size()) want.push_back(norm(a.substr(d+1))); }
              std::wstring exe;
              HANDLE ps=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
              if(ps!=INVALID_HANDLE_VALUE){ PROCESSENTRY32W pe{sizeof(pe)};
                  if(Process32FirstW(ps,&pe)) do{
                      std::wstring nm=norm(pe.szExeFile); bool hit=false;
                      for(auto& w:want) if(w.size()>=3 && w==nm){ hit=true; break; }
                      if(hit){ HANDLE ph=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pe.th32ProcessID);
                          if(ph){ wchar_t p[MAX_PATH]; DWORD cn=MAX_PATH; if(QueryFullProcessImageNameW(ph,0,p,&cn)) exe=p; CloseHandle(ph); }
                          if(!exe.empty()) break; }
                  } while(Process32NextW(ps,&pe));
                  CloseHandle(ps); }
              if(!exe.empty()) t=GetAppIconHi(nullptr,exe);
          }
          if(t){ std::lock_guard<std::mutex> lk(g_nIconMtx); g_nIcons[key]=t; } } }

    // cards dismissed here vanish at once, whatever the next poll says for the next few seconds
    static std::unordered_map<uint32_t,ULONGLONG> gone;
    { ULONGLONG now=GetTickCount64();
      for(auto it=gone.begin(); it!=gone.end();) it = (now-it->second>6000)? gone.erase(it) : std::next(it);
      snap.erase(std::remove_if(snap.begin(),snap.end(),[&](const Notif& n){ return gone.count(n.id)>0; }),snap.end()); }

    struct Grp{ std::string key, app, aumid; std::vector<Notif> items; };
    std::vector<Grp> groups;
    for(auto& n:snap){
        std::string key=n.app.empty()? n.aumid : n.app;
        Grp* g=nullptr; for(auto& x:groups) if(x.key==key){ g=&x; break; }
        if(!g){ groups.push_back({key,n.app,n.aumid,{}}); g=&groups.back(); }
        g->items.push_back(n);
    }

    const bool born = FrameBornOn() && !g_mons.empty();   // the toast yields to the panel (see g_medOpening), so no special case
    int mi=FrameMon();
    float bT = born? FrameInset(EDGE_TOP) : 10.0f, bR = born? FrameInset(EDGE_RIGHT) : 10.0f;
    float MWl=(float)g_mw/g_uiScale;
    float toMon = MWl-W;                                     // notif-window x -> monitor x
    // match quick settings' width when it hangs underneath, so the column is one straight shape
    float panelL = 0.0f;
    if(born && g_qsPanelOn) panelL = std::clamp(g_qsPanelL-toMon, 0.0f, W-200.0f);
    static float s_panelL=-1; if(s_panelL<0) s_panelL=panelL; s_panelL += (panelL-s_panelL)*std::min(1.0f,dt*14.0f);
    const float PAD=12.0f;
    float cx0=s_panelL+PAD, cx1=W-bR-PAD, cardW=cx1-cx0;
    float yTop = born? bT+PAD : (g_medShowing? g_toastStackH+6.0f : 16.0f);

    bool click=io.MouseClicked[0];
    static std::unordered_map<std::string,bool> expToggled;       // a card the user opened/closed (vs the default)
    auto isExpanded=[&](const std::string& k){ bool t=expToggled.count(k)? expToggled[k] : false;
                                               return g_nfExpandable && (g_nfExpandDefault != t); };
    RECT hit={0,0,0,0}; bool anyHit=false;
    auto addHit=[&](ImVec2 a,ImVec2 b){
        if(!anyHit){ hit=RECT{(LONG)a.x,(LONG)a.y,(LONG)b.x,(LONG)b.y}; anyHit=true; return; }
        hit.left=std::min(hit.left,(LONG)a.x); hit.top=std::min(hit.top,(LONG)a.y);
        hit.right=std::max(hit.right,(LONG)b.x); hit.bottom=std::max(hit.bottom,(LONG)b.y); };

    // ---- per-card animation state -------------------------------------------------------------------
    struct CS { float y=-1, h=0, appear=0, leave=0; bool leaving=false; Grp g; ULONGLONG seen=0;
                float dragX=0, leaveDir=1, leaveFromX=0; };
    static std::map<std::string,CS> cs;
    ULONGLONG nowT=GetTickCount64();
    for(auto& g:groups){ CS& c=cs[g.key]; c.g=g; c.seen=nowT; c.leaving=false; }
    for(auto& kv:cs) if(kv.second.seen!=nowT && !kv.second.leaving){ kv.second.leaving=true; }

    // ---- layout (target positions) ----
    const float ROWH=64.0f, SUBH=40.0f, GAP=8.0f;
    std::vector<std::string> order; for(auto& g:groups) order.push_back(g.key);
    for(auto& kv:cs) if(kv.second.leaving && std::find(order.begin(),order.end(),kv.first)==order.end()){
        // a leaving card keeps its place until it has collapsed
        float y=kv.second.y; size_t at=order.size();
        for(size_t i=0;i<order.size();i++) if(cs[order[i]].y>y){ at=i; break; }
        order.insert(order.begin()+at,kv.first); }

    // the header stays while there is anything to show and eases in/out - it used to vanish in one frame
    // when the second-to-last group was dismissed, jerking every card up by 30px
    static float s_headH=-1; { float want = groups.empty()? 0.0f : 30.0f; if(s_headH<0) s_headH=want;
                               s_headH += (want-s_headH)*std::min(1.0f,dt*14.0f); }
    float headH = s_headH;
    float y=yTop+headH;
    const float wrapW = std::max(80.0f, cx1-cx0-66.0f-40.0f);
    auto bodyOf=[](const Notif& n){ std::string b=n.body; for(auto& ch:b) if(ch=='\r') ch=' '; return b; };
    for(auto& k:order){
        CS& c=cs[k];
        bool exp = isExpanded(k) && !c.g.items.empty();
        float want = ROWH;
        if(exp){
            const Notif& top=c.g.items.front();
            std::vector<std::string> bl; std::string bd=bodyOf(top);
            if(!bd.empty()){ std::string one=bd; for(auto& ch:one) if(ch=='\n') ch=' '; WrapLines(g_fSml,14,one,wrapW,7,bl); }
            float y0 = top.title.empty()? 36.0f : 56.0f;
            want = y0 + (float)bl.size()*19.0f + 14.0f;
            want = std::max(want, (float)ROWH);
            want += (float)std::min<size_t>(c.g.items.size()-1,5)*SUBH;
        }
        if(c.leaving){ c.leave=std::min(1.0f,c.leave+dt*1000.0f/std::max(60,g_nfDismissMs));
                       want*= (1.0f-Cael::eval(Cael::EMPHASIZED_DECEL,std::min(1.0f,c.leave*1.3f))); }
        else { c.appear=std::min(1.0f,c.appear+dt*1000.0f/std::max(60,g_nfArriveMs)); }
        if(c.y<0){ c.y=y; c.h=want; }
        c.y += (y-c.y)*std::min(1.0f,dt*16.0f);
        c.h += (want-c.h)*std::min(1.0f,dt*18.0f);
        y += c.h + (c.leaving? GAP*(1.0f-c.leave) : GAP);
    }
    // a card that has finished collapsing leaves the layout AND the draw order in the same frame. (It used to
    // stay in `order`; cs[k] below then re-created it EMPTY, and the next frame drew a card with no
    // notifications in it - a null read that crashed the shell the moment a dismissed card finished.)
    for(auto it=cs.begin(); it!=cs.end();){
        if(it->second.leaving && it->second.leave>=1.0f){
            order.erase(std::remove(order.begin(),order.end(),it->first),order.end());
            it=cs.erase(it);
        } else ++it; }
    float contentB = y-GAP+PAD;
    if(order.empty()) contentB = yTop;

    // ---- the panel: height follows the content, the reveal grows it out of the border ----
    static float s_contentB=-1; if(s_contentB<0) s_contentB=contentB;
    s_contentB += (contentB-s_contentB)*std::min(1.0f,dt*14.0f);
    float rv=std::min(g_notifReveal,1.3f);
    float yBot = bT + (s_contentB-bT)*rv;
    bool mergeBelow = born && g_qsPanelOn && g_qsPanelT<=s_contentB+2.0f;
    if(born){
        FrameCornerPanel(dl,mi,V(-toMon,0),V(W,H),s_panelL,W,yBot,bT,bR,g_panelRound,1.0f,mergeBelow,
                         g_dashNeckOn? std::max(bT+0.5f,g_dashNeckDepth) : 0.0f);
        dl->PushClipRect(V(s_panelL,bT),V(W,std::max(bT,yBot)),true);
    }
    g_nfPanelOn = born && !order.empty();
    g_nfPanelL = s_panelL+toMon; g_nfPanelB = yBot;
    // the window region clips rendering: it must hold the whole panel and its shoulders, not just the cards
    if(born && yBot>bT+1.0f) addHit(V(s_panelL-g_panelRound+30.0f,0),V(W,yBot+g_panelRound-30.0f));

    // ---- header ----
    if(headH>0.5f){
        int ha=(int)(255*std::clamp(headH/30.0f,0.0f,1.0f));
        size_t total=0; for(auto& g:groups) total+=g.items.size();
        char hdr[48]; snprintf(hdr,48,"Notifications  \xC2\xB7  %zu",total);
        TextAt(dl,g_fMed,15,V(cx0+6,yTop+4),WithA(COL_INK2,(int)(ha*0.9f)),hdr);
        const char* ca="Clear all"; float cw=TextW(g_fSml,13,ca)+20, bx=cx1-cw, by=yTop;
        bool hov=io.MousePos.x>bx&&io.MousePos.x<cx1&&io.MousePos.y>by&&io.MousePos.y<by+24;
        float hv=HoverAnim(7250,hov);
        dl->AddRectFilled(V(bx,by),V(cx1,by+24),WithA(COL_INK,(int)((14+hv*30)*ha/255)),12);
        TextAt(dl,g_fSml,13,V(bx+10,by+4),WithA(COL_INK,(int)((190+hv*65)*ha/255)),ca);
        if(hov) addHit(V(bx,by),V(cx1,by+24));
        if(click&&hov){ for(auto& g:groups) for(auto& n:g.items){ gone[n.id]=nowT; NotifDismiss(n.id); } g_reqClearAll=1; }
    }

    // ---- cards ----
    auto surface=[&](int a){ return g_darkUI? IM_COL32(255,255,255,(int)(a*0.075f)) : IM_COL32(255,255,255,(int)(a*0.55f)); };
    static std::string dragKey; static float dragStartX=0, dragLastX=0, dragVel=0; static bool dragMoved=false;
    const float cardWd=cx1-cx0;
    auto dismissCard=[&](CS& c,float dir){
        for(auto& n:c.g.items){ gone[n.id]=nowT; NotifDismiss(n.id); }
        c.leaving=true; c.leave=0; c.leaveDir= dir<0? -1.0f : 1.0f; c.leaveFromX=c.dragX; };
    if(!io.MouseDown[0] && !io.MouseReleased[0]) { if(!dragKey.empty()) dragKey.clear(); }
    int ci=0;
    for(auto& k:order){
        auto cit=cs.find(k); if(cit==cs.end() || cit->second.g.items.empty()){ ci++; continue; }
        CS& c=cit->second; if(c.h<2.0f){ ci++; continue; }
        // ---- how it arrives / leaves ----
        float fa=1.0f, slideX=c.dragX, slideY=0.0f, scale=1.0f;
        if(c.leaving){
            float lt=Cael::eval(Cael::EMPHASIZED,c.leave);
            switch(g_nfDismissAnim){
            case 0: slideX=c.leaveFromX + c.leaveDir*lt*cardWd*1.6f; fa=1.0f-std::max(0.0f,lt-0.55f)/0.45f; break;   // slide
            case 1: slideX=c.leaveFromX*(1.0f-lt); fa=1.0f-lt; break;                                                  // fade
            default: slideX=c.leaveFromX*(1.0f-lt); scale=1.0f-0.25f*lt; fa=1.0f-lt; break;                            // shrink
            }
        } else {
            float ae=Cael::eval(Cael::EMPHASIZED_DECEL,c.appear);
            switch(g_nfArriveAnim){
            case 0: slideX+= (1.0f-ae)*cardWd; fa=std::min(1.0f,c.appear*3.0f); break;   // slide in from the right
            case 1: fa=ae; break;                                                         // fade
            case 2: scale=0.86f+0.14f*Cael::eval(Cael::FAST_SPATIAL,c.appear); fa=std::min(1.0f,c.appear*2.5f); break;
            default: break;
            }
            // not being dragged: a released card springs back to its place
            if(dragKey!=k) c.dragX += (0.0f-c.dragX)*std::min(1.0f,dt*16.0f);
        }
        fa=std::clamp(fa,0.0f,1.0f);
        ImVec2 a=V(cx0+slideX, c.y+slideY), b=V(cx1+slideX, c.y+slideY+c.h);
        if(scale!=1.0f){ ImVec2 ctr=V((a.x+b.x)*0.5f,(a.y+b.y)*0.5f); float hw=(b.x-a.x)*0.5f*scale, hh=(b.y-a.y)*0.5f*scale;
                         a=V(ctr.x-hw,ctr.y-hh); b=V(ctr.x+hw,ctr.y+hh); }
        // a card dragged away from its place fades with the distance, so the throw reads
        if(!c.leaving && fabsf(c.dragX)>1.0f) fa*=std::clamp(1.0f-fabsf(c.dragX)/(cardWd*1.1f),0.25f,1.0f);
        int A=(int)(255*fa);
        bool hov=!c.leaving && io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;
        float hv=HoverAnim(7200+ci,hov||dragKey==k);
        float rnd=std::min(26.0f,c.h*0.5f);
        dl->AddRectFilled(a,b,surface((int)(A*(1.0f+hv*0.6f))),rnd);
        dl->PushClipRect(a,b,true);
        const Notif& top=c.g.items.front();
        bool exp=isExpanded(k);
        // icon chip
        ImVec2 ic=V(a.x+34,a.y+32);
        dl->AddCircleFilled(ic,20.0f,g_darkUI? IM_COL32(255,255,255,(int)(A*0.09f)) : IM_COL32(0,0,0,(int)(A*0.05f)));
        ID3D11ShaderResourceView* tex=nullptr;
        { std::lock_guard<std::mutex> lk(g_nIconMtx); auto it=g_nIcons.find(c.g.aumid.empty()? c.g.app : c.g.aumid); if(it!=g_nIcons.end()) tex=it->second; }
        if(tex) dl->AddImageRounded((ImTextureID)tex,V(ic.x-13,ic.y-13),V(ic.x+13,ic.y+13),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,A),5.0f);
        else { std::string ini=c.g.app.empty()? "!" : c.g.app.substr(0,1); for(auto& ch:ini) ch=(char)toupper((unsigned char)ch);
               TextAt(dl,g_fMed,18,V(ic.x-TextW(g_fMed,18,ini.c_str())*0.5f,ic.y-12),WithA(COL_GOLD,A),ini.c_str()); }
        // text
        size_t cnt=c.g.items.size();
        float tx=a.x+66, rightPad = (cnt>1? 44.0f : 0.0f) + (g_nfExpandable? 34.0f : 0.0f) + 36.0f;
        std::string age=NotifAge(top.at);
        std::string appName=c.g.app.empty()? std::string("Notification") : c.g.app;
        std::string l1=Clip(g_fMed,16,appName,b.x-tx-rightPad-(age.empty()?0.0f:TextW(g_fSml,13,age.c_str())+24.0f));
        TextAt(dl,g_fMed,16,V(tx,a.y+12),WithA(COL_INK,A),l1.c_str());
        float l1w=TextW(g_fMed,16,l1.c_str());
        if(!age.empty()){
            TextAt(dl,g_fSml,13,V(tx+l1w+8,a.y+14),WithA(COL_INK2,A),"\xE2\x80\xA2");
            TextAt(dl,g_fSml,13,V(tx+l1w+20,a.y+14),WithA(COL_INK2,A),age.c_str()); }
        float detailEnd=a.y+ROWH;
        if(!exp){
            std::string msg = top.title;
            if(!top.body.empty()){ std::string b1=top.body; for(auto& ch:b1) if(ch=='\n'||ch=='\r') ch=' '; msg = msg.empty()? b1 : msg+"  \xE2\x80\x94  "+b1; }
            TextAt(dl,g_fSml,14,V(tx,a.y+36),WithA(COL_INK2,A),Clip(g_fSml,14,msg,b.x-tx-rightPad).c_str());
        } else {
            float yy=a.y+36;
            if(!top.title.empty()){ TextAt(dl,g_fMed,15,V(tx,yy),WithA(COL_INK,A),Clip(g_fMed,15,top.title,wrapW).c_str()); yy+=20; }
            std::string one=bodyOf(top); for(auto& ch:one) if(ch=='\n') ch=' ';
            std::vector<std::string> bl; if(!one.empty()) WrapLines(g_fSml,14,one,wrapW,7,bl);
            for(auto& ln:bl){ TextAt(dl,g_fSml,14,V(tx,yy),WithA(COL_INK2,A),ln.c_str()); yy+=19; }
            detailEnd=std::max(a.y+(float)ROWH, yy+14);
        }
        // right side: count badge, chevron, and the close button on hover
        ImVec2 rc=V(b.x-26,a.y+32);
        bool onButtons = io.MousePos.x>b.x-rightPad && io.MousePos.y<a.y+ROWH;
        if(hv>0.02f){
            bool xh = fabsf(io.MousePos.x-rc.x)<15 && fabsf(io.MousePos.y-rc.y)<15;
            dl->AddCircleFilled(rc,15,WithA(COL_INK,(int)((xh?46:24)*hv*fa)));
            ImU32 xc=WithA(COL_INK,(int)(230*hv*fa));
            dl->AddLine(V(rc.x-5,rc.y-5),V(rc.x+5,rc.y+5),xc,1.8f); dl->AddLine(V(rc.x-5,rc.y+5),V(rc.x+5,rc.y-5),xc,1.8f);
            if(click&&xh){ dismissCard(c,1.0f); }
        }
        float chevX = b.x-26-(hv>0.02f? 34.0f*hv : 0.0f);
        if(g_nfExpandable){
            ImVec2 cc=V(chevX,a.y+32);
            bool chh = !c.leaving && fabsf(io.MousePos.x-cc.x)<14 && fabsf(io.MousePos.y-cc.y)<14;
            if(chh) dl->AddCircleFilled(cc,13,WithA(COL_INK,(int)(30*fa)));
            float d=exp? -1.0f : 1.0f; ImU32 chc=WithA(COL_INK2,A);
            dl->AddLine(V(cc.x-5,cc.y-2.5f*d),V(cc.x,cc.y+2.5f*d),chc,1.7f);
            dl->AddLine(V(cc.x,cc.y+2.5f*d),V(cc.x+5,cc.y-2.5f*d),chc,1.7f);
            if(click&&chh){ expToggled[k] = !(expToggled.count(k)? expToggled[k] : false); }
        }
        if(cnt>1){
            ImVec2 cc=V((g_nfExpandable? chevX-30 : chevX),a.y+32);
            char nb[8]; snprintf(nb,8,"%zu",cnt);
            float nw=std::max(22.0f,TextW(g_fSml,12,nb)+12.0f);
            dl->AddRectFilled(V(cc.x-nw*0.5f,cc.y-11),V(cc.x+nw*0.5f,cc.y+11),WithA(COL_GOLD,(int)(A*0.85f)),11);
            TextAt(dl,g_fSml,12,V(cc.x-TextW(g_fSml,12,nb)*0.5f,cc.y-8),g_darkUI?IM_COL32(14,18,16,A):IM_COL32(250,252,251,A),nb);
        }
        // expanded group: the older messages under the newest
        if(exp && cnt>1){
            for(size_t j=1;j<cnt && j<=5;j++){
                const Notif& n=c.g.items[j];
                float ry=detailEnd+(j-1)*SUBH;
                if(ry>b.y) break;
                std::string m2=n.title; if(!n.body.empty()){ std::string b1=n.body; for(auto& ch:b1) if(ch=='\n'||ch=='\r') ch=' '; m2 = m2.empty()? b1 : m2+"  \xE2\x80\x94  "+b1; }
                dl->AddLine(V(tx,ry),V(b.x-18,ry),WithA(COL_INK2,(int)(A*0.18f)),1.0f);
                TextAt(dl,g_fSml,14,V(tx,ry+11),WithA(COL_INK,(int)(A*0.85f)),Clip(g_fSml,14,m2,b.x-tx-60).c_str());
                std::string ag=NotifAge(n.at);
                if(!ag.empty()) TextAt(dl,g_fSml,12,V(b.x-18-TextW(g_fSml,12,ag.c_str()),ry+12),WithA(COL_INK2,A),ag.c_str());
            }
        }
        dl->PopClipRect();
        // ---- pointer: middle-click dismiss, drag to flick, plain click opens the app ----
        if(!c.leaving){
            if(hov && g_nfMiddle && io.MouseClicked[2]) dismissCard(c,1.0f);
            else {
                if(hov && io.MouseClicked[0] && !onButtons){ dragKey=k; dragStartX=dragLastX=io.MousePos.x; dragVel=0; dragMoved=false; }
                if(dragKey==k && io.MouseDown[0]){
                    float dx=io.MousePos.x-dragStartX;
                    if(fabsf(dx)>6.0f) dragMoved=true;
                    if(g_nfFlick && dragMoved) c.dragX=dx;
                    float inst=(io.MousePos.x-dragLastX)/std::max(0.001f,dt); dragVel+=(inst-dragVel)*std::min(1.0f,dt*20.0f); dragLastX=io.MousePos.x;
                }
                if(dragKey==k && io.MouseReleased[0]){
                    if(dragMoved && g_nfFlick){
                        if(fabsf(c.dragX)>cardWd*0.30f || fabsf(dragVel)>1300.0f) dismissCard(c, c.dragX!=0? c.dragX : dragVel);
                    } else if(!dragMoved && !top.aumid.empty()){
                        ActivateAumid(top.aumid); dismissCard(c,1.0f); }
                    dragKey.clear();
                }
            }
        }
        if(!c.leaving) addHit(V(std::min(a.x,cx0),a.y),V(std::max(b.x,cx1),b.y));
        ci++;
    }
    if(born) dl->PopClipRect();
    g_notifStackBot = s_contentB;
    if(order.empty()) g_forceNotif=false;
    g_notifRect = anyHit? hit : RECT{0,0,0,0};
}

// "Now Playing" flyout — Medal-style: pops in on a new track, sits, slides back out.
// g_medOpening (target) drives which easing to use so the in/out animations differ.
static bool g_medOpening=false;

// The toast stack: now-playing on top, the screen-recording card under it. Both share ToastCard.
static void DrawMediaFlyout(){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x;
    bool click=io.MouseClicked[0];
    float y=12.0f; RECT hit={0,0,0,0}; bool any=false;
    auto grow=[&](ImVec2 a,ImVec2 b){
        if(!any){ hit=RECT{(LONG)a.x,(LONG)a.y,(LONG)b.x,(LONG)b.y}; any=true; }
        else { hit.left=(LONG)std::min((float)hit.left,a.x); hit.top=(LONG)std::min((float)hit.top,a.y);
               hit.right=(LONG)std::max((float)hit.right,b.x); hit.bottom=(LONG)std::max((float)hit.bottom,b.y); } };

    // ------------------------------------------------------------------ now playing
    // Rebuilt: it comes out of the top-right corner of the border like the notifications (or slides in as a
    // card with the frame look off), names the player, keeps a live progress bar and the transport always
    // visible, and - when the song has synced lyrics - sings the current line with the same glow as the
    // Media tab. Hovering holds it open; the pin keeps it up as a small lyrics player until unpinned.
    static bool s_medPinned=false;
    float ma=std::clamp(g_medAnim,0.0f,1.0f);
    if(ma>0.002f && g_md.has){
        LyricsMaybeFetch();
        ULONGLONG nowM=GetTickCount64();
        if(s_medPinned) g_medUntil=std::max(g_medUntil.load(),nowM+1500);
        ImU32 tint=g_mdTint.load(); if(!tint) tint=COL_GOLD;
        bool lyr = g_lyricsToast && g_lyricsState.load()==2;
        static float s_h=-1; { float want= lyr? 170.0f : 124.0f; if(s_h<0) s_h=want; s_h+=(want-s_h)*std::min(1.0f,g_frameDt*10.0f); }
        float p = std::min(g_medAnim,1.3f);
        const bool bornM = FrameBornOn() && !g_mons.empty();
        float H=io.DisplaySize.y;
        const float cardW=W-24;
        ImVec2 a,b; float al;
        if(bornM){
            int mi=FrameMon();
            float bT=FrameInset(EDGE_TOP), bR=FrameInset(EDGE_RIGHT);
            float MWl=(float)g_mw/g_uiScale, MHl=(float)g_mh/g_uiScale, toMon=MWl-W;
            float x0=W-bR-cardW;
            float yBot=bT+(s_h+16.0f)*p;
            FrameCornerPanel(dl,mi,V(-toMon,0),V(W,MHl),x0,W,yBot,bT,bR,g_panelRound,1.0f);
            dl->PushClipRect(V(x0,bT),V(W,std::max(bT,std::min(yBot,H))),true);
            a=V(x0+8,yBot-8-s_h); b=V(W-bR-8,yBot-8);                    // content rides the growing bottom edge
            al=std::clamp(ma*1.6f,0.0f,1.0f);
            grow(V(x0-g_panelRound,0),V(W,std::min(H,yBot+g_panelRound)));
            y=yBot+10.0f;
        } else {
            float x=(W-12-cardW)+(1.0f-p)*(cardW+40);
            a=V(x,12); b=V(x+cardW,12+s_h); al=std::clamp(ma,0.0f,1.0f);
            ToastCard(dl,a,b,al,tint,20.0f);
            grow(a,b);
            y=b.y+10;
        }
        bool hov=io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y-8&&io.MousePos.y<b.y+8;
        if(hov) g_medUntil=std::max(g_medUntil.load(),nowM+1500);
        auto A=[&](ImU32 c){ return MulA(c,al); };

        // ---- art ----
        const float artS=84.0f;
        ImVec2 ap=V(a.x+10,a.y+10), ae=V(ap.x+artS,ap.y+artS);
        for(int i=5;i>0;i--) dl->AddRectFilled(V(ap.x-i*0.5f,ap.y+i*0.9f),V(ae.x+i*0.5f,ae.y+i*1.1f),IM_COL32(0,0,0,(int)(11*al)),16.0f);
        if(g_mdArt) DrawArtFit(dl,ap,ae,16.0f,(int)(255*al),tint);
        else { dl->AddRectFilled(ap,ae,A(WithA(tint,70)),16.0f);
               ImVec2 nc=V((ap.x+ae.x)/2,(ap.y+ae.y)/2); ImU32 c2=A(tint);
               dl->AddCircleFilled(V(nc.x-8,nc.y+10),6,c2); dl->AddCircleFilled(V(nc.x+12,nc.y+5),6,c2);
               dl->AddRectFilled(V(nc.x-3,nc.y-16),V(nc.x,nc.y+11),c2,1); dl->AddRectFilled(V(nc.x+17,nc.y-20),V(nc.x+20,nc.y+6),c2,1);
               dl->AddRectFilled(V(nc.x-3,nc.y-20),V(nc.x+20,nc.y-14),c2,2); }

        // ---- eyebrow: little equaliser + state + player ----
        float tx=ae.x+14, tRight=b.x-10;
        { float bars[3]; float tt=(float)nowM*0.001f;
          for(int i=0;i<3;i++) bars[i]= g_md.playing? 0.35f+0.65f*fabsf(sinf(tt*(5.0f+i*1.7f)+i*1.3f)) : 0.3f;
          for(int i=0;i<3;i++){ float hgt=10.0f*bars[i]; dl->AddRectFilled(V(tx+i*4.5f,ap.y+13-hgt),V(tx+i*4.5f+3,ap.y+13),A(tint),1.2f); } }
        std::string eb = std::string(g_md.playing? "NOW PLAYING" : "PAUSED");
        if(!g_mdSource.empty()) eb += "  \xC2\xB7  " + g_mdSource;
        TextAt(dl,g_fSml,11,V(tx+18,ap.y+2),A(WithA(COL_INK2,220)),Clip(g_fSml,11,eb,tRight-tx-18-54).c_str());
        TextAt(dl,g_fMed,18,V(tx,ap.y+20),A(COL_INK),Clip(g_fMed,18,g_md.title,tRight-tx).c_str());
        TextAt(dl,g_fSml,14,V(tx,ap.y+44),A(WithA(COL_INK2,230)),Clip(g_fSml,14,g_md.artist,tRight-tx).c_str());

        // ---- pin + close (top right) ----
        { ImVec2 pc=V(b.x-46,a.y+16), xc=V(b.x-20,a.y+16);
          bool ph=fabsf(io.MousePos.x-pc.x)<11&&fabsf(io.MousePos.y-pc.y)<11, xh=fabsf(io.MousePos.x-xc.x)<11&&fabsf(io.MousePos.y-xc.y)<11;
          float vis = (hov||s_medPinned)? 1.0f : 0.0f;
          if(vis>0){
              if(ph||s_medPinned) dl->AddCircleFilled(pc,11,A(s_medPinned? WithA(tint,110) : WithA(COL_INK2,50)));
              ImU32 pcol=A(WithA(COL_INK,s_medPinned?255:200));      // pin: head + needle
              dl->AddCircleFilled(V(pc.x,pc.y-2.5f),3.8f,pcol); dl->AddLine(V(pc.x,pc.y+1),V(pc.x,pc.y+7),pcol,1.6f);
              dl->AddLine(V(pc.x-4.5f,pc.y+1),V(pc.x+4.5f,pc.y+1),pcol,1.6f);
              if(xh) dl->AddCircleFilled(xc,11,A(WithA(COL_INK2,50)));
              ImU32 xcol=A(WithA(COL_INK,200));
              dl->AddLine(V(xc.x-4,xc.y-4),V(xc.x+4,xc.y+4),xcol,1.6f); dl->AddLine(V(xc.x+4,xc.y-4),V(xc.x-4,xc.y+4),xcol,1.6f);
              if(click&&ph) s_medPinned=!s_medPinned;
              if(click&&xh){ s_medPinned=false; g_medUntil=0; }
          } }

        // ---- transport + time ----
        float rowY=ap.y+70;
        if(ToastBtn(dl,V(tx+12,rowY),13,0,al,io,7101) && click) g_reqPrev=1;
        if(ToastBtn(dl,V(tx+46,rowY),15,1,al,io,7102) && click) g_reqPlay=1;
        if(ToastBtn(dl,V(tx+80,rowY),13,2,al,io,7103) && click) g_reqNext=1;
        double posM=MediaPos(), durM=g_md.dur;
        auto ts=[&](double v){ char c[16]; snprintf(c,16,"%d:%02d",(int)v/60,(int)v%60); return std::string(c); };
        if(durM>0){ std::string tm=ts(posM)+" / "+ts(durM);
            TextAt(dl,g_fSml,12,V(tRight-TextW(g_fSml,12,tm.c_str()),rowY-8),A(WithA(COL_INK2,210)),tm.c_str()); }

        // ---- progress (click to seek) ----
        float py0=ae.y+12, px0=a.x+12, px1=b.x-12;
        if(durM>0){
            float f=(float)std::clamp(posM/durM,0.0,1.0);
            bool bh=io.MousePos.x>px0&&io.MousePos.x<px1&&io.MousePos.y>py0-7&&io.MousePos.y<py0+9;
            float th=bh? 5.0f : 3.5f;
            dl->AddRectFilled(V(px0,py0),V(px1,py0+th),A(WithA(COL_INK2,55)),th*0.5f);
            dl->AddRectFilled(V(px0,py0),V(px0+(px1-px0)*f,py0+th),A(tint),th*0.5f);
            dl->AddCircleFilled(V(px0+(px1-px0)*f,py0+th*0.5f),bh?6.0f:4.0f,A(tint));
            dl->AddCircleFilled(V(px0+(px1-px0)*f,py0+th*0.5f),bh?11.0f:8.0f,A(WithA(tint,40)));
            if(bh&&click) g_reqSeek=(double)std::clamp((io.MousePos.x-px0)/(px1-px0),0.0f,1.0f)*durM;
        }

        // ---- the lyric being sung ----
        if(lyr){
            std::vector<LyricLine> L; { std::lock_guard<std::mutex> lk(g_lyricsMtx); L=g_lyrics; }
            float lp=0; int cur=LyricCurrent(L,posM+LyrOffset(),lp);
            static int s_prev=-2; static float s_chg=1.0f;
            if(cur!=s_prev){ s_prev=cur; s_chg=0.0f; }
            s_chg=std::min(1.0f,s_chg+g_frameDt/0.35f);
            float e=Cael::eval(Cael::EMPHASIZED_DECEL,s_chg);
            float ly=py0+14+(1.0f-e)*10.0f, boxW=px1-px0;
            if(cur>=0 && !L[cur].text.empty()){
                std::vector<std::string> subs; LyricWrap(g_fMed,16,L[cur].text,boxW,subs);
                if(subs.size()>2) subs.resize(2);
                float glowK=(0.7f+0.3f*sinf((float)nowM*0.004f))*al*e;
                LyricSing(dl,g_fMed,16,subs,px0,ly,boxW,20.0f,true,lp,A(WithA(COL_INK,(int)(110*e))),A(WithA(COL_INK,(int)(255*e))),WithA(tint,255),glowK);
                if(subs.size()<2 && cur+1<(int)L.size() && !L[cur+1].text.empty()){
                    std::string nx=Clip(g_fSml,13,L[cur+1].text,boxW);
                    TextAt(dl,g_fSml,13,V(px0+(boxW-TextW(g_fSml,13,nx.c_str()))*0.5f,ly+24),A(WithA(COL_INK2,(int)(150*e))),nx.c_str()); }
            } else {
                float t2=(float)nowM*0.004f;                                 // instrumental / before the first line
                for(int i=0;i<3;i++){ float k=0.5f+0.5f*sinf(t2-i*0.9f);
                    dl->AddCircleFilled(V((px0+px1)*0.5f-16+i*16,ly+12),3.0f+k*1.5f,A(WithA(tint,(int)(80+k*160)))); }
            }
        }
        if(bornM) dl->PopClipRect();
    }

    // ------------------------------------------------------------------ screen recording
    bool wantRec = GetTickCount64()<g_recToastUntil || (g_recording && g_recToastExpanded);
    Approach(g_recToastAnim, wantRec?1.0f:0.0f, wantRec?11.0f:8.0f);
    if(g_recToastAnim>0.004f){
        float ra=std::clamp(g_recToastAnim,0.0f,1.0f);
        float p = wantRec? EaseOutBack(ra) : (1.0f-EaseInCubic(1.0f-ra));
        float al=ra;
        float cardW=W-24, cardH=g_recToastExpanded? 158.0f : 76.0f;
        float x=(W-12-cardW) + (1.0f-p)*(cardW+40);
        ImVec2 a=V(x,y), b=V(x+cardW,y+cardH);
        ImU32 accent = g_recording? IM_COL32(226,86,86,255) : COL_GOLD;
        ToastCard(dl,a,b,al,accent,20.0f);
        bool hov=io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;

        // pulsing record dot
        ImVec2 dotc=V(a.x+34,a.y+38);
        float pulse=0.5f+0.5f*sinf((float)GetTickCount64()*0.005f);
        if(g_recording){
            dl->AddCircleFilled(dotc,13.0f+pulse*2.5f,MulA(WithA(accent,(int)(40+40*pulse)),al));
            dl->AddCircleFilled(dotc,7.0f,MulA(accent,al));
        } else {
            dl->AddCircleFilled(dotc,13.0f,MulA(WithA(COL_INK2,40),al));
            dl->AddRectFilled(V(dotc.x-5,dotc.y-5),V(dotc.x+5,dotc.y+5),MulA(COL_INK2,al),1.5f);
        }
        TextAt(dl,g_fSml,12,V(a.x+58,a.y+15),MulA(accent,al), g_recording? "SCREEN RECORDING" : "RECORDING STOPPED");
        char line[220];
        if(g_recording){
            unsigned s=(unsigned)((GetTickCount64()-g_recStart)/1000);
            snprintf(line,220,"%u:%02u  \xE2\x80\xA2  %s  \xE2\x80\xA2  %s", s/60,s%60,
                     g_recMonName.c_str(), g_recAudio? g_recAudioWhat.c_str():"no audio");
        } else snprintf(line,220,"Saved to %s", g_recDir.empty()? "your Videos folder":g_recDir.c_str());
        TextAt(dl,g_fMed,16,V(a.x+58,a.y+34),MulA(COL_INK,al),
               Clip(g_fMed,16,line,cardW-58-40).c_str());
        if(!g_recToastExpanded)
            TextAt(dl,g_fSml,12,V(a.x+58,a.y+55),MulA(COL_INK2,al),"Click for details");

        // expanded details
        if(g_recToastExpanded){
            float ly=a.y+70, lx=a.x+58;
            auto row=[&](const char* k,const std::string& v){
                TextAt(dl,g_fSml,13,V(lx,ly),MulA(COL_INK2,al),k);
                TextAt(dl,g_fSml,13,V(lx+92,ly),MulA(COL_INK,al),
                       Clip(g_fSml,13,v,cardW-(lx-a.x)-100).c_str());
                ly+=20; };
            row("Display", g_recMonName.empty()? std::string("unknown") : g_recMonName);
            row("Audio",   g_recAudio? g_recAudioWhat : std::string("no audio"));
            row("Folder",  g_recDir.empty()? std::string("Videos") : g_recDir);
            float bw2=150, bx2=b.x-bw2-16, by2=b.y-32;
            bool ohov=io.MousePos.x>bx2&&io.MousePos.x<bx2+bw2&&io.MousePos.y>by2&&io.MousePos.y<by2+24;
            dl->AddRectFilled(V(bx2,by2),V(bx2+bw2,by2+24),MulA(WithA(COL_INK2,(int)(ohov?60:34)),al),8);
            const char* ot="Open the folder";
            TextAt(dl,g_fSml,13,V(bx2+bw2/2-TextW(g_fSml,13,ot)/2,by2+4),MulA(COL_INK,al),ot);
            if(click&&ohov){ OpenRecordings(); g_recToastUntil=GetTickCount64()+3000; }
        }
        grow(a,b);                     // always: the window region clips drawing, not just clicks
        if(hov){
            bool onBody = io.MousePos.y < b.y-(g_recToastExpanded?36.0f:0.0f);
            if(click && onBody){
                g_recToastExpanded=!g_recToastExpanded;
                g_recToastUntil=GetTickCount64()+(g_recToastExpanded? 20000 : 4000);
            }
        }
        y+=cardH+10;
    } else g_recToastExpanded=false;

    // ------------------------------------------------------------------ screenshot saved
    bool wantShot = GetTickCount64()<g_shotToastUntil;
    Approach(g_shotToastAnim, wantShot?1.0f:0.0f, wantShot?11.0f:8.0f);
    if(g_shotToastAnim>0.004f){
        float sa=std::clamp(g_shotToastAnim,0.0f,1.0f);
        float pp = wantShot? EaseOutBack(sa) : (1.0f-EaseInCubic(1.0f-sa));
        float cardW=W-24, cardH=72;
        float x=(W-12-cardW) + (1.0f-pp)*(cardW+40);
        ImVec2 a=V(x,y), b=V(x+cardW,y+cardH);
        ImU32 accent = g_shotOk? COL_GOLD : IM_COL32(226,86,86,255);
        ToastCard(dl,a,b,sa,accent,20.0f);
        // camera glyph
        ImVec2 cc=V(a.x+34,a.y+36);
        dl->AddRect(V(cc.x-13,cc.y-8),V(cc.x+13,cc.y+9),MulA(accent,sa),3,0,1.8f);
        dl->AddRectFilled(V(cc.x-5,cc.y-12),V(cc.x+5,cc.y-8),MulA(accent,sa),1);
        dl->AddCircle(cc,5.0f,MulA(accent,sa),0,1.8f);
        TextAt(dl,g_fSml,12,V(a.x+58,a.y+13),MulA(accent,sa), g_shotOk? "SCREENSHOT SAVED":"SCREENSHOT COPIED");
        TextAt(dl,g_fMed,16,V(a.x+58,a.y+32),MulA(COL_INK,sa),
               Clip(g_fMed,16, g_shotOk? g_shotFile : std::string("Saved to the clipboard"), cardW-58-120).c_str());
        // open-folder button
        bool click2=io.MouseClicked[0];
        float bw2=104, bx2=b.x-bw2-14, by2=b.y-30;
        bool ohov=io.MousePos.x>bx2&&io.MousePos.x<bx2+bw2&&io.MousePos.y>by2&&io.MousePos.y<by2+22;
        dl->AddRectFilled(V(bx2,by2),V(bx2+bw2,by2+22),MulA(WithA(COL_INK2,(int)(ohov?60:34)),sa),8);
        const char* ot="Open folder";
        TextAt(dl,g_fSml,12,V(bx2+bw2/2-TextW(g_fSml,12,ot)/2,by2+3),MulA(COL_INK,sa),ot);
        bool hov=io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;
        grow(a,b); (void)hov;          // always: the window region clips drawing, not just clicks
        if(click2&&ohov && !g_shotDir.empty()){
            AetherShellExec(nullptr,L"open",U82W(g_shotDir).c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            g_shotToastUntil=GetTickCount64()+2500;
        }
        y+=cardH+10;
    }

    g_toastStackH = y;
    g_medRect = any? hit : RECT{0,0,0,0};   // every card grows `hit` whether hovered or not: the region clips drawing
}

