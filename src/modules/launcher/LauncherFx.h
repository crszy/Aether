// src/modules/launcher/LauncherFx.h  —  Aether shell
// Everything that makes the launcher customisable beyond its original list. All of it is opt-in: with the defaults
// (layout "list", shape "rounded", no glow, flat, no typing effects, selection "fill", results "cascade") the
// launcher looks exactly as it did.
//
//   layout      list | grid | horizontal (a dock-like strip) | vertical (a column on a screen edge) | radial (a ring)
//   shape       rounded | pill | square | chamfer | hexagon | slant        + glow (accent / rainbow / custom colour)
//   depth       flat | tilt (faces the pointer, tumbles in) | extrude (a solid slab) | both
//   background  none | nebula | particles | aurora, drawn inside the panel
//   typing      per-letter animation (pop, wave, bounce, glitch, rainbow, typewriter), caret style, a smear trail
//               behind the caret (neovide-style) and sparks off each new letter
//   results     cascade | fade | scale | slide | flip | none; selection fill | glide | outline | bar | glow;
//               icon hover bounce | grow | wiggle; a ripple / zoom when something is launched
//   tray        a favourites shelf in the launcher: click opens, drag to reorder, right-click unpins, + pins any exe
//   keywords    "ff" + Enter opens Firefox; each keyword can also carry a global hotkey that opens it from anywhere
//
// Icons load on a worker thread (SHGetFileInfo resolves shortcuts, which could stall the render loop on a
// shortcut pointing at a sleeping drive).
#pragma once
#ifndef IM_PI
#define IM_PI 3.14159265358979323846f
#endif

static void DrawNebula(ImDrawList* dl,ImVec2 a,ImVec2 b,float opacity,float rnd=0);   // fwd (SettingsFx.h: real nebula footage)
static void LLaunch();                                                     // fwd

// ============================================================================================ async icons
static std::mutex g_lfxIcMtx;
static std::deque<std::pair<std::wstring,bool>> g_lfxIcQ;
static std::vector<std::tuple<std::wstring,bool,HICON>> g_lfxIcDone;
static std::unordered_map<std::wstring,ID3D11ShaderResourceView*> g_lfxIcons[2];
static std::unordered_set<std::wstring> g_lfxIcAsked[2];
static std::atomic<bool> g_lfxIcRun{false};
static void LfxIconWorker(bool smallOnly){
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    for(;;){
        std::pair<std::wstring,bool> job; bool have=false;
        { std::lock_guard<std::mutex> lk(g_lfxIcMtx);
          for(auto it=g_lfxIcQ.begin(); it!=g_lfxIcQ.end(); ++it) if(!smallOnly || !it->second){ job=*it; g_lfxIcQ.erase(it); have=true; break; } }
        if(!have){ Sleep(40); continue; }
        HICON ico=nullptr;
        if(job.second){                                   // big: the shell's 256px jumbo list
            SHFILEINFOW sfi={};
            if(SHGetFileInfoW(job.first.c_str(),0,&sfi,sizeof(sfi),SHGFI_SYSICONINDEX)){
                IImageList* il=nullptr;
                if(SUCCEEDED(SHGetImageList(SHIL_JUMBO,IID_IImageList,(void**)&il)) && il){ il->GetIcon(sfi.iIcon,ILD_TRANSPARENT,&ico); il->Release(); } }
        }
        if(!ico){ SHFILEINFOW sfi={}; if(SHGetFileInfoW(job.first.c_str(),0,&sfi,sizeof(sfi),SHGFI_ICON|SHGFI_LARGEICON)) ico=sfi.hIcon; }
        std::lock_guard<std::mutex> lk(g_lfxIcMtx); g_lfxIcDone.emplace_back(job.first,job.second,ico);
    }
    if(SUCCEEDED(hr)) CoUninitialize();
}
static ID3D11ShaderResourceView* LfxIcon(const std::wstring& path,bool big){
    if(path.empty()) return nullptr;
    auto& cache=g_lfxIcons[big?1:0];
    auto it=cache.find(path); if(it!=cache.end()) return it->second;
    if(g_lfxIcAsked[big?1:0].insert(path).second){
        std::lock_guard<std::mutex> lk(g_lfxIcMtx);
        if(big) g_lfxIcQ.emplace_back(path,big); else g_lfxIcQ.emplace_front(path,big);   // small icons are quick: serve them first
        if(!g_lfxIcRun.exchange(true)){ std::thread(LfxIconWorker,true).detach(); std::thread(LfxIconWorker,false).detach(); } }   // one thread only ever does the fast small icons
    if(big){ auto s2=g_lfxIcons[0].find(path); if(s2!=g_lfxIcons[0].end()) return s2->second;   // the jumbo list is slow cold: show the small one meanwhile
             LfxIcon(path,false); }
    return nullptr;
}
static void LfxIconPump(){                                 // render thread: HICON -> texture (D3D is not thread-safe)
    std::vector<std::tuple<std::wstring,bool,HICON>> done; { std::lock_guard<std::mutex> lk(g_lfxIcMtx); done.swap(g_lfxIcDone); }
    for(auto& d:done){
        HICON h=std::get<2>(d); bool big=std::get<1>(d);
        ID3D11ShaderResourceView* t = h? (big? IconTexTrim(h,128) : IconTex(h,40)) : nullptr;
        if(h) DestroyIcon(h);
        g_lfxIcons[big?1:0][std::get<0>(d)]=t; }
}

// ============================================================================================ small helpers
static float LfxT(){ return (float)ImGui::GetTime(); }
static ImU32 LfxHue(float h,int a,float s=0.55f){ h-=floorf(h); float r,g,b; ImGui::ColorConvertHSVtoRGB(h,s,1.0f,r,g,b); return IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),std::clamp(a,0,255)); }
static ImU32 LfxHex(const std::string& s,ImU32 fb){
    std::string h=s; if(!h.empty()&&h[0]=='#') h=h.substr(1); if(h.size()!=6) return fb;
    unsigned v=0; if(sscanf_s(h.c_str(),"%x",&v)!=1) return fb;
    return IM_COL32((v>>16)&255,(v>>8)&255,v&255,255); }
static ImU32 LfxGlowCol(int a,float phase=0){
    if(g_launGlowCol==1) return LfxHue(LfxT()*0.12f+phase,a,0.6f);
    if(g_launGlowCol==2) return WithA(LfxHex(g_launGlowHex,COL_GOLD),a);
    return WithA(COL_GOLD,a); }
static ImU32 LfxMulAlphaCol(ImU32 c,float k){ int a=(int)(((c>>IM_COL32_A_SHIFT)&0xFF)*std::clamp(k,0.0f,1.0f)); return (c&~IM_COL32_A_MASK)|((ImU32)a<<IM_COL32_A_SHIFT); }
static void LfxFadeVerts(ImDrawList* dl,int v0,float k){ if(k>=0.999f) return; for(int i=v0;i<dl->VtxBuffer.Size;i++) dl->VtxBuffer[i].col=LfxMulAlphaCol(dl->VtxBuffer[i].col,k); }
static void LfxMoveVerts(ImDrawList* dl,int v0,float dx,float dy){ if(fabsf(dx)<0.01f&&fabsf(dy)<0.01f) return; for(int i=v0;i<dl->VtxBuffer.Size;i++){ dl->VtxBuffer[i].pos.x+=dx; dl->VtxBuffer[i].pos.y+=dy; } }
static void LfxRotVerts(ImDrawList* dl,int v0,ImVec2 c,float ang){ if(fabsf(ang)<0.0005f) return; float ca=cosf(ang), sa=sinf(ang);
    for(int i=v0;i<dl->VtxBuffer.Size;i++){ ImVec2 p=dl->VtxBuffer[i].pos; float x=p.x-c.x, y=p.y-c.y; dl->VtxBuffer[i].pos=ImVec2(c.x+x*ca-y*sa,c.y+x*sa+y*ca); } }
static float LfxBounce(float t){ const float n=7.5625f, d=2.75f;
    if(t<1/d) return n*t*t; if(t<2/d){ t-=1.5f/d; return n*t*t+0.75f; } if(t<2.5f/d){ t-=2.25f/d; return n*t*t+0.9375f; } t-=2.625f/d; return n*t*t+0.984375f; }
static float LfxHash(int a,int b){ uint32_t h=(uint32_t)a*374761393u+(uint32_t)b*668265263u; h=(h^(h>>13))*1274126177u; return ((h^(h>>16))&0xFFFF)/65535.0f; }
static bool LfxInList(const std::vector<std::string>& v,const std::string& p){ for(auto& x:v) if(_stricmp(x.c_str(),p.c_str())==0) return true; return false; }

// ============================================================================================ shapes + panel
static float LfxShapeInset(int shape,float w,float h){
    if(shape==4) return std::min(h*0.30f,w*0.18f);
    if(shape==5) return std::min(h*0.26f,w*0.14f);
    return 0.0f; }
static void LfxShape(ImVec2 a,ImVec2 b,float rnd,int shape,std::vector<ImVec2>& P){
    P.clear(); float w=b.x-a.x, h=b.y-a.y; if(w<2||h<2) return;
    if(shape==1) rnd= std::min(w,h)<=180.0f? std::min(w,h)*0.5f : std::max(90.0f,std::min(w,h)*0.22f);   // a true pill on a strip, super-round on a big card
    if(shape==2) rnd=0;
    switch(shape){
    case 3: { float c=std::clamp(rnd*1.1f+8.0f,6.0f,std::min(w,h)*0.3f);
        P={V(a.x+c,a.y),V(b.x-c,a.y),V(b.x,a.y+c),V(b.x,b.y-c),V(b.x-c,b.y),V(a.x+c,b.y),V(a.x,b.y-c),V(a.x,a.y+c)}; return; }
    case 4: { float k=LfxShapeInset(4,w,h), cy=(a.y+b.y)*0.5f;
        P={V(a.x+k,a.y),V(b.x-k,a.y),V(b.x,cy),V(b.x-k,b.y),V(a.x+k,b.y),V(a.x,cy)}; return; }
    case 5: { float k=std::min(h*0.26f,w*0.14f);
        P={V(a.x+k,a.y),V(b.x,a.y),V(b.x-k,b.y),V(a.x,b.y)}; return; }
    }
    rnd=std::clamp(rnd,0.0f,std::min(w,h)*0.5f);
    if(rnd<0.5f){ P={a,V(b.x,a.y),b,V(a.x,b.y)}; return; }
    int seg=std::clamp((int)(rnd/2.5f),4,48);
    auto arc=[&](float cx,float cy,float a0){ for(int i=0;i<=seg;i++){ float t=a0+(IM_PI*0.5f)*i/seg; P.push_back(V(cx+cosf(t)*rnd,cy+sinf(t)*rnd)); } };
    arc(a.x+rnd,a.y+rnd,IM_PI); arc(b.x-rnd,a.y+rnd,IM_PI*1.5f); arc(b.x-rnd,b.y-rnd,0); arc(a.x+rnd,b.y-rnd,IM_PI*0.5f);
}
static void LfxCircle(ImVec2 c,float r,std::vector<ImVec2>& P){ P.clear(); int n=std::clamp((int)(r/3.0f),24,120); for(int i=0;i<n;i++){ float t=IM_PI*2*i/n; P.push_back(V(c.x+cosf(t)*r,c.y+sinf(t)*r)); } }
// the frosted backdrop, mapped onto any convex outline (ImGui can only round rectangles)
static void LfxImagePoly(ImDrawList* dl,ImTextureID tex,const std::vector<ImVec2>& P,float W,float H,ImU32 col){
    int n=(int)P.size(); if(n<3||!tex||W<1||H<1) return;
    dl->PushTextureID(tex);
    dl->PrimReserve((n-2)*3,n);
    ImDrawIdx base=(ImDrawIdx)dl->_VtxCurrentIdx;
    for(auto& p:P) dl->PrimWriteVtx(p,ImVec2(std::clamp(p.x/W,0.0f,1.0f),std::clamp(p.y/H,0.0f,1.0f)),col);
    for(int i=1;i<n-1;i++){ dl->PrimWriteIdx(base); dl->PrimWriteIdx((ImDrawIdx)(base+i)); dl->PrimWriteIdx((ImDrawIdx)(base+i+1)); }
    dl->PopTextureID();
}
static void LfxBgFx(ImDrawList* dl,ImVec2 a,ImVec2 b,float e){
    float w=b.x-a.x, h=b.y-a.y, t=LfxT();
    if(g_launBgFx==1){ DrawNebula(dl,a,b,0.55f*e); return; }
    if(g_launBgFx==2){
        for(int i=0;i<54;i++){
            float x0=LfxHash(i,1), y0=LfxHash(i,2), sp=0.3f+LfxHash(i,3)*0.9f, sz=0.8f+LfxHash(i,4)*2.2f;
            float x=a.x+fmodf(x0*w+t*sp*16.0f,w), y=a.y+fmodf(y0*h-t*sp*10.0f+h*8.0f,h);
            float tw=0.45f+0.55f*sinf(t*(1.5f+sp)+i);
            ImU32 c = g_launGlowCol==1? LfxHue(t*0.05f+i*0.02f,(int)(150*tw*e)) : WithA(Mix(COL_INK,COL_GOLD,LfxHash(i,5)),(int)(130*tw*e));
            dl->AddCircleFilled(V(x,y),sz,c,8); }
        return; }
    if(g_launBgFx==3){
        for(int k=0;k<4;k++){
            float cx=a.x+w*(0.5f+0.42f*sinf(t*(0.13f+k*0.05f)+k*1.7f)), cy=a.y+h*(0.5f+0.35f*cosf(t*(0.11f+k*0.04f)+k));
            float r=std::max(w,h)*(0.32f+0.08f*k);
            ImU32 base = g_launGlowCol==1? LfxHue(t*0.03f+k*0.22f,255,0.7f) : Mix(COL_GOLD,IM_COL32(120,90,230,255),k*0.3f);
            for(int s=7;s>=1;s--) dl->AddCircleFilled(V(cx,cy),r*s/7.0f,WithA(base,(int)(7*e)),48); }
    }
}
static bool LfxCustomPanel(){ return g_launShape!=0 || g_launGlow || g_launDepth>=2 || g_launBgFx!=0; }
static bool LfxWantsFloat(){ return LfxCustomPanel() || g_launDepth!=0 || g_launLayout!=0; }
// the whole panel: extrusion, glow halo, shadow, frost, fill, background effect, border
static void LfxPanelPoly(ImDrawList* dl,const std::vector<ImVec2>& P,ImVec2 a,ImVec2 b,float e,float W,float H,float fxInset=-1){
    if(P.size()<3) return;
    int al=(int)(255*std::clamp(e,0.0f,1.0f)); float t=LfxT();
    if(g_launDepth>=2){                                               // a solid slab under the panel
        const int N=12;
        for(int i=N;i>=1;i--){ std::vector<ImVec2> Q=P; for(auto& q:Q){ q.x+=i*1.0f; q.y+=i*1.7f; }
            ImU32 c=Mix(PanelCol(255),IM_COL32(0,0,0,255),0.45f+0.35f*i/N);
            if(g_launGlow) c=Mix(c,LfxGlowCol(255,i*0.01f),0.22f);
            dl->AddConvexPolyFilled(Q.data(),(int)Q.size(),WithA(c,al)); } }
    if(g_launGlow){
        float pulse=g_launGlowPulse? 0.72f+0.28f*sinf(t*2.3f) : 1.0f, str=std::clamp(g_launGlowStr,0.0f,1.5f)*pulse;
        for(int i=9;i>=1;i--){
            float th=i*3.4f; int ga=(int)(al*str*0.11f*(1.0f-i/10.0f)+al*str*0.02f);
            if(g_launGlowCol==1){ int n=(int)P.size();
                for(int k=0;k<n;k++){ const ImVec2& p0=P[k]; const ImVec2& p1=P[(k+1)%n]; ImU32 c=LfxHue(t*0.12f+(float)k/n,ga,0.65f);
                    dl->AddLine(p0,p1,c,th); dl->AddCircleFilled(p0,th*0.5f,c,10); } }
            else dl->AddPolyline(P.data(),(int)P.size(),LfxGlowCol(ga),ImDrawFlags_Closed,th); }
    } else if(g_panelShadow){
        for(int i=6;i>0;i--){ std::vector<ImVec2> Q=P; for(auto& q:Q) q.y+=3;
            dl->AddPolyline(Q.data(),(int)Q.size(),IM_COL32(0,0,0,(int)(11*e)),ImDrawFlags_Closed,i*2.4f); } }
    if(g_launBlur && g_launBg) LfxImagePoly(dl,(ImTextureID)g_launBg,P,W,H,IM_COL32(255,255,255,al));
    dl->AddConvexPolyFilled(P.data(),(int)P.size(),WithA(PanelCol(255),(int)(std::clamp(g_launOpacity,0.1f,1.0f)*255*std::clamp(e,0.0f,1.0f))));
    if(g_launBgFx){
        // the effect is clipped to the largest rectangle inside the outline (a clip rect cannot follow a curve or a cut corner)
        float w=b.x-a.x, h=b.y-a.y, ix=2, iy=2;
        if(fxInset>=0){ ix=iy=fxInset; }
        else switch(g_launShape){
            case 0: ix=iy=std::clamp(g_launRound,0.0f,std::min(w,h)*0.5f)*0.30f; break;
            case 1: ix=iy=(std::min(w,h)<=180.0f? std::min(w,h)*0.5f : std::max(90.0f,std::min(w,h)*0.22f))*0.30f; break;
            case 3: ix=iy=std::clamp(g_launRound*1.1f+8.0f,6.0f,std::min(w,h)*0.3f)*0.5f; break;
            case 4: case 5: ix=LfxShapeInset(g_launShape,w,h); break; }
        ImVec2 fa=V(a.x+ix,a.y+iy), fb=V(b.x-ix,b.y-iy);
        dl->PushClipRect(fa,fb,true); LfxBgFx(dl,a,b,e);
        // feather the clip edges into the panel so the effect has no hard rectangular border
        const float F=std::min(34.0f,std::min(fb.x-fa.x,fb.y-fa.y)*0.25f);
        ImU32 c1=WithA(PanelCol(255),(int)(std::max(0.75f,std::clamp(g_launOpacity,0.1f,1.0f))*235*std::clamp(e,0.0f,1.0f))), c0=WithA(PanelCol(255),0);
        dl->AddRectFilledMultiColor(fa,V(fb.x,fa.y+F),c1,c1,c0,c0);
        dl->AddRectFilledMultiColor(V(fa.x,fb.y-F),fb,c0,c0,c1,c1);
        dl->AddRectFilledMultiColor(fa,V(fa.x+F,fb.y),c1,c0,c0,c1);
        dl->AddRectFilledMultiColor(V(fb.x-F,fa.y),fb,c0,c1,c1,c0);
        dl->PopClipRect(); }
    if(g_launGlow && g_launGlowCol==1){ int n=(int)P.size();
        for(int k=0;k<n;k++) dl->AddLine(P[k],P[(k+1)%n],LfxHue(t*0.12f+(float)k/n,(int)(230*e),0.5f),1.8f); }
    else dl->AddPolyline(P.data(),(int)P.size(),g_launGlow? LfxGlowCol((int)(220*e)) : WithA(COL_INK2,(int)(40*e)),ImDrawFlags_Closed,g_launGlow? 1.8f : 1.0f);
}
static void LfxPanel(ImDrawList* dl,ImVec2 a,ImVec2 b,float rnd,float e,float W,float H,int shapeOverride=-1){
    std::vector<ImVec2> P; LfxShape(a,b,rnd,shapeOverride>=0? shapeOverride : g_launShape,P);
    LfxPanelPoly(dl,P,a,b,e,W,H);
}
// 3D: the panel faces the pointer and tumbles in as it opens
static void LfxTilt(ImDrawList* dl,int v0,ImVec2 c,float pw,float ph,float e){
    if(!(g_launDepth==1||g_launDepth==3)) return;
    static float ax=0, ay=0;
    ImVec2 m=LaunCursorLogical();
    float tx=std::clamp((m.y-c.y)/(ph*0.5f+120.0f),-1.0f,1.0f), ty=std::clamp((m.x-c.x)/(pw*0.5f+120.0f),-1.0f,1.0f);
    float amt=std::clamp(g_launTilt,0.0f,1.0f)*0.26f;
    float k=std::min(1.0f,g_frameDt*7.0f);
    ax += (-tx*amt-ax)*k; ay += (ty*amt-ay)*k;
    float rx=ax+(1.0f-std::clamp(e,0.0f,1.0f))*0.85f, ry=ay;
    float cx=cosf(rx), sx=sinf(rx), cy=cosf(ry), sy=sinf(ry), f=1500.0f;
    for(int i=v0;i<dl->VtxBuffer.Size;i++){
        ImVec2 p=dl->VtxBuffer[i].pos; float x=p.x-c.x, y=p.y-c.y;
        float x1=x*cy, z1=-x*sy;
        float y2=y*cx-z1*sx, z2=y*sx+z1*cx;
        float s=f/(f+z2); if(s<0.05f) s=0.05f;
        dl->VtxBuffer[i].pos=ImVec2(c.x+x1*s,c.y+y2*s); }
}

// ============================================================================================ typing
struct LfxSpark { ImVec2 p,v; float life,max,size; ImU32 c; };
struct LfxGhost { char ch; ImVec2 p; float at; };
static std::vector<LfxSpark> g_lfxSparks;
static std::vector<LfxGhost> g_lfxGhosts;
// Draws the query with the chosen per-letter animation, the caret (+ smear) and sparks. Returns the text end x.
static float LfxSearchText(ImDrawList* dl,ImFont* f,float fs,ImVec2 pos,float lineH,const char* text,const char* placeholder,int al){
    static std::string prev; static std::vector<float> birth; static float head=-1, tail=-1; static ULONGLONG lastType=0, openSeen=0;
    float now=LfxT();
    std::string cur=text? text : "";
    if(openSeen!=g_launOpenAt){ openSeen=g_launOpenAt; prev=cur; birth.assign(cur.size(),-10.0f); head=tail=-1; g_lfxSparks.clear(); g_lfxGhosts.clear(); }
    float midY=pos.y+lineH*0.5f;
    if(cur!=prev){
        size_t k=0; while(k<cur.size()&&k<prev.size()&&cur[k]==prev[k]) k++;
        if(prev.size()>k && g_launSearchAnim!=0){ float x=pos.x+TextW(f,fs,prev.substr(0,k).c_str());
            for(size_t i=k;i<prev.size()&&g_lfxGhosts.size()<40;i++){ char s2[2]={prev[i],0}; g_lfxGhosts.push_back({prev[i],V(x,pos.y),now}); x+=TextW(f,fs,s2); } }
        birth.resize(cur.size()); for(size_t i=k;i<cur.size();i++) birth[i]=now;
        if(g_launSparks && cur.size()>k){
            float x=pos.x+TextW(f,fs,cur.c_str());
            for(int i=0;i<10;i++){ float ang=-IM_PI*0.5f+(LfxHash((int)(now*1000),i)-0.5f)*2.6f, sp=90.0f+LfxHash(i,(int)(now*977))*170.0f;
                ImU32 c = g_launGlowCol==1||g_launSearchAnim==5? LfxHue(now*0.3f+i*0.07f,255,0.7f) : Mix(COL_GOLD,IM_COL32(255,255,255,255),LfxHash(i,9)*0.5f);
                g_lfxSparks.push_back({V(x,midY),V(cosf(ang)*sp,sinf(ang)*sp),0.0f,0.35f+LfxHash(i,3)*0.35f,1.2f+LfxHash(i,4)*1.8f,c}); } }
        prev=cur; lastType=GetTickCount64();
    }
    float x=pos.x;
    for(size_t i=0;i<cur.size();i++){
        char s2[2]={cur[i],0}; float cw=TextW(f,fs,s2);
        float age=now-(i<birth.size()? birth[i] : -10.0f), k=std::clamp(age/0.30f,0.0f,1.0f);
        float dx=0, dy=0, sc=1, a=1; ImU32 col=COL_INK;
        int v0=dl->VtxBuffer.Size;
        switch(g_launSearchAnim){
        case 1: sc=0.35f+0.65f*EaseOutBack(k); a=std::min(1.0f,k*3.0f); break;                                 // pop
        case 2: dy=sinf(now*6.0f-(float)i*0.55f)*2.2f+(1.0f-EaseOutCubic(k))*10.0f; a=std::min(1.0f,k*3.0f); break;   // wave
        case 3: dy=-(1.0f-LfxBounce(k))*18.0f; break;                                                           // bounce
        case 4: if(age<0.24f){ float j=(LfxHash((int)i,(int)(now*60))-0.5f)*7.0f; dx=j;                          // glitch
                    TextAt(dl,f,fs,V(x+dx-2.0f,pos.y),IM_COL32(255,60,90,(int)(al*0.7f)),s2);
                    TextAt(dl,f,fs,V(x+dx+2.0f,pos.y),IM_COL32(60,230,255,(int)(al*0.7f)),s2);
                    if(age<0.10f){ s2[0]=(char)(33+(int)(LfxHash((int)i,(int)(now*90))*90)); } } break;
        case 5: col=LfxHue(now*0.25f+(float)i*0.06f,255,0.6f); break;                                           // rainbow
        case 6: a=k; dx=(1.0f-EaseOutCubic(k))*9.0f; break;                                                     // typewriter
        }
        TextAt(dl,f,fs,V(x+dx,pos.y+dy),WithA(col,(int)(al*a)),s2);
        if(sc!=1.0f) ScaleVerts(dl,v0,V(x+cw*0.5f,midY),sc);
        x+=TextW(f,fs,std::string(1,cur[i]).c_str());
    }
    // letters just deleted fall away
    for(auto it=g_lfxGhosts.begin(); it!=g_lfxGhosts.end();){
        float age=now-it->at; if(age>0.35f){ it=g_lfxGhosts.erase(it); continue; }
        float k=age/0.35f; char s2[2]={it->ch,0};
        int v0=dl->VtxBuffer.Size;
        TextAt(dl,f,fs,V(it->p.x,it->p.y+k*k*20.0f),WithA(COL_INK2,(int)(al*(1.0f-k))),s2);
        LfxRotVerts(dl,v0,V(it->p.x+fs*0.25f,midY+k*k*20.0f),k*0.6f);
        ++it; }
    if(cur.empty() && placeholder) TextAt(dl,f,fs,V(pos.x+12,pos.y),WithA(COL_INK2,(int)(al*0.8f)),placeholder);
    // caret
    float target=x; if(head<0){ head=tail=target; }
    float dt=std::min(g_frameDt,0.05f);
    head += (target-head)*std::min(1.0f,dt*30.0f);
    tail += (head-tail)*std::min(1.0f,dt*(g_launSmear? 8.0f : 30.0f));
    bool blinkOn = GetTickCount64()-lastType<600 || fmodf(now,1.0f)<0.55f;
    ImU32 cc = (g_launSearchAnim==5||g_launGlowCol==1&&g_launGlow)? LfxHue(now*0.25f,al,0.6f) : AccA(al);
    float cy0=midY-11, cy1=midY+11, blockW=fs*0.55f;
    if(g_launSmear && fabsf(tail-head)>1.0f){
        float x0=std::min(tail,head), x1=std::max(tail,head); bool rightward=head>tail;
        float th=(cy1-cy0)*0.5f, tt=th*0.25f;
        ImVec2 q[4]={ V(tail,midY-tt), V(head,midY-th), V(head,midY+th), V(tail,midY+tt) };
        if(!rightward){ q[0]=V(tail,midY-tt); q[3]=V(tail,midY+tt); }
        dl->AddQuadFilled(q[0],q[1],q[2],q[3],LfxMulAlphaCol(cc,0.45f));
        ImU32 c0=LfxMulAlphaCol(cc,0.0f), c1=LfxMulAlphaCol(cc,0.55f);
        if(rightward) dl->AddRectFilledMultiColor(V(x0,midY-tt*0.8f),V(x1,midY+tt*0.8f),c0,c1,c1,c0);
        else          dl->AddRectFilledMultiColor(V(x0,midY-tt*0.8f),V(x1,midY+tt*0.8f),c1,c0,c0,c1);
    }
    if(blinkOn || (g_launSmear && fabsf(tail-head)>1.0f)){
        if(g_launCaret==1) dl->AddRectFilled(V(head,cy0),V(head+blockW,cy1),LfxMulAlphaCol(cc,0.55f),3);
        else if(g_launCaret==2) dl->AddRectFilled(V(head,cy1-1),V(head+blockW,cy1+2),cc,1.5f);
        else dl->AddRectFilled(V(head,cy0),V(head+1.8f,cy1),cc);
    }
    for(auto it=g_lfxSparks.begin(); it!=g_lfxSparks.end();){
        it->life+=dt; if(it->life>=it->max){ it=g_lfxSparks.erase(it); continue; }
        it->v.y+=320.0f*dt; it->v.x*=0.96f; it->p.x+=it->v.x*dt; it->p.y+=it->v.y*dt;
        float k=1.0f-it->life/it->max;
        dl->AddCircleFilled(it->p,it->size*(0.6f+0.4f*k),LfxMulAlphaCol(it->c,k*al/255.0f),8);
        ++it; }
    return x;
}

// ============================================================================================ keywords + hotkeys
struct LfxAlias { std::string key, hotkey, path; };
static std::vector<LfxAlias> LfxAliases(){
    std::vector<LfxAlias> out;
    for(auto& s:g_launAliases){ size_t a=s.find('|'); if(a==std::string::npos) continue; size_t b=s.find('|',a+1); if(b==std::string::npos) continue;
        out.push_back({s.substr(0,a),s.substr(a+1,b-a-1),s.substr(b+1)}); }
    return out; }
static void LfxRegisterAliasHotkeys();
static void LfxSaveAliases(const std::vector<LfxAlias>& v){
    g_launAliases.clear(); for(auto& x:v) g_launAliases.push_back(x.key+"|"+x.hotkey+"|"+x.path);
    SaveConfig(); LfxRegisterAliasHotkeys(); }
static std::vector<bool> g_lfxHkOk;
static bool g_lfxHkPaused=false;
static void LfxRegisterAliasHotkeys(){
    for(int i=0;i<64;i++) UnregisterHotKey(g_hwnd,1000+i);
    auto v=LfxAliases(); g_lfxHkOk.assign(v.size(),false);
    if(g_lfxHkPaused) return;
    for(size_t i=0;i<v.size()&&i<64;i++){ if(v[i].hotkey.empty()) continue; UINT m=0,k=0; ParseHotkey(v[i].hotkey,m,k);
        if(k) g_lfxHkOk[i]=RegisterHotKey(g_hwnd,1000+(int)i,m|MOD_NOREPEAT,k)!=0; }
}
static std::wstring LfxAppNameW(const std::wstring& path){
    for(auto& a:g_lapps) if(_wcsicmp(a.path.c_str(),path.c_str())==0) return a.name;
    return SlLeaf(path); }
static void LfxAliasHotkey(int i){ auto v=LfxAliases(); if(i>=0 && i<(int)v.size()) AetherOpen(U82W(v[i].path)); }
static int LfxAliasMatch(const char* q){
    std::string s=q? q : ""; while(!s.empty()&&s.back()==' ') s.pop_back(); size_t st=s.find_first_not_of(' '); if(st==std::string::npos) return -1; s=s.substr(st);
    auto v=LfxAliases(); for(size_t i=0;i<v.size();i++) if(!v[i].key.empty() && _stricmp(v[i].key.c_str(),s.c_str())==0) return (int)i;
    return -1; }
static std::string LfxAliasFor(const std::string& path){ for(auto& a:LfxAliases()) if(_stricmp(a.path.c_str(),path.c_str())==0) return a.key; return ""; }

// ============================================================================================ launch effect
static float g_lfxFxAt=-10; static ImVec2 g_lfxFxC; static ID3D11ShaderResourceView* g_lfxFxIcon=nullptr;
static void LfxLaunchFx(ImVec2 c,ID3D11ShaderResourceView* icon){ g_lfxFxAt=LfxT(); g_lfxFxC=c; g_lfxFxIcon=icon; }
static void LfxDrawLaunchFx(ImDrawList* dl){
    if(g_launLaunchFx==0) return; float age=LfxT()-g_lfxFxAt; if(age<0||age>0.45f) return; float k=age/0.45f, e=EaseOutCubic(k);
    if(g_launLaunchFx==1){ for(int i=0;i<3;i++){ float kk=std::clamp(k*1.3f-i*0.15f,0.0f,1.0f); if(kk<=0) continue;
            dl->AddCircle(g_lfxFxC,20+EaseOutCubic(kk)*260,LfxGlowCol((int)(200*(1-kk))),64,3.0f*(1-kk)+1); } }
    else if(g_launLaunchFx==2 && g_lfxFxIcon){ float s=40+e*140; dl->AddImage((ImTextureID)g_lfxFxIcon,V(g_lfxFxC.x-s*0.5f,g_lfxFxC.y-s*0.5f),V(g_lfxFxC.x+s*0.5f,g_lfxFxC.y+s*0.5f),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(255*(1-k)))); }
}

// ============================================================================================ rows: selection / result anim / icon hover
static float LfxRowK(int r,float e){ return EaseOutCubic(std::clamp((e-(float)r*0.045f)/0.5f,0.0f,1.0f)); }
static void LfxRowPost(ImDrawList* dl,int v0,ImVec2 a,ImVec2 b,int r,float e){
    int m=g_launResAnim; if(m==0||m==5) return;
    float k=LfxRowK(r,e); if(k>=0.999f) return;
    ImVec2 c=V((a.x+b.x)*0.5f,(a.y+b.y)*0.5f);
    for(int i=v0;i<dl->VtxBuffer.Size;i++){
        ImDrawVert& vx=dl->VtxBuffer[i];
        if(m==2){ vx.pos.x=c.x+(vx.pos.x-c.x)*(0.8f+0.2f*k); vx.pos.y=c.y+(vx.pos.y-c.y)*(0.8f+0.2f*k); }
        else if(m==3){ vx.pos.x+=(1.0f-k)*70.0f; }
        else if(m==4){ vx.pos.y=c.y+(vx.pos.y-c.y)*k; }
        vx.col=LfxMulAlphaCol(vx.col, m==4? 0.3f+0.7f*k : k);
    }
}
// the highlight that glides between rows / tiles (its leading edge moves first, so it stretches like jelly)
static void LfxGlide(ImDrawList* dl,ImVec2 a,ImVec2 b,float rnd,float e,int key){
    static std::unordered_map<int,ImVec4> st;
    auto it=st.find(key); if(it==st.end()){ st[key]=ImVec4(a.x,a.y,b.x,b.y); it=st.find(key); }
    ImVec4& s=it->second; float dt=std::min(g_frameDt,0.05f);
    float fast=std::min(1.0f,dt*22.0f), slow=std::min(1.0f,dt*11.0f);
    bool down=a.y>s.y+0.5f, right=a.x>s.x+0.5f;
    s.x+=(a.x-s.x)*(right? slow : fast); s.z+=(b.x-s.z)*(right? fast : slow);
    s.y+=(a.y-s.y)*(down? slow : fast);  s.w+=(b.y-s.w)*(down? fast : slow);
    dl->AddRectFilled(V(s.x,s.y),V(s.z,s.w),WithA(Mix(COL_CARD2,COL_GOLD,0.28f),(int)(230*e)),rnd);
    dl->AddRect(V(s.x,s.y),V(s.z,s.w),WithA(COL_GOLD,(int)(110*e)),rnd,0,1.2f);
}
// selection styles other than the classic fill (1 = glide draws its own highlight; rows only show hover)
static void LfxRowSel(ImDrawList* dl,ImVec2 a0,ImVec2 b0,bool sel,bool hov,float e,int id,float rnd=12){
    float ha=HoverAnim(0x7B000+id,sel);
    if(hov && !sel) dl->AddRectFilled(a0,b0,WithA(COL_INK2,(int)(e*30)),rnd);
    switch(g_launSel){
    case 2: if(ha>0.01f){ dl->AddRectFilled(a0,b0,WithA(COL_GOLD,(int)(e*28*ha)),rnd); dl->AddRect(a0,b0,WithA(COL_GOLD,(int)(e*230*ha)),rnd,0,2.0f); } break;
    case 3: if(ha>0.01f){ dl->AddRectFilled(a0,b0,WithA(Mix(COL_CARD2,COL_INK2,0.2f),(int)(e*200*ha)),rnd);
                float cy=(a0.y+b0.y)*0.5f, hh=(b0.y-a0.y-12)*0.5f*ha; dl->AddRectFilled(V(a0.x+3,cy-hh),V(a0.x+7,cy+hh),WithA(COL_GOLD,(int)(255*e)),2); } break;
    case 4: if(ha>0.01f){ for(int i=5;i>=1;i--) dl->AddRect(V(a0.x-i*1.5f,a0.y-i*1.5f),V(b0.x+i*1.5f,b0.y+i*1.5f),LfxGlowCol((int)(e*ha*28)),rnd+i*1.5f,0,2.5f);
                dl->AddRectFilled(a0,b0,WithA(Mix(COL_CARD2,COL_GOLD,0.18f),(int)(e*220*ha)),rnd); } break;
    }
}
static void LfxIconPost(ImDrawList* dl,int v0,ImVec2 c,float ha){
    if(ha<0.01f) return; float t=LfxT();
    switch(g_launIconHover){
    case 1: LfxMoveVerts(dl,v0,0,-fabsf(sinf(t*9.0f))*5.0f*ha); break;
    case 2: ScaleVerts(dl,v0,c,1.0f+0.22f*ha); break;
    case 3: LfxRotVerts(dl,v0,c,sinf(t*18.0f)*0.16f*ha); break;
    }
}

// ============================================================================================ favourites tray
static void LfxToggleFav(const std::string& p){
    if(LfxInList(g_launFavs,p)) g_launFavs.erase(std::remove_if(g_launFavs.begin(),g_launFavs.end(),[&](const std::string& x){ return _stricmp(x.c_str(),p.c_str())==0; }),g_launFavs.end());
    else g_launFavs.push_back(p);
    SaveConfig(); LRebuild(); }
static float LfxTrayH(){ return g_launTray? (g_launTrayLabels? 92.0f : 72.0f) : 0.0f; }
static void LfxPinDialog(){
    PickFileAsync(L"Apps and shortcuts\0*.exe;*.lnk;*.url;*.bat;*.cmd;*.appref-ms\0All files\0*.*\0",L"Pin an app to the launcher tray",
        [](const std::string& f){ if(!LfxInList(g_launFavs,f)){ g_launFavs.push_back(f); SaveConfig(); LRebuild(); } g_launShow=true; });
}
// returns true if it launched something (the caller closes the launcher)
static bool LfxDrawTray(ImDrawList* dl,ImGuiIO& io,float x0,float y,float x1,int al,float e,bool click,bool vertical=false,float hOverride=0){
    float h=hOverride>0? hOverride : LfxTrayH(); if(!g_launTray || h<=0) return false;
    const float T=54.0f, G=12.0f;
    bool launched=false;
    ImVec2 mp=io.MousePos;
    dl->AddRectFilled(V(x0,y+4),V(x1,y+h-4),WithA(Mix(COL_CARD2,COL_INK2,0.08f),(int)(150*e)),16);
    int n=(int)g_launFavs.size();
    static int drag=-1; static float dragStartX=0, dragStartY=0; static bool dragging=false; static float scroll=0;
    float cols=vertical? std::max(1.0f,floorf((x1-x0-G)/(T+G))) : 0;
    float full=(n+1)*(T+G)+G;
    bool inTray=mp.x>=x0&&mp.x<x1&&mp.y>=y&&mp.y<y+h;
    if(!vertical){ if(inTray && io.MouseWheel!=0) scroll-=io.MouseWheel*60; scroll=std::clamp(scroll,0.0f,std::max(0.0f,full-(x1-x0))); }
    dl->PushClipRect(V(x0+2,y),V(x1-2,y+h),true);
    auto tilePos=[&](int i)->ImVec2{
        if(vertical){ int c=(int)cols; return V(x0+G+(i%c)*(T+G),y+10+(i/c)*(T+(g_launTrayLabels?26:G))); }
        return V(x0+G+i*(T+G)-scroll,y+10); };
    const char* tip=nullptr; std::string tipS;
    int hoverIdx=-1;
    for(int i=0;i<=n;i++){
        ImVec2 p=tilePos(i);
        if(dragging && i==drag) continue;
        ImVec2 a=p, b=V(p.x+T,p.y+T);
        bool hov=mp.x>=a.x&&mp.x<b.x&&mp.y>=a.y&&mp.y<b.y;
        float ha=HoverAnim(0x7C000+i,hov);
        float lift=-4.0f*ha;
        int v0=dl->VtxBuffer.Size;
        if(i==n){                                                       // the + tile
            dl->AddRect(V(a.x,a.y+lift),V(b.x,b.y+lift),WithA(COL_INK2,(int)((90+80*ha)*e)),16,0,1.4f);
            MsIcon(dl,"add",V(a.x+T*0.5f,a.y+T*0.5f+lift),24,WithA(COL_INK2,al));
            if(hov){ tip="Pin any app or file"; if(click) LfxPinDialog(); }
            if(g_launTrayLabels) TextAt(dl,g_fSml,12,V(a.x+T*0.5f-TextW(g_fSml,12,"Add")*0.5f,b.y+4),WithA(COL_INK2,(int)(al*0.8f)),"Add");
            continue; }
        const std::string& path=g_launFavs[i];
        std::wstring wp=U82W(path);
        dl->AddRectFilled(V(a.x,a.y+lift),V(b.x,b.y+lift),WithA(Mix(COL_CARD2,COL_INK2,0.12f+0.18f*ha),(int)(235*e)),16);
        ID3D11ShaderResourceView* ic=LfxIcon(wp,true);
        if(ic) dl->AddImage((ImTextureID)ic,V(a.x+10,a.y+10+lift),V(b.x-10,b.y-10+lift),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,al));
        else MsIcon(dl,"apps",V(a.x+T*0.5f,a.y+T*0.5f+lift),26,WithA(COL_INK2,al));
        LfxIconPost(dl,v0,V(a.x+T*0.5f,a.y+T*0.5f),ha);
        std::string kw=LfxAliasFor(path);
        if(!kw.empty()){ float kw_w=TextW(g_fSml,10.5f,kw.c_str())+10; ImVec2 ka=V(b.x-kw_w+4,a.y-5+lift);
            dl->AddRectFilled(ka,V(ka.x+kw_w,ka.y+16),WithA(COL_GOLD,al),8); TextAt(dl,g_fSml,10.5f,V(ka.x+5,ka.y+1),WithA(M3OnPrimary(),al),kw.c_str()); }
        std::string nm=W2U8(LfxAppNameW(wp));
        if(g_launTrayLabels){ std::string c2=Clip(g_fSml,12,nm,T+8); TextAt(dl,g_fSml,12,V(a.x+T*0.5f-TextW(g_fSml,12,c2.c_str())*0.5f,b.y+4),WithA(COL_INK,(int)(al*0.9f)),c2.c_str()); }
        if(hov){ hoverIdx=i; tipS=nm+"  \xC2\xB7  drag to reorder, right-click to unpin"; tip=tipS.c_str();
            if(io.MouseClicked[0]){ drag=i; dragStartX=mp.x; dragStartY=mp.y; dragging=false; }
            if(io.MouseClicked[1]){ g_launFavs.erase(g_launFavs.begin()+i); SaveConfig(); LRebuild(); break; } }
    }
    // drag to reorder
    if(drag>=0 && drag<n){
        if(io.MouseDown[0]){
            if(!dragging && (fabsf(mp.x-dragStartX)>6||fabsf(mp.y-dragStartY)>6)) dragging=true;
            if(dragging){
                ImVec2 a=V(mp.x-T*0.5f,mp.y-T*0.5f);
                dl->AddRectFilled(V(a.x+3,a.y+6),V(a.x+T+3,a.y+T+6),IM_COL32(0,0,0,(int)(60*e)),16);
                dl->AddRectFilled(a,V(a.x+T,a.y+T),WithA(Mix(COL_CARD2,COL_GOLD,0.25f),(int)(245*e)),16);
                ID3D11ShaderResourceView* ic=LfxIcon(U82W(g_launFavs[drag]),true);
                if(ic) dl->AddImage((ImTextureID)ic,V(a.x+10,a.y+10),V(a.x+T-10,a.y+T-10));
                // where it would land
                int to=drag;
                if(vertical){ int c=(int)cols; int cx=(int)std::clamp((mp.x-x0-G)/(T+G),0.0f,(float)(c-1)); int ry=(int)std::max(0.0f,(mp.y-y-10)/(T+(g_launTrayLabels?26:G))); to=std::clamp(ry*c+cx,0,n-1); }
                else to=std::clamp((int)((mp.x-x0-G+scroll)/(T+G)),0,n-1);
                if(to!=drag){ std::string moved=g_launFavs[drag]; g_launFavs.erase(g_launFavs.begin()+drag); g_launFavs.insert(g_launFavs.begin()+to,moved); drag=to; }
            }
        } else {
            if(!dragging && hoverIdx==drag){ AetherOpen(U82W(g_launFavs[drag])); LfxLaunchFx(V(tilePos(drag).x+T*0.5f,tilePos(drag).y+T*0.5f),LfxIcon(U82W(g_launFavs[drag]),true)); launched=true; }
            else if(dragging) SaveConfig();
            drag=-1; dragging=false; }
    }
    dl->PopClipRect();
    if(n==0 && !g_launTrayLabels){ const char* m="Pin favourites here \xE2\x80\x94 click + or right-click an app"; TextAt(dl,g_fSml,13,V(x0+G*2+T,y+h*0.5f-9),WithA(COL_INK2,(int)(al*0.8f)),m); }
    else if(n==0){ const char* m="Pin favourites here: click +, or right-click any app"; TextAt(dl,g_fSml,13,V(x0+G*2+T+4,y+26),WithA(COL_INK2,(int)(al*0.8f)),m); }
    if(tip && !dragging){ float tw=TextW(g_fSml,13,tip)+20; ImVec2 t0=V(std::clamp(mp.x-tw*0.5f,x0,std::max(x0,x1-tw)),y-30);
        dl->AddRectFilled(t0,V(t0.x+tw,t0.y+26),IM_COL32(28,28,34,240),13); TextAt(dl,g_fSml,13,V(t0.x+10,t0.y+5),IM_COL32(236,236,240,255),tip); }
    return launched;
}

// "↵ Firefox" at the right of the search field while the query is exactly a keyword. Returns its width (0 = none).
static float LfxAliasChip(ImDrawList* dl,float x1,float y,float h,int al){
    int ai=LfxAliasMatch(g_lsearch); if(ai<0) return 0;
    auto v=LfxAliases(); std::string nm="\xE2\x86\xB5  "+W2U8(LfxAppNameW(U82W(v[ai].path)));
    float chipW=TextW(g_fSml,13,nm.c_str())+44; ImVec2 ca=V(x1-chipW-8,y+(h-28)*0.5f);
    float pop=EaseOutBack(std::clamp(HoverAnim(0x7E000+ai,true),0.0f,1.0f));
    int v0=dl->VtxBuffer.Size;
    dl->AddRectFilled(ca,V(x1-8,ca.y+28),WithA(COL_GOLD,al),14);
    ID3D11ShaderResourceView* ic=LfxIcon(U82W(v[ai].path),false);
    if(ic) dl->AddImage((ImTextureID)ic,V(ca.x+7,ca.y+5),V(ca.x+25,ca.y+23)); else MsIcon(dl,"bolt",V(ca.x+16,ca.y+14),16,WithA(M3OnPrimary(),al));
    TextAt(dl,g_fSml,13,V(ca.x+30,ca.y+6),WithA(M3OnPrimary(),al),nm.c_str());
    ScaleVerts(dl,v0,V(x1-8-chipW*0.5f,ca.y+14),0.6f+0.4f*pop);
    return chipW+8;
}

// ============================================================================================ settings: keywords + hotkeys
static void LfxSettingsAliases(ImDrawList* dl,ImGuiIO& io,float mx,float& my,float rowW,int al,float e,bool click){
    auto v=LfxAliases();
    static int editKey=-1, capHk=-1; static ULONGLONG capAt=0;
    const float RH=58;
    ImVec2 mp=io.MousePos;
    bool changed=false; int remove=-1;
    if(v.empty()){ TextAt(dl,g_fSml,13,V(mx,my+2),WithA(COL_INK2,al),"No keywords yet \xE2\x80\x94 add an app below."); my+=28; }
    for(size_t i=0;i<v.size();i++){
        ImVec2 a=V(mx-8,my), b=V(mx+rowW,my+RH-6);
        dl->AddRectFilled(a,b,WithA(Mix(COL_CARD,COL_CARD2,0.5f),(int)(255*e)),12);
        std::wstring wp=U82W(v[i].path);
        ImVec2 icc=V(mx+14,my+(RH-6)*0.5f);
        ID3D11ShaderResourceView* ic=LfxIcon(wp,false);
        if(ic) dl->AddImage((ImTextureID)ic,V(icc.x-14,icc.y-14),V(icc.x+14,icc.y+14),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,al));
        else MsIcon(dl,"apps",icc,22,WithA(COL_INK2,al));
        float kx=mx+rowW*0.44f, kw=std::min(130.0f,rowW*0.2f);
        float tx=mx+38;
        TextAt(dl,g_fMed,15,V(tx,my+8),WithA(COL_INK,al),Clip(g_fMed,15,W2U8(LfxAppNameW(wp)),kx-tx-10).c_str());
        TextAt(dl,g_fSml,12,V(tx,my+29),WithA(COL_INK2,al),Clip(g_fSml,12,v[i].path,kx-tx-10).c_str());
        // keyword
        ImVec2 ka=V(kx,my+12), kb=V(kx+kw,my+RH-18);
        bool kh=mp.x>=ka.x&&mp.x<kb.x&&mp.y>=ka.y&&mp.y<kb.y; bool editing=editKey==(int)i;
        dl->AddRectFilled(ka,kb,WithA(editing? Mix(COL_CARD2,COL_GOLD,0.2f) : COL_TRACK,al),10);
        if(editing) dl->AddRect(ka,kb,WithA(COL_GOLD,al),10,0,1.5f);
        std::string ks=v[i].key.empty()? std::string("keyword") : v[i].key;
        TextAt(dl,g_fMed,14,V(ka.x+10,ka.y+5),WithA(v[i].key.empty()? COL_INK2 : COL_INK,al),ks.c_str());
        if(editing && fmodf(LfxT(),1.0f)<0.55f){ float cx=ka.x+10+(v[i].key.empty()? 0 : TextW(g_fMed,14,v[i].key.c_str())); dl->AddRectFilled(V(cx+1,ka.y+6),V(cx+2.6f,kb.y-6),WithA(COL_GOLD,al)); }
        if(click && kh){ editKey=(int)i; capHk=-1; }
        else if(click && editing && !kh) editKey=-1;
        if(editing){
            for(int q=0;q<io.InputQueueCharacters.Size;q++){ ImWchar c=io.InputQueueCharacters[q];
                if(((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.') && v[i].key.size()<16){ v[i].key.push_back((char)tolower((int)c)); changed=true; } }
            if(ImGui::IsKeyPressed(ImGuiKey_Backspace) && !v[i].key.empty()){ v[i].key.pop_back(); changed=true; }
            if(ImGui::IsKeyPressed(ImGuiKey_Enter)||ImGui::IsKeyPressed(ImGuiKey_Escape)) editKey=-1;
        }
        // hotkey
        float hx=kb.x+10, hw=std::min(170.0f,rowW*0.24f);
        ImVec2 ha=V(hx,my+12), hb=V(hx+hw,my+RH-18);
        bool hh=mp.x>=ha.x&&mp.x<hb.x&&mp.y>=ha.y&&mp.y<hb.y;
        bool cap=capHk==(int)i;
        bool bad=!v[i].hotkey.empty() && i<g_lfxHkOk.size() && !g_lfxHkOk[i] && !g_lfxHkPaused;
        dl->AddRectFilled(ha,hb,WithA(cap? Mix(COL_CARD2,COL_GOLD,0.25f) : COL_TRACK,al),10);
        std::string hs= cap? std::string("Press keys\xE2\x80\xA6") : v[i].hotkey.empty()? std::string("+ hotkey") : v[i].hotkey;
        MsIcon(dl,"keyboard",V(ha.x+14,(ha.y+hb.y)*0.5f),15,WithA(bad? IM_COL32(226,110,100,255) : COL_INK2,al));
        TextAt(dl,g_fSml,13,V(ha.x+28,ha.y+6),WithA(bad? IM_COL32(226,110,100,255) : (v[i].hotkey.empty()&&!cap? COL_INK2 : COL_GOLD),al),Clip(g_fSml,13,hs,hw-34).c_str());
        if(bad && hh){ const char* t="Another app already owns this combination"; float tw=TextW(g_fSml,12,t)+16; dl->AddRectFilled(V(ha.x,ha.y-28),V(ha.x+tw,ha.y-4),IM_COL32(28,28,34,240),10); TextAt(dl,g_fSml,12,V(ha.x+8,ha.y-24),IM_COL32(236,236,240,255),t); }
        if(click && hh && !cap){ capHk=(int)i; capAt=GetTickCount64(); editKey=-1; g_lfxHkPaused=true; LfxRegisterAliasHotkeys(); }
        // remove
        ImVec2 xc=V(b.x-20,my+(RH-6)*0.5f); bool xh=fabsf(mp.x-xc.x)<13&&fabsf(mp.y-xc.y)<13;
        if(xh) dl->AddCircleFilled(xc,13,WithA(COL_INK2,50),20);
        MsIcon(dl,"delete",xc,17,WithA(COL_INK2,al));
        if(click && xh) remove=(int)i;
        my+=RH;
    }
    if(capHk>=0){
        if(capHk>=(int)v.size()){ capHk=-1; g_lfxHkPaused=false; LfxRegisterAliasHotkeys(); }
        else {
            UINT mods=0;
            if(GetAsyncKeyState(VK_CONTROL)&0x8000) mods|=MOD_CONTROL;
            if(GetAsyncKeyState(VK_MENU)&0x8000)    mods|=MOD_ALT;
            if(GetAsyncKeyState(VK_SHIFT)&0x8000)   mods|=MOD_SHIFT;
            if((GetAsyncKeyState(VK_LWIN)&0x8000)||(GetAsyncKeyState(VK_RWIN)&0x8000)) mods|=MOD_WIN;
            UINT got=0;
            for(UINT vk=0x08; vk<=0xFE; vk++){
                if(vk==VK_CONTROL||vk==VK_MENU||vk==VK_SHIFT||vk==VK_LWIN||vk==VK_RWIN||vk==VK_LCONTROL||vk==VK_RCONTROL||vk==VK_LMENU||vk==VK_RMENU||vk==VK_LSHIFT||vk==VK_RSHIFT) continue;
                if(vk==VK_LBUTTON||vk==VK_RBUTTON||vk==VK_MBUTTON||vk==VK_XBUTTON1||vk==VK_XBUTTON2) continue;
                if(GetAsyncKeyState(vk)&0x8000){ got=vk; break; } }
            bool done=false;
            if(got==VK_ESCAPE) done=true;
            else if((got==VK_BACK||got==VK_DELETE) && !mods){ v[capHk].hotkey.clear(); changed=true; done=true; }
            else if(got && mods){ v[capHk].hotkey=HotkeyName(mods,got); changed=true; done=true; }
            else if(GetTickCount64()-capAt>8000) done=true;
            if(done){ capHk=-1; g_lfxHkPaused=false; if(!changed) LfxRegisterAliasHotkeys(); }
        }
    }
    if(remove>=0 && remove<(int)v.size()){ v.erase(v.begin()+remove); changed=true; editKey=-1; capHk=-1; g_lfxHkPaused=false; }
    if(changed) LfxSaveAliases(v);
    // buttons
    my+=4;
    float bx=mx;
    auto btn=[&](const char* icon,const char* lab)->bool{
        float bw=TextW(g_fSml,13,lab)+44; ImVec2 a=V(bx,my), b=V(bx+bw,my+32);
        bool h=mp.x>=a.x&&mp.x<b.x&&mp.y>=a.y&&mp.y<b.y; float ha=HoverAnim(0x7E100+(int)bx,h);
        dl->AddRectFilled(a,b,WithA(COL_INK2,(int)((30+ha*46)*e)),16);
        MsIcon(dl,icon,V(a.x+18,my+16),16,WithA(COL_INK,al)); TextAt(dl,g_fSml,13,V(a.x+32,my+8),WithA(COL_INK,al),lab);
        bx+=bw+8; return h&&click; };
    auto keyFor=[](const std::string& path){ std::string k=W2U8(SlLeaf(U82W(path))); std::string o; for(char c:k) if(isalnum((unsigned char)c)) o.push_back((char)tolower((unsigned char)c)); if(o.size()>8) o.resize(8); return o.empty()? std::string("app") : o; };
    if(btn("add","Add an app\xE2\x80\xA6")){
        PickFileAsync(L"Apps and shortcuts\0*.exe;*.lnk;*.url;*.bat;*.cmd;*.appref-ms\0All files\0*.*\0",L"Pick an app for a keyword",
            [keyFor](const std::string& f){ auto v2=LfxAliases(); v2.push_back({keyFor(f),"",f}); LfxSaveAliases(v2); }); }
    if(btn("favorite","Add my favourites")){
        auto v2=LfxAliases(); bool any=false;
        for(auto& f:g_launFavs){ bool have=false; for(auto& x:v2) if(_stricmp(x.path.c_str(),f.c_str())==0) have=true; if(!have){ v2.push_back({keyFor(f),"",f}); any=true; } }
        if(any) LfxSaveAliases(v2); }
    my+=44;
}

// ============================================================================================ shared search bar
// pill + magnifier + animated text + a chip saying what Enter will do (a keyword's app, or the maths result)
static void LfxSearchBar(ImDrawList* dl,float x0,float y,float x1,float h,int al,float e,const char* placeholder){
    dl->AddRectFilled(V(x0,y),V(x1,y+h),WithA(Mix(COL_CARD2,COL_INK2,0.10f),(int)(e*245)),h*0.5f);
    if(g_launGlow) dl->AddRect(V(x0,y),V(x1,y+h),LfxGlowCol((int)(90*e)),h*0.5f,0,1.2f);
    MsIcon(dl,"search",V(x0+h*0.5f+4,y+h*0.5f),22,WithA(COL_INK2,al));
    float qx=x0+h+6;
    float chipW=LfxAliasChip(dl,x1,y,h,al);
    if(chipW<=0 && g_calcOk && g_lsearch[0]){ char rb[64]; double v=g_calcVal;
        if(fabs(v-llround(v))<1e-9 && fabs(v)<9e15) snprintf(rb,64,"= %lld",(long long)llround(v)); else snprintf(rb,64,"= %.8g",v);
        chipW=TextW(g_fMed,16,rb)+24; TextAt(dl,g_fMed,16,V(x1-chipW,y+h*0.5f-11),WithA(COL_GOLD,al),rb); }
    dl->PushClipRect(V(qx-4,y),V(x1-chipW-10,y+h),true);
    LfxSearchText(dl,g_fReg,18,V(qx,y+h*0.5f-11),22,g_lsearch,placeholder,al);
    dl->PopClipRect();
}

// ============================================================================================ alternative layouts
struct LfxItem { int kind; ID3D11ShaderResourceView* icon; std::string name, sub; std::wstring path; };   // kind 0 calc 1 window 2 app
static void LfxItems(std::vector<LfxItem>& out,bool big){
    out.clear();
    if(g_calcOk){ char rb[64]; double v=g_calcVal;
        if(fabs(v-llround(v))<1e-9 && fabs(v)<9e15) snprintf(rb,64,"%lld",(long long)llround(v)); else snprintf(rb,64,"%.8g",v);
        out.push_back({0,nullptr,std::string("= ")+rb,"Enter copies",L""}); }
    for(auto& w:g_winHits) out.push_back({1,w.icon,w.title.empty()? w.exe : w.title,"Open window",L""});
    for(int i:g_lfilt){ auto& a=g_lapps[i]; out.push_back({2,a.icon,W2U8(a.name),a.desc,a.path}); }   // icons are asked for only when a tile is drawn
}
static void LfxItemGlyph(ImDrawList* dl,LfxItem& it,ImVec2 c,float s,int al){
    if(!it.icon && it.kind==2) it.icon=LfxIcon(it.path,s>40.0f);
    if(it.icon) dl->AddImage((ImTextureID)it.icon,V(c.x-s*0.5f,c.y-s*0.5f),V(c.x+s*0.5f,c.y+s*0.5f),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,al));
    else if(it.kind==0){ dl->AddCircleFilled(c,s*0.46f,WithA(Mix(COL_CARD2,COL_GOLD,0.3f),al),32); TextAt(dl,g_fMed,s*0.5f,V(c.x-TextW(g_fMed,s*0.5f,"=")*0.5f,c.y-s*0.32f),WithA(COL_INK,al),"="); }
    else MsIcon(dl,it.kind==1? "select_window" : "apps",c,s*0.7f,WithA(COL_INK2,al));
}
static void LfxAltClickItem(int idx,ImVec2 c,const LfxItem& it){
    g_lsel=idx; LfxLaunchFx(c,it.icon); LLaunch(); }
static void LfxRightClickItem(const LfxItem& it){ if(it.kind==2) LfxToggleFav(W2U8(it.path)); }

static void LfxDrawAltLayout(ImGuiIO& io,ImDrawList* dl,float W,float H,float a,float e,int al,bool armed,bool click){
    const int layout=g_launLayout;
    std::vector<LfxItem> items; LfxItems(items,layout!=3);
    int total=(int)items.size();
    ImVec2 mp=io.MousePos;
    const float S=std::clamp(g_launIconSize,0.6f,1.8f);
    const char* placeholder=g_launStyle==1? "Type \">\" for commands" : "Search apps\xE2\x80\xA6   Type > for commands";
    float trayH=LfxTrayH();
    int v0=dl->VtxBuffer.Size;
    auto enter=[&]{ if(armed&&(ImGui::IsKeyPressed(ImGuiKey_Enter)||ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) { LLaunch(); return true; } return false; };
    auto nav=[&](int d){ if(total<=0) return; g_lsel=((g_lsel+d)%total+total)%total; g_launMouseLive=false; g_launMousePos=LaunCursorLogical(); };
    if(total>0) g_lsel=((g_lsel%total)+total)%total; else g_lsel=0;
    float edgeGap=(float)g_gap+26.0f;
    auto restY=[&](float h){ return g_launPos==LPOS_CENTRE? (H-h)*0.5f : g_launPos==LPOS_TOP? edgeGap : H-h-edgeGap; };
    auto travel=[&](float h){ float t=(h*0.30f+56.0f)*(1.0f-e); return g_launAnimStyle==LANIM_RISE? t : g_launAnimStyle==LANIM_DROP? -t : 0.0f; };
    bool launched=false;
    if(enter()) return;

    // ----------------------------------------------------------------------------- GRID
    if(layout==1||layout==2){
        const bool strip=(layout==2);
        float T=104.0f*S, pad=18.0f, searchH=50.0f;
        int cols = strip? std::clamp((int)((std::min(W-80.0f,1400.0f)-pad*2)/T),4,14) : std::clamp(g_launGridCols,3,10);
        int rowsVis = strip? 1 : std::clamp((int)ceilf((float)std::max(total,1)/cols),1,std::clamp(g_launchMax/2,1,6));
        float pw=cols*T+pad*2, ph=pad+searchH+14+rowsVis*(T+8)+trayH+pad;
        float px=(W-pw)*0.5f, py=restY(ph)+travel(ph);
        // keys
        if(ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nav(+1);
        if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  nav(-1);
        if(ImGui::IsKeyPressed(ImGuiKey_DownArrow))  nav(strip? +1 : +cols);
        if(ImGui::IsKeyPressed(ImGuiKey_UpArrow))    nav(strip? -1 : -cols);
        if(ImGui::IsKeyPressed(ImGuiKey_Tab))        nav(io.KeyShift? -1 : +1);
        int vp=dl->VtxBuffer.Size;
        if(LfxCustomPanel()) LfxPanel(dl,V(px,py),V(px+pw,py+ph),g_launRound,e,W,H);
        else { if(g_launBlur && g_launBg) dl->AddImageRounded((ImTextureID)g_launBg,V(px,py),V(px+pw,py+ph),ImVec2(std::clamp(px/W,0.0f,1.0f),std::clamp(py/H,0.0f,1.0f)),ImVec2(std::clamp((px+pw)/W,0.0f,1.0f),std::clamp((py+ph)/H,0.0f,1.0f)),IM_COL32(255,255,255,al),g_launRound);
               GlassPanel(dl,V(px,py),V(px+pw,py+ph),g_launRound,0,nullptr,e,(int)(std::clamp(g_launOpacity,0.1f,1.0f)*255.0f)); }
        float ins=LfxShapeInset(g_launShape,pw,ph);
        LfxSearchBar(dl,px+pad+ins,py+pad,px+pw-pad-ins,searchH,al,e,placeholder);
        float gy=py+pad+searchH+14, gx=px+pad;
        float trayY = gy+rowsVis*(T+8);
        // scrolling (rows for the grid, columns for the strip), keyboard-led, springy
        static float sc=0; float want=0;
        int selRow = strip? 0 : g_lsel/cols, selCol = strip? g_lsel : g_lsel%cols;
        static int firstRow=0, firstCol=0;
        if(strip){ if(selCol<firstCol) firstCol=selCol; if(selCol>=firstCol+cols) firstCol=selCol-cols+1; firstCol=std::clamp(firstCol,0,std::max(0,total-cols)); want=(float)firstCol*T; }
        else { int rowsAll=(int)ceilf((float)total/cols); if(selRow<firstRow) firstRow=selRow; if(selRow>=firstRow+rowsVis) firstRow=selRow-rowsVis+1; firstRow=std::clamp(firstRow,0,std::max(0,rowsAll-rowsVis)); want=(float)firstRow*(T+8); }
        { bool over=mp.x>px&&mp.x<px+pw&&mp.y>gy&&mp.y<trayY; if(over && io.MouseWheel!=0) nav(io.MouseWheel>0? (strip? -1 : -cols) : (strip? 1 : cols)); }
        sc += (want-sc)*std::min(1.0f,g_frameDt*14.0f);
        dl->PushClipRect(V(px+4,gy-6),V(px+pw-4,trayY+2),true);
        // glide highlight first, under the tiles
        if(total>0 && g_launSel==1){ float tx0 = strip? gx+g_lsel*T-sc : gx+selCol*T, ty0 = strip? gy : gy+selRow*(T+8)-sc;
            LfxGlide(dl,V(tx0+4,ty0+2),V(tx0+T-4,ty0+T+4),18,e,layout); }
        for(int i=0;i<total;i++){
            int r = strip? 0 : i/cols, c = strip? i : i%cols;
            float tx = gx+c*T-(strip? sc : 0), ty = gy+r*(T+8)-(strip? 0 : sc);
            if(tx>px+pw || tx+T<px || ty>trayY || ty+T<gy-T) continue;
            ImVec2 ta=V(tx+4,ty+2), tb=V(tx+T-4,ty+T+4);
            bool hov=mp.x>=ta.x&&mp.x<tb.x&&mp.y>=ta.y&&mp.y<tb.y&&mp.y<trayY;
            if(hov && g_launMouseLive) g_lsel=i;
            bool sel=i==g_lsel;
            int vr=dl->VtxBuffer.Size;
            if(g_launSel==0){ if(sel) dl->AddRectFilled(ta,tb,WithA(Mix(COL_CARD2,COL_GOLD,0.30f),(int)(e*235)),18); else if(hov) dl->AddRectFilled(ta,tb,WithA(COL_INK2,(int)(e*34)),18); }
            else if(g_launSel!=1) LfxRowSel(dl,ta,tb,sel,hov,e,i,18);
            else if(hov&&!sel) dl->AddRectFilled(ta,tb,WithA(COL_INK2,(int)(e*30)),18);
            float isz=56.0f*S; ImVec2 ic=V(tx+T*0.5f,ty+10+isz*0.5f);
            float ha=HoverAnim(0x7D000+i,hov||sel);
            int vi=dl->VtxBuffer.Size;
            LfxItemGlyph(dl,items[i],ic,isz,al);
            LfxIconPost(dl,vi,ic,ha);
            std::string nm=Clip(g_fSml,13,items[i].name,T-12);
            TextAt(dl,g_fSml,13,V(tx+T*0.5f-TextW(g_fSml,13,nm.c_str())*0.5f,ty+T-22),WithA(COL_INK,al),nm.c_str());
            if(items[i].kind==2 && LfxInList(g_launFavs,W2U8(items[i].path))) MsIcon(dl,"favorite",V(tb.x-12,ta.y+12),14,WithA(COL_GOLD,al));
            LfxRowPost(dl,vr,ta,tb,strip? i : r*2+c,e);
            if(armed && hov && click){ LfxAltClickItem(i,ic,items[i]); launched=true; break; }
            if(hov && io.MouseClicked[1]) { LfxRightClickItem(items[i]); break; }
        }
        if(total==0){ const char* m="No matching app"; TextAt(dl,g_fSml,15,V(px+pw*0.5f-TextW(g_fSml,15,m)*0.5f,gy+rowsVis*(T+8)*0.5f-8),WithA(COL_INK2,al),m); }
        dl->PopClipRect();
        if(!launched && trayH>0 && LfxDrawTray(dl,io,px+pad+ins,trayY,px+pw-pad-ins,al,e,armed&&click)) g_launShow=false;
        LfxTilt(dl,vp,V(px+pw*0.5f,py+ph*0.5f),pw,ph,e);
        if(g_launAnimStyle==LANIM_POP) ScaleVerts(dl,v0,V(px+pw*0.5f,py+ph*0.5f),0.86f+0.14f*std::clamp(e,0.0f,1.0f));
    }
    // ----------------------------------------------------------------------------- VERTICAL (a column on a screen edge)
    else if(layout==3){
        float gap=16.0f, pad=14.0f, searchH=48.0f, rowH=52.0f*std::clamp(S,0.8f,1.4f);
        // stay clear of the frame / the bar: the larger of the frame inset and the monitor's reserved work area
        float eL=0,eR=0,eT=0,eB=0;
        if(FrameBornOn()){ eL=FrameInset(EDGE_LEFT); eR=FrameInset(EDGE_RIGHT); eT=FrameInset(EDGE_TOP); eB=FrameInset(EDGE_BOTTOM); }
        { MONITORINFO mi{sizeof(mi)}; HMONITOR hm=MonitorFromPoint(POINT{g_mx+g_mw/2,g_my+g_mh/2},MONITOR_DEFAULTTONEAREST);
          if(GetMonitorInfoW(hm,&mi)){ eL=std::max(eL,(mi.rcWork.left-g_mx)/g_uiScale); eT=std::max(eT,(mi.rcWork.top-g_my)/g_uiScale);
              eR=std::max(eR,(g_mx+g_mw-mi.rcWork.right)/g_uiScale); eB=std::max(eB,(g_my+g_mh-mi.rcWork.bottom)/g_uiScale); } }
        float pw=std::clamp(g_launWidth*0.62f,300.0f,520.0f), ph=H-eT-eB-gap*2;
        bool right=g_launSide==1;
        float px = right? W-eR-pw-gap : eL+gap, py=eT+gap;
        px += (1.0f-e)*(pw+gap+30.0f)*(right? 1.0f : -1.0f);
        if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)||ImGui::IsKeyPressed(ImGuiKey_Tab)) nav(+1);
        if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nav(-1);
        int vp=dl->VtxBuffer.Size;
        if(LfxCustomPanel()) LfxPanel(dl,V(px,py),V(px+pw,py+ph),g_launRound,e,W,H);
        else { if(g_launBlur && g_launBg) dl->AddImageRounded((ImTextureID)g_launBg,V(px,py),V(px+pw,py+ph),ImVec2(std::clamp(px/W,0.0f,1.0f),0),ImVec2(std::clamp((px+pw)/W,0.0f,1.0f),std::clamp((py+ph)/H,0.0f,1.0f)),IM_COL32(255,255,255,al),g_launRound);
               GlassPanel(dl,V(px,py),V(px+pw,py+ph),g_launRound,0,nullptr,e,(int)(std::clamp(g_launOpacity,0.1f,1.0f)*255.0f)); }
        float ins=LfxShapeInset(g_launShape,pw,ph);
        LfxSearchBar(dl,px+pad+ins,py+pad,px+pw-pad-ins,searchH,al,e,"Search apps\xE2\x80\xA6");
        float y=py+pad+searchH+12;
        // tray as a small grid under the search
        float trayRows = g_launTray? std::min(3.0f,std::max(1.0f,ceilf(((float)g_launFavs.size()+1)/std::max(1.0f,floorf((pw-pad*2-12)/66.0f))))) : 0;
        if(g_launTray){
            float th=trayRows*(54+(g_launTrayLabels?26:12))+20;
            // LfxDrawTray reads its own height; give it a tall box by drawing into the space
            if(LfxDrawTray(dl,io,px+pad+ins,y,px+pw-pad-ins,al,e,armed&&click,true,th)) g_launShow=false;
            y+=th+8;
        }
        float listTop=y, listBot=py+ph-pad;
        int vis=std::max(1,(int)((listBot-listTop)/rowH));
        static int first=0; if(g_lsel<first) first=g_lsel; if(g_lsel>=first+vis) first=g_lsel-vis+1; first=std::clamp(first,0,std::max(0,total-vis));
        { bool over=mp.x>px&&mp.x<px+pw&&mp.y>listTop&&mp.y<listBot; if(over&&io.MouseWheel!=0) nav(io.MouseWheel>0? -1 : 1); }
        dl->PushClipRect(V(px+4,listTop-2),V(px+pw-4,listBot),true);
        if(total>0 && g_launSel==1 && g_lsel>=first && g_lsel<first+vis){ float ry=listTop+(g_lsel-first)*rowH; LfxGlide(dl,V(px+pad+ins-4,ry),V(px+pw-pad-ins+4,ry+rowH-4),14,e,layout); }
        for(int r=0;r<vis;r++){ int i=first+r; if(i>=total) break;
            float ry=listTop+r*rowH; ImVec2 ra=V(px+pad+ins-4,ry), rb=V(px+pw-pad-ins+4,ry+rowH-4);
            bool hov=mp.x>=ra.x&&mp.x<rb.x&&mp.y>=ra.y&&mp.y<rb.y; if(hov&&g_launMouseLive) g_lsel=i;
            bool sel=i==g_lsel; int vr=dl->VtxBuffer.Size;
            if(g_launSel==0){ if(sel) dl->AddRectFilled(ra,rb,WithA(COL_GOLD,(int)(e*225)),14); else if(hov) dl->AddRectFilled(ra,rb,WithA(COL_INK2,(int)(e*40)),14); }
            else if(g_launSel!=1) LfxRowSel(dl,ra,rb,sel,hov,e,i,14);
            ImU32 nc=(sel&&g_launSel==0)? (g_darkUI?IM_COL32(14,20,16,255):IM_COL32(250,254,252,255)) : COL_INK;
            float isz=32; ImVec2 ic=V(ra.x+10+isz*0.5f,ry+(rowH-4)*0.5f);
            int vi=dl->VtxBuffer.Size; LfxItemGlyph(dl,items[i],ic,isz,al); LfxIconPost(dl,vi,ic,HoverAnim(0x7D400+i,hov||sel));
            float tx=ic.x+isz*0.5f+12;
            TextAt(dl,g_fMed,16,V(tx,ry+(g_launchDesc?6.0f:14.0f)),WithA(nc,al),Clip(g_fMed,16,items[i].name,rb.x-tx-10).c_str());
            if(g_launchDesc) TextAt(dl,g_fSml,12.5f,V(tx,ry+27),WithA(sel&&g_launSel==0? MulA(nc,0.72f) : COL_INK2,al),Clip(g_fSml,12.5f,items[i].sub,rb.x-tx-10).c_str());
            LfxRowPost(dl,vr,ra,rb,r,e);
            if(armed&&hov&&click){ LfxAltClickItem(i,ic,items[i]); launched=true; break; }
            if(hov&&io.MouseClicked[1]){ LfxRightClickItem(items[i]); break; }
        }
        if(total==0){ const char* m="No matching app"; TextAt(dl,g_fSml,15,V(px+pw*0.5f-TextW(g_fSml,15,m)*0.5f,listTop+30),WithA(COL_INK2,al),m); }
        dl->PopClipRect();
        LfxTilt(dl,vp,V(px+pw*0.5f,py+ph*0.5f),pw,ph,e);
    }
    // ----------------------------------------------------------------------------- RADIAL (a ring around the search)
    else if(layout==4){
        ImVec2 c=V(W*0.5f,H*0.5f);
        int n1=std::min(total,12), n2=std::clamp(total-12,0,18);
        float R1=235.0f*S, R2=345.0f*S, tile=74.0f*S;
        float Rp=(n2>0? R2 : R1)+tile*0.5f+46.0f;
        if(ImGui::IsKeyPressed(ImGuiKey_RightArrow)||ImGui::IsKeyPressed(ImGuiKey_DownArrow)||ImGui::IsKeyPressed(ImGuiKey_Tab)) nav(+1);
        if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow)||ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nav(-1);
        if(io.MouseWheel!=0) nav(io.MouseWheel>0? -1 : 1);
        float grow=0.55f+0.45f*EaseOutBack(std::clamp(e,0.0f,1.0f));
        int vp=dl->VtxBuffer.Size;
        { std::vector<ImVec2> P; LfxCircle(c,Rp*grow,P); LfxPanelPoly(dl,P,V(c.x-Rp*grow,c.y-Rp*grow),V(c.x+Rp*grow,c.y+Rp*grow),e,W,H,Rp*grow*0.30f); }
        // rings
        dl->AddCircle(c,R1,WithA(COL_INK2,(int)(30*e)),96,1.0f);
        if(n2>0) dl->AddCircle(c,R2,WithA(COL_INK2,(int)(22*e)),128,1.0f);
        // ring 1 turns so the selection sits at the top
        static float rot=0; float step1=n1>0? IM_PI*2/n1 : 0;
        float wantRot = (g_lsel<n1)? -g_lsel*step1 : rot;
        { float d=wantRot-rot; while(d>IM_PI) d-=IM_PI*2; while(d<-IM_PI) d+=IM_PI*2; rot+=d*std::min(1.0f,g_frameDt*9.0f); }
        float spin=(1.0f-e)*1.2f;
        int hoverI=-1;
        for(int i=0;i<n1+n2;i++){
            bool inner=i<n1; int k=inner? i : i-n1, n=inner? n1 : n2;
            float ang=-IM_PI*0.5f+(inner? rot : 0.0f)+k*(IM_PI*2/n)+spin*(inner? 1.0f : -1.0f);
            float ke=EaseOutBack(std::clamp((e-(float)i*0.025f)/0.6f,0.0f,1.0f));
            float R=(inner? R1 : R2)*(0.25f+0.75f*ke);
            ImVec2 p=V(c.x+cosf(ang)*R,c.y+sinf(ang)*R);
            bool sel=i==g_lsel;
            float sa=HoverAnim(0x7D800+i,sel);
            float ts=tile*(inner? 1.0f : 0.8f)*(1.0f+0.22f*sa);
            bool hov=(mp.x-p.x)*(mp.x-p.x)+(mp.y-p.y)*(mp.y-p.y)<ts*ts*0.25f;
            if(hov&&g_launMouseLive){ g_lsel=i; hoverI=i; }
            int vr=dl->VtxBuffer.Size;
            if(sel && g_launSel!=0){ for(int s2=5;s2>=1;s2--) dl->AddCircle(p,ts*0.5f+s2*2.0f,LfxGlowCol((int)(40*e*sa)),40,2.5f); }
            dl->AddCircleFilled(p,ts*0.5f,WithA(sel? Mix(COL_CARD2,COL_GOLD,0.32f) : Mix(COL_CARD2,COL_INK2,hov? 0.2f : 0.08f),(int)(235*e)),40);
            int vi=dl->VtxBuffer.Size;
            LfxItemGlyph(dl,items[i],p,ts*0.56f,(int)(al*std::clamp(ke,0.0f,1.0f)));
            LfxIconPost(dl,vi,p,sa);
            LfxFadeVerts(dl,vr,std::clamp(ke,0.0f,1.0f));
            if(armed&&hov&&click){ LfxAltClickItem(i,p,items[i]); launched=true; break; }
            if(hov&&io.MouseClicked[1]){ LfxRightClickItem(items[i]); break; }
        }
        (void)hoverI;
        // centre: search pill + the selected name
        float sw=std::min(R1*2-tile-30,380.0f), sh=48;
        LfxSearchBar(dl,c.x-sw*0.5f,c.y-sh-8,c.x+sw*0.5f,sh,al,e,"Search\xE2\x80\xA6");
        if(total>0 && g_lsel<n1+n2){ const std::string& nm=items[g_lsel].name; std::string cl=Clip(g_fMed,19,nm,sw);
            TextAt(dl,g_fMed,19,V(c.x-TextW(g_fMed,19,cl.c_str())*0.5f,c.y+10),WithA(COL_INK,al),cl.c_str());
            std::string sub=Clip(g_fSml,13,items[g_lsel].sub,sw);
            TextAt(dl,g_fSml,13,V(c.x-TextW(g_fSml,13,sub.c_str())*0.5f,c.y+36),WithA(COL_INK2,al),sub.c_str()); }
        else if(total==0){ const char* m="No matching app"; TextAt(dl,g_fSml,15,V(c.x-TextW(g_fSml,15,m)*0.5f,c.y+14),WithA(COL_INK2,al),m); }
        if(total>n1+n2){ char more[32]; snprintf(more,32,"+%d more \xE2\x80\x94 keep typing",total-n1-n2); TextAt(dl,g_fSml,12.5f,V(c.x-TextW(g_fSml,12.5f,more)*0.5f,c.y+58),WithA(COL_INK2,(int)(al*0.8f)),more); }
        if(!launched && g_launTray){ float tw=std::min(W-80.0f,(float)(g_launFavs.size()+1)*66.0f+24.0f); float ty=c.y+Rp*grow+18;
            if(ty+LfxTrayH()>H-10) ty=c.y-Rp*grow-LfxTrayH()-18;
            if(LfxDrawTray(dl,io,c.x-tw*0.5f,ty,c.x+tw*0.5f,al,e,armed&&click)) g_launShow=false; }
        LfxTilt(dl,vp,c,Rp*2,Rp*2,e);
    }
    LfxDrawLaunchFx(dl);
    g_launRect=RECT{0,0,(LONG)W,(LONG)H};
}
