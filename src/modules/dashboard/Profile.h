// src/modules/dashboard/Profile.h  —  Aether shell
// The profile card from Caelestia's dashboard (s11.mp4 ~0:01): a round avatar, then "icon : value" rows for the
// distro, the window manager and the uptime, each icon in a different accent role. On top of that, the part a
// profile card is actually for: your name, a presence dot and a custom status you type in yourself.
//
//   * click the status line (or the avatar) -> the status editor pops out of the card
//   * click the presence dot                 -> online -> idle -> do not disturb -> invisible
//   * everything is a setting (config\profile.toml, Settings -> Dashboard -> Profile) and IPC:
//       Aether.exe -s status=<text> | status_clear | status_icon=<material symbol> | presence=online|idle|dnd|invisible
//   * the info rows are templates: "icon|text|colour; ...", any custom-widget binding works ({cpu%}, {media.title}...)
//   * custom widgets get the profile back as bindings: {name} {status} {status_icon} {presence} {os} {wm} {uptime_long}
#pragma once

static const char* PROFILE_ICONS[] = {
    "mood","sentiment_satisfied","favorite","sports_esports","music_note","headphones","code","terminal",
    "work","school","bedtime","coffee","restaurant","fitness_center","flight","local_fire_department",
    "star","pets","movie","brush","chat","celebration","self_improvement","do_not_disturb_on" };
static const int PROFILE_NICONS = (int)(sizeof(PROFILE_ICONS)/sizeof(PROFILE_ICONS[0]));
static const int   PROFILE_CLEAR_MIN[]   = { 0, 30, 60, 240, -1 };
static const char* PROFILE_CLEAR_LABEL[] = { "Never", "30 minutes", "1 hour", "4 hours", "End of day" };
static int ProfileClearIndex(int m){ for(int i=0;i<5;i++) if(PROFILE_CLEAR_MIN[i]==m) return i; return 0; }

// ------------------------------------------------------------------------------------------------- state
static ImU32 ProfilePresenceCol(int p){
    switch(p){
        case PRES_ONLINE:    return IM_COL32( 67,181,129,255);
        case PRES_IDLE:      return IM_COL32(240,178, 50,255);
        case PRES_DND:       return IM_COL32(237, 66, 69,255);
        case PRES_INVISIBLE: return IM_COL32(128,132,142,255);
    }
    return 0;
}
static const char* ProfilePresenceLabel(int p){
    static const char* L[]={"Online","Idle","Do not disturb","Invisible",""};
    return L[std::clamp(p,0,4)];
}
// Presence and the shell's do-not-disturb are one switch when profile.presence_sets_dnd is on, so the dot never
// disagrees with the bell in the bar - whichever of the two was flipped last wins.
static int ProfilePresenceNow(){
    int p=std::clamp(g_presence,0,(int)PRES_NONE);
    if(g_presenceDndLink){
        if(p==PRES_DND && !g_dnd) return PRES_ONLINE;
        if((p==PRES_ONLINE||p==PRES_IDLE) && g_dnd) return PRES_DND;
    }
    return p;
}
static void ProfileSetPresence(int p){
    g_presence=std::clamp(p,0,(int)PRES_NONE);
    if(g_presenceDndLink && g_presence!=PRES_NONE) g_dnd=(g_presence==PRES_DND);
    SaveConfig();
}
static void ProfileSetStatus(const std::string& text,const std::string& icon,int clearMin){
    g_status=text; if(!icon.empty()) g_statusIcon=icon;
    g_statusClearMin=clearMin;
    g_statusSetAt= text.empty()? 0 : (int)time(nullptr);
    SaveConfig();
}
// a status set to clear after a while clears itself (checked at most once a second)
static void ProfileExpire(){
    static ULONGLONG last=0; ULONGLONG t=GetTickCount64();
    if(t-last<1000) return; last=t;
    if(g_status.empty() || g_statusClearMin==0 || g_statusSetAt<=0) return;
    time_t now=time(nullptr), set=(time_t)g_statusSetAt; bool gone=false;
    if(g_statusClearMin>0) gone = (now-set) >= (time_t)g_statusClearMin*60;
    else { struct tm a,b; localtime_s(&a,&set); localtime_s(&b,&now); gone = a.tm_yday!=b.tm_yday || a.tm_year!=b.tm_year; }
    if(gone){ g_status.clear(); g_statusSetAt=0; SaveConfig(); }
}
static std::string ProfileDisplayName(){
    if(!g_profileName.empty()) return g_profileName;
    static std::string user; if(user.empty()){ wchar_t u[UNLEN+1]; DWORD n=UNLEN+1; if(GetUserNameW(u,&n)) user=W2U8(u); }
    return user;
}
// "Windows 11 Pro" - ProductName still says "Windows 10" on 11, so the build number decides
static std::string ProfileOsName(){
    static std::string os; if(!os.empty()) return os;
    wchar_t prod[128]={0}, build[32]={0}; DWORD n=sizeof(prod);
    RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"ProductName",RRF_RT_REG_SZ,nullptr,prod,&n);
    n=sizeof(build);
    RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"CurrentBuildNumber",RRF_RT_REG_SZ,nullptr,build,&n);
    os = prod[0]? W2U8(prod) : "Windows";
    if(_wtoi(build)>=22000){ size_t p=os.find("Windows 10"); if(p!=std::string::npos) os.replace(p,10,"Windows 11"); }
    return os;
}
// Caelestia's wording: "up 1 hour, 23 minutes"
static std::string ProfileUptimeLong(){
    ULONGLONG m=GetTickCount64()/60000ULL, d=m/1440, h=(m/60)%24, mi=m%60;
    auto pl=[](ULONGLONG k,const char* u){ return std::to_string(k)+" "+u+(k==1?"":"s"); };
    if(d) return "up "+pl(d,"day")+", "+pl(h,"hour");
    if(m>=60) return "up "+pl(m/60,"hour")+", "+pl(mi,"minute");
    return "up "+pl(mi,"minute");
}

struct ProfileRow { std::string icon, text, color; };
static const std::vector<ProfileRow>& ProfileRows(){
    static std::string key="\x01"; static std::vector<ProfileRow> rows;
    if(key==g_profileRows) return rows;
    key=g_profileRows; rows.clear();
    size_t i=0; const std::string& s=g_profileRows;
    while(i<=s.size()){
        size_t e=s.find_first_of(";\n",i); std::string part=Tml::Trim(s.substr(i,e==std::string::npos? std::string::npos : e-i));
        if(!part.empty()){
            ProfileRow r; size_t a=part.find('|');
            if(a==std::string::npos) r.text=part;
            else { r.icon=Tml::Trim(part.substr(0,a)); std::string rest=part.substr(a+1); size_t b=rest.rfind('|');
                   if(b==std::string::npos) r.text=Tml::Trim(rest); else { r.text=Tml::Trim(rest.substr(0,b)); r.color=Tml::Trim(rest.substr(b+1)); } }
            rows.push_back(r);
            if(rows.size()>=8) break;
        }
        if(e==std::string::npos) break; i=e+1;
    }
    return rows;
}

// ------------------------------------------------------------------------------------------------- editor
struct ProfileEditor {
    bool  open=false, fresh=false;
    int   owner=0;                  // uid of the card it pops out of (0 = the first card drawn claims it)
    ImVec2 a{0,0}, b{0,0};          // that card, this frame
    ImVec2 pa{0,0}, pb{0,0};        // the popover, last frame (clicks inside it are kept from the widgets below)
    char  text[128]={0};
    std::string icon="mood";
    int   presence=0, clearMin=0;
    float anim=0;
    bool  shielded=false; bool keepDown=false, keepClicked0=false, keepClicked1=false, keepReleased=false; float keepWheel=0;
};
static ProfileEditor g_pe;
static bool g_wInteractive=false;   // widgets are being drawn where they can be clicked (the drawer, not in edit mode)
static bool DrawerTyping(){ return g_calNoteDay!=0 || g_pe.open || g_termFocus; }

static void ProfileEditorOpen(int owner){
    g_pe.open=true; g_pe.fresh=true; g_pe.owner=owner; g_pe.anim=0;
    snprintf(g_pe.text,sizeof(g_pe.text),"%s",g_status.c_str());
    g_pe.icon=g_statusIcon.empty()? "mood" : g_statusIcon;
    g_pe.presence=std::clamp(g_presence,0,(int)PRES_NONE);
    g_pe.clearMin=g_statusClearMin;
    g_drawerForceUntil=GetTickCount64()+1500;
}

// ------------------------------------------------------------------------------------------------- icons
// A row icon is a Material Symbols glyph (what Caelestia uses) or a REAL picture: the Windows logo, a distro mark
// from assets\logos, an app's own icon, or any image file.
static ID3D11ShaderResourceView* GetAppIconHi(HWND h,const std::wstring& exe);   // fwd
static ID3D11ShaderResourceView* IconTex(HICON,int,bool);   // fwd
static bool ProfileIcon(ImDrawList* dl,const std::string& icon,ImVec2 c,float px,ImU32 col){
    if(icon.empty() || px<2.0f) return false;
    std::string l=icon; for(auto& ch:l) ch=(char)tolower((unsigned char)ch);
    auto drawTex=[&](ID3D11ShaderResourceView* t,int w,int h,float box)->bool{
        if(!t) return false;
        float sc=box/(float)std::max(1,std::max(w,h)); float dw=w*sc, dh=h*sc;
        dl->AddImage((ImTextureID)t,V(c.x-dw*0.5f,c.y-dh*0.5f),V(c.x+dw*0.5f,c.y+dh*0.5f),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(col>>IM_COL32_A_SHIFT)&0xFF));
        return true; };
    std::string path;
    if(l=="os"||l=="windows") path=ExeDir()+"assets\\logos\\windows-11.png";
    else if(l.rfind("logo:",0)==0){ std::string k=l.substr(5);
        for(int i=0;i<NBARLOGOS;i++) if(k==BAR_LOGOS[i].key && BAR_LOGOS[i].file){ path=ExeDir()+"assets\\logos\\"+BAR_LOGOS[i].file; break; }
        if(path.empty()) path=ExeDir()+"assets\\logos\\"+icon.substr(5)+(k.find('.')==std::string::npos? ".png" : ""); }
    else if(l.rfind("exe:",0)==0){
        static std::unordered_map<std::string,ID3D11ShaderResourceView*> cache;
        auto it=cache.find(l);
        if(it==cache.end()){
            std::wstring name=U82W(icon.substr(4)); std::wstring full=name;
            if(name.find(L'\\')==std::wstring::npos){
                wchar_t buf[MAX_PATH]={0};
                if(SearchPathW(nullptr,name.c_str(),name.find(L'.')==std::wstring::npos? L".exe" : nullptr,MAX_PATH,buf,nullptr)) full=buf;
                else { // App Paths (discord, spotify... register there) then the running processes
                    std::wstring key=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\"+name+(name.find(L'.')==std::wstring::npos? L".exe" : L"");
                    DWORD n=sizeof(buf);
                    if(RegGetValueW(HKEY_CURRENT_USER,key.c_str(),nullptr,RRF_RT_REG_SZ,nullptr,buf,&n)==ERROR_SUCCESS ||
                       (n=sizeof(buf), RegGetValueW(HKEY_LOCAL_MACHINE,key.c_str(),nullptr,RRF_RT_REG_SZ,nullptr,buf,&n)==ERROR_SUCCESS)) full=buf;
                    else {
                        std::wstring want=name; if(want.find(L'.')==std::wstring::npos) want+=L".exe";
                        HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0); PROCESSENTRY32W pe{}; pe.dwSize=sizeof(pe);
                        if(snap!=INVALID_HANDLE_VALUE){
                            for(BOOL ok=Process32FirstW(snap,&pe); ok; ok=Process32NextW(snap,&pe)) if(_wcsicmp(pe.szExeFile,want.c_str())==0){
                                HANDLE hp=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pe.th32ProcessID);
                                if(hp){ DWORD m=MAX_PATH; if(QueryFullProcessImageNameW(hp,0,buf,&m)) full=buf; CloseHandle(hp); }
                                break; }
                            CloseHandle(snap); }
                    }
                }
            }
            // the icon resource closest to the drawn size: a 256px jumbo icon squeezed to 16px aliases badly
            ID3D11ShaderResourceView* t=nullptr;
            if(GetFileAttributesW(full.c_str())!=INVALID_FILE_ATTRIBUTES){
                int phys=(int)std::lround(px*1.05f*g_uiScale);
                if(phys<=40){ SHFILEINFOW fi{};
                    if(SHGetFileInfoW(full.c_str(),0,&fi,sizeof(fi),SHGFI_ICON|(phys<=20? SHGFI_SMALLICON : SHGFI_LARGEICON)) && fi.hIcon){
                        t=IconTex(fi.hIcon,phys<=20? 16 : 32,false); DestroyIcon(fi.hIcon); } }
                if(!t) t=GetAppIconHi(nullptr,full);
            }
            it=cache.emplace(l,t).first;
        }
        return drawTex(it->second,1,1,px*1.05f);
    }
    else { size_t d=l.rfind('.'); std::string ext= d==std::string::npos? "" : l.substr(d);
        if(ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".gif"||ext==".ico"||ext==".bmp"||ext==".webp")
            path = (icon.find(':')!=std::string::npos || icon.rfind("\\\\",0)==0)? icon : CfgDir()+icon; }
    if(!path.empty()){
        size_t d=l.rfind('.');
        if(d!=std::string::npos && l.substr(d)==".gif"){                     // animated: the shared GIF player
            ImgAnim* ia=GetImg(path);
            if(ia && !ia->frames.empty()) return drawTex(ia->frames[std::min(ia->frame,(int)ia->frames.size()-1)],ia->w,ia->h,px*0.9f);
            return false; }
        // A 482px logo sampled straight down to 13px aliases into stripes (the Windows logo came out as two bars),
        // so stills are decoded at the size they are drawn, like MsIcon does for glyphs.
        int phys=std::clamp((int)std::lround(px*0.9f*g_uiScale),8,512);
        static std::unordered_map<std::string,std::tuple<ID3D11ShaderResourceView*,int,int>> sized;
        std::string key=path+"|"+std::to_string(phys);
        auto it=sized.find(key);
        if(it==sized.end()){
            std::vector<uint8_t> px4; int w=0,h=0; ID3D11ShaderResourceView* t=nullptr;
            if(DecodeFirstFrame(U82W(path),phys,px4,w,h) && w>0 && h>0) t=MakeTextureBGRA(px4.data(),w,h);
            it=sized.emplace(key,std::make_tuple(t,w,h)).first;
        }
        return drawTex(std::get<0>(it->second),std::get<1>(it->second),std::get<2>(it->second),px*0.9f);
    }
    return MsIcon(dl,icon,c,px,col);
}

// ------------------------------------------------------------------------------------------------- avatar
static void ProfileAvatar(ImDrawList* dl,ImVec2 c,float R,float ringA){
    int shape=M3ShapeFromName(g_avatarShape); if(shape<0) shape=M3_CIRCLE;
    ID3D11ShaderResourceView* tex=nullptr; int tw=1,th=1;
    if(!g_avatarImage.empty()){ ImgAnim* ia=GetImg(g_avatarImage);
        if(ia && !ia->frames.empty()){ tex=ia->frames[std::min(ia->frame,(int)ia->frames.size()-1)]; tw=ia->w; th=ia->h; } }
    if(!tex){ LoadAvatar(); tex=g_avatar; }
    if(tex){
        if(shape==M3_CIRCLE){ ImVec2 uv0(0,0),uv1(1,1); CoverUV(tw,th,R*2,R*2,uv0,uv1);
            dl->AddImageRounded((ImTextureID)tex,V(c.x-R,c.y-R),V(c.x+R,c.y+R),uv0,uv1,IM_COL32(255,255,255,255),R); }
        else M3ShapeImage(dl,c,R,tex,tw,th,shape,0.0f);
    } else {
        if(shape==M3_CIRCLE) dl->AddCircleFilled(c,R,COL_CARD2,48); else M3Shape(dl,c,R,COL_CARD2,shape,0.0f);
        dl->AddCircleFilled(V(c.x,c.y-R*0.29f),R*0.35f,COL_INK2);
        dl->PathArcTo(V(c.x,c.y+R*0.62f),R*0.48f,3.1416f,6.2832f,20); dl->PathStroke(COL_INK2,0,R*0.38f);
    }
    if(ringA>0.01f){
        ImU32 rc=WithA(COL_GOLD,(int)(255*std::clamp(ringA,0.0f,1.0f)));
        if(shape==M3_CIRCLE) dl->AddCircle(c,R+3.0f,rc,64,2.2f);
        else { const float* tab=M3Table(shape); ImVec2 p[M3_SAMPLES];
            for(int i=0;i<M3_SAMPLES;i++){ float a=(float)i/M3_SAMPLES*6.2831853f; float r=R*tab[i]+3.0f; p[i]=V(c.x+cosf(a)*r,c.y+sinf(a)*r); }
            dl->AddPolyline(p,M3_SAMPLES,rc,ImDrawFlags_Closed,2.2f); }
    }
}
// the presence dot, cut out of the card like Discord's
static void ProfilePresenceDot(ImDrawList* dl,ImVec2 p,float r,int pres,ImU32 hole){
    if(pres<0 || pres>=PRES_NONE) return;
    ImU32 col=ProfilePresenceCol(pres);
    dl->AddCircleFilled(p,r+std::max(2.0f,r*0.38f),hole,32);
    dl->AddCircleFilled(p,r,col,32);
    if(pres==PRES_IDLE)      dl->AddCircleFilled(V(p.x-r*0.42f,p.y-r*0.42f),r*0.62f,hole,24);   // a crescent
    else if(pres==PRES_DND)  dl->AddRectFilled(V(p.x-r*0.56f,p.y-r*0.17f),V(p.x+r*0.56f,p.y+r*0.17f),hole,r*0.17f);
    else if(pres==PRES_INVISIBLE) dl->AddCircleFilled(p,r*0.48f,hole,24);
}

// ------------------------------------------------------------------------------------------------- the card
static void WUser(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,int uid,bool drawCard){
    ProfileExpire();
    if(drawCard) Card(dl,o,s);
    const bool live=g_wInteractive;
    if(g_pe.open && (g_pe.owner==uid || g_pe.owner==0)){ g_pe.owner=uid; g_pe.a=o; g_pe.b=V(o.x+s.x,o.y+s.y); }
    ImU32 hole = g_cardMode==2? g_cardCol : (g_cardMode==1? PanelCol(255) : COL_CARD);
    int pres=ProfilePresenceNow();
    const std::vector<ProfileRow>& rows=ProfileRows();
    CwCtx cx; cx.o=o; cx.s=s; cx.t=(float)((double)GetTickCount64()/1000.0);
    cx.hover = io.MousePos.x>=o.x&&io.MousePos.x<o.x+s.x&&io.MousePos.y>=o.y&&io.MousePos.y<o.y+s.y;
    const bool hasStatus=!g_status.empty();
    auto inR=[&](ImVec2 a,ImVec2 b){ return io.MousePos.x>=a.x&&io.MousePos.x<b.x&&io.MousePos.y>=a.y&&io.MousePos.y<b.y; };
    const bool click = live && io.MouseClicked[0];
    const ImU32 rowCol[3]={ COL_GOLD, M3Secondary(), M3Tertiary() };

    float pad=std::clamp(std::min(s.x,s.y)*0.09f,10.0f,18.0f);
    bool tall = s.y > s.x*1.08f;
    bool showName=g_profileShowName, showStatus=hasStatus||live;
    int  nRows=(int)rows.size();

    float R, acx, acy, fx, fw;
    if(!tall){
        R  =std::clamp(std::min(s.y*0.36f,s.x*0.16f),14.0f,46.0f);
        acx=o.x+pad+R; acy=o.y+s.y*0.5f;
        fx =acx+R+pad*1.15f; fw=o.x+s.x-pad-fx;
    } else {
        R  =std::clamp(std::min(s.x*0.26f,s.y*0.20f),16.0f,58.0f);
        acx=o.x+s.x*0.5f; acy=o.y+pad+R+2.0f;
        fx =o.x+pad; fw=s.x-pad*2;
    }
    // fit the text: shrink, then drop the name, then an empty status prompt, then rows from the bottom
    float availH = tall? (o.y+s.y-pad) - (acy+R+pad*0.8f) : s.y-pad*1.6f;
    auto need=[&](float f){ return (showName? f*1.15f*1.45f : 0.0f) + (showStatus? f*1.6f : 0.0f) + nRows*f*1.62f; };
    float fs=16.0f;
    while(fs>11.0f && need(fs)>availH) fs-=0.5f;
    if(need(fs)>availH && showName){ showName=false; fs=16.0f; while(fs>11.0f && need(fs)>availH) fs-=0.5f; }
    if(need(fs)>availH && showStatus && !hasStatus){ showStatus=false; }
    while(need(fs)>availH && nRows>0) nRows--;
    if(fw<48.0f){ showName=false; showStatus=false; nRows=0; }       // avatar only

    // ---- avatar ----
    ImVec2 ac=V(acx,acy);
    bool avHov = live && inR(V(acx-R,acy-R),V(acx+R,acy+R));
    float avH = HoverAnim(0x5100000+uid,avHov);
    float pulse=0.5f+0.5f*sinf((float)ImGui::GetTime()*2.2f);
    float Rd=R*(1.0f+0.035f*avH);
    ProfileAvatar(dl,ac,Rd, g_avatarRing? (0.45f+pulse*0.35f) : avH*0.6f);
    float dotR=std::max(4.5f,R*0.19f);
    ImVec2 dp=V(acx+Rd*0.7071f,acy+Rd*0.7071f);
    if(pres!=PRES_NONE){
        bool dh = live && fabsf(io.MousePos.x-dp.x)<dotR+5 && fabsf(io.MousePos.y-dp.y)<dotR+5;
        float dH=HoverAnim(0x5200000+uid,dh);
        ProfilePresenceDot(dl,dp,dotR*(1.0f+0.18f*dH),pres,hole);
        if(dh && click){ int np = pres==PRES_INVISIBLE? PRES_ONLINE : pres+1; ProfileSetPresence(np); }
        else if(avHov && click) ProfileEditorOpen(uid);
    } else if(avHov && click) ProfileEditorOpen(uid);

    // ---- text block ----
    float H=need(fs); float y = tall? acy+R+pad*0.8f + std::max(0.0f,(availH-H)*0.5f) : o.y+(s.y-H)*0.5f;
    auto lineX=[&](float w){ return tall? o.x+(s.x-w)*0.5f : fx; };
    if(showName){
        float nf=fs*1.15f; std::string nm=Clip(g_fMed,nf,ProfileDisplayName(),fw);
        const char* pl = pres!=PRES_NONE? ProfilePresenceLabel(pres) : "";
        std::string tail = *pl? std::string("  \xC2\xB7  ")+pl : std::string();
        float nw=TextW(g_fMed,nf,nm.c_str()), tw=TextW(g_fSml,fs*0.85f,tail.c_str());
        if(nw+tw>fw) tail.clear(), tw=0;
        float x=lineX(nw+tw);
        TextAt(dl,g_fMed,nf,V(x,y),COL_INK,nm.c_str());
        if(!tail.empty()) TextAt(dl,g_fSml,fs*0.85f,V(x+nw,y+nf*0.2f),COL_INK2,tail.c_str());
        y+=nf*1.45f;
    }
    if(showStatus){
        std::string st = hasStatus? g_status : std::string("Set a status");
        float ip=fs*1.2f, gap=fs*0.45f;
        std::string shown=Clip(g_fSml,fs,st,fw-ip-gap-12);
        float w=ip+gap+TextW(g_fSml,fs,shown.c_str());
        float x=lineX(w);
        ImVec2 ha=V(x-7,y-fs*0.22f), hb=V(x+w+8,y+fs*1.32f);
        bool sh = live && inR(ha,hb);
        float sH = HoverAnim(0x5300000+uid,sh);
        if(sH>0.01f) dl->AddRectFilled(ha,hb,WithA(COL_INK2,(int)(34*sH)),fs);
        ImU32 ic = hasStatus? COL_GOLD : WithA(COL_INK2,200);
        const std::string& icn = hasStatus? g_statusIcon : std::string("add_reaction");
        if(!MsIcon(dl,icn.empty()? "mood":icn,V(x+ip*0.5f,y+fs*0.55f),ip,ic)) dl->AddCircle(V(x+ip*0.5f,y+fs*0.55f),ip*0.36f,ic,16,1.5f);
        TextAt(dl,g_fSml,fs,V(x+ip+gap,y),hasStatus? COL_INK : WithA(COL_INK2,210),shown.c_str());
        if(sh && click) ProfileEditorOpen(uid);
        y+=fs*1.6f;
    }
    for(int i=0;i<nRows;i++){
        const ProfileRow& r=rows[i];
        std::string val=CwText(r.text,cx);
        ImU32 ic = r.color.empty()? rowCol[i%3] : CwColor(r.color,rowCol[i%3]);
        float ip=fs*1.18f;
        std::string sep=" :  ";
        float sw=TextW(g_fSml,fs,sep.c_str());
        bool hasIcon=!r.icon.empty();
        float lead = hasIcon? ip+sw : 0.0f;
        std::string shown=Clip(g_fSml,fs,val,fw-lead-4);
        float w=lead+TextW(g_fSml,fs,shown.c_str());
        float x=lineX(w);
        if(hasIcon){
            if(!ProfileIcon(dl,r.icon,V(x+ip*0.5f,y+fs*0.56f),ip,ic)) dl->AddCircleFilled(V(x+ip*0.5f,y+fs*0.56f),fs*0.22f,ic);
            TextAt(dl,g_fSml,fs,V(x+ip,y),COL_INK,sep.c_str());
        }
        TextAt(dl,g_fSml,fs,V(x+lead,y),COL_INK,shown.c_str());
        y+=fs*1.62f;
    }
}

// ------------------------------------------------------------------------------------------------- editor popover
// Called by the drawer BEFORE its pages: clicks that land on last frame's popover are held back from the widgets
// underneath (they would otherwise hit the calendar or a media button through it), and handed back afterwards.
static void ProfileEditorShield(ImGuiIO& io){
    g_pe.shielded=false;
    if(!g_pe.open || g_pe.anim<0.05f) return;
    if(io.MousePos.x<g_pe.pa.x||io.MousePos.x>=g_pe.pb.x||io.MousePos.y<g_pe.pa.y||io.MousePos.y>=g_pe.pb.y) return;
    g_pe.shielded=true;
    g_pe.keepDown=io.MouseDown[0]; g_pe.keepClicked0=io.MouseClicked[0]; g_pe.keepClicked1=io.MouseClicked[1];
    g_pe.keepReleased=io.MouseReleased[0]; g_pe.keepWheel=io.MouseWheel;
    io.MouseDown[0]=false; io.MouseClicked[0]=false; io.MouseClicked[1]=false; io.MouseReleased[0]=false; io.MouseWheel=0;
}
static void ProfileEditorDraw(ImDrawList* dl,ImGuiIO& io,ImVec2 areaA,ImVec2 areaB){
    if(g_pe.shielded){ io.MouseDown[0]=g_pe.keepDown; io.MouseClicked[0]=g_pe.keepClicked0; io.MouseClicked[1]=g_pe.keepClicked1;
                       io.MouseReleased[0]=g_pe.keepReleased; io.MouseWheel=g_pe.keepWheel; g_pe.shielded=false; }
    float want=g_pe.open? 1.0f : 0.0f;
    g_pe.anim += (want-g_pe.anim)*std::min(1.0f,g_frameDt*(g_pe.open? 13.0f : 18.0f));
    if(fabsf(want-g_pe.anim)<0.003f) g_pe.anim=want;
    if(g_pe.anim<0.003f){ g_pe.pa=g_pe.pb=V(0,0); return; }
    if(g_pe.open) g_drawerForceUntil=std::max(g_drawerForceUntil,GetTickCount64()+700);   // typing: the drawer stays

    const float PW=372.0f, PAD=14.0f;
    const float PH=PAD+24+40+46+(30*2+6)+10+22+34+10+36+PAD;
    // below the card, else above it, else beside it - always inside the drawer
    float px=g_pe.a.x, py=g_pe.b.y+8;
    if(py+PH>areaB.y){ py=g_pe.a.y-8-PH; }
    if(py<areaA.y){ py=g_pe.a.y; px = (g_pe.b.x+10+PW<areaB.x)? g_pe.b.x+10 : g_pe.a.x-10-PW; }
    px=std::clamp(px,areaA.x,std::max(areaA.x,areaB.x-PW)); py=std::clamp(py,areaA.y,std::max(areaA.y,areaB.y-PH));
    float e=Cael::eval(Cael::DEFAULT_SPATIAL,std::clamp(g_pe.anim,0.0f,1.0f));
    float sc=0.92f+0.08f*e; int al=(int)(255*std::clamp(g_pe.anim,0.0f,1.0f));
    ImVec2 pc=V(px+PW*0.5f,py+PH*0.5f);
    ImVec2 A=V(pc.x-PW*0.5f*sc,pc.y-PH*0.5f*sc), B=V(pc.x+PW*0.5f*sc,pc.y+PH*0.5f*sc);
    g_pe.pa=A; g_pe.pb=B;
    auto a_=[&](ImU32 c){ return MulA(c,al/255.0f); };
    const bool live = g_pe.open && g_pe.anim>0.5f;
    const bool click = live && io.MouseClicked[0] && !g_pe.fresh;
    auto hit=[&](ImVec2 a,ImVec2 b){ return live && io.MousePos.x>=a.x&&io.MousePos.x<b.x&&io.MousePos.y>=a.y&&io.MousePos.y<b.y; };

    dl->AddRectFilled(V(A.x+2,A.y+6),V(B.x+2,B.y+8),IM_COL32(0,0,0,(int)(40*g_pe.anim)),18);      // soft drop
    dl->AddRectFilled(A,B,a_(g_darkUI? IM_COL32(30,30,36,250) : IM_COL32(250,249,252,252)),18);
    dl->AddRect(A,B,a_(WithA(COL_GOLD,90)),18,0,1.2f);
    dl->PushClipRect(A,B,true);
    // everything below is laid out at scale 1 from the scaled origin; at the end of the spring sc is 1 anyway
    float x0=A.x+PAD, x1=B.x-PAD, y=A.y+PAD;

    TextAt(dl,g_fMed,16,V(x0,y),a_(COL_INK),"Status");
    { const char* hint="Enter saves  \xC2\xB7  Esc cancels"; TextAt(dl,g_fSml,11,V(x1-TextW(g_fSml,11,hint),y+4),a_(COL_INK2),hint); }
    y+=24;

    // presence chips
    { float gw=6, cw=(x1-x0-gw*3)/4.0f;
      for(int i=0;i<4;i++){
          ImVec2 ca=V(x0+i*(cw+gw),y), cb=V(ca.x+cw,y+32); bool h=hit(ca,cb), sel=(g_pe.presence==i);
          float hA=HoverAnim(0x5400000+i,h);
          dl->AddRectFilled(ca,cb,a_(sel? WithA(COL_GOLD,52) : WithA(COL_INK2,(int)(22+hA*26))),16);
          if(sel) dl->AddRect(ca,cb,a_(WithA(COL_GOLD,170)),16,0,1.3f);
          ImU32 hole2=g_darkUI? IM_COL32(40,40,48,255) : IM_COL32(236,234,240,255);
          ProfilePresenceDot(dl,V(ca.x+15,ca.y+16),5.0f,i,MulA(sel? hole2 : hole2,1.0f));
          static const char* SH[]={"Online","Idle","Busy","Invisible"};
          std::string l=Clip(g_fSml,12.5f,SH[i],cw-30);
          TextAt(dl,g_fSml,12.5f,V(ca.x+26,ca.y+8),a_(sel? COL_INK : COL_INK2),l.c_str());
          if(h&&click) g_pe.presence=i;
      } }
    y+=40;

    // text field with the chosen icon in front of it
    { ImVec2 fa=V(x0,y), fb=V(x1,y+38);
      dl->AddRectFilled(fa,fb,a_(WithA(COL_INK2,34)),12);
      dl->AddRect(fa,fb,a_(WithA(COL_GOLD,150)),12,0,1.4f);
      MsIcon(dl,g_pe.icon,V(fa.x+20,fa.y+19),20,a_(COL_GOLD));
      if(live){
          size_t len=strlen(g_pe.text);
          for(int k=0;k<io.InputQueueCharacters.Size;k++){ unsigned c2=io.InputQueueCharacters[k];
              if(c2<32 || c2==127 || (c2>=0xD800&&c2<=0xDFFF)) continue;
              char u[4]; int n=0;
              if(c2<0x80){ u[0]=(char)c2; n=1; }
              else if(c2<0x800){ u[0]=(char)(0xC0|(c2>>6)); u[1]=(char)(0x80|(c2&0x3F)); n=2; }
              else { u[0]=(char)(0xE0|(c2>>12)); u[1]=(char)(0x80|((c2>>6)&0x3F)); u[2]=(char)(0x80|(c2&0x3F)); n=3; }
              if(len+n<sizeof(g_pe.text)-1){ memcpy(g_pe.text+len,u,n); len+=n; g_pe.text[len]=0; } }
          if(ImGui::IsKeyPressed(ImGuiKey_Backspace) && len>0){
              if(io.KeyCtrl) g_pe.text[0]=0;
              else { size_t k=len-1; while(k>0 && (((unsigned char)g_pe.text[k])&0xC0)==0x80) k--; g_pe.text[k]=0; } }
      }
      std::string shown=g_pe.text; float maxW=(fb.x-fa.x)-52;
      while(!shown.empty() && TextW(g_fSml,15,shown.c_str())>maxW){ size_t k=0; do{ k++; } while(k<shown.size() && (((unsigned char)shown[k])&0xC0)==0x80); shown.erase(0,k); }
      if(shown.empty()) TextAt(dl,g_fSml,15,V(fa.x+38,fa.y+10),a_(WithA(COL_INK2,190)),"What's happening?");
      else TextAt(dl,g_fSml,15,V(fa.x+38,fa.y+10),a_(COL_INK),shown.c_str());
      if(live && ((GetTickCount64()/530)&1)==0){ float cxp=fa.x+38+TextW(g_fSml,15,shown.c_str())+1;
          dl->AddRectFilled(V(cxp,fa.y+9),V(cxp+1.8f,fb.y-9),a_(COL_INK)); } }
    y+=46;

    // icon grid: 12 per row, 2 rows
    { const int PER=12; float cell=(x1-x0)/PER;
      for(int i=0;i<PROFILE_NICONS && i<PER*2;i++){
          ImVec2 cc=V(x0+cell*(i%PER)+cell*0.5f, y+15+(i/PER)*36);
          ImVec2 ca=V(cc.x-cell*0.5f+1,cc.y-15), cb=V(cc.x+cell*0.5f-1,cc.y+15);
          bool h=hit(ca,cb), sel=(g_pe.icon==PROFILE_ICONS[i]);
          float hA=HoverAnim(0x5500000+i,h);
          if(sel) dl->AddCircleFilled(cc,14,a_(WithA(COL_GOLD,60)),24);
          else if(hA>0.01f) dl->AddCircleFilled(cc,14,a_(WithA(COL_INK2,(int)(40*hA))),24);
          MsIcon(dl,PROFILE_ICONS[i],cc,18,a_(sel? COL_GOLD : COL_INK2));
          if(h&&click) g_pe.icon=PROFILE_ICONS[i];
      } }
    y+=30*2+6+10;

    // clear after
    TextAt(dl,g_fSml,12,V(x0,y),a_(COL_INK2),"Clear after");
    y+=22;
    { float gw=6, cw=(x1-x0-gw*4)/5.0f; int cur=ProfileClearIndex(g_pe.clearMin);
      for(int i=0;i<5;i++){
          ImVec2 ca=V(x0+i*(cw+gw),y), cb=V(ca.x+cw,y+28); bool h=hit(ca,cb), sel=(cur==i);
          float hA=HoverAnim(0x5600000+i,h);
          dl->AddRectFilled(ca,cb,a_(sel? WithA(COL_GOLD,52) : WithA(COL_INK2,(int)(22+hA*26))),14);
          std::string l=Clip(g_fSml,12,PROFILE_CLEAR_LABEL[i],cw-8);
          TextAt(dl,g_fSml,12,V(ca.x+(cw-TextW(g_fSml,12,l.c_str()))*0.5f,ca.y+7),a_(sel? COL_INK : COL_INK2),l.c_str());
          if(h&&click) g_pe.clearMin=PROFILE_CLEAR_MIN[i];
      } }
    y+=34+10;

    // buttons
    bool save=false, clear=false, cancel=false;
    { ImVec2 sa=V(x1-104,y), sb=V(x1,y+34); bool h=hit(sa,sb); float hA=HoverAnim(0x5700001,h);
      dl->AddRectFilled(sa,sb,a_(WithA(COL_GOLD,(int)(215+40*hA))),17);
      TextAt(dl,g_fMed,14,V(sa.x+(104-TextW(g_fMed,14,"Save"))*0.5f,sa.y+9),a_(M3OnPrimary()),"Save");
      if(h&&click) save=true;
      ImVec2 ca=V(x0,y), cb=V(x0+118,y+34); bool h2=hit(ca,cb); float hB=HoverAnim(0x5700002,h2);
      dl->AddRectFilled(ca,cb,a_(WithA(COL_INK2,(int)(28+30*hB))),17);
      MsIcon(dl,"backspace",V(ca.x+20,ca.y+17),16,a_(COL_INK2));
      TextAt(dl,g_fSml,13.5f,V(ca.x+34,ca.y+9),a_(COL_INK),"Clear status");
      if(h2&&click) clear=true;
      ImVec2 xa=V(x1-104-10-84,y), xb=V(x1-104-10,y+34); bool h3=hit(xa,xb); float hC=HoverAnim(0x5700003,h3);
      if(hC>0.01f) dl->AddRectFilled(xa,xb,a_(WithA(COL_INK2,(int)(30*hC))),17);
      TextAt(dl,g_fSml,13.5f,V(xa.x+(84-TextW(g_fSml,13.5f,"Cancel"))*0.5f,xa.y+9),a_(COL_INK2),"Cancel");
      if(h3&&click) cancel=true; }
    dl->PopClipRect();

    if(live){
        if(ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) save=true;
        if(ImGui::IsKeyPressed(ImGuiKey_Escape)) cancel=true;
        if(click && !(io.MousePos.x>=A.x&&io.MousePos.x<B.x&&io.MousePos.y>=A.y&&io.MousePos.y<B.y)) cancel=true;
    }
    if(save){
        std::string t=Tml::Trim(g_pe.text);
        ProfileSetStatus(t,g_pe.icon,g_pe.clearMin);
        if(g_pe.presence!=g_presence || g_presenceDndLink) ProfileSetPresence(g_pe.presence);
        g_pe.open=false;
    } else if(clear){ ProfileSetStatus("",g_pe.icon,0); g_pe.open=false; }
    else if(cancel) g_pe.open=false;
    g_pe.fresh=false;
}
