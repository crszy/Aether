// Strive.h - Guilty Gear Strive-style motion extras for Aether. Every piece is OPT-IN (strive.* settings,
// Settings > Effects > Guilty Gear Strive); nothing here changes the shell until it is switched on.
//
//   * Impact frames  - when a panel opens: a one-to-two frame flash, a slash that tears across the screen
//                      and a short, decaying shake. Hooked into RenderL, so every panel gets it without its
//                      own wiring: the first frame a layer renders after being hidden IS its opening.
//   * Workspace banner - a huge slanted call ("WORKSPACE 2") that slams onto a black plate, holds on a
//                      hit-stop, then is sliced in half and thrown off - the round call of a fighting game.
//   * Intro          - the fight opening: letterbox bars close in, each beat of text slams in, holds and is
//                      cut away, and the last one hits hardest. On startup and/or after unlocking.
//
// The banner and the intro draw on the DIM layer: topmost, click-through (WS_EX_TRANSPARENT), the size of
// the active monitor - so they can cover the screen and still never take a click. Their clocks are stepped
// at motion.strive_fps, the way Arc System Works animates on twos/threes so motion reads as drawn.
#pragma once

static const ImU32 STV_RED  = IM_COL32(226,36,52,255);   // Strive's red
static const ImU32 STV_BLACK= IM_COL32(8,8,10,255);

// stepped clock: t (ms) snapped down to the strive frame rate
static float StvStep(float t){
    if(Cael::g_striveFps<=0) return t;
    float step=1000.0f/(float)Cael::g_striveFps; return floorf(t/step)*step;
}
static inline float StvClamp01(float x){ return x<0?0:(x>1?1:x); }
// -s strive_intro_solid / strive_banner_solid=<n>: play over pure black instead of the desktop - for recording
// demos and screenshots without whatever happens to be on screen. Cleared when the preview ends.
static bool g_stvSolid=false;
static ULONGLONG g_stvIntroAt=0;          // 0 = not playing
static inline float StvOutCubic(float x){ x=StvClamp01(x); float m=1-x; return 1-m*m*m; }
static inline ImU32 StvA(ImU32 c,float a){ int al=(int)(((c>>24)&0xFF)*StvClamp01(a)); return (c&0x00FFFFFFu)|((ImU32)al<<24); }

// Text centred on c, scaled about its centre and sheared (x += skew*dy) - the italic slam of the round calls.
static void StvText(ImDrawList* dl,ImFont* f,float size,ImVec2 c,ImU32 col,const char* t,float sc,float skew){
    if(!f || !t || !*t) return;
    ImVec2 ts=f->CalcTextSizeA(size,FLT_MAX,0,t);
    int v0=dl->VtxBuffer.Size;
    dl->AddText(f,size,ImVec2(c.x-ts.x*0.5f,c.y-ts.y*0.5f),col,t);
    for(int i=v0;i<dl->VtxBuffer.Size;i++){ ImVec2& p=dl->VtxBuffer[i].pos;
        float dx=(p.x-c.x)*sc, dy=(p.y-c.y)*sc; p=ImVec2(c.x+dx-skew*dy, c.y+dy); }
}
// A slanted plate across the whole width, from y0 to y1.
static void StvPlate(ImDrawList* dl,float W,float y0,float y1,float skew,ImU32 col,float x0=-200.0f,float x1=-1.0f){
    if(x1<0) x1=W+200.0f; float cy=(y0+y1)*0.5f;
    dl->AddQuadFilled(ImVec2(x0-skew*(y0-cy),y0),ImVec2(x1-skew*(y0-cy),y0),ImVec2(x1-skew*(y1-cy),y1),ImVec2(x0-skew*(y1-cy),y1),col);
}
// A slanted call that is SLICED: the top half and the bottom half are drawn clipped and pushed apart.
static void StvSlicedText(ImDrawList* dl,ImFont* f,float size,ImVec2 c,ImU32 col,const char* t,float sc,float skew,
                          float split,float W,float H){
    if(split<=0.001f){ StvText(dl,f,size,c,col,t,sc,skew); return; }
    dl->PushClipRect(ImVec2(0,0),ImVec2(W,c.y),false);
    StvText(dl,f,size,ImVec2(c.x-split,c.y-split*0.18f),col,t,sc,skew); dl->PopClipRect();
    dl->PushClipRect(ImVec2(0,c.y),ImVec2(W,H),false);
    StvText(dl,f,size,ImVec2(c.x+split,c.y+split*0.18f),col,t,sc,skew); dl->PopClipRect();
}
static ImFont* StvFont(){ return g_fClock? g_fClock : (g_fHuge? g_fHuge : g_fMed); }

// =====================================================================================================
// Impact frames
// =====================================================================================================
static std::unordered_map<IDXGISwapChain1*,ULONGLONG> g_stvLastRender;   // when each layer last drew
static std::unordered_map<IDXGISwapChain1*,ULONGLONG> g_stvImpactAt;     // an impact playing on it
static float g_stvShakeX=0, g_stvShakeY=0;                               // this layer's shake, physical px
static bool StvImpactLayer(IDXGISwapChain1* sc){
    return sc && (sc==g_launSc||sc==g_sc||sc==g_ovSc||sc==g_lockSc||sc==g_sessSc||sc==g_setSc||sc==g_sideSc||sc==g_swSc);
}
static const float STV_IMPACT_MS=260.0f;
// Called by RenderL BEFORE ImGui::Render(): notices an opening, draws the flash and the slash into the
// layer's foreground list, and leaves the shake for StvImpactShake.
static void StvImpactPre(IDXGISwapChain1* sc){
    g_stvShakeX=g_stvShakeY=0;
    if(!sc) return;
    const ULONGLONG now=GetTickCount64();
    ULONGLONG& last=g_stvLastRender[sc];
    const bool opening = last==0 || now-last>250;
    last=now;
    if(!g_stvImpact || !StvImpactLayer(sc)) return;
    if(opening) g_stvImpactAt[sc]=now;
    auto it=g_stvImpactAt.find(sc); if(it==g_stvImpactAt.end()) return;
    const float t=(float)(now-it->second);
    if(t>STV_IMPACT_MS){ g_stvImpactAt.erase(it); return; }
    const float s=g_stvImpactStrength;
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetForegroundDrawList();
    const float W=io.DisplaySize.x, H=io.DisplaySize.y;
    // flash: one white frame, one red frame
    if(t<34.0f)      dl->AddRectFilled(ImVec2(0,0),ImVec2(W,H),StvA(IM_COL32(255,255,255,255),0.26f*s));
    else if(t<70.0f) dl->AddRectFilled(ImVec2(0,0),ImVec2(W,H),StvA(STV_RED,0.12f*s));
    // slash: a bright blade with a red edge tearing from lower left to upper right
    if(t<200.0f){
        float k=StvOutCubic(StvStep(t)/200.0f), fade=1.0f-StvClamp01((t-120.0f)/80.0f);
        float cx=-0.25f*W+1.5f*W*k, cy=H*0.5f, len=W*0.55f, ang=-0.36f;
        ImVec2 d(cosf(ang),sinf(ang)), n(-d.y,d.x);
        auto blade=[&](float half,ImU32 col){
            ImVec2 a(cx-d.x*len,cy-d.y*len), b(cx+d.x*len,cy+d.y*len);
            dl->AddQuadFilled(ImVec2(a.x+n.x*half*0.2f,a.y+n.y*half*0.2f),ImVec2(b.x+n.x*half,b.y+n.y*half),
                              ImVec2(b.x-n.x*half,b.y-n.y*half),ImVec2(a.x-n.x*half*0.2f,a.y-n.y*half*0.2f),col); };
        blade(26.0f*s,StvA(STV_RED,0.55f*fade));
        blade(9.0f*s, StvA(IM_COL32(255,255,255,255),0.95f*fade));
    }
    // shake: a new direction every strive frame, dying off
    if(t<220.0f){
        float amp=10.0f*s*(1.0f-t/220.0f)*(1.0f-t/220.0f);
        unsigned f=(unsigned)(StvStep(t)/16.0f)+(unsigned)(uintptr_t)sc;
        float a=(float)((f*2654435761u)>>8 & 1023)/1023.0f*6.2831853f;
        g_stvShakeX=cosf(a)*amp; g_stvShakeY=sinf(a)*amp*0.6f;
    }
}
// Called by RenderL after the draw data is scaled to physical pixels: moves the whole layer by the shake.
static void StvImpactShake(ImDrawData* dd){
    if(!dd || (g_stvShakeX==0 && g_stvShakeY==0)) return;
    for(int n=0;n<dd->CmdListsCount;n++){ ImDrawList* l=dd->CmdLists[n];
        for(int i=0;i<l->VtxBuffer.Size;i++){ l->VtxBuffer[i].pos.x+=g_stvShakeX; l->VtxBuffer[i].pos.y+=g_stvShakeY; } }
}

// =====================================================================================================
// Workspace banner
// =====================================================================================================
static ULONGLONG   g_stvBanAt=0;        // 0 = no banner
static std::string g_stvBanText;
static const float STV_BAN_MS=1150.0f;
static void StvBannerFire(int number,const char* name){
    char buf[96]; std::string fmt=g_stvBannerText.empty()? std::string("WORKSPACE %d") : g_stvBannerText;
    // %d and %s are the only placeholders; anything else in the text is literal
    std::string out;
    for(size_t i=0;i<fmt.size();i++){
        if(fmt[i]=='%' && i+1<fmt.size() && fmt[i+1]=='d'){ snprintf(buf,sizeof(buf),"%d",number); out+=buf; i++; }
        else if(fmt[i]=='%' && i+1<fmt.size() && fmt[i+1]=='s'){ out+=(name&&*name)? name : std::to_string(number); i++; }
        else out+=fmt[i]; }
    for(auto& ch:out) ch=(char)toupper((unsigned char)ch);
    const ULONGLONG now=GetTickCount64();
    // Rapid switching: a banner that is still up is re-called at its slam, not replayed from nothing -
    // the same "speed up, never drop" rule as every other animation.
    if(g_stvBanAt && (float)(now-g_stvBanAt)<STV_BAN_MS-150.0f) g_stvBanAt=now-60;
    else g_stvBanAt=now;
    g_stvBanText=out;
}
static void StvDrawBanner(ImDrawList* dl,float W,float H){
    if(!g_stvBanAt) return;
    const float raw=(float)(GetTickCount64()-g_stvBanAt);
    if(raw>STV_BAN_MS){ g_stvBanAt=0; if(!g_stvIntroAt) g_stvSolid=false; return; }
    const float t=StvStep(raw);
    if(g_stvSolid) dl->AddRectFilled(ImVec2(0,0),ImVec2(W,H),STV_BLACK);
    const float cy=H*0.5f, ph=H*0.19f, skew=0.22f;
    // plate: grows open fast, holds, then closes as the call is sliced away
    float open = StvOutCubic(t/130.0f);
    float close= StvClamp01((t-820.0f)/260.0f);
    float h=ph*open*(1.0f-StvOutCubic(close));
    float px=-W*(1.0f-open);                                   // the plate sweeps in from the left
    if(h>1.0f){
        StvPlate(dl,W,cy-h*0.5f,cy+h*0.5f,skew,StvA(STV_BLACK,0.94f),px-200.0f,W+200.0f);
        StvPlate(dl,W,cy-h*0.5f-6,cy-h*0.5f,skew,StvA(STV_RED,0.95f),px-200.0f,W+200.0f);
        StvPlate(dl,W,cy+h*0.5f,cy+h*0.5f+6,skew,StvA(STV_RED,0.95f),px-200.0f,W+200.0f);
        // speed lines streaking along the plate while it holds
        if(t>150.0f && t<850.0f){
            for(int i=0;i<9;i++){ float r=(float)((i*2654435761u)>>9 & 1023)/1023.0f;
                float ly=cy-h*0.42f+h*0.84f*r, sp=0.9f+1.4f*r, lx=fmodf(t*sp*1.6f+r*W,W+400.0f)-200.0f;
                dl->AddLine(ImVec2(lx-skew*(ly-cy),ly),ImVec2(lx+140.0f+220.0f*r-skew*(ly-cy),ly),StvA(IM_COL32(255,255,255,255),0.10f+0.12f*r),1.5f); } }
    }
    // the call: slams from huge, freezes on the hit, drifts, is cut in half and thrown off
    const char* txt=g_stvBanText.c_str();
    float slam=StvClamp01((t-50.0f)/120.0f);
    float sc=1.0f+1.5f*(1.0f-StvOutCubic(slam));
    float alpha=StvClamp01(slam*3.0f)*(1.0f-close);
    float drift=StvClamp01((t-240.0f)/600.0f)*22.0f;
    float split=StvOutCubic(close)*W*0.35f;
    ImVec2 c(W*0.5f+drift-(1.0f-StvOutCubic(slam))*90.0f, cy);
    float fs=h>1.0f? std::min(ph*0.86f,220.0f) : 170.0f;
    if(alpha>0.01f){
        StvSlicedText(dl,StvFont(),fs,ImVec2(c.x+7,c.y+6),StvA(STV_RED,alpha*0.9f),txt,sc,skew,split,W,H);   // red echo
        StvSlicedText(dl,StvFont(),fs,c,StvA(IM_COL32(248,246,244,255),alpha),txt,sc,skew,split,W,H);
    }
    // the hit: the frame the call lands, the plate flashes white
    if(t>=170.0f && t<240.0f && h>1.0f) StvPlate(dl,W,cy-h*0.5f,cy+h*0.5f,skew,StvA(IM_COL32(255,255,255,255),0.35f*(1.0f-(t-170.0f)/70.0f)));
}
// Watches the active monitor's workspace and fires the banner when it changes.
static void StvBannerTick(){
    static int lastMon=-1, lastWs=-1;
    int mi=std::clamp(g_actMon,0,15); const WsRing& r=g_wsRing[mi];
    int cur=r.cur;
    if(mi!=lastMon){ lastMon=mi; lastWs=cur; return; }        // moving to another screen is not a switch
    if(cur==lastWs) return;
    lastWs=cur;
    if(!g_stvBanner) return;
    const char* nm=(cur>=0 && cur<64)? r.name[cur] : "";
    StvBannerFire(cur+1,nm);
}

// =====================================================================================================
// Intro - the fight opening
// =====================================================================================================
static ULONGLONG g_stvIntroSkipAt=0;      // a key/click asked it to go
static std::vector<std::string> g_stvBeats;
static int g_stvIntroCount=0;
static const float STV_IN_MS=420.0f, STV_BEAT_MS=1000.0f, STV_LAST_MS=1450.0f, STV_OUT_MS=380.0f;
static float StvIntroLength(){ int n=(int)g_stvBeats.size(); return n<=0? 0.0f : STV_IN_MS+(n-1)*STV_BEAT_MS+STV_LAST_MS+STV_OUT_MS; }
static void StvIntroStart(){
    g_stvIntroCount++;
    g_stvBeats.clear();
    std::string src=g_stvIntroLines.empty()? std::string("HEAVEN OR HELL|DUEL %n|LET'S ROCK") : g_stvIntroLines, cur;
    auto push=[&](){
        std::string o; for(size_t i=0;i<cur.size();i++){
            if(cur[i]=='%' && i+1<cur.size() && cur[i+1]=='n'){ o+=std::to_string(g_stvIntroCount); i++; }
            else if(cur[i]=='%' && i+1<cur.size() && cur[i+1]=='u'){ o+=ProfileDisplayName(); i++; }
            else o+=cur[i]; }
        size_t b=o.find_first_not_of(' '), e=o.find_last_not_of(' ');
        if(b!=std::string::npos){ o=o.substr(b,e-b+1); for(auto& ch:o) ch=(char)toupper((unsigned char)ch);
                                  if(g_stvBeats.size()<6) g_stvBeats.push_back(o); }
        cur.clear(); };
    for(char ch:src){ if(ch=='|') push(); else cur+=ch; }
    push();
    if(g_stvBeats.empty()) return;
    g_stvIntroAt=GetTickCount64(); g_stvIntroSkipAt=0;
}
static void StvIntroSkip(){ if(g_stvIntroAt && !g_stvIntroSkipAt) g_stvIntroSkipAt=GetTickCount64(); }
// ---- the intro's building blocks -------------------------------------------------------------------
static const ImU32 STV_CYAN=IM_COL32(40,232,255,255);
static inline float StvHash(unsigned x){ x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; x^=x>>16; return (x&0xFFFFFF)/16777215.0f; }
// One character, drawn centred on c, then scaled, sheared and rotated about its own centre.
static void StvChar(ImDrawList* dl,ImFont* f,float size,ImVec2 c,ImU32 col,char ch,float sc,float rot,float skew){
    char t[2]={ch,0}; ImVec2 ts=f->CalcTextSizeA(size,FLT_MAX,0,t);
    int v0=dl->VtxBuffer.Size;
    dl->AddText(f,size,ImVec2(c.x-ts.x*0.5f,c.y-ts.y*0.5f),col,t);
    const float cs=cosf(rot), sn=sinf(rot);
    for(int i=v0;i<dl->VtxBuffer.Size;i++){ ImVec2& p=dl->VtxBuffer[i].pos;
        float x=(p.x-c.x)*sc, y=(p.y-c.y)*sc; x-=skew*y;
        p=ImVec2(c.x+x*cs-y*sn, c.y+x*sn+y*cs); }
}
// A shape's radius at angle a (unit-ish), for the emblem that morphs from one to the next every beat.
static float StvShape(int k,float a){
    const float PI=3.14159265f;
    switch(((k%4)+4)%4){
    case 0: return 1.0f;                                                            // circle
    case 1: { float m=fmodf(a+PI*2.0f,PI/3.0f)-PI/6.0f; return cosf(PI/6.0f)/cosf(m); }   // hexagon
    case 2: return 1.18f/(fabsf(cosf(a))+fabsf(sinf(a)));                            // diamond
    default:{ float c4=fabsf(cosf(a*4.0f)); return 0.58f+0.52f*powf(c4,3.0f); }      // eight-point burst
    }
}
static void StvEmblem(ImDrawList* dl,ImVec2 c,float R,int fromShape,int toShape,float m,float rot,ImU32 col,float thick){
    const int N=120; dl->PathClear();
    for(int i=0;i<=N;i++){ float a=i*6.2831853f/N;
        float r=R*(StvShape(fromShape,a)*(1.0f-m)+StvShape(toShape,a)*m);
        dl->PathLineTo(ImVec2(c.x+cosf(a+rot)*r, c.y+sinf(a+rot)*r)); }
    dl->PathStroke(col,ImDrawFlags_Closed,thick);
}
// A word whose letters converge from scattered, spinning positions, sit with a red/cyan split, and shatter.
//   bt   = ms into this beat,  len = the beat's length,  hitAt = when the last letter has landed
static void StvWord(ImDrawList* dl,ImFont* f,float size,ImVec2 c,const char* txt,float bt,float len,unsigned seed,
                    float hitDecay,bool finale,float W,float H){
    const int n=(int)strlen(txt); if(n<=0) return;
    std::vector<float> xs(n), ws(n); float total=0;
    for(int i=0;i<n;i++){ char t[2]={txt[i],0}; ws[i]=f->CalcTextSizeA(size,FLT_MAX,0,t).x; xs[i]=total; total+=ws[i]; }
    const float skew=0.20f, stagger=finale? 55.0f : 20.0f, enter=finale? 120.0f : 160.0f;
    const float exitK=finale? 0.0f : StvOutCubic((bt-(len-210.0f))/210.0f);
    for(int i=0;i<n;i++){
        if(txt[i]==' ') continue;
        const unsigned h=seed*131u+(unsigned)i*977u;
        float k=StvOutCubic((bt-i*stagger)/enter);
        if(k<=0.0f) continue;
        // where it comes from: scattered, oversized, spinning
        float ang=StvHash(h)*6.2831853f, dist=(finale? 380.0f : 700.0f)+StvHash(h+1)*420.0f;
        float rot0=(StvHash(h+2)-0.5f)*2.6f, sc0=finale? 3.2f : 2.4f;
        ImVec2 tgt(c.x-total*0.5f+xs[i]+ws[i]*0.5f, c.y);
        float x=tgt.x+cosf(ang)*dist*(1.0f-k), y=tgt.y+sinf(ang)*dist*(1.0f-k)*0.6f;
        float rot=rot0*(1.0f-k), sc=1.0f+(sc0-1.0f)*(1.0f-k), al=StvClamp01(k*2.5f);
        // the hit: every letter jolts on the landing
        sc*=1.0f+0.10f*hitDecay;
        // exit: thrown outward from the centre, spinning, fading
        if(exitK>0.0f){ float ea=atan2f(tgt.y-c.y+ (StvHash(h+3)-0.5f)*60.0f, tgt.x-c.x+0.01f);
            x+=cosf(ea)*exitK*(500.0f+StvHash(h+4)*500.0f); y+=sinf(ea)*exitK*260.0f+(StvHash(h+5)-0.5f)*exitK*300.0f;
            rot+=(StvHash(h+6)-0.5f)*4.0f*exitK; sc*=1.0f+0.4f*exitK; al*=1.0f-exitK; }
        // glitch: while landing and while leaving, the letter jumps sideways on some frames
        float gx=0; if((k<1.0f || exitK>0.0f) && StvHash(h+(unsigned)(StvStep(bt)/33.0f)*7u)>0.6f) gx=(StvHash(h+99u+(unsigned)bt)-0.5f)*46.0f;
        // chromatic split: wide as it flies in, spikes on the hit, a hair when settled
        float ch=2.0f+22.0f*(1.0f-k)+12.0f*hitDecay+30.0f*exitK;
        StvChar(dl,f,size,ImVec2(x-ch+gx,y),StvA(STV_RED,al*0.85f),txt[i],sc,rot,skew);
        StvChar(dl,f,size,ImVec2(x+ch+gx,y),StvA(STV_CYAN,al*0.70f),txt[i],sc,rot,skew);
        StvChar(dl,f,size,ImVec2(x+gx,y),StvA(IM_COL32(252,250,248,255),al),txt[i],sc,rot,skew);
    }
}
static void StvShockwave(ImDrawList* dl,ImVec2 c,float dt,float H,float power){
    if(dt<0 || dt>520.0f) return;
    for(int r=0;r<2;r++){ float d=dt-r*70.0f; if(d<0) continue;
        float p=StvOutCubic(d/450.0f); float rad=H*0.06f+H*0.62f*p*power;
        dl->AddCircle(c,rad,StvA(r==0? IM_COL32(255,255,255,255) : STV_CYAN,(1.0f-p)*0.9f),96,(r==0? 16.0f : 7.0f)*(1.0f-p)+1.0f); }
    if(dt<260.0f){ float p=StvOutCubic(dt/260.0f);               // radial burst of speed lines
        for(int i=0;i<32;i++){ float a=i*6.2831853f/32+StvHash(i)*0.15f, r1=H*(0.10f+0.50f*p), r2=r1+H*(0.10f+0.12f*StvHash(i+40));
            dl->AddLine(ImVec2(c.x+cosf(a)*r1,c.y+sinf(a)*r1),ImVec2(c.x+cosf(a)*r2,c.y+sinf(a)*r2),
                        StvA(i%3==0? STV_RED : IM_COL32(255,255,255,255),(1.0f-p)*0.8f),i%3==0? 3.0f : 1.6f); } }
}
static void StvBracket(ImDrawList* dl,ImVec2 p,float sx,float sy,float L,ImU32 col){
    dl->AddLine(p,ImVec2(p.x+sx*L,p.y),col,3.0f); dl->AddLine(p,ImVec2(p.x,p.y+sy*L),col,3.0f);
    dl->AddRectFilled(ImVec2(p.x+sx*L-(sx>0?0:10),p.y-2),ImVec2(p.x+sx*L+(sx>0?10:0),p.y+2),STV_RED);
}

static void StvDrawIntro(ImDrawList* dl,float W,float H){
    if(!g_stvIntroAt) return;
    const ULONGLONG now=GetTickCount64();
    const float raw=(float)(now-g_stvIntroAt), total=StvIntroLength();
    float outK;                                                 // 0 = fully in, 1 = gone
    if(g_stvIntroSkipAt){ outK=StvClamp01((float)(now-g_stvIntroSkipAt)/240.0f); if(outK>=1.0f){ g_stvIntroAt=0; g_stvSolid=false; return; } }
    else { if(raw>=total){ g_stvIntroAt=0; g_stvSolid=false; return; } outK=StvClamp01((raw-(total-STV_OUT_MS))/STV_OUT_MS); }
    const float t=StvStep(raw);
    const int v0=dl->VtxBuffer.Size;                            // everything below gets the camera punch
    const float bootK=StvOutCubic(t/STV_IN_MS), gone=StvOutCubic(outK), inK=bootK*(1.0f-gone);
    const ImVec2 C(W*0.5f,H*0.5f);
    const int n=(int)g_stvBeats.size();

    // which beat, and how hard the last hit still rings
    int beat=-1; float bt=0, blen=STV_BEAT_MS, hitDecay=0, hitAt=0;
    for(int i=0;i<n;i++){ float b0=STV_IN_MS+i*STV_BEAT_MS, len=(i==n-1)? STV_LAST_MS : STV_BEAT_MS;
        if(t>=b0 && t<b0+len){ beat=i; bt=t-b0; blen=len; } }
    if(beat>=0){ int nl=(int)g_stvBeats[beat].size(); bool fin=(beat==n-1);
        hitAt=(fin? 55.0f : 20.0f)*(float)(nl-1)+(fin? 120.0f : 160.0f);
        if(bt>=hitAt) hitDecay=1.0f-StvClamp01((bt-hitAt)/260.0f); }

    // ---- backdrop: darkened desktop, a HUD grid, one scan line per beat ----
    dl->AddRectFilled(ImVec2(0,0),ImVec2(W,H),StvA(STV_BLACK,g_stvSolid? 1.0f : 0.82f*inK));
    for(float gx=fmodf(t*0.04f,80.0f); gx<W; gx+=80.0f) dl->AddLine(ImVec2(gx,0),ImVec2(gx,H),StvA(IM_COL32(255,255,255,255),0.035f*inK),1.0f);
    for(float gy=0; gy<H; gy+=80.0f) dl->AddLine(ImVec2(0,gy),ImVec2(W,gy),StvA(IM_COL32(255,255,255,255),0.035f*inK),1.0f);
    if(!g_stvIntroSkipAt){ float sy=fmodf(t*1.1f,H*1.2f)-H*0.1f;
        dl->AddRectFilledMultiColor(ImVec2(0,sy-60),ImVec2(W,sy),0,0,StvA(STV_CYAN,0.10f*inK),StvA(STV_CYAN,0.10f*inK));
        dl->AddLine(ImVec2(0,sy),ImVec2(W,sy),StvA(STV_CYAN,0.45f*inK),1.5f); }

    // ---- the emblem: one shape per beat, morphing into the next, pulsing on the hit ----
    if(!g_stvIntroSkipAt){
        int from=beat<0? 0 : beat, to=from+1; float m=beat<0? 0.0f : StvOutCubic(bt/blen);
        float R=H*0.27f*bootK*(1.0f+0.14f*hitDecay)*(1.0f-gone);
        if(R>2.0f){ float rot=t*0.0006f;
            StvEmblem(dl,C,R,from,to,m,rot,StvA(IM_COL32(255,255,255,255),0.40f+0.4f*hitDecay),3.5f+3.5f*hitDecay);
            StvEmblem(dl,C,R*1.10f,to,from+2,m,-rot*1.7f,StvA(STV_RED,0.65f),3.0f);
            StvEmblem(dl,C,R*0.86f,from+3,to+1,m,rot*2.3f,StvA(STV_CYAN,0.30f),1.5f); } }

    // ---- the plate: opens for each beat, BREATHES on the hit, collapses to a line between beats ----
    float plate=0;
    if(beat>=0 && !g_stvIntroSkipAt){
        float open=StvOutCubic(bt/140.0f), shut=(beat==n-1)? 0.0f : StvOutCubic((bt-(blen-160.0f))/160.0f);
        plate=H*0.22f*open*(1.0f-shut)*(1.0f+0.18f*hitDecay); }
    if(plate>1.0f){
        StvPlate(dl,W,C.y-plate*0.5f,C.y+plate*0.5f,0.22f,StvA(STV_BLACK,0.92f*inK));
        StvPlate(dl,W,C.y-plate*0.5f-5,C.y-plate*0.5f,0.22f,StvA(STV_RED,inK));
        StvPlate(dl,W,C.y+plate*0.5f,C.y+plate*0.5f+5,0.22f,StvA(STV_RED,inK));
    }

    // ---- the call ----
    if(beat>=0 && !g_stvIntroSkipAt){
        bool fin=(beat==n-1);
        float fs=fin? 250.0f : 190.0f;
        ImVec2 c(C.x+StvClamp01((bt-hitAt)/700.0f)*18.0f, C.y);
        StvWord(dl,StvFont(),fs,c,g_stvBeats[beat].c_str(),bt,blen,(unsigned)(beat+1)*7919u,hitDecay,fin,W,H);
        if(fin){ int nl=(int)g_stvBeats[beat].size();           // the finale lands letter by letter: a ring for each
            for(int i=0;i<nl;i++){ float d=bt-(i*55.0f+120.0f); if(d>=0 && d<200.0f)
                dl->AddCircle(ImVec2(c.x+(i-(nl-1)*0.5f)*fs*0.52f,c.y),30.0f+d*0.9f,StvA(IM_COL32(255,255,255,255),1.0f-d/200.0f),48,3.0f); } }
        StvShockwave(dl,C,bt-hitAt,H,fin? 1.35f : 1.0f);
        // telemetry tag under the plate
        char tag[64]; snprintf(tag,sizeof(tag),"// BEAT %02d/%02d  \xC2\xB7  %s",beat+1,n,fin? "ENGAGE" : "SYNC");
        dl->AddText(g_fMono? g_fMono : g_fSml,15.0f,ImVec2(C.x-150,C.y+plate*0.5f+18),StvA(STV_CYAN,0.8f*inK),tag);
    }

    // ---- letterbox + HUD frame ----
    float bar=H*0.13f*inK*(1.0f+0.06f*hitDecay);
    dl->AddRectFilled(ImVec2(0,0),ImVec2(W,bar),STV_BLACK);
    dl->AddRectFilled(ImVec2(0,H-bar),ImVec2(W,H),STV_BLACK);
    dl->AddRectFilled(ImVec2(0,bar),ImVec2(W*inK,bar+3),StvA(STV_RED,inK));
    dl->AddRectFilled(ImVec2(W*(1.0f-inK),H-bar-3),ImVec2(W,H-bar),StvA(STV_RED,inK));
    for(float tx=fmodf(-t*0.25f,40.0f); tx<W; tx+=40.0f){              // ticks crawling along the bars
        dl->AddLine(ImVec2(tx,bar-10),ImVec2(tx,bar-2),StvA(IM_COL32(255,255,255,255),0.25f*inK),1.0f);
        dl->AddLine(ImVec2(W-tx,H-bar+2),ImVec2(W-tx,H-bar+10),StvA(IM_COL32(255,255,255,255),0.25f*inK),1.0f); }
    { float ins=48.0f+(1.0f-inK)*220.0f, L=110.0f;                    // corner brackets snap in from outside
      ImU32 bc=StvA(IM_COL32(255,255,255,255),0.85f*inK);
      StvBracket(dl,ImVec2(ins,bar+ins*0.5f),1,1,L,bc);       StvBracket(dl,ImVec2(W-ins,bar+ins*0.5f),-1,1,L,bc);
      StvBracket(dl,ImVec2(ins,H-bar-ins*0.5f),1,-1,L,bc);    StvBracket(dl,ImVec2(W-ins,H-bar-ins*0.5f),-1,-1,L,bc); }
    { ImFont* mono=g_fMono? g_fMono : g_fSml;                           // telemetry
      time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
      char l1[64], l2[64], r1[64];
      snprintf(l1,sizeof(l1),"AETHER  //  SESSION %02d",g_stvIntroCount);
      snprintf(l2,sizeof(l2),"%02d:%02d:%02d  \xC2\xB7  %03d FPS LOCK",lt.tm_hour,lt.tm_min,lt.tm_sec,Cael::g_striveFps>0? Cael::g_striveFps : 60);
      snprintf(r1,sizeof(r1),"SYNC %3d%%",(int)(StvClamp01(t/(total-STV_OUT_MS))*100.0f));
      dl->AddText(mono,17.0f,ImVec2(60,bar*0.5f-18),StvA(IM_COL32(255,255,255,255),0.9f*inK),l1);
      dl->AddText(mono,14.0f,ImVec2(60,bar*0.5f+4),StvA(STV_CYAN,0.8f*inK),l2);
      ImVec2 rs=mono->CalcTextSizeA(17.0f,FLT_MAX,0,r1);
      dl->AddText(mono,17.0f,ImVec2(W-60-rs.x,bar*0.5f-18),StvA(STV_RED,0.95f*inK),r1);
      dl->AddRectFilled(ImVec2(W-60-220,bar*0.5f+8),ImVec2(W-60,bar*0.5f+12),StvA(IM_COL32(255,255,255,255),0.15f*inK));
      dl->AddRectFilled(ImVec2(W-60-220,bar*0.5f+8),ImVec2(W-60-220+220*StvClamp01(t/(total-STV_OUT_MS)),bar*0.5f+12),StvA(STV_RED,inK)); }

    // ---- flashes: each landing, and the finale's white-out as everything shatters ----
    if(beat>=0 && bt>=hitAt && bt<hitAt+70.0f)
        dl->AddRectFilled(ImVec2(0,0),ImVec2(W,H),StvA(IM_COL32(255,255,255,255),(beat==n-1? 0.50f : 0.30f)*(1.0f-(bt-hitAt)/70.0f)));
    if(!g_stvIntroSkipAt){ float fend=total-STV_OUT_MS;
        if(t>=fend-40.0f && t<fend+160.0f) dl->AddRectFilled(ImVec2(0,0),ImVec2(W,H),StvA(IM_COL32(255,255,255,255),0.85f*(1.0f-StvClamp01((t-fend+40.0f)/200.0f))));
        // shards: the plate breaks into triangles that fly out as the letterbox opens
        if(outK>0.0f){ for(int i=0;i<36;i++){
            float hx=StvHash(i*3+1), hy=StvHash(i*3+2), hr=StvHash(i*3+3);
            ImVec2 o(W*hx, C.y+(hy-0.5f)*H*0.24f); float ang=atan2f(o.y-C.y,o.x-C.x);
            float d=gone*(300.0f+600.0f*hr), sz=30.0f+50.0f*hr, rr=gone*6.0f*(hr-0.5f);
            ImVec2 q(o.x+cosf(ang)*d, o.y+sinf(ang)*d*0.7f+gone*gone*200.0f);
            ImVec2 a(q.x+cosf(rr)*sz,q.y+sinf(rr)*sz), b2(q.x+cosf(rr+2.2f)*sz*0.7f,q.y+sinf(rr+2.2f)*sz*0.7f), c2(q.x+cosf(rr+4.1f)*sz*0.8f,q.y+sinf(rr+4.1f)*sz*0.8f);
            dl->AddTriangleFilled(a,b2,c2,StvA(i%4==0? STV_RED : STV_BLACK,(1.0f-gone)*0.95f));
            dl->AddTriangle(a,b2,c2,StvA(i%3==0? STV_CYAN : IM_COL32(255,255,255,255),(1.0f-gone)*0.7f),1.5f); } }
    }

    // ---- camera punch: the whole frame kicks in on every hit and settles ----
    float z=1.0f+0.045f*hitDecay*hitDecay;
    if(z>1.0005f) for(int i=v0;i<dl->VtxBuffer.Size;i++){ ImVec2& p=dl->VtxBuffer[i].pos; p=ImVec2(C.x+(p.x-C.x)*z, C.y+(p.y-C.y)*z); }
}

static void StvSetSolid(){ g_stvSolid=true; }
// ---- hooks -------------------------------------------------------------------------------------------
static bool StvDimActive(){ return g_stvBanAt!=0 || g_stvIntroAt!=0; }
static void StvDrawDim(ImDrawList* dl,float W,float H){ StvDrawBanner(dl,W,H); StvDrawIntro(dl,W,H); }
// once per render-loop pass
static void StvTick(){
    StvBannerTick();
    // a click skips the intro too (the dim layer is click-through, so it has to be read here)
    static bool wasDown=false; bool down=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    if(down && !wasDown) StvIntroSkip();
    wasDown=down;
}
