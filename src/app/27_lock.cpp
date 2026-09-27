// Aether - the lock screen.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =============================================================== Caelestia-style lock screen
// Three-column layout over a blurred capture of the screen: LEFT = weather + system fetch + media,
// CENTER = clock/date/avatar/password, RIGHT = performance + notifications. Password is validated
// with LockValidatePw (LogonUser). SAFE: this is a plain topmost overlay, so Ctrl+Alt+Del always
// reaches the OS secure desktop — the machine can never be truly locked out by a bug here.
#include "src/modules/lock/LockV2.h"   // the newer Caelestia lock screen
static void DrawLock(){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x,H=io.DisplaySize.y;
    float a=std::clamp(g_lockAnim,0.0f,1.0f), ea=1-(1-a)*(1-a);
    auto A=[](ImU32 c,float t){ int al=(int)(((c>>24)&0xFF)*std::clamp(t,0.0f,1.0f)); return (c&0x00FFFFFF)|((ImU32)al<<24); };
    // blurred background (frozen capture) + dark scrim
    if(g_lockBg) dl->AddImage((ImTextureID)g_lockBg,V(0,0),V(W,H),ImVec2(0,0),ImVec2(1,1),A(IM_COL32(255,255,255,255),ea));
    // Scrim kept deliberately light: the point of this screen is that your desktop shows THROUGH it.
    // The cards sit at ~204 alpha, so legibility comes from them, not from drowning the backdrop.
    dl->AddRectFilled(V(0,0),V(W,H),A(WithA(g_darkUI?IM_COL32(6,8,12,255):IM_COL32(12,14,20,255),112),ea));

    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
    const char* mo3[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
    const char* dnU[]={"SUNDAY","MONDAY","TUESDAY","WEDNESDAY","THURSDAY","FRIDAY","SATURDAY"};

    // scheme-aware Material palette (matches theirs: surface cards over the blur, not a black sheet)
    bool dk=g_darkUI;
    ImU32 CINK = dk?IM_COL32(236,238,244,255):IM_COL32(28,30,36,255);
    ImU32 CINK2= dk?IM_COL32(176,180,192,255):IM_COL32(96,100,112,255);
    ImU32 CSURF= COL_CARD;                                     // m3 surfaceContainer
    ImU32 CTRK = dk?IM_COL32(70,74,84,255):IM_COL32(206,210,218,255);

    // ---- intro: lock glyph scales + rotates in, then hands off to the content ----
    float intro=std::clamp((a-0.0f)/0.55f,0.0f,1.0f);
    float content=std::clamp((a-0.35f)/0.65f,0.0f,1.0f);
    { float sc=0.5f+0.5f*EaseOutBack(intro); float rot=(1.0f-intro)*1.4f;
      float fade=(intro<1.0f)? intro : std::clamp(1.0f-content*1.4f,0.0f,1.0f);
      if(fade>0.01f){ ImVec2 c=V(W/2, g_lockStyle==0? H*0.5f-8.0f : H*0.42f); float r=34*sc;
        auto rp=[&](float ox,float oy){ float cs=cosf(rot),sn=sinf(rot); return V(c.x+ox*cs-oy*sn,c.y+ox*sn+oy*cs); };
        dl->AddRectFilled(rp(-r*0.6f,0),rp(r*0.6f,r*0.9f),A(WithA(COL_GOLD,255),fade*ea),8);
        dl->PathClear(); for(int k=0;k<=16;k++){ float t=3.1416f*k/16; dl->PathLineTo(rp((-r*0.42f)*cosf(t),-(r*0.42f)*sinf(t))); }
        dl->PathStroke(A(WithA(COL_GOLD,255),fade*ea),0,5.0f); } }

    float cyo=(1.0f-content)*22.0f;
    auto Ac=[&](ImU32 c){ return A(c,content); };
    ImFont* mono = g_fMono? g_fMono : g_fMed;               // CaskaydiaCove-style mono like theirs
    ImU32 CCONT = Mix(COL_CARD, dk?IM_COL32(0,0,0,255):IM_COL32(255,255,255,255), dk?0.46f:0.16f);  // outer shell
    ImU32 CTILE = dk? Mix(COL_CARD,IM_COL32(255,255,255,255),0.06f) : COL_CARD;                       // inner cards
    static std::string uname; if(uname.empty()){ wchar_t u[256]; DWORD n=256; if(GetUserNameW(u,&n)) uname=W2U8(u); }
    const char* mnF[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
    const char* dnF[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};

    // -------- animation layer --------
    float T=(float)ImGui::GetTime();
    float el=(float)((double)((long long)GetTickCount64()-(long long)g_lockOpenAt)/1000.0);   // seconds since open
    float pulse=0.5f+0.5f*sinf(T*2.2f);                                    // slow breathing 0..1
    auto stg =[&](int i){ return EaseOutCubic(std::clamp((content-i*0.05f)/0.55f,0.0f,1.0f)); };  // staggered entrance
    auto fillAt=[&](int i){ return EaseOutCubic(std::clamp((el-0.15f-i*0.09f)/0.7f,0.0f,1.0f)); }; // gauge sweep-in
    // typing pop + error shake, updated once per frame here
    static int   s_prevN=0; static float s_dotPop=0.0f, s_shake=0.0f; static bool s_prevErr=false;
    int nchNow=(int)strlen(g_lockPw);
    if(nchNow>s_prevN) s_dotPop=1.0f;
    s_prevN=nchNow; s_dotPop=std::max(0.0f,s_dotPop-g_frameDt*6.0f);
    bool errNow = GetTickCount64() < g_lockErrUntil;
    if(errNow && !s_prevErr) s_shake=1.0f; s_prevErr=errNow;
    s_shake=std::max(0.0f,s_shake-g_frameDt*3.0f);
    float shakeX = sinf(T*46.0f)*s_shake*10.0f;

    // ---- blurred VFX background: soft drifting aurora blobs over the scrim ----
    { auto blob=[&](float bx,float by,float rad,ImU32 col,float amul){
          // one smooth radial falloff (SoftDisc). It used to be eight flat discs stacked at alpha 6, which
          // is what drew concentric RINGS across the blurred backdrop. 0.17 is what that stack added up to
          // at the centre, so the blobs are as strong as before - just soft.
          SoftDisc(dl,V(bx,by),rad,col,0.17f*amul*std::clamp(content,0.0f,1.0f)); };
      float bt=T*0.14f;
      blob(W*0.30f+sinf(bt)*46,        H*0.34f+cosf(bt*0.8f)*34, W*0.24f, COL_GOLD, 1.0f);
      blob(W*0.72f+cosf(bt*1.1f)*54,   H*0.64f+sinf(bt*0.9f)*40, W*0.26f, Mix(COL_GOLD,IM_COL32(150,120,255,255),0.5f), 0.9f);
      blob(W*0.55f+sinf(bt*0.7f)*64,   H*0.20f+cosf(bt)*26,      W*0.20f, Mix(COL_GOLD,IM_COL32(120,190,255,255),0.5f), 0.7f);
      blob(W*0.18f+cosf(bt*1.3f)*40,   H*0.78f,                  W*0.18f, Mix(COL_GOLD,IM_COL32(255,150,190,255),0.5f), 0.7f); }

    // Two layouts. "Panel" is Caelestia's: one big container holding three columns. "Floating"
    // is sarods2d's: the same information as M3 shape widgets scattered straight onto the blurred
    // wallpaper, with no container at all. Both share the backdrop, the blobs and the input code
    // below, so only the arrangement differs.
    if(g_lockStyle==2){ DrawLockV2(dl,io,W,H,a,content,errNow,shakeX,s_dotPop); }
    else if(g_lockStyle==0){
        // ---- ONE big rounded container holds all three columns (like theirs), scale + rise entrance ----
        float cw2=std::min(W*0.72f,1400.0f), ch2=std::min(H*0.70f,780.0f);
        float x0=(W-cw2)*0.5f, y0=(H-ch2)*0.5f;
        // Caelestia's lock surface GROWS out of the lock icon: a disc behind the glyph widens into a bar,
        // then opens to full height - one continuous shape, not a panel fading and scaling in.
        const float S0=104.0f;
        float gw=Cael::eval(Cael::EMPHASIZED_DECEL,std::clamp((a-0.06f)/0.40f,0.0f,1.0f));
        float gh=Cael::eval(Cael::EMPHASIZED_DECEL,std::clamp((a-0.28f)/0.46f,0.0f,1.0f));
        float curW=S0+(cw2-S0)*gw, curH=S0+(ch2-S0)*gh;
        float gx0=(W-curW)*0.5f, gy0=(H-curH)*0.5f;
        float crnd=std::min(34.0f+(S0*0.5f-34.0f)*(1.0f-gh), curH*0.5f);
        float shellA=std::clamp(a/0.12f,0.0f,1.0f);
        for(int i=8;i>0;i--){ float e=i*3.2f; dl->AddRect(V(gx0-e,gy0-e+5),V(gx0+curW+e,gy0+curH+e+5),A(IM_COL32(0,0,0,8),shellA),crnd+e,0,e); }
        dl->AddRectFilled(V(gx0,gy0),V(gx0+curW,gy0+curH),A(WithA(CCONT,dk?204:226),shellA),crnd);
        // top sheen (subtle) + moving highlight sweep along the top edge
        dl->AddRectFilledMultiColor(V(gx0,gy0),V(gx0+curW,gy0+curH*0.5f),
            A(IM_COL32(255,255,255,dk?12:22),content),A(IM_COL32(255,255,255,dk?12:22),content),A(0,0),A(0,0));
        { float sw=curW*0.28f, sxp=gx0 - sw + fmodf(T*0.10f,1.0f)*(curW+sw);      // slow left-to-right glint
          dl->PushClipRect(V(gx0,gy0),V(gx0+curW,gy0+8),true);
          dl->AddRectFilledMultiColor(V(sxp,gy0),V(sxp+sw,gy0+4),A(0,0),A(WithA(COL_GOLD,90),content),A(WithA(COL_GOLD,90),content),A(0,0));
          dl->PopClipRect(); }
        dl->AddRect(V(gx0,gy0),V(gx0+curW,gy0+curH),A(dk?IM_COL32(255,255,255,16):IM_COL32(255,255,255,150),shellA),crnd,0,1.2f);
        dl->PushClipRect(V(gx0,gy0),V(gx0+curW,gy0+curH),true);      // the content is revealed BY the surface
        // inner card helper (opaque tile that sits on the container)
        auto card=[&](ImVec2 a0,ImVec2 b0,float rnd){
            dl->AddRectFilled(a0,b0,A(WithA(CTILE,dk?250:255),content),rnd);
            dl->AddRect(a0,b0,A(dk?IM_COL32(255,255,255,12):IM_COL32(0,0,0,14),content),rnd,0,1.0f);
        };
        auto mkey=[&](BYTE vk){ keybd_event(vk,0,0,0); keybd_event(vk,0,KEYEVENTF_KEYUP,0); };

        float pad=24;
        float colW=(cw2-2*pad)*0.30f;                 // side columns ~30% each, like the reference
        float lx=x0+pad, rx=x0+cw2-pad-colW, ccx=x0+cw2*0.5f;

        // ============================ CENTER ============================
        { float rise0=(1.0f-stg(0))*24.0f, rise2=(1.0f-stg(2))*26.0f, rise3=(1.0f-stg(3))*28.0f;
          char clk[12]; int h12=g_clock24?lt.tm_hour:((lt.tm_hour%12)==0?12:lt.tm_hour%12);
          snprintf(clk,12,"%02d:%02d",h12,lt.tm_min);
          const char* ap=g_clock24?"":(lt.tm_hour<12?"AM":"PM");
          float fs=92; float cwd=g_fHuge->CalcTextSizeA(fs,FLT_MAX,0,clk).x;
          float apw = *ap? mono->CalcTextSizeA(30,FLT_MAX,0,ap).x+14 : 0;
          float clkY=y0+ch2*0.14f+rise0, cx=ccx-(cwd+apw)/2;
          // soft glow behind the clock that breathes
          dl->AddText(g_fHuge,fs,V(cx,clkY),A(WithA(COL_GOLD,(int)(40+pulse*40)),content),clk);   // glow pass
          dl->AddText(g_fHuge,fs,V(cx,clkY),Ac(WithA(COL_GOLD,255)),clk);
          if(*ap) dl->AddText(mono,30,V(cx+cwd+14,clkY+fs*0.42f),Ac(WithA(COL_GOLD,255)),ap);
          char date[64]; snprintf(date,64,"%s, %d %s %d",dnF[lt.tm_wday],lt.tm_mday,mnF[lt.tm_mon],lt.tm_year+1900);
          float dw=mono->CalcTextSizeA(20,FLT_MAX,0,date).x;
          dl->AddText(mono,20,V(ccx-dw/2,clkY+fs+10),Ac(WithA(COL_GOLD,220)),date);
          // avatar orb — dark speckled disc + neon glyph, breathing glow, slow-rotating speckles
          float R=94, acy=y0+ch2*0.55f+rise2; LoadAvatar();
          dl->AddCircle(V(ccx,acy),R+8+pulse*5,A(WithA(COL_GOLD,(int)(50+pulse*70)),content),0,2.2f);   // breathing halo
          dl->AddCircleFilled(V(ccx,acy),R+8,Ac(dk?IM_COL32(12,12,16,255):IM_COL32(26,26,32,255)));
          if(g_avatar){ dl->PushClipRect(V(ccx-R,acy-R),V(ccx+R,acy+R),true);
              dl->AddImageRounded((ImTextureID)g_avatar,V(ccx-R,acy-R),V(ccx+R,acy+R),ImVec2(0,0),ImVec2(1,1),A(IM_COL32(255,255,255,255),content),R);
              dl->PopClipRect(); }
          else { dl->AddCircleFilled(V(ccx,acy),R,Ac(dk?IM_COL32(30,30,38,255):IM_COL32(42,44,56,255)));
              for(int s=0;s<160;s++){ float ang=s*2.399963f+T*0.15f, rr=R*0.94f*sqrtf((s%37)/37.0f);
                  dl->AddCircleFilled(V(ccx+cosf(ang)*rr,acy+sinf(ang)*rr),0.9f,Ac(WithA(IM_COL32(200,200,210,255),24))); }
              float hx=ccx-16, hy=acy; dl->AddCircleFilled(V(hx-5,hy-4),6,Ac(WithA(COL_GOLD,255))); dl->AddCircleFilled(V(hx+5,hy-4),6,Ac(WithA(COL_GOLD,255)));
              dl->AddTriangleFilled(V(hx-10,hy-1),V(hx+10,hy-1),V(hx,hy+11),Ac(WithA(COL_GOLD,255)));
              float ax=ccx+24; dl->AddTriangleFilled(V(ax-8,hy-10),V(ax-8,hy+10),V(ax+9,hy),Ac(WithA(COL_GOLD,255))); }
          dl->AddCircle(V(ccx,acy),R+8,Ac(WithA(COL_GOLD,110)),0,2.0f);
          // password pill (shakes on a wrong attempt)
          bool err = errNow;
          float pcx=ccx+shakeX;
          float pw2=std::min(360.0f,cw2*0.26f), ph2=48, pxx=pcx-pw2/2, pyy=y0+ch2*0.80f+rise3;
          dl->AddRectFilled(V(pxx,pyy),V(pxx+pw2,pyy+ph2),Ac(dk?IM_COL32(255,255,255,16):IM_COL32(0,0,0,18)),ph2*0.5f);
          dl->AddRect(V(pxx,pyy),V(pxx+pw2,pyy+ph2),Ac(err?WithA(COL_ERR,230):WithA(COL_GOLD,(int)(70+pulse*60))),ph2*0.5f,0,1.6f);
          float lkx=pxx+24, lky=pyy+ph2*0.5f;
          if(!ThemedSym(dl,V(lkx,lky+2),10.0f,Ac(WithA(COL_GOLD,220)),"system-lock-screen")){
            dl->AddRectFilled(V(lkx-6,lky-1),V(lkx+6,lky+9),Ac(WithA(COL_GOLD,220)),2);
            dl->PathArcTo(V(lkx,lky-1),5,3.1416f,6.2832f,10); dl->PathStroke(Ac(WithA(COL_GOLD,220)),0,2.0f); }
          int nch=(int)strlen(g_lockPw);
          if(nch==0 && !err){ const char* ph="Enter your password"; TextAt(dl,mono,15,V(pcx-mono->CalcTextSizeA(15,FLT_MAX,0,ph).x/2,pyy+15),Ac(WithA(CINK2,200)),ph); }
          else if(err && nch==0){ const char* ph="Wrong \xE2\x80\x94 try again"; TextAt(dl,mono,15,V(pcx-mono->CalcTextSizeA(15,FLT_MAX,0,ph).x/2,pyy+15),Ac(WithA(COL_ERR,255)),ph); }
          else { float tot=std::min(nch,20)*15.0f, sx=pcx-tot/2; for(int i=0;i<nch&&i<20;i++){
                     float pop=(i==nch-1)? 1.0f+0.7f*s_dotPop : 1.0f;
                     dl->AddCircleFilled(V(sx+i*15+7,pyy+ph2*0.5f),4.5f*pop,Ac(CINK)); } }
          ImVec2 ub=V(pxx+pw2-26,pyy+ph2*0.5f); bool ubh=fabsf(io.MousePos.x-ub.x)<20&&fabsf(io.MousePos.y-ub.y)<20;
          float ubr=15+HoverAnim(48800,ubh)*3.0f;
          dl->AddCircleFilled(ub,ubr,Ac(ubh?WithA(COL_GOLD,255):WithA(COL_GOLD,150)));
          ImU32 arrc=dk?IM_COL32(16,20,16,255):IM_COL32(250,254,252,255);
          dl->AddLine(V(ub.x-4,ub.y),V(ub.x+5,ub.y),Ac(arrc),2.2f);
          dl->AddLine(V(ub.x+1,ub.y-4),V(ub.x+5,ub.y),Ac(arrc),2.2f); dl->AddLine(V(ub.x+1,ub.y+4),V(ub.x+5,ub.y),Ac(arrc),2.2f);
          // ---- Windows Hello row (Caelestia's fprint/Howdy slot) ----------------------------------
          // Only drawn when the machine actually has Hello set up, and only as an ALTERNATIVE: the
          // password pill above stays the primary path, so a broken sensor can never lock you out.
          g_lockHelloBh=false;
          float infoY=pyy+ph2+14;
          if(g_helloOn && g_helloAvail.load()==1 && !g_helloBroken.load()){
              int hs=g_helloState.load();
              const char* hl = hs==HELLO_ASKING? "Waiting for Windows Hello…"
                             : hs==HELLO_OK    ? "Verified"
                                               : "Unlock with Windows Hello";
              float hw=mono->CalcTextSizeA(14,FLT_MAX,0,hl).x+58, hh=38;
              float hx0=ccx-hw*0.5f, hy0=pyy+ph2+14;
              bool hbh = hs==HELLO_IDLE && io.MousePos.x>hx0&&io.MousePos.x<hx0+hw&&io.MousePos.y>hy0&&io.MousePos.y<hy0+hh;
              float hha=HoverAnim(48801,hbh);
              dl->AddRectFilled(V(hx0,hy0),V(hx0+hw,hy0+hh),Ac(dk?WithA(IM_COL32(255,255,255,255),(int)(14+hha*16))
                                                                  :WithA(IM_COL32(0,0,0,255),(int)(16+hha*16))),hh*0.5f);
              dl->AddRect(V(hx0,hy0),V(hx0+hw,hy0+hh),Ac(WithA(COL_GOLD,(int)(60+hha*90))),hh*0.5f,0,1.3f);
              // fingerprint mark: concentric arcs, and it SPINS its opacity while the prompt is up
              // fingerprint: near-full concentric rings with the gap at the BOTTOM (an arc across the
              // top alone just reads as a Wi-Fi fan), plus the centre whorl. Breathes while Hello asks.
              { ImVec2 fc=V(hx0+24,hy0+hh*0.5f+1.0f);
                float pl = hs==HELLO_ASKING? (0.45f+0.55f*sinf((float)GetTickCount64()*0.006f)):1.0f;
                for(int r2=0;r2<4;r2++){ float rr2=3.0f+r2*2.8f; float gap=0.55f+r2*0.10f;
                    dl->PathArcTo(fc,rr2,1.5708f+gap,1.5708f+6.2832f-gap,20);
                    dl->PathStroke(Ac(WithA(COL_GOLD,(int)((235-r2*28)*pl))),0,1.6f); }
                dl->AddLine(V(fc.x,fc.y-2.0f),V(fc.x,fc.y+2.0f),Ac(WithA(COL_GOLD,(int)(235*pl))),1.6f); }
              TextAt(dl,mono,14,V(hx0+44,hy0+11),Ac(WithA(hs==HELLO_OK?COL_GOLD:CINK,235)),hl);
              g_lockHelloBh=hbh;
              infoY = hy0+hh+12;
          } else if(g_helloOn && (g_helloAvail.load()<0 || g_helloBroken.load())){
              const char* wn = g_helloBroken.load()? g_helloBrokenWhy.c_str() : HelloWhyNot(-g_helloAvail.load()-2);
              TextAt(dl,mono,12,V(ccx-mono->CalcTextSizeA(12,FLT_MAX,0,wn).x/2,infoY),Ac(WithA(CINK2,140)),wn);
              infoY+=20;
          }
          if(GetTickCount64()<g_helloMsgUntil && !g_helloMsg.empty()){
              TextAt(dl,mono,13,V(ccx-mono->CalcTextSizeA(13,FLT_MAX,0,g_helloMsg.c_str()).x/2,infoY),Ac(WithA(COL_ERR,235)),g_helloMsg.c_str());
              infoY+=20; }
          { const char* st = (GetKeyState(VK_NUMLOCK)&1)? "Num lock is ON." : "Ctrl+Alt+Del always works";
            TextAt(dl,mono,13,V(ccx-mono->CalcTextSizeA(13,FLT_MAX,0,st).x/2,infoY),Ac(WithA(CINK2,190)),st); }
          g_lockUbh=ubh;
        }

        // ============================ LEFT column ============================
        { lx -= (1.0f-stg(1))*54.0f;                              // slide in from the left
          float wH=150, fH=270, mH=190, gap=16;
          float stackH=wH+gap+fH+gap+mH; float ly=y0+(ch2-stackH)*0.5f + (1.0f-stg(1))*16.0f;
          // weather card — big rain icon left, "Weather"/condition/temp right (like the reference)
          { ImVec2 a0=V(lx,ly),b0=V(lx+colW,ly+wH); card(a0,b0,22);
            float trx=lx+colW*0.42f;
            TextAt(dl,g_fBig,28,V(trx,ly+26),Ac(WithA(COL_GOLD,255)),"Weather");
            if(g_wx.ok){ WxIcon(dl,V(lx+70,ly+wH*0.58f),34,g_wx.code);
              TextAt(dl,g_fMed,21,V(trx,ly+68),Ac(CINK),WxText(g_wx.code));
              char hm[24]; snprintf(hm,24,"%d\xC2\xB0",(int)round(g_wx.temp));
              TextAt(dl,mono,15,V(trx,ly+98),Ac(CINK2),hm); }
            else { WxIcon(dl,V(lx+70,ly+wH*0.58f),34,0); TextAt(dl,mono,14,V(trx,ly+70),Ac(CINK2),"unavailable"); }
            ly+=wH+gap; }
          // fetch card (distro logo + KV + palette swatches)
          { ImVec2 a0=V(lx,ly),b0=V(lx+colW,ly+fH); card(a0,b0,22);
            dl->AddCircleFilled(V(lx+28,ly+26),12,Ac(WithA(COL_GOLD,dk?60:50)));
            TextAt(dl,mono,13,V(lx+22,ly+18),Ac(WithA(COL_GOLD,255)),">");
            TextAt(dl,mono,15,V(lx+48,ly+18),Ac(CINK),"aetherfetch");
            // the Aether mark, at size
            { float lgx=lx+66, lgy=ly+168-30, hgt=64;
              AetherMark(dl, V(lgx,lgy), hgt*0.62f, Ac(WithA(COL_GOLD,255))); }
            // KV rows on the right
            float kx=lx+122, ky=ly+70;
            auto kv=[&](const char* k,const char* v){ TextAt(dl,mono,14,V(kx,ky),Ac(WithA(COL_GOLD,220)),k);
                TextAt(dl,mono,14,V(kx+54,ky),Ac(CINK),Clip(mono,14,v,colW-(kx-lx)-64).c_str()); ky+=30; };
            unsigned long long mins=GetTickCount64()/60000ULL; char up[32];
            if(mins<60) snprintf(up,32,"%llu min",mins); else snprintf(up,32,"%lluh %llum",mins/60,mins%60);
            kv("WM","Aether"); kv("OS","Windows 11"); kv("USER",uname.c_str()); kv("UP",up);
            // palette swatch row (matugen-style) — CIRCLES like the reference
            ImU32 sw[7]={ dk?IM_COL32(46,46,54,255):IM_COL32(60,60,68,255), COL_GOLD,
                Mix(COL_GOLD,IM_COL32(255,150,140,255),0.5f), Mix(COL_GOLD,IM_COL32(255,255,255,255),0.5f),
                Mix(COL_GOLD,IM_COL32(150,140,255,255),0.55f), Mix(COL_GOLD,IM_COL32(210,120,255,255),0.55f),
                Mix(COL_GOLD,IM_COL32(120,190,255,255),0.55f) };
            float sr=13, sy=ly+fH-30, sgap=(colW-40-7*(sr*2))/6.0f;
            for(int i=0;i<7;i++){ float scx=lx+20+sr+i*(sr*2+sgap); dl->AddCircleFilled(V(scx,sy),sr,Ac(sw[i])); }
            ly+=fH+gap; }
          // now-playing card (album art background + 5 round controls)
          { bool hasM=g_md.has&&!g_md.title.empty();
            ImVec2 a0=V(lx,ly),b0=V(lx+colW,ly+mH); card(a0,b0,22);
            dl->PushClipRect(a0,b0,true);
            if(g_mdArt) DrawArtFit(dl,a0,b0,18.0f,(int)(150*content),COL_GOLD);
            dl->AddRectFilled(a0,b0,A(IM_COL32(10,10,16,150),content),18);
            dl->PopClipRect();
            TextAt(dl,mono,12,V(lx+18,ly+14),Ac(WithA(IM_COL32(235,235,242,255),230)),"Now playing");
            float tw=colW-36;
            std::string t1=hasM?Clip(g_fBig,22,g_md.title,tw):"Nothing playing";
            TextAt(dl,g_fBig,22,V(lx+colW*0.5f-TextW(g_fBig,22,t1.c_str())/2,ly+72),Ac(IM_COL32(245,245,250,255)),t1.c_str());
            if(hasM){ std::string t2=Clip(g_fSml,15,g_md.artist,tw); TextAt(dl,g_fSml,15,V(lx+colW*0.5f-TextW(g_fSml,15,t2.c_str())/2,ly+102),Ac(WithA(IM_COL32(220,220,228,255),220)),t2.c_str()); }
            // 3 round controls (prev / play / next), centred at the bottom like the reference
            float by=ly+mH-38; float bxs=lx+colW*0.5f; float sp=64;
            struct Ctl{int kind;BYTE key;} ctls[3]={{1,VK_MEDIA_PREV_TRACK},{2,VK_MEDIA_PLAY_PAUSE},{3,VK_MEDIA_NEXT_TRACK}};
            for(int i=0;i<3;i++){ float cxb=bxs+(i-1)*sp; bool big=(i==1);
              float br=big?24:20; bool bh=fabsf(io.MousePos.x-cxb)<br&&fabsf(io.MousePos.y-by)<br;
              dl->AddCircleFilled(V(cxb,by),br,A(WithA(IM_COL32(210,210,225,255),bh?190:110),content));
              ImU32 gc=IM_COL32(245,245,250,255);
              if(ctls[i].kind==2){ if(g_md.playing){ dl->AddRectFilled(V(cxb-6,by-9),V(cxb-2,by+9),Ac(gc)); dl->AddRectFilled(V(cxb+2,by-9),V(cxb+6,by+9),Ac(gc)); }
                                   else dl->AddTriangleFilled(V(cxb-6,by-9),V(cxb-6,by+9),V(cxb+9,by),Ac(gc)); }
              else if(ctls[i].kind==1){ dl->AddRectFilled(V(cxb-8,by-8),V(cxb-6,by+8),Ac(gc)); dl->AddTriangleFilled(V(cxb+8,by-8),V(cxb+8,by+8),V(cxb-4,by),Ac(gc)); }
              else { dl->AddRectFilled(V(cxb+6,by-8),V(cxb+8,by+8),Ac(gc)); dl->AddTriangleFilled(V(cxb-8,by-8),V(cxb-8,by+8),V(cxb+4,by),Ac(gc)); }
              if(io.MouseClicked[0]&&bh) mkey(ctls[i].key); }
            ly+=mH; }
        }

        // ============================ RIGHT column (2x2 ring-icon tiles + notifications) ============
        { rx += (1.0f-stg(3))*54.0f;                              // slide in from the right
          float ly=y0+pad;
          float tileW=(colW-14)/2, gap=14;
          float gridH=tileW*2+gap;
          // stack grid + notif card, centred vertically like the left
          float notifH=ch2-2*pad-gridH-gap;
          ly=y0+pad;
          auto ringTile=[&](float tx,float ty,float frac,int icon,int ai){
              ImVec2 a0=V(tx,ty),b0=V(tx+tileW,ty+tileW); card(a0,b0,22);
              float cxx=tx+tileW*0.5f, cyy=ty+tileW*0.5f, rad=tileW*0.30f, th=5.0f;
              float f=std::clamp(frac,0.0f,1.0f)*fillAt(ai);                  // sweep-fill on open
              float a1=-2.618f, a2=a1+5.236f;
              dl->PathArcTo(V(cxx,cyy),rad,a1,a2,40); dl->PathStroke(Ac(CTRK),0,th);
              float ae=a1+5.236f*f;
              dl->PathArcTo(V(cxx,cyy),rad,a1,ae,40); dl->PathStroke(Ac(WithA(COL_GOLD,255)),0,th);
              if(f>0.02f) dl->AddCircleFilled(V(cxx+cosf(ae)*rad,cyy+sinf(ae)*rad),th*0.7f+pulse*1.2f,Ac(WithA(COL_GOLD,255)));  // glowing head
              ImU32 ic=Ac(CINK);
              if(icon==0){ dl->AddRect(V(cxx-9,cyy-9),V(cxx+9,cyy+9),ic,2,0,2.0f); dl->AddRectFilled(V(cxx-4,cyy-4),V(cxx+4,cyy+4),ic,1); // CPU chip
                  for(int k=-1;k<=1;k++){ dl->AddLine(V(cxx+k*6,cyy-13),V(cxx+k*6,cyy-9),ic,1.6f); dl->AddLine(V(cxx+k*6,cyy+9),V(cxx+k*6,cyy+13),ic,1.6f);
                      dl->AddLine(V(cxx-13,cyy+k*6),V(cxx-9,cyy+k*6),ic,1.6f); dl->AddLine(V(cxx+9,cyy+k*6),V(cxx+13,cyy+k*6),ic,1.6f); } }
              else if(icon==1){ dl->AddRectFilled(V(cxx-3,cyy-11),V(cxx+3,cyy+4),ic,3); dl->AddCircleFilled(V(cxx,cyy+7),6,ic); }  // thermometer
              else if(icon==2){ dl->AddRect(V(cxx-11,cyy-7),V(cxx+11,cyy+7),ic,2,0,2.0f); for(int k=-2;k<=2;k++) dl->AddLine(V(cxx+k*4,cyy-7),V(cxx+k*4,cyy+2),ic,1.4f); } // RAM
              else { dl->AddCircle(V(cxx,cyy),10,ic,0,2.0f); dl->AddCircleFilled(V(cxx,cyy),2.5f,ic); }  // disk
          };
          float memF=g_st.memTotal?(float)g_st.memUsed/g_st.memTotal:0;
          float diskF=g_st.diskTotal?(float)g_st.diskUsed/g_st.diskTotal:0;
          float tempF=std::clamp((float)(g_st.cpuTemp>0?g_st.cpuTemp:g_st.gpuTemp)/100.0f,0.0f,1.0f);
          ringTile(rx,        ly,          g_st.cpuUsage/100.0f, 0, 0);
          ringTile(rx+tileW+gap, ly,       tempF,                1, 1);
          ringTile(rx,        ly+tileW+gap,memF,                 2, 2);
          ringTile(rx+tileW+gap, ly+tileW+gap, diskF,            3, 3);
          ly+=gridH+gap;
          // notifications card (asymmetric bottom-right corner)
          std::vector<Notif> snap; { std::lock_guard<std::mutex> lk(g_notifMtx); snap=g_notifs; }
          int shown=std::min((int)snap.size(),(int)((notifH-44)/52));
          ImVec2 a0=V(rx,ly),b0=V(rx+colW,y0+ch2-pad);
          dl->AddRectFilled(a0,b0,A(WithA(CTILE,dk?250:255),content),18,ImDrawFlags_RoundCornersAll&~ImDrawFlags_RoundCornersBottomRight);
          dl->AddRectFilled(V(a0.x+(b0.x-a0.x)*0.45f,a0.y+(b0.y-a0.y)*0.45f),b0,A(WithA(CTILE,dk?250:255),content),30,ImDrawFlags_RoundCornersBottomRight);
          dl->AddRect(a0,b0,A(dk?IM_COL32(255,255,255,12):IM_COL32(0,0,0,14),content),18,ImDrawFlags_RoundCornersAll&~ImDrawFlags_RoundCornersBottomRight,1.0f);
          char hd[40]; snprintf(hd,40,"%d notification%s",(int)snap.size(),snap.size()==1?"":"s");
          TextAt(dl,mono,13,V(rx+18,ly+14),Ac(CINK2),hd);
          if(snap.empty()) TextAt(dl,mono,13,V(rx+18,ly+(b0.y-ly)*0.5f-8),Ac(WithA(CINK2,180)),"Unlock for notifications");
          for(int i=0;i<shown;i++){ float na=EaseOutCubic(std::clamp((el-0.25f-i*0.08f)/0.5f,0.0f,1.0f));   // cascade in
            float cy2=ly+40+i*52 + (1.0f-na)*18.0f; int aa=(int)(content*na*255);
            std::string ini=snap[i].app.empty()?std::string("!"):snap[i].app.substr(0,1); for(auto&ch:ini)ch=(char)toupper((unsigned char)ch);
            ID3D11ShaderResourceView* nic=nullptr;
            { std::lock_guard<std::mutex> lk(g_nIconMtx); auto it=g_nIcons.find(snap[i].aumid.empty()? snap[i].app : snap[i].aumid); if(it!=g_nIcons.end()) nic=it->second; }
            dl->AddCircleFilled(V(rx+32,cy2+18),15,WithA(COL_GOLD,(int)((dk?(nic?30:70):60)*(aa/255.0f))));
            if(nic) dl->AddImageRounded((ImTextureID)nic,V(rx+22,cy2+8),V(rx+42,cy2+28),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,aa),4.0f);
            else TextAt(dl,g_fMed,15,V(rx+32-TextW(g_fMed,15,ini.c_str())/2,cy2+9),WithA(COL_GOLD,aa),ini.c_str());
            float tx=rx+56, tw=colW-(56+16);
            TextAt(dl,mono,11,V(tx,cy2+4),WithA(CINK2,(int)(aa*0.86f)),Clip(mono,11,snap[i].app.empty()?std::string("Notification"):snap[i].app,tw).c_str());
            TextAt(dl,g_fMed,15,V(tx,cy2+20),WithA(CINK,aa),Clip(g_fMed,15,snap[i].title,tw).c_str()); }
        }
        dl->PopClipRect();

    } else {
    // ======================= FLOATING layout (the rice) =======================
    // A left rail of M3 blob widgets on the blur, a centred greeting over the password pill, and a
    // media card with the session buttons bottom-right. No container: the wallpaper IS the surface.
    const float rail = 150.0f;                      // rail column width
    float railX = W*0.055f, railTop = H*0.16f;
    float blobR = std::min(66.0f, rail*0.46f);
    // the rail: clock, calendar, weather - each a different M3 silhouette, like theirs
    { struct RB { int shape; } rbs[3] = { {M3_COOKIE7}, {M3_PENTAGON}, {M3_CIRCLE} };
      for(int i=0;i<3;i++){
        float sa=stg(i), rise=(1.0f-sa)*26.0f;
        ImVec2 c=V(railX+rail*0.5f, railTop + i*(blobR*2.0f+34.0f) + blobR + rise);
        float bspin=ShellPhase()*(0.07f+0.02f*i);
        int bA,bB; float bT;
        { const int cyc[4]={ rbs[i].shape, M3_COOKIE9, M3_SOFTBURST, M3_VERYSUNNY };
          M3MorphPick(7200+i, cyc, 4, 7.0f, bA, bB, bT); }
        M3ShapeMorph(dl,c,blobR,A(WithA(CTILE,dk?238:250),content*sa),bA,bB,bT,bspin);
        M3ShapeMorph(dl,c,blobR,A(dk?IM_COL32(255,255,255,10):IM_COL32(0,0,0,12),content*sa),bA,bB,bT,bspin);
        ImU32 ink=A(CINK,content*sa), ink2=A(CINK2,content*sa);
        if(i==0){
            // analogue hands, the way the reference draws its clock blob
            float hr=(lt.tm_hour%12)+lt.tm_min/60.0f, mi=lt.tm_min/60.0f;
            float ha2=hr*0.5236f-1.5708f, ma=mi*6.2832f-1.5708f;
            dl->AddLine(c,V(c.x+cosf(ha2)*blobR*0.36f,c.y+sinf(ha2)*blobR*0.36f),ink,3.4f);
            dl->AddLine(c,V(c.x+cosf(ma)*blobR*0.54f,c.y+sinf(ma)*blobR*0.54f),ink,2.6f);
            dl->AddCircleFilled(c,3.0f,ink);
        } else if(i==1){
            if(!ThemedSym(dl,V(c.x,c.y-blobR*0.30f),11.0f,ink,"view-calendar-symbolic"))
                dl->AddRect(V(c.x-11,c.y-blobR*0.30f-8),V(c.x+11,c.y-blobR*0.30f+9),ink,3,0,1.8f);
            char d1[40]; snprintf(d1,40,"%s %d %s",dnF[lt.tm_wday],lt.tm_mday,mnF[lt.tm_mon]);
            std::string ds=Clip(g_fSml,12,std::string(d1),blobR*1.5f);
            TextAt(dl,g_fSml,12,V(c.x-TextW(g_fSml,12,ds.c_str())*0.5f,c.y+blobR*0.05f),ink,ds.c_str());
            char d2[12]; snprintf(d2,12,"%d",lt.tm_year+1900);
            TextAt(dl,g_fSml,12,V(c.x-TextW(g_fSml,12,d2)*0.5f,c.y+blobR*0.32f),ink2,d2);
        } else {
            // moon + temperature, exactly the third blob in the video
            dl->AddCircleFilled(V(c.x-blobR*0.34f,c.y),blobR*0.22f,ink);
            dl->AddCircleFilled(V(c.x-blobR*0.22f,c.y-blobR*0.09f),blobR*0.20f,A(WithA(CTILE,dk?238:250),content*sa));
            char tp[16];
            if(g_wx.ok) snprintf(tp,16,"%d\302\260",(int)std::round(g_wx.temp)); else snprintf(tp,16,"--\302\260");
            TextAt(dl,g_fMed,24,V(c.x+blobR*0.02f,c.y-14),ink,tp);
        }
      } }

    // ---- centre: greeting over the password pill ----
    float ccx2=W*0.52f;
    { int hh=lt.tm_hour;
      const char* part = (hh<5)?"Good night":(hh<12)?"Good morning":(hh<18)?"Good afternoon":(hh<22)?"Good evening":"Good night";
      float sa=stg(1), rise=(1.0f-sa)*24.0f;
      char greet[64]; snprintf(greet,64,"%s,",part);
      float gw=g_fMed->CalcTextSizeA(30,FLT_MAX,0,greet).x;
      dl->AddText(g_fMed,30,V(ccx2-gw*0.5f,H*0.36f+rise),A(WithA(CINK2,230),content*sa),greet);
      std::string un = uname.empty()? std::string("welcome") : uname;
      float uw=g_fHuge->CalcTextSizeA(58,FLT_MAX,0,un.c_str()).x;
      dl->AddText(g_fHuge,58,V(ccx2-uw*0.5f,H*0.36f+40+rise),A(WithA(COL_GOLD,(int)(40+pulse*40)),content*sa),un.c_str());
      dl->AddText(g_fHuge,58,V(ccx2-uw*0.5f,H*0.36f+40+rise),A(CINK,content*sa),un.c_str());
      const char* hint = (g_helloOn? "Look at the camera, or type your password" : "Type your password to unlock");
      float hw=mono->CalcTextSizeA(14,FLT_MAX,0,hint).x;
      dl->AddText(mono,14,V(ccx2-hw*0.5f,H*0.36f+112+rise),A(WithA(CINK2,190),content*sa),hint); }

    // ---- the password pill ----
    { bool err=errNow;
      float sa=stg(3), rise=(1.0f-sa)*26.0f;
      float pw2=std::min(430.0f,W*0.26f), ph2=52;
      // Caelestia's lock surface grows out of a disc: the pill starts round and widens into place
      pw2 = ph2 + (pw2-ph2)*Cael::eval(Cael::EMPHASIZED_DECEL,std::clamp((content-0.12f)/0.55f,0.0f,1.0f));
      float pxx=ccx2+shakeX-pw2*0.5f, pyy=H*0.62f+rise;
      dl->AddRectFilled(V(pxx,pyy),V(pxx+pw2,pyy+ph2),A(WithA(CTILE,dk?232:248),content*sa),ph2*0.5f);
      dl->AddRect(V(pxx,pyy),V(pxx+pw2,pyy+ph2),
                  A(err?WithA(COL_ERR,230):WithA(COL_GOLD,(int)(70+pulse*60)),content*sa),ph2*0.5f,0,1.6f);
      float lkx=pxx+26, lky=pyy+ph2*0.5f;
      if(!ThemedSym(dl,V(lkx,lky+2),10.0f,A(WithA(COL_GOLD,220),content*sa),"system-lock-screen")){
          dl->AddRectFilled(V(lkx-6,lky-1),V(lkx+6,lky+9),A(WithA(COL_GOLD,220),content*sa),2);
          dl->PathArcTo(V(lkx,lky-1),5,3.1416f,6.2832f,10); dl->PathStroke(A(WithA(COL_GOLD,220),content*sa),0,2.0f); }
      int nch=(int)strlen(g_lockPw);
      if(nch==0 && !err){ const char* ph="Password";
          TextAt(dl,mono,15,V(ccx2+shakeX-mono->CalcTextSizeA(15,FLT_MAX,0,ph).x*0.5f,pyy+17),A(WithA(CINK2,200),content*sa),ph); }
      else if(err && nch==0){ const char* ph="Wrong \xE2\x80\x94 try again";
          TextAt(dl,mono,15,V(ccx2+shakeX-mono->CalcTextSizeA(15,FLT_MAX,0,ph).x*0.5f,pyy+17),A(WithA(COL_ERR,255),content*sa),ph); }
      else { float tot=std::min(nch,22)*15.0f, sx=ccx2+shakeX-tot*0.5f;
          for(int i=0;i<nch&&i<22;i++){ float pop=(i==nch-1)?1.0f+0.7f*s_dotPop:1.0f;
              dl->AddCircleFilled(V(sx+i*15+7,pyy+ph2*0.5f),4.5f*pop,A(CINK,content*sa)); } }
      // the submit arrow doubles as the hit target the shared input code reads
      ImVec2 ub=V(pxx+pw2-28,pyy+ph2*0.5f);
      g_lockUbh = fabsf(io.MousePos.x-ub.x)<20 && fabsf(io.MousePos.y-ub.y)<20;
      float ubr=16+HoverAnim(48801,g_lockUbh)*3.0f;
      dl->AddCircleFilled(ub,ubr,A(g_lockUbh?WithA(COL_GOLD,255):WithA(COL_GOLD,150),content*sa));
      ImU32 arrc=dk?IM_COL32(16,20,16,255):IM_COL32(250,254,252,255);
      dl->AddLine(V(ub.x-5,ub.y-5),V(ub.x+4,ub.y),A(arrc,content*sa),2.2f);
      dl->AddLine(V(ub.x-5,ub.y+5),V(ub.x+4,ub.y),A(arrc,content*sa),2.2f); }

    // ---- bottom-right: now playing, then the session row under it ----
    float cardW=std::min(360.0f,W*0.24f), cardH=112;
    float cx0=W-cardW-W*0.045f, cy0=H-cardH-118.0f;
    { float sa=stg(4), rise=(1.0f-sa)*28.0f; cy0+=rise;
      dl->AddRectFilled(V(cx0,cy0),V(cx0+cardW,cy0+cardH),A(WithA(CTILE,dk?238:250),content*sa),20);
      dl->AddRect(V(cx0,cy0),V(cx0+cardW,cy0+cardH),A(dk?IM_COL32(255,255,255,12):IM_COL32(0,0,0,14),content*sa),20,0,1.0f);
      bool hasM=g_md.has && !g_md.title.empty();
      std::string t1=hasM?Clip(g_fMed,15,g_md.title,cardW-32):std::string("Nothing playing");
      TextAt(dl,g_fMed,15,V(cx0+18,cy0+14),A(CINK,content*sa),t1.c_str());
      if(hasM && !g_md.artist.empty())
          TextAt(dl,g_fSml,13,V(cx0+18,cy0+34),A(CINK2,content*sa),Clip(g_fSml,13,g_md.artist,cardW-32).c_str());
      float bty=cy0+cardH-34, bcx0=cx0+cardW*0.5f;
      auto tb=[&](float bcx,int kind,BYTE key){
          bool h=fabsf(io.MousePos.x-bcx)<16&&fabsf(io.MousePos.y-bty)<16;
          ImU32 cc=A(h?WithA(COL_GOLD,255):CINK,content*sa);
          if(h) dl->AddCircleFilled(V(bcx,bty),15,A(WithA(CINK2,60),content*sa));
          if(kind==0){ dl->AddRectFilled(V(bcx-6,bty-6),V(bcx-4,bty+6),cc);
                       dl->AddTriangleFilled(V(bcx+6,bty-6),V(bcx+6,bty+6),V(bcx-3,bty),cc); }
          else if(kind==1){ if(g_md.playing){ dl->AddRectFilled(V(bcx-5,bty-7),V(bcx-1,bty+7),cc);
                                              dl->AddRectFilled(V(bcx+1,bty-7),V(bcx+5,bty+7),cc); }
                            else dl->AddTriangleFilled(V(bcx-5,bty-7),V(bcx-5,bty+7),V(bcx+7,bty),cc); }
          else { dl->AddRectFilled(V(bcx+4,bty-6),V(bcx+6,bty+6),cc);
                 dl->AddTriangleFilled(V(bcx-6,bty-6),V(bcx-6,bty+6),V(bcx+3,bty),cc); }
          // mkey lives inside the Panel branch, so send the key straight from here
          if(io.MouseClicked[0]&&h&&content>0.8f){
              keybd_event(key,0,0,0); keybd_event(key,0,KEYEVENTF_KEYUP,0); } };
      tb(bcx0-46,0,VK_MEDIA_PREV_TRACK); tb(bcx0,1,VK_MEDIA_PLAY_PAUSE); tb(bcx0+46,2,VK_MEDIA_NEXT_TRACK); }

    // sign out / restart / shut down, as three pills - the row along the bottom of the video
    { float sa=stg(5), rise=(1.0f-sa)*24.0f;
      const char* sl[3]={"Sign out","Restart","Shut down"};
      const int   si[3]={2,3,4};                       // indices into SessIcon's five
      float bw=cardW/3.0f-6, bh=40, by=cy0+cardH+16+rise;
      for(int i=0;i<3;i++){
          float bx=cx0+i*(bw+9);
          bool h=io.MousePos.x>bx&&io.MousePos.x<bx+bw&&io.MousePos.y>by&&io.MousePos.y<by+bh;
          float ha=HoverAnim(48810+i,h);
          dl->AddRectFilled(V(bx,by),V(bx+bw,by+bh),
                            A(h?WithA(COL_GOLD,(int)(60+ha*90)):WithA(CTILE,dk?226:246),content*sa),bh*0.5f);
          SessIcon(dl,si[i],V(bx+20,by+bh*0.5f),A(h?WithA(COL_GOLD,255):CINK2,content*sa),
                   A(WithA(CTILE,255),content*sa));
          std::string lb=Clip(g_fSml,12,std::string(sl[i]),bw-40);
          TextAt(dl,g_fSml,12,V(bx+36,by+bh*0.5f-7),A(h?WithA(COL_GOLD,255):CINK2,content*sa),lb.c_str());
          if(io.MouseClicked[0]&&h&&content>0.8f){
              if(i==0) ExitWindowsEx(EWX_LOGOFF,0);
              else if(i==1) AetherShellExec(nullptr,L"open",L"shutdown.exe",L"/r /t 0",nullptr,SW_HIDE);
              else AetherShellExec(nullptr,L"open",L"shutdown.exe",L"/s /t 0",nullptr,SW_HIDE); } } }
    }

    // ---- input: type into g_lockPw, Backspace, Enter/arrow-button to submit ----
    // ---- Windows Hello: consume whatever the worker settled on ----
    {   int hs=g_helloState.load();
        if(hs==HELLO_OK){ g_lockShow=false; g_lockPw[0]=0; g_lockFails=0; g_helloState.store(HELLO_IDLE); }
        else if(hs==HELLO_FAIL) g_helloState.store(HELLO_IDLE);   // the message is already staged
    }
    bool ready = GetTickCount64() > g_lockOpenAt+250;   // ignore the keystroke that opened it
    // Offer Hello ONCE per lock, a beat after the screen settles - Windows does the same, and
    // firing it on the opening frame would race the overlay's own fade-in for the foreground.
    if(g_helloOn && !g_helloNoAuto && !g_helloBroken.load() && g_helloAvail.load()==1 && g_helloState.load()==HELLO_IDLE
       && GetTickCount64() > g_lockOpenAt+900){
        static ULONGLONG autoFor=0;
        if(autoFor!=g_lockOpenAt){ autoFor=g_lockOpenAt; HelloVerify(g_lockHwnd); }
    }
    if(ready && !g_helloNoAuto && !g_helloBroken.load() && g_helloState.load()==HELLO_IDLE && io.MouseClicked[0] && g_lockHelloBh) HelloVerify(g_lockHwnd);
    if(ready){
        int len=(int)strlen(g_lockPw);
        for(int i=0;i<io.InputQueueCharacters.Size;i++){ ImWchar c=io.InputQueueCharacters[i];
            if(c>=32&&c<127&&len<(int)sizeof(g_lockPw)-1){ g_lockPw[len++]=(char)c; g_lockPw[len]=0; } }
        if(ImGui::IsKeyPressed(ImGuiKey_Backspace)&&len>0){ g_lockPw[--len]=0; }
        bool submit = ImGui::IsKeyPressed(ImGuiKey_Enter)||ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)
                    || (io.MouseClicked[0]&&g_lockUbh);
        if(submit){
            bool codeSet = g_lockCode[0]!=0;
            bool ok = (codeSet && strcmp(g_lockPw,g_lockCode)==0)   // shell unlock code (any account type)
                    || LockValidatePw(g_lockPw);                    // or the real Windows password
            if(ok){ g_lockShow=false; g_lockPw[0]=0; g_lockFails=0; }
            else { g_lockFails++; g_lockPw[0]=0; g_lockErrUntil=GetTickCount64()+1600; }
        }
    }
    g_lockRect = RECT{0,0,(LONG)W,(LONG)H};   // eats all input while up
}
