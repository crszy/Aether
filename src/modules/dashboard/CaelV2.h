// src/modules/dashboard/CaelV2.h  —  Aether shell
// The newer Caelestia look (reference\v2\NOTES.md): the Dashboard / Media / Performance tabs rebuilt card for card.
// Every card here is ALSO a placeable widget (+ Add widget > Caelestia), and each tab chooses its layout in
// Settings > Dashboard ("classic" keeps the older Aether pages). Nothing in here replaces anything.
#pragma once

// ============================================================================================ primitives
static ImU32 V2Cont(ImU32 role){ return Mix(COL_CARD2, role, g_darkUI? 0.32f : 0.42f); }     // an M3 "container"
static ImU32 V2Track(){ return Mix(COL_CARD2, COL_INK2, g_darkUI? 0.42f : 0.35f); }
static ImFont* V2Cond(){ return g_fCond? g_fCond : g_fMed; }

// M3 arc with round caps
static void V2Arc(ImDrawList* dl,ImVec2 c,float R,float a0,float a1,ImU32 col,float th){
    if(a1-a0<0.002f || R<1.0f) return;
    int seg=std::clamp((int)((a1-a0)*R/2.5f),8,160);
    dl->PathArcTo(c,R,a0,a1,seg); dl->PathStroke(col,0,th);
    dl->AddCircleFilled(V(c.x+cosf(a0)*R,c.y+sinf(a0)*R),th*0.5f,col,12);
    dl->AddCircleFilled(V(c.x+cosf(a1)*R,c.y+sinf(a1)*R),th*0.5f,col,12);
}
// M3 circular progress: active arc, a gap, then the track (Caelestia's CircularProgress)
static void V2Ring(ImDrawList* dl,ImVec2 c,float R,float th,float frac,ImU32 act,ImU32 track,
                   float a0=1.95f,float sweep=5.85f){
    frac=std::clamp(frac,0.0f,1.0f);
    float gap=(th*2.2f)/std::max(R,1.0f);
    float ae=a0+sweep*frac;
    if(frac>0.004f) V2Arc(dl,c,R,a0,ae,act,th);
    float ts = frac>0.004f? ae+gap : a0, te=a0+sweep - (frac>0.996f? 0.0f : 0.0f);
    if(te-ts>0.03f) V2Arc(dl,c,R,ts,te,track,th*0.78f);
}
// M3 linear progress: active, gap, track, dot at the end
static void V2Bar(ImDrawList* dl,float x0,float x1,float y,float th,float frac,ImU32 act,ImU32 track){
    frac=std::clamp(frac,0.0f,1.0f); float r=th*0.5f;
    float xa=x0+(x1-x0)*frac;
    if(frac>0.003f) dl->AddRectFilled(V(x0,y-r),V(std::max(xa,x0+th),y+r),act,r);
    float ts = frac>0.003f? xa+th+3.0f : x0;
    if(ts<x1-th) dl->AddRectFilled(V(ts,y-r),V(x1,y+r),track,r);
    dl->AddCircleFilled(V(x1-r,y),r*0.62f,act,12);
}
// a round / rounded-square icon button; returns true on click
static bool V2Btn(ImDrawList* dl,ImGuiIO& io,ImVec2 c,float w,float h,const char* icon,bool filled,int id,bool click,float round=-1){
    ImVec2 a=V(c.x-w*0.5f,c.y-h*0.5f), b=V(c.x+w*0.5f,c.y+h*0.5f);
    bool hov=io.MousePos.x>=a.x&&io.MousePos.x<b.x&&io.MousePos.y>=a.y&&io.MousePos.y<b.y;
    float ha=HoverAnim(id,hov);
    float rr = round>=0? round : std::min(w,h)*0.5f;
    ImU32 bg = filled? Mix(COL_INK,COL_GOLD,0.25f) : WithA(Mix(COL_CARD2,COL_INK2,0.18f),255);
    if(ha>0.01f) bg=Mix(bg, filled? COL_INK : COL_INK2, 0.12f*ha);
    dl->AddRectFilled(a,b,bg,rr);
    MsIcon(dl,icon,c,std::min(w,h)*0.52f, filled? M3OnPrimary() : COL_INK);
    return hov&&click;
}
// split button: [icon label][⌄]
static int V2Split(ImDrawList* dl,ImGuiIO& io,ImVec2 a,float w,float h,const char* icon,const std::string& label,int id,bool click){
    float cw=h; ImVec2 b=V(a.x+w-cw-4,a.y+h);
    bool h1=io.MousePos.x>=a.x&&io.MousePos.x<b.x&&io.MousePos.y>=a.y&&io.MousePos.y<b.y;
    ImVec2 ca=V(b.x+4,a.y), cb=V(a.x+w,a.y+h);
    bool h2=io.MousePos.x>=ca.x&&io.MousePos.x<cb.x&&io.MousePos.y>=ca.y&&io.MousePos.y<cb.y;
    float ha=HoverAnim(id,h1), hb=HoverAnim(id+1,h2);
    ImU32 base=Mix(COL_CARD2,COL_INK2,0.20f);
    dl->AddRectFilled(a,b,Mix(base,COL_INK2,0.15f*ha),h*0.5f,ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled(ca,cb,Mix(base,COL_INK2,0.15f*hb),h*0.5f,ImDrawFlags_RoundCornersRight);
    float fs=std::min(14.0f,h*0.52f);
    std::string l=Clip(g_fSml,fs,label,(b.x-a.x)-fs*2.6f);
    float tw=fs*1.3f+TextW(g_fSml,fs,l.c_str()); float x=a.x+((b.x-a.x)-tw)*0.5f;
    MsIcon(dl,icon,V(x+fs*0.55f,a.y+h*0.5f),fs*1.1f,COL_INK);
    TextAt(dl,g_fSml,fs,V(x+fs*1.3f,a.y+(h-fs)*0.5f-1),COL_INK,l.c_str());
    MsIcon(dl,"expand_more",V((ca.x+cb.x)*0.5f,a.y+h*0.5f),fs*1.2f,COL_INK);
    if(click&&h1) return 1; if(click&&h2) return 2; return 0;
}
// a small drop-down list anchored under/over a rect; returns picked index or -1. `open` is toggled by the caller.
static int V2Menu(ImDrawList* dl,ImGuiIO& io,bool& open,float& anim,ImVec2 anchorA,ImVec2 anchorB,
                  const std::vector<std::string>& items,int cur,bool up,bool clickConsumedByToggle){
    anim += ((open?1.0f:0.0f)-anim)*std::min(1.0f,g_frameDt*16.0f);
    if(anim<0.01f){ anim=open?anim:0.0f; if(!open) return -1; }
    float rh=26, W=std::max(anchorB.x-anchorA.x,160.0f), H=items.size()*rh+10;
    ImVec2 a = up? V(anchorA.x,anchorA.y-6-H) : V(anchorA.x,anchorB.y+6);
    ImVec2 b = V(a.x+W,a.y+H);
    int al=(int)(255*std::clamp(anim,0.0f,1.0f));
    dl->AddRectFilled(V(a.x+1,a.y+3),V(b.x+1,b.y+4),IM_COL32(0,0,0,(int)(50*anim)),12);
    dl->AddRectFilled(a,b,MulA(Mix(COL_CARD2,COL_INK2,0.10f),anim),12);
    int pick=-1;
    for(size_t i=0;i<items.size();i++){
        ImVec2 ra=V(a.x+5,a.y+5+i*rh), rb=V(b.x-5,ra.y+rh);
        bool h=io.MousePos.x>=ra.x&&io.MousePos.x<rb.x&&io.MousePos.y>=ra.y&&io.MousePos.y<rb.y;
        if((int)i==cur) dl->AddRectFilled(ra,rb,WithA(COL_GOLD,(int)(50*anim)),8);
        else if(h) dl->AddRectFilled(ra,rb,WithA(COL_INK2,(int)(40*anim)),8);
        std::string l=Clip(g_fSml,13.5f,items[i],W-24);
        TextAt(dl,g_fSml,13.5f,V(ra.x+10,ra.y+5),WithA((int)i==cur? COL_INK : COL_INK2,al),l.c_str());
        if(h&&io.MouseClicked[0]&&open) pick=(int)i;
    }
    bool inside=io.MousePos.x>=a.x&&io.MousePos.x<b.x&&io.MousePos.y>=a.y&&io.MousePos.y<b.y;
    if(pick>=0) open=false;
    else if(io.MouseClicked[0] && !inside && !clickConsumedByToggle) open=false;
    return pick;
}
// ImGui 1.88 has no ellipse primitive
static void V2Ellipse(ImDrawList* dl,ImVec2 c,ImVec2 r,ImU32 col,float rot=0.0f){
    const int N=24; ImVec2 pts[N]; float cr=cosf(rot), sr=sinf(rot);
    for(int i=0;i<N;i++){ float a=i*6.2831853f/N; float x=cosf(a)*r.x, y=sinf(a)*r.y; pts[i]=V(c.x+x*cr-y*sr,c.y+x*sr+y*cr); }
    dl->AddConvexPolyFilled(pts,N,col);
}
// Puff: Aether's own little mascot for the media card - a round cloud with a leaf sprout that bobs to the music
static void V2Puff(ImDrawList* dl,ImVec2 c,float s,bool playing){
    float t=(float)ImGui::GetTime(); float bob = playing? sinf(t*6.2f)*s*0.06f : sinf(t*1.4f)*s*0.02f;
    float sq = playing? 1.0f+0.05f*sinf(t*12.4f) : 1.0f;
    ImU32 body=IM_COL32(245,245,247,255), shade=IM_COL32(210,212,220,255), ink=IM_COL32(40,40,48,255);
    ImVec2 b=V(c.x,c.y+bob);
    V2Ellipse(dl,V(c.x,c.y+s*0.46f),ImVec2(s*0.62f,s*0.09f),IM_COL32(0,0,0,40));
    dl->AddCircleFilled(V(b.x-s*0.34f,b.y+s*0.08f),s*0.30f*sq,shade,28);
    dl->AddCircleFilled(V(b.x+s*0.36f,b.y+s*0.10f),s*0.28f*sq,shade,28);
    dl->AddCircleFilled(V(b.x,b.y-s*0.06f),s*0.42f*sq,body,32);
    dl->AddCircleFilled(V(b.x-s*0.30f,b.y+s*0.06f),s*0.27f*sq,body,28);
    dl->AddCircleFilled(V(b.x+s*0.32f,b.y+s*0.08f),s*0.25f*sq,body,28);
    dl->AddRectFilled(V(b.x-s*0.52f,b.y+s*0.02f),V(b.x+s*0.52f,b.y+s*0.32f),body,s*0.14f);
    // sprout
    float sw = playing? sinf(t*6.2f+0.8f)*0.25f : 0.0f;
    ImVec2 st=V(b.x,b.y-s*0.46f), tip=V(b.x+sinf(sw)*s*0.16f,b.y-s*0.70f);
    dl->AddLine(st,tip,IM_COL32(120,190,120,255),s*0.05f);
    V2Ellipse(dl,V(tip.x+s*0.09f,tip.y-s*0.02f),ImVec2(s*0.10f,s*0.05f),IM_COL32(140,210,130,255),-0.5f+sw);
    // face: closed happy eyes when playing
    float ey=b.y+s*0.02f;
    if(playing){ dl->PathArcTo(V(b.x-s*0.16f,ey),s*0.06f,3.4f,6.0f,8); dl->PathStroke(ink,0,s*0.035f);
                 dl->PathArcTo(V(b.x+s*0.16f,ey),s*0.06f,3.4f,6.0f,8); dl->PathStroke(ink,0,s*0.035f); }
    else { dl->AddCircleFilled(V(b.x-s*0.16f,ey),s*0.04f,ink,10); dl->AddCircleFilled(V(b.x+s*0.16f,ey),s*0.04f,ink,10); }
    dl->PathArcTo(V(b.x,ey+s*0.06f),s*0.05f,0.4f,2.7f,8); dl->PathStroke(ink,0,s*0.03f);
    dl->AddCircleFilled(V(b.x-s*0.28f,ey+s*0.09f),s*0.05f,IM_COL32(255,170,180,150),10);
    dl->AddCircleFilled(V(b.x+s*0.28f,ey+s*0.09f),s*0.05f,IM_COL32(255,170,180,150),10);
    if(playing){ float n=fmodf(t*0.9f,1.0f);   // a note floating off
        ImVec2 np=V(b.x+s*0.55f+n*s*0.2f,b.y-s*0.35f-n*s*0.5f); int na=(int)(220*(1.0f-n));
        MsIcon(dl,"music_note",np,s*0.28f,WithA(COL_INK,na)); }
}
static std::string V2Rate(double bps){ char c[32];
    if(bps>=1048576.0) snprintf(c,32,"%.1f MiB/s",bps/1048576.0);
    else if(bps>=1024.0) snprintf(c,32,"%.1f KiB/s",bps/1024.0);
    else snprintf(c,32,"%.1f B/s",bps);
    return c; }

// ============================================================================================ data
struct V2Drive { std::string root, label; unsigned long long used=0,total=0; };
static std::vector<V2Drive> g_v2Drives; static int g_v2Drive=0;
static void V2DrivesTick(){
    static ULONGLONG last=0; ULONGLONG t=GetTickCount64(); if(last && t-last<3000) return; last=t;
    std::vector<V2Drive> out; wchar_t buf[512]; DWORD n=GetLogicalDriveStringsW(511,buf);
    for(wchar_t* p=buf; n && *p; p+=wcslen(p)+1){
        if(GetDriveTypeW(p)!=DRIVE_FIXED) continue;
        ULARGE_INTEGER fr{},tot{},frAll{}; if(!GetDiskFreeSpaceExW(p,&fr,&tot,&frAll)) continue;
        V2Drive d; d.root=W2U8(p); if(!d.root.empty()&&d.root.back()=='\\') d.root.pop_back();
        wchar_t lab[MAX_PATH]={0}; GetVolumeInformationW(p,lab,MAX_PATH,nullptr,nullptr,nullptr,nullptr,0);
        d.label=W2U8(lab); d.total=tot.QuadPart; d.used=tot.QuadPart-frAll.QuadPart; out.push_back(d);
    }
    g_v2Drives=out; if(g_v2Drive>=(int)g_v2Drives.size()) g_v2Drive=0;
}
static std::vector<float> g_v2UpHist;
static void V2NetTick(){
    static ULONGLONG last=0; ULONGLONG t=GetTickCount64(); if(t-last<1000) return; last=t;
    g_v2UpHist.push_back((float)(g_st.netUp/1024.0)); if(g_v2UpHist.size()>60) g_v2UpHist.erase(g_v2UpHist.begin());
}

// ============================================================================================ Performance cards
static void V2HeroCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,int which){
    (void)io; Card(dl,o,s);
    double usage = which==0? g_st.cpuUsage : g_st.gpuUsage;
    double temp  = which==0? g_st.cpuTemp  : g_st.gpuTemp;
    float u=Cael::anim(0x62000+which,(float)usage,Cael::DUR_DEFAULT_SPATIAL*2,Cael::DEFAULT_SPATIAL);
    float pad=std::clamp(s.y*0.14f,10.0f,16.0f);
    float iconR=std::clamp(s.y*0.18f,12.0f,19.0f);
    ImVec2 ic=V(o.x+pad+iconR,o.y+pad+iconR);
    // The badge layout's little usage ring with the chip icon in it. The arc layout puts its own
    // big gauge in that corner, so drawing this too just stacks two rings on top of each other.
    if(g_heroStyle!=1){
        V2Ring(dl,ic,iconR,2.6f,u,COL_GOLD,V2Track(),-1.5708f,6.2832f);
        MsIcon(dl,which==0? "memory" : "desktop_windows",ic,iconR*1.05f,COL_INK);
    }
    float tx=ic.x+iconR+12;

    // ---- dashboard.hero_style = "arc": the reference shell's layout -----------------------------
    // Measured off the capture: ONE wide arc, and the thing it encodes is USAGE - not temperature,
    // which is the easy misreading. The temperature is the big number in the middle of that arc with
    // its label beneath, and the usage percentage sits OUTSIDE the arc to its right. Aether's own
    // layout puts the percentage in the middle and the temperature on a bar, which is why the two
    // never looked alike however the colours were tuned.
    if(g_heroStyle==1){
        // The hero cards are WIDE AND SHORT (about 430x75 here), so the title cannot sit above the
        // arc the way it does in the badge layout - doing that left ~40px for the whole gauge and
        // the arc came out the size of a coin. Everything is laid out in a row instead: arc hard
        // left using the full height, the usage percentage beside it, and the name off to the right.
        float R = std::clamp(std::min(s.y*0.40f, s.x*0.16f), 16.0f, 60.0f);
        ImVec2 c = V(o.x+pad+R, o.y+s.y*0.5f);
        V2Ring(dl,c,R,std::max(3.0f,R*0.15f),u,COL_GOLD,V2Track(),2.3562f,4.7124f);

        // Temperature, large, in the middle of the arc. g_fMed, NOT the condensed face: the
        // condensed font has no degree glyph and rendered "52" as "5???".
        char tcArc[16];
        if(temp>0) snprintf(tcArc,16,"%d°",(int)std::round(temp));
        else       snprintf(tcArc,16,"--°");
        float tf2=R*0.62f;
        { float w=TextW(g_fMed,tf2,tcArc); if(w>R*1.45f) tf2*=R*1.45f/w; }
        TextAt(dl,g_fMed,tf2,V(c.x-TextW(g_fMed,tf2,tcArc)*0.5f,c.y-tf2*0.72f),temp>85? COL_ERR : COL_INK,tcArc);
        float lf=std::clamp(R*0.26f,9.0f,13.0f);
        TextAt(dl,g_fSml,lf,V(c.x-TextW(g_fSml,lf,"Temp")*0.5f,c.y+tf2*0.16f),COL_INK2,"Temp");

        // usage percentage OUTSIDE the arc, to its right
        char pcArc[16]; snprintf(pcArc,16,"%d%%",(int)std::round(u*100));
        ImFont* cf2=V2Cond();
        float ux=c.x+R+std::clamp(s.x*0.035f,10.0f,20.0f);
        float uf2=std::clamp(R*0.80f,14.0f,34.0f);
        TextAt(dl,cf2,uf2,V(ux,c.y-uf2*0.80f),COL_INK,pcArc);
        float ulf=std::clamp(R*0.24f,9.0f,13.0f);
        TextAt(dl,g_fSml,ulf,V(ux,c.y+uf2*0.10f),COL_INK2,"Usage");

        // name block to the right of the number
        float nx=ux+std::max(TextW(cf2,uf2,pcArc),TextW(g_fSml,ulf,"Usage"))+std::clamp(s.x*0.04f,12.0f,26.0f);
        float room=(o.x+s.x-pad)-nx;
        if(room>40){
            float hf2=std::clamp(s.y*0.20f,13.0f,18.0f);
            TextAt(dl,g_fMed,hf2,V(nx,c.y-hf2*1.05f),COL_INK,which==0? "CPU":"GPU");
            std::string nm2=W2U8(which==0? g_st.cpuName : g_st.gpuName);
            float nf2=std::clamp(s.y*0.15f,10.0f,13.0f);
            TextAt(dl,g_fSml,nf2,V(nx,c.y+2),COL_INK2,Clip(g_fSml,nf2,nm2,room).c_str());
        }
        return;
    }

    float bR=std::clamp(std::min(s.y*0.30f,s.x*0.14f),16.0f,40.0f);
    ImVec2 bc=V(o.x+s.x-pad-bR,o.y+s.y*0.60f);
    TextAt(dl,g_fMed,std::clamp(s.y*0.19f,14.0f,18.0f),V(tx,o.y+pad-2),COL_INK,which==0? "CPU":"GPU");
    std::string nm=W2U8(which==0? g_st.cpuName : g_st.gpuName);
    float nf=std::clamp(s.y*0.14f,11.0f,14.0f);
    nm=Clip(g_fSml,nf,nm,(bc.x-bR-10)-tx);
    TextAt(dl,g_fSml,nf,V(tx,o.y+pad+std::clamp(s.y*0.21f,16.0f,22.0f)),COL_INK2,nm.c_str());
    // usage: a cookie badge with the number set condensed
    TextAt(dl,g_fSml,nf,V(bc.x-TextW(g_fSml,nf,"Usage")*0.5f,bc.y-bR-nf-4),COL_INK2,"Usage");
    M3Shape(dl,bc,bR,V2Cont(COL_INK2),M3_COOKIE4,0.0f);
    char pc[16]; snprintf(pc,16,"%d%%",(int)std::round(u*100));
    ImFont* cf=V2Cond(); float pf=bR*0.95f; { float w=TextW(cf,pf,pc); if(w>bR*1.55f) pf*=bR*1.55f/w; }
    TextAt(dl,cf,pf,V(bc.x-TextW(cf,pf,pc)*0.5f,bc.y-pf*0.58f),COL_INK,pc);
    // temperature + bar
    float ty=o.y+s.y-pad-std::clamp(s.y*0.30f,20.0f,30.0f);
    char tc[16]; if(temp>0) snprintf(tc,16,"%d\xC2\xB0""C",(int)std::round(temp)); else snprintf(tc,16,"--\xC2\xB0""C");
    float tf=std::clamp(s.y*0.16f,12.0f,16.0f);
    MsIcon(dl,"device_thermostat",V(o.x+pad+tf*0.45f,ty+tf*0.55f),tf*1.1f,COL_INK2);
    TextAt(dl,g_fMed,tf,V(o.x+pad+tf*1.1f,ty),temp>85? COL_ERR : COL_INK,tc);
    float by=o.y+s.y-pad-2, bx1=std::max(o.x+pad+60,std::min(o.x+s.x*0.52f,bc.x-bR-16));
    V2Bar(dl,o.x+pad,bx1,by,4.0f,temp>0? (float)std::clamp(temp/100.0,0.0,1.0) : 0.0f,temp>85? COL_ERR : COL_INK,V2Track());
}
static void V2StorageCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,int uid){
    Card(dl,o,s); V2DrivesTick();
    const V2Drive* d = g_v2Drives.empty()? nullptr : &g_v2Drives[std::clamp(g_v2Drive,0,(int)g_v2Drives.size()-1)];
    float frac = d&&d->total? (float)((double)d->used/d->total) : 0.0f;
    float fa=Cael::anim(0x62100+uid,frac,Cael::DUR_DEFAULT_SPATIAL*2,Cael::DEFAULT_SPATIAL);
    float pad=std::clamp(s.y*0.08f,10.0f,16.0f);
    float btnH=std::clamp(s.y*0.17f,22.0f,28.0f);
    float R=std::clamp(std::min((s.y-btnH-pad*3)*0.5f,s.x*0.20f),18.0f,60.0f);
    ImVec2 c=V(o.x+pad+R+4,o.y+pad+R+2);
    V2Ring(dl,c,R,std::max(3.0f,R*0.075f),fa,COL_GOLD,V2Track());
    MsIcon(dl,"hard_drive",V(c.x,c.y-R*0.46f),R*0.30f,COL_INK2);
    char pc[16]; snprintf(pc,16,"%d%%",(int)std::round(fa*100));
    float pf=R*0.40f; TextAt(dl,g_fMed,pf,V(c.x-TextW(g_fMed,pf,pc)*0.5f,c.y-pf*0.45f),COL_INK,pc);
    float uf=std::max(10.0f,R*0.22f); TextAt(dl,g_fSml,uf,V(c.x-TextW(g_fSml,uf,"Used")*0.5f,c.y+pf*0.55f),COL_INK2,"Used");
    float tx=c.x+R+16, tw=o.x+s.x-pad-tx;
    float hf=std::clamp(R*0.34f,13.0f,18.0f);
    TextAt(dl,g_fMed,hf,V(tx,c.y-hf*1.2f),COL_INK,"Storage");
    std::string sub = d? GiB(d->used)+" / "+GiB(d->total)+" GiB" : std::string("no drives");
    TextAt(dl,g_fSml,hf*0.88f,V(tx,c.y+2),COL_INK2,Clip(g_fSml,hf*0.88f,sub,tw).c_str());
    // drive picker
    static std::unordered_map<int,bool> open; static std::unordered_map<int,float> anim;
    float bw=std::min(s.x-pad*2,std::max(140.0f,s.x*0.60f));
    ImVec2 ba=V(o.x+(s.x-bw)*0.5f,o.y+s.y-pad-btnH);
    std::string lab = d? (d->root+(d->label.empty()? "" : "  "+d->label)) : std::string("-");
    int hit=V2Split(dl,io,ba,bw,btnH,"storage",lab,0x62200+uid*4,io.MouseClicked[0]);
    bool toggled=false; if(hit){ open[uid]=!open[uid]; toggled=true; }
    std::vector<std::string> items; for(auto& x:g_v2Drives) items.push_back(x.root+(x.label.empty()? "" : "  "+x.label)+"   "+GiB(x.used)+" / "+GiB(x.total)+" GiB");
    int p=V2Menu(dl,io,open[uid],anim[uid],ba,V(ba.x+bw,ba.y+btnH),items,g_v2Drive,true,toggled);
    if(p>=0) g_v2Drive=p;
}
static void V2NetworkCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s){
    (void)io; Card(dl,o,s); V2NetTick();
    float pad=std::clamp(s.y*0.08f,10.0f,16.0f), hf=std::clamp(s.y*0.11f,13.0f,18.0f);
    MsIcon(dl,"swap_vert",V(o.x+pad+hf*0.5f,o.y+pad+hf*0.62f),hf*1.15f,COL_INK);
    TextAt(dl,g_fMed,hf,V(o.x+pad+hf*1.35f,o.y+pad),COL_INK,"Network");
    float rowH=std::clamp(s.y*0.14f,16.0f,24.0f);
    float gx0=o.x+pad, gx1=o.x+s.x-pad, gy0=o.y+pad+hf*1.8f, gy1=o.y+s.y-pad-rowH*3-4;
    auto graph=[&](const std::vector<float>& h,ImU32 line,ImU32 fill){
        int n=(int)h.size(); if(n<2 || gy1-gy0<8) return;
        float mx=1; for(float v:h) mx=std::max(mx,v); for(float v:g_st.netHist) mx=std::max(mx,v);
        std::vector<ImVec2> pts; pts.reserve(n);
        for(int i=0;i<n;i++) pts.push_back(V(gx0+(gx1-gx0)*i/(n-1), gy1-(gy1-gy0)*std::clamp(h[i]/mx,0.0f,1.0f)));
        for(int i=0;i+1<n;i++) dl->AddQuadFilled(pts[i],pts[i+1],V(pts[i+1].x,gy1),V(pts[i].x,gy1),fill);
        dl->AddPolyline(pts.data(),n,line,0,1.4f);
    };
    graph(g_v2UpHist,WithA(COL_INK2,160),WithA(COL_INK2,40));
    graph(g_st.netHist,COL_INK,WithA(COL_INK,46));
    float rf=std::clamp(rowH*0.62f,11.0f,14.0f), ry=gy1+6;
    struct R{ const char* ic; const char* l; std::string v; } rows[]={
        {"download","Download",V2Rate(g_st.netDown)},{"upload","Upload",V2Rate(g_st.netUp)},
        {"history","Total",std::string("\xE2\x86\x93")+GiBs(g_st.netTotalIn)+" \xE2\x86\x91"+GiBs(g_st.netTotalOut)} };
    for(auto& r:rows){
        MsIcon(dl,r.ic,V(o.x+pad+rf*0.5f,ry+rf*0.6f),rf*1.2f,COL_INK2);
        TextAt(dl,g_fSml,rf,V(o.x+pad+rf*1.5f,ry),COL_INK2,r.l);
        TextAt(dl,g_fMed,rf,V(o.x+s.x-pad-TextW(g_fMed,rf,r.v.c_str()),ry),COL_INK,r.v.c_str());
        ry+=rowH;
    }
}
static void V2MemoryCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s){
    (void)io; Card(dl,o,s);
    float pad=std::clamp(s.y*0.08f,10.0f,16.0f), hf=std::clamp(s.y*0.11f,13.0f,18.0f);
    MsIcon(dl,"memory_alt",V(o.x+pad+hf*0.5f,o.y+pad+hf*0.62f),hf*1.15f,COL_INK);
    TextAt(dl,g_fMed,hf,V(o.x+pad+hf*1.35f,o.y+pad),COL_INK,"Memory");
    float frac=g_st.memTotal? (float)((double)g_st.memUsed/g_st.memTotal) : 0.0f;
    float fa=Cael::anim(0x62300,frac,Cael::DUR_DEFAULT_SPATIAL*2,Cael::DEFAULT_SPATIAL);
    float foot=hf*1.6f;
    float R=std::clamp(std::min(s.x*0.32f,(s.y-pad*2-hf*1.8f-foot)*0.5f),14.0f,60.0f);
    ImVec2 c=V(o.x+s.x*0.5f,o.y+pad+hf*1.8f+R+2);
    V2Ring(dl,c,R,std::max(3.0f,R*0.08f),fa,COL_GOLD,V2Track());
    char pc[16]; snprintf(pc,16,"%d%%",(int)std::round(fa*100));
    float pf=R*0.46f; TextAt(dl,g_fMed,pf,V(c.x-TextW(g_fMed,pf,pc)*0.5f,c.y-pf*0.62f),COL_INK,pc);
    float uf=std::max(10.0f,R*0.24f); TextAt(dl,g_fSml,uf,V(c.x-TextW(g_fSml,uf,"Used")*0.5f,c.y+pf*0.45f),COL_INK2,"Used");
    std::string sub=GiB(g_st.memUsed)+" / "+GiB(g_st.memTotal)+" GiB";
    TextAt(dl,g_fMed,hf*0.9f,V(c.x-TextW(g_fMed,hf*0.9f,sub.c_str())*0.5f,o.y+s.y-pad-hf),COL_INK,sub.c_str());
}
static void DrawPerformanceV2(ImDrawList* dl,ImVec2 org,ImVec2 area){
    ImGuiIO& io=ImGui::GetIO(); const float g=10;
    float h1=(area.y-g)*0.40f, h2=area.y-g-h1, hw=(area.x-g)*0.5f;
    V2HeroCard(dl,io,org,V(hw,h1),0);
    V2HeroCard(dl,io,V(org.x+hw+g,org.y),V(hw,h1),1);
    float y2=org.y+h1+g, w1=(area.x-2*g)*0.385f, w3=area.x-2*g-2*w1;
    V2StorageCard(dl,io,V(org.x,y2),V(w1,h2),0);
    V2NetworkCard(dl,io,V(org.x+w1+g,y2),V(w1,h2));
    V2MemoryCard(dl,io,V(org.x+2*(w1+g),y2),V(w3,h2));
}

// ============================================================================================ Media
static std::string V2Time(double sec){ char c[16]; if(sec<0) sec=0; snprintf(c,16,"%d:%02d",(int)sec/60,(int)sec%60); return c; }
// dotted visualiser ring around the cover
static void V2VisRing(ImDrawList* dl,ImVec2 c,float artR,float outR,ImU32 col){
    const int N=72; float bands[72]; AudioBands(bands,N,g_md.playing);
    float r0=artR+std::max(6.0f,artR*0.14f), span=std::max(4.0f,outR-r0);
    float th=std::clamp(artR*0.055f,2.0f,4.0f);
    for(int i=0;i<N;i++){
        float a=i*(6.2831853f/N)-1.5708f;
        float len=th*1.6f + bands[i]*span;
        ImVec2 p0=V(c.x+cosf(a)*r0,c.y+sinf(a)*r0), p1=V(c.x+cosf(a)*(r0+len),c.y+sinf(a)*(r0+len));
        dl->AddLine(p0,p1,col,th); dl->AddCircleFilled(p0,th*0.5f,col,8); dl->AddCircleFilled(p1,th*0.5f,col,8);
    }
    dl->AddCircleFilled(c,artR,WithA(COL_CARD2,255),48);
    if(g_mdArt){ ImVec2 uv0,uv1; CoverUV(g_mdArtW>0?g_mdArtW:1,g_mdArtH>0?g_mdArtH:1,artR*2,artR*2,uv0,uv1);
        dl->AddImageRounded((ImTextureID)g_mdArt,V(c.x-artR,c.y-artR),V(c.x+artR,c.y+artR),uv0,uv1,IM_COL32(255,255,255,255),artR); }
    else MsIcon(dl,"music_note",c,artR*0.8f,COL_INK2);
}
// wavy elapsed part + thumb + track (Caelestia's media slider)
static void V2MediaSlider(ImDrawList* dl,ImGuiIO& io,float x0,float x1,float y,int id){
    double dur=g_md.dur, pos=MediaPos();
    bool over=io.MousePos.x>x0-6&&io.MousePos.x<x1+6&&io.MousePos.y>y-12&&io.MousePos.y<y+12;
    static std::unordered_map<int,bool> drag;
    if(over&&io.MouseClicked[0]) drag[id]=true;
    if(drag[id] && dur>0){ g_mdSeekPreview=(float)std::clamp((io.MousePos.x-x0)/(x1-x0),0.0f,1.0f)*(float)dur;
        if(!io.MouseDown[0]){ g_reqSeek=(double)g_mdSeekPreview; g_mdSeekPreview=-1.0f; drag[id]=false; } }
    double sp = g_mdSeekPreview>=0&&drag[id]? g_mdSeekPreview : pos;
    float f=(float)(dur>0? std::clamp(sp/dur,0.0,1.0) : 0.0);
    float tx=x0+(x1-x0)*f;
    // wave up to the thumb
    if(tx-x0>3){
        float ph=(float)ImGui::GetTime()*(g_md.playing? 5.0f : 0.0f); int n=std::max(2,(int)((tx-x0)/2.0f));
        std::vector<ImVec2> pts; pts.reserve(n+1);
        for(int i=0;i<=n;i++){ float x=x0+(tx-6-x0)*i/(float)n; float amp=g_md.playing? 3.0f : 0.8f;
            pts.push_back(V(x,y+sinf((x-x0)*0.42f-ph)*amp)); }
        if(tx-6>x0) dl->AddPolyline(pts.data(),(int)pts.size(),COL_INK,0,3.0f);
    }
    dl->AddRectFilled(V(tx-2,y-11),V(tx+2,y+11),COL_INK,2);
    if(x1-(tx+6)>6) dl->AddRectFilled(V(tx+6,y-4),V(x1,y+4),V2Track(),4);
    dl->AddCircleFilled(V(x1-4,y),2.0f,COL_INK,10);
}
static void V2PlayerCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,bool card,int uid){
    if(card) Card(dl,o,s);
    bool has=g_md.has&&!g_md.title.empty();
    float pad=card? 14.0f : 4.0f;
    float visR=std::clamp(std::min(s.y*0.5f-pad,s.x*0.24f),30.0f,120.0f);
    ImVec2 vc=V(o.x+pad+visR,o.y+s.y*0.5f);
    V2VisRing(dl,vc,visR*0.62f,visR,COL_INK);
    float cx0=vc.x+visR+std::max(16.0f,s.x*0.035f), cx1=o.x+s.x-pad;
    float tf=std::clamp(s.y*0.11f,15.0f,24.0f);
    float y=o.y+s.y*0.5f-tf*4.2f;
    y=std::max(y,o.y+pad);
    std::string t1=has? Clip(g_fMed,tf,g_md.title,cx1-cx0) : std::string("Nothing playing");
    TextAt(dl,g_fMed,tf,V(cx0,y),COL_INK,t1.c_str()); y+=tf*1.35f;
    float sf=tf*0.72f;
    TextAt(dl,g_fSml,sf,V(cx0,y),COL_INK2,Clip(g_fSml,sf,has? g_md.artist : "",cx1-cx0).c_str()); y+=sf*1.45f;
    TextAt(dl,g_fSml,sf,V(cx0,y),COL_INK2,Clip(g_fSml,sf,has? g_md.album : "",cx1-cx0).c_str()); y+=sf*1.9f;
    // slider row
    float tsz=std::max(11.0f,sf*0.82f);
    std::string tl=V2Time(g_mdSeekPreview>=0? g_mdSeekPreview : MediaPos()), tr=V2Time(g_md.dur);
    TextAt(dl,g_fSml,tsz,V(cx0,y-tsz*0.55f),COL_INK,tl.c_str());
    float sx0=cx0+TextW(g_fSml,tsz,"00:00")+6, sx1=cx1-TextW(g_fSml,tsz,tr.c_str())-8;
    V2MediaSlider(dl,io,sx0,sx1,y,0x63000+uid);
    TextAt(dl,g_fSml,tsz,V(cx1-TextW(g_fSml,tsz,tr.c_str()),y-tsz*0.55f),COL_INK,tr.c_str());
    y+=tf*1.6f;
    // controls
    float bs=std::clamp(s.y*0.17f,26.0f,40.0f), gap=bs*0.16f;
    float total=bs*4+bs*1.25f+gap*4; float bx=cx0+std::max(0.0f,((cx1-cx0)-total)*0.5f);
    bool click=io.MouseClicked[0];
    auto at=[&](float w){ float c=bx+w*0.5f; bx+=w+gap; return c; };
    int shf=g_mdShuffle.load(), rep=g_mdRepeat.load();
    float c1=at(bs); if(V2Btn(dl,io,V(c1,y),bs,bs,shf==1? "shuffle_on" : "shuffle",shf==1,0x63100+uid*8,click)) g_reqShuffle=1;
    float c2=at(bs); if(V2Btn(dl,io,V(c2,y),bs,bs,"skip_previous",false,0x63101+uid*8,click)) g_reqPrev=1;
    float c3=at(bs*1.25f); if(V2Btn(dl,io,V(c3,y),bs*1.25f,bs*1.08f,g_md.playing? "pause" : "play_arrow",true,0x63102+uid*8,click,bs*0.32f)) g_reqPlay=1;
    float c4=at(bs); if(V2Btn(dl,io,V(c4,y),bs,bs,"skip_next",false,0x63103+uid*8,click)) g_reqNext=1;
    float c5=at(bs); if(V2Btn(dl,io,V(c5,y),bs,bs,rep==1? "repeat_one" : "repeat",rep>0,0x63104+uid*8,click)) g_reqRepeat=1;
}
static void V2LyricsCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,bool card,int uid){
    if(card) Card(dl,o,s);
    float pad=card? 14.0f : 8.0f, hf=std::clamp(s.y*0.085f,14.0f,18.0f);
    MsIcon(dl,"lyrics",V(o.x+pad+hf*0.55f,o.y+pad+hf*0.62f),hf*1.2f,COL_INK);
    TextAt(dl,g_fMed,hf,V(o.x+pad+hf*1.45f,o.y+pad),COL_INK,"Lyrics");
    // kebab menu: timing + source
    static std::unordered_map<int,bool> mOpen; static std::unordered_map<int,float> mAnim;
    ImVec2 kc=V(o.x+s.x-pad-13,o.y+pad+hf*0.6f);
    bool toggled=false;
    if(V2Btn(dl,io,kc,26,26,"more_vert",false,0x63200+uid*4,io.MouseClicked[0],8)){ mOpen[uid]=!mOpen[uid]; toggled=true; }
    // player split button
    float bh=std::clamp(s.y*0.13f,22.0f,28.0f);
    ImVec2 ba=V(o.x+pad,o.y+s.y-pad-bh); float bw=s.x-pad*2;
    std::vector<MediaSession> sess; { std::lock_guard<std::mutex> lk(g_mdSessMtx); sess=g_mdSessions; }
    std::string cur; { std::lock_guard<std::mutex> lk(g_mdPickMtx); cur=g_mdPick; }
    std::string src=g_mdSource.empty()? std::string("Player") : g_mdSource;
    static std::unordered_map<int,bool> pOpen; static std::unordered_map<int,float> pAnim;
    int hit=V2Split(dl,io,ba,bw,bh,"speaker",src,0x63210+uid*4,io.MouseClicked[0]);
    bool ptog=false; if(hit){ pOpen[uid]=!pOpen[uid]; ptog=true; }
    // the lines
    ImVec2 la=V(o.x+pad,o.y+pad+hf*1.9f), lb=V(o.x+s.x-pad,ba.y-6);
    ImU32 tint=COL_INK;
    DrawLyricsColumn(dl,la,lb,tint,io,io.MouseClicked[0] && !pOpen[uid] && !mOpen[uid]);
    // menus last, on top
    std::vector<std::string> items={"Follow current player"}; int sel=cur.empty()? 0 : -1;
    for(size_t i=0;i<sess.size();i++){ items.push_back(sess[i].name+(sess[i].playing? "  \xE2\x96\xB6" : "")); if(sess[i].id==cur) sel=(int)i+1; }
    int p=V2Menu(dl,io,pOpen[uid],pAnim[uid],ba,V(ba.x+bw,ba.y+bh),items,sel,true,ptog);
    if(p>=0){ std::lock_guard<std::mutex> lk(g_mdPickMtx); g_mdPick = p==0? std::string() : sess[p-1].id; }
    char ob[48]; snprintf(ob,48,"Timing  %+.2fs",LyrSongOffset());
    std::vector<std::string> mi={"Lyrics earlier  (-0.25s)","Lyrics later  (+0.25s)",ob[0]? std::string("Reset timing") : "",
                                 g_mediaLyrics? "Hide lyrics" : "Show lyrics"};
    ImVec2 ma=V(o.x+s.x-pad-190,kc.y+13), mb=V(o.x+s.x-pad,kc.y+13);
    int m=V2Menu(dl,io,mOpen[uid],mAnim[uid],V(ma.x,ma.y-26),mb,mi,-1,false,toggled);
    if(m==0) LyrSongOffsetAdd(-0.25f); else if(m==1) LyrSongOffsetAdd(+0.25f);
    else if(m==2) LyrSongOffsetAdd(-LyrSongOffset()); else if(m==3){ g_mediaLyrics=!g_mediaLyrics; SaveConfig(); }
}
static void DrawMediaV2(ImDrawList* dl,ImVec2 org,ImVec2 area,ImGuiIO& io){
    float lw=area.x*0.31f;
    V2PlayerCard(dl,io,org,V(area.x-lw-12,area.y),false,0);
    V2LyricsCard(dl,io,V(org.x+area.x-lw,org.y),V(lw,area.y),false,0);
}

// ============================================================================================ Dashboard cards
static void V2WeatherCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s){
    (void)io; Card(dl,o,s);
    if(!g_wx.ok){ TextAt(dl,g_fSml,14,V(o.x+16,o.y+s.y*0.5f-8),COL_INK2,"Weather\xE2\x80\xA6"); return; }
    float R=std::clamp(std::min(s.y*0.36f,s.x*0.18f),16.0f,48.0f);
    ImVec2 ic=V(o.x+std::max(14.0f,s.x*0.08f)+R,o.y+s.y*0.5f);
    if(!MsIcon(dl,WxMaterial(g_wx.code,!g_wx.isDay),ic,R*2.0f,COL_INK)) WxIcon(dl,ic,R*0.8f,g_wx.code);
    float tx=ic.x+R+std::max(10.0f,s.x*0.05f), tw=o.x+s.x-12-tx;
    char t[16]; snprintf(t,16,"%d\xC2\xB0""C",(int)std::round(g_wx.temp));
    float tf=std::clamp(s.y*0.30f,18.0f,38.0f);
    TextAt(dl,g_fMed,tf,V(tx,o.y+s.y*0.5f-tf*0.95f),COL_INK,t);
    float sf=std::clamp(tf*0.46f,11.0f,16.0f);
    TextAt(dl,g_fSml,sf,V(tx,o.y+s.y*0.5f+tf*0.18f),COL_INK,Clip(g_fSml,sf,WxText(g_wx.code),tw).c_str());
}
// white silhouette of a logo, so it can be tinted like Caelestia's ColouredIcon
static bool V2LogoMask(ImDrawList* dl,const std::string& file,ImVec2 c,float px,ImU32 col){
    static std::unordered_map<std::string,std::tuple<ID3D11ShaderResourceView*,int,int>> cache;
    int phys=std::clamp((int)std::lround(px*g_uiScale),8,256);
    std::string key=file+"|"+std::to_string(phys);
    auto it=cache.find(key);
    if(it==cache.end()){
        std::vector<uint8_t> p; int w=0,h=0; ID3D11ShaderResourceView* t=nullptr;
        if(DecodeFirstFrame(U82W(file),phys,p,w,h)&&w>0&&h>0){
            for(int i=0;i<w*h;i++){ uint8_t a=p[i*4+3]; p[i*4]=a; p[i*4+1]=a; p[i*4+2]=a; }   // premultiplied white
            t=MakeTextureBGRA(p.data(),w,h); }
        it=cache.emplace(key,std::make_tuple(t,w,h)).first;
    }
    auto [t,w,h]=it->second; if(!t) return false;
    float sc=px/(float)std::max(w,h); float dw=w*sc, dh=h*sc;
    dl->AddImage((ImTextureID)t,V(c.x-dw*0.5f,c.y-dh*0.5f),V(c.x+dw*0.5f,c.y+dh*0.5f),ImVec2(0,0),ImVec2(1,1),col);
    return true;
}
static void V2UserCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,int uid){
    Card(dl,o,s);
    ProfileExpire();
    const bool live=g_wInteractive; const bool click=live&&io.MouseClicked[0];
    float pad=std::clamp(s.y*0.10f,8.0f,16.0f);
    float R=std::clamp((s.y-pad*2)*0.5f,18.0f,70.0f);
    float gemR=std::clamp(R*0.34f,10.0f,24.0f);
    ImVec2 gc=V(o.x+pad+gemR,o.y+pad+gemR);
    ImVec2 ac=V(gc.x+gemR*0.55f+R,o.y+s.y*0.5f);
    // avatar
    bool avHov=live && (io.MousePos.x-ac.x)*(io.MousePos.x-ac.x)+(io.MousePos.y-ac.y)*(io.MousePos.y-ac.y)<R*R;
    float ah=HoverAnim(0x64000+uid,avHov);
    ProfileAvatar(dl,ac,R,0.0f);
    if(ah>0.01f){ dl->AddCircleFilled(ac,R,IM_COL32(0,0,0,(int)(90*ah)),48);
        M3Shape(dl,ac,R*0.42f*(0.7f+0.3f*ah),WithA(COL_GOLD,(int)(255*ah)),M3_DIAMOND,0.0f);
        MsIcon(dl,"person_edit",ac,R*0.36f,WithA(M3OnPrimary(),(int)(255*ah))); }
    if(avHov&&click) ProfileEditorOpen(uid);
    // presence dot
    int pres=ProfilePresenceNow();
    if(pres!=PRES_NONE) ProfilePresenceDot(dl,V(ac.x+R*0.70f,ac.y-R*0.70f),std::max(4.0f,R*0.12f),pres,COL_CARD);
    // gem with the OS logo
    M3Shape(dl,gc,gemR,V2Cont(COL_GOLD),M3_GEM,0.0f);
    { std::string lg=g_v2Logo; std::string l=lg; for(auto& ch:l) ch=(char)tolower((unsigned char)ch);
      ImU32 on=Mix(COL_INK,COL_GOLD,0.2f); bool ok=false;
      if(l=="os"||l=="windows") ok=V2LogoMask(dl,ExeDir()+"assets\\logos\\windows-11.png",gc,gemR*1.05f,on);
      else if(l.rfind("logo:",0)==0){ for(int i=0;i<NBARLOGOS;i++) if(l.substr(5)==BAR_LOGOS[i].key&&BAR_LOGOS[i].file){ ok=V2LogoMask(dl,ExeDir()+"assets\\logos\\"+BAR_LOGOS[i].file,gc,gemR*1.05f,on); break; } }
      if(!ok) ProfileIcon(dl,lg,gc,gemR*1.1f,on); }
    // speech bubble (WM or your status) + trailing dots
    CwCtx cx; cx.o=o; cx.s=s;
    std::string bub=CwText(g_v2Bubble,cx);
    if(bub.empty() && !g_status.empty()) bub=g_status;
    float rx=ac.x+R+6, rw=o.x+s.x-pad-rx;
    float bf=std::clamp(s.y*0.13f,11.0f,15.0f), bh=bf*2.0f;
    if(rw>40 && !bub.empty()){
        std::string shown=Clip(g_fSml,bf,bub,rw-bf*2.6f);
        float bw=std::min(rw,TextW(g_fSml,bf,shown.c_str())+bf*2.8f);
        ImVec2 ba=V(o.x+s.x-pad-bw,o.y+pad), bb=V(ba.x+bw,ba.y+bh);
        ImU32 sc=V2Cont(M3Secondary());
        dl->AddRectFilled(ba,bb,sc,bh*0.5f);
        dl->AddCircleFilled(V(ba.x+bh*0.25f,bb.y+bh*0.18f),bh*0.20f,sc,16);
        dl->AddCircleFilled(V(ba.x-bh*0.08f,bb.y+bh*0.50f),bh*0.13f,sc,12);
        MsIcon(dl,g_status.empty()||bub!=g_status? "select_window" : (g_statusIcon.empty()? "mood" : g_statusIcon),V(ba.x+bf*1.05f,ba.y+bh*0.5f),bf*1.15f,COL_INK);
        TextAt(dl,g_fSml,bf,V(ba.x+bf*1.9f,ba.y+(bh-bf)*0.5f-1),COL_INK,shown.c_str());
        bool bh2=live&&io.MousePos.x>=ba.x&&io.MousePos.x<bb.x&&io.MousePos.y>=ba.y&&io.MousePos.y<bb.y;
        if(bh2&&click) ProfileEditorOpen(uid);
    }
    // uptime clamshell
    float cR=std::clamp(s.y*0.16f,10.0f,20.0f);
    ImVec2 cc=V(ac.x+R*0.62f,o.y+s.y-pad-cR*0.9f);
    M3Shape(dl,cc,cR,V2Cont(M3Tertiary()),M3_CLAMSHELL,0.0f);
    MsIcon(dl,"clock_arrow_up",cc,cR*1.05f,COL_INK);
    float uf=std::clamp(s.y*0.13f,11.0f,15.0f);
    std::string up=ProfileUptimeLong();
    float ux=cc.x+cR+6;
    TextAt(dl,g_fSml,uf,V(ux,cc.y-uf*0.6f),COL_INK,Clip(g_fSml,uf,up,o.x+s.x-pad-ux).c_str());
}
static void V2ClockCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s){
    (void)io; Card(dl,o,s);
    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
    int h=g_clock24? lt.tm_hour : ((lt.tm_hour%12)==0? 12 : lt.tm_hour%12);
    char hh[8],mm[8]; snprintf(hh,8,"%02d",h); snprintf(mm,8,"%02d",lt.tm_min);
    float f=std::clamp(std::min(s.x*0.46f,s.y*0.16f),16.0f,56.0f);
    float cx=o.x+s.x*0.5f, total=f*(g_clock24? 2.6f : 3.5f), y=o.y+(s.y-total)*0.5f;
    ImU32 col=WithA(COL_INK,215);
    TextAt(dl,g_fMed,f,V(cx-TextW(g_fMed,f,hh)*0.5f,y),col,hh); y+=f*1.08f;
    for(int i=0;i<3;i++) dl->AddCircleFilled(V(cx+(i-1)*f*0.24f,y+f*0.12f),f*0.075f,col,10);
    y+=f*0.36f;
    TextAt(dl,g_fMed,f,V(cx-TextW(g_fMed,f,mm)*0.5f,y),col,mm); y+=f*1.12f;
    if(!g_clock24){ const char* ap=lt.tm_hour<12? "AM" : "PM"; float af=f*0.62f;
        TextAt(dl,g_fMed,af,V(cx-TextW(g_fMed,af,ap)*0.5f,y),col,ap); }
}
static void V2CalendarCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,int uid){
    Card(dl,o,s);
    static std::unordered_map<int,int> offs; int& off=offs[uid];
    time_t nn=time(nullptr); struct tm now; localtime_s(&now,&nn);
    int y=now.tm_year+1900, m=now.tm_mon+off; while(m<0){ m+=12; y--; } while(m>11){ m-=12; y++; }
    float pad=std::clamp(s.y*0.05f,8.0f,14.0f);
    float hf=std::clamp(s.y*0.075f,12.0f,17.0f);
    static const char* MN[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
    char ttl[40]; snprintf(ttl,40,"%s %d",MN[m],y);
    float cxm=o.x+s.x*0.5f, hy=o.y+pad+hf*0.2f;
    TextAt(dl,g_fMed,hf,V(cxm-TextW(g_fMed,hf,ttl)*0.5f,hy),COL_INK,ttl);
    bool click=io.MouseClicked[0];
    auto arrow=[&](float x,const char* ic,int d){ ImVec2 c=V(x,hy+hf*0.55f); bool h=fabsf(io.MousePos.x-c.x)<12&&fabsf(io.MousePos.y-c.y)<12;
        float ha=HoverAnim(0x65000+uid*4+d+1,h); if(ha>0.01f) dl->AddCircleFilled(c,12,WithA(COL_INK2,(int)(40*ha)),20);
        MsIcon(dl,ic,c,hf*1.1f,COL_INK); if(h&&click) off+=d; };
    arrow(o.x+s.x*0.14f,"chevron_left",-1); arrow(o.x+s.x*0.86f,"chevron_right",+1);
    static const char* WD[]={"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
    float gx0=o.x+pad, gw=s.x-pad*2, cw=gw/7.0f;
    float gy=hy+hf*1.9f, rows=6.0f, ch=(o.y+s.y-pad-gy-hf*1.4f)/rows;
    float df=std::clamp(std::min(cw*0.36f,ch*0.62f),9.0f,16.0f);
    for(int i=0;i<7;i++){ float x=gx0+cw*(i+0.5f); TextAt(dl,g_fMed,df,V(x-TextW(g_fMed,df,WD[i])*0.5f,gy),COL_INK,WD[i]); }
    gy+=df*1.6f;
    struct tm first{}; first.tm_year=y-1900; first.tm_mon=m; first.tm_mday=1; first.tm_hour=12; mktime(&first);
    int lead=(first.tm_wday+6)%7;
    int dim=DaysInMonth(y,m), pm=(m+11)%12, py=m==0? y-1 : y, dimPrev=DaysInMonth(py,pm);
    for(int cell=0;cell<42;cell++){
        int r=cell/7, c=cell%7; int day; bool inM=true;
        if(cell<lead){ day=dimPrev-lead+cell+1; inM=false; }
        else if(cell-lead>=dim){ day=cell-lead-dim+1; inM=false; }
        else day=cell-lead+1;
        ImVec2 cc=V(gx0+cw*(c+0.5f),gy+ch*(r+0.5f));
        char d[4]; snprintf(d,4,"%d",day);
        bool today=inM && day==now.tm_mday && m==now.tm_mon && y==now.tm_year+1900;
        ImU32 col = !inM? WithA(COL_INK2,110) : (c>=5? COL_INK : WithA(COL_INK,200));
        if(today){ float rr=std::min(cw,ch)*0.50f; M3Shape(dl,cc,rr,Mix(COL_INK,COL_GOLD,0.2f),[]{ int k=M3ShapeFromName(g_v2TodayShape); return k<0? (int)M3_COOKIE9 : k; }(),(float)ImGui::GetTime()*0.15f); col=M3OnPrimary(); }
        TextAt(dl,g_fSml,df,V(cc.x-TextW(g_fSml,df,d)*0.5f,cc.y-df*0.55f),col,d);
    }
    float wh=io.MouseWheel; if(wh!=0 && io.MousePos.x>=o.x&&io.MousePos.x<o.x+s.x&&io.MousePos.y>=o.y&&io.MousePos.y<o.y+s.y) off += wh>0? -1 : 1;
}
static void V2RingsCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s){
    (void)io; Card(dl,o,s);
    float fr[3]={ (float)g_st.cpuUsage, g_st.memTotal? (float)((double)g_st.memUsed/g_st.memTotal) : 0.0f,
                  g_st.diskTotal? (float)((double)g_st.diskUsed/g_st.diskTotal) : 0.0f };
    static const char* IC[3]={"memory","memory_alt","hard_drive_2"};
    bool vert = s.y>=s.x;
    for(int i=0;i<3;i++){
        float R = vert? std::min(s.x*0.36f,s.y/3.0f*0.36f) : std::min(s.y*0.36f,s.x/3.0f*0.36f);
        ImVec2 c = vert? V(o.x+s.x*0.5f,o.y+s.y*((i+0.5f)/3.0f)) : V(o.x+s.x*((i+0.5f)/3.0f),o.y+s.y*0.5f);
        float a=Cael::anim(0x66000+i,fr[i],Cael::DUR_DEFAULT_SPATIAL*2,Cael::DEFAULT_SPATIAL);
        V2Ring(dl,c,R,std::max(3.0f,R*0.085f),a,COL_INK,V2Track());
        MsIcon(dl,IC[i],c,R*0.72f,COL_INK);
    }
}
static void V2MediaCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,int uid){
    Card(dl,o,s);
    bool has=g_md.has&&!g_md.title.empty();
    float pad=std::clamp(s.x*0.08f,8.0f,14.0f);
    float R=std::clamp(std::min(s.x*0.36f,s.y*0.17f),20.0f,70.0f);
    ImVec2 c=V(o.x+s.x*0.5f,o.y+pad+R*1.12f);
    // art under an arc gauge
    dl->AddCircleFilled(c,R*0.84f,WithA(COL_CARD2,255),48);
    if(g_mdArt&&has){ float ar=R*0.84f; ImVec2 uv0,uv1; CoverUV(g_mdArtW>0?g_mdArtW:1,g_mdArtH>0?g_mdArtH:1,ar*2,ar*2,uv0,uv1);
        dl->AddImageRounded((ImTextureID)g_mdArt,V(c.x-ar,c.y-ar),V(c.x+ar,c.y+ar),uv0,uv1,IM_COL32(255,255,255,200),ar); }
    float f=(float)(g_md.dur>0? std::clamp(MediaPos()/g_md.dur,0.0,1.0) : 0.0);
    const float a0=2.45f, sw=4.51f;       // an open arc across the top
    V2Arc(dl,c,R,a0,a0+sw,V2Track(),3.0f);
    if(f>0.003f) V2Arc(dl,c,R,a0,a0+sw*f,COL_INK,4.0f);
    float y=c.y+R+std::max(8.0f,s.y*0.03f);
    float tf=std::clamp(s.x*0.10f,12.0f,18.0f), w=s.x-pad*2;
    auto centred=[&](ImFont* fn,float sz,const std::string& t,ImU32 col){ std::string cl=Clip(fn,sz,t,w); TextAt(dl,fn,sz,V(c.x-TextW(fn,sz,cl.c_str())*0.5f,y),col,cl.c_str()); y+=sz*1.35f; };
    centred(g_fMed,tf,has? g_md.title : "Nothing playing",COL_INK);
    centred(g_fSml,tf*0.8f,has? g_md.album : "",COL_INK2);
    centred(g_fSml,tf*0.8f,has? g_md.artist : "",COL_INK2);
    y+=tf*0.4f;
    float bs=std::clamp(s.x*0.20f,20.0f,36.0f), gap=bs*0.12f;
    bool click=io.MouseClicked[0];
    float cy=y+bs*0.5f;
    if(V2Btn(dl,io,V(c.x-bs*1.25f-gap,cy),bs,bs,"skip_previous",false,0x67000+uid*4,click)) g_reqPrev=1;
    if(V2Btn(dl,io,V(c.x,cy),bs*1.25f,bs*1.1f,g_md.playing? "pause" : "play_arrow",true,0x67001+uid*4,click,bs*0.3f)) g_reqPlay=1;
    if(V2Btn(dl,io,V(c.x+bs*1.25f+gap,cy),bs,bs,"skip_next",false,0x67002+uid*4,click)) g_reqNext=1;
    y=cy+bs*0.5f+6;
    float left=o.y+s.y-pad-y;
    if(left>30){ ImVec2 mc=V(c.x,y+left*0.5f);
        if(!DrawMediaImage(dl,mc,w,left,g_md.playing)) V2Puff(dl,V(mc.x,mc.y+left*0.05f),std::min(w*0.40f,left*0.60f),g_md.playing); }
}
static void DrawDashboardV2(ImDrawList* dl,ImVec2 org,ImVec2 area){
    ImGuiIO& io=ImGui::GetIO(); const float g=10;
    float mediaW=area.x*0.215f, leftW=area.x-mediaW-g;
    float r1=(area.y-g)*0.30f, r2=area.y-g-r1;
    float wW=leftW*0.43f;
    V2WeatherCard(dl,io,org,V(wW,r1));
    V2UserCard(dl,io,V(org.x+wW+g,org.y),V(leftW-wW-g,r1),0x51F1);
    float y2=org.y+r1+g, clockW=leftW*0.155f, ringsW=leftW*0.20f, calW=leftW-clockW-ringsW-2*g;
    V2ClockCard(dl,io,V(org.x,y2),V(clockW,r2));
    V2CalendarCard(dl,io,V(org.x+clockW+g,y2),V(calW,r2),0);
    V2RingsCard(dl,io,V(org.x+clockW+calW+2*g,y2),V(ringsW,r2));
    V2MediaCard(dl,io,V(org.x+leftW+g,org.y),V(mediaW,area.y),0);
}
