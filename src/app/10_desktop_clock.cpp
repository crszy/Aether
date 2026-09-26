// Aether - the desktop layer: clock, wallpaper, desktop widgets.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =============================================================================================
// DESKTOP CLOCK  (<- Caelestia modules/background, the bottom-right block in the reference rice)
//   09:21 PM │ MAY
//            │ 04
//            │ Monday
// Heavy geometric digits with the hour brighter than the minute, a square-dot colon, a small
// meridiem hung off the top, a hairline rule, then the date stacked to its right. All greyscale:
// this lives ON the wallpaper, so tinting it with the accent would make it read as chrome.
// =============================================================================================
static void DrawDesktopClock(ImDrawList* dl,float x0,float y0,float x1,float y1){
    (void)y0;
    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
    int hh = g_clock24 ? lt.tm_hour : (((lt.tm_hour%12)==0)?12:lt.tm_hour%12);
    char sH[8],sM[8],sD[8]; snprintf(sH,8,"%02d",hh); snprintf(sM,8,"%02d",lt.tm_min);
    snprintf(sD,8,"%02d",lt.tm_mday);
    static const char* MON[12]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
    static const char* DOW[7] ={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
    const char* ap = g_clock24? "" : (lt.tm_hour<12?"AM":"PM");

    ImFont* fb = g_fClock? g_fClock : g_fHuge;      // heavy face for the numerals
    // Sizes are RENDERED pixel heights measured off the reference rice. TextAt multiplies by the
    // user's text scale, so divide it out here — the wallpaper clock is a fixed-size ornament, not
    // body copy that should shrink when someone dials the UI text down.
    const float S = 1.0f/std::max(0.5f,g_textScale);
    const float BIG=108.0f*S, MER=24.0f*S, MO=26.0f*S, DAY=46.0f*S, DOWSZ=23.0f*S;
    // greys — the wallpaper shows through everything, so these are white at varying strength
    ImU32 cHour = IM_COL32(255,255,255,222), cMin = IM_COL32(255,255,255,150);
    ImU32 cCol  = IM_COL32(255,255,255,130), cMer = IM_COL32(255,255,255,178);
    ImU32 cRule = IM_COL32(255,255,255,120), cMon = IM_COL32(255,255,255,205);
    ImU32 cDay  = IM_COL32(255,255,255,222), cDow = IM_COL32(255,255,255,150);
    if(!g_darkUI){   // on a light scheme the same block reads as dark ink instead
        cHour=IM_COL32(16,20,24,208); cMin=IM_COL32(16,20,24,140); cCol=IM_COL32(16,20,24,120);
        cMer =IM_COL32(16,20,24,168); cRule=IM_COL32(16,20,24,110); cMon=IM_COL32(16,20,24,196);
        cDay =IM_COL32(16,20,24,208); cDow =IM_COL32(16,20,24,140);
    }
    // ---- measure so the whole block can be right-aligned inside the bubble ----
    float wH=TextW(fb,BIG,sH), wM=TextW(fb,BIG,sM);
    float colW=BIG*0.34f;                                     // the square-dot colon column
    float wMer= *ap? TextW(fb,MER,ap)+10.0f : 0.0f;
    float wMon=TextW(fb,MO,MON[lt.tm_mon]), wDay=TextW(fb,DAY,sD), wDow=TextW(g_fReg,DOWSZ,DOW[lt.tm_wday]);
    float dateW=std::max(wMon,std::max(wDay,wDow));
    float ruleGap=26.0f;
    float total = wH+colW+wM+wMer + ruleGap + 2.0f + ruleGap*0.85f + dateW;
    float marginR=46.0f, marginB=72.0f;
    float bx = x1-marginR-total;
    float baseY = y1-marginB;                                  // baseline-ish anchor of the digit row
    if(bx < x0+20.0f) return;                                  // no room on a tiny screen: skip it
    float digTop = baseY-BIG*0.86f;

    // ---- 09 : 21 ----
    float cx=bx;
    TextAt(dl,fb,BIG,V(cx,digTop),cHour,sH); cx+=wH;
    { float d=BIG*0.077f, mid=digTop+BIG*0.44f;                // colon = two rounded squares
      float ccx=cx+colW*0.5f;
      dl->AddRectFilled(V(ccx-d,mid-BIG*0.155f-d),V(ccx+d,mid-BIG*0.155f+d),cCol,d*0.5f);
      dl->AddRectFilled(V(ccx-d,mid+BIG*0.115f-d),V(ccx+d,mid+BIG*0.115f+d),cCol,d*0.5f); }
    cx+=colW;
    TextAt(dl,fb,BIG,V(cx,digTop),cMin,sM); cx+=wM;
    // cap-top alignment: TextAt takes the glyph-BOX top, and the box is ~20% leading above the cap,
    // so a smaller run has to be pushed down by 20% of the size difference to line its caps up with
    // the digits (otherwise "PM" and the date float above the numerals).
    if(*ap){ TextAt(dl,fb,MER,V(cx+10.0f,digTop+(BIG-MER)*0.20f),cMer,ap); cx+=wMer; }

    // ---- hairline rule ----
    float rx=cx+ruleGap;
    dl->AddRectFilled(V(rx,digTop+BIG*0.03f),V(rx+2.0f,baseY+BIG*0.06f),cRule,1.0f);

    // ---- MAY / 04 / Monday ----
    float dx=rx+2.0f+ruleGap*0.85f, dy=digTop+(BIG-MO)*0.20f;
    TextAt(dl,fb,MO,V(dx,dy),cMon,MON[lt.tm_mon]);            dy+=MO*1.30f;
    TextAt(dl,fb,DAY,V(dx,dy),cDay,sD);                       dy+=DAY*1.12f;
    TextAt(dl,g_fReg,DOWSZ,V(dx,dy),cDow,DOW[lt.tm_wday]);
}

// UVs for the desktop wallpaper inside a box given in the desk layer's logical coordinates. Normally a
// cover-fit of the box; in Span mode the image is cover-fitted to the whole virtual screen (as Windows
// does) and the box takes its piece of it.
static void DeskCoverUV(int tw,int th,float bx,float by,float bw,float bh,ImVec2& uv0,ImVec2& uv1){
    if(!g_wallSpan || tw<=0 || th<=0){ CoverUV(tw,th,bw,bh,uv0,uv1); return; }
    const float VW=(float)(g_dvs.right-g_dvs.left), VH=(float)(g_dvs.bottom-g_dvs.top);
    if(VW<1||VH<1){ CoverUV(tw,th,bw,bh,uv0,uv1); return; }
    ImVec2 v0,v1; CoverUV(tw,th,VW,VH,v0,v1);                    // the image over the virtual screen
    // the box in physical screen pixels, then as a fraction of the virtual screen
    float sx0=(float)g_vs.left+bx*g_uiScale-(float)g_dvs.left, sy0=(float)g_vs.top+by*g_uiScale-(float)g_dvs.top;
    float fx0=sx0/VW, fy0=sy0/VH, fx1=(sx0+bw*g_uiScale)/VW, fy1=(sy0+bh*g_uiScale)/VH;
    uv0=ImVec2(v0.x+fx0*(v1.x-v0.x), v0.y+fy0*(v1.y-v0.y));
    uv1=ImVec2(v0.x+fx1*(v1.x-v0.x), v0.y+fy1*(v1.y-v0.y));
}
static void WrapLines(ImFont* f,float sz,const std::string& in,float w,int maxLines,std::vector<std::string>& out);   // fwd
static ImU32 CwColor(const std::string& s0,ImU32 def);   // fwd
#include "src/modules/desktop/DesktopMusic.h"   // desktop lyrics + background visualiser
static void DrawDeskMon(ImDrawList* dl,int mi,float L,float T,float MW,float MH){
    if(!DeskOnMon(mi)) return;        // display switched off: leave it to Windows, draw nothing
    float R=L+MW, B=T+MH;
    int il,it,ir,ib; BubbleInsetsFor(mi,il,it,ir,ib);     // the bar's edge gets the wide margin
    float x0=L+(float)il, y0=T+(float)it, x1=R-(float)ir, y1=B-(float)ib;
    float rnd=g_bubble?g_bubbleRound:0.0f;
    const WeTrans* WT = (mi>=0 && mi<16 && g_weTr[mi].phase)? &g_weTr[mi] : nullptr;
    const bool weHold = WT && !WT->scrim && WT->phase==2;                 // a still image held while the live one loads
    const bool monLive = mi>=0 && mi<16 && g_monLive[mi] && !weHold;     // a live wallpaper is drawing on THIS screen
    const bool live = (g_bubble && g_deskLive && DesktopHostPresent()) || monLive;   // transparent interior => the animated wallpaper shows through; static-image fallback when explorer/WE host is gone (else black)
    if(monLive && !g_bubble) return;          // no bubble: the live wallpaper gets the whole screen
    // ---- the surround: frosted glass made from the wallpaper itself, so the area outside the
    // "display" blends with the taskbar/panels instead of reading as a flat mat ----
    if(live){
        DrawLiveBubbleFrame(dl,L,T,R,B,x0,y0,x1,y1,rnd, monLive? (int)(std::clamp(g_liveFrameAlpha,0.0f,1.0f)*255) : 255);
        // popout notch still carves over the live wallpaper (drawn below); plugins still paint on it.
    } else {
    dl->AddRectFilled(V(L,T),V(R,B),COL_DESKBG);
    if(g_deskGlow && g_bubble && g_deskFrost){
        ImVec2 uv0,uv1; DeskCoverUV(g_deskFrostW,g_deskFrostH,L,T,MW,MH,uv0,uv1);
        dl->AddImage((ImTextureID)g_deskFrost,V(L,T),V(R,B),uv0,uv1,IM_COL32(255,255,255,255));
        // knock it back toward the panel colour, but keep enough of the wallpaper's own colour
        // showing that the margin reads as frosted glass rather than a black mat
        dl->AddRectFilled(V(L,T),V(R,B), g_darkUI?IM_COL32(12,14,18,118):IM_COL32(238,243,241,120));
        dl->AddRectFilled(V(L,T),V(R,B), WithA(COL_DESKBG,64));
        dl->AddRectFilled(V(L,T),V(R,B), WithA(COL_GOLD,g_darkUI?16:10));   // faint accent wash
        // vignette: darker toward the screen edges -> the surround curves away from the viewer
        for(int i=0;i<16;i++){ float e=i*5.0f;
            dl->AddRect(V(L+e,T+e),V(R-e,B-e), IM_COL32(0,0,0,7), 0,0, 6.0f); }
    }
    if(g_bubble){   // recessed well: the desktop sits INSIDE the glass, not on top of it
        if(g_deskGlow)
            for(int i=9;i>0;i--){ float e=i*2.2f;
                dl->AddRect(V(x0-e,y0-e),V(x1+e,y1+e),IM_COL32(0,0,0,11),rnd+e,0,e*0.9f); }
        // bevel around the opening: light on the bottom/right lip, shadow on the top/left
        dl->AddRect(V(x0-2.5f,y0-2.5f),V(x1+2.5f,y1+2.5f),
                    g_darkUI?IM_COL32(255,255,255,g_panelOutline?26:0):IM_COL32(255,255,255,g_panelOutline?110:0),rnd+2.5f,0,2.0f);
        if(g_panelOutline)
            dl->AddRect(V(x0-0.5f,y0-0.5f),V(x1+0.5f,y1+0.5f),IM_COL32(0,0,0,90),rnd,0,1.4f);
    }
    float bw=x1-x0,bh=y1-y0;
    // The bubble corner is a 36px arc against a hard, high-contrast edge, and ImGui's default 1px
    // AA fringe leaves it visibly stepped - measured across the arc, some scanlines crossed from
    // surround straight to wallpaper with NO blended pixel at all. _FringeScale widens the fringe
    // for this one shape only. (Tessellation was not the cause: raising the arc table from 48 to
    // 128 samples moved those same pixels by at most 10 levels out of 255.)
    const float deskFringe = dl->_FringeScale;
    dl->_FringeScale = 2.8f;
    auto blit=[&](ID3D11ShaderResourceView* t,int tw,int th){
        ImVec2 uv0,uv1; DeskCoverUV(tw,th,x0,y0,bw,bh,uv0,uv1);
        dl->AddImageRounded((ImTextureID)t,V(x0,y0),V(x1,y1),uv0,uv1,IM_COL32(255,255,255,255),rnd); };
    // The settled image, with every running transition layered over it below.
    { int bw2=0,bh2=0; ID3D11ShaderResourceView* base = g_deskBase? g_deskBase : nullptr;
      if(base){ bw2=g_deskBaseW; bh2=g_deskBaseH; }
      if(weHold && WT->L.under){ base=WT->L.under; bw2=WT->L.uw; bh2=WT->L.uh; }   // this screen's own starting image
      else if(g_deskWall&&g_deskWallW>0){ base=g_deskWall; bw2=g_deskWallW; bh2=g_deskWallH; }
      if(base) blit(base,bw2,bh2);
      else dl->AddRectFilled(V(x0,y0),V(x1,y1),Mix(COL_DESKBG,COL_CARD,0.25f),rnd); }
    dl->_FringeScale = deskFringe;

    // ---- transition engine: each layer reveals its image over whatever is already composited ----
    static std::vector<WipeLayer> s_layers;
    s_layers.assign(g_wipeStack.begin(),g_wipeStack.end());
    if(weHold && WT->L.tex){ if(WT->toImage) s_layers.clear(); s_layers.push_back(WT->L); }
    for(size_t wli=0; wli<s_layers.size(); ++wli){
        const WipeLayer& WL=s_layers[wli];
        if(!WL.tex) continue;
        double t=std::clamp((double)WL.prog,0.0,1.0);
        const WTrans   g_transNow = WL.kind;          // shadow the globals: this layer's own values
        const ImVec2   g_wipeAt   = WL.at;
        ID3D11ShaderResourceView* const g_deskWall     = WL.tex;
        const int g_deskWallW=WL.tw, g_deskWallH=WL.th;
        ID3D11ShaderResourceView* const g_deskWallOld  = WL.under;
        const int g_deskWallOldW=WL.uw, g_deskWallOldH=WL.uh;
        (void)g_deskWallOldW; (void)g_deskWallOldH;
        float maxR=0; ImVec2 corners[4]={V(x0,y0),V(x1,y0),V(x0,y1),V(x1,y1)};
        for(auto&p:corners){ float d=sqrtf((p.x-g_wipeAt.x)*(p.x-g_wipeAt.x)+(p.y-g_wipeAt.y)*(p.y-g_wipeAt.y)); maxR=std::max(maxR,d); }
        // draw a texture as a circle, with UVs remapped out of the bubble's cover-fit mapping
        auto circleTex=[&](ID3D11ShaderResourceView* tex,int tw,int th,float cxp,float cyp,float R,int alpha){
            if(R<1||!tex) return;
            ImVec2 uv0,uv1; DeskCoverUV(tw,th,x0,y0,bw,bh,uv0,uv1);
            ImVec2 a=V(cxp-R,cyp-R), b=V(cxp+R,cyp+R);
            auto ux=[&](float x){ return uv0.x+(x-x0)/bw*(uv1.x-uv0.x); };
            auto uy=[&](float y){ return uv0.y+(y-y0)/bh*(uv1.y-uv0.y); };
            dl->AddImageRounded((ImTextureID)tex,a,b,ImVec2(ux(a.x),uy(a.y)),ImVec2(ux(b.x),uy(b.y)),
                                IM_COL32(255,255,255,alpha),R);
        };
        // draw the new wallpaper clipped to an arbitrary rect
        auto rectNew=[&](float rx0,float ry0,float rx1,float ry1,float ox,float oy,int alpha){
            if(rx1<=rx0||ry1<=ry0) return;
            ImVec2 uv0,uv1; DeskCoverUV(g_deskWallW,g_deskWallH,x0,y0,bw,bh,uv0,uv1);
            dl->PushClipRect(V(rx0,ry0),V(rx1,ry1),true);
            dl->AddImageRounded((ImTextureID)g_deskWall,V(x0+ox,y0+oy),V(x1+ox,y1+oy),uv0,uv1,
                                IM_COL32(255,255,255,alpha),rnd);
            dl->PopClipRect();
        };
        dl->PushClipRect(V(x0,y0),V(x1,y1),true);
        switch(g_transNow){
        case WTrans::Fade:
            rectNew(x0,y0,x1,y1,0,0,(int)(E3io(t)*255)); break;
        case WTrans::Grow:
            circleTex(g_deskWall,g_deskWallW,g_deskWallH,g_wipeAt.x,g_wipeAt.y,(float)(E3o(t)*maxR),255); break;
        case WTrans::Outer: {                      // new everywhere, old shrinking to a point
            rectNew(x0,y0,x1,y1,0,0,255);
            float R=(float)((1.0-E3io(t))*maxR);
            circleTex(g_deskWallOld,g_deskWallOldW,g_deskWallOldH,g_wipeAt.x,g_wipeAt.y,R,255); } break;
        case WTrans::WipeLeft:  rectNew(x0,y0,x0+bw*(float)E3io(t),y1,0,0,255); break;
        case WTrans::WipeRight: rectNew(x1-bw*(float)E3io(t),y0,x1,y1,0,0,255); break;
        case WTrans::WipeDown:  rectNew(x0,y0,x1,y0+bh*(float)E3io(t),0,0,255); break;
        case WTrans::WipeUp:    rectNew(x0,y1-bh*(float)E3io(t),x1,y1,0,0,255); break;
        case WTrans::SlideLeft: { float e=(float)E3io(t); rectNew(x0,y0,x1,y1, bw*(1-e),0,255); } break;
        case WTrans::SlideRight:{ float e=(float)E3io(t); rectNew(x0,y0,x1,y1,-bw*(1-e),0,255); } break;
        case WTrans::SlideUp:   { float e=(float)E3io(t); rectNew(x0,y0,x1,y1,0, bh*(1-e),255); } break;
        case WTrans::SlideDown: { float e=(float)E3io(t); rectNew(x0,y0,x1,y1,0,-bh*(1-e),255); } break;
        case WTrans::Zoom: {                       // scales up from the origin while fading in
            float e=(float)E3io(t); float s=0.6f+0.4f*e;
            ImVec2 uv0,uv1; DeskCoverUV(g_deskWallW,g_deskWallH,x0,y0,bw,bh,uv0,uv1);
            float cxp=g_wipeAt.x, cyp=g_wipeAt.y;
            ImVec2 a=V(cxp+(x0-cxp)*s, cyp+(y0-cyp)*s), b=V(cxp+(x1-cxp)*s, cyp+(y1-cyp)*s);
            dl->AddImageRounded((ImTextureID)g_deskWall,a,b,uv0,uv1,IM_COL32(255,255,255,(int)(e*255)),rnd); } break;
        case WTrans::Diagonal: {                   // sweeping line, approximated by thin columns
            const int N=48; float cw=bw/N; double dp=t*(bw+bh);
            for(int i=0;i<N;i++){ float cxp=x0+i*cw;
                float h=(float)std::clamp(dp-(cxp-x0),0.0,(double)bh);
                if(h>0.5f) rectNew(cxp,y0,cxp+cw+1,y0+h,0,0,255); } } break;
        case WTrans::Ripple: {                     // three concentric fronts, staggered
            for(int r=0;r<3;r++){ double st=r*0.22; if(t<st) continue;
                float R=(float)(E3o(std::min(1.0,(t-st)/(1.0-st)))*maxR);
                circleTex(g_deskWall,g_deskWallW,g_deskWallH,g_wipeAt.x,g_wipeAt.y,R,255); } } break;
        case WTrans::Stripes: {                    // ten columns, alternating delay
            const int N=10; float cw=bw/N;
            for(int i=0;i<N;i++){ double dd=(i%2)*0.18;
                double sp=std::clamp((t-dd)/(1.0-dd),0.0,1.0); float h=(float)(E3o(sp)*bh);
                if(h>0.5f) rectNew(x0+i*cw,y0,x0+i*cw+cw+1,y0+h,0,0,255); } } break;
        default:                                    // None: instant
            rectNew(x0,y0,x1,y1,0,0,255); break;
        }
        // a fading accent ring rides the front of the radial transitions
        if(g_transNow==WTrans::Grow||g_transNow==WTrans::Ripple||g_transNow==WTrans::Any)
            dl->AddCircle(g_wipeAt,(float)(E3o(t)*maxR),AccA((int)(170*(1.0-t))),96,3.0f);
        dl->PopClipRect();
    }
    }  // end static (non-live) rendering
    // the scrim: the bubble interior fading out and back in across a live switch
    if(WT && WT->scrim && g_liveTransStyle==0){
        // stripes in the incoming wallpaper's colours sweep over, hold with Loading!!!!!! while Wallpaper Engine
        // swaps underneath, and sweep away off the new wallpaper
        float f=std::clamp(WT->fade,0.0f,1.0f);
        float pin  = WT->phase==1? f : 1.0f;
        float pout = WT->phase==3? 1.0f-f : 0.0f;
        float textA = WT->phase==2? 1.0f : WT->phase==1? std::clamp((f-0.65f)/0.35f,0.0f,1.0f) : std::clamp((f-0.55f)/0.45f,0.0f,1.0f);
        bool wc=g_stripeLiveWallColors && WT->npal>0;
        DrawStripeCover(dl,V(x0,y0),V(x1,y1),pin,pout,wc? WT->pal : nullptr,wc? WT->npal : 0,1.0f,textA);
    }
    else if(WT && WT->scrim){
        float a = (WT->phase==2 || (!live && WT->phase<3))? 1.0f : std::clamp(WT->fade,0.0f,1.0f);
        if(a>0.002f){ ImU32 sc=COL_DESKBG;
            dl->AddRectFilled(V(x0,y0),V(x1,y1),WithA(sc,(int)(a*255)),rnd);
            if(g_deskFrost){ ImVec2 uv0,uv1; DeskCoverUV(g_deskFrostW,g_deskFrostH,x0,y0,x1-x0,y1-y0,uv0,uv1);
                dl->AddImageRounded((ImTextureID)g_deskFrost,V(x0,y0),V(x1,y1),uv0,uv1,IM_COL32(255,255,255,(int)(a*110)),rnd); } }
    }
    // hand-over, last step: the held still image fading off the now-live wallpaper
    if(live && WT && !WT->scrim && WT->phase==3 && WT->L.tex){
        ImVec2 uv0,uv1; DeskCoverUV(WT->L.tw,WT->L.th,x0,y0,x1-x0,y1-y0,uv0,uv1);
        dl->AddImageRounded((ImTextureID)WT->L.tex,V(x0,y0),V(x1,y1),uv0,uv1,
                            IM_COL32(255,255,255,(int)(std::clamp(WT->fade,0.0f,1.0f)*255)),rnd);
    }
    // ---- carve the bar-popout NOTCH out of the desktop (BOTH live & static): fill the popout's rect with
    // the surround colour, rounded on the wallpaper side, so the desktop curves around the popout and it
    // reads as the shell CUTTING INTO the desktop rather than a card floating on top.
    if(g_flyCut && g_flyR>x0 && g_flyL<x1 && g_flyB>y0 && g_flyT<y1){
        dl->PushClipRect(V(x0-1,y0-1),V(x1+1,y1+1),true);
        for(int i=9;i>0;i--){ float e=i*2.2f;
            dl->AddRect(V(g_flyL,g_flyT-e),V(g_flyR+e,g_flyB+e),IM_COL32(0,0,0,11),g_flyRad+e,ImDrawFlags_RoundCornersRight,e*0.9f); }
        dl->AddRectFilled(V(g_flyL,g_flyT),V(g_flyR,g_flyB),COL_DESKBG,g_flyRad,ImDrawFlags_RoundCornersRight);
        dl->AddRect(V(g_flyL,g_flyT-0.5f),V(g_flyR+0.5f,g_flyB+0.5f),IM_COL32(0,0,0,90),g_flyRad,ImDrawFlags_RoundCornersRight,1.4f);
        dl->AddRect(V(g_flyL,g_flyT-2.5f),V(g_flyR+2.5f,g_flyB+2.5f),g_darkUI?IM_COL32(255,255,255,22):IM_COL32(255,255,255,90),g_flyRad+2.5f,ImDrawFlags_RoundCornersRight,2.0f);
        dl->PopClipRect();
    }
    // ---- app-preview NOTCH (same treatment) so the window thumbnail also reads as cut INTO the desktop ----
    if(g_thumbCut && g_thumbR>x0 && g_thumbL<x1 && g_thumbB>y0 && g_thumbT<y1){
        dl->PushClipRect(V(x0-1,y0-1),V(x1+1,y1+1),true);
        for(int i=9;i>0;i--){ float e=i*2.2f;
            dl->AddRect(V(g_thumbL,g_thumbT-e),V(g_thumbR+e,g_thumbB+e),IM_COL32(0,0,0,11),g_thumbRad+e,ImDrawFlags_RoundCornersRight,e*0.9f); }
        dl->AddRectFilled(V(g_thumbL,g_thumbT),V(g_thumbR,g_thumbB),COL_DESKBG,g_thumbRad,ImDrawFlags_RoundCornersRight);
        dl->AddRect(V(g_thumbL,g_thumbT-0.5f),V(g_thumbR+0.5f,g_thumbB+0.5f),IM_COL32(0,0,0,90),g_thumbRad,ImDrawFlags_RoundCornersRight,1.4f);
        dl->AddRect(V(g_thumbL,g_thumbT-2.5f),V(g_thumbR+2.5f,g_thumbB+2.5f),g_darkUI?IM_COL32(255,255,255,22):IM_COL32(255,255,255,90),g_thumbRad+2.5f,ImDrawFlags_RoundCornersRight,2.0f);
        dl->PopClipRect();
    }
    // ---- in-bar app-preview NOTCH (same treatment) so the captured-window preview reads as cut INTO the desktop ----
    if(g_prevCut && g_prevR>x0 && g_prevL<x1 && g_prevB>y0 && g_prevT<y1){
        dl->PushClipRect(V(x0-1,y0-1),V(x1+1,y1+1),true);
        for(int i=9;i>0;i--){ float e=i*2.2f;
            dl->AddRect(V(g_prevL,g_prevT-e),V(g_prevR+e,g_prevB+e),IM_COL32(0,0,0,11),g_prevRad+e,ImDrawFlags_RoundCornersRight,e*0.9f); }
        dl->AddRectFilled(V(g_prevL,g_prevT),V(g_prevR,g_prevB),COL_DESKBG,g_prevRad,ImDrawFlags_RoundCornersRight);
        dl->AddRect(V(g_prevL,g_prevT-0.5f),V(g_prevR+0.5f,g_prevB+0.5f),IM_COL32(0,0,0,90),g_prevRad,ImDrawFlags_RoundCornersRight,1.4f);
        dl->AddRect(V(g_prevL,g_prevT-2.5f),V(g_prevR+2.5f,g_prevB+2.5f),g_darkUI?IM_COL32(255,255,255,22):IM_COL32(255,255,255,90),g_prevRad+2.5f,ImDrawFlags_RoundCornersRight,2.0f);
        dl->PopClipRect();
    }
    // Exactly ONE monitor carries the desktop furniture. `mi==0 || primary` looked like that but is
    // an OR: on a two-screen setup whose monitor 0 is not the primary, both screens satisfy it and
    // the clock, the widgets and the desktop plugins are all drawn twice.
    bool canvasMon;
    { int pi=0;
      for(size_t k=0;k<g_mons.size();k++) if(g_mons[k].primary){ pi=(int)k; break; }
      canvasMon = g_mons.empty() || mi==pi; }

    // ---- desktop clock (Caelestia's background/DesktopClock) ---------------------------------
    // The signature bottom-right block from the reference rice: a heavy "09:21" with a small
    // superscript meridiem, a hairline rule, then MAY / 04 / Monday stacked beside it. Greyscale,
    // never the accent — it sits on the wallpaper, so it reads as part of the photo, not the UI.
    if(g_deskClock && canvasMon)
        DrawDesktopClock(dl,x0,y0,x1,y1);
    if((g_deskLyrics||g_deskViz) && canvasMon)
        DrawDesktopMusic(dl,mi,x0,y0,x1,y1);
    // ---- desktop widget canvas ----------------------------------------------------------------
    // The rice has its clock, spectrum and fetch cards floating free on the wallpaper. Rather than a
    // SECOND widget system with its own editor, one dashboard tab is mirrored here: same widgets,
    // same normalised layout, still edited with the drawer's existing drag/resize editor. Which tab
    // is Settings > Taskbar > "Widgets on the wallpaper".
    if(canvasMon && !g_deskTabName.empty()){
        for(auto& t:g_tabs){
            if(t.name!=g_deskTabName) continue;
            const float ins=30.0f*g_uiScale;
            ImVec2 o=V(x0+ins,y0+ins);
            ImVec2 ar=V((x1-x0)-ins*2.0f,(y1-y0)-ins*2.0f);
            if(ar.x>40.0f && ar.y>40.0f){
                ImGuiIO& dio=ImGui::GetIO();
                for(auto& w:t.widgets){
                    // full-page widgets would paper over the whole wallpaper; they belong in the drawer
                    if(w.kind>=WK_PAGE_DASH && w.kind<=WK_PAGE_WEATHER) continue;
                    ImVec2 wo=V(o.x+w.x*ar.x, o.y+w.y*ar.y);
                    ImVec2 ws=V(std::max(20.0f,w.w*ar.x), std::max(20.0f,w.h*ar.y));
                    DrawWidget(dl,dio,w,wo,ws);
                }
            }
            break;
        }
    }
    // desktop-surface plugins paint on the wallpaper, inside the bubble — on the primary monitor
    // only, so a widget is not duplicated across every screen
    if(canvasMon)
        PluginsDraw(dl,PSURF_DESKTOP,x0,y0,x1-x0,y1-y0);
}
// The desk window spans the whole VIRTUAL SCREEN, so every monitor gets a desktop.
static void DrawDesk(){
    ImDrawList* dl=ImGui::GetBackgroundDrawList();
    if(g_mons.empty()){ ImGuiIO& io=ImGui::GetIO();
        DrawDeskMon(dl,0,0,0,io.DisplaySize.x,io.DisplaySize.y); return; }
    for(size_t i=0;i<g_mons.size();i++){
        const RECT& r=g_mons[i].rc;
        DrawDeskMon(dl,(int)i,(r.left-g_vs.left)/g_uiScale,(r.top-g_vs.top)/g_uiScale,
                    (r.right-r.left)/g_uiScale,(r.bottom-r.top)/g_uiScale);
    }
}
static void ExportPalette(const std::wstring& path);   // fwd
static void RunPostApply(const std::wstring& path);   // fwd
// change the desktop wallpaper and kick off the configured transition
// path is the image the picker shows. For a Wallpaper Engine entry that is the project's preview, and
// weProj is its project.json - picking it hands the wallpaper to Wallpaper Engine instead of Windows.
struct Wall{ std::wstring path; std::string name; ID3D11ShaderResourceView* tex=nullptr; bool tried=false; int w=0,h=0;
             std::wstring weProj; };
static std::vector<Wall> g_walls; static int g_wallSel=0;
// guards g_walls' tex/tried fields against the thumbnail worker (declared here: the apply path needs it)
static std::mutex g_wallMtx;
static std::wstring g_weExe;      // wallpaper64.exe (or 32) once found
static std::mutex   g_weMtx;
static void WeControl(const std::wstring& args){
    std::wstring exe; { std::lock_guard<std::mutex> lk(g_weMtx); exe=g_weExe; }
    if(exe.empty()) return;
    std::thread([exe,args]{
        std::wstring cmd=L"\""+exe+L"\" "+args;
        STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
        std::vector<wchar_t> b(cmd.begin(),cmd.end()); b.push_back(0);
        if(CreateProcessW(nullptr,b.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
            WaitForSingleObject(pi.hProcess,15000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    }).detach();
}
static int WeMonitorFor(ImVec2 deskPt){
    POINT p{ (LONG)(g_vs.left+deskPt.x*g_uiScale), (LONG)(g_vs.top+deskPt.y*g_uiScale) };
    int mi=MonIndexAt(p); if(mi<0||mi>=16) mi=std::clamp(g_actMon,0,15);
    return mi;
}
static ID3D11ShaderResourceView* CurrentBaseTex(int mi,int& w,int& h){
    if(mi>=0 && mi<16 && g_monLive[mi] && g_weShown[mi]){ w=g_weShownW[mi]; h=g_weShownH[mi]; return g_weShown[mi]; }
    if(g_deskBase){ w=g_deskBaseW; h=g_deskBaseH; return g_deskBase; }
    w=g_deskWallW; h=g_deskWallH; return g_deskWall;
}
// A REAL frame of the live wallpaper on monitor `mi`, full resolution. Wallpaper Engine's project previews are
// small (often 400-512px, sometimes a GIF); the hand-over used to animate into that, stretched across the
// screen - "a low quality pic, and big". PrintWindow with PW_RENDERFULLCONTENT reads the composed DirectX
// content. A window spanning several monitors is cropped to this one. Returns null when the capture is black
// or fails, so callers fall back to the preview.
static ID3D11ShaderResourceView* WeCaptureMon(int mi,int& outW,int& outH){
    outW=outH=0;
    if(mi<0||mi>=16||mi>=(int)g_mons.size()) return nullptr;
    HWND h=g_monLiveHwnd[mi]; if(!h||!IsWindow(h)||IsHungAppWindow(h)) return nullptr;
    RECT wr; if(!GetWindowRect(h,&wr)) return nullptr;
    int sw=wr.right-wr.left, sh=wr.bottom-wr.top; if(sw<=1||sh<=1||sw>16384||sh>16384) return nullptr;
    RECT mr=g_mons[mi].rc, in; if(!IntersectRect(&in,&wr,&mr)) return nullptr;
    HDC scr=GetDC(nullptr), full=CreateCompatibleDC(scr);
    BITMAPINFO bi={}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=sw; bi.bmiHeader.biHeight=-sh;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr; HBITMAP bmp=CreateDIBSection(full,&bi,DIB_RGB_COLORS,&bits,nullptr,0); HGDIOBJ old=SelectObject(full,bmp);
    ID3D11ShaderResourceView* tex=nullptr;
    if(bmp && bits && PrintWindow(h,full,PW_RENDERFULLCONTENT)){
        int cx=in.left-wr.left, cy=in.top-wr.top, cw=in.right-in.left, ch=in.bottom-in.top;
        std::vector<uint8_t> out((size_t)cw*ch*4);
        const uint8_t* px=(const uint8_t*)bits; long long lum=0; int samples=0;
        for(int y=0;y<ch;y++){
            memcpy(&out[(size_t)y*cw*4], px+(((size_t)(cy+y))*sw+cx)*4, (size_t)cw*4);
            if((y%37)==0) for(int x=0;x<cw;x+=41){ const uint8_t* q=&out[((size_t)y*cw+x)*4]; lum+=q[0]+q[1]+q[2]; samples++; } }
        for(size_t i=3;i<out.size();i+=4) out[i]=255;
        if(samples>0 && lum/(samples*3) >= 3){                // all-black = nothing was captured
            tex=MakeTextureBGRA(out.data(),cw,ch); if(tex){ outW=cw; outH=ch; } }
    }
    SelectObject(full,old); if(bmp) DeleteObject(bmp); DeleteDC(full); ReleaseDC(nullptr,scr);
    return tex;
}
static void ApplyWeWallpaper(const std::wstring& proj,const std::wstring& preview,ImVec2 from){
    POINT cp; if(g_transPosMouse && GetCursorPos(&cp)) from=V((cp.x-g_vs.left)/g_uiScale,(cp.y-g_vs.top)/g_uiScale);
    int mi=WeMonitorFor(from);
    WeTrans& T=g_weTr[mi]; WeTransEnd(T);
    bool wasStill=g_monStill[mi]; g_monStill[mi]=false;
    std::wstring cmd=L"-control openWallpaper -file \""+proj+L"\" -monitor "+std::to_wstring(mi);
    // keep the preview only as "what this screen shows" for things that want a thumbnail (never animated)
    T.npal=0;
    { std::vector<uint8_t> px; int w=0,h=0;
      if(DecodeFirstFrame(preview,1024,px,w,h)){ T.npal=PaletteFromPixels(px,w,h,T.pal); ID3D11ShaderResourceView* tex=MakeTextureBGRA(px.data(),w,h);
          if(tex){ if(g_weShown[mi]) g_weShown[mi]->Release(); g_weShown[mi]=tex; g_weShownW[mi]=w; g_weShownH[mi]=h; } } }
    T.L=WipeLayer(); T.t0=GetTickCount64(); T.liveAt=0; T.toImage=false; T.wasLive=g_monLive[mi]; T.sawDrop=false;
    if(g_monLive[mi] || wasStill){
        T.scrim=true; T.cmd=cmd; T.phase=1; T.fade=0.0f;               // fade out first, then switch
    } else {
        int uw=0,uh=0; ID3D11ShaderResourceView* under=CurrentBaseTex(mi,uw,uh); if(under) under->AddRef();
        T.scrim=false; T.L.under=under; T.L.uw=uw; T.L.uh=uh; T.phase=2; T.fade=1.0f;
        WeControl(cmd);
    }
    g_deskDirty=true;
}
static void SetWallpaperFile(const std::wstring& path, ImVec2 from){
    // A Wallpaper Engine entry goes to Wallpaper Engine, on the screen it was picked on.
    { std::wstring proj;
      { std::lock_guard<std::mutex> lk(g_wallMtx);
        for(auto& w:g_walls) if(!w.weProj.empty() && _wcsicmp(w.path.c_str(),path.c_str())==0){ proj=w.weProj; break; } }
      if(!proj.empty()){ ApplyWeWallpaper(proj,path,from); return; } }
    // A plain image picked on a screen Wallpaper Engine is drawing on: take that screen back from it and
    // transition out of the live wallpaper's preview rather than out of an old still image.
    { int mi=WeMonitorFor(from);
      if(mi>=0 && mi<16 && g_monLive[mi]){
          WeTrans& T=g_weTr[mi]; WeTransEnd(T);
          T.L=WipeLayer(); T.scrim=true; T.toImage=true; T.sawDrop=false;
          T.cmd=L"-control closeWallpaper -monitor "+std::to_wstring(mi);
          T.phase=1; T.fade=0.0f; T.t0=GetTickCount64();
          g_monStill[mi]=true;                                    // ours now, whatever Wallpaper Engine does
          g_weToImageSnap=true;                                   // the still image lands under the scrim: no wipe
      } }
    // SPI_SETDESKWALLPAPER with SPIF_SENDCHANGE broadcasts WM_SETTINGCHANGE to EVERY top-level
    // window and waits on each one, and Windows re-encodes the image into TranscodedWallpaper on
    // the way. Hundreds of milliseconds, sometimes seconds - and it used to run first, on the
    // render thread, so the shell froze solid before the transition had drawn a single frame.
    // Nothing we draw depends on it: our own wallpaper comes from the FILE, so the handover to
    // Windows is fire-and-forget while the transition starts immediately.
    LoadDeskWallpaper(true, path);      // the file we are applying, not the one Windows still has
    bool randomPos=false; g_transNow=ResolveTrans(g_transCfg,randomPos);
    // swww --transition-pos: mouse (default), the caller's point, or a random one for any/random.
    // The origin is in the DESK window's space, which spans every monitor.
    if(g_transPosMouse || randomPos){
        POINT cp; if(GetCursorPos(&cp)) from=V((cp.x-g_vs.left)/g_uiScale,(cp.y-g_vs.top)/g_uiScale);
    }
    if(randomPos){ from=V((float)(rand()%std::max(1,(int)((g_vs.right-g_vs.left)/g_uiScale))),
                          (float)(rand()%std::max(1,(int)((g_vs.bottom-g_vs.top)/g_uiScale)))); }
    g_wipe = (g_transNow==WTrans::None)? 1.0f : 0.0f;
    g_wipeAt=from; g_deskDirty=true;
    // Stack it. Anything already animating keeps animating underneath instead of being replaced.
    if(g_weToImageSnap){ g_transNow=WTrans::None; g_wipe=1.0f; g_weToImageSnap=false; }
    PushWipe(g_deskWall,g_deskWallW,g_deskWallH,g_transNow,from,g_transMs);
    if(g_dynamicColor) ApplyDynamicAccent();
    g_lastWall=W2U8(path); SaveConfig();
    if(g_paletteExport) std::thread([path]{ ExportPalette(path); }).detach();
    // Hand it to Windows, and run the user's post-apply hook, off the render thread. RunPostApply
    // spawns a process, which is another place the UI used to stall.
    std::thread([path]{
        SystemParametersInfoW(SPI_SETDESKWALLPAPER,0,(void*)path.c_str(),
                              SPIF_UPDATEINIFILE|SPIF_SENDCHANGE);
        RunPostApply(path);
    }).detach();
}


// area graph (0..fixedMax, or auto) — used by CPU/GPU/Memory/Storage/Network cards
static void AreaGraph(ImDrawList* dl, float x0,float y0,float x1,float y1,
                      const std::vector<float>& h, ImU32 line, ImU32 fill, float fixedMax=0) {
    int n=(int)h.size(); if(n<2) return;
    float mx=fixedMax; if(mx<=0){ mx=1e-4f; for(float v:h) mx=std::max(mx,v); }
    std::vector<ImVec2> p; p.reserve(n);
    for(int i=0;i<n;i++){ float x=x0+(x1-x0)*i/(n-1); float y=y1-(y1-y0)*std::clamp(h[i]/mx,0.0f,1.0f); p.push_back(V(x,y)); }
    for(int i=0;i+1<n;i++) dl->AddQuadFilled(p[i],p[i+1],V(p[i+1].x,y1),V(p[i].x,y1), fill);
    dl->AddPolyline(p.data(),n, line, 0, 2.0f);
    if(g_idleMotion){
        float ph=ShellPhase();
        // light travelling along the plot, and a live head on the newest sample
        Sheen(dl,V(x0,y0),V(x1,y1), ph*0.13f + y0*0.0013f, 20, 0.26f);
        float pulse=0.5f+0.5f*sinf(ph*2.6f + x0*0.01f);
        dl->AddCircleFilled(p[n-1], 2.2f+1.3f*pulse, WithA(line,(int)(150+90*pulse)));
        dl->AddCircleFilled(p[n-1], 1.8f, line);
    }
}
// line graph with min/max autoscale (for temperature — can be negative)
static void LineGraph(ImDrawList* dl, float x0,float y0,float x1,float y1,
                      const std::vector<double>& h, int n, ImU32 line, ImU32 fill) {
    if(n<2) return; double mn=1e9,mx=-1e9; for(int i=0;i<n;i++){ mn=std::min(mn,h[i]); mx=std::max(mx,h[i]); }
    if(mx-mn<0.5) mx=mn+1;
    std::vector<ImVec2> p; p.reserve(n);
    for(int i=0;i<n;i++){ float x=x0+(x1-x0)*i/(n-1); float y=y1-(y1-y0)*(float)((h[i]-mn)/(mx-mn)); p.push_back(V(x,y)); }
    for(int i=0;i+1<n;i++) dl->AddQuadFilled(p[i],p[i+1],V(p[i+1].x,y1),V(p[i].x,y1), fill);
    dl->AddPolyline(p.data(),n, line, 0, 2.0f);
    if(g_idleMotion){
        float ph=ShellPhase();
        // light travelling along the plot, and a live head on the newest sample
        Sheen(dl,V(x0,y0),V(x1,y1), ph*0.13f + y0*0.0013f, 20, 0.26f);
        float pulse=0.5f+0.5f*sinf(ph*2.6f + x0*0.01f);
        dl->AddCircleFilled(p[n-1], 2.2f+1.3f*pulse, WithA(line,(int)(150+90*pulse)));
        dl->AddCircleFilled(p[n-1], 1.8f, line);
    }
}

// ---- individual cards ----
static void HeroCard(ImDrawList* dl, ImVec2 p, ImVec2 s, const char* title, const std::string& name,
                     double usage, double temp, ImU32 accent, const std::vector<float>& hist) {
    Card(dl,p,s);
    // The reading only changes once a second, so drawn raw the ring and the percentage step. Easing
    // them means the card is always either moving or settling.
    { int aid=AnimIdFor(title,9700);
      usage=Cael::anim(aid,   (float)usage, Cael::DUR_DEFAULT_SPATIAL*2, Cael::DEFAULT_SPATIAL);
      if(temp>=0) temp=Cael::anim(aid+256,(float)temp, Cael::DUR_DEFAULT_SPATIAL*2, Cael::DEFAULT_SPATIAL); }
    // usage history graph across the middle
    ImU32 fill=WithA(accent,46);
    AreaGraph(dl, p.x+14, p.y+56, p.x+s.x-14, p.y+s.y-34, hist, accent, fill, 1.0f);
    // badge (ring + icon dot)
    ImVec2 bc = V(p.x+30, p.y+32);
    Ring(dl, bc, 16, 4, (float)usage, COL_TRACK, accent);
    dl->AddCircleFilled(bc, 5.5f, accent);
    // title + name (clipped: device names are long and must not blow the card out)
    TextAt(dl,g_fMed,18, V(p.x+56,p.y+14), COL_INK, title);
    { std::string nm=name; float maxw=s.x-150;
      while(!nm.empty()&&TextW(g_fSml,13,nm.c_str())>maxw) nm.pop_back();
      TextAt(dl,g_fSml,13, V(p.x+56,p.y+36), COL_INK2, nm.c_str()); }
    // usage pill (top right)
    char pc[16]; snprintf(pc,16,"%d%%",(int)std::round(usage*100));
    float pw = TextW(g_fMed,18,pc)+20;
    ImVec2 pp = V(p.x+s.x-pw-12, p.y+12);
    dl->AddRectFilled(pp, V(pp.x+pw,pp.y+26), COL_GOLDBG, 10);
    TextAt(dl,g_fMed,18, V(pp.x+10,pp.y+3), COL_GOLD, pc);
    // temperature bar
    float by = p.y+s.y-22, bx0=p.x+30, bx1=p.x+s.x-58;
    bool hot = temp>90;
    dl->AddText(g_fSml,13, V(p.x+14,by-3), hot?COL_ERR:COL_INK2, "\xC2\xB0");
    dl->AddRectFilled(V(bx0,by), V(bx1,by+6), COL_TRACK, 3);
    if (temp>=0) dl->AddRectFilled(V(bx0,by), V(bx0+(bx1-bx0)*(float)std::clamp(temp/100.0,0.0,1.0),by+6), hot?COL_ERR:accent, 3);
    char tc[16]; if (temp>=0) snprintf(tc,16,"%d\xC2\xB0""C",(int)std::round(temp)); else snprintf(tc,16,"--\xC2\xB0""C");
    TextAt(dl,g_fSml,13, V(bx1+6,by-4), hot?COL_ERR:COL_INK2, tc);
}

static void GaugeCard(ImDrawList* dl, ImVec2 p, ImVec2 s, const char* title,
                      double frac, const std::string& sub, const char* foot,
                      const std::vector<float>& hist) {
    Card(dl,p,s);
    frac = Cael::anim(AnimIdFor(title,9800), (float)frac, Cael::DUR_DEFAULT_SPATIAL*2, Cael::DEFAULT_SPATIAL);
    // small ring top-left
    ImVec2 gc = V(p.x+30, p.y+32);
    Ring(dl, gc, 16, 4, (float)frac, COL_TRACK, COL_GOLD);
    char pc[16]; snprintf(pc,16,"%d%%",(int)std::round(frac*100));
    dl->AddCircleFilled(gc, 5.0f, COL_GOLD);
    // title + sub + usage pill
    TextAt(dl,g_fMed,18, V(p.x+56,p.y+14), COL_INK, title);
    { std::string sb=sub; float maxw=s.x-130;
      while(!sb.empty()&&TextW(g_fSml,13,sb.c_str())>maxw) sb.pop_back();
      TextAt(dl,g_fSml,13, V(p.x+56,p.y+36), COL_INK2, sb.c_str()); }
    float pw=TextW(g_fMed,18,pc)+20; ImVec2 pp=V(p.x+s.x-pw-12,p.y+12);
    dl->AddRectFilled(pp, V(pp.x+pw,pp.y+26), COL_GOLDBG, 10);
    TextAt(dl,g_fMed,18, V(pp.x+10,pp.y+3), COL_GOLD, pc);
    // history graph across the body
    ImU32 fill=WithA(COL_GOLD,46);
    AreaGraph(dl, p.x+14, p.y+58, p.x+s.x-14, p.y+s.y-(foot&&*foot?34:14), hist, COL_GOLD, fill, 1.0f);
    if (foot && *foot) {
        ImVec2 fp=V(p.x+14,p.y+s.y-28);
        float fw=TextW(g_fSml,13,foot)+24;
        dl->AddRectFilled(fp, V(fp.x+fw,fp.y+22), COL_CARD2, 8);
        TextAt(dl,g_fSml,13, V(fp.x+10,fp.y+4), COL_INK2, foot);
    }
}

static void NetworkCard(ImDrawList* dl, ImVec2 p, ImVec2 s) {
    Card(dl,p,s);
    TextAt(dl,g_fMed,18, V(p.x+14,p.y+13), COL_INK, "Network");
    // graph
    float gx0=p.x+14, gx1=p.x+s.x-14, gy0=p.y+40, gy1=p.y+s.y*0.50f;
    auto& h=g_st.netHist;
    if (h.size()>=2) {
        float mx=1; for(float v:h) mx=std::max(mx,v);
        int n=(int)h.size();
        std::vector<ImVec2> pts; pts.reserve(n+2);
        for (int i=0;i<n;i++){ float x=gx0+(gx1-gx0)*i/(n-1); float y=gy1-(gy1-gy0)*(h[i]/mx); pts.push_back(V(x,y)); }
        // fill under curve
        for (int i=0;i+1<n;i++) dl->AddQuadFilled(pts[i],pts[i+1],V(pts[i+1].x,gy1),V(pts[i].x,gy1), AccA(42));
        dl->AddPolyline(pts.data(),(int)pts.size(), COL_GOLD, 0, 2.0f);
    }
    // rows
    auto rate=[&](double bps){ char c[24]; if(bps>=1048576) snprintf(c,24,"%.1f MB/s",bps/1048576.0); else snprintf(c,24,"%.1f KB/s",bps/1024.0); return std::string(c); };
    float ry=gy1+10; const float rh=22;
    struct R{const char* l; std::string v;} rows[]={
        {"Download", rate(g_st.netDown)}, {"Upload", rate(g_st.netUp)},
        {"Total", std::string("\xE2\x86\x93")+GiBs(g_st.netTotalIn)+"  \xE2\x86\x91"+GiBs(g_st.netTotalOut)} };
    for (auto& r:rows) {
        TextAt(dl,g_fSml,13, V(p.x+22,ry), COL_INK2, r.l);
        TextAt(dl,g_fSml,13, V(p.x+s.x-14-TextW(g_fSml,13,r.v.c_str()),ry), COL_INK, r.v.c_str());
        ry+=rh;
    }
}
// Human byte size. The old version stopped scaling at MB — so a 131 GB session total rendered as
// "134994.8MB", and anything under a gigabyte was printed in KB ("512000KB" for half a gig).
static std::string GiBs(unsigned long long b){ char c[32];
    if      (b>=1099511627776ULL) snprintf(c,32,"%.2fTB",b/1099511627776.0);
    else if (b>=1073741824ULL)    snprintf(c,32,"%.1fGB",b/1073741824.0);
    else if (b>=1048576ULL)       snprintf(c,32,"%.1fMB",b/1048576.0);
    else                          snprintf(c,32,"%.0fKB",b/1024.0);
    return c; }

static void BatteryCard(ImDrawList* dl, ImVec2 p, ImVec2 s) {
    dl->AddRectFilledMultiColor(p, V(p.x+s.x,p.y+s.y), IM_COL32(46,132,116,255), IM_COL32(46,132,116,255),
                                IM_COL32(26,92,82,255), IM_COL32(26,92,82,255));
    // rounded overlay corners are approximate; draw rounded card border
    dl->AddRect(p, V(p.x+s.x,p.y+s.y), IM_COL32(0,0,0,0), 20);
    TextAt(dl,g_fMed,21, V(p.x+20,p.y+34), IM_COL32(250,248,235,255), "Battery");
    char pc[16]; snprintf(pc,16,"%d%%",g_st.battPct);
    TextAt(dl,g_fBig,34, V(p.x+20,p.y+s.y-58), IM_COL32(255,252,240,255), pc);
    TextAt(dl,g_fSml,15, V(p.x+20,p.y+s.y-84), IM_COL32(240,238,220,220), g_st.charging?"Charging":"On battery");
}

// ---- the Performance tab layout ----
static float g_drawerContent=0.0f;   // content settles slightly after the panel (staged open animation)
// Caelestia-style Performance tab: a row of big circular ring gauges. Each shows a large value in
// the middle (temperature when available, otherwise usage), a label, and a secondary stat below.
// Icons used on the Performance cards
static void PerfIcon(ImDrawList* dl, ImVec2 c, int kind, ImU32 col){
    if(kind==0){ dl->AddRect(V(c.x-8,c.y-8),V(c.x+8,c.y+8),col,2,0,2.0f); dl->AddRectFilled(V(c.x-3,c.y-3),V(c.x+3,c.y+3),col,1);   // CPU chip
        for(int k=-1;k<=1;k++){ dl->AddLine(V(c.x+k*5,c.y-11),V(c.x+k*5,c.y-8),col,1.5f); dl->AddLine(V(c.x+k*5,c.y+8),V(c.x+k*5,c.y+11),col,1.5f);
            dl->AddLine(V(c.x-11,c.y+k*5),V(c.x-8,c.y+k*5),col,1.5f); dl->AddLine(V(c.x+8,c.y+k*5),V(c.x+11,c.y+k*5),col,1.5f); } }
    else if(kind==1){ dl->AddRect(V(c.x-10,c.y-7),V(c.x+10,c.y+5),col,2,0,2.0f); dl->AddLine(V(c.x-4,c.y+5),V(c.x-4,c.y+9),col,2); dl->AddLine(V(c.x+4,c.y+5),V(c.x+4,c.y+9),col,2); dl->AddLine(V(c.x-7,c.y+9),V(c.x+7,c.y+9),col,2); } // GPU monitor
    else if(kind==2){ dl->AddRect(V(c.x-10,c.y-7),V(c.x+10,c.y+7),col,2,0,2.0f); for(int k=-1;k<=1;k++) dl->AddLine(V(c.x+k*6,c.y-7),V(c.x+k*6,c.y+2),col,1.6f); } // memory
    else { dl->AddRect(V(c.x-9,c.y-9),V(c.x+9,c.y+9),col,4,0,2.0f); dl->AddCircleFilled(V(c.x+3,c.y+3),2.0f,col); } // storage
}
static void DrawPerformanceV2(ImDrawList* dl,ImVec2 org,ImVec2 area);                 // fwd (CaelV2.h)
static void DrawMediaV2(ImDrawList* dl,ImVec2 org,ImVec2 area,ImGuiIO& io);            // fwd
static void DrawDashboardV2(ImDrawList* dl,ImVec2 org,ImVec2 area);                    // fwd
static void DrawPerformance(ImDrawList* dl, ImVec2 org, ImVec2 area) {
    if(g_perfLayout==1){ DrawPerformanceV2(dl,org,area); return; }
    // Matches Caelestia's Performance tab (clear3): CPU/GPU cards = name + big Usage% + Temp bar;
    // then Memory + Storage (big ring w/ % inside) + Network (graph + down/up/total).
    const float gap=12;
    float rowH=(area.y-gap)/2, heroW=(area.x-gap)/2;
    auto hero=[&](ImVec2 p,ImVec2 s,int ic,const char* title,std::string name,double usage,double temp){
        Card(dl,p,s);
        PerfIcon(dl,V(p.x+26,p.y+26),ic,COL_INK2);
        std::string t=std::string(title)+" - "+name; float mw=s.x-160;
        while(!t.empty()&&TextW(g_fMed,16,t.c_str())>mw) t.pop_back();
        TextAt(dl,g_fMed,16,V(p.x+46,p.y+18),COL_INK,t.c_str());
        TextAt(dl,g_fSml,13,V(p.x+s.x-108,p.y+16),COL_INK2,"Usage");
        char u[16]; snprintf(u,16,"%d%%",(int)std::round(usage*100));
        TextAt(dl,g_fBig,32,V(p.x+s.x-24-TextW(g_fBig,32,u),p.y+30),COL_INK,u);
        // temp + bar (bottom)
        char tc[16]; if(temp>0) snprintf(tc,16,"%d\xC2\xB0""C",(int)std::round(temp)); else snprintf(tc,16,"--");
        bool hot=temp>85;
        TextAt(dl,g_fMed,18,V(p.x+18,p.y+s.y-46),hot?COL_ERR:COL_INK,tc);
        TextAt(dl,g_fSml,13,V(p.x+18+TextW(g_fMed,18,tc)+8,p.y+s.y-42),COL_INK2,"Temp");
        float bx0=p.x+18,bx1=p.x+s.x-18,by=p.y+s.y-22;
        dl->AddRectFilled(V(bx0,by),V(bx1,by+6),COL_TRACK,3);
        if(temp>0) dl->AddRectFilled(V(bx0,by),V(bx0+(bx1-bx0)*(float)std::clamp(temp/100.0,0.0,1.0),by+6),hot?COL_ERR:COL_GOLD,3);
    };
    hero(V(org.x,org.y),           V(heroW,rowH),0,"CPU",W2U8(g_st.cpuName),g_st.cpuUsage,g_st.cpuTemp);
    hero(V(org.x+heroW+gap,org.y), V(heroW,rowH),1,"GPU",W2U8(g_st.gpuName),g_st.gpuUsage,g_st.gpuTemp);
    float y2=org.y+rowH+gap, tW=(area.x-2*gap)/3;
    auto ringCard=[&](ImVec2 p,ImVec2 s,int ic,const std::string& title,double frac,const std::string& sub){
        Card(dl,p,s);
        PerfIcon(dl,V(p.x+26,p.y+26),ic,COL_INK2);
        TextAt(dl,g_fMed,16,V(p.x+46,p.y+18),COL_INK,title.c_str());
        ImVec2 c=V(p.x+s.x*0.5f,p.y+s.y*0.54f); float R=std::min(s.x*0.24f,s.y*0.28f);
        Ring(dl,c,R,7.0f,(float)frac,COL_TRACK,COL_GOLD);
        char pc[8]; snprintf(pc,8,"%d%%",(int)std::round(frac*100));
        TextAt(dl,g_fBig,24,V(c.x-TextW(g_fBig,24,pc)/2,c.y-16),COL_INK,pc);
        TextAt(dl,g_fSml,13,V(p.x+s.x*0.5f-TextW(g_fSml,13,sub.c_str())/2,p.y+s.y-26),COL_INK2,sub.c_str());
    };
    double diskF=g_st.diskTotal?(double)g_st.diskUsed/g_st.diskTotal:0;
    double memF =g_st.memTotal ?(double)g_st.memUsed/g_st.memTotal:0;
    ringCard(V(org.x,y2),           V(tW,rowH),2,"Memory",memF,GiB(g_st.memUsed)+" / "+GiB(g_st.memTotal)+" GiB");
    ringCard(V(org.x+tW+gap,y2),    V(tW,rowH),3,"Storage",diskF,GiB(g_st.diskUsed)+" / "+GiB(g_st.diskTotal)+" GiB");
    NetworkCard(dl,V(org.x+2*(tW+gap),y2),V(tW,rowH));
}

// ---- weather icon + calendar helpers ----
static int DaysInMonth(int y,int m){ static const int d[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(m==1 && ((y%4==0&&y%100!=0)||y%400==0)) return 29; return d[m]; }

// ---- weather glyphs -----------------------------------------------------------------------------
// These were three hardcoded shapes that never changed and never moved: a dot with eight identical
// spokes, a lump of circles, and three straight lines for rain. Rebuilt with some actual character,
// and with the motion the rest of the shell now has - a sun whose spikes turn and shimmer, cloud
// puffs that roll, and rain that actually falls.

// Twelve TAPERED spikes, alternating long and short, the whole ring turning while a wave of
// brightness travels round it. `R` is the outer reach of the long spikes.
static void SunGlyph(ImDrawList* dl, ImVec2 c, float R, ImU32 col){
    const float TAU=6.2831853f;
    float ph=ShellPhase();
    float breathe=0.5f+0.5f*sinf(ph*1.3f);
    const int N=12;
    float rot=ph*0.17f;
    for(int i=0;i<N;i++){
        float a=rot + (float)i*(TAU/N);
        bool  lng=(i%2)==0;
        // each spike leads the next, so the ring shimmers around instead of blinking as one
        float wv=0.5f+0.5f*sinf(ph*1.9f - (float)i*(TAU/N)*1.5f);
        float r0=R*0.60f;
        float r1=R*(lng?1.00f:0.80f) + R*0.06f*wv;
        float hw=lng?0.115f:0.085f;
        dl->AddTriangleFilled(V(c.x+cosf(a-hw)*r0, c.y+sinf(a-hw)*r0),
                              V(c.x+cosf(a+hw)*r0, c.y+sinf(a+hw)*r0),
                              V(c.x+cosf(a)*r1,    c.y+sinf(a)*r1),
                              WithA(col,(int)(150+105*wv)));
    }
    // no hard outline ring: a flat disc with a stroke around it is exactly the clipart look this
    // was rebuilt to get away from. Two soft coronas give the edge instead.
    dl->AddCircleFilled(c, R*0.70f+R*0.05f*breathe, WithA(col,18));
    dl->AddCircleFilled(c, R*0.56f+R*0.03f*breathe, WithA(col,40));
    dl->AddCircleFilled(c, R*0.42f+R*0.02f*breathe, col);
}

// Puffs that roll: each lobe drifts on its own phase so the silhouette keeps changing shape.
static void CloudGlyph(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float seed){
    float ph=ShellPhase()+seed;
    float sway=sinf(ph*0.45f)*r*0.05f;
    struct Puff{ float ox,oy,rr,sp; };
    const Puff pf[4]={ {-0.52f, 0.14f, 0.44f, 1.10f}, {-0.10f,-0.22f, 0.58f, 0.80f},
                       { 0.40f,-0.02f, 0.50f, 1.35f}, { 0.72f, 0.18f, 0.36f, 0.95f} };
    for(const Puff& p:pf){
        float b=1.0f + 0.05f*sinf(ph*p.sp);
        dl->AddCircleFilled(V(c.x+sway+r*p.ox, c.y+r*p.oy + r*0.03f*sinf(ph*p.sp*0.8f)),
                            r*p.rr*b, col);
    }
    dl->AddRectFilled(V(c.x+sway-r*0.82f, c.y+r*0.06f), V(c.x+sway+r*0.86f, c.y+r*0.52f), col, r*0.26f);
}

// WMO weather code -> Material Symbols glyph. The hand-drawn set below is kept as the fallback for
// the other icon themes, but it is multi-coloured cartoon art (a yellow spiked sun, grey clouds,
// blue drops) which sat very badly next to Material glyphs everywhere else in the shell - and at
// forecast size the sun's spikes overlapped the hi/lo text under it.
static const char* WxMaterial(int code,bool night){
    if(code<=1)  return night? "clear_night" : "clear_day";
    if(code<=3)  return night? "clear_night" : "partly_cloudy_day";
    if(code==45||code==48) return "foggy";
    if(code<=48) return "cloudy";
    if((code>=71&&code<=77)||code==85||code==86) return "snowing";
    if(code>=95) return "thunderstorm";
    return "rainy";
}
static void WxIcon(ImDrawList* dl, ImVec2 c, float r, int code){
    if(g_iconSet==ICONSET_MATERIAL){
        // sun-bearing conditions take the accent; everything else reads as plain foreground, so the
        // row does not turn into a wall of gold.
        bool sunny = (code<=3);
        ImU32 col = sunny? WithA(COL_GOLD,255) : WithA(COL_INK,235);
        if(MSym(dl,c,r*2.2f,col,MIconCp(WxMaterial(code,false)))) return;
    }
    const ImU32 SUN =IM_COL32(236,186,64,255);
    const ImU32 CLD =IM_COL32(186,184,178,255);
    const ImU32 CLD2=IM_COL32(150,160,182,255);
    const ImU32 WET =IM_COL32(96,150,215,255);
    float ph=ShellPhase();

    if(code<=1){ SunGlyph(dl,c,r*1.32f,SUN); return; }                    // clear

    if(code<=3){                                                          // partly cloudy
        SunGlyph(dl,V(c.x+r*0.42f,c.y-r*0.46f),r*0.78f,SUN);
        CloudGlyph(dl,V(c.x-r*0.10f,c.y+r*0.12f),r*0.92f,CLD,0.0f);
        return;
    }
    if(code<=48){ CloudGlyph(dl,c,r,CLD,0.0f); return; }                  // overcast / fog

    // everything wet or frozen sits under a cloud; what falls out of it is the difference
    bool snow    = (code>=71&&code<=77)||code==85||code==86;
    bool thunder = code>=95;
    CloudGlyph(dl,c,r*0.96f,thunder?IM_COL32(128,134,156,255):CLD2,0.0f);

    if(thunder){
        // a bolt that flashes on a slow, irregular beat rather than sitting there lit
        float f=sinf(ph*2.3f)*sinf(ph*0.77f);
        int a=(int)(120+135*std::clamp(f,0.0f,1.0f));
        ImVec2 p0=V(c.x+r*0.10f, c.y+r*0.46f), p1=V(c.x-r*0.16f, c.y+r*1.02f);
        ImVec2 p2=V(c.x+r*0.10f, c.y+r*0.94f), p3=V(c.x-r*0.08f, c.y+r*1.52f);
        dl->AddTriangleFilled(p0,V(c.x+r*0.36f,c.y+r*0.50f),p1,WithA(IM_COL32(248,206,86,255),a));
        dl->AddTriangleFilled(p1,p2,p3,WithA(IM_COL32(248,206,86,255),a));
        return;
    }
    // falling drops / flakes: each column has its own speed and wraps, so it never stops
    for(int i=0;i<4;i++){
        float x=c.x-r*0.54f+i*r*0.36f;
        float sp=0.55f+0.12f*(float)((i*7)%3);
        float t=fmodf(ph*sp + (float)i*0.37f, 1.0f);
        float y=c.y+r*0.58f + t*r*0.78f;
        int   a=(int)(235*(1.0f-t*t));
        if(snow){
            ImU32 wc=WithA(IM_COL32(226,236,248,255),a);
            float k=r*0.13f, rot=ph*1.1f+(float)i;
            for(int q=0;q<3;q++){ float ang=rot+q*1.0472f;
                dl->AddLine(V(x-cosf(ang)*k,y-sinf(ang)*k),V(x+cosf(ang)*k,y+sinf(ang)*k),wc,1.4f); }
        } else {
            // a teardrop, not a dash: round belly, drawn point-up
            ImU32 wc=WithA(WET,a);
            dl->AddCircleFilled(V(x,y+r*0.05f), r*0.085f, wc);
            dl->AddTriangleFilled(V(x-r*0.075f,y+r*0.045f),V(x+r*0.075f,y+r*0.045f),V(x,y-r*0.14f),wc);
        }
    }
}

static void DrawWeather(ImDrawList* dl, ImVec2 org, ImVec2 area){
    if(!g_wx.ok){ const char* m= g_wxSource==0? "Weather is off \xE2\x80\x94 pick a location in Settings > Dashboard" : "Fetching weather\xE2\x80\xA6";
        TextAt(dl,g_fMed,21,V(org.x+area.x/2-TextW(g_fMed,21,m)/2,org.y+area.y/2-10),COL_INK2,m); return; }
    const float gap=12; bool dk=g_darkUI;
    time_t now=time(nullptr); struct tm lt; localtime_s(&lt,&now);
    auto sub=[&](ImVec2 a,ImVec2 b){ dl->AddRectFilled(a,b,WithA(COL_INK2,dk?26:22),16);
        dl->AddRect(a,b,WithA(dk?IM_COL32(255,255,255,255):IM_COL32(0,0,0,255),dk?12:12),16,0,1.0f); };
    // ---- header: city + date (left), sunrise / sunset (right) ----
    TextAt(dl,g_fHuge,34,V(org.x+4,org.y-2),COL_INK,g_wx.city.c_str());
    { char ds[64]; strftime(ds,64,"%A, %d %B",&lt); TextAt(dl,g_fSml,14,V(org.x+6,org.y+40),COL_INK2,ds); }
    if(!g_wx.sunrise.empty()){ float rx=org.x+area.x-320;
        // sunrise
        ImVec2 sc=V(rx,org.y+22);
        if(g_iconSet!=ICONSET_MATERIAL || !MSym(dl,sc,26.0f,WithA(COL_GOLD,255),MIconCp("clear_day"))){
            dl->AddCircleFilled(V(sc.x,sc.y+3),7,WithA(COL_GOLD,255));
            for(int k=0;k<5;k++){ float a=3.1416f+k*(3.1416f/4); dl->AddLine(V(sc.x+cosf(a)*10,sc.y+3+sinf(a)*10),V(sc.x+cosf(a)*14,sc.y+3+sinf(a)*14),WithA(COL_GOLD,255),1.6f); }
            dl->AddLine(V(sc.x,sc.y-8),V(sc.x-4,sc.y-3),COL_GOLD,1.8f); dl->AddLine(V(sc.x,sc.y-8),V(sc.x+4,sc.y-3),COL_GOLD,1.8f); }
        TextAt(dl,g_fSml,13,V(rx+24,org.y+8),COL_INK2,"Sunrise"); TextAt(dl,g_fMed,16,V(rx+24,org.y+26),COL_INK,g_wx.sunrise.c_str());
        // sunset (moon)
        float mx2=org.x+area.x-150; ImVec2 mc=V(mx2,org.y+22);
        if(g_iconSet!=ICONSET_MATERIAL || !MSym(dl,mc,26.0f,WithA(COL_INK2,255),MIconCp("bedtime"))){
            dl->AddCircleFilled(mc,9,WithA(COL_INK2,255)); dl->AddCircleFilled(V(mc.x+4,mc.y-3),8,dk?COL_CARD:COL_PANELL); }
        TextAt(dl,g_fSml,13,V(mx2+22,org.y+8),COL_INK2,"Sunset"); TextAt(dl,g_fMed,16,V(mx2+22,org.y+26),COL_INK,g_wx.sunset.c_str()); }

    // ---- big current card: icon + big temp + condition ----
    float bigY=org.y+62, bigH=std::min(area.y*0.26f,150.0f);
    sub(V(org.x,bigY),V(org.x+area.x,bigY+bigH));
    { float cyc=bigY+bigH*0.5f; WxIcon(dl,V(org.x+area.x*0.30f,cyc),34,g_wx.code);
      char t[16]; snprintf(t,16,"%d\xC2\xB0""C",(int)lround(g_wx.temp));
      TextAt(dl,g_fHuge,64,V(org.x+area.x*0.40f,cyc-40),COL_INK,t);
      TextAt(dl,g_fMed,18,V(org.x+area.x*0.40f+4,cyc+26),COL_INK2,WxText(g_wx.code)); }

    // ---- Humidity / Feels Like / Wind row ----
    float chY=bigY+bigH+gap, chH=std::min(area.y*0.15f,74.0f), chW=(area.x-2*gap)/3;
    { struct C{const char* l; char v[24]; int ic;} cs[3];
      snprintf(cs[0].v,24,"%d%%",g_wx.hum);            cs[0].l="Humidity";  cs[0].ic=0;
      snprintf(cs[1].v,24,"%d\xC2\xB0""C",(int)lround(g_wx.feels)); cs[1].l="Feels Like"; cs[1].ic=1;
      snprintf(cs[2].v,24,"%.0f km/h",g_wx.wind);      cs[2].l="Wind";      cs[2].ic=2;
      for(int i=0;i<3;i++){ ImVec2 a=V(org.x+i*(chW+gap),chY),b=V(a.x+chW,chY+chH); sub(a,b);
          ImVec2 ic=V(a.x+26,(a.y+b.y)*0.5f); ImU32 gc=WithA(COL_GOLD,255);
          static const char* WXCHIP[3]={"water_drop","thermostat","air"};
          bool drew = (g_iconSet==ICONSET_MATERIAL) && MSym(dl,ic,24.0f,gc,MIconCp(WXCHIP[cs[i].ic]));
          if(!drew){
          if(cs[i].ic==0){ dl->AddCircleFilled(V(ic.x,ic.y+2),5,gc); dl->AddTriangleFilled(V(ic.x-5,ic.y+1),V(ic.x+5,ic.y+1),V(ic.x,ic.y-8),gc); }   // droplet
          else if(cs[i].ic==1){ dl->AddRectFilled(V(ic.x-2,ic.y-7),V(ic.x+2,ic.y+3),gc,2); dl->AddCircleFilled(V(ic.x,ic.y+5),4,gc); }                // thermometer
          else { for(int k=0;k<3;k++) dl->AddLine(V(ic.x-7,ic.y-4+k*4),V(ic.x+6,ic.y-4+k*4),gc,1.8f); dl->AddCircleFilled(V(ic.x+6,ic.y),2.5f,gc); }  // wind
          }
          TextAt(dl,g_fSml,13,V(a.x+48,a.y+chH*0.5f-16),COL_INK2,cs[i].l);
          TextAt(dl,g_fMed,18,V(a.x+48,a.y+chH*0.5f+2),COL_INK,cs[i].v); } }

    // ---- 7-Day Forecast: a row of day cards (day / date / icon / hi / lo) ----
    float fY=chY+chH+gap; TextAt(dl,g_fMed,16,V(org.x+4,fY),COL_INK,"7-Day Forecast"); fY+=26;
    int n=(int)std::min(g_wx.dMax.size(),g_wx.dCode.size()); if(n>7)n=7;
    if(n>0){ const char* dn[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        const char* mo[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        float cw=(area.x-(n-1)*gap)/n, ch=org.y+area.y-fY;
        for(int i=0;i<n;i++){ ImVec2 a=V(org.x+i*(cw+gap),fY),b=V(a.x+cw,fY+ch); sub(a,b);
            float mid=(a.x+b.x)*0.5f;
            const char* d=(i==0)?"Today":dn[(lt.tm_wday+i)%7];
            TextAt(dl,g_fMed,15,V(mid-TextW(g_fMed,15,d)/2,a.y+12),COL_INK,d);
            struct tm dd=lt; dd.tm_mday+=i; dd.tm_hour=12; mktime(&dd);
            char db[16]; snprintf(db,16,"%d %s",dd.tm_mday,mo[dd.tm_mon]);
            TextAt(dl,g_fSml,12,V(mid-TextW(g_fSml,12,db)/2,a.y+32),COL_INK2,db);
            // Centre the glyph in the gap between the date line above and the hi/lo below rather
            // than at a fixed fraction of the card - the Material glyph fills its box far more than
            // the old cartoon art did, so at the old size and offset it sat on top of both.
            WxIcon(dl,V(mid,(a.y+50.0f + b.y-34.0f)*0.5f),16,g_wx.dCode[i]);
            char hl[24]; snprintf(hl,24,"%d\xC2\xB0 / %d\xC2\xB0",(int)lround(g_wx.dMax[i]),(int)lround(g_wx.dMin[i]));
            TextAt(dl,g_fMed,15,V(mid-TextW(g_fMed,15,hl)/2,b.y-30),COL_INK,hl); } }
}

static void SpeakerIcon(ImDrawList* dl,ImVec2 c,ImU32 col);   // fwd (defined with the sidebar icons)
static void SunIcon(ImDrawList* dl,ImVec2 c,ImU32 col);       // fwd
static float g_volCache=-1;                                   // system volume cache (used by bar + dashboard)

// bongo cat — bobs its paws while music plays (like the reference's media card)
static bool g_recording=false;   // screen recorder state (drives the recording toast)
