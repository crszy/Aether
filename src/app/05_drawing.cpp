// Aether - drawing helpers, fonts, textures, icons.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ================================================================= drawing helpers
static ImVec2 V(float x,float y){ return ImVec2(x,y); }
static std::string W2U8(const std::wstring& w){
    if (w.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr);
    std::string s(n,0); WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),s.data(),n,nullptr,nullptr);
    return s;
}
static std::wstring U82W(const std::string& s){
    if (s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0);
    std::wstring w(n,0); MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),&w[0],n);
    return w;
}
static std::string GiB(unsigned long long b){ char c[32]; snprintf(c,32,"%.1f",b/1073741824.0); return c; }
static std::string GiBs(unsigned long long b);   // fwd

// Global text-size multiplier. 1.0 = the current look; ~0.86 gives Caelestia's dense Material-3 feel.
// Applied in BOTH helpers so measured width and drawn size always agree (no clipping from a mismatch).
static float g_textScale=1.0f;
static void TextAt(ImDrawList* dl, ImFont* f, float sz, ImVec2 p, ImU32 c, const char* t){ dl->AddText(f,sz*g_textScale,p,c,t); }
// A soft round glow: ONE disc whose alpha falls smoothly from `peak` at the centre to 0 at the rim, done
// with per-vertex colour so the GPU interpolates it. Stacking flat AddCircleFilled discs (the old way)
// gives every disc a hard edge - the lock screen's "aurora" showed as eight concentric rings per blob
// instead of a blur. The falloff profile is roughly Gaussian (four rings of vertices).
static void SoftDisc(ImDrawList* dl,ImVec2 c,float rad,ImU32 col,float peak){
    if(rad<1.0f || peak<=0.0f) return;
    const int SEG=72;
    static const float RR[4]={0.0f,0.35f,0.70f,1.0f};
    static const float AA[4]={1.0f,0.74f,0.30f,0.0f};
    auto C=[&](float a){ int al=(int)(peak*255.0f*a+0.5f); if(al<0)al=0; if(al>255)al=255; return (col&0x00FFFFFFu)|((ImU32)al<<24); };
    const ImVec2 uv=ImGui::GetFontTexUvWhitePixel();
    dl->PrimReserve(SEG*3+2*SEG*6, 1+3*SEG);
    const unsigned int base=dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(c,uv,C(AA[0]));
    for(int k=1;k<4;k++) for(int i=0;i<SEG;i++){ float t=i*6.2831853f/SEG;
        dl->PrimWriteVtx(ImVec2(c.x+cosf(t)*rad*RR[k],c.y+sinf(t)*rad*RR[k]),uv,C(AA[k])); }
    for(int i=0;i<SEG;i++){ int j=(i+1)%SEG;
        dl->PrimWriteIdx((ImDrawIdx)base); dl->PrimWriteIdx((ImDrawIdx)(base+1+i)); dl->PrimWriteIdx((ImDrawIdx)(base+1+j)); }
    for(int k=1;k<3;k++){ unsigned int r0=base+1+(k-1)*SEG, r1=base+1+k*SEG;
        for(int i=0;i<SEG;i++){ int j=(i+1)%SEG;
            dl->PrimWriteIdx((ImDrawIdx)(r0+i)); dl->PrimWriteIdx((ImDrawIdx)(r1+i)); dl->PrimWriteIdx((ImDrawIdx)(r1+j));
            dl->PrimWriteIdx((ImDrawIdx)(r0+i)); dl->PrimWriteIdx((ImDrawIdx)(r1+j)); dl->PrimWriteIdx((ImDrawIdx)(r0+j)); } }
}
static float TextW(ImFont* f, float sz, const char* t){ return f->CalcTextSizeA(sz*g_textScale,FLT_MAX,0,t).x; }
// text rotated about `pos` (ImGui has no rotated text: draw it, then spin the vertices it emitted)
static void TextRot(ImDrawList* dl, ImFont* f, float sz, ImVec2 pos, ImU32 c, const char* t, float ang){
    int v0=dl->VtxBuffer.Size; dl->AddText(f,sz,pos,c,t); int v1=dl->VtxBuffer.Size;
    float cs=cosf(ang), sn=sinf(ang);
    for(int i=v0;i<v1;i++){ ImVec2 p=dl->VtxBuffer[i].pos; p.x-=pos.x; p.y-=pos.y;
        dl->VtxBuffer[i].pos=ImVec2(pos.x+p.x*cs-p.y*sn, pos.y+p.x*sn+p.y*cs); }
}

// ---- UI scale ------------------------------------------------------------------------
// Every panel is authored in LOGICAL pixels. Rather than sprinkling a scale factor through
// ~2500 lines of hardcoded geometry, we shrink ImGui's display size and blow the finished
// draw data back up to the physical framebuffer (this backend predates FramebufferScale).
static void ScaleDrawData(ImDrawData* dd,float s){
    if(!dd || s==1.0f) return;
    for(int n=0;n<dd->CmdListsCount;n++){ ImDrawList* cl=dd->CmdLists[n];
        for(int i=0;i<cl->VtxBuffer.Size;i++){ cl->VtxBuffer[i].pos.x*=s; cl->VtxBuffer[i].pos.y*=s; }
        for(int i=0;i<cl->CmdBuffer.Size;i++){ ImVec4& r=cl->CmdBuffer[i].ClipRect; r.x*=s;r.y*=s;r.z*=s;r.w*=s; } }
    dd->DisplaySize.x*=s; dd->DisplaySize.y*=s;
}

// ---- idle motion --------------------------------------------------------------------------------
// Nothing in the reference rice sits still. Every idling element in the shell reads its phase off
// ONE free-running clock: a widget with its own timer drifts out of step with its neighbours within
// a minute, and a dashboard whose cards each breathe at their own rate looks broken rather than
// alive. This lives up here, above the drawing primitives, because Card/Ring/AreaGraph all use it -
// animating those three is what makes the whole dashboard move rather than four hand-tuned tabs.
static bool  g_idleMotion=true;     // Settings > Taskbar > Shape gauges
static float g_idleRate=1.0f;       // 0.25 (barely breathing) .. 2.0
static float ShellPhase(){
    static const ULONGLONG t0=GetTickCount64();
    if(!g_idleMotion) return 0.0f;
    return (float)((double)(GetTickCount64()-t0)/1000.0) * g_idleRate;
}
// A stable animation id derived from a caller's literal label, so two cards easing the same kind of
// value do not share a slot and yank each other around.
static int AnimIdFor(const char* s, int base){
    unsigned h=2166136261u; for(const char* p=s; p && *p; ++p){ h^=(unsigned char)*p; h*=16777619u; }
    return base + (int)(h % 512u);
}
// A soft band of light travelling across a rect, clipped to it. The one gesture that makes a flat
// surface read as a surface rather than a painted rectangle.
static void Sheen(ImDrawList* dl, ImVec2 a, ImVec2 b, float phase, int alpha, float widthFrac=0.34f){
    if(!g_idleMotion || alpha<=0) return;
    float w=(b.x-a.x); if(w<8.0f) return;
    float sw=w*widthFrac;
    float x=a.x - sw + fmodf(phase,1.0f)*(w+sw*2.0f);
    dl->PushClipRect(a,b,true);
    ImU32 c0=IM_COL32(255,255,255,0), c1=IM_COL32(255,255,255,alpha);
    dl->AddRectFilledMultiColor(V(x,a.y),         V(x+sw*0.5f,b.y), c0,c1,c1,c0);
    dl->AddRectFilledMultiColor(V(x+sw*0.5f,a.y), V(x+sw,b.y),      c1,c0,c0,c1);
    dl->PopClipRect();
}

// arc gauge: value 0..1, sweeps ~270deg starting bottom-left
static void Ring(ImDrawList* dl, ImVec2 c, float r, float th, float value, ImU32 track, ImU32 fill) {
    const float PIf = 3.14159265358979f;
    const float a0 = PIf*0.75f, a1 = PIf*0.75f + PIf*1.5f;
    dl->PathArcTo(c, r, a0, a1, 64); dl->PathStroke(track, 0, th);
    float v = std::clamp(value,0.0f,1.0f);
    float a = a0 + (a1-a0)*v;
    dl->PathArcTo(c, r, a0, a, 64); dl->PathStroke(fill, 0, th);
    if(!g_idleMotion) return;
    // a short bright arc sweeping along the filled part, and a cap that breathes at its head - a
    // ring drawn once is the most obviously frozen thing on a dashboard
    float ph=ShellPhase();
    if(v>0.02f){
        float span=(a-a0);
        float seg=std::min(span, 0.55f);
        float t=fmodf(ph*0.42f + (c.x+c.y)*0.004f, 1.0f);
        float s0=a0 + (span-seg)*t, s1=s0+seg;
        dl->PathArcTo(c, r, s0, s1, 24);
        dl->PathStroke(WithA(fill,(int)(90+40*sinf(ph*2.0f))), 0, th*0.72f);
        float pulse=0.5f+0.5f*sinf(ph*2.4f);
        dl->AddCircleFilled(V(c.x+cosf(a)*r, c.y+sinf(a)*r), th*(0.46f+0.20f*pulse), WithA(fill,225));
    }
}

static int   g_cardMode=0;          // set around one widget by its style: 0 normal, 1 no card, 2 colour
static ImU32 g_cardCol=0;
static void Card(ImDrawList* dl, ImVec2 p, ImVec2 s, ImU32 col=COL_CARD) {
    if(g_cardMode==1) return;
    if(g_cardMode==2) col=g_cardCol;
    // cards get a hairline top highlight + bottom shade so they read as raised, not painted on
    if(g_deskGlow) dl->AddRectFilled(V(p.x+1,p.y+2), V(p.x+s.x+1,p.y+s.y+2), IM_COL32(0,0,0,26), g_cardRound);
    dl->AddRectFilled(p, V(p.x+s.x,p.y+s.y), col, g_cardRound);
    // the light crawling over the surface, and an edge that breathes with it - every card in the
    // shell goes through here, so this is what stops the dashboard reading as a printed page
    ImVec2 a=p, b=V(p.x+s.x,p.y+s.y);
    Sheen(dl,a,b, ShellPhase()*0.075f + (p.x+p.y)*0.0011f, g_darkUI?17:24);
    float br=0.5f+0.5f*sinf(ShellPhase()*0.9f + (p.x*0.013f+p.y*0.017f));
    int   ea=(int)((g_darkUI?14:120) * (0.80f+0.20f*br));
    if(g_panelOutline) dl->AddRect(a, b, IM_COL32(255,255,255,ea), g_cardRound, 0, 1.0f);
}

// ---- animation helpers -------------------------------------------------------------------
static float EaseOutBack(float t);   // fwd
static float EaseInCubic(float t);   // fwd
// Global motion scale (Settings > Accessibility). >1 = snappier, <1 = languid; "reduce motion"
// parks it high enough that every transition effectively lands on the next frame.
// the multiplier now lives with the animator (Anim.h) so BOTH the Material-3 curves and the
// plain Approach() lerps below obey it; this is just the old name pointing at it
#define g_animMul Cael::g_animSpeed
static bool  g_reduceMotion=false;
// exponential approach; snaps when close so idle frames settle exactly
static void Approach(float& v,float target,float speed){
    v += (target-v)*std::min(1.0f,g_frameDt*speed*g_animMul);
    if(fabsf(target-v)<0.0015f) v=target;
}
// Real damped-spring integrator, ported from the WindowPaper engine (Z:\WindowPaper1-swww-parity).
// An exponential lerp can never overshoot, which is exactly what makes the reference shell's
// carousel feel alive — so the cover-flow uses this instead.
struct Spring {
    float pos=0, vel=0, target=0, k=280.f, d=24.f;
    void step(float dt){ float f=k*(target-pos)-d*vel; vel+=f*dt; pos+=vel*dt; }
    void snap(){ pos=target; vel=0; }
    bool settled(float e=0.5f) const { return fabsf(pos-target)<e && fabsf(vel)<e; }
};
// Perspective-tilt every vertex emitted since `v0`, hinged on the horizontal line through `c`.
// Panels open by standing up out of the desktop plane instead of fading in flat. The hinge is put
// on the panel's BOTTOM edge and the angle is negative, so the whole panel only ever recedes
// (s<=1) — that keeps it inside any clip rect pushed while drawing it.
static void Warp3D(ImDrawList* dl,int v0,ImVec2 c,float angle,float f=1500.0f){
    if(fabsf(angle)<0.0006f) return;
    float ca=cosf(angle), sa=sinf(angle);
    for(int i=v0;i<dl->VtxBuffer.Size;i++){
        ImVec2 p=dl->VtxBuffer[i].pos; float x=p.x-c.x, y=p.y-c.y;
        float yr=y*ca, z=y*sa, s=f/(f+z); if(s<0.05f) s=0.05f;
        dl->VtxBuffer[i].pos=ImVec2(c.x+x*s, c.y+yr*s);
    }
}
// Uniform scale about a point, applied to everything a block emitted - the same trick Warp3D uses,
// which is the only way to transform text and images together in an immediate-mode list.
static void ScaleVerts(ImDrawList* dl,int v0,ImVec2 c,float s){
    if(fabsf(s-1.0f)<0.0005f) return;
    for(int i=v0;i<dl->VtxBuffer.Size;i++){
        ImVec2 p=dl->VtxBuffer[i].pos;
        dl->VtxBuffer[i].pos=ImVec2(c.x+(p.x-c.x)*s, c.y+(p.y-c.y)*s);
    }
}
// ---- hover tilt -------------------------------------------------------------------------
// Every card/chip/row can tilt to face the cursor. Wrap a block of drawing in TiltBegin/TiltEnd
// and the vertices it emitted get a small perspective rotation whose near corner is the one under
// the mouse. The depth field is biased so z>=0 everywhere (s<=1): the element only ever shrinks,
// never grows, which keeps it inside any clip rect AND keeps the point under the cursor at scale
// 1 — so you click exactly what you are pointing at.
static float g_tiltAmount=0.18f;                       // 0 = flat; config appearance.tilt
static std::unordered_map<int,ImVec2> g_tiltMap;
static ImVec2 TiltAnim(int id, ImVec2 target){
    ImVec2& v=g_tiltMap[id]; float k=std::min(1.0f,g_frameDt*13.0f);
    v.x+=(target.x-v.x)*k; v.y+=(target.y-v.y)*k;
    if(fabsf(target.x-v.x)<0.002f) v.x=target.x;
    if(fabsf(target.y-v.y)<0.002f) v.y=target.y;
    return v;
}
static int TiltBegin(ImDrawList* dl){ return dl->VtxBuffer.Size; }
static void TiltEnd(ImDrawList* dl,int v0,ImVec2 a,ImVec2 b,int id,float strength=1.0f){
    if(g_tiltAmount<=0.001f || v0>=dl->VtxBuffer.Size) return;
    ImVec2 c=V((a.x+b.x)*0.5f,(a.y+b.y)*0.5f);
    float hw=std::max(2.0f,(b.x-a.x)*0.5f), hh=std::max(2.0f,(b.y-a.y)*0.5f);
    ImGuiIO& io=ImGui::GetIO();
    bool hov = io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;
    ImVec2 t = hov ? V(std::clamp((io.MousePos.x-c.x)/hw,-1.0f,1.0f),
                       std::clamp((io.MousePos.y-c.y)/hh,-1.0f,1.0f))
                   : V(0,0);
    ImVec2 n = TiltAnim(id,t);
    if(fabsf(n.x)<0.004f && fabsf(n.y)<0.004f) return;
    float k=g_tiltAmount*strength;
    float bias=(hw*fabsf(n.x)+hh*fabsf(n.y))*k;        // shifts the whole plane back so z>=0
    float f=std::max(520.0f,(hw+hh)*3.0f);
    for(int i=v0;i<dl->VtxBuffer.Size;i++){
        ImVec2 p=dl->VtxBuffer[i].pos; float x=p.x-c.x, y=p.y-c.y;
        float z=-(x*n.x+y*n.y)*k + bias;
        float s=f/(f+z); if(s<0.05f) s=0.05f;
        dl->VtxBuffer[i].pos=V(c.x+x*s, c.y+y*s);
    }
}
// swww easings used by the transition engine
static double E3o (double t){ return 1.0-std::pow(1.0-t,3.0); }                       // ease-out cubic
static double E3io(double t){ if(t<0.5) return 4*t*t*t; double p=t*2-2; return 1+(p*p*p)/2; }
// stagger: item i of a cascade driven by a 0..1 master value
static float Stagger(float a,int i,float per=0.045f,float span=0.55f){
    return std::clamp((a-i*per)/span,0.0f,1.0f);
}

// A frosted panel with depth: layered drop shadow, the acrylic/image background, a top-to-bottom
// sheen and a light/dark rim. Every floating surface in the shell goes through this so they all
// share the same material.
// fillOverride: 0..255 to replace the panel's own fill/tint alpha, or -1 to use the global one.
// Without it every surface in the shell shared g_drawerAlpha and could not be themed apart.
// A light wash that is brightest at the top edge and fades SMOOTHLY to nothing by `frac` of the height.
// It replaces four flat washes (panels, the media page, toasts, the dock) that each stopped dead partway
// down - a hard line across every surface that testers read as a separator dividing the colours. The
// shape is the panel's own rounded rect, so the corners stay clean; only the vertex alpha is ramped.
static void TopWash(ImDrawList* dl, ImVec2 a, ImVec2 b, int alpha, float rnd, ImDrawFlags fl, float frac=0.5f){
    if(alpha<=0 || b.y<=a.y) return;
    float end=a.y+(b.y-a.y)*std::clamp(frac,0.05f,1.0f);
    int v0=dl->VtxBuffer.Size;
    dl->AddRectFilled(a, V(b.x,end), IM_COL32(255,255,255,alpha), rnd, fl&(~ImDrawFlags_RoundCornersBottom));
    for(int i=v0;i<dl->VtxBuffer.Size;i++){
        ImDrawVert& v=dl->VtxBuffer[i];
        float t=std::clamp((v.pos.y-a.y)/(end-a.y),0.0f,1.0f);
        float k=(1.0f-t)*(1.0f-t);                               // eases out - no visible bottom edge
        int va=(int)(((v.col>>IM_COL32_A_SHIFT)&0xFF)*k);
        v.col=(v.col&~IM_COL32_A_MASK)|((ImU32)va<<IM_COL32_A_SHIFT);
    }
}
static void GlassPanel(ImDrawList* dl, ImVec2 a, ImVec2 b, float rnd, ImDrawFlags fl,
                       ID3D11ShaderResourceView* frost, float alpha, int fillOverride=-1){
    if(alpha<=0.002f) return;
    if(g_panelShadow)
        for(int i=6;i>0;i--){ float e=i*2.4f;
            dl->AddRect(V(a.x-e,a.y-e+3),V(b.x+e,b.y+e+3), IM_COL32(0,0,0,(int)(11*alpha)), rnd+e, fl, e*0.9f); }
    ID3D11ShaderResourceView* bg = (g_bgMode==1 && !g_bgFrames.empty())
        ? g_bgFrames[g_bgFrame % (int)g_bgFrames.size()] : (g_bgMode==0 ? frost : nullptr);
    if(bg){
        dl->AddImageRounded((ImTextureID)bg, a, b, ImVec2(0,0),ImVec2(1,1), IM_COL32(255,255,255,(int)(255*alpha)), rnd, fl);
        // Coverage of the frost by the panel colour. This was raised once to chase an unreadable
        // dashboard, but that turned out to be the Media page painting its album-art wash over the
        // Dashboard (see drawPage/g_pageBleed) - not translucency. With that fixed and the frost now
        // a real 64px blur, the original curve reads fine, so the shell keeps the see-through look
        // it was designed around instead of the heavier one I introduced on a bad diagnosis.
        int tint = (fillOverride>=0)? (int)(fillOverride*alpha)
                                    : (int)(((1.0f-g_bgOpacity)*210+40)*alpha);
        dl->AddRectFilled(a, b, PanelCol(tint), rnd, fl);
    } else {
        int fa = (fillOverride>=0)? fillOverride : g_drawerAlpha;
        dl->AddRectFilled(a, b, PanelCol((int)(fa*alpha)), rnd, fl);
    }
    // sheen: a soft light wash across the upper part, fading out (it used to end in a hard line at 45%)
    TopWash(dl, a, b, (int)((g_darkUI?14:32)*alpha), rnd, fl, 0.6f);
    // ...and a glint travelling across it. Every drawer, flyout, menu and toast is a GlassPanel, so
    // this is the single edit that stops the shell's big surfaces reading as printed card stock.
    Sheen(dl, a, b, ShellPhase()*0.048f + a.y*0.0007f, (int)((g_darkUI?12:17)*alpha), 0.30f);
    // rim: bright along the top edge, dark along the bottom -> a bevel, breathing gently
    if(g_panelOutline){
      { float br=0.5f+0.5f*sinf(ShellPhase()*0.7f + a.x*0.006f);
        float k=0.82f+0.18f*br;
        dl->AddRect(V(a.x+0.5f,a.y+0.5f), V(b.x-0.5f,b.y-0.5f),
                    g_darkUI? IM_COL32(255,255,255,(int)(30*alpha*k)) : IM_COL32(255,255,255,(int)(150*alpha*k)), rnd, fl, 1.2f); }
      dl->AddRect(a, b, g_darkUI? IM_COL32(0,0,0,(int)(90*alpha)) : IM_COL32(0,0,0,(int)(34*alpha)), rnd, fl, 1.0f);
    }
}

// ---- textures / background / config ----
// ---- live texture accounting ---------------------------------------------------------------------
// A tiny COM object is attached to each texture with SetPrivateDataInterface. D3D holds the only
// reference and releases it when the texture is destroyed, so the tally is exact without wrapping
// every Release() in the program.
struct TexSite { long live=0; long long bytes=0; long made=0; };
static std::mutex g_texMtx;
static std::map<std::string,TexSite> g_texSites;
struct TexTracker final : IUnknown {
    std::atomic<long> ref{1}; std::string site; long long bytes;
    TexTracker(std::string s,long long b):site(std::move(s)),bytes(b){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** o) override {
        if(id==__uuidof(IUnknown)){ *o=this; AddRef(); return S_OK; } *o=nullptr; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)++ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        long r=--ref;
        if(r==0){ { std::lock_guard<std::mutex> lk(g_texMtx);
                    TexSite& ts=g_texSites[site]; ts.live--; ts.bytes-=bytes; }
                  delete this; }
        return (ULONG)r; }
};
static const GUID TEXTRACK_GUID={0x6ae7c0de,0x51b2,0x4c1e,{0x9a,0x33,0x7e,0x11,0x0a,0x55,0x42,0x19}};
static ID3D11ShaderResourceView* MakeTextureBGRA_(const void* bits,int w,int h,const char* fn,int line){
    if(!bits||w<=0||h<=0) return nullptr;
    D3D11_TEXTURE2D_DESC d={}; d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;
    d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sr={}; sr.pSysMem=bits; sr.SysMemPitch=w*4;
    ID3D11Texture2D* t=nullptr; if(FAILED(g_dev->CreateTexture2D(&d,&sr,&t))||!t)return nullptr;
    { char key[160]; snprintf(key,sizeof(key),"%s:%d",fn,line);
      long long bytes=(long long)w*h*4;
      TexTracker* tr=new TexTracker(key,bytes);
      { std::lock_guard<std::mutex> lk(g_texMtx); TexSite& ts=g_texSites[key]; ts.live++; ts.made++; ts.bytes+=bytes; }
      if(FAILED(t->SetPrivateDataInterface(TEXTRACK_GUID,tr))){        // not tracked: undo the tally
          std::lock_guard<std::mutex> lk(g_texMtx); TexSite& ts=g_texSites[key]; ts.live--; ts.made--; ts.bytes-=bytes; }
      tr->Release(); }                                                  // the texture owns it now
    ID3D11ShaderResourceView* srv=nullptr; g_dev->CreateShaderResourceView(t,nullptr,&srv); t->Release(); return srv;
}
int ImGui_ImplDX11_SharedFontInfo(int* w,int* h);   // imgui_impl_dx11.cpp (Aether patch)
static void TexDump(){
    PROCESS_MEMORY_COUNTERS_EX pm{}; pm.cb=sizeof(pm);
    GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&pm,sizeof(pm));
    std::vector<std::pair<std::string,TexSite>> v;
    { std::lock_guard<std::mutex> lk(g_texMtx); for(auto& e:g_texSites) v.push_back(e); }
    std::sort(v.begin(),v.end(),[](auto& a,auto& b){ return a.second.bytes>b.second.bytes; });
    long long tot=0; long totN=0; for(auto& e:v){ tot+=e.second.bytes; totN+=e.second.live; }
    FILE* f=fopen((ExeDir()+"texdump.txt").c_str(),"a"); if(!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f,"=== %02d:%02d:%02d  private %.1f MB  working set %.1f MB  |  textures live %ld = %.1f MB\n",
            st.wHour,st.wMinute,st.wSecond,pm.PrivateUsage/1048576.0,pm.WorkingSetSize/1048576.0,totN,tot/1048576.0);
    { int aw=0,ah=0; int up=ImGui_ImplDX11_SharedFontInfo(&aw,&ah);
      fprintf(f,"  font atlas %dx%d = %.1f MB, uploaded %d time(s)\n",aw,ah,aw*ah*4/1048576.0,up); }
    for(auto& e:v) if(e.second.made>0)
        fprintf(f,"  %8.2f MB  live %5ld  made %6ld  %s\n",e.second.bytes/1048576.0,e.second.live,e.second.made,e.first.c_str());
    fclose(f);
}
// Sliding-window box blur. The running sum for x is the sum for x-1 plus the pixel entering the
// window minus the one leaving it, so a pass costs the SAME whether the radius is 2 or 40. Edges average
// only the in-bounds pixels, so they are not darkened toward black.
//
// Precision: the passes run on 8.8 fixed point and the result is DITHERED back to 8 bits. The old form
// truncated to whole 8-bit steps after every pass. A frost this wide turns the backdrop into very smooth,
// mostly dark gradients, and whole steps across a smooth dark gradient are visible contour lines - the
// lock screen showed concentric rings around every bright spot instead of a blur.
static void BoxBlur16(std::vector<uint16_t>& A,int w,int h,int r,int passes){
    if(w<1||h<1||r<1||passes<1) return;
    std::vector<uint16_t> T(A.size());
    for(int pass=0;pass<passes;pass++){
        for(int y=0;y<h;y++){                                   // rows: A -> T
            const uint16_t* row=&A[(size_t)y*w*3]; uint16_t* out=&T[(size_t)y*w*3];
            int s0=0,s1=0,s2=0,c=0, lo=0, hi=-1;
            for(int x=0;x<w;x++){
                int nlo=x-r, nhi=x+r; if(nlo<0)nlo=0; if(nhi>w-1)nhi=w-1;
                while(hi<nhi){ ++hi; const uint16_t* q=row+(size_t)hi*3; s0+=q[0]; s1+=q[1]; s2+=q[2]; ++c; }
                while(lo<nlo){ const uint16_t* q=row+(size_t)lo*3; s0-=q[0]; s1-=q[1]; s2-=q[2]; --c; ++lo; }
                uint16_t* d=out+(size_t)x*3; const int hc=c/2;
                d[0]=(uint16_t)((s0+hc)/c); d[1]=(uint16_t)((s1+hc)/c); d[2]=(uint16_t)((s2+hc)/c);
            }
        }
        for(int x=0;x<w;x++){                                   // columns: T -> A
            int s0=0,s1=0,s2=0,c=0, lo=0, hi=-1;
            for(int y=0;y<h;y++){
                int nlo=y-r, nhi=y+r; if(nlo<0)nlo=0; if(nhi>h-1)nhi=h-1;
                while(hi<nhi){ ++hi; const uint16_t* q=&T[((size_t)hi*w+x)*3]; s0+=q[0]; s1+=q[1]; s2+=q[2]; ++c; }
                while(lo<nlo){ const uint16_t* q=&T[((size_t)lo*w+x)*3]; s0-=q[0]; s1-=q[1]; s2-=q[2]; --c; ++lo; }
                uint16_t* d=&A[((size_t)y*w+x)*3]; const int hc=c/2;
                d[0]=(uint16_t)((s0+hc)/c); d[1]=(uint16_t)((s1+hc)/c); d[2]=(uint16_t)((s2+hc)/c);
            }
        }
    }
}
static void Load16(const uint8_t* p,size_t n,std::vector<uint16_t>& A){
    A.resize(n*3);
    for(size_t i=0;i<n;i++){ A[i*3]=(uint16_t)(p[i*4]<<8); A[i*3+1]=(uint16_t)(p[i*4+1]<<8); A[i*3+2]=(uint16_t)(p[i*4+2]<<8); }
}
// A fixed per-pixel dither in 1/256ths of an 8-bit step: the same pixel gets the same offset every time,
// so a frost that is captured again never shimmers.
static inline uint32_t DitherHash(int x,int y){
    uint32_t h=(uint32_t)x*0x8DA6B343u ^ (uint32_t)y*0xD8163841u; h^=h>>13; h*=0x5bd1e995u; h^=h>>15; return h; }
static void Store16Dither(const std::vector<uint16_t>& A,int w,int h,uint8_t* p){
    for(int y=0;y<h;y++) for(int x=0;x<w;x++){
        const uint32_t hs=DitherHash(x,y); const size_t i=(size_t)y*w+x;
        for(int k=0;k<3;k++){ int v=((int)A[i*3+k]+(int)((hs>>(k*8))&255))>>8; p[i*4+k]=(uint8_t)(v>255?255:v); }
        p[i*4+3]=255; }
}
static void BoxBlur(uint8_t* p,int w,int h,int r,int passes){
    if(w<1||h<1||r<1||passes<1) return;
    std::vector<uint16_t> A; Load16(p,(size_t)w*h,A);
    BoxBlur16(A,w,h,r,passes);
    Store16Dither(A,w,h,p);
}
// The blur is computed small (a quarter of the screen each way for a full-screen frost) and used to be
// stretched up by the GPU. That smooths the dither away again and brings the rings back, so the full-size
// image is made here instead: bilinear in 8.8 fixed point, dithered at the size it is shown at.
static ID3D11ShaderResourceView* Blur16Upscaled(const std::vector<uint16_t>& A,int w,int h,int ow,int oh){
    if(ow<=w || oh<=h){ std::vector<uint8_t> px((size_t)w*h*4); Store16Dither(A,w,h,px.data()); return MakeTextureBGRA(px.data(),w,h); }
    std::vector<uint8_t> px((size_t)ow*oh*4);
    const int sxStep=(int)(((int64_t)w<<16)/ow), syStep=(int)(((int64_t)h<<16)/oh);
    for(int y=0;y<oh;y++){
        int fy=(int)(((int64_t)y*syStep)+(syStep>>1)-32768); if(fy<0) fy=0;
        int y0=fy>>16, y1=std::min(y0+1,h-1); int wy=(fy>>8)&255; if(y0>h-1){ y0=y1=h-1; wy=0; }
        const uint16_t* r0=&A[(size_t)y0*w*3]; const uint16_t* r1=&A[(size_t)y1*w*3];
        uint8_t* out=&px[(size_t)y*ow*4];
        for(int x=0;x<ow;x++){
            int fx=(int)(((int64_t)x*sxStep)+(sxStep>>1)-32768); if(fx<0) fx=0;
            int x0=fx>>16, x1=std::min(x0+1,w-1); int wx=(fx>>8)&255; if(x0>w-1){ x0=x1=w-1; wx=0; }
            const uint32_t hs=DitherHash(x,y);
            for(int k=0;k<3;k++){
                int a0=r0[x0*3+k], a1=r0[x1*3+k], b0=r1[x0*3+k], b1=r1[x1*3+k];
                int top=a0+(((a1-a0)*wx)>>8), bot=b0+(((b1-b0)*wx)>>8);
                int v=top+(((bot-top)*wy)>>8);                  // still 8.8
                v=(v+(int)((hs>>(k*8))&255))>>8; out[x*4+k]=(uint8_t)(v>255?255:(v<0?0:v));
            }
            out[x*4+3]=255;
        }
    }
    return MakeTextureBGRA(px.data(),ow,oh);
}
// capture a screen region and blur it -> acrylic frost (used by drawer + dock)
static bool SavePng(const std::wstring& path,const uint8_t* bgra,int w,int h);   // fwd (33_snip.cpp)
static bool g_blurDump=false;   // -s blur_dump: the next CaptureRegionBlur saves its source and result as PNGs
static ID3D11ShaderResourceView* CaptureRegionBlur(int sx,int sy,int iw,int ih){
    // Cost scales with the captured AREA (the radius is free now that BoxBlur slides a window), so
    // large captures still shrink harder before blurring: ~4x fewer pixels for the full-screen
    // Settings / session / lock frost. Small captures (the bar's own acrylic strip, ~1900x39) stay
    // at half res - quartering a 39px-tall strip leaves almost nothing to blur.
    int shrink = ((long long)iw*ih > 800000) ? 4 : 2;
    int dw=iw/shrink, dh=ih/shrink; if(dw<2||dh<2) return nullptr;
    HDC scr=GetDC(nullptr),mem=CreateCompatibleDC(scr);
    BITMAPINFO bi={}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=dw; bi.bmiHeader.biHeight=-dh; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr; HBITMAP bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0); HGDIOBJ old=SelectObject(mem,bmp);
    SetStretchBltMode(mem,HALFTONE); StretchBlt(mem,0,0,dw,dh,scr,sx,sy,iw,ih,SRCCOPY);
    // Radius chosen in SCREEN pixels and then converted, so the frost looks the same whether this
    // capture was shrunk by 2 or by 4. Radius 6 at half res (12px) left a busy backdrop - a video,
    // album art, a page of text - still legible straight through the panel, which is what made the
    // dashboard unreadable over a light wallpaper. 36px is a real frost: colour and luminance carry
    // through, shapes and text do not. Two passes of a box blur approximate a Gaussian well enough.
    // One texel of this buffer covers `shrink` screen pixels, so the texel radius is
    // RAD_SCREEN/shrink. Dividing by shrink*2 (the first attempt) silently halved the frost.
    const int RAD_SCREEN = 64;
    int rad = std::max(2, RAD_SCREEN / shrink);
    std::vector<uint16_t> A; Load16((const uint8_t*)bits,(size_t)dw*dh,A);
    BoxBlur16(A,dw,dh,rad,2);
    if(g_blurDump){ g_blurDump=false;
        for(int i=0;i<dw*dh;i++)((uint8_t*)bits)[i*4+3]=255;
        SavePng(U82W(ExeDir()+"blur_src.png"),(const uint8_t*)bits,dw,dh);          // (bits still holds the grab)
        std::vector<uint8_t> o((size_t)dw*dh*4); Store16Dither(A,dw,dh,o.data());
        SavePng(U82W(ExeDir()+"blur_out.png"),o.data(),dw,dh); }
    ID3D11ShaderResourceView* tex=Blur16Upscaled(A,dw,dh,iw,ih);   // full size, dithered - see Blur16Upscaled
    SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,scr);
    return tex;
}
// Capture ONCE, then hand back progressively blurrier copies of the same frame. Level 0 is the
// sharp grab; each later level is another blur pass over the SAME buffer, so level N costs one
// pass rather than a fresh capture-and-blur. Cross-fading between adjacent levels reads as a lens
// pulling out of focus - a single blurred image faded in over the desktop only ever reads as a
// ghost sliding over it, which is what the picker's backdrop was doing.
static void CaptureBlurLevels(int sx,int sy,int iw,int ih,
                              ID3D11ShaderResourceView** out,const int* radii,int levels){
    for(int i=0;i<levels;i++) out[i]=nullptr;
    int shrink = ((long long)iw*ih > 800000) ? 4 : 2;
    int dw=iw/shrink, dh=ih/shrink; if(dw<2||dh<2) return;
    HDC scr=GetDC(nullptr),mem=CreateCompatibleDC(scr);
    BITMAPINFO bi={}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=dw;
    bi.bmiHeader.biHeight=-dh; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr; HBITMAP bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    HGDIOBJ old=SelectObject(mem,bmp);
    SetStretchBltMode(mem,HALFTONE); StretchBlt(mem,0,0,dw,dh,scr,sx,sy,iw,ih,SRCCOPY);
    for(int i=0;i<dw*dh;i++)((uint8_t*)bits)[i*4+3]=255;
    for(int i=0;i<levels;i++){
        int r = radii[i]/shrink;                 // radii are given in SCREEN pixels
        if(r>=2) BoxBlur((uint8_t*)bits,dw,dh,r,2);
        for(int p=0;p<dw*dh;p++)((uint8_t*)bits)[p*4+3]=255;
        out[i]=MakeTextureBGRA(bits,dw,dh);
    }
    SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,scr);
}
// load a PNG (1 frame) or animated GIF (N frames) via WIC
static void LoadBgImage(const std::string& path){
    for(auto t:g_bgFrames) if(t)t->Release(); g_bgFrames.clear(); g_bgDelays.clear(); g_bgFrame=0; g_bgClock=0;
    if(path.empty())return;
    IWICImagingFactory* fac=nullptr;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&fac))))return;
    std::wstring wp=U82W(path); IWICBitmapDecoder* dec=nullptr;
    if(SUCCEEDED(fac->CreateDecoderFromFilename(wp.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec))){
        UINT n=0; dec->GetFrameCount(&n);
        for(UINT i=0;i<n;i++){
            IWICBitmapFrameDecode* fr=nullptr; if(FAILED(dec->GetFrame(i,&fr)))continue;
            IWICFormatConverter* conv=nullptr; fac->CreateFormatConverter(&conv);
            if(conv && SUCCEEDED(conv->Initialize(fr,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeMedianCut))){
                UINT w=0,h=0; conv->GetSize(&w,&h);
                if(w&&h){ std::vector<uint8_t> buf((size_t)w*h*4); conv->CopyPixels(nullptr,w*4,(UINT)buf.size(),buf.data());
                    if(g_bgBlur) BoxBlur(buf.data(),w,h,5,2);
                    ID3D11ShaderResourceView* tex=MakeTextureBGRA(buf.data(),w,h); if(tex) g_bgFrames.push_back(tex);
                    int delay=100; IWICMetadataQueryReader* mq=nullptr;
                    if(SUCCEEDED(fr->GetMetadataQueryReader(&mq))){ PROPVARIANT v; PropVariantInit(&v);
                        if(SUCCEEDED(mq->GetMetadataByName(L"/grctlext/Delay",&v)) && v.vt==VT_UI2){ delay=v.uiVal*10; if(delay<20)delay=100; }
                        PropVariantClear(&v); mq->Release(); }
                    g_bgDelays.push_back(delay);
                }
            }
            if(conv)conv->Release(); fr->Release();
        }
        dec->Release();
    }
    fac->Release();
}

// ---- Windows account picture -----------------------------------------------------------
// Windows stores the sign-in avatar per-SID. The registry holds ready-made paths at every size
// Windows itself renders (Image1080 down to Image32); we take the largest that exists and let the
// GPU downscale. Falls back to the roaming AccountPictures folder, then the stock user.png.
static ID3D11ShaderResourceView* g_avatar=nullptr;
static bool  g_avatarTried=false;
static std::wstring CurrentUserSidString(){
    HANDLE tok=nullptr; std::wstring out;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&tok)) return out;
    DWORD need=0; GetTokenInformation(tok,TokenUser,nullptr,0,&need);
    std::vector<BYTE> buf(need?need:1);
    if(need && GetTokenInformation(tok,TokenUser,buf.data(),need,&need)){
        LPWSTR s=nullptr;
        if(ConvertSidToStringSidW(((TOKEN_USER*)buf.data())->User.Sid,&s) && s){ out=s; LocalFree(s); }
    }
    CloseHandle(tok); return out;
}
static std::wstring FindAccountPicture(){
    std::wstring sid=CurrentUserSidString();
    if(!sid.empty()){
        std::wstring key=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AccountPicture\\Users\\"+sid;
        // largest first — Windows only writes the sizes it has actually generated
        static const wchar_t* SIZES[]={L"Image1080",L"Image448",L"Image424",L"Image240",
                                       L"Image208",L"Image192",L"Image96",L"Image48",L"Image40",L"Image32"};
        for(const wchar_t* v:SIZES){
            std::wstring p=RegString(HKEY_LOCAL_MACHINE,key.c_str(),v);
            if(!p.empty() && GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES) return p;
        }
    }
    // roaming copy: %APPDATA%\Microsoft\Windows\AccountPictures\*.png|jpg
    wchar_t ap[MAX_PATH]={0};
    if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_APPDATA,nullptr,0,ap))){
        std::wstring dir=std::wstring(ap)+L"\\Microsoft\\Windows\\AccountPictures";
        for(const wchar_t* ext:{L"\\*.png",L"\\*.jpg",L"\\*.jpeg",L"\\*.bmp"}){
            WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW((dir+ext).c_str(),&fd);
            if(h!=INVALID_HANDLE_VALUE){ std::wstring hit=dir+L"\\"+fd.cFileName; FindClose(h); return hit; }
        }
    }
    // the stock silhouette Windows ships
    std::wstring stock=L"C:\\ProgramData\\Microsoft\\User Account Pictures\\user.png";
    if(GetFileAttributesW(stock.c_str())!=INVALID_FILE_ATTRIBUTES) return stock;
    return L"";
}
static bool DecodeFirstFrame(const std::wstring& path,int maxDim,std::vector<uint8_t>& out,int& ow,int& oh);  // fwd
static void LoadAvatar(){
    if(g_avatarTried) return;
    g_avatarTried=true;
    std::wstring p=FindAccountPicture(); if(p.empty()) return;
    std::vector<uint8_t> px; int w=0,h=0;
    if(DecodeFirstFrame(p,256,px,w,h) && w>0 && h>0) g_avatar=MakeTextureBGRA(px.data(),w,h);
}

// decode an image (first frame), downscaled, to BGRA — for wallpaper colour analysis
static bool DecodeFirstFrame(const std::wstring& path,int maxDim,std::vector<uint8_t>& out,int& ow,int& oh){
    IWICImagingFactory* fac=nullptr; if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&fac))))return false;
    bool ok=false; IWICBitmapDecoder* dec=nullptr;
    if(SUCCEEDED(fac->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec))){
        IWICBitmapFrameDecode* fr=nullptr;
        if(SUCCEEDED(dec->GetFrame(0,&fr))){
            UINT w=0,h=0; fr->GetSize(&w,&h);
            if(w&&h){ int sw=(w>=h)?maxDim:std::max(1,(int)(maxDim*(double)w/h)); int sh=(h>=w)?maxDim:std::max(1,(int)(maxDim*(double)h/w));
                IWICBitmapScaler* sc=nullptr; fac->CreateBitmapScaler(&sc);
                if(sc && SUCCEEDED(sc->Initialize(fr,sw,sh,WICBitmapInterpolationModeFant))){
                    IWICFormatConverter* cv=nullptr; fac->CreateFormatConverter(&cv);
                    if(cv && SUCCEEDED(cv->Initialize(sc,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeMedianCut))){
                        out.resize((size_t)sw*sh*4); if(SUCCEEDED(cv->CopyPixels(nullptr,sw*4,(UINT)out.size(),out.data()))){ ow=sw;oh=sh;ok=true; }
                    }
                    if(cv)cv->Release();
                }
                if(sc)sc->Release();
            }
            fr->Release();
        }
        dec->Release();
    }
    fac->Release(); return ok;
}
// ---------------------------------------------------------------------------------------------
// BAR LOGO — the strip used to draw a hand-coded ring+dot, which the user called bland. These are
// the REAL distro marks, fetched into assets/logos/ as 512px RGBA PNGs (source:
// github.com/homarr-labs/dashboard-icons). They are trademarks of their projects, used here as
// local UI decoration, which is why they are shipped as swappable assets rather than baked in.
// WIC cannot decode SVG, so PNG is deliberate - the upstream CachyOS art is SVG-only.
static std::string ExeDir();   // fwd: defined below, needed here to locate the logo assets
struct BarLogoDef{ const char* key; const char* label; const char* file; };
static const BarLogoDef BAR_LOGOS[]={
    {"default", "Aether ring (built-in)", nullptr             },
    {"cachyos", "CachyOS",                "cachyos-linux.png" },
    {"arch",    "Arch Linux",             "arch-linux.png"    },
    {"hyprland","Hyprland",               "hyprland.png"      },
    {"tux",     "Tux (Linux)",            "linux.png"         },
    {"nixos",   "NixOS",                  "nixos.png"         },
    {"fedora",  "Fedora",                 "fedora.png"        },
    {"garuda",  "Garuda Linux",           "garuda-linux.png"  },
    {"windows", "Windows 11",             "windows-11.png"    },
};
static const int NBARLOGOS=(int)(sizeof(BAR_LOGOS)/sizeof(BAR_LOGOS[0]));
static std::string g_barLogo="cachyos";   // the user dual-boots CachyOS; "default" restores the ring+dot
// OsIcon.qml puts the distro mark behind a ColouredIcon tinted m3tertiary, so upstream shows a flat
// accent silhouette. Off gives the vendor artwork its own colours.
static bool g_barLogoTint = true;
// Config.bar.workspaces.shown (default 5). Caelestia ALWAYS draws this many slots - the empty ones
// are the small dots in the reference frames - so the workspace pill is a fixed landmark instead of
// appearing and vanishing with the desktop count. Windows starts every session with exactly one
// virtual desktop, which is why our strip had no workspaces on it at all.
static int g_wsShown=5;
// Where the workspaces come from. Caelestia reads Hyprland; on Windows the honest equivalent is
// komorebi (a real tiling WM with named, per-monitor workspace rings and an event stream), and
// virtual desktops are the fallback for machines that do not run it.
enum { WSSRC_AUTO=0, WSSRC_KOMOREBI, WSSRC_VDESK };
static int g_wsSource=WSSRC_AUTO;
// A Wayland shell declares an exclusive zone and the compositor tiles around it. komorebi has the
// same idea as monitor-work-area-offset, but nothing tells it the offset - so without this the bar
// gets tiled straight over, which is exactly what happened on the second display.
static bool g_komoReserve=true;
// Keep explorer.exe alive alongside the shell. OFF by default: replacing explorer is the whole
// point of a shell replacement, and the isolated test showed it is NOT required (see
// EnsureExplorerForKomorebi). Opt-in only.
static bool g_keepExplorer=false;
static bool g_komoAutoStart=false;   // launch komorebi with the shell if it is not already up
static bool g_komoMasir=false;       // start it with --masir (focus follows mouse, like niri)
static bool g_komoWhkd=true;         // start it with --whkd so the user's keybindings actually run
static bool g_niriMode=false;        // komorebi's `scrolling` layout on every workspace
static int  g_niriCols=2;            // visible columns in that layout
static bool g_komoAutoAdopt=true;    // hand already-open windows to komorebi when it comes up
// --komolog, or "komoLog": true in config.json. The CLI flag alone is not enough in practice: when
// Aether is the shell, WinLogon relaunches it WITHOUT arguments, so a flag passed by hand survives
// exactly until the next restart - which is precisely when a komorebi problem needs watching.
// Declared here rather than in Komorebi.h because the config loader runs long before that include.
static bool g_komoLog = false;
static const char* WSSRC_KEY[3]   = {"auto","komorebi","desktops"};
static const char* WSSRC_LABEL[3] = {"Auto","komorebi","Virtual desktops"};
static ID3D11ShaderResourceView* g_barLogoTex=nullptr;
static std::string g_barLogoTexKey="\x01";      // deliberately != any key, so the first call loads
static int g_barLogoW=0, g_barLogoH=0;
// Lazy + cached: the texture is only rebuilt when the chosen key actually changes, so this is safe
// to call from the render loop on every frame and on every monitor's bar.
static ID3D11ShaderResourceView* BarLogoTex(int& lw,int& lh){
    if(g_barLogoTexKey!=g_barLogo){
        if(g_barLogoTex){ g_barLogoTex->Release(); g_barLogoTex=nullptr; }
        g_barLogoW=g_barLogoH=0; g_barLogoTexKey=g_barLogo;
        const BarLogoDef* d=nullptr;
        for(int i=0;i<NBARLOGOS;i++) if(g_barLogo==BAR_LOGOS[i].key){ d=&BAR_LOGOS[i]; break; }
        if(d && d->file){
            std::vector<uint8_t> px; int w=0,h=0;
            if(DecodeFirstFrame(U82W(ExeDir()+"assets\\logos\\"+d->file),128,px,w,h) && w>0 && h>0){
                g_barLogoTex=MakeTextureBGRA(px.data(),w,h); g_barLogoW=w; g_barLogoH=h; } }
    }
    lw=g_barLogoW; lh=g_barLogoH; return g_barLogoTex;
}
// Fit the mark inside a square box without distorting it - several of these logos are not square
// (CachyOS is 668x512, Hyprland 395x512), so a naive square draw visibly stretches them.
// tint: OsIcon.qml wraps the distro mark in a ColouredIcon, so upstream draws it as a flat
// m3tertiary silhouette rather than in the vendor's own colours. Pass 0 to keep the artwork's
// colours (Settings > Taskbar > Logo colour).
// ---- the Aether mark ----------------------------------------------------------------------------
// Two earlier attempts were wrong for the same reason: they were generic. Nested triangles are the
// Arch mark with the middle knocked out; a letter inside a circle is the placeholder-app-icon
// formula. Three other directions were drawn and rejected on sight - a prism reads as Dark Side of
// the Moon, concentric arcs read as a wifi icon, and nested pentagons turn to mush at 21px.
//
// This is the AE ligature: it spells the name, it is a real typographic form rather than a
// geometric cliche, and it survives being shrunk. The 9-degree slant gives it forward motion. The
// A's crossbar is what makes it read as an A rather than a slash, so it is kept at every size and
// the middle arm of the E - which is decoration - is what gets dropped when there is no room.
//
// A spark runs the natural stroke of it (up the diagonal, down the stem, out along the foot), which
// is the mark's share of the shell's idle motion.
static void AetherMark(ImDrawList* dl, ImVec2 c, float r, ImU32 col){
    if(r < 3.0f){ dl->AddCircleFilled(c, std::max(1.0f,r), col); return; }
    const float SLANT = 0.16f;
    float t = std::max(1.2f, r*0.185f);
    auto P = [&](float x, float y){ return V(c.x + r*(x - y*SLANT), c.y + r*y); };

    const float STEM = 0.06f;
    ImVec2 foot  = P(-0.98f, 0.95f);
    ImVec2 apex  = P(STEM, -0.95f);
    ImVec2 base  = P(STEM,  0.95f);

    dl->AddLine(foot, apex, col, t);                       // the A's rising stroke
    dl->AddLine(apex, base, col, t);                       // the stem, shared by A and E
    dl->AddLine(apex, P(0.88f,-0.95f), col, t*0.92f);      // E: top arm
    dl->AddLine(base, P(0.88f, 0.95f), col, t*0.92f);      // E: foot
    // The A's crossbar and the E's middle arm sit at the SAME height and read as ONE bar running
    // the full width of the ligature. Drawn at different heights they looked like a rendering
    // fault rather than a letterform; unified, the bar is the thing that ties A and E together.
    dl->AddLine(P(-0.56f,0.12f), P(0.62f,0.12f), col, t*0.85f);

    // the spark, running foot -> apex -> base -> out along the foot arm
    if(r >= 7.0f){
        ImVec2 path[4] = { foot, apex, base, P(0.80f,0.95f) };
        float seg[3];
        float total=0.0f;
        for(int k=0;k<3;k++){
            float dx=path[k+1].x-path[k].x, dy=path[k+1].y-path[k].y;
            seg[k]=sqrtf(dx*dx+dy*dy); total+=seg[k];
        }
        if(total>1.0f){
            float d = fmodf(ShellPhase()*0.22f, 1.0f) * total;
            for(int k=0;k<3;k++){
                if(d > seg[k]){ d -= seg[k]; continue; }
                float u = seg[k]>0.001f ? d/seg[k] : 0.0f;
                ImVec2 sp = V(path[k].x+(path[k+1].x-path[k].x)*u,
                              path[k].y+(path[k+1].y-path[k].y)*u);
                dl->AddCircleFilled(sp, t*0.85f, col);
                dl->AddCircleFilled(sp, t*1.90f, WithA(col, 30));
                break;
            }
        }
    }
}

static bool DrawLogoMark(ImDrawList* dl, ImVec2 c, float box, float alpha, ImU32 tint=0){
    int lw=0,lh=0; ID3D11ShaderResourceView* t=BarLogoTex(lw,lh);
    if(!t||lw<=0||lh<=0) return false;
    float sc=box/(float)std::max(lw,lh), w=lw*sc, h=lh*sc;
    int a=(int)(255*std::clamp(alpha,0.0f,1.0f));
    ImU32 col = tint? WithA(tint,a) : IM_COL32(255,255,255,a);
    dl->AddImage((ImTextureID)t,V(c.x-w*0.5f,c.y-h*0.5f),V(c.x+w*0.5f,c.y+h*0.5f),
                 ImVec2(0,0),ImVec2(1,1),col);
    return true;
}

// recolor the accent from the desktop wallpaper (vibrant-weighted average) — Material-You
// The DECODE half: opening a 4K wallpaper is slow, so this is written to be callable from a
// worker thread. It touches no globals and only reads the wallpaper path.
static bool WallpaperSeed(ImU32& out){
    wchar_t wp[MAX_PATH]={0}; if(!SystemParametersInfoW(SPI_GETDESKWALLPAPER,MAX_PATH,wp,0)||!wp[0])return false;
    std::vector<uint8_t> px; int w=0,h=0; if(!DecodeFirstFrame(wp,72,px,w,h))return false;
    double ar=0,ag=0,ab=0,wsum=0;
    for(int i=0;i<w*h;i++){ double b=px[i*4],g=px[i*4+1],r=px[i*4+2];
        double mx=std::max(std::max(r,g),b),mn=std::min(std::min(r,g),b);
        double v=mx/255.0, s=mx>0?(mx-mn)/mx:0; double wt=s*s*v;   // favour colourful, bright pixels
        ar+=r*wt; ag+=g*wt; ab+=b*wt; wsum+=wt; }
    if(wsum<1e-6)return false;
    out = IM_COL32((int)std::clamp(ar/wsum,0.0,255.0),
                   (int)std::clamp(ag/wsum,0.0,255.0),
                   (int)std::clamp(ab/wsum,0.0,255.0),255);
    return true;
}
// The APPLY half: pure arithmetic on an already-computed seed, so it is cheap enough to run on the
// render thread - which it must, because it writes the colour globals.
static void ApplySeedPalette(ImU32 seed){
    Pal palBefore=PalNow();
    int r=seed&0xFF, g=(seed>>8)&0xFF, b=(seed>>16)&0xFF;
    const Scheme& s = SCHEMES[std::clamp(g_scheme,0,NSCHEMES-1)];
    bool dark = s.dark; g_darkUI = dark;
    // Accent for on-surface UI (icons, clock, highlights): keep the seed's HUE but normalise its
    // lightness/chroma to a proper Material tone — matugen does this so the accent is a vivid, legible
    // colour rather than the muddy raw average (which reads as a dull dark-red on a red wallpaper). Dark
    // theme => a bright tone; light theme => a deeper tone. This is the single biggest "polish" lever.
    { float hh,ss,vv; ImGui::ColorConvertRGBtoHSV(r/255.f,g/255.f,b/255.f,hh,ss,vv);
      if(dark){ ss=std::clamp(ss,0.42f,0.82f); vv=std::max(vv,0.92f); }
      else    { ss=std::clamp(ss,0.55f,0.95f); vv=std::clamp(vv,0.45f,0.72f); }
      float rr,gg,bb; ImGui::ColorConvertHSVtoRGB(hh,ss,vv,rr,gg,bb);
      COL_GOLD = IM_COL32((int)(rr*255+0.5f),(int)(gg*255+0.5f),(int)(bb*255+0.5f),255); }
    COL_GOLDBG = dark ? IM_COL32(r/3+18,g/3+18,b/3+18,255)
                      : IM_COL32(std::min(255,r+150),std::min(255,g+120),std::min(255,b+120),255);
    float wS = dark?0.12f:0.09f;   // surface tint strength
    float wT = 0.05f;              // text tint (very subtle, keep contrast)
    COL_PANELL = Mix(s.panel,seed,wS); COL_BG = COL_PANELL;
    COL_CARD   = Mix(s.card ,seed,wS); COL_CARD2 = Mix(s.card2,seed,wS);
    COL_TRACK  = Mix(s.track,seed,wS);
    COL_DESKBG = Mix(s.deskbg,seed,dark?0.16f:0.11f);
    COL_INK    = Mix(s.ink ,seed,wT); COL_INK2 = Mix(s.ink2,seed,wT);
    COL_TABBG  = Mix(COL_PANELL,COL_CARD,0.5f); COL_ACCD = COL_GOLD;
    PalCommit(palBefore);
}
static void ApplyDynamicAccent(){ ImU32 seed; if(WallpaperSeed(seed)) ApplySeedPalette(seed); }

// take over the Windows taskbar (Cairo-style): hide Shell_TrayWnd + secondary bars and
// expand the work area so maximised windows fill the screen. Restore on exit.
// ---- desktop "bubble": the shell owns the whole screen; the desktop is an inset rounded rect ----
static bool  g_bubble=true; static int g_gap=10, g_gapLeft=46; static float g_bubbleRound=22.0f;
// LIVE bubble: instead of blitting a STATIC snapshot of the wallpaper (which freezes animated wallpapers
// like Wallpaper Engine), leave the bubble INTERIOR transparent so the real, live desktop wallpaper shows
// through (the desk layer sits at HWND_BOTTOM over per-pixel alpha), and paint only the rounded frame +
// recessed bevel. This is the Caelestia inset look without stealing the user's animated wallpaper.
static bool  g_deskLive=true;
static bool  g_deskClock=true;         // the big bottom-right wallpaper clock (Settings > Taskbar)
// Which dashboard tab is mirrored onto the wallpaper, by NAME so reordering the tabs cannot point
// this at the wrong one. Empty = nothing on the desktop.
static std::string g_deskTabName;
static void DrawDesktopClock(ImDrawList* dl,float x0,float y0,float x1,float y1);   // fwd
// ---- modern magnifying dock (bottom), macOS-style ---------------------------------------------------
static bool  g_dockOn=true;            // show the bottom dock at all
static bool  g_dockAutohide=false;     // reveal only when the cursor hits the bottom edge (else always shown)
static float g_dockIcon=46.0f;         // resting icon size (px)
static float g_dockMag=1.75f;          // peak magnification scale under the cursor (1 = no zoom)
static float g_dockMagRange=3.0f;      // magnify falloff radius, in icon-steps
static float g_dockGap=14.0f;          // gap between icons
static float g_dockRound=24.0f;        // container corner radius
static float g_dockOpacity=0.82f;      // container opacity 0..1
static bool  g_dockRunningOnly=false;  // (reserved) show only running apps vs pinned+running
static int   g_dockBounce=-1; static float g_dockBounceT=0; // which icon is mid launch-bounce
// pinned apps: exe paths that always sit in the dock, even when the app isn't running (declared
// early so LoadConfig can populate them; icons are built lazily in RefreshDock)
struct DockPin{ std::wstring exe; ID3D11ShaderResourceView* icon=nullptr; };
static std::vector<DockPin> g_dockPins;
static float g_uiScale=1.0f;
static bool  g_barAutoHide=true;   // taskbar right-click menu toggles this (persisted to config)
static bool  g_hideOnFullscreen=true;  // pull the whole shell off-screen over fullscreen apps/games
static bool  g_decoOn=false;           // Linux-style titlebar on the focused window (Settings > Window Behavior) - opt-in
// Show only the windows living on this bar's monitor, the way every real taskbar does.
static bool  g_barSameMonitor=true;
static bool  g_barPreviews=true;      // DWM live window previews on hover
static bool  g_isShell=false;          // set at startup: we are the shell this session
// Set by RestartShell(); read by the exit code at the very bottom of wWinMain.
static bool  g_restartOnExit=false;

// Restarting used to be: launch a second copy, then close this one. Those two race, and the race is
// usually LOST - the new copy starts while this one still holds the Local\AetherShell mutex, sees
// ERROR_ALREADY_EXISTS and exits at once; then this one exits with code 1, which tells WinLogon not
// to relaunch. The result is a session with no shell at all.
//
// So: when we ARE the shell, do not spawn anything. Exit with code 0 and let WinLogon relaunch us -
// that is what AutoRestartShell is for, and it cannot race with itself. When we are not the shell
// there is nothing to relaunch us, so hand the job to a detached command that waits for this process
// to be gone before starting the next one.
static void RestartShell(){
    if(g_isShell){
        g_restartOnExit=true;                 // -> exit code 0 -> WinLogon starts us again
    } else {
        wchar_t ex[MAX_PATH]; GetModuleFileNameW(nullptr,ex,MAX_PATH);
        std::wstring cmd=L"cmd /c ping 127.0.0.1 -n 3 >nul & start \"\" \""+std::wstring(ex)+L"\"";
        STARTUPINFOW si={sizeof(si)}; si.dwFlags=STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION pi={};
        std::vector<wchar_t> buf(cmd.begin(),cmd.end()); buf.push_back(0);
        if(CreateProcessW(nullptr,buf.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    }
    PostMessageW(g_hwnd,WM_CLOSE,0,0);
}
static bool g_winKeyLauncher=true;   // tapping Super opens the launcher (Settings > Shortcuts)
static bool g_swEnable=true;         // Alt+Tab shows OUR window switcher (config shortcuts.altTab).
enum { SWSTYLE_ROW=0, SWSTYLE_COVER };
static int   g_swStyle=SWSTYLE_ROW;   // shortcuts.switcherStyle
static bool  g_swReflect=true;        // mirrored cards under the coverflow carousel
                                     // Declared up here because SaveConfig/LoadConfig run long before
                                     // the switcher module itself.
static std::string g_catPath;        // image/GIF drawn on the media card instead of the drawn cat
static bool g_catDirty=false;        // reload it on the main thread (WIC needs COM there)

// ---- widget kinds + registry ----
enum { WK_PAGE_DASH=0, WK_PAGE_MEDIA, WK_PAGE_PERF, WK_PAGE_WEATHER,
       WK_CLOCK, WK_CALENDAR, WK_WEATHER, WK_PROFILE, WK_VOLUME,
       WK_CPU, WK_GPU, WK_MEMORY, WK_STORAGE, WK_NETWORK, WK_MEDIA,
       WK_IMAGE, WK_IMAGE_FILL, WK_TEXT,
       WK_M3_CPU, WK_M3_GPU, WK_M3_MEM, WK_M3_DISK,
       // Appended, never inserted: config stores widgets by string id but the palette and the
       // WREG table are indexed by this enum, so an insert would silently relabel every widget.
       WK_RING_CPU, WK_RING_MEM, WK_RING_DISK,
       WK_CUSTOM,                                   // a user widget: arg = file in config\widgets (.toml / .py)
       WK_USER,                                     // profile: avatar, presence, custom status, info rows
       // the newer Caelestia cards (CaelV2.h)
       WK_V2_HOME, WK_V2_MEDIAPAGE, WK_V2_PERFPAGE,
       WK_V2_WEATHER, WK_V2_USER, WK_V2_CLOCK, WK_V2_CALENDAR, WK_V2_RINGS, WK_V2_MEDIA,
       WK_V2_PLAYER, WK_V2_LYRICS, WK_V2_CPU, WK_V2_GPU, WK_V2_STORAGE, WK_V2_NETWORK, WK_V2_MEMORY,
       WK_TERMINAL,                                 // a real shell (Terminal.h)
       WK_MIXER,                                    // the audio mixer (AudioMixer.h)
       WK_COUNT };
struct WReg { const char* id; const char* label; const char* cat; float dw,dh; bool wantsArg; };
static const WReg WREG[WK_COUNT] = {
    {"page_dash",  "Full Dashboard", "Pages",   1.00f,1.00f,false},
    {"page_media", "Full Media",     "Pages",   1.00f,1.00f,false},
    {"page_perf",  "Full Performance","Pages",  1.00f,1.00f,false},
    {"page_weather","Full Weather",  "Pages",   1.00f,1.00f,false},
    {"clock",      "Clock",          "Widgets", 0.17f,0.64f,false},
    {"calendar",   "Calendar",       "Widgets", 0.34f,0.64f,false},
    {"weather",    "Weather",        "Widgets", 0.28f,0.34f,false},
    {"profile",    "System info",    "Widgets", 0.36f,0.34f,false},
    {"volume",     "Volume",         "Widgets", 0.12f,0.64f,false},
    {"cpu",        "CPU",            "Performance",0.30f,0.42f,false},
    {"gpu",        "GPU",            "Performance",0.30f,0.42f,false},
    {"memory",     "Memory",         "Performance",0.22f,0.42f,false},
    {"storage",    "Storage",        "Performance",0.22f,0.42f,false},
    {"network",    "Network",        "Performance",0.22f,0.42f,false},
    {"media",      "Now playing",    "Widgets", 0.26f,1.00f,false},
    {"image",      "Image / GIF",    "Media",   0.26f,0.42f,true},
    {"image_fill", "Image (fill)",   "Media",   0.26f,0.42f,true},
    {"text",       "Text",           "Media",   0.30f,0.14f,true},
    // the M3 shape gauges - square-ish by default, because a shape gauge in a wide box is a shape
    // with dead space either side
    {"m3_cpu",     "CPU shape",      "Performance",0.15f,0.30f,false},
    {"m3_gpu",     "GPU shape",      "Performance",0.15f,0.30f,false},
    {"m3_mem",     "Memory shape",   "Performance",0.15f,0.30f,false},
    {"m3_disk",    "Storage shape",  "Performance",0.15f,0.30f,false},
    // the arc gauges the built-in Dashboard page draws inline - the ones in the reference rice.
    // Until now they existed only inside DrawDashboard, so breaking that page apart could not
    // give them back and you got shape gauges in their place instead.
    {"ring_cpu",   "CPU ring",       "Performance",0.11f,0.32f,false},
    {"ring_mem",   "Memory ring",    "Performance",0.11f,0.32f,false},
    {"ring_disk",  "Storage ring",   "Performance",0.11f,0.32f,false},
    {"custom",     "Custom widget",  "Custom",  0.30f,0.40f,true},
    {"user",       "Profile",        "Widgets", 0.36f,0.34f,false},
    {"v2_home",    "Dashboard page", "Caelestia",1.00f,1.00f,false},
    {"v2_mediapage","Media page",    "Caelestia",1.00f,1.00f,false},
    {"v2_perfpage","Performance page","Caelestia",1.00f,1.00f,false},
    {"v2_weather", "Weather",        "Caelestia",0.33f,0.30f,false},
    {"v2_user",    "User card",      "Caelestia",0.42f,0.30f,false},
    {"v2_clock",   "Vertical clock", "Caelestia",0.12f,0.66f,false},
    {"v2_calendar","Calendar",       "Caelestia",0.36f,0.66f,false},
    {"v2_rings",   "Resource rings", "Caelestia",0.14f,0.66f,false},
    {"v2_media",   "Media card",     "Caelestia",0.21f,1.00f,false},
    {"v2_player",  "Player",         "Caelestia",0.66f,1.00f,false},
    {"v2_lyrics",  "Lyrics",         "Caelestia",0.32f,1.00f,false},
    {"v2_cpu",     "CPU card",       "Caelestia",0.49f,0.40f,false},
    {"v2_gpu",     "GPU card",       "Caelestia",0.49f,0.40f,false},
    {"v2_storage", "Storage card",   "Caelestia",0.38f,0.58f,false},
    {"v2_network", "Network card",   "Caelestia",0.38f,0.58f,false},
    {"v2_memory",  "Memory card",    "Caelestia",0.20f,0.58f,false},
    {"terminal",   "Terminal",       "Caelestia",1.00f,1.00f,false},
    {"mixer",      "Audio mixer",    "Widgets", 0.60f,1.00f,false},
};
// Which M3 silhouette the shape gauges use. The literal is unavoidable - SaveConfig is 1800 lines
// above the M3_* enum - so a static_assert next to the enum keeps the two honest.
static int g_m3Shape = 8;   // M3_PENTAGON

static int WKindFromId(const std::string& id){ for(int i=0;i<WK_COUNT;i++) if(id==WREG[i].id) return i; return -1; }

// style = per-placement look, "key=value; ..." : bg=card|none|#hex, image=path(.gif ok), shape=cookie12,
//         color=#hex|primary..., radius=N, opacity=0..1, padding=N, border=#hex
struct Widget { int kind; float x,y,w,h; std::string arg; std::string style; int uid=0; };
// fwd: the desktop layer (1700 lines above the dispatcher) mirrors a tab's widgets onto the wallpaper
static void DrawWidget(ImDrawList* dl,ImGuiIO& io,Widget& w,ImVec2 o,ImVec2 s);
struct DashTab { std::string name; int icon; bool builtin; std::vector<Widget> widgets; std::string iconName; };
static const char* TAB_ICON_DEFAULT[4] = { "dashboard","queue_music","speed","cloud" };   // Caelestia's four
static std::string TabIconName(const DashTab& t){
    if(!t.iconName.empty()) return t.iconName;
    // the stock tabs by name first: older configs saved the Dashboard tab with the Weather tab's icon index
    if(_stricmp(t.name.c_str(),"Dashboard")==0) return "dashboard";
    if(_stricmp(t.name.c_str(),"Media")==0) return "queue_music";
    if(_stricmp(t.name.c_str(),"Performance")==0) return "speed";
    if(_stricmp(t.name.c_str(),"Weather")==0) return "cloud";
    return TAB_ICON_DEFAULT[std::clamp(t.icon,0,3)]; }
// ---- the tab bar is the user's ----
static const char* TABSTYLE_NAMES[] = { "caelestia","pills","segmented","minimal" };
static const char* TABIND_NAMES[]   = { "underline","pill","dot","none" };
static const char* LYRLAYOUT_NAMES[]= { "beside","tab" };
static int  g_tabStyle=0, g_tabIndicator=0;
static bool g_tabIcons=true, g_tabLabels=true, g_tabSeparator=true, g_tabFill=true, g_tabWheel=true;
static int  g_lyricsLayout=1;       // media.lyrics_layout: beside the player, or its own section of the Media tab
static std::string g_cwPython;      // widgets.python
static std::vector<DashTab> g_tabs;
static int g_tab = 2;   // active tab index into g_tabs
static bool g_editTab=false;          // drawer edit mode
static int  g_renameTab=-1;           // settings: which tab name is being typed
static int  g_editSel=-1;             // selected widget within the active tab
static bool g_wPaletteOpen=false;     // the "add widget" palette

static void DefaultTabs(){
    g_tabs.clear();
    auto mk=[&](const char* n,int icon,int page){ DashTab t; t.name=n; t.icon=icon; t.builtin=true;
        t.widgets.push_back({page,0,0,1,1,""}); g_tabs.push_back(t); };
    mk("Dashboard",0,WK_PAGE_DASH);
    mk("Media",1,WK_PAGE_MEDIA);
    mk("Performance",2,WK_PAGE_PERF);
    mk("Weather",3,WK_PAGE_WEATHER);
}

static bool g_freshConfig=false;     // no config.json existed: first run on this machine
// Lua plugin settings live up here because SaveConfig/LoadConfig sit far above the plugin engine
static bool  g_pluginsOn=true;
static std::vector<std::string> g_plDisabled;   // plugin filenames the user switched off

// ---- global shortcuts, rebindable ---------------------------------------------------------
// The defaults are US-layout keys (Ctrl+Alt+[ / ]), which simply do not exist on other layouts,
// and Alt+Space is a popular hotkey that other launchers grab first. So every one of them is now
// data: user-chosen, saved in the config, and reported on Settings > Shortcuts when Windows
// refuses to register it because something else owns it.
enum { HK_QUIT=0, HK_LAUNCHER, HK_WALLPREV, HK_WALLNEXT, HK_SNIP, HK_CLIP, HK_WALLPICK, HK_OVERVIEW, HK_COUNT };
struct Hotkey { const char* id; const char* label; UINT mods; UINT vk; bool ok; };
static Hotkey g_hk[HK_COUNT] = {
    { "quit",         "Quit the shell",     MOD_CONTROL|MOD_ALT, 'Q',       false },
    { "launcher",     "Open the launcher",  MOD_ALT|MOD_NOREPEAT, VK_SPACE, false },
    { "wallpaperPrev","Previous wallpaper", MOD_CONTROL|MOD_ALT, VK_OEM_4,  false },
    { "wallpaperNext","Next wallpaper",     MOD_CONTROL|MOD_ALT, VK_OEM_6,  false },
    { "snip",         "Snip a screenshot",  MOD_CONTROL|MOD_SHIFT, 'S',     false },
    // NOT Win+V: Windows owns that one and will not give it up, so the shell's own history would
    // silently lose the race against the built-in panel.
    { "clipboard",    "Clipboard history",  MOD_CONTROL|MOD_ALT,   'V',     false },
    { "wallpaperPick","Wallpaper picker",   MOD_CONTROL|MOD_ALT,   'W',     false },
    // NOT Super+Tab: Windows reserves that for Task View and RegisterHotKey is refused, the same way
    // it refuses Win+V. The Super+Tab gesture is taken in the keyboard hook instead (overview.super_tab),
    // which is how the switcher takes Alt+Tab; this is the ordinary hotkey people can rebind.
    { "overview",     "Workspace overview",  MOD_CONTROL|MOD_ALT,   'E',     false },
};
// name a virtual key the way the user's keyboard actually reads it
static std::string VkName(UINT vk){
    if(!vk) return "None";
    if((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9')) return std::string(1,(char)vk);
    if(vk>=VK_F1&&vk<=VK_F24){ char b[8]; snprintf(b,8,"F%u",vk-VK_F1+1); return b; }
    switch(vk){
        case VK_SPACE:return"Space"; case VK_RETURN:return"Enter"; case VK_TAB:return"Tab";
        case VK_ESCAPE:return"Esc";  case VK_BACK:return"Backspace"; case VK_DELETE:return"Del";
        case VK_INSERT:return"Ins";  case VK_HOME:return"Home"; case VK_END:return"End";
        case VK_PRIOR:return"PgUp";  case VK_NEXT:return"PgDn";
        case VK_LEFT:return"Left";   case VK_RIGHT:return"Right"; case VK_UP:return"Up"; case VK_DOWN:return"Down";
        case VK_PAUSE:return"Pause"; case VK_SNAPSHOT:return"PrtSc"; case VK_APPS:return"Menu";
        default: break;
    }
    // OEM/punctuation keys differ by layout, so ask the layout what this key prints
    UINT ch=MapVirtualKeyW(vk,MAPVK_VK_TO_CHAR)&0x7FFF;
    if(ch>32 && ch<127) return std::string(1,(char)ch);
    char b[16]; snprintf(b,16,"Key %u",vk); return b;
}
static std::string HotkeyName(UINT mods,UINT vk){
    if(!vk) return "Not set";
    std::string s;
    if(mods&MOD_CONTROL) s+="Ctrl + ";
    if(mods&MOD_ALT)     s+="Alt + ";
    if(mods&MOD_SHIFT)   s+="Shift + ";
    if(mods&MOD_WIN)     s+="Super + ";
    return s+VkName(vk);
}
// "Ctrl+Alt+Q" / "Alt+Space" / "Ctrl+Shift+Key 220" -> mods + vk
static void ParseHotkey(const std::string& in,UINT& mods,UINT& vk){
    mods=0; vk=0; std::string tok; std::string s=in;
    auto apply=[&](std::string t){
        // trim
        while(!t.empty()&&(t.front()==' ')) t.erase(t.begin());
        while(!t.empty()&&(t.back()==' ')) t.pop_back();
        if(t.empty()) return;
        std::string l; for(char c:t) l+=(char)tolower((unsigned char)c);
        if(l=="ctrl"||l=="control") mods|=MOD_CONTROL;
        else if(l=="alt") mods|=MOD_ALT;
        else if(l=="shift") mods|=MOD_SHIFT;
        else if(l=="win"||l=="super"||l=="meta") mods|=MOD_WIN;
        else if(l=="space") vk=VK_SPACE;
        else if(l=="enter"||l=="return") vk=VK_RETURN;
        else if(l=="tab") vk=VK_TAB;      else if(l=="esc"||l=="escape") vk=VK_ESCAPE;
        else if(l=="backspace") vk=VK_BACK; else if(l=="del"||l=="delete") vk=VK_DELETE;
        else if(l=="ins"||l=="insert") vk=VK_INSERT;
        else if(l=="home") vk=VK_HOME;    else if(l=="end") vk=VK_END;
        else if(l=="pgup") vk=VK_PRIOR;   else if(l=="pgdn") vk=VK_NEXT;
        else if(l=="left") vk=VK_LEFT;    else if(l=="right") vk=VK_RIGHT;
        else if(l=="up") vk=VK_UP;        else if(l=="down") vk=VK_DOWN;
        else if(l=="prtsc") vk=VK_SNAPSHOT; else if(l=="pause") vk=VK_PAUSE;
        else if(l.size()>4 && l.compare(0,4,"key ")==0) vk=(UINT)atoi(l.c_str()+4);
        else if(l[0]=='f' && l.size()<=3 && isdigit((unsigned char)l[1])) vk=VK_F1+(UINT)atoi(l.c_str()+1)-1;
        else if(t.size()==1){
            char c=(char)toupper((unsigned char)t[0]);
            if((c>='A'&&c<='Z')||(c>='0'&&c<='9')) vk=(UINT)c;
            else { SHORT sc=VkKeyScanW((wchar_t)t[0]); if(sc!=-1) vk=(UINT)(sc&0xFF); }
        }
    };
    size_t p=0;
    while(true){ size_t q=s.find('+',p);
        if(q==std::string::npos){ apply(s.substr(p)); break; }
        apply(s.substr(p,q-p)); p=q+1; }
}
