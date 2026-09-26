// src/modules/record/RecordHud.h  —  Aether shell
// The recording checker. When a recording starts, a red dot flies in from a screen corner or edge
// (record.hud_position) and MORPHS - widens into a pill, then opens into a card: "Recording", the running time,
// the real frame rate and file size from ffmpeg, pause / stop, and a settings button that opens the audio sources
// and frame rate right there. After a few seconds it tucks itself into the edge as a small pulsing tab; touch
// that edge (or the tab) and it comes back out. Stopping shows "Saved" with an Open folder button, then it flies off.
#pragma once

static bool RecHudActive();
static float g_recHudOpen=0, g_recHudPeek=0;
static bool  g_recHudSettings=false;
static ULONGLONG g_recHudSavedUntil=0, g_recHudStart=0, g_recHudLastInside=0;
static std::string g_recHudSavedFile;

// ---- dshow audio device list (what ffmpeg can record from) ----
// g_recDevMtx / g_recAudioDevs / g_recDevState live with the globals in main.cpp (Settings > Audio reads them too)
static void RecListAudioDevices(){
    if(g_recDevState.exchange(1)==1) return;
    std::thread([]{
        std::vector<std::string> out;
        std::wstring ff=V2FindFfmpeg();
        if(!ff.empty()){
            SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE}; HANDLE rd=nullptr, wr=nullptr;
            if(CreatePipe(&rd,&wr,&sa,0)){
                SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0);
                STARTUPINFOW si{sizeof(si)}; si.dwFlags=STARTF_USESTDHANDLES; si.hStdError=wr; si.hStdOutput=wr; si.hStdInput=nullptr;
                PROCESS_INFORMATION pi{};
                std::wstring cmd=L"\""+ff+L"\" -hide_banner -list_devices true -f dshow -i dummy";
                std::vector<wchar_t> cl(cmd.begin(),cmd.end()); cl.push_back(0);
                if(CreateProcessW(nullptr,cl.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
                    CloseHandle(wr); wr=nullptr;
                    std::string all; char b[4096]; DWORD n=0;
                    while(ReadFile(rd,b,sizeof(b),&n,nullptr)&&n>0) all.append(b,n);
                    WaitForSingleObject(pi.hProcess,4000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                    std::istringstream ss(all); std::string line;
                    while(std::getline(ss,line)){
                        if(line.find("(audio)")==std::string::npos) continue;
                        size_t a=line.find('"'), b2=line.find('"',a+1);
                        if(a!=std::string::npos && b2!=std::string::npos) out.push_back(line.substr(a+1,b2-a-1));
                    }
                }
                if(wr) CloseHandle(wr); CloseHandle(rd);
            }
        }
        { std::lock_guard<std::mutex> lk(g_recDevMtx); g_recAudioDevs=out; }
        g_recDevState=2;
    }).detach();
}

static bool RecHudActive(){ return g_recHud && (g_recording || GetTickCount64()<g_recHudSavedUntil || g_recHudOpen>0.01f); }

static void DrawRecordHud(ImDrawList* dl,ImGuiIO& io){
    static bool wasRec=false;
    ULONGLONG now=GetTickCount64();
    if(g_recording && !wasRec){ g_recHudStart=now; g_recHudLastInside=now; g_recHudPeek=0; g_recHudSavedUntil=0; }
    if(!g_recording && wasRec){ g_recHudSavedUntil=now+3200; g_recHudSavedFile=g_ffFile; g_recHudPeek=0; g_recHudSettings=false; }
    wasRec=g_recording;
    g_recHudRect=RECT{0,0,0,0};
    if(!g_recHud){ g_recHudOpen=0; return; }
    bool saved = !g_recording && now<g_recHudSavedUntil;
    float want = (g_recording||saved)? 1.0f : 0.0f;
    float dt=std::min(g_frameDt,0.05f);
    g_recHudOpen += (want-g_recHudOpen)*std::min(1.0f,dt*(want>g_recHudOpen? 3.2f : 5.0f));
    if(fabsf(want-g_recHudOpen)<0.003f) g_recHudOpen=want;
    if(g_recHudOpen<0.004f) return;
    const float S=1.0f/std::max(0.5f,g_uiScale);

    // ---- where: the active monitor, inside the frame ----
    int mi=FrameMon();
    const RECT& mr = g_mons.empty()? RECT{0,0,(LONG)(io.DisplaySize.x*g_uiScale),(LONG)(io.DisplaySize.y*g_uiScale)} : g_mons[mi].rc;
    float L=(mr.left-g_vs.left)/g_uiScale, T=(mr.top-g_vs.top)/g_uiScale, R=(mr.right-g_vs.left)/g_uiScale, B=(mr.bottom-g_vs.top)/g_uiScale;
    float il=FrameInset(EDGE_LEFT), it=FrameInset(EDGE_TOP), ir=FrameInset(EDGE_RIGHT), ib=FrameInset(EDGE_BOTTOM);
    const float M=12*S;
    const float W=344*S, H0=118*S, HS=H0+188*S, D=46*S;
    float H = g_recHudSettings? HS : H0;
    static float hAnim=0; if(hAnim<=0) hAnim=H; hAnim += (H-hAnim)*std::min(1.0f,dt*12.0f);
    int pos=std::clamp(g_recHudPos,0,7);    // 0 TL 1 TR 2 BL 3 BR 4 L 5 R 6 T 7 B
    bool fromLeft = pos==0||pos==2||pos==4, fromRight = pos==1||pos==3||pos==5, fromTop = pos==6, fromBottom = pos==7;
    float tx = fromLeft? L+il+M : fromRight? R-ir-M-W : (L+R)*0.5f-W*0.5f;
    float ty = (pos==0||pos==1||pos==6)? T+it+M : (pos==2||pos==3||pos==7)? B-ib-M-hAnim : (T+B)*0.5f-hAnim*0.5f;

    // ---- the morph: a dot flies in, widens into a pill, opens into the card ----
    float o=Cael::eval(Cael::DEFAULT_SPATIAL,std::clamp(g_recHudOpen,0.0f,1.0f));
    float fly=Cael::eval(Cael::EMPHASIZED_DECEL,std::clamp(g_recHudOpen/0.45f,0.0f,1.0f));
    float wide=Cael::eval(Cael::DEFAULT_SPATIAL,std::clamp((g_recHudOpen-0.30f)/0.45f,0.0f,1.0f));
    float tall=Cael::eval(Cael::DEFAULT_SPATIAL,std::clamp((g_recHudOpen-0.55f)/0.45f,0.0f,1.0f));
    (void)o;
    // peek: tucked into its edge as a tab
    bool canPeek = g_recHudPeekOn && g_recording && !g_recHudSettings && now-g_recHudStart>(ULONGLONG)std::max(800,g_recHudStayMs);
    // the real cursor: while tucked the window region is only the tab, so ImGui never hears the mouse reach the edge
    ImVec2 mp=io.MousePos; { POINT cp; if(GetCursorPos(&cp)) mp=ImVec2((cp.x-g_vs.left)/g_uiScale,(cp.y-g_vs.top)/g_uiScale); }
    // the tab / edge strip that brings it back
    float pw = D + (W-D)*wide, ph = D + (hAnim-D)*tall;
    float cxT = fromLeft? tx : fromRight? tx+W-pw : tx+(W-pw)*0.5f;
    float cyT = (pos==2||pos==3||pos==7)? ty+hAnim-ph : (pos==4||pos==5)? ty+(hAnim-ph)*0.5f : ty;
    ImVec2 A=V(cxT,cyT), Bv=V(cxT+pw,cyT+ph);
    bool inside = mp.x>=A.x-20&&mp.x<Bv.x+20&&mp.y>=A.y-20&&mp.y<Bv.y+20;
    // where the tab sits when tucked
    float tabR=D*0.5f;
    ImVec2 tabC = fromLeft? V(L+il+tabR*0.35f,cyT+tabR) : fromRight? V(R-ir-tabR*0.35f,cyT+tabR) : fromTop? V((L+R)*0.5f,T+it+tabR*0.35f) : V((L+R)*0.5f,B-ib-tabR*0.35f);
    bool nearTab = (mp.x-tabC.x)*(mp.x-tabC.x)+(mp.y-tabC.y)*(mp.y-tabC.y) < (tabR+26*S)*(tabR+26*S);
    bool atEdge = (fromLeft && mp.x<=L+il+6 && fabsf(mp.y-tabC.y)<120*S) || (fromRight && mp.x>=R-ir-6 && fabsf(mp.y-tabC.y)<120*S)
               || (fromTop && mp.y<=T+it+6 && fabsf(mp.x-tabC.x)<160*S) || (fromBottom && mp.y>=B-ib-6 && fabsf(mp.x-tabC.x)<160*S);
    if(inside && g_recHudPeek<0.5f) g_recHudLastInside=now;
    float peekWant = g_recHudPeek;
    if(canPeek && !inside && now-g_recHudLastInside>1200) peekWant=1.0f;
    if(nearTab || atEdge){ peekWant=0.0f; g_recHudLastInside=now; }
    if(!canPeek) peekWant=0.0f;
    g_recHudPeek += (peekWant-g_recHudPeek)*std::min(1.0f,dt*9.0f);
    float pk=Cael::eval(Cael::DEFAULT_SPATIAL,std::clamp(g_recHudPeek,0.0f,1.0f));

    // final rect: open card -> tab
    ImVec2 fa=V(A.x+(tabC.x-tabR-A.x)*pk, A.y+(tabC.y-tabR-A.y)*pk), fb=V(Bv.x+(tabC.x+tabR-Bv.x)*pk, Bv.y+(tabC.y+tabR-Bv.y)*pk);
    // the fly-in slides the whole thing from beyond its edge
    float travel = fromLeft? -(fb.x-L+20) : fromRight? (R-fa.x+20) : fromTop? -(fb.y-T+20) : (B-fa.y+20);
    float off=(1.0f-fly)*travel;
    if(fromLeft||fromRight){ fa.x+=off; fb.x+=off; } else { fa.y+=off; fb.y+=off; }
    float rnd=std::min((fb.x-fa.x),(fb.y-fa.y))*0.5f; rnd=rnd*(1.0f-tall*(1.0f-pk))+20*S*tall*(1.0f-pk); rnd=std::min(rnd,std::min(fb.x-fa.x,fb.y-fa.y)*0.5f);
    int al=(int)(255*std::clamp(g_recHudOpen*2.0f,0.0f,1.0f));
    ImU32 salmon=IM_COL32(232,110,100,255), salmonInk=IM_COL32(94,24,22,255);
    ImU32 cardC=Mix(PanelCol(255),COL_CARD2,0.42f);
    // shadow + body
    for(int i=5;i>0;i--) dl->AddRectFilled(V(fa.x-i,fa.y-i+3),V(fb.x+i,fb.y+i+4),IM_COL32(0,0,0,(int)(10*al/255)),rnd+i);
    dl->AddRectFilled(fa,fb,WithA(cardC,al),rnd);
    dl->AddRect(fa,fb,WithA(g_recording? salmon : COL_GOLD,(int)(90*al/255)),rnd,0,1.4f*S);
    g_recHudRect=RECT{(LONG)(fa.x-8),(LONG)(fa.y-8),(LONG)(fb.x+8),(LONG)(fb.y+8)};

    float pulse=0.5f+0.5f*sinf((float)ImGui::GetTime()*4.0f);
    bool paused=g_ffPaused;
    // collapsed (dot / tab) look
    float contentA = std::clamp((tall-0.55f)/0.45f,0.0f,1.0f)*(1.0f-std::clamp(pk*1.6f,0.0f,1.0f));
    if(contentA<0.99f){
        ImVec2 c=V((fa.x+fb.x)*0.5f,(fa.y+fb.y)*0.5f);
        if(pk>0.5f){ c = fromLeft? V(fb.x-tabR*0.85f,c.y) : fromRight? V(fa.x+tabR*0.85f,c.y) : fromTop? V(c.x,fb.y-tabR*0.85f) : V(c.x,fa.y+tabR*0.85f); }
        float r=D*0.20f*(saved? 1.0f : 0.85f+0.25f*pulse);
        ImU32 dc = saved? COL_GOLD : paused? IM_COL32(240,190,70,255) : salmon;
        dl->AddCircleFilled(c,r,WithA(dc,(int)(al*(1.0f-contentA))),20);
    }
    if(contentA<=0.01f) return;
    int ca=(int)(al*contentA);
    auto CA=[&](ImU32 c){ return MulA(c,ca/255.0f); };
    bool click=io.MouseClicked[0];
    float x0=fa.x+16*S, x1=fb.x-16*S, y0=fa.y+14*S;
    if(saved){
        MsIcon(dl,"check_circle",V(x0+12*S,y0+14*S),26*S,CA(COL_GOLD));
        TextAt(dl,g_fMed,19*S,V(x0+32*S,y0+3*S),CA(COL_INK),"Recording saved");
        std::string nm=g_recHudSavedFile; size_t sl=nm.find_last_of("\\/"); if(sl!=std::string::npos) nm=nm.substr(sl+1);
        if(nm.empty()) nm="Videos\\Captures";
        TextAt(dl,g_fSml,14*S,V(x0,y0+36*S),CA(COL_INK2),Clip(g_fSml,14*S,nm,x1-x0).c_str());
        ImVec2 ba=V(x0,fb.y-44*S), bb=V(x0+132*S,fb.y-12*S); bool h=mp.x>=ba.x&&mp.x<bb.x&&mp.y>=ba.y&&mp.y<bb.y;
        dl->AddRectFilled(ba,bb,CA(h? Mix(COL_INK,COL_GOLD,0.35f) : Mix(COL_INK,COL_GOLD,0.2f)),16*S);
        MsIcon(dl,"folder_open",V(ba.x+18*S,(ba.y+bb.y)*0.5f),18*S,CA(M3OnPrimary()));
        TextAt(dl,g_fMed,14*S,V(ba.x+32*S,ba.y+8*S),CA(M3OnPrimary()),"Open folder");
        if(h&&click){ OpenRecordings(); g_recHudSavedUntil=now+400; }
        if(inside) g_recHudSavedUntil=std::max(g_recHudSavedUntil,now+1200);
        return;
    }
    // row 1: dot + state + mode
    ImVec2 dotC=V(x0+7*S,y0+11*S);
    dl->AddCircleFilled(dotC,6*S*(paused? 1.0f : 0.8f+0.3f*pulse),CA(paused? IM_COL32(240,190,70,255) : salmon),16);
    TextAt(dl,g_fMed,17*S,V(x0+22*S,y0),CA(COL_INK),paused? "Paused" : "Recording");
    const char* mode = g_ffProc? (g_recMode==1? "All screens" : "Fullscreen") : "Game Bar";
    TextAt(dl,g_fSml,13*S,V(x1-TextW(g_fSml,13*S,mode),y0+3*S),CA(COL_INK2),mode);
    // row 2: time + fps + size
    ULONGLONG ms=now-g_recStart-g_ffPausedMs-(g_ffPaused? now-g_ffPauseAt : 0);
    char tm[24]; if(ms>=3600000) snprintf(tm,24,"%llu:%02llu:%02llu",ms/3600000,(ms/60000)%60,(ms/1000)%60); else snprintf(tm,24,"%02llu:%02llu",ms/60000,(ms/1000)%60);
    TextAt(dl,g_fMed,32*S,V(x0,y0+26*S),CA(COL_INK),tm);
    float cx=x0+TextW(g_fMed,32*S,tm)+12*S;
    auto chip=[&](const std::string& t){ float w=TextW(g_fSml,13*S,t.c_str())+16*S;
        dl->AddRectFilled(V(cx,y0+36*S),V(cx+w,y0+58*S),CA(Mix(COL_CARD2,COL_INK2,0.2f)),11*S);
        TextAt(dl,g_fSml,13*S,V(cx+8*S,y0+39*S),CA(COL_INK),t.c_str()); cx+=w+6*S; };
    if(g_ffProc){ char f[24]; float fps=g_recFpsNow.load(); if(fps>0.5f) snprintf(f,24,"%.0f fps",fps); else snprintf(f,24,"%d fps",g_recFps); chip(f);
        uint64_t by=g_recBytes.load(); if(by>0){ char sz[24]; if(by>1073741824ULL) snprintf(sz,24,"%.2f GB",by/1073741824.0); else snprintf(sz,24,"%.1f MB",by/1048576.0); chip(sz); } }
    // buttons along the bottom
    float by0=y0+74*S, bs=34*S;
    float bx=x1;
    auto btn=[&](const char* ic,ImU32 bg,ImU32 fg,int id)->bool{
        ImVec2 ba=V(bx-bs,by0), bb=V(bx,by0+bs); bool h=mp.x>=ba.x&&mp.x<bb.x&&mp.y>=ba.y&&mp.y<bb.y;
        float ha=HoverAnim(0x6D000+id,h);
        dl->AddRectFilled(ba,bb,CA(Mix(bg,IM_COL32(255,255,255,255),0.10f*ha)),bs*0.5f);
        MsIcon(dl,ic,V((ba.x+bb.x)*0.5f,(ba.y+bb.y)*0.5f),20*S,CA(fg));
        bx-=bs+8*S; return h&&click; };
    if(btn("stop",salmon,salmonInk,1)) V2RecToggle();
    if(g_ffProc && btn(paused? "play_arrow" : "pause",Mix(COL_CARD2,COL_INK2,0.2f),COL_INK,2)) V2RecPause();
    if(btn(g_recHudSettings? "expand_less" : "settings",Mix(COL_CARD2,COL_INK2,0.2f),COL_INK,3)){ g_recHudSettings=!g_recHudSettings; if(g_recHudSettings) RecListAudioDevices(); }
    TextAt(dl,g_fSml,12.5f*S,V(x0,by0+9*S),CA(COL_INK2),Clip(g_fSml,12.5f*S,"Saves to Videos\\Captures",bx-x0-6*S).c_str());

    // ---- settings, opened in place ----
    if(g_recHudSettings && hAnim>H0+20*S){
        float sy=y0+120*S;
        dl->PushClipRect(fa,fb,true);
        dl->AddLine(V(x0,sy-8*S),V(x1,sy-8*S),CA(WithA(COL_INK2,70)),1.0f);
        TextAt(dl,g_fSml,13*S,V(x0,sy),CA(COL_INK2),"Frame rate");
        { static const int FPS[3]={30,60,120}; float cw=56*S, gx=x0+96*S;
          for(int i=0;i<3;i++){ ImVec2 ca2=V(gx+i*(cw+6*S),sy-4*S), cb=V(ca2.x+cw,ca2.y+26*S);
              bool h=mp.x>=ca2.x&&mp.x<cb.x&&mp.y>=ca2.y&&mp.y<cb.y; bool on=g_recFps==FPS[i];
              dl->AddRectFilled(ca2,cb,CA(on? Mix(COL_INK,COL_GOLD,0.25f) : (h? Mix(COL_CARD2,COL_INK2,0.35f) : Mix(COL_CARD2,COL_INK2,0.18f))),13*S);
              char b[8]; snprintf(b,8,"%d",FPS[i]); TextAt(dl,g_fSml,13*S,V((ca2.x+cb.x)*0.5f-TextW(g_fSml,13*S,b)*0.5f,ca2.y+5*S),CA(on? M3OnPrimary() : COL_INK),b);
              if(h&&click){ g_recFps=FPS[i]; SaveConfig(); } } }
        std::vector<std::string> devs; { std::lock_guard<std::mutex> lk(g_recDevMtx); devs=g_recAudioDevs; }
        static bool m1=false,m2=false; static float a1=0,a2=0;
        auto srcRow=[&](float yy,const char* label,const char* icon,std::string& val,bool& open,float& anim,int id){
            TextAt(dl,g_fSml,13*S,V(x0,yy+6*S),CA(COL_INK2),label);
            ImVec2 sa=V(x0+96*S,yy); float sw=x1-sa.x;
            std::string lab= val.empty()? "None" : val;
            int hit=V2Split(dl,io,sa,sw,28*S,icon,lab,0x6D100+id,click);
            bool tog=false; if(hit){ open=!open; tog=true; if(open) RecListAudioDevices(); }
            std::vector<std::string> items={"None"}; int sel=0;
            if(g_recDevState.load()!=2) items.push_back("Looking for devices\xE2\x80\xA6");
            for(size_t i=0;i<devs.size();i++){ items.push_back(devs[i]); if(devs[i]==val) sel=(int)i+1; }
            dl->PopClipRect();
            int p=V2Menu(dl,io,open,anim,sa,V(sa.x+sw,sa.y+28*S),items,sel,false,tog);
            dl->PushClipRect(fa,fb,true);
            if(open){ float mh=items.size()*26+10; RECT mr2{(LONG)sa.x,(LONG)(sa.y+28*S),(LONG)(sa.x+sw),(LONG)(sa.y+34*S+mh)}; UnionRect(&g_recHudRect,&g_recHudRect,&mr2); }
            if(p>=0){ int di = g_recDevState.load()!=2? p-2 : p-1; val = p==0? std::string() : (di>=0 && di<(int)devs.size()? devs[di] : val); SaveConfig(); } };
        srcRow(sy+34*S,"System audio","speaker",g_recAudioSys,m1,a1,1);
        srcRow(sy+70*S,"Microphone","mic",g_recAudioMic,m2,a2,2);
        const char* note = V2FindFfmpeg().empty()? "Needs ffmpeg for audio and frame rate (Game Bar uses its own)"
                         : (g_recAudioSys.empty() && !devs.empty())? "Tip: \"Stereo Mix\" or a Voicemeeter output records what you hear"
                         : "Changes apply to the next recording";
        TextAt(dl,g_fSml,12*S,V(x0,sy+108*S),CA(COL_INK2),Clip(g_fSml,12*S,note,x1-x0).c_str());
        { static const char* PN[8]={"Top left","Top right","Bottom left","Bottom right","Left","Right","Top","Bottom"};
          TextAt(dl,g_fSml,13*S,V(x0,sy+140*S),CA(COL_INK2),"Flies in from");
          ImVec2 ba=V(x0+96*S,sy+134*S), bb=V(x1,sy+160*S); bool h=mp.x>=ba.x&&mp.x<bb.x&&mp.y>=ba.y&&mp.y<bb.y;
          dl->AddRectFilled(ba,bb,CA(h? Mix(COL_CARD2,COL_INK2,0.35f) : Mix(COL_CARD2,COL_INK2,0.18f)),13*S);
          TextAt(dl,g_fSml,13*S,V(ba.x+12*S,ba.y+5*S),CA(COL_INK),PN[pos]);
          MsIcon(dl,"chevron_right",V(bb.x-14*S,(ba.y+bb.y)*0.5f),18*S,CA(COL_INK));
          if(h&&click){ g_recHudPos=(g_recHudPos+1)%8; SaveConfig(); } }
        dl->PopClipRect();
    }
}
