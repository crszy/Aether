// Aether - frame-born panels (the Caelestia screen border).
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =========================================================================================================
// FRAME-BORN PANELS - the Caelestia look.
// Studied frame by frame from the caelestia-aw demo: the screen border and the dashboard are ONE surface.
// The drawer does not float in as a card; it is the top border growing downward. Its top edge never has
// corners (it is the border), its bottom corners are round, and where its sides meet the border there are
// CONCAVE fillets, so the border bends into the panel instead of a card sitting on it. It slides down with
// its content clipped at the border (bottom revealed first), and notifications grow out of the border the
// same way. Earlier fillets here were dropped because the drawer was a different material from the surround,
// so the join showed as a nub. This paints panels with the surround's own material - the same frosted
// wallpaper, cover-fitted to the same monitor, with the same tints - so there is nothing to see at the join.
static bool FrameMaterialLive(int mi){ return mi>=0 && mi<16 && g_monLive[mi]; }
// Textured triangle fan in the current draw list, UVs taken from each vertex's screen position.
static void FrameFan(ImDrawList* dl,ID3D11ShaderResourceView* tex,ImVec2 uv0,ImVec2 uv1,ImVec2 m0,ImVec2 m1,
                     const std::vector<ImVec2>& pts,ImU32 col){
    if(pts.size()<3) return;
    auto uvOf=[&](ImVec2 p){ float fx=(p.x-m0.x)/std::max(1.0f,m1.x-m0.x), fy=(p.y-m0.y)/std::max(1.0f,m1.y-m0.y);
                             return ImVec2(uv0.x+fx*(uv1.x-uv0.x), uv0.y+fy*(uv1.y-uv0.y)); };
    dl->PushTextureID((ImTextureID)tex);
    int n=(int)pts.size();
    dl->PrimReserve((n-2)*3,n);
    ImDrawIdx base=(ImDrawIdx)dl->_VtxCurrentIdx;
    for(int i=0;i<n;i++) dl->PrimWriteVtx(pts[i],uvOf(pts[i]),col);
    for(int i=1;i<n-1;i++){ dl->PrimWriteIdx(base); dl->PrimWriteIdx((ImDrawIdx)(base+i)); dl->PrimWriteIdx((ImDrawIdx)(base+i+1)); }
    dl->PopTextureID();
}
// ONE SHAPE, ONE FILL. Panels used to be a body polygon plus separate shoulder polygons. Each piece got its own
// anti-aliased fringe, and where two pieces met the fringes overlapped: the translucent tints stacked into a
// 1px dark seam (measured 20,22,26 against the panel's 56,62,70) - the corners read as stuck-on bits, not part
// of the panel. Now a panel is a single outline (body, round corners and concave shoulders together),
// ear-clipped into triangles, with the AA fringe only along the outline. Edges flagged `hard` get no fringe:
// they are where the shape meets a neighbour drawn separately (the frame, a merged panel), so the two fills
// abut exactly instead of each fading out over the other.
static void EarClip(const std::vector<ImVec2>& p,std::vector<int>& tris){
    tris.clear(); int n=(int)p.size(); if(n<3) return;
    double A=0; for(int i=0;i<n;i++){ const ImVec2& a=p[i],&b=p[(i+1)%n]; A+=(double)a.x*b.y-(double)b.x*a.y; }
    const float sg = A>=0? 1.0f : -1.0f;
    std::vector<int> idx(n); for(int i=0;i<n;i++) idx[i]=i;
    auto cross=[](ImVec2 a,ImVec2 b,ImVec2 c){ return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); };
    int guard=0;
    while((int)idx.size()>3 && guard++<4096){
        int m=(int)idx.size(); bool cut=false;
        for(int k=0;k<m;k++){
            int ia=idx[(k+m-1)%m], ib=idx[k], ic=idx[(k+1)%m];
            ImVec2 a=p[ia],b=p[ib],c=p[ic];
            float cr=cross(a,b,c)*sg;
            if(cr<=1e-6f) continue;                                   // reflex (or flat): not an ear
            bool inside=false;
            for(int q=0;q<m && !inside;q++){ int iq=idx[q]; if(iq==ia||iq==ib||iq==ic) continue;
                ImVec2 t=p[iq];
                if(cross(a,b,t)*sg>=0 && cross(b,c,t)*sg>=0 && cross(c,a,t)*sg>=0) inside=true; }
            if(inside) continue;
            tris.push_back(ia); tris.push_back(ib); tris.push_back(ic);
            idx.erase(idx.begin()+k); cut=true; break;
        }
        if(!cut){                                                    // numerically stuck: fan the rest
            for(int k=1;k+1<(int)idx.size();k++){ tris.push_back(idx[0]); tris.push_back(idx[k]); tris.push_back(idx[k+1]); }
            idx.clear(); break; }
    }
    if(idx.size()==3){ tris.push_back(idx[0]); tris.push_back(idx[1]); tris.push_back(idx[2]); }
}
struct FShape { std::vector<ImVec2> p; std::vector<unsigned char> hard; };   // hard[i]: edge p[i] -> p[i+1]
static void FShapeAdd(FShape& sh,ImVec2 q,bool hardNext=false){
    if(!sh.p.empty()){ ImVec2 l=sh.p.back(); if(fabsf(l.x-q.x)<0.01f && fabsf(l.y-q.y)<0.01f){ if(hardNext) sh.hard.back()=1; return; } }
    sh.p.push_back(q); sh.hard.push_back(hardNext?1:0);
}
static void FShapeFill(ImDrawList* dl,const FShape& sh,const std::vector<int>& tris,ID3D11ShaderResourceView* tex,
                       ImVec2 uv0,ImVec2 uv1,ImVec2 m0,ImVec2 m1,ImU32 col){
    int n=(int)sh.p.size(); if(n<3 || tris.empty()) return;
    auto uvOf=[&](ImVec2 q)->ImVec2{
        if(!tex) return ImGui::GetFontTexUvWhitePixel();
        float fx=(q.x-m0.x)/std::max(1.0f,m1.x-m0.x), fy=(q.y-m0.y)/std::max(1.0f,m1.y-m0.y);
        return ImVec2(uv0.x+fx*(uv1.x-uv0.x), uv0.y+fy*(uv1.y-uv0.y)); };
    if(tex) dl->PushTextureID((ImTextureID)tex);
    // outward normals per vertex (averaged, miter-clamped), from the winding
    double A=0; for(int i=0;i<n;i++){ const ImVec2& a=sh.p[i],&b=sh.p[(i+1)%n]; A+=(double)a.x*b.y-(double)b.x*a.y; }
    const float sg = A>=0? 1.0f : -1.0f;
    std::vector<ImVec2> en(n), vn(n);
    for(int i=0;i<n;i++){ ImVec2 d(sh.p[(i+1)%n].x-sh.p[i].x, sh.p[(i+1)%n].y-sh.p[i].y); float L=sqrtf(d.x*d.x+d.y*d.y); if(L<1e-4f){ en[i]=ImVec2(0,0); continue; }
        en[i]=ImVec2(d.y/L*sg,-d.x/L*sg); }
    for(int i=0;i<n;i++){ ImVec2 a=en[(i+n-1)%n], b=en[i]; ImVec2 m=ImVec2(a.x+b.x,a.y+b.y);
        float L2=m.x*m.x+m.y*m.y; if(L2<1e-6f){ vn[i]=b; continue; }
        vn[i]=ImVec2(m.x/(L2*0.5f), m.y/(L2*0.5f));                 // miter: unit distance from both edges
        float vl=sqrtf(vn[i].x*vn[i].x+vn[i].y*vn[i].y); if(vl>2.5f){ vn[i].x*=2.5f/vl; vn[i].y*=2.5f/vl; } }
    int soft=0; for(int i=0;i<n;i++) if(!sh.hard[i]) soft++;
    const float AA=1.0f;
    ImU32 col0=col&~IM_COL32_A_MASK;
    dl->PrimReserve((int)tris.size()+soft*6, n+soft*4);
    ImDrawIdx base=(ImDrawIdx)dl->_VtxCurrentIdx;
    for(int i=0;i<n;i++) dl->PrimWriteVtx(sh.p[i],uvOf(sh.p[i]),col);
    for(int t:tris) dl->PrimWriteIdx((ImDrawIdx)(base+t));
    for(int i=0;i<n;i++){
        if(sh.hard[i]) continue;
        int j=(i+1)%n;
        // fringe quad: the edge itself at full alpha, pushed out by AA px at zero alpha. The outer
        // corners use the shared vertex normals so neighbouring fringe quads meet without gaps.
        ImVec2 oi(sh.p[i].x+vn[i].x*AA, sh.p[i].y+vn[i].y*AA), oj(sh.p[j].x+vn[j].x*AA, sh.p[j].y+vn[j].y*AA);
        ImDrawIdx q=(ImDrawIdx)dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(sh.p[i],uvOf(sh.p[i]),col); dl->PrimWriteVtx(sh.p[j],uvOf(sh.p[j]),col);
        dl->PrimWriteVtx(oj,uvOf(oj),col0);           dl->PrimWriteVtx(oi,uvOf(oi),col0);
        dl->PrimWriteIdx(q); dl->PrimWriteIdx((ImDrawIdx)(q+1)); dl->PrimWriteIdx((ImDrawIdx)(q+2));
        dl->PrimWriteIdx(q); dl->PrimWriteIdx((ImDrawIdx)(q+2)); dl->PrimWriteIdx((ImDrawIdx)(q+3));
    }
    if(tex) dl->PopTextureID();
}
// Paint a shape with the surround material of monitor `mon` whose rect in this draw list's coordinates is m0..m1.
static void FrameMaterialShape(ImDrawList* dl,int mon,ImVec2 m0,ImVec2 m1,const FShape& sh,float alpha){
    if(sh.p.size()<3 || alpha<=0.002f) return;
    std::vector<int> tris; EarClip(sh.p,tris); if(tris.empty()) return;
    ImVec2 z(0,0);
    auto solid=[&](ImU32 c){ int a=(int)(((c>>IM_COL32_A_SHIFT)&0xFF)*alpha); if(a<=0) return;
        FShapeFill(dl,sh,tris,nullptr,z,z,m0,m1,WithA(c,a)); };
    if(FrameMaterialLive(mon)){                          // over a live wallpaper the frame is tinted glass
        ImU32 frame = g_frameTint? g_frameTint : (g_darkUI? IM_COL32(9,10,13,255) : IM_COL32(226,230,234,255));
        // panels sit over app windows, not just the wallpaper: the frame's see-through opacity let the windows
        // behind show through the dashboard. Keep them nearly solid.
        solid(WithA(frame,(int)(std::max(0.94f,std::clamp(g_liveFrameAlpha,0.0f,1.0f))*255))); return; }
    solid(g_frameTint? g_frameTint : COL_DESKBG);
    if(g_deskGlow && g_bubble && g_deskFrost){
        ImVec2 uv0,uv1; CoverUV(g_deskFrostW,g_deskFrostH,m1.x-m0.x,m1.y-m0.y,uv0,uv1);
        FShapeFill(dl,sh,tris,g_deskFrost,uv0,uv1,m0,m1,IM_COL32(255,255,255,(int)(255*alpha)));
        solid(g_darkUI?IM_COL32(12,14,18,118):IM_COL32(238,243,241,120));
        solid(WithA(COL_DESKBG,64));
        solid(WithA(COL_GOLD,g_darkUI?16:10));
    }
}
static void FrameMaterialPoly(ImDrawList* dl,int mon,ImVec2 m0,ImVec2 m1,const std::vector<ImVec2>& pts,float alpha){
    FShape sh; for(auto& q:pts) FShapeAdd(sh,q); FrameMaterialShape(dl,mon,m0,m1,sh,alpha);
}
// A panel born from ANY border edge. It is built in edge space - u runs along the edge, v is the distance
// in from the screen edge - and mapped onto the edge, so the same shape (square where it meets the edge,
// round far corners, concave shoulders into the border) serves the dashboard, launcher, quick settings,
// OSD and session menu. `depth` is how far the far side is from the SCREEN edge; `border` is the frame's
// thickness on that edge.
static int FrameMon(){ return g_mons.empty()? 0 : std::clamp(g_actMon,0,(int)g_mons.size()-1); }
static float FrameInset(int edge){
    int il=0,it=0,ir=0,ib=0; BubbleInsetsFor(FrameMon(),il,it,ir,ib);
    int v = edge==EDGE_TOP? it : edge==EDGE_BOTTOM? ib : edge==EDGE_LEFT? il : ir;
    return (float)v/g_uiScale;
}
static bool FrameBornOn(){ return g_frameBorn && g_bubble; }
// ---- motion resolver ------------------------------------------------------------------------------
struct MotionSpec { int curve=Cael::DEFAULT_SPATIAL; int ms=500; float b[4]={0.38f,1.21f,0.22f,1.0f}; };
static bool ParseBezier(const std::string& txt,float out[4]){
    std::string t=txt; for(auto& c:t) if(c==','||c==';'||c=='('||c==')') c=' ';
    float v[4]; if(sscanf(t.c_str(),"%f %f %f %f",&v[0],&v[1],&v[2],&v[3])!=4) return false;
    v[0]=std::clamp(v[0],0.0f,1.0f); v[2]=std::clamp(v[2],0.0f,1.0f);     // x must stay in 0..1 for a function
    v[1]=std::clamp(v[1],-1.0f,3.0f); v[3]=std::clamp(v[3],-1.0f,3.0f);
    for(int i=0;i<4;i++) out[i]=v[i]; return true;
}
static MotionSpec MotionStyleSpec(int style){
    MotionSpec m;
    switch(style){
    case MSTY_EXPRESSIVE: m.curve=Cael::DEFAULT_SPATIAL;  m.ms=500; break;
    case MSTY_EXP_FAST:   m.curve=Cael::FAST_SPATIAL;     m.ms=350; break;
    case MSTY_EXP_SLOW:   m.curve=Cael::SLOW_SPATIAL;     m.ms=650; break;
    case MSTY_EMPHASIZED: m.curve=Cael::EMPHASIZED;       m.ms=400; break;
    case MSTY_SMOOTH:     m.curve=Cael::EMPHASIZED_DECEL; m.ms=400; break;
    case MSTY_STANDARD:   m.curve=Cael::STANDARD;         m.ms=300; break;
    case MSTY_LINEAR:     m.curve=Cael::LINEAR;           m.ms=250; break;
    case MSTY_BOUNCE:     m.curve=Cael::BOUNCE;           m.ms=700; break;
    case MSTY_INSTANT:    m.curve=Cael::LINEAR;           m.ms=1;   break;
    case MSTY_STRIVE:     m.curve=Cael::STRIVE;           m.ms=420; break;
    case MSTY_CUSTOM: { m.curve=Cael::CUSTOM; m.ms=500;
        static std::string last; static float cb[4]={0.38f,1.21f,0.22f,1.0f};
        if(last!=g_motionCustom){ last=g_motionCustom; float t[4]; if(ParseBezier(last,t)) for(int i=0;i<4;i++) cb[i]=t[i]; }
        for(int i=0;i<4;i++) m.b[i]=cb[i]; } break;
    }
    return m;
}
static MotionSpec MotionResolve(int panel,bool opening){
    // A keybind's graph beats the panel's, for the motion that keybind set off.
    int keyStyle=0, keyMs=0;
    if(g_mgKey>=0 && g_mgKey<HK_COUNT && HK_PANEL[g_mgKey]==panel && panel>=0 &&
       GetTickCount64()-g_mgKeyAt < 2000){ keyStyle=g_hkStyle[g_mgKey]; keyMs=g_hkMs[g_mgKey]; }
    if(keyStyle>0){
        MotionSpec km=MotionStyleSpec(std::clamp(keyStyle-1,0,MSTY_N-1));
        if(keyMs>0 && keyStyle-1!=MSTY_INSTANT) km.ms=keyMs;
        return km;
    }
    bool own = panel>=0 && panel<MP_COUNT && g_mpStyle[panel]>0;
    int style = own? g_mpStyle[panel]-1 : g_motionStyle;
    if(!opening && !own && g_motionCloseStyle>0) style=g_motionCloseStyle-1;
    MotionSpec m=MotionStyleSpec(std::clamp(style,0,MSTY_N-1));
    int ms = keyMs>0 ? keyMs
           : (panel>=0 && panel<MP_COUNT && g_mpMs[panel]>0)? g_mpMs[panel] : g_motionMs;
    if(!opening && !(panel>=0 && panel<MP_COUNT && g_mpMs[panel]>0) && g_motionCloseMs>0) ms=g_motionCloseMs;
    if(ms>0 && style!=MSTY_INSTANT) m.ms=ms;
    return m;
}
// Animate `id` toward `target` with the motion the user picked for `panel`. Opening = heading up.
static float MotionAnim(int panel,int id,float target){
    bool opening = !Cael::known(id) || target >= Cael::current(id);
    MotionSpec m=MotionResolve(panel,opening);
    float from=Cael::current(id);
    float v=Cael::animX(id,target,m.ms,m.curve,m.b[0],m.b[1],m.b[2],m.b[3]);
    if(!g_motionOvershoot){ auto it=Cael::g_st.find(id);
        if(it!=Cael::g_st.end()){ float lo=std::min(it->second.from,it->second.to), hi=std::max(it->second.from,it->second.to);
            v=std::clamp(v,lo,hi); it->second.cur=v; } }
    (void)from;
    return std::max(0.0f,v);
}
static float g_launReveal=0.0f, g_setReveal=0.0f, g_osdReveal=0.0f;
// Published every frame so neighbouring panels can MERGE into one shape (monitor-logical coordinates).
static bool  g_nfPanelOn=false; static float g_nfPanelL=0, g_nfPanelB=0, g_nfPanelVis=0;
static bool  g_qsPanelOn=false; static float g_qsPanelL=0, g_qsPanelT=0;
static bool  g_dashNeckOn=false; static RECT g_dashNeckRect={0,0,0,0};   // drawer-window logical px
static float g_dashNeckDepth=0;   // how far down the bridge reaches (the notification panel squares its corner there)
// joinA0 / joinA1: a neighbour is attached along that SIDE of the panel, from the screen edge down to this
// depth (edge space). That side then has no shoulder, no fringe where the neighbour covers it, and a square
// corner when the neighbour reaches the far side. 0 = free side.
static void FrameEdgePanel(ImDrawList* dl,int mon,ImVec2 m0,ImVec2 m1,int edge,float a0,float a1,float depth,
                           float border,float rnd,float alpha,float joinA0=0.0f,float joinA1=0.0f){
    if(a1<=a0+1 || depth<=border+0.5f || alpha<=0.002f) return;
    auto map=[&](float u,float v)->ImVec2{
        switch(edge){ case EDGE_TOP: return V(u,m0.y+v); case EDGE_BOTTOM: return V(u,m1.y-v);
                      case EDGE_LEFT: return V(m0.x+v,u); default: return V(m1.x-v,u); } };
    float visH=depth-border;
    float f=std::min(rnd,std::max(0.0f,visH)*0.5f); if(f<0.75f) f=0.0f;
    float r=std::min({rnd,(a1-a0)*0.5f,std::max(0.0f,visH-f)});
    bool sh0 = f>0.0f && joinA0<=0.0f, sh1 = f>0.0f && joinA1<=0.0f;
    float r0 = (joinA0>=depth-r-0.5f)? 0.0f : r, r1 = (joinA1>=depth-r-0.5f)? 0.0f : r;
    const int NA=12;
    FShape s;
    auto add=[&](float u,float v,bool hardNext=false){ FShapeAdd(s,map(u,v),hardNext); };
    // --- down the a0 side ---
    if(sh0){
        add(a0-f,-2,true); add(a0-f,border,false);                               // over the frame: hard
        for(int i=1;i<=NA;i++){ float t=1.5707963f*(float)i/NA; add(a0-f+f*sinf(t), border+f-f*cosf(t)); }
    } else if(joinA0>0.0f){
        add(a0,-2,true); if(joinA0<depth-r0-0.5f) add(a0,joinA0,false);
    } else add(a0,-2,false);
    if(r0>0.0f){ add(a0,depth-r0);
        for(int i=1;i<=NA;i++){ float t=3.1415927f-1.5707963f*(float)i/NA; add(a0+r0+cosf(t)*r0, depth-r0+sinf(t)*r0); } }
    else add(a0,depth);
    // --- along the far side and up the a1 side ---
    if(r1>0.0f){ add(a1-r1,depth);
        for(int i=1;i<=NA;i++){ float t=1.5707963f-1.5707963f*(float)i/NA; add(a1-r1+cosf(t)*r1, depth-r1+sinf(t)*r1); } }
    else add(a1,depth, joinA1>0.0f);
    if(sh1){
        add(a1,border+f);
        for(int i=1;i<=NA;i++){ float t=1.5707963f-1.5707963f*(float)i/NA; add(a1+f-f*sinf(t), border+f-f*cosf(t), i==NA); }
        add(a1+f,-2,true);
    } else if(joinA1>0.0f){
        if(joinA1<depth-r1-0.5f) add(a1,joinA1,true);
        if(!s.hard.empty()) s.hard.back()=1;
        add(a1,-2,true);
    } else add(a1,-2,true);
    if(!s.hard.empty()) s.hard.back()=1;                                         // closing edge is off-screen
    // mirrored mappings reverse the winding - EarClip and the fringe normals take either
    FrameMaterialShape(dl,mon,m0,m1,s,alpha);
}
// Rounded-bottom rectangle as a polygon (top edge square), for panels born from the top edge.
static void FrameBornPanel(ImDrawList* dl,int mon,ImVec2 m0,ImVec2 m1,float x0,float yTop,float x1,float yBot,
                           float rnd,float border,float alpha){
    (void)yTop; FrameEdgePanel(dl,mon,m0,m1,EDGE_TOP,x0,x1,yBot,border,rnd,alpha); return;
    if(yBot<=yTop+0.5f || x1<=x0+1) return;
    float visH=yBot-border;                                  // how far it has come out past the border
    float r=std::min({rnd, (x1-x0)*0.5f, std::max(0.0f,yBot-yTop)*0.5f});
    const int N=10;
    std::vector<ImVec2> body;
    body.push_back(V(x0,yTop)); body.push_back(V(x1,yTop));
    for(int i=0;i<=N;i++){ float a=0.0f+1.5707963f*(float)i/N; body.push_back(V(x1-r+cosf(a)*r, yBot-r+sinf(a)*r)); }
    for(int i=0;i<=N;i++){ float a=1.5707963f+1.5707963f*(float)i/N; body.push_back(V(x0+r+cosf(a)*r, yBot-r+sinf(a)*r)); }
    FrameMaterialPoly(dl,mon,m0,m1,body,alpha);
    // concave shoulders where the sides meet the border; they grow with the panel so a closed panel has none
    float f=std::min(rnd, std::max(0.0f,visH)*0.5f);
    if(f>0.75f){
        auto shoulder=[&](float cx,float sx){            // cx: the panel side; sx: +1 right side, -1 left side
            std::vector<ImVec2> p; p.push_back(V(cx,border));                        // corner on the panel side
            for(int i=0;i<=N;i++){ float a=(float)i/N*1.5707963f;
                p.push_back(V(cx+sx*(f-sinf(a)*f)*1.0f, border+f-cosf(a)*f)); }    // arc from (cx+sx*f,border) to (cx,border+f)
            FrameMaterialPoly(dl,mon,m0,m1,p,alpha); };
        shoulder(x0,-1.0f); shoulder(x1,+1.0f);
    }
}
// A panel born from the TOP-RIGHT corner of the border (Caelestia's notifications): its top is the top border
// and its right side is the right border, so only the bottom-left corner is round. Concave shoulders join its
// left side to the top border and its bottom edge to the right border.
static void FrameCornerPanel(ImDrawList* dl,int mon,ImVec2 m0,ImVec2 m1,float x0,float xR,float yBot,
                             float borderT,float borderR,float rnd,float alpha,bool mergeBelow=false,float joinLeft=0.0f){
    if(yBot<=borderT+0.5f || xR<=x0+1) return;
    float visH=yBot-borderT;
    float f=std::min(rnd,visH*0.5f); if(f<0.75f) f=0.0f;
    float r=std::min({rnd,(xR-x0)*0.5f,std::max(0.0f,visH-f)});
    if(mergeBelow || joinLeft>=yBot-r-0.5f) r=0.0f;
    bool shL = f>0.0f && joinLeft<=0.0f, shB = f>0.0f && !mergeBelow;
    const int NA=12;
    FShape s;
    auto add=[&](float x,float y,bool hardNext=false){ FShapeAdd(s,V(x,y),hardNext); };
    if(shL){
        add(x0-f,-2,true); add(x0-f,borderT);
        for(int i=1;i<=NA;i++){ float t=1.5707963f*(float)i/NA; add(x0-f+f*sinf(t), borderT+f-f*cosf(t)); }
    } else if(joinLeft>0.0f){
        add(x0,-2,true); if(joinLeft<yBot-r-0.5f) add(x0,joinLeft);
    } else add(x0,-2);
    if(r>0.0f){ add(x0,yBot-r);
        for(int i=1;i<=NA;i++){ float t=3.1415927f-1.5707963f*(float)i/NA; add(x0+r+cosf(t)*r, yBot-r+sinf(t)*r); } }
    else add(x0,yBot,mergeBelow);
    float xi=xR-borderR;
    if(shB){
        add(xi-f,yBot);
        for(int i=1;i<=NA;i++){ float t=1.5707963f*(float)i/NA; add(xi-f+f*sinf(t), yBot+f-f*cosf(t), i==NA); }
        add(xR+2,yBot+f,true);
    } else { if(!s.hard.empty() && mergeBelow) s.hard.back()=1; add(xR+2,yBot,true); }
    add(xR+2,-2,true);
    s.hard.back()=1;
    FrameMaterialShape(dl,mon,m0,m1,s,alpha);
}
static void DrawUI() {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x, H=io.DisplaySize.y;
    if (g_reveal < 0.002f){ g_dashNeckOn=false; g_dashNeckRect=RECT{0,0,0,0}; g_dashNeckDepth=0; return; }  // fully hidden -> draw nothing

    const Panel& P=g_pn[PN_DRAWER];
    // spanOverride re-anchors the rect at the morphed width, so a centred drawer stays centred
    // while it grows instead of growing off one side.
    PRect r=PanelRect(P,W,H, g_drawerMorphW>8.0f? g_drawerMorphW : 0.0f);
    const float DW=r.w, DH=(g_drawerMorphH>8.0f? g_drawerMorphH : r.h), TABH=DRAWER_TABH;
    float dx=r.x;
    float slide=g_reveal;   // already curved by Cael::anim; easing again would flatten it
    // The slide distance has to cover the height actually being DRAWN, not the configured one. The
    // drawer morphs its height to fit the active tab (g_drawerMorphH), so on a tab whose content is
    // taller than bar.size the panel was only pushed up by P.size and its bottom strip stayed on
    // screen at slide 0 - it popped in instead of sliding in. `extra` exists for exactly this.
    PanelSlide(P,r,slide,std::max(0.0f,DH-r.h));
    float dy=r.y;                                         // flush with its edge when fully open
    // born from the border: the content sits below the border, not under it (the tab row was clipped)
    if(g_frameBorn && g_bubble && P.edge==EDGE_TOP){
        int mi0=std::clamp(g_actMon,0,std::max(0,(int)g_mons.size()-1)); int l0=0,t0=0,r0=0,b0=0;
        BubbleInsetsFor(mi0,l0,t0,r0,b0); dy+=(float)t0/g_uiScale; }

    // drawer panel — configurable background (acrylic frost / image-gif / solid)
    ImVec2 pmin=V(dx,dy), pmax=V(dx+DW,dy+DH);
    bool frameBorn = g_frameBorn && g_bubble && P.edge==EDGE_TOP;
    if(frameBorn){
        int mi=std::clamp(g_actMon,0,(int)g_mons.size()-1);
        int il=0,it=0,ir=0,ib=0; BubbleInsetsFor(mi,il,it,ir,ib);
        float border=(float)it/g_uiScale;
        // the panel's top is the screen edge; it reaches down to wherever the slide has brought its bottom
        // MERGE with the notification panel: the border between them thickens into one surface (a neck with
        // concave corners down into both panels), so the two read as one shape growing out of the frame.
        bool neck = g_nfPanelOn && g_nfPanelL > dx+DW+2.0f;
        g_dashNeckOn = neck; if(!neck){ g_dashNeckRect=RECT{0,0,0,0}; g_dashNeckDepth=0; }
        { float jn=0.0f;
          if(neck){ float grow=std::min(std::clamp((dy+DH-border)/std::max(1.0f,DH),0.0f,1.0f), std::clamp(g_nfPanelVis,0.0f,1.0f));
                    grow=Cael::eval(Cael::EMPHASIZED_DECEL,grow);
                    float D=std::min(dy+DH,g_nfPanelB), hN=std::max(border+0.5f, border+(D-border)*grow);
                    float fb=std::min({g_panelRound, std::fabs(dy+DH-g_nfPanelB)*0.9f, (g_nfPanelL-dx-DW)*0.5f, std::max(0.0f,(D-border)*grow)});
                    jn = hN + ((fb>0.75f && dy+DH>g_nfPanelB)? fb : 0.0f); }      // the bridge's curve runs down OUR side
          FrameEdgePanel(dl,mi,V(0,0),V(W,H),EDGE_TOP,dx,dx+DW,dy+DH,border,g_panelRound,1.0f,0.0f,jn); }
        if(neck){
            // a full BRIDGE: the gap between the two panels fills down to the shorter one's bottom, and a
            // concave curve carries that edge into the taller one - one continuous shape out of the border
            float x0n=dx+DW, x1n=g_nfPanelL;
            float dashB=dy+DH, nfB=g_nfPanelB;
            float grow=std::min(std::clamp((dashB-border)/std::max(1.0f,DH),0.0f,1.0f), std::clamp(g_nfPanelVis,0.0f,1.0f));
            grow=Cael::eval(Cael::EMPHASIZED_DECEL,grow);
            float D=std::min(dashB,nfB);
            float hN=std::max(border+0.5f, border+(D-border)*grow);
            float f=std::min({g_panelRound, std::fabs(dashB-nfB)*0.9f, (x1n-x0n)*0.5f, std::max(0.0f,(D-border)*grow)});
            g_dashNeckRect=RECT{(LONG)(x0n-4),0,(LONG)(x1n+4),(LONG)(hN+std::max(0.0f,f)+4)};
            // one outline for the bridge; its sides are where the two panels are, so they are hard edges
            FShape b; const int NA=12;
            FShapeAdd(b,V(x0n,-2),false); FShapeAdd(b,V(x1n,-2),true);
            if(f>0.75f && dashB<=nfB){                          // notifications deeper: curve down their side
                FShapeAdd(b,V(x1n,hN+f),false);
                for(int i=1;i<=NA;i++){ float t=1.5707963f*(float)i/NA; FShapeAdd(b,V(x1n-f+f*cosf(t), hN+f-f*sinf(t))); }
                FShapeAdd(b,V(x0n,hN),true);
            } else if(f>0.75f){                                  // dashboard deeper: curve down its side
                FShapeAdd(b,V(x1n,hN),false);
                FShapeAdd(b,V(x0n+f,hN));
                for(int i=1;i<=NA;i++){ float t=1.5707963f*(float)i/NA; FShapeAdd(b,V(x0n+f-f*sinf(t), hN+f-f*cosf(t)), i==NA); }
            } else { FShapeAdd(b,V(x1n,hN),false); FShapeAdd(b,V(x0n,hN),true); }
            b.hard.back()=1;
            FrameMaterialShape(dl,mi,V(0,0),V(W,H),b,1.0f);
            g_dashNeckDepth = hN + ((f>0.75f && dashB<=nfB)? f : 0.0f);   // includes the curve down their side
        }
        // the content slides with the panel but never shows above the border it comes out of
        dl->PushClipRect(V(dx,border),V(dx+DW,std::max(border,dy+DH)),true);
    } else
    GlassPanel(dl,pmin,pmax,g_panelRound,PanelCorners(P),g_acrylicTex,1.0f);

    // ---- tab bar ----------------------------------------------------------------------------------------------
    // Caelestia's Tabs.qml: every tab gets an equal share of the width, icon over label, the current one in the
    // primary colour, a 3px rounded indicator the width of the tab's content that SLIDES (x and width both
    // animated) to the current tab, a state layer on hover, and a thin outline-variant separator under it all.
    // Everything about it is a setting: style (caelestia / pills / segmented / minimal), indicator, icons,
    // labels, separator, fill vs fit, wheel-to-switch - and each tab's icon is any Material Symbols name.
    int nvis=(int)g_tabs.size(); if(nvis<1) nvis=1;
    if(g_tab>=nvis) g_tab=0;
    bool edit=g_editTab;
    int nchips = nvis + (edit?1:0);
    const float TPADX=18.0f;
    const bool showIcons = g_tabIcons || (!g_tabLabels);
    const bool showLabels= g_tabLabels && g_tabStyle!=3? true : (g_tabStyle==3);
    auto tabContentW=[&](int k)->float{
        if(k>=nvis) return 34.0f;
        const DashTab& t=g_tabs[k];
        float lw = showLabels? TextW(g_fSml,15,t.name.c_str()) : 0.0f;
        if(g_tabStyle==1||g_tabStyle==2) return (showIcons? 24.0f : 0.0f) + (showIcons&&showLabels? 8.0f : 0.0f) + lw;
        return std::max(showIcons? 24.0f : 0.0f, lw);
    };
    float slotW[64]; float totalW=0;
    { float avail=DW-TPADX*2;
      for(int k=0;k<nchips && k<64;k++){
          float natural = tabContentW(k) + (g_tabStyle==0||g_tabStyle==3? 36.0f : 40.0f);
          slotW[k]=natural; totalW+=natural; }
      if(g_tabFill || totalW>avail){ for(int k=0;k<nchips && k<64;k++) slotW[k]=avail/nchips; totalW=avail; } }
    float tx=dx+(DW-totalW)*0.5f;
    auto slotX=[&](int k)->float{ float x=tx; for(int i=0;i<k && i<64;i++) x+=slotW[i]; return x; };
    const float barTop=dy+2, barBot=dy+TABH-2;
    const float barMid=(barTop+barBot)*0.5f;
    bool overBar = io.MousePos.x>dx && io.MousePos.x<dx+DW && io.MousePos.y>barTop && io.MousePos.y<barBot;
    if(g_tabWheel && overBar && io.MouseWheel!=0 && !edit){
        int nt=std::clamp(g_tab+(io.MouseWheel<0? 1 : -1),0,nvis-1); g_tab=nt; }
    int cur=TabSlot();
    // the moving mark: its centre and width both run on the dashboard's motion
    float indCX = MotionAnim(MP_DASHBOARD,840001, slotX(cur)+slotW[std::min(cur,63)]*0.5f);
    float indW  = MotionAnim(MP_DASHBOARD,840002, tabContentW(cur));
    // segmented: one track holding every tab
    if(g_tabStyle==2){
        float h=34, y0=barMid-h*0.5f;
        dl->AddRectFilled(V(tx,y0),V(tx+totalW,y0+h),WithA(COL_INK2,g_darkUI?30:24),h*0.5f);
        dl->AddRect(V(tx,y0),V(tx+totalW,y0+h),WithA(COL_INK2,70),h*0.5f,0,1.0f);
        float sw=slotW[std::min(cur,63)];
        float selX=MotionAnim(MP_DASHBOARD,840003,slotX(cur));
        dl->AddRectFilled(V(selX+3,y0+3),V(selX+sw-3,y0+h-3),AccA(g_darkUI?80:70),(h-6)*0.5f);
        for(int k=1;k<nchips;k++){ float lx=slotX(k); if(fabsf(lx-selX)<2||fabsf(lx-(selX+sw))<2) continue;
            dl->AddLine(V(lx,y0+8),V(lx,y0+h-8),WithA(COL_INK2,50),1.0f); }
    }
    if(g_tabStyle==0 || g_tabStyle==3){
        if(g_tabIndicator==1){                                   // pill behind the current tab's content
            float ph2=TABH-12;
            dl->AddRectFilled(V(indCX-indW*0.5f-16,barMid-ph2*0.5f),V(indCX+indW*0.5f+16,barMid+ph2*0.5f),AccA(g_darkUI?46:40),ph2*0.5f);
        }
    }
    for(int k=0;k<nvis;k++){
        DashTab& t=g_tabs[k];
        float sx=slotX(k), sw=slotW[std::min(k,63)], cx=sx+sw*0.5f;
        bool sel=(k==cur);
        bool thov=io.MousePos.x>sx&&io.MousePos.x<sx+sw&&io.MousePos.y>barTop&&io.MousePos.y<barBot;
        float ha=HoverAnim(4000+k,thov);
        float sa=Cael::anim(4100+k, sel? 1.0f : 0.0f, Cael::DUR_DEFAULT_EFFECTS, Cael::DEFAULT_EFFECTS);   // colour cross-fade
        ImU32 fg = Mix(COL_INK2, COL_GOLD, sa);
        std::string iconN=TabIconName(t);
        const char* lbl=t.name.c_str();
        float lw=TextW(g_fSml,15,lbl);
        switch(g_tabStyle){
        case 1: {   // pills
            float cw=tabContentW(k)+28, h=34, x0=cx-cw*0.5f, y0=barMid-h*0.5f;
            ImU32 bg = sel? AccA(225) : WithA(COL_INK2,(int)(22+ha*36));
            dl->AddRectFilled(V(x0,y0),V(x0+cw,y0+h),bg,h*0.5f);
            ImU32 c2 = sel? M3OnPrimary() : COL_INK;
            float ix=x0+14;
            if(showIcons){ if(!MsIcon(dl,iconN,V(ix+12,barMid),20,c2)) TabIcon(dl,t.icon,V(ix+12,barMid),c2); ix+=32; }
            if(showLabels) TextAt(dl,g_fSml,15,V(ix,barMid-10),c2,lbl);
        } break;
        case 2: {   // segmented
            ImU32 c2 = sel? COL_INK : COL_INK2;
            float cw=tabContentW(k), ix=cx-cw*0.5f;
            if(ha>0.01f && !sel) dl->AddRectFilled(V(sx+4,barMid-14),V(sx+sw-4,barMid+14),WithA(COL_INK,(int)(ha*16)),14);
            if(showIcons){ if(!MsIcon(dl,sel? std::string("check") : iconN,V(ix+12,barMid),19,c2)) TabIcon(dl,t.icon,V(ix+12,barMid),c2); ix+=32; }
            if(showLabels) TextAt(dl,g_fSml,15,V(ix,barMid-10),c2,lbl);
        } break;
        case 3: {   // minimal: labels only
            if(ha>0.01f) dl->AddRectFilled(V(cx-lw*0.5f-14,barMid-15),V(cx+lw*0.5f+14,barMid+15),WithA(COL_INK,(int)(ha*14)),15);
            ImFont* f= sel? g_fMed : g_fSml;
            float w2=TextW(f,15,lbl);
            TextAt(dl,f,15,V(cx-w2*0.5f,barMid-11),fg,lbl);
        } break;
        default: {  // caelestia
            // state layer: full slot width, a little taller than the content, primary-tinted when current
            if(ha>0.01f) dl->AddRectFilled(V(sx+4,barTop+1),V(sx+sw-4,barBot-7),
                                           WithA(sel? COL_GOLD : COL_INK,(int)(ha*(sel? 26 : 18))),12);
            float iconY = showLabels? barTop+15 : barMid-3;
            if(showIcons){ if(!MsIcon(dl,iconN,V(cx,iconY),22,fg)) TabIcon(dl,t.icon,V(cx,iconY),fg); }
            if(showLabels) TextAt(dl,g_fSml,15,V(cx-lw*0.5f, showIcons? barTop+26 : barMid-12),fg,lbl);
        } break;
        }
        if(io.MouseClicked[0] && thov){ g_tab=k; }
    }
    if(edit){  // the "new tab" chip
        float sx=slotX(nvis), sw=slotW[std::min(nvis,63)], cx=sx+sw*0.5f;
        bool thov=io.MousePos.x>sx&&io.MousePos.x<sx+sw&&io.MousePos.y>barTop&&io.MousePos.y<barBot;
        float ha=HoverAnim(4099,thov);
        if(ha>0.01f) dl->AddRectFilled(V(cx-22,barTop+3),V(cx+22,barBot-7),AccA((int)(ha*24)),11);
        MsIcon(dl,"add",V(cx,barTop+15),22,COL_GOLD);
        TextAt(dl,g_fSml,12,V(cx-TextW(g_fSml,12,"New")/2,barTop+27),COL_INK2,"New");
        if(io.MouseClicked[0]&&thov){ DashTab nt; nt.name="Tab"; nt.icon=0; nt.iconName="widgets"; nt.builtin=false;
            g_tabs.push_back(nt); g_tab=(int)g_tabs.size()-1; g_editSel=-1; SaveConfig(); }
    }
    if(g_tabStyle==0 || g_tabStyle==3){
        if(g_tabIndicator==0){                                   // Caelestia: 3px, rounded, content-wide
            float y0=barBot-5;
            dl->AddRectFilled(V(indCX-indW*0.5f,y0),V(indCX+indW*0.5f,y0+3),COL_GOLD,1.5f);
        } else if(g_tabIndicator==2){
            dl->AddCircleFilled(V(indCX,barBot-4),3.0f,COL_GOLD);
        }
    }
    if(g_tabSeparator && g_tabStyle!=2)
        dl->AddLine(V(dx+TPADX,barBot+0.5f),V(dx+DW-TPADX,barBot+0.5f),WithA(COL_INK2,g_darkUI?48:60),1.0f);

    // ---- content: the active tab's widgets (or a sliding page for the built-in single-page tabs) ----
    float ca=EaseOutCubic(std::clamp(g_drawerContent,0.0f,1.0f));
    float lift=(1.0f-ca)*14.0f;
    ImVec2 corg=V(dx+16, dy+TABH+6+lift); ImVec2 carea=V(DW-32, DH-TABH-22);
    // Default bleed rect: the drawer's inner area. drawPage re-points this per page so a page
    // that is parked off-screen bleeds off-screen too (see below).
    g_pageBleed0=V(dx, dy+TABH-2); g_pageBleed1=V(dx+DW, dy+DH);
    // clip to the WHOLE drawer, not 6px in: a full-bleed backdrop needs the edge, and every widget
    // is still laid out from corg (inset 16), so nothing else can reach it.
    dl->PushClipRect(g_pageBleed0, g_pageBleed1, true);
    // A page is laid out from an origin + area, so a "zoom" is just drawing it at a smaller area
    // anchored on a point, and a "defocus" is a slight scale plus a veil. That gets real depth out
    // of an immediate-mode draw list without needing an offscreen target or a blur shader.
    float fxT = Cael::eval(Cael::EMPHASIZED,std::clamp(g_tabFxT,0.0f,1.0f));
    bool  fxRun = (g_tabFx!=TT_SLIDE) && fxT<0.999f;
    g_wInteractive = !edit;
    ProfileEditorShield(io);
    auto drawPage=[&](int k,float scale,ImVec2 anchor,float veil,float slideOff){
        DashTab& t=g_tabs[k];
        ImVec2 o=corg, ar=carea;
        if(scale!=1.0f){
            ar=V(carea.x*scale, carea.y*scale);
            // keep `anchor` (0..1 of the content box) pinned while the rest scales around it
            o = V(corg.x + (carea.x-ar.x)*anchor.x, corg.y + (carea.y-ar.y)*anchor.y);
        }
        o.x += slideOff;
        // Move the full-bleed rect WITH the page. The Media page paints its album-art wash into
        // this rect and deliberately escapes the content clip to do it, so that it reaches the
        // panel edge. But every tab within one screen of the current one is drawn here to make the
        // slide work, which meant the parked Media page kept painting an 83%-opaque wash over the
        // FIXED drawer rect - i.e. straight on top of whatever tab you were actually looking at.
        // Dashboard is index 0, the only page drawn before Media, so it was the one that got
        // buried: its cards, calendar and clock all sat under a song's cover art and visualiser
        // blobs. Offsetting the rect parks the wash off-screen with the page that owns it.
        g_pageBleed0=V(dx+slideOff, dy+TABH-2);
        g_pageBleed1=V(dx+DW+slideOff, dy+DH);
        for(size_t wi=0; wi<t.widgets.size(); wi++){
            Widget& w=t.widgets[wi];
            ImVec2 wo=V(o.x + w.x*ar.x, o.y + w.y*ar.y);
            ImVec2 ws=V(std::max(20.0f,w.w*ar.x), std::max(20.0f,w.h*ar.y));
            DrawWidget(dl,io,w,wo,ws);
        }
        // an empty tab said nothing at all, and read as a tab that failed to load
        if(t.widgets.empty() && !edit){
            const char* l1="This tab is empty";
            const char* l2=t.builtin? "Edit the dashboard and press \"Restore page\" to bring it back" : "Edit the dashboard and use \"+ Add widget\"";
            ImVec2 c=V(o.x+ar.x*0.5f, o.y+ar.y*0.5f);
            MsIcon(dl,"dashboard_customize",V(c.x,c.y-34),30,WithA(COL_INK2,160));
            TextAt(dl,g_fMed,18,V(c.x-TextW(g_fMed,18,l1)*0.5f,c.y-6),COL_INK,l1);
            TextAt(dl,g_fSml,14,V(c.x-TextW(g_fSml,14,l2)*0.5f,c.y+20),COL_INK2,l2);
        }
        if(veil>0.004f)
            dl->AddRectFilled(V(o.x-10,o.y-10),V(o.x+ar.x+10,o.y+ar.y+10),
                              PanelCol((int)(std::clamp(veil,0.0f,1.0f)*236)),14);
        return o;
    };

    if(!fxRun){
        for (int k=0;k<nvis;k++) {
            float off=(float)k-g_tabX; if (fabsf(off)>1.15f) continue;
            ImVec2 o=drawPage(k,1.0f,ImVec2(0.5f,0.5f),0.0f,off*DW);
            if(edit && k==g_tab && fabsf(off)<0.05f) EditOverlay(dl,io,g_tabs[k],o,carea);
        }
    } else {
        // outgoing first, incoming on top of it
        ImVec2 anch=ImVec2(0.5f,0.5f);
        if(g_tabFx==TT_ZOOM && carea.x>1 && carea.y>1)
            anch=V(std::clamp((g_tabFxOrigin.x-corg.x)/carea.x,0.0f,1.0f),
                   std::clamp((g_tabFxOrigin.y-corg.y)/carea.y,0.0f,1.0f));
        int from=std::clamp(g_tabFxFrom,0,nvis-1), to=std::clamp(g_tab,0,nvis-1);
        if(from!=to){
            float outSc = (g_tabFx==TT_ZOOM)? 1.0f+0.22f*fxT : 1.0f-0.07f*fxT;
            drawPage(from,outSc,anch,fxT*0.96f,0.0f);
        }
        float inSc = (g_tabFx==TT_ZOOM)? 0.80f+0.20f*fxT : 1.06f-0.06f*fxT;
        ImVec2 o=drawPage(to,inSc,anch,1.0f-fxT,0.0f);
        if(edit) EditOverlay(dl,io,g_tabs[to],o,carea);
    }
    g_wInteractive=false;
    if(edit) g_pe.open=false;
    ProfileEditorDraw(dl,io,corg,V(corg.x+carea.x,corg.y+carea.y));
    dl->PopClipRect();
    if(frameBorn) dl->PopClipRect();
    if(edit){ EditToolbar(dl,io,V(dx,dy),V(DW,DH));
        if(ImGui::IsKeyPressed(ImGuiKey_Escape)){ g_editTab=false; g_wPaletteOpen=false; g_editSel=-1; SaveConfig(); } }
    if(g_wPaletteOpen) WidgetPalette(dl,io,V(dx,dy),V(DW,DH),carea);
}
