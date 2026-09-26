// src/components/AlertCard.h  —  Aether shell
// The popups SafeLaunch.h raises ("Couldn't open Steam", "Still opening...", "Discord stopped working"). They live in
// the bar window - the one surface that spans every monitor and is always up - and drop in at the top centre of the
// screen the pointer is on, stacked, each a card that morphs out of a small pill. Hovering one holds it open.
// Buttons appear only when they make sense: Try again, Run as administrator, Show in folder, and a close X.
#pragma once

static RECT g_alertRect={0,0,0,0};          // union of the cards, for the bar window's hit region (logical px)
static bool AlertsActive(){ std::lock_guard<std::mutex> lk(g_alertMtx); return !g_alerts.empty(); }

static void DrawAlerts(ImDrawList* dl,ImGuiIO& io){
    g_alertRect=RECT{0,0,0,0};
    std::vector<AetherAlert> snap; { std::lock_guard<std::mutex> lk(g_alertMtx); snap=g_alerts; }
    static std::unordered_map<uint32_t,float> openA;       // 0..1 per card
    static std::unordered_map<uint32_t,bool> closing;
    static std::vector<AetherAlert> leaving;                // cards animating out after removal
    ULONGLONG now=GetTickCount64();
    const float S=1.0f/std::max(0.5f,g_uiScale);
    float dt=std::min(g_frameDt,0.05f);
    // cards removed from the list (dismissed / expired / a pending launch finished) play their exit first
    { std::unordered_set<uint32_t> ids; for(auto& a:snap) ids.insert(a.id);
      for(auto it=openA.begin(); it!=openA.end(); ++it) if(!ids.count(it->first) && !closing[it->first]){
          closing[it->first]=true; }
    }
    if(snap.empty() && openA.empty()) return;

    // the screen under the pointer
    POINT cp; GetCursorPos(&cp); int mi=g_mons.empty()? 0 : MonIndexAt(cp);
    const RECT& mr = g_mons.empty()? RECT{0,0,(LONG)(io.DisplaySize.x*g_uiScale),(LONG)(io.DisplaySize.y*g_uiScale)} : g_mons[mi].rc;
    float L=(mr.left-g_vs.left)/g_uiScale, T=(mr.top-g_vs.top)/g_uiScale, R=(mr.right-g_vs.left)/g_uiScale;
    ImVec2 mp=io.MousePos; { POINT c2; if(GetCursorPos(&c2)) mp=ImVec2((c2.x-g_vs.left)/g_uiScale,(c2.y-g_vs.top)/g_uiScale); }
    bool click=io.MouseClicked[0];
    const float CW=400*S;
    float y=T+FrameInset(EDGE_TOP)+14*S;
    float cx=(L+R)*0.5f;
    LONG rl=LONG_MAX, rt=LONG_MAX, rr=LONG_MIN, rb=LONG_MIN;
    std::vector<uint32_t> dismiss;
    struct Act{ int what; AetherAlert a; }; std::vector<Act> acts;

    for(auto& a:snap){
        float& o=openA[a.id];
        bool isClosing=closing[a.id];
        o += ((isClosing? 0.0f : 1.0f)-o)*std::min(1.0f,dt*(isClosing? 10.0f : 7.0f));
        float e=Cael::eval(Cael::EMPHASIZED_DECEL,std::clamp(o,0.0f,1.0f));
        ImU32 tone = a.kind==0? IM_COL32(226,104,96,255) : a.kind==1? IM_COL32(236,184,72,255) : COL_GOLD;
        std::vector<std::string> lines; WrapLines(g_fSml,14*S,a.body,CW-84*S,4,lines);
        bool hasBtns = a.canRetry||a.canAdmin||a.canReveal;
        float H = 58*S + lines.size()*19*S + (hasBtns? 44*S : 6*S);
        // morph: a pill that widens, then opens to full height
        float wide=Cael::eval(Cael::DEFAULT_SPATIAL,std::clamp(o/0.55f,0.0f,1.0f));
        float tall=Cael::eval(Cael::DEFAULT_SPATIAL,std::clamp((o-0.30f)/0.70f,0.0f,1.0f));
        float w=44*S+(CW-44*S)*wide, h=44*S+(H-44*S)*tall;
        float yy=y-(1.0f-e)*30*S;
        ImVec2 A=V(cx-w*0.5f,yy), B=V(cx+w*0.5f,yy+h);
        bool hov=mp.x>=A.x&&mp.x<B.x&&mp.y>=A.y&&mp.y<B.y;
        if(hov && a.until && !isClosing){ std::lock_guard<std::mutex> lk(g_alertMtx);   // hovering holds it
            for(auto& x:g_alerts) if(x.id==a.id) x.until=std::max(x.until,now+4000); }
        if(a.until && now>a.until && !hov) dismiss.push_back(a.id);
        int al=(int)(255*std::clamp(o*1.6f,0.0f,1.0f));
        float rnd=std::min(22*S,h*0.5f);
        for(int i=6;i>0;i--) dl->AddRectFilled(V(A.x-i,A.y-i+4),V(B.x+i,B.y+i+5),IM_COL32(0,0,0,(int)(9*al/255)),rnd+i);
        dl->AddRectFilled(A,B,WithA(Mix(PanelCol(255),COL_CARD2,0.45f),al),rnd);
        dl->AddRect(A,B,WithA(tone,(int)(110*al/255)),rnd,0,1.5f*S);
        float cA=std::clamp((tall-0.6f)/0.4f,0.0f,1.0f);
        auto CA=[&](ImU32 c){ return WithA(c,(int)(((c>>IM_COL32_A_SHIFT)&0xFF)*cA*al/255)); };
        // icon
        ImVec2 ic=V(A.x+28*S,A.y+(cA>0.01f? 30*S : h*0.5f));
        float pulse = a.kind==2? 0.5f+0.5f*sinf((float)ImGui::GetTime()*4.0f) : 1.0f;
        dl->AddCircleFilled(ic,16*S,WithA(tone,(int)((cA>0.01f? 55 : 200)*al/255)),24);
        MsIcon(dl,a.icon.empty()? "error" : a.icon.c_str(),ic,20*S,WithA(cA>0.01f? tone : COL_INK,(int)(al*(0.6f+0.4f*pulse))));
        if(cA>0.01f){
            float tx=A.x+54*S;
            TextAt(dl,g_fMed,16*S,V(tx,A.y+18*S),CA(COL_INK),Clip(g_fMed,16*S,a.title,B.x-40*S-tx).c_str());
            float ly=A.y+42*S;
            for(auto& ln:lines){ TextAt(dl,g_fSml,14*S,V(tx,ly),CA(COL_INK2),ln.c_str()); ly+=19*S; }
            // close X
            ImVec2 xc=V(B.x-22*S,A.y+22*S); bool xh=fabsf(mp.x-xc.x)<13*S&&fabsf(mp.y-xc.y)<13*S;
            if(xh) dl->AddCircleFilled(xc,13*S,CA(WithA(COL_INK2,60)),20);
            MsIcon(dl,"close",xc,17*S,CA(COL_INK2));
            if(xh&&click) dismiss.push_back(a.id);
            // time-left bar
            if(a.until && !hov){ float left=std::clamp((float)(a.until>now? a.until-now : 0)/15000.0f,0.0f,1.0f);
                dl->AddRectFilled(V(A.x+rnd,B.y-3*S),V(A.x+rnd+(B.x-A.x-rnd*2)*left,B.y-1*S),CA(WithA(tone,120)),1.5f*S); }
            if(hasBtns){
                float bx=tx, by=B.y-38*S, bh=28*S;
                auto btn=[&](const char* icon,const char* lab,int what,bool primary){
                    float bw=TextW(g_fSml,13.5f*S,lab)+40*S;
                    ImVec2 ba=V(bx,by), bb=V(bx+bw,by+bh); bool h2=mp.x>=ba.x&&mp.x<bb.x&&mp.y>=ba.y&&mp.y<bb.y;
                    float ha=HoverAnim(0x7A000+a.id*4+what,h2);
                    ImU32 bg = primary? Mix(tone,IM_COL32(255,255,255,255),0.12f*ha) : Mix(Mix(COL_CARD2,COL_INK2,0.18f),COL_INK2,0.25f*ha);
                    dl->AddRectFilled(ba,bb,CA(bg),bh*0.5f);
                    ImU32 fg = primary? IM_COL32(30,16,14,255) : COL_INK;
                    MsIcon(dl,icon,V(bx+16*S,by+bh*0.5f),16*S,CA(fg));
                    TextAt(dl,g_fSml,13.5f*S,V(bx+28*S,by+6*S),CA(fg),lab);
                    if(h2&&click) acts.push_back({what,a});
                    bx+=bw+8*S; };
                if(a.canRetry)  btn("refresh","Try again",0,true);
                if(a.canAdmin)  btn("shield_person","Run as admin",1,false);
                if(a.canReveal) btn("folder_open","Show in folder",2,false);
            }
        }
        rl=std::min(rl,(LONG)(A.x-8)); rt=std::min(rt,(LONG)(A.y-8)); rr=std::max(rr,(LONG)(B.x+8)); rb=std::max(rb,(LONG)(B.y+10));
        y += h*e + 10*S;
    }
    // exits for cards already gone from the list
    for(auto it=openA.begin(); it!=openA.end();){
        bool inSnap=false; for(auto& a:snap) if(a.id==it->first){ inSnap=true; break; }
        if(!inSnap){ it->second += (0.0f-it->second)*std::min(1.0f,dt*10.0f);
            if(it->second<0.01f){ closing.erase(it->first); it=openA.erase(it); continue; } }
        ++it; }
    if(rl<rr) g_alertRect=RECT{rl,rt,rr,rb};
    if(!dismiss.empty()){ std::lock_guard<std::mutex> lk(g_alertMtx);
        g_alerts.erase(std::remove_if(g_alerts.begin(),g_alerts.end(),[&](const AetherAlert& x){
            return std::find(dismiss.begin(),dismiss.end(),x.id)!=dismiss.end(); }),g_alerts.end()); }
    for(auto& ac:acts){
        { std::lock_guard<std::mutex> lk(g_alertMtx);
          g_alerts.erase(std::remove_if(g_alerts.begin(),g_alerts.end(),[&](const AetherAlert& x){ return x.id==ac.a.id; }),g_alerts.end()); }
        if(ac.what==0) AetherShellExec(nullptr,nullptr,ac.a.file.c_str(),ac.a.params.empty()? nullptr : ac.a.params.c_str(),ac.a.dir.empty()? nullptr : ac.a.dir.c_str(),SW_SHOWNORMAL);
        else if(ac.what==1) AetherShellExec(nullptr,L"runas",ac.a.file.c_str(),ac.a.params.empty()? nullptr : ac.a.params.c_str(),ac.a.dir.empty()? nullptr : ac.a.dir.c_str(),SW_SHOWNORMAL);
        else { std::wstring arg=L"/select,\""+ac.a.file+L"\""; AetherShellExec(nullptr,L"open",L"explorer.exe",arg.c_str(),nullptr,SW_SHOWNORMAL); }
    }
}
