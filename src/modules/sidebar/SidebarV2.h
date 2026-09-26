// src/modules/sidebar/SidebarV2.h  —  Aether shell
// Caelestia's sidebar as one merged panel (reference\v2\NOTES.md §4): notifications on top, then Keep Awake,
// the Screen Recorder and Quick Toggles, with a column of action buttons (logout / power / avatar / updates /
// reload) running down its left side. Chosen with sidebar.style = "caelestia"; "classic" keeps the separate
// notification panel and quick settings. Every part is a setting: which toggles, which buttons, the width,
// the avatar GIF, the recorder mode.
#pragma once
static void DrawAudioMixer(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,int uid,bool card);   // fwd (AudioMixer.h)

// ============================================================================================ radios
// Wi-Fi / Bluetooth on-off through Windows.Devices.Radios, off the render thread (the calls block)
static std::atomic<int>  g_radioWifi{-1}, g_radioBt{-1};      // -1 unknown, 0 off, 1 on
static std::atomic<bool> g_radioBusy{false};
static void RadioRefreshAsync(){
    if(g_radioBusy.exchange(true)) return;
    std::thread([]{
        try{ winrt::init_apartment(winrt::apartment_type::multi_threaded); }catch(...){}
        try{
            using namespace winrt::Windows::Devices::Radios;
            auto radios=Radio::GetRadiosAsync().get(); int w=-1,b=-1;
            for(auto const& r:radios){ int on = r.State()==RadioState::On? 1 : 0;
                if(r.Kind()==RadioKind::WiFi) w=std::max(w,on); else if(r.Kind()==RadioKind::Bluetooth) b=std::max(b,on); }
            g_radioWifi=w; g_radioBt=b;
        }catch(...){}
        g_radioBusy=false;
    }).detach();
}
static void RadioToggleAsync(int kind){          // 0 Wi-Fi, 1 Bluetooth
    int cur = kind==0? g_radioWifi.load() : g_radioBt.load();
    if(kind==0) g_radioWifi = cur==1? 0 : 1; else g_radioBt = cur==1? 0 : 1;   // optimistic
    std::thread([kind,cur]{
        try{ winrt::init_apartment(winrt::apartment_type::multi_threaded); }catch(...){}
        try{
            using namespace winrt::Windows::Devices::Radios;
            Radio::RequestAccessAsync().get();
            auto radios=Radio::GetRadiosAsync().get();
            auto want = kind==0? RadioKind::WiFi : RadioKind::Bluetooth;
            for(auto const& r:radios) if(r.Kind()==want) r.SetStateAsync(cur==1? RadioState::Off : RadioState::On).get();
        }catch(...){}
        Sleep(400); g_radioBusy=false; RadioRefreshAsync();
    }).detach();
}

// ============================================================================================ recorder
// "Fullscreen" / "All screens" record with ffmpeg (gdigrab) when it is installed - a real file, pausable -
// and "Game Bar" drives Windows' own capture (Win+Alt+R, the focused window) like the classic panel did.
static HANDLE    g_ffProc=nullptr, g_ffIn=nullptr;
static bool      g_ffPaused=false;
static ULONGLONG g_ffPauseAt=0, g_ffPausedMs=0;
static std::string g_ffFile;
static std::wstring V2FindFfmpeg(){
    wchar_t b[MAX_PATH]; if(SearchPathW(nullptr,L"ffmpeg.exe",nullptr,MAX_PATH,b,nullptr)) return b;
    wchar_t la[MAX_PATH]; if(GetEnvironmentVariableW(L"LOCALAPPDATA",la,MAX_PATH)){
        std::wstring p=std::wstring(la)+L"\\Microsoft\\WinGet\\Links\\ffmpeg.exe";
        if(GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES) return p; }
    return L"";
}
static void V2SuspendProc(HANDLE h,bool suspend){
    using NtFn = LONG(NTAPI*)(HANDLE);
    HMODULE nt=GetModuleHandleW(L"ntdll.dll"); if(!nt||!h) return;
    NtFn f=(NtFn)GetProcAddress(nt,suspend? "NtSuspendProcess" : "NtResumeProcess"); if(f) f(h);
}
static bool V2RecStart(int mode){                 // 0 current screen, 1 all screens
    std::wstring ff=V2FindFfmpeg(); if(ff.empty()) return false;
    std::string dir=CapturesDir(); if(dir.empty()) return false;
    CreateDirectoryW(U82W(dir).c_str(),nullptr);
    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn); char nm[64]; strftime(nm,64,"Aether %Y-%m-%d %H-%M-%S.mp4",&lt);
    g_ffFile=dir+"\\"+nm;
    wchar_t fr[16]; swprintf(fr,16,L"%d",std::clamp(g_recFps,10,240));
    // -progress pipe:1 feeds the recording checker its real frame rate and file size
    std::wstring args=L"\""+ff+L"\" -hide_banner -loglevel error -nostats -progress pipe:1 -y -f gdigrab -framerate "+std::wstring(fr)+L" -draw_mouse 1 ";
    if(mode==0){ POINT cp; GetCursorPos(&cp); const RECT& r=MonRect(MonIndexAt(cp));
        wchar_t g[128]; swprintf(g,128,L"-offset_x %ld -offset_y %ld -video_size %ldx%ld ",r.left,r.top,r.right-r.left,r.bottom-r.top); args+=g; }
    args+=L"-i desktop ";
    int ain=0;
    if(!g_recAudioSys.empty()){ args+=L"-f dshow -i audio=\""+U82W(g_recAudioSys)+L"\" "; ain++; }
    if(!g_recAudioMic.empty()){ args+=L"-f dshow -i audio=\""+U82W(g_recAudioMic)+L"\" "; ain++; }
    if(ain==2) args+=L"-filter_complex \"[1:a][2:a]amix=inputs=2:duration=longest[a]\" -map 0:v -map \"[a]\" ";
    else if(ain==1) args+=L"-map 0:v -map 1:a ";
    args+=L"-c:v libx264 -preset ultrafast -crf 22 -pix_fmt yuv420p ";
    if(ain) args+=L"-c:a aac -b:a 192k ";
    args+=L"\""+U82W(g_ffFile)+L"\"";
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE}; HANDLE rd=nullptr, wr=nullptr, ord=nullptr, owr=nullptr;
    if(!CreatePipe(&rd,&wr,&sa,0)) return false;
    if(!CreatePipe(&ord,&owr,&sa,0)){ CloseHandle(rd); CloseHandle(wr); return false; }
    SetHandleInformation(wr,HANDLE_FLAG_INHERIT,0); SetHandleInformation(ord,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOW si{sizeof(si)}; si.dwFlags=STARTF_USESTDHANDLES; si.hStdInput=rd; si.hStdOutput=owr; si.hStdError=nullptr;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cl(args.begin(),args.end()); cl.push_back(0);
    BOOL ok=CreateProcessW(nullptr,cl.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
    CloseHandle(rd); CloseHandle(owr);
    if(!ok){ CloseHandle(wr); CloseHandle(ord); return false; }
    CloseHandle(pi.hThread); g_ffProc=pi.hProcess; g_ffIn=wr;
    g_recFpsNow=0; g_recBytes=0;
    std::thread([ord]{
        std::string buf; char b[1024]; DWORD n=0;
        while(ReadFile(ord,b,sizeof(b),&n,nullptr)&&n>0){
            buf.append(b,n); size_t e;
            while((e=buf.find('\n'))!=std::string::npos){
                std::string line=buf.substr(0,e); buf.erase(0,e+1);
                if(!line.empty()&&line.back()=='\r') line.pop_back();
                if(line.rfind("fps=",0)==0) g_recFpsNow=(float)atof(line.c_str()+4);
                else if(line.rfind("total_size=",0)==0){ long long v=atoll(line.c_str()+11); if(v>0) g_recBytes=(uint64_t)v; }
            }
        }
        CloseHandle(ord);
    }).detach();
    g_ffPaused=false; g_ffPausedMs=0; g_recording=true; g_recStart=GetTickCount64();
    ReadRecordingSettings(); g_recAudio=ain>0; g_recAudioWhat= ain==2? "system audio + microphone" : !g_recAudioSys.empty()? "system audio" : ain? "microphone" : "no audio"; g_recDir=dir;
    return true;
}
static void V2RecStop(){
    if(!g_ffProc){ return; }
    if(g_ffPaused){ V2SuspendProc(g_ffProc,false); g_ffPaused=false; }
    DWORD w=0; WriteFile(g_ffIn,"q",1,&w,nullptr); CloseHandle(g_ffIn); g_ffIn=nullptr;
    HANDLE p=g_ffProc; g_ffProc=nullptr; g_recording=false;
    std::thread([p]{ if(WaitForSingleObject(p,6000)!=WAIT_OBJECT_0) TerminateProcess(p,0); CloseHandle(p); }).detach();
}
static void V2RecPause(){
    if(!g_ffProc) return;
    g_ffPaused=!g_ffPaused; V2SuspendProc(g_ffProc,g_ffPaused);
    if(g_ffPaused) g_ffPauseAt=GetTickCount64(); else g_ffPausedMs+=GetTickCount64()-g_ffPauseAt;
}
static void V2RecToggle(){
    if(g_ffProc){ V2RecStop(); return; }
    if(g_recording && !g_ffProc){ ToggleRecording(); return; }          // a Game Bar take is running
    if(g_recMode==2 || !V2RecStart(g_recMode)) ToggleRecording();      // Game Bar, or ffmpeg missing
}

// ============================================================================================ pieces
static std::string V2Ago(ULONGLONG at){
    ULONGLONG s=(GetTickCount64()-at)/1000; char b[16];
    if(s<60) return "now"; if(s<3600){ snprintf(b,16,"%llum",s/60); return b; }
    if(s<86400){ snprintf(b,16,"%lluh",s/3600); return b; } snprintf(b,16,"%llud",s/86400); return b;
}
// M3 switch with the check / close glyph in the thumb
static bool V2Switch(ImDrawList* dl,ImGuiIO& io,ImVec2 c,bool on,int id,bool click){
    const float w=46,h=26; ImVec2 a=V(c.x-w*0.5f,c.y-h*0.5f), b=V(c.x+w*0.5f,c.y+h*0.5f);
    bool hov=io.MousePos.x>=a.x&&io.MousePos.x<b.x&&io.MousePos.y>=a.y&&io.MousePos.y<b.y;
    float t=Cael::anim(id,on? 1.0f : 0.0f,Cael::DUR_FAST_SPATIAL,Cael::FAST_SPATIAL);
    ImU32 tr=Mix(Mix(COL_CARD2,COL_INK2,0.25f),Mix(COL_INK,COL_GOLD,0.25f),std::clamp(t,0.0f,1.0f));
    dl->AddRectFilled(a,b,tr,h*0.5f);
    if(t<0.5f) dl->AddRect(a,b,WithA(COL_INK2,(int)(160*(1-t*2))),h*0.5f,0,1.6f);
    float r = 8.0f+3.0f*t+(hov? 1.0f : 0.0f);
    ImVec2 k=V(a.x+h*0.5f+(w-h)*t,c.y);
    dl->AddCircleFilled(k,r,Mix(COL_INK2,M3OnPrimary(),std::clamp(t,0.0f,1.0f)),20);
    MsIcon(dl,t>0.5f? "check" : "close",k,r*1.3f,t>0.5f? Mix(COL_INK,COL_GOLD,0.25f) : Mix(COL_CARD2,COL_INK2,0.25f));
    return hov&&click;
}
// a card row: round icon, title, subtitle
static void V2Row(ImDrawList* dl,ImVec2 a,float h,const char* icon,const char* title,const std::string& sub,bool lit){
    float R=h*0.5f-2;
    ImVec2 ic=V(a.x+R+2,a.y+h*0.5f);
    dl->AddCircleFilled(ic,R,lit? Mix(COL_INK,COL_GOLD,0.25f) : Mix(COL_CARD2,COL_INK2,0.22f),28);
    MsIcon(dl,icon,ic,R*1.05f,lit? M3OnPrimary() : COL_INK);
    float tx=ic.x+R+12;
    TextAt(dl,g_fMed,15.5f,V(tx,a.y+h*0.5f-17),COL_INK,title);
    TextAt(dl,g_fSml,13,V(tx,a.y+h*0.5f+2),COL_INK2,sub.c_str());
}
// original pixel scene for an empty notification list: a little sprout-blob hopping past a cactus, a cloud drifting
static void V2EmptyScene(ImDrawList* dl,ImVec2 c,float px,ImU32 col){
    static const char* CRIT[]={
        "....X.....",
        "...XX.....",
        "..XXXXXX..",
        ".XXXXXXXX.",
        "XX.XXXX.XX",
        "XXXXXXXXXX",
        "XXXX..XXXX",
        ".XXXXXXXX.",
        "..X....X.."};
    static const char* CACT[]={
        "..XX..",
        "..XX.X",
        "X.XX.X",
        "X.XXXX",
        "XXXX..",
        "..XX..",
        "..XX..",
        "..XX.."};
    static const char* CLOUD[]={
        "....XXX.....",
        "..XX...XX...",
        ".X.......XX.",
        "XXXXXXXXXXXX"};
    auto blit=[&](const char* const* rows,int n,ImVec2 tl,ImU32 cc){
        for(int y=0;y<n;y++) for(int x=0;rows[y][x];x++) if(rows[y][x]=='X')
            dl->AddRectFilled(V(tl.x+x*px,tl.y+y*px),V(tl.x+(x+1)*px,tl.y+(y+1)*px),cc); };
    float t=(float)ImGui::GetTime();
    float ground=c.y+px*4;
    dl->AddRectFilled(V(c.x-px*26,ground),V(c.x+px*26,ground+px*0.7f),col);
    for(int i=0;i<6;i++){ float gx=c.x-px*22+fmodf(i*px*9.0f - t*px*6.0f + px*60,px*48); dl->AddRectFilled(V(gx,ground+px*1.6f),V(gx+px*(i%2? 1.0f:2.0f),ground+px*2.2f),col); }
    float hop=fabsf(sinf(t*2.4f))*px*6.0f;
    blit(CRIT,9,V(c.x-px*18,ground-px*9-hop),col);
    blit(CACT,8,V(c.x+px*14,ground-px*8),col);
    float cx=fmodf(t*px*2.5f,px*60.0f)-px*30.0f;
    blit(CLOUD,4,V(c.x+cx,ground-px*22),WithA(col,160));
}

// ============================================================================================ the panel
// Measured off the reference at 1080p (reference\v2\c\sideFull.png) in PHYSICAL pixels and scaled by 1/uiScale,
// so on this machine it lands the same size as the video rather than a shrunken copy of it.
static void DrawSidebarV2(float raw){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x, H=io.DisplaySize.y;
    const float S=1.0f/std::max(0.5f,g_uiScale);
    float rv=std::min(g_sideReveal,1.3f);
    bool click=io.MouseClicked[0];
    static ULONGLONG radioAt=0; if(GetTickCount64()-radioAt>3000){ radioAt=GetTickCount64(); RadioRefreshAsync(); }
    // staged entrance: each block slides in from the edge a beat after the one above it
    auto stage=[&](int i)->float{ float t=std::clamp((raw-0.10f*i)/0.60f,0.0f,1.0f); return Cael::eval(Cael::DEFAULT_SPATIAL,t); };

    { std::vector<std::pair<std::string,std::vector<uint8_t>>> pend;              // keep notification icons decoding
      { std::lock_guard<std::mutex> lk(g_nIconMtx); pend.swap(g_nIconBytes); }
      int aw=g_mdArtW, ah=g_mdArtH;
      for(auto& p:pend){ ID3D11ShaderResourceView* t=DecodeBufferToTexture(p.second.data(),p.second.size());
          std::lock_guard<std::mutex> lk(g_nIconMtx); g_nIcons[p.first]=t; }
      g_mdArtW=aw; g_mdArtH=ah; }

    // ---- geometry ----
    const bool born=FrameBornOn() && !g_mons.empty();
    float bT=born? FrameInset(EDGE_TOP) : 10.0f, bB=born? FrameInset(EDGE_BOTTOM) : 10.0f, bR=born? FrameInset(EDGE_RIGHT) : 10.0f;
    float pw=(float)std::clamp(g_sideWidth,300,900)*S;
    float y0=bT+(born? 0.0f : 0.0f), y1=H-bB;
    std::vector<std::string> acts; { size_t i=0; while(i<=g_sideActions.size()){ size_t e=g_sideActions.find(',',i);
        std::string a=Tml::Trim(g_sideActions.substr(i,e==std::string::npos? std::string::npos : e-i)); if(!a.empty()) acts.push_back(a);
        if(e==std::string::npos) break; i=e+1; } }
    const float BW=78*S, BH=72*S, BG=16*S, BUMPX=14*S, BUMPY=22*S, BR=14*S;
    float bumpW = acts.empty()? 0.0f : BW+BUMPX*2;
    // closing slides the action column out WITH the panel: it used to stay parked at the screen edge (and the header
    // buttons with it) until the reveal reached zero. Over 1 the overshoot just widens the panel, as before.
    float px = rv>=1.0f? W-bR-pw*rv : W-bR-pw*rv+(bumpW+14*S)*(1.0f-rv);
    float bumpH = acts.empty()? 0.0f : acts.size()*BH+(acts.size()-1)*BG+BUMPY*2;
    float bumpY0=y0+(y1-y0)*0.513f-bumpH*0.5f, bumpY1=bumpY0+bumpH;
    float bx0=px-bumpW;
    int mon=FrameMon();
    // a solid backing under the frame material: the sidebar sits over app windows, which must not show through it
    { ImU32 solidC = FrameMaterialLive(mon)? (g_darkUI? IM_COL32(9,10,13,255) : IM_COL32(226,230,234,255)) : WithA(COL_DESKBG,255);
      int oa=(int)(255*std::clamp(g_sideOpacity,0.0f,1.0f)*std::clamp(raw*1.6f,0.0f,1.0f));
      float sh=std::min(g_panelRound,18.0f*S);
      if(oa>0){ dl->AddRectFilled(V(px+1,y0+sh),V(W,y1-sh),WithA(solidC,oa),g_panelRound,ImDrawFlags_RoundCornersLeft);
          if(bumpW>0) dl->AddRectFilled(V(bx0+1,bumpY0+1),V(px+8*S,bumpY1-1),WithA(solidC,oa),20*S,ImDrawFlags_RoundCornersLeft); } }
    if(born){
        FrameEdgePanel(dl,mon,V(0,0),V(W,H),EDGE_RIGHT,y0,y1,std::max(bR,W-px),bR,g_panelRound,1.0f,0.0f,0.0f);
        if(bumpW>0){                                   // the action column: part of the SAME surface, with concave fillets
            const float f=std::min(18*S,bumpH*0.25f), r=20*S; const int NA=10;
            FShape sh;
            auto add=[&](float x,float y,bool hard=false){ FShapeAdd(sh,V(x,y),hard); };
            add(px+6*S,bumpY0-f,true);
            add(px,bumpY0-f);
            for(int i=1;i<=NA;i++){ float t=1.5707963f*(float)i/NA; add(px-f+f*cosf(t), bumpY0-f+f*sinf(t)); }
            add(bx0+r,bumpY0);
            for(int i=1;i<=NA;i++){ float t=1.5707963f*(float)i/NA; add(bx0+r-r*sinf(t), bumpY0+r-r*cosf(t)); }
            add(bx0,bumpY1-r);
            for(int i=1;i<=NA;i++){ float t=1.5707963f*(float)i/NA; add(bx0+r-r*cosf(t), bumpY1-r+r*sinf(t)); }
            add(px-f,bumpY1);
            for(int i=1;i<=NA;i++){ float t=1.5707963f*(float)i/NA; add(px-f+f*sinf(t), bumpY1+f-f*cosf(t), i==NA); }
            add(px+6*S,bumpY1+f,true);
            FrameMaterialShape(dl,mon,V(0,0),V(W,H),sh,1.0f);
        }
    } else {
        GlassPanel(dl,V(px,y0),V(px+pw,y1),g_panelRound,ImDrawFlags_RoundCornersAll,g_sideAcrylic,raw);
        if(bumpW>0) GlassPanel(dl,V(bx0,bumpY0),V(px+2,bumpY1),20*S,ImDrawFlags_RoundCornersLeft,g_sideAcrylic,raw);
    }
    g_sideRect=PanelHitRect(PRect{std::min(px,bx0),y0,W-std::min(px,bx0),y1-y0});
    g_qsPanelOn=false;
    dl->PushClipRect(V(std::min(px,bx0),y0),V(W,y1),true);

    ImU32 panelC=PanelCol(255);
    ImU32 cardC =Mix(panelC,COL_CARD2,0.42f);
    ImU32 chipC =Mix(panelC,COL_CARD2,0.95f);
    ImU32 lightC=Mix(COL_INK,COL_INK2,0.12f);
    ImU32 onLight=Mix(panelC,IM_COL32(0,0,0,255),0.35f);
    ImU32 salmon=IM_COL32(232,142,134,255), salmonInk=IM_COL32(94,24,22,255);

    // ---- action column ----
    { float e=stage(1); int ali=(int)(255*e);
      static ULONGLONG logoutArm=0;
      for(size_t k=0;k<acts.size();k++){
          float cx=bx0+BUMPX+BW*0.5f+(1.0f-e)*30*S, cy=bumpY0+BUMPY+BH*0.5f+k*(BH+BG);
          ImVec2 ba=V(cx-BW*0.5f,cy-BH*0.5f), bb=V(cx+BW*0.5f,cy+BH*0.5f);
          const std::string& a=acts[k];
          bool hov=io.MousePos.x>=ba.x&&io.MousePos.x<bb.x&&io.MousePos.y>=ba.y&&io.MousePos.y<bb.y;
          float ha=HoverAnim(0x68000+(int)k,hov);
          if(a=="avatar"){
              ImVec2 c=V(cx,cy); float R=BH*0.5f*(1.0f+0.05f*ha);
              if(!g_sideAvatar.empty()){ ImgAnim* ia=GetImg(g_sideAvatar);
                  if(ia&&!ia->frames.empty()){ float sc=std::min(BW/ia->w,BH/(float)ia->h)*(1.0f+0.05f*ha); float w=ia->w*sc, h=ia->h*sc;
                      dl->AddImage((ImTextureID)ia->frames[std::min(ia->frame,(int)ia->frames.size()-1)],V(c.x-w*0.5f,c.y-h*0.5f),V(c.x+w*0.5f,c.y+h*0.5f),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,ali)); } }
              else ProfileAvatar(dl,c,R*0.92f,0.0f);
              continue;
          }
          bool armed = a=="logout" && GetTickCount64()<logoutArm;
          ImU32 bg = armed? salmon : Mix(chipC,Mix(COL_INK2,chipC,0.45f),ha);
          dl->AddRectFilled(ba,bb,MulA(bg,e),BR);
          const char* ic = a=="logout"? "logout" : a=="power"? "power_settings_new" : a=="updates"? "downloading" :
                           a=="reload"? "sync" : a=="lock"? "lock" : a=="settings"? "settings" : a=="sleep"? "bedtime" : a.c_str();
          MsIcon(dl,ic,V(cx,cy),32*S,MulA(armed? salmonInk : COL_INK,e));
          if(hov && click){
              if(a=="logout"){ if(armed){ logoutArm=0; ExitWindowsEx(EWX_LOGOFF,SHTDN_REASON_FLAG_PLANNED); } else logoutArm=GetTickCount64()+2500; }
              else if(a=="power") g_sessShow=true;
              else if(a=="updates") AetherShellExec(nullptr,L"open",L"ms-settings:windowsupdate",nullptr,nullptr,SW_SHOWNORMAL);
              else if(a=="reload") RestartShell();
              else if(a=="lock") DoLock();
              else if(a=="settings") g_setShow=true;
              else if(a=="sleep") DoSleep();
          }
      }
    }

    // ---- main column, bottom-up ----
    const float L=px+8*S, R=std::min(W-bR,px+pw)-14*S, CW=R-L;   // the content rides with the panel
    const float CR=14*S, CP=12*S, GAP=10*S;
    std::vector<std::string> tg; { size_t i=0; while(i<=g_sideToggles.size()){ size_t e=g_sideToggles.find(',',i);
        std::string a=Tml::Trim(g_sideToggles.substr(i,e==std::string::npos? std::string::npos : e-i)); if(!a.empty()) tg.push_back(a);
        if(e==std::string::npos) break; i=e+1; } }
    const int TC=4; int trows=(int)((tg.size()+TC-1)/TC);
    const float PH=42*S, PG=7*S;
    float qH = tg.empty()? 0.0f : 44*S+trows*PH+(trows-1)*10*S+CP;
    float recH=127*S, kaH=75*S;
    float yb=y1-10*S;
    auto slideX=[&](int i){ return (1.0f-stage(i))*40*S; };

    // quick toggles
    if(qH>0){ float e=stage(5), ox=slideX(5);
        ImVec2 a=V(L+ox,yb-qH), b=V(R+ox,yb); dl->AddRectFilled(a,b,MulA(cardC,e),CR);
        TextAt(dl,g_fMed,18*S,V(a.x+CP,a.y+11*S),MulA(COL_INK,e),"Quick Toggles");
        float cw=(CW-CP*2-(TC-1)*PG)/TC;
        for(size_t i=0;i<tg.size();i++){
            int r=(int)i/TC, cc=(int)i%TC;
            ImVec2 pa=V(a.x+CP+cc*(cw+PG),a.y+45*S+r*(PH+10*S)), pb=V(pa.x+cw,pa.y+PH);
            const std::string& k=tg[i];
            bool on=false; const char* ic="toggle_on";
            if(k=="wifi"){ on=g_radioWifi.load()==1 || (g_radioWifi.load()<0 && g_st.online); ic=on? "wifi" : "wifi_off"; }
            else if(k=="bluetooth"){ on=g_radioBt.load()==1; ic=on? "bluetooth" : "bluetooth_disabled"; }
            else if(k=="mic"){ on=!g_micMuted; ic=on? "mic" : "mic_off"; }
            else if(k=="settings"){ ic="settings"; }
            else if(k=="gamemode"){ on=g_dnd&&g_keepAwake; ic="sports_esports"; }
            else if(k=="dnd"){ on=g_dnd; ic="notifications_off"; }
            else if(k=="keepawake"){ on=g_keepAwake; ic="coffee"; }
            else if(k=="theme"){ on=g_darkUI; ic=on? "dark_mode" : "light_mode"; }
            else if(k=="record"){ on=g_recording; ic="screen_record"; }
            else if(k=="lock"){ ic="lock"; }
            else if(k=="vpn"){ ic="vpn_key"; }
            else if(k=="volume"){ ic="volume_up"; }
            else ic=k.c_str();
            bool hov=io.MousePos.x>=pa.x&&io.MousePos.x<pb.x&&io.MousePos.y>=pa.y&&io.MousePos.y<pb.y;
            float ha=HoverAnim(0x68100+(int)i,hov);
            float onA=Cael::anim(0x68180+(int)i,on? 1.0f : 0.0f,Cael::DUR_DEFAULT_EFFECTS,Cael::DEFAULT_EFFECTS);
            ImU32 bg=Mix(Mix(chipC,Mix(chipC,COL_INK2,0.35f),ha),Mix(lightC,COL_INK,0.25f*ha),onA);
            float rr=14*S*(1.0f-0.35f*onA)+PH*0.5f*0.35f*onA;     // an active toggle rounds towards a pill
            dl->AddRectFilled(pa,pb,MulA(bg,e),rr);
            MsIcon(dl,ic,V((pa.x+pb.x)*0.5f,(pa.y+pb.y)*0.5f),24*S,MulA(Mix(COL_INK,onLight,onA),e));
            if(hov && click){
                if(k=="wifi") RadioToggleAsync(0);
                else if(k=="bluetooth") RadioToggleAsync(1);
                else if(k=="mic"){ g_micMuted=!g_micMuted; SetMicMute(g_micMuted); }
                else if(k=="settings") g_setShow=true;
                else if(k=="gamemode"){ bool gm=!(g_dnd&&g_keepAwake); g_dnd=gm; g_keepAwake=gm; ApplyKeepAwake(); SaveConfig(); }
                else if(k=="dnd"){ g_dnd=!g_dnd; SaveConfig(); }
                else if(k=="keepawake"){ g_keepAwake=!g_keepAwake; ApplyKeepAwake(); }
                else if(k=="theme"){ g_themeMode=g_darkUI?1:2; ApplyThemeMode(); SetLightTheme(!g_darkUI); SaveConfig(); g_deskDirty=true; }
                else if(k=="record") V2RecToggle();
                else if(k=="lock") DoLock();
                else if(k=="vpn") VpnQuickToggle();
            }
            if(hov && io.MouseClicked[1]){
                if(k=="wifi"){ g_sideWifiOn=true; g_sideMixerOn=false; WifiScanAsync(); } else if(k=="bluetooth"){ g_sideView=2; RefreshBt(); }
                else if(k=="volume"||k=="mic") g_sideView=4;
            }
        }
        yb=a.y-GAP;
    }
    // screen recorder
    { float e=stage(4), ox=slideX(4);
      ImVec2 a=V(L+ox,yb-recH), b=V(R+ox,yb); dl->AddRectFilled(a,b,MulA(cardC,e),CR);
      float icR=25*S; ImVec2 ic=V(a.x+CP+icR,a.y+37*S);
      float lit=Cael::anim(0x68220,g_recording? 1.0f : 0.0f,Cael::DUR_DEFAULT_EFFECTS,Cael::DEFAULT_EFFECTS);
      dl->AddCircleFilled(ic,icR,MulA(Mix(chipC,lightC,lit),e),32);
      MsIcon(dl,"screen_record",ic,30*S,MulA(Mix(COL_INK,onLight,lit),e));
      float tx=ic.x+icR+11*S;
      // mode split button
      static bool mOpen=false; static float mAnim=0;
      static const char* MODES[]={"Fullscreen","All screens","Game Bar"};
      float sw=158*S, sh=36*S; ImVec2 sa=V(b.x-CP-sw,a.y+19*S);
      // the labels stop short of the split button - "Screen Recorder" used to run underneath it
      { float lw=sa.x-8*S-tx;
        TextAt(dl,g_fMed,19*S,V(tx,a.y+16*S),MulA(COL_INK,e),Clip(g_fMed,19*S,"Screen Recorder",lw).c_str());
        TextAt(dl,g_fSml,17*S,V(tx,a.y+40*S),MulA(COL_INK2,e),Clip(g_fSml,17*S,g_recording? (g_ffPaused? "Paused" : "Running\xE2\x80\xA6") : "Idle",lw).c_str()); }
      { float cw2=33*S; ImVec2 s0=sa, s1=V(sa.x+sw-cw2-4*S,sa.y+sh), c0=V(s1.x+4*S,sa.y), c1=V(sa.x+sw,sa.y+sh);
        bool h1=io.MousePos.x>=s0.x&&io.MousePos.x<s1.x&&io.MousePos.y>=s0.y&&io.MousePos.y<s1.y;
        bool h2=io.MousePos.x>=c0.x&&io.MousePos.x<c1.x&&io.MousePos.y>=c0.y&&io.MousePos.y<c1.y;
        float ha1=HoverAnim(0x68230,h1), ha2=HoverAnim(0x68231,h2);
        ImU32 sb=Mix(panelC,COL_CARD2,0.75f);
        dl->AddRectFilled(s0,s1,MulA(Mix(sb,COL_INK2,0.12f*ha1),e),sh*0.5f,ImDrawFlags_RoundCornersLeft);
        dl->AddRectFilled(c0,c1,MulA(Mix(sb,COL_INK2,0.12f*ha2),e),sh*0.5f,ImDrawFlags_RoundCornersRight);
        const char* ml=MODES[std::clamp(g_recMode,0,2)]; float fs=16*S;
        float tw=22*S+TextW(g_fSml,fs,ml); float mx=s0.x+((s1.x-s0.x)-tw)*0.5f;
        MsIcon(dl,g_recMode==2? "sports_esports" : g_recMode==1? "desktop_windows" : "fit_screen",V(mx+8*S,sa.y+sh*0.5f),18*S,MulA(COL_INK2,e));
        TextAt(dl,g_fSml,fs,V(mx+22*S,sa.y+(sh-fs)*0.5f-1),MulA(COL_INK2,e),ml);
        MsIcon(dl,"expand_more",V((c0.x+c1.x)*0.5f,sa.y+sh*0.5f),20*S,MulA(COL_INK2,e));
        bool tog=false; if((h1||h2)&&click&&!g_recording){ mOpen=!mOpen; tog=true; }
        // row two
        float ry=a.y+79*S, rh=42*S;
        if(g_recording){
            ImVec2 ca=V(a.x+CP,ry+7*S), cb=V(ca.x+46*S,ry+35*S);
            float blink=0.75f+0.25f*sinf((float)ImGui::GetTime()*4.0f);
            dl->AddRectFilled(ca,cb,MulA(WithA(salmon,(int)(255*(g_ffPaused? 0.6f : blink))),e),14*S);
            TextAt(dl,g_fMed,15*S,V(ca.x+(46*S-TextW(g_fMed,15*S,"REC"))*0.5f,ca.y+5*S),MulA(salmonInk,e),"REC");
            ULONGLONG ms=GetTickCount64()-g_recStart-g_ffPausedMs-(g_ffPaused? GetTickCount64()-g_ffPauseAt : 0);
            char t[48]; snprintf(t,48,"Recording for %llu:%02llu",ms/60000,(ms/1000)%60);
            TextAt(dl,g_fSml,18*S,V(cb.x+9*S,ry+10*S),MulA(COL_INK,e),t);
            ImVec2 st0=V(b.x-CP-52*S,ry), st1=V(b.x-CP,ry+rh);
            ImVec2 pa0=V(st0.x-5*S-52*S,ry), pa1=V(st0.x-5*S,ry+rh);
            bool hs=io.MousePos.x>=st0.x&&io.MousePos.x<st1.x&&io.MousePos.y>=st0.y&&io.MousePos.y<st1.y;
            bool hp=io.MousePos.x>=pa0.x&&io.MousePos.x<pa1.x&&io.MousePos.y>=pa0.y&&io.MousePos.y<pa1.y;
            float hsa=HoverAnim(0x68240,hs), hpa=HoverAnim(0x68241,hp);
            if(g_ffProc){ dl->AddRectFilled(pa0,pa1,MulA(Mix(chipC,COL_INK2,0.25f*hpa),e),rh*0.5f);
                MsIcon(dl,g_ffPaused? "play_arrow" : "pause",V((pa0.x+pa1.x)*0.5f,ry+rh*0.5f),24*S,MulA(COL_INK,e));
                if(hp&&click) V2RecPause(); }
            dl->AddRectFilled(st0,st1,MulA(Mix(salmon,IM_COL32(255,255,255,255),0.12f*hsa),e),rh*0.5f);
            MsIcon(dl,"stop",V((st0.x+st1.x)*0.5f,ry+rh*0.5f),22*S,MulA(salmonInk,e));
            if(hs&&click) V2RecToggle();
        } else {
            std::string hint = g_recMode==2? "Records the focused window" : (V2FindFfmpeg().empty()? "ffmpeg not found - using Game Bar" : "Saves to Videos\\Captures");
            TextAt(dl,g_fSml,15*S,V(a.x+CP+4*S,ry+12*S),MulA(COL_INK2,e),hint.c_str());
            ImVec2 st0=V(b.x-CP-104*S,ry), st1=V(b.x-CP,ry+rh);
            bool hs=io.MousePos.x>=st0.x&&io.MousePos.x<st1.x&&io.MousePos.y>=st0.y&&io.MousePos.y<st1.y;
            float hsa=HoverAnim(0x68242,hs);
            dl->AddRectFilled(st0,st1,MulA(Mix(salmon,IM_COL32(255,255,255,255),0.12f*hsa),e),rh*0.5f);
            MsIcon(dl,"radio_button_checked",V(st0.x+24*S,ry+rh*0.5f),20*S,MulA(salmonInk,e));
            TextAt(dl,g_fMed,15.5f*S,V(st0.x+40*S,ry+rh*0.5f-10*S),MulA(salmonInk,e),"Record");
            if(hs&&click&&!mOpen) V2RecToggle();
        }
        std::vector<std::string> items={"Fullscreen","All screens","Game Bar (focused window)"};
        int p=V2Menu(dl,io,mOpen,mAnim,sa,V(sa.x+sw,sa.y+sh),items,g_recMode,false,tog);
        if(p>=0){ g_recMode=p; SaveConfig(); } }
      yb=a.y-GAP; }
    // keep awake
    { float e=stage(3), ox=slideX(3);
      ImVec2 a=V(L+ox,yb-kaH), b=V(R+ox,yb); dl->AddRectFilled(a,b,MulA(cardC,e),CR);
      float icR=25*S; ImVec2 ic=V(a.x+CP+icR,a.y+kaH*0.5f);
      float lit=Cael::anim(0x68320,g_keepAwake? 1.0f : 0.0f,Cael::DUR_DEFAULT_EFFECTS,Cael::DEFAULT_EFFECTS);
      dl->AddCircleFilled(ic,icR,MulA(Mix(chipC,lightC,lit),e),32);
      MsIcon(dl,"coffee",ic,28*S,MulA(Mix(COL_INK,onLight,lit),e));
      float tx=ic.x+icR+11*S;
      // M3 switch
      float swW=44*S, swH=26*S; ImVec2 s0=V(b.x-CP-swW,a.y+(kaH-swH)*0.5f), s1=V(s0.x+swW,s0.y+swH);
      // the labels stop short of the switch - "Normal power management" used to run underneath it
      { float lw=s0.x-10*S-tx;
        TextAt(dl,g_fMed,19*S,V(tx,a.y+14*S),MulA(COL_INK,e),Clip(g_fMed,19*S,"Keep Awake",lw).c_str());
        TextAt(dl,g_fSml,17*S,V(tx,a.y+38*S),MulA(COL_INK2,e),Clip(g_fSml,17*S,g_keepAwake? "Staying awake" : "Normal power management",lw).c_str()); }
      bool hs=io.MousePos.x>=s0.x-6&&io.MousePos.x<s1.x+6&&io.MousePos.y>=s0.y-6&&io.MousePos.y<s1.y+6;
      float t=Cael::anim(0x68300,g_keepAwake? 1.0f : 0.0f,Cael::DUR_FAST_SPATIAL,Cael::FAST_SPATIAL);
      dl->AddRectFilled(s0,s1,MulA(Mix(Mix(panelC,COL_CARD2,0.55f),lightC,std::clamp(t,0.0f,1.0f)),e),swH*0.5f);
      float kr=(9.0f+3.0f*t+(hs? 1.0f : 0.0f))*S;
      ImVec2 kc=V(s0.x+swH*0.5f+(swW-swH)*t,s0.y+swH*0.5f);
      dl->AddCircleFilled(kc,kr,MulA(Mix(Mix(COL_INK2,COL_INK,0.3f),onLight,std::clamp(t,0.0f,1.0f)),e),24);
      MsIcon(dl,t>0.5f? "check" : "close",kc,kr*1.4f,MulA(t>0.5f? lightC : Mix(panelC,COL_CARD2,0.6f),e));
      if(hs&&click){ g_keepAwake=!g_keepAwake; ApplyKeepAwake(); }
      yb=a.y-GAP; }
    // divider
    { float e=stage(2); dl->AddLine(V(L,yb),V(R,yb),MulA(Mix(panelC,COL_INK2,0.22f),e),1.0f*S); }
    float ny1=yb-10*S;

    // ---- notifications ----
    std::vector<Notif> snap; { std::lock_guard<std::mutex> lk(g_notifMtx); snap=g_notifs; }
    static std::unordered_map<uint32_t,ULONGLONG> gone;
    { ULONGLONG now=GetTickCount64(); for(auto it=gone.begin(); it!=gone.end();) it=(now-it->second>6000)? gone.erase(it) : std::next(it);
      snap.erase(std::remove_if(snap.begin(),snap.end(),[&](const Notif& n){ return gone.count(n.id)>0; }),snap.end()); }
    std::sort(snap.begin(),snap.end(),[](const Notif& a,const Notif& b){ return a.at>b.at; });
    struct G{ std::string key,app,aumid; std::vector<Notif> items; };
    std::vector<G> groups;
    for(auto& n:snap){ std::string key=n.app.empty()? n.aumid : n.app; G* g=nullptr; for(auto& x:groups) if(x.key==key){ g=&x; break; }
        if(!g){ groups.push_back({key,n.app,n.aumid,{}}); g=&groups.back(); } g->items.push_back(n); }
    float e0=stage(0);
    float ny=y0+18*S;
    char hd[40]; if(snap.empty()) snprintf(hd,40,"Notifications"); else snprintf(hd,40,"%zu notification%s",snap.size(),snap.size()==1? "" : "s");
    if(g_sideMixerOn) snprintf(hd,40,"Audio mixer");
    if(g_sideWifiOn) snprintf(hd,40,"Wi-Fi");
    TextAt(dl,g_fMed,19*S,V(L+6*S+slideX(0),ny),MulA(COL_INK2,e0),hd);
    if(g_mixerInSidebar && V2Btn(dl,io,V(R-18*S,ny+11*S),32*S,32*S,g_sideMixerOn? "notifications" : "tune",g_sideMixerOn,0x68600,click,10*S)) g_sideMixerOn=!g_sideMixerOn;
    if(!g_mixerInSidebar) g_sideMixerOn=false;
    if(V2Btn(dl,io,V(R-(g_mixerInSidebar? 58 : 18)*S,ny+11*S),32*S,32*S,g_sideWifiOn? "notifications" : "wifi",g_sideWifiOn,0x68610,click,10*S)){ g_sideWifiOn=!g_sideWifiOn; if(g_sideWifiOn){ g_sideMixerOn=false; WifiScanAsync(); } }
    if(g_sideMixerOn) g_sideWifiOn=false;
    ny+=34*S;
    if(g_sideMixerOn){ DrawAudioMixer(dl,io,V(L,ny),V(R-L,ny1-ny),900,true); dl->PopClipRect(); return; }
    if(g_sideWifiOn){
        if(!g_wifiScanning && GetTickCount64()-g_wifiLastScan>15000) WifiScanAsync();
        float used=DrawWifiList(dl,io,L,ny,R,S,9,60*S,click,(int)(255*e0),0x69000);
        const char* sl=g_wifiScanning? "Scanning\xE2\x80\xA6" : "Scan for networks";
        float sw=TextW(g_fSml,15*S,sl)+36*S; ImVec2 sa=V(L,std::min(ny1-44*S,ny+used+8*S)), sb=V(L+sw,sa.y+36*S);
        bool sh=io.MousePos.x>=sa.x&&io.MousePos.x<sb.x&&io.MousePos.y>=sa.y&&io.MousePos.y<sb.y;
        dl->AddRectFilled(sa,sb,MulA(Mix(chipC,COL_INK2,sh? 0.3f : 0.0f),e0),18*S);
        MsIcon(dl,"refresh",V(sa.x+18*S,sa.y+18*S),18*S,MulA(COL_INK,e0));
        TextAt(dl,g_fSml,15*S,V(sa.x+30*S,sa.y+8*S),MulA(COL_INK,e0),sl);
        if(sh&&click) WifiScanAsync();
        dl->PopClipRect(); return; }
    const float clearS=52*S;
    float listBot=ny1-clearS-20*S;
    if(groups.empty()){
        float cy=ny+(listBot-ny)*0.55f;
        V2EmptyScene(dl,V((L+R)*0.5f+slideX(0),cy),4.0f*S,MulA(Mix(panelC,COL_INK2,0.55f),e0));
        const char* m="All up to date!"; float f=30*S;
        TextAt(dl,g_fMed,f,V((L+R)*0.5f-TextW(g_fMed,f,m)*0.5f+slideX(0),cy+34*S),MulA(Mix(panelC,COL_INK2,0.60f),e0),m);
    } else {
        static std::unordered_map<std::string,bool> expanded;
        static std::unordered_map<uint32_t,ULONGLONG> seen;
        static float scroll=0;
        bool inList=io.MousePos.x>=L&&io.MousePos.x<R&&io.MousePos.y>=ny&&io.MousePos.y<listBot;
        if(inList && io.MouseWheel!=0) scroll=std::max(0.0f,scroll-io.MouseWheel*70.0f*S);
        dl->PushClipRect(V(L-2,ny-2),V(R+2,ny1),true);
        float y=ny-scroll;
        const float CH=60*S, LH=24*S;
        int gi=0;
        for(auto& g:groups){
            ULONGLONG now=GetTickCount64();
            if(!seen.count(g.items[0].id)) seen[g.items[0].id]=now;
            float ap=std::clamp((now-seen[g.items[0].id])/350.0f,0.0f,1.0f); ap=Cael::eval(Cael::DEFAULT_SPATIAL,ap);
            float ge=std::min(stage(0),ap);
            bool ex=expanded[g.key] && g.items.size()>1;
            int shown = ex? (int)g.items.size() : 1;
            float ch=CH+(shown-1)*LH;
            float gx=(1.0f-ge)*40*S;
            ImVec2 a=V(L+gx,y), b=V(R+gx,y+ch);
            bool hov=io.MousePos.x>=a.x&&io.MousePos.x<b.x&&io.MousePos.y>=a.y&&io.MousePos.y<b.y;
            float ha=HoverAnim(0x68400+gi,hov);
            dl->AddRectFilled(a,b,MulA(Mix(cardC,Mix(cardC,COL_INK2,0.18f),ha),ge),12*S);
            float icR=17*S; ImVec2 ic=V(a.x+13*S+icR,a.y+CH*0.5f);
            dl->AddCircleFilled(ic,icR,MulA(Mix(panelC,COL_CARD2,0.95f),ge),28);
            ID3D11ShaderResourceView* tex=nullptr;
            { std::lock_guard<std::mutex> lk(g_nIconMtx); auto it=g_nIcons.find(g.aumid.empty()? g.app : g.aumid); if(it!=g_nIcons.end()) tex=it->second;
              if(!tex){ it=g_nIcons.find(g.app); if(it!=g_nIcons.end()) tex=it->second; } }
            if(tex) dl->AddImageRounded((ImTextureID)tex,V(ic.x-11*S,ic.y-11*S),V(ic.x+11*S,ic.y+11*S),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(255*ge)),4*S);
            else MsIcon(dl,"notifications",ic,20*S,MulA(COL_INK,ge));
            float tx=ic.x+icR+12*S;
            std::string ago=V2Ago(g.items[0].at);
            float rx=b.x-12*S;
            // count + chevron, or a dismiss button while hovered
            if(g.items.size()>1){
                char cnt[16]; snprintf(cnt,16,"%zu",g.items.size());
                ImVec2 cc=V(rx-9*S,a.y+19*S);
                MsIcon(dl,ex? "expand_less" : "expand_more",cc,20*S,MulA(COL_INK2,ge));
                float cw2=TextW(g_fSml,16*S,cnt);
                TextAt(dl,g_fSml,16*S,V(cc.x-12*S-cw2,a.y+10*S),MulA(COL_INK2,ge),cnt);
                bool chh=io.MousePos.x>=cc.x-20*S-cw2&&io.MousePos.x<cc.x+12*S&&io.MousePos.y>=a.y&&io.MousePos.y<a.y+34*S;
                if(chh&&click) expanded[g.key]=!ex;
                rx=cc.x-22*S-cw2;
            }
            if(hov){
                ImVec2 xc=V(rx-10*S,a.y+19*S); bool xh=fabsf(io.MousePos.x-xc.x)<12*S&&fabsf(io.MousePos.y-xc.y)<12*S;
                if(xh) dl->AddCircleFilled(xc,12*S,MulA(Mix(cardC,COL_INK2,0.35f),ge),20);
                MsIcon(dl,"close",xc,17*S,MulA(COL_INK2,ge));
                if(xh&&click){ for(auto& n:g.items){ NotifDismiss(n.id); gone[n.id]=now; } }
                rx=xc.x-16*S;
            }
            float agw=TextW(g_fSml,16*S,ago.c_str());
            TextAt(dl,g_fSml,16*S,V(rx-agw,a.y+10*S),MulA(COL_INK2,ge),ago.c_str());
            TextAt(dl,g_fSml,17*S,V(tx,a.y+9*S),MulA(Mix(COL_INK,COL_INK2,0.15f),ge),Clip(g_fSml,17*S,g.app.empty()? "Notification" : g.app,rx-agw-10*S-tx).c_str());
            auto flat=[](std::string t){ for(auto& ch2:t) if(ch2==10||ch2==13||ch2==9) ch2=32; return t; };
            for(int k=0;k<shown;k++){
                std::string ti=flat(g.items[k].title), bo=flat(g.items[k].body);
                float ly=a.y+32*S+k*LH;
                std::string tt=Clip(g_fSml,16*S,ti,(b.x-12*S-tx)*0.62f);
                TextAt(dl,g_fSml,16*S,V(tx,ly),MulA(COL_INK,ge),tt.c_str());
                float bx=tx+TextW(g_fSml,16*S,tt.c_str())+7*S;
                if(bx<b.x-14*S) TextAt(dl,g_fSml,16*S,V(bx,ly),MulA(COL_INK2,ge),Clip(g_fSml,16*S,bo,b.x-12*S-bx).c_str());
            }
            if(hov && io.MouseClicked[2]){ for(auto& n:g.items){ NotifDismiss(n.id); gone[n.id]=now; } }
            y+=ch+8*S; gi++;
        }
        float maxScroll=std::max(0.0f,(y+scroll)-listBot); scroll=std::min(scroll,maxScroll);
        dl->PopClipRect();
    }
    // clear all
    { float e=stage(2);
      ImVec2 a=V(R-6*S-clearS,ny1-20*S-clearS), b=V(R-6*S,ny1-20*S);
      bool hov=io.MousePos.x>=a.x&&io.MousePos.x<b.x&&io.MousePos.y>=a.y&&io.MousePos.y<b.y;
      float ha=HoverAnim(0x68500,hov);
      bool any=!snap.empty();
      float sc=1.0f+0.05f*ha; ImVec2 c=V((a.x+b.x)*0.5f,(a.y+b.y)*0.5f);
      ImVec2 a2=V(c.x-clearS*0.5f*sc,c.y-clearS*0.5f*sc), b2=V(c.x+clearS*0.5f*sc,c.y+clearS*0.5f*sc);
      dl->AddRectFilled(a2,b2,MulA(any? lightC : chipC,e),12*S);
      MsIcon(dl,"clear_all",c,28*S,MulA(any? onLight : COL_INK2,e));
      if(hov&&click&&any){ for(auto& n:snap){ NotifDismiss(n.id); gone[n.id]=GetTickCount64(); } } }
    dl->PopClipRect();
}

// ============================================================================================ toasts
// New notifications while the merged sidebar is closed (reference\v2\NOTES.md §2): a card under the bar - round
// app icon, "Title  •  now", the message, a chevron that opens the full text. Click the card to open the sidebar,
// hover to keep it, the X (on hover) dismisses. notifications.toast_style = "caelestia".
static void DrawToastsV2(float rvRaw){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x;
    const float S=1.0f/std::max(0.5f,g_uiScale);
    bool click=io.MouseClicked[0];
    std::vector<Notif> snap; { std::lock_guard<std::mutex> lk(g_notifMtx); snap=g_notifs; }
    std::sort(snap.begin(),snap.end(),[](const Notif& a,const Notif& b){ return a.at>b.at; });
    static std::unordered_map<uint32_t,ULONGLONG> gone;
    { ULONGLONG now=GetTickCount64(); for(auto it=gone.begin(); it!=gone.end();) it=(now-it->second>6000)? gone.erase(it) : std::next(it);
      snap.erase(std::remove_if(snap.begin(),snap.end(),[&](const Notif& n){ return gone.count(n.id)>0; }),snap.end()); }
    if(snap.size()>3) snap.resize(3);
    const bool born=FrameBornOn() && !g_mons.empty();
    float bT=born? FrameInset(EDGE_TOP) : 10.0f, bR=born? FrameInset(EDGE_RIGHT) : 10.0f;
    float tw=std::min(420*S,W-bR-20);
    float x1=W-bR-10*S, x0=x1-tw;
    float y=bT+10*S;
    float e=Cael::eval(Cael::DEFAULT_SPATIAL,std::clamp(rvRaw,0.0f,1.0f));
    RECT hit{0,0,0,0};
    static std::unordered_map<uint32_t,bool> open;
    static std::unordered_map<uint32_t,ULONGLONG> seen;
    ImU32 cardC=Mix(PanelCol(255),COL_CARD2,0.42f);
    int k=0;
    for(auto& n:snap){
        ULONGLONG now=GetTickCount64(); if(!seen.count(n.id)) seen[n.id]=now;
        float ap=Cael::eval(Cael::DEFAULT_SPATIAL,std::clamp((now-seen[n.id])/420.0f,0.0f,1.0f));
        float a=std::min(e,ap);
        bool ex=open[n.id];
        auto flat=[](std::string t){ for(auto& c:t) if(c==10||c==13||c==9) c=32; return t; };
        std::string title=flat(n.title.empty()? n.app : n.title), body=flat(n.body);
        std::vector<std::string> lines;
        if(ex) WrapLines(g_fSml,17*S,body,tw-94*S,6,lines); else lines.push_back(Clip(g_fSml,17*S,body,tw-94*S));
        float h=(54+ (lines.size()-1)*22)*S+18*S;
        float ox=(1.0f-a)*(tw*0.35f);
        ImVec2 A=V(x0+ox,y), B=V(x1+ox,y+h);
        bool hov=io.MousePos.x>=A.x&&io.MousePos.x<B.x&&io.MousePos.y>=A.y&&io.MousePos.y<B.y;
        float ha=HoverAnim(0x69000+k,hov);
        dl->AddRectFilled(V(A.x+2*S,A.y+5*S),V(B.x+2*S,B.y+7*S),IM_COL32(0,0,0,(int)(60*a)),16*S);
        dl->AddRectFilled(A,B,MulA(Mix(cardC,Mix(cardC,COL_INK2,0.2f),ha),a),16*S);
        dl->AddRect(A,B,MulA(WithA(COL_INK2,40),a),16*S,0,1.0f);
        float icR=22*S; ImVec2 ic=V(A.x+14*S+icR,A.y+36*S);
        dl->AddCircleFilled(ic,icR,MulA(Mix(PanelCol(255),COL_CARD2,0.95f),a),32);
        ID3D11ShaderResourceView* tex=nullptr;
        { std::lock_guard<std::mutex> lk(g_nIconMtx); auto it=g_nIcons.find(n.aumid.empty()? n.app : n.aumid); if(it!=g_nIcons.end()) tex=it->second;
          if(!tex){ it=g_nIcons.find(n.app); if(it!=g_nIcons.end()) tex=it->second; } }
        if(tex) dl->AddImageRounded((ImTextureID)tex,V(ic.x-14*S,ic.y-14*S),V(ic.x+14*S,ic.y+14*S),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(255*a)),5*S);
        else MsIcon(dl,"notifications",ic,24*S,MulA(COL_INK,a));
        float tx=ic.x+icR+14*S;
        // chevron / close
        ImVec2 cc=V(B.x-24*S,A.y+26*S);
        bool ch=fabsf(io.MousePos.x-cc.x)<14*S&&fabsf(io.MousePos.y-cc.y)<14*S;
        if(ch) dl->AddCircleFilled(cc,14*S,MulA(WithA(COL_INK2,50),a),20);
        MsIcon(dl,ex? "expand_less" : "expand_more",cc,22*S,MulA(COL_INK,a));
        if(ch&&click) open[n.id]=!ex;
        float rx=cc.x-18*S;
        if(hov){ ImVec2 xc=V(rx-10*S,cc.y); bool xh=fabsf(io.MousePos.x-xc.x)<12*S&&fabsf(io.MousePos.y-xc.y)<12*S;
            if(xh) dl->AddCircleFilled(xc,12*S,MulA(WithA(COL_INK2,50),a),20);
            MsIcon(dl,"close",xc,18*S,MulA(COL_INK2,a));
            if(xh&&click){ NotifDismiss(n.id); gone[n.id]=now; }
            rx=xc.x-16*S; }
        std::string ago=" \xE2\x80\xA2 "+V2Ago(n.at);
        float agW=TextW(g_fSml,17*S,ago.c_str());
        std::string tt=Clip(g_fMed,18*S,title,rx-tx-agW);
        TextAt(dl,g_fMed,18*S,V(tx,A.y+14*S),MulA(COL_INK,a),tt.c_str());
        TextAt(dl,g_fSml,17*S,V(tx+TextW(g_fMed,18*S,tt.c_str()),A.y+15*S),MulA(COL_INK2,a),ago.c_str());
        for(size_t q=0;q<lines.size();q++) TextAt(dl,g_fSml,17*S,V(tx,A.y+40*S+q*22*S),MulA(COL_INK2,a),lines[q].c_str());
        bool onCtl=ch || (hov && io.MousePos.x>rx-30*S && io.MousePos.y<A.y+44*S);
        if(hov&&click&&!onCtl) g_sideForceUntil=GetTickCount64()+4000;     // open the sidebar
        if(hov&&io.MouseClicked[2]){ NotifDismiss(n.id); gone[n.id]=now; }
        RECT r{(LONG)(A.x-4),(LONG)(A.y-4),(LONG)(B.x+8),(LONG)(B.y+10)};
        if(hit.right<=hit.left) hit=r; else UnionRect(&hit,&hit,&r);
        y+=h+10*S; k++;
    }
    g_notifRect=hit; g_notifStackBot=y; g_nfPanelOn=false;
}
