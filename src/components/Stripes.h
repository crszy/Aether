// src/components/Stripes.h  —  Aether shell
// The "stripes + Loading!!!!!!" cover, shared by the Settings page transition and the live-wallpaper switch.
// pin 0..1 sweeps the stripes IN, pout 0..1 sweeps them back OUT; while both are done the cover holds and the
// Loading text bounces. The stripe colours flow through `pal` over time, so a palette taken from a wallpaper
// makes the whole cover breathe that wallpaper's colours.
#pragma once
static void M3Shape(ImDrawList* dl, ImVec2 c, float R, ImU32 col, int shape, float spin);   // fwd
static ImU32 CwColor(const std::string& s0,ImU32 def);   // fwd (CustomWidgets.h)
static int M3ShapeFromName(const std::string& n0);        // fwd

// a few vivid colours out of an image (BGRA, premultiplied is fine): hue buckets weighted by saturation * value
static int PaletteFromPixels(const std::vector<uint8_t>& px,int w,int h,ImU32 out[6]){
    if(w<=0||h<=0||px.size()<(size_t)w*h*4) return 0;
    const int B=18; double wsum[B]={0}; double rs[B]={0},gs[B]={0},bs[B]={0};
    int step=std::max(1,(int)sqrt((double)w*h/6000.0));
    for(int y=0;y<h;y+=step) for(int x=0;x<w;x+=step){
        const uint8_t* p=&px[((size_t)y*w+x)*4];
        float r=p[2]/255.0f, g=p[1]/255.0f, b=p[0]/255.0f, hh,ss,vv;
        ImGui::ColorConvertRGBtoHSV(r,g,b,hh,ss,vv);
        if(vv<0.18f || ss<0.22f) continue;
        int bin=std::clamp((int)(hh*B),0,B-1); double wt=ss*vv*vv;
        wsum[bin]+=wt; rs[bin]+=r*wt; gs[bin]+=g*wt; bs[bin]+=b*wt;
    }
    int idx[B]; for(int i=0;i<B;i++) idx[i]=i;
    std::sort(idx,idx+B,[&](int a,int b){ return wsum[a]>wsum[b]; });
    int n=0; double top=wsum[idx[0]];
    for(int k=0;k<B && n<6;k++){ int i=idx[k]; if(wsum[i]<=0 || wsum[i]<top*0.04) break;
        // skip a bucket right next to one already taken, so the palette is not four shades of one colour
        bool adjacent=false; for(int q=0;q<k;q++){ int j=idx[q]; if(wsum[j]>=top*0.04 && (abs(i-j)==1||abs(i-j)==B-1)) { adjacent=true; break; } }
        if(adjacent && n>=2) continue;
        float r=(float)(rs[i]/wsum[i]), g=(float)(gs[i]/wsum[i]), b=(float)(bs[i]/wsum[i]), hh,ss,vv;
        ImGui::ColorConvertRGBtoHSV(r,g,b,hh,ss,vv);
        ss=std::clamp(ss*1.15f,0.35f,0.95f); vv=std::clamp(vv*1.10f,0.55f,1.0f);     // stripes want some punch
        ImGui::ColorConvertHSVtoRGB(hh,ss,vv,r,g,b);
        out[n++]=IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),255);
    }
    return n;
}

// the user's palette (stripes.colors), parsed once per change
static int StripePalette(ImU32 out[8]){
    static std::string key="\x01"; static ImU32 cache[8]; static int cn=0; static int dark=-1;
    if(key!=g_stripeColors || dark!=(int)g_darkUI){ key=g_stripeColors; dark=(int)g_darkUI; cn=0;
        size_t i=0; while(i<=key.size() && cn<8){ size_t e=key.find(',',i); std::string t=Tml::Trim(key.substr(i,e==std::string::npos? std::string::npos : e-i));
            if(!t.empty()) cache[cn++]=CwColor(t,COL_GOLD); if(e==std::string::npos) break; i=e+1; } }
    for(int k=0;k<cn;k++) out[k]=cache[k];
    return cn;
}
static void DrawStripeCover(ImDrawList* dl,ImVec2 a,ImVec2 b,float pin,float pout,const ImU32* pal,int npal,float alpha,float textA){
    if(alpha<=0.002f || pin<=0.0f || pout>=1.0f) return;
    ImU32 user[8]; int nu=StripePalette(user);
    ImU32 fallback[4]={ COL_GOLD, M3Secondary(), M3Tertiary(), Mix(COL_GOLD,COL_INK,0.30f) };
    if(!pal || npal<=0){ if(nu>0){ pal=user; npal=nu; } else { pal=fallback; npal=4; } }
    const int N=std::clamp(g_stripeCount,3,30);
    float W=b.x-a.x, H=b.y-a.y, slant=H*std::clamp(g_stripeSlant,0.0f,1.5f);
    float sw=(W+slant)/N+2.0f;
    float tt=(float)ImGui::GetTime();
    const bool left = g_stripeDir==1;
    const float stag=std::min(0.06f,0.54f/N);
    dl->PushClipRect(a,b,true);
    for(int i=0;i<N;i++){
        float d=i*stag;
        float ein =Cael::eval(Cael::EMPHASIZED_DECEL,std::clamp((pin*(1.0f+N*stag)-d),0.0f,1.0f));
        float eout=Cael::eval(Cael::EMPHASIZED_ACCEL,std::clamp((pout*(1.0f+N*stag)-d),0.0f,1.0f));
        float travel=W+slant*2.0f+sw;
        float off=-(1.0f-ein)*travel + eout*travel;
        if(left) off=-off;
        int si = left? N-1-i : i;
        float x0=a.x-slant+si*sw+off;
        float ph=tt*g_stripeColorSpeed+i*0.37f; int k0=(int)floorf(ph); float fr=ph-k0; fr=fr*fr*(3.0f-2.0f*fr);
        ImU32 c=Mix(pal[((k0%npal)+npal)%npal],pal[(((k0+1)%npal)+npal)%npal],fr);
        ImVec2 q[4]={ V(x0,b.y), V(x0+sw,b.y), V(x0+sw+slant,a.y), V(x0+slant,a.y) };
        dl->AddConvexPolyFilled(q,4,MulA(WithA(c,255),alpha));
        if(g_stripeEdge) dl->AddLine(q[1],q[2],MulA(IM_COL32(255,255,255,70),alpha),2.0f);
    }
    if(textA>0.01f && !g_stripeText.empty()){
        const char* msg=g_stripeText.c_str();
        float fs=std::min(std::clamp(g_stripeTextSize,16.0f,160.0f),W*0.085f);
        ImU32 tc=CwColor(g_stripeTextColor,IM_COL32(255,255,255,255));
        int shape=M3ShapeFromName(g_stripeShape);
        float total=TextW(g_fMed,fs,msg)+(shape>=0? fs*0.9f : 0.0f);
        float x=a.x+(W-total)*0.5f+(shape>=0? fs*0.9f : 0.0f), cy=a.y+H*0.5f-fs*0.55f;
        if(shape>=0) M3Shape(dl,V(x-fs*0.62f,cy+fs*0.55f),fs*0.40f,MulA(tc,textA*alpha),shape,tt*3.0f);
        int i=0;
        for(const char* ch=msg; *ch; ){
            int len=1; unsigned char u=(unsigned char)*ch; if(u>=0xF0) len=4; else if(u>=0xE0) len=3; else if(u>=0xC0) len=2;
            std::string g(ch,ch+len);
            float bounce=g_stripeBounce? sinf(tt*9.0f-i*0.55f)*fs*0.10f : 0.0f;
            dl->AddText(g_fMed,fs*g_textScale,V(x+2,cy+bounce+3),MulA(IM_COL32(0,0,0,110),textA*alpha),g.c_str());
            dl->AddText(g_fMed,fs*g_textScale,V(x,cy+bounce),MulA(tc,textA*alpha),g.c_str());
            x+=TextW(g_fMed,fs,g.c_str()); ch+=len; i++;
        }
    }
    dl->PopClipRect();
}
