// Aether - the dashboard tab creator (custom tabs and widgets).
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =============================================================================================
// DASHBOARD TAB CREATOR — custom tabs, freely placed/sized widgets, multiple images per tab.
// A tab is a list of Widgets; each Widget is a kind + a rect stored as FRACTIONS of the drawer's
// content area, so a layout scales with the drawer. The four original tabs are the default set,
// each a single full-bleed "page" widget, so nothing changes until the user edits.
// =============================================================================================

// ---- a per-path image cache, so every image/GIF widget has its own animation ----
struct ImgAnim { std::vector<ID3D11ShaderResourceView*> frames; std::vector<int> delays;
                 int w=0,h=0,frame=0; double clock=0; bool tried=false; };
static std::map<std::string,ImgAnim> g_imgCache;
static ImgAnim* GetImg(const std::string& path){
    if(path.empty()) return nullptr;
    auto it=g_imgCache.find(path);
    if(it!=g_imgCache.end()) return &it->second;
    ImgAnim ia; ia.tried=true;
    IWICImagingFactory* fac=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&fac)))){
        std::wstring wp=U82W(path); IWICBitmapDecoder* dec=nullptr;
        if(SUCCEEDED(fac->CreateDecoderFromFilename(wp.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec))){
            UINT n=0; dec->GetFrameCount(&n);
            for(UINT i=0;i<n && i<300;i++){
                IWICBitmapFrameDecode* fr=nullptr; if(FAILED(dec->GetFrame(i,&fr))) continue;
                IWICFormatConverter* cv=nullptr; fac->CreateFormatConverter(&cv);
                if(cv && SUCCEEDED(cv->Initialize(fr,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeMedianCut))){
                    UINT w=0,h=0; cv->GetSize(&w,&h);
                    if(w&&h){ std::vector<uint8_t> buf((size_t)w*h*4); cv->CopyPixels(nullptr,w*4,(UINT)buf.size(),buf.data());
                        ID3D11ShaderResourceView* tex=MakeTextureBGRA(buf.data(),w,h);
                        if(tex){ ia.frames.push_back(tex); ia.w=(int)w; ia.h=(int)h;
                            int delay=100; IWICMetadataQueryReader* mq=nullptr;
                            if(SUCCEEDED(fr->GetMetadataQueryReader(&mq))){ PROPVARIANT v; PropVariantInit(&v);
                                if(SUCCEEDED(mq->GetMetadataByName(L"/grctlext/Delay",&v)) && v.vt==VT_UI2){ delay=v.uiVal*10; if(delay<20)delay=100; }
                                PropVariantClear(&v); mq->Release(); }
                            ia.delays.push_back(delay); } } }
                if(cv)cv->Release(); fr->Release();
            }
            dec->Release();
        }
        fac->Release();
    }
    g_imgCache[path]=std::move(ia);
    return &g_imgCache[path];
}
static void AdvanceImages(){                          // called once per frame
    for(auto& kv2:g_imgCache){ ImgAnim& a=kv2.second;
        if(a.frames.size()<2) continue;
        a.clock+=g_frameDt*1000.0; int guard=0;
        while(a.frame<(int)a.delays.size() && a.clock>=a.delays[a.frame] && guard++<300){
            a.clock-=a.delays[a.frame]; a.frame=(a.frame+1)%(int)a.frames.size(); } }
}
static void FreeImageCache(){ for(auto& kv2:g_imgCache) for(auto t:kv2.second.frames) if(t)t->Release(); g_imgCache.clear(); }

// ---- SVG icons (real Adwaita / Breeze artwork from the ISO) ------------------------------
// nanosvg rasterizes the vector to RGBA at the requested pixel size. Adwaita "symbolic" icons
// are monochrome black-on-transparent, so passing tint!=0 repaints them any colour (keeping the
// coverage/alpha). Full-colour icons pass tint=0 to keep their own palette. Cached per path+px+tint.
struct SvgTex { ID3D11ShaderResourceView* srv=nullptr; int w=0,h=0; };
static std::map<std::string,SvgTex> g_svgCache;
static NSVGrasterizer* g_svgRast=nullptr;
static SvgTex GetSvgIcon(const std::string& path,int px,ImU32 tint=0){
    if(path.empty()||px<=0) return {};
    char key[512]; snprintf(key,sizeof key,"%s|%d|%08X",path.c_str(),px,(unsigned)tint);
    auto it=g_svgCache.find(key); if(it!=g_svgCache.end()) return it->second;
    SvgTex out;
    if(!g_svgRast) g_svgRast=nsvgCreateRasterizer();
    // nsvgParseFromFile mutates the buffer & needs a writable path string
    std::string p=path;
    NSVGimage* img=nsvgParseFromFile(p.c_str(),"px",96.0f);
    if(img && g_svgRast && img->width>0 && img->height>0){
        int w=px, h=px;
        float sc = px / (img->width>img->height? img->width : img->height);
        w=(int)(img->width*sc+0.5f);  h=(int)(img->height*sc+0.5f);
        if(w<1)w=1; if(h<1)h=1; if(w>1024)w=1024; if(h>1024)h=1024;
        std::vector<uint8_t> rgba((size_t)w*h*4,0);
        nsvgRasterize(g_svgRast,img,0,0,sc,rgba.data(),w,h,w*4);
        // nanosvg emits RGBA; MakeTextureBGRA wants BGRA. Swap, and optionally repaint to tint.
        int tr=tint?((tint)&0xFF):0, tg=tint?((tint>>8)&0xFF):0, tb=tint?((tint>>16)&0xFF):0; // ImU32 = ABGR? no: IM_COL32=RGBA in mem -> r=byte0
        // ImGui IM_COL32 packs as 0xAABBGGRR, so tint&0xFF=R, >>8=G, >>16=B
        for(size_t i=0;i<rgba.size();i+=4){
            uint8_t r=rgba[i],g=rgba[i+1],b=rgba[i+2],a=rgba[i+3];
            if(tint){ rgba[i]=(uint8_t)tb; rgba[i+1]=(uint8_t)tg; rgba[i+2]=(uint8_t)tr; }
            else    { rgba[i]=b; rgba[i+2]=r; }               // RGBA->BGRA
            rgba[i+3]=a;
        }
        out.srv=MakeTextureBGRA(rgba.data(),w,h); out.w=w; out.h=h;
    }
    if(img) nsvgDelete(img);
    g_svgCache[key]=out; return g_svgCache[key];
}
static void FreeSvgCache(){ for(auto& kv:g_svgCache) if(kv.second.srv) kv.second.srv->Release(); g_svgCache.clear();
                            if(g_svgRast){ nsvgDeleteRasterizer(g_svgRast); g_svgRast=nullptr; } }
// Resolve a themed icon name to an on-disk SVG. Tries Adwaita symbolic first, then breeze.
static std::string g_iconRoot=ExeDir()+"linux\\icons";
static std::string ResolveIcon(const std::string& name){
    // common Adwaita symbolic locations
    const char* dirs[]={
        "\\Adwaita\\symbolic\\", // then category subfolders below
    };
    // Adwaita stores symbolics under scalable/<category>/<name>-symbolic.svg in some builds,
    // and symbolic/<category>/ in others; probe a handful of category folders.
    const char* cats[]={"actions","places","devices","status","apps","categories","emblems","mimetypes","ui"};
    std::string base=g_iconRoot;
    for(const char* c:cats){
        std::string a=base+"\\Adwaita\\symbolic\\"+c+"\\"+name+"-symbolic.svg";
        FILE* f=fopen(a.c_str(),"rb"); if(f){fclose(f);return a;}
        std::string a2=base+"\\Adwaita\\scalable\\"+c+"\\"+name+"-symbolic.svg";
        f=fopen(a2.c_str(),"rb"); if(f){fclose(f);return a2;}
    }
    // breeze fallback (full path guesses)
    std::string b=base+"\\breeze\\actions\\24\\"+name+".svg";
    FILE* f=fopen(b.c_str(),"rb"); if(f){fclose(f);return b;}
    return "";
}
// Draw a real Adwaita symbolic icon centred at c, box ~2*s px, tinted col.
// Returns false if the SVG couldn't be found/loaded (caller falls back to hand-drawn).
static bool AdwSym(ImDrawList* dl,ImVec2 c,float s,ImU32 col,const char* name){
    std::string path=ResolveIcon(name);
    if(path.empty()) return false;
    int box=(int)(s*2.0f+0.5f); int ras=box*2>16?box*2:32;   // rasterize 2x for crispness
    SvgTex t=GetSvgIcon(path,ras,col);
    if(!t.srv) return false;
    float hw=box*0.5f, hh=box*0.5f*(t.h/(float)(t.w?t.w:box));
    dl->AddImage((ImTextureID)t.srv,V(c.x-hw,c.y-hh),V(c.x+hw,c.y+hh));
    return true;
}
// ---- themed icon lookup ------------------------------------------------------------------------
// Breeze scatters icons over category folders AND size folders with no single predictable path, so
// resolving one means probing a grid. That is ~100 fopen calls, which is fine ONCE and ruinous every
// frame - hence the permanent name->path cache (a miss caches the empty string too, so a missing
// icon costs one sweep for the life of the process, not one per frame).
static std::unordered_map<std::string,std::string> g_themedPath;
static const std::string& ThemedIconPath(const std::string& name){
    static const std::string empty;
    auto it=g_themedPath.find(name); if(it!=g_themedPath.end()) return it->second;
    std::string found;
    // Existing is not good enough. Breeze 6 ships two different things under the same names: the
    // old single-path icons (style="fill:currentColor", which tint correctly), and layered artwork
    // whose root carries fill="none" and whose sub-paths have no fill of their own. Our rasterizer
    // does not inherit fill from <svg>, so it paints those sub-paths solid black - which is how a
    // gamepad became an opaque rounded slab in the workspace strip. Take the file only if it can
    // actually be tinted, and keep probing otherwise.
    auto probe=[&](const std::string& p)->bool{
        FILE* f=fopen(p.c_str(),"rb"); if(!f) return false;
        char head[4096]; size_t n=fread(head,1,sizeof(head)-1,f); head[n]=0; fclose(f);
        std::string h(head,n);
        if(h.find("currentColor")==std::string::npos && h.find("fill=\"none\"")!=std::string::npos)
            return false;
        found=p; return true; };
    if(g_iconSet==ICONSET_ADWAITA){
        const char* cats[]={"actions","places","devices","status","apps","categories","emblems","ui","legacy"};
        for(const char* c:cats){
            if(probe(g_iconRoot+"\\Adwaita\\symbolic\\"+c+"\\"+name+"-symbolic.svg")) break;
            if(probe(g_iconRoot+"\\Adwaita\\scalable\\"+c+"\\"+name+"-symbolic.svg")) break; }
    } else {
        // 22 and 24 first: those are the panel sizes, and they carry the hinted panel artwork.
        const char* cats[]={"actions","status","devices","applets","apps","preferences","places","categories","emblems"};
        const char* szs []={"22","24","16","32","48","64","12"};
        for(const char* c:cats){ bool hit=false;
            for(const char* z:szs){
                if(probe(g_iconRoot+"\\breeze\\"+c+"\\"+z+"\\"+name+"-symbolic.svg")){ hit=true; break; }
                if(probe(g_iconRoot+"\\breeze\\"+c+"\\"+z+"\\"+name+".svg")){ hit=true; break; } }
            if(hit) break; }
    }
    return g_themedPath.emplace(name,found).first->second;
}
// Draw a themed icon centred at c in a 2s box, tinted col. false => the theme has no such icon (or
// the set is "Aether"), and the caller draws its own glyph.
// name -> codepoint. Accepts BOTH the freedesktop names already scattered through the shell and
// the Material Symbols names, so call sites did not have to be rewritten.
static unsigned short MIconCp(const char* name){
    if(!name||!*name) return 0;
    for(int i=0;i<MICON_COUNT;i++) if(!strcmp(MICONS[i].name,name)) return MICONS[i].cp;
    return 0;
}
// Draw one Material Symbols glyph centred on c. px is the EM size, which for this font is also the
// icon's nominal box, so it lines up with the SVG path's `s*2` box.
static bool MSym(ImDrawList* dl,ImVec2 c,float px,ImU32 col,unsigned short cp){
    if(!g_fIcon || !cp || px<1.0f) return false;
    char u8[8]; int n=0; unsigned v=cp;                       // UTF-8 encode (all of these are 3-byte)
    if(v<0x80)        { u8[n++]=(char)v; }
    else if(v<0x800)  { u8[n++]=(char)(0xC0|(v>>6));    u8[n++]=(char)(0x80|(v&0x3F)); }
    else              { u8[n++]=(char)(0xE0|(v>>12));   u8[n++]=(char)(0x80|((v>>6)&0x3F));
                        u8[n++]=(char)(0x80|(v&0x3F)); }
    u8[n]=0;
    ImVec2 sz=g_fIcon->CalcTextSizeA(px,FLT_MAX,0.0f,u8);
    if(sz.x<=0.0f) return false;
    dl->AddText(g_fIcon,px,V(c.x-sz.x*0.5f,c.y-sz.y*0.5f),col,u8);
    return true;
}
// ---- every Material Symbols icon, by name ------------------------------------------------------------
// The font atlas only carries the ~100 glyphs the shell draws itself. Dashboard tabs and custom widgets can
// name ANY of the 4,284 Material Symbols (the same icon names Caelestia uses: "dashboard", "queue_music",
// "lyrics"...), so those are rasterised on demand from the full font with stb_truetype and cached per size.
#ifndef STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"
#endif
struct MsGlyph { ID3D11ShaderResourceView* tex=nullptr; int w=0,h=0; float x0=0,y0=0; };
static struct { int state=0; std::vector<unsigned char> data; stbtt_fontinfo font{}; float ascent=0;
                std::unordered_map<std::string,unsigned> codes; std::unordered_map<uint64_t,MsGlyph> cache; } g_ms;
static bool MsLoad(){
    if(g_ms.state) return g_ms.state==1;
    g_ms.state=2;
    std::string base=ExeDir()+"assets\\";
    { FILE* f=fopen((base+"MaterialSymbolsRounded-full.ttf").c_str(),"rb"); if(!f) return false;
      fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
      if(n<=0){ fclose(f); return false; }
      g_ms.data.resize((size_t)n); fread(g_ms.data.data(),1,(size_t)n,f); fclose(f); }
    if(!stbtt_InitFont(&g_ms.font,g_ms.data.data(),stbtt_GetFontOffsetForIndex(g_ms.data.data(),0))) return false;
    int asc=0,desc=0,gap=0; stbtt_GetFontVMetrics(&g_ms.font,&asc,&desc,&gap); g_ms.ascent=(float)asc;
    { FILE* f=fopen((base+"MaterialSymbolsRounded.codepoints").c_str(),"rb");
      if(f){ char line[256]; while(fgets(line,sizeof(line),f)){ char nm[200]; unsigned cp=0;
                 if(sscanf(line,"%199s %x",nm,&cp)==2) g_ms.codes[nm]=cp; } fclose(f); } }
    g_ms.state=1; return true;
}
static unsigned MsCode(const std::string& name){
    if(name.empty() || !MsLoad()) return 0;
    auto it=g_ms.codes.find(name); if(it!=g_ms.codes.end()) return it->second;
    std::string n=name; for(auto& c:n){ if(c=='-'||c==' ') c='_'; else c=(char)tolower((unsigned char)c); }
    it=g_ms.codes.find(n); if(it!=g_ms.codes.end()) return it->second;
    if(n.size()>=4 && n.size()<=5 && strspn(n.c_str(),"0123456789abcdef")==n.size()) return (unsigned)strtoul(n.c_str(),nullptr,16);
    return 0;
}
// Draw icon `name` centred on c, em size px (logical), tinted col. false = no such icon.
static bool MsIcon(ImDrawList* dl,const std::string& name,ImVec2 c,float px,ImU32 col){
    unsigned cp=MsCode(name); if(!cp || px<2.0f) return false;
    int phys=(int)std::round(px*g_uiScale/2.0f)*2; if(phys<4) phys=4; if(phys>512) phys=512;
    uint64_t key=((uint64_t)cp<<16)|(uint64_t)phys;
    auto it=g_ms.cache.find(key);
    if(it==g_ms.cache.end()){
        MsGlyph gl; float sc=stbtt_ScaleForMappingEmToPixels(&g_ms.font,(float)phys);
        int w=0,h=0,xo=0,yo=0;
        unsigned char* bm=stbtt_GetCodepointBitmap(&g_ms.font,sc,sc,(int)cp,&w,&h,&xo,&yo);
        if(bm && w>0 && h>0){
            std::vector<uint8_t> px4((size_t)w*h*4);
            for(int i=0;i<w*h;i++){ px4[i*4]=255; px4[i*4+1]=255; px4[i*4+2]=255; px4[i*4+3]=bm[i]; }
            gl.tex=MakeTextureBGRA(px4.data(),w,h); gl.w=w; gl.h=h;
            // position inside the em box: x from the pen, y from the top of the em (ascent)
            gl.x0=(float)xo; gl.y0=g_ms.ascent*sc+(float)yo;
        }
        if(bm) stbtt_FreeBitmap(bm,nullptr);
        it=g_ms.cache.emplace(key,gl).first;
    }
    const MsGlyph& gl=it->second; if(!gl.tex) return false;
    float k=px/(float)phys;                                     // physical -> logical at this size
    ImVec2 a=V(c.x-px*0.5f+gl.x0*k, c.y-px*0.5f+gl.y0*k);
    dl->AddImage((ImTextureID)gl.tex,a,V(a.x+gl.w*k,a.y+gl.h*k),ImVec2(0,0),ImVec2(1,1),col);
    return true;
}
static bool ThemedSym(ImDrawList* dl,ImVec2 c,float s,ImU32 col,const char* name){
    if(g_iconSet==ICONSET_AETHER || !name || !*name) return false;
    if(g_iconSet==ICONSET_MATERIAL) return MSym(dl,c,s*2.0f,col,MIconCp(name));
    const std::string& path=ThemedIconPath(name);
    if(path.empty()) return false;
    int box=(int)(s*2.0f+0.5f); if(box<8) box=8;
    int ras=box*2>32? box*2:32;                       // rasterize 2x so panel sizes stay crisp
    SvgTex t=GetSvgIcon(path,ras,col);
    if(!t.srv) return false;
    float hw=box*0.5f, hh=box*0.5f*(t.h/(float)(t.w?t.w:box));
    dl->AddImage((ImTextureID)t.srv,V(c.x-hw,c.y-hh),V(c.x+hw,c.y+hh));
    return true;
}
// draw an image, aspect-fit (cover=false, whole image shown) or cover-fill (cover=true, cropped)
static void DrawImgWidget(ImDrawList* dl,ImVec2 a,ImVec2 b,const std::string& path,bool cover,float rnd){
    ImgAnim* ia=GetImg(path);
    if(!ia||ia->frames.empty()){
        dl->AddRectFilled(a,b,WithA(COL_INK2,26),rnd);
        const char* m = path.empty()? "Click \"Choose image\" to set one":"Could not load that image";
        TextAt(dl,g_fSml,13,V((a.x+b.x)/2-TextW(g_fSml,13,m)/2,(a.y+b.y)/2-8),COL_INK2,m);
        return;
    }
    int idx=std::min(ia->frame,(int)ia->frames.size()-1);
    float bw=b.x-a.x, bh=b.y-a.y, sw=(float)ia->w, sh=(float)ia->h;
    dl->PushClipRect(a,b,true);
    if(cover){
        ImVec2 uv0,uv1; CoverUV(ia->w,ia->h,bw,bh,uv0,uv1);
        dl->AddImageRounded((ImTextureID)ia->frames[idx],a,b,uv0,uv1,IM_COL32(255,255,255,255),rnd);
    } else {
        float sc=std::min(bw/sw,bh/sh); float w=sw*sc,h=sh*sc;
        ImVec2 c=V((a.x+b.x)/2,(a.y+b.y)/2);
        dl->AddImageRounded((ImTextureID)ia->frames[idx],V(c.x-w/2,c.y-h/2),V(c.x+w/2,c.y+h/2),
                            ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,255),rnd*0.5f);
    }
    dl->PopClipRect();
}

// ---- individual widget renderers (extracted so they can be placed anywhere) ----
static void WClock(ImDrawList* dl,ImVec2 o,ImVec2 s){
    Card(dl,o,s); time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
    int h12=g_clock24? lt.tm_hour : (((lt.tm_hour%12)==0)?12:lt.tm_hour%12);
    char hh[8],mm[8]; snprintf(hh,8,"%02d",h12); snprintf(mm,8,"%02d",lt.tm_min);
    float mx=o.x+s.x*0.5f, cyc=o.y+s.y*0.5f;
    float fs=std::min(40.0f,s.y*0.30f);
    TextAt(dl,g_fBig,fs,V(mx-TextW(g_fBig,fs,hh)/2,cyc-fs*1.7f),COL_INK,hh);
    for(int i=0;i<3;i++) dl->AddCircleFilled(V(mx-10+i*10,cyc-4),2.6f,COL_INK2);
    TextAt(dl,g_fBig,fs,V(mx-TextW(g_fBig,fs,mm)/2,cyc+6),COL_INK,mm);
    const char* ap=g_clock24?"":(lt.tm_hour<12?"AM":"PM");
    if(*ap) TextAt(dl,g_fMed,18,V(mx-TextW(g_fMed,18,ap)/2,cyc+fs*1.3f),COL_GOLD,ap);
}
static void WCalendar(ImDrawList* dl,ImVec2 o,ImVec2 s){
    Card(dl,o,s); ImGuiIO& io=ImGui::GetIO();
    DrawCalendar(dl,o,V(o.x+s.x,o.y+s.y),io);
}
static void WWeather(ImDrawList* dl,ImVec2 o,ImVec2 s){
    Card(dl,o,s);
    if(g_wx.ok){ WxIcon(dl,V(o.x+42,o.y+s.y*0.5f),26,g_wx.code);
        char t[16]; snprintf(t,16,"%d\xC2\xB0",(int)round(g_wx.temp));
        TextAt(dl,g_fBig,36,V(o.x+84,o.y+s.y*0.5f-30),COL_INK,t);
        TextAt(dl,g_fSml,15,V(o.x+86,o.y+s.y*0.5f+8),COL_INK2,WxText(g_wx.code));
        if(!g_wx.city.empty()) TextAt(dl,g_fSml,13,V(o.x+20,o.y+14),COL_INK2,g_wx.city.c_str());
    } else TextAt(dl,g_fSml,15,V(o.x+24,o.y+s.y*0.5f-8),COL_INK2,"Weather\xE2\x80\xA6");
}
// Same card the built-in Dashboard page draws in its second column: avatar, then icon rows for
// distro / shell / uptime. The old widget version was a different thing entirely - a fixed 24px
// avatar with hard-coded "OS  Windows 11" label-value columns at absolute pixel offsets, so in a
// wide box it huddled on the left with a third of the card empty, and in a short one the uptime
// line fell out of the bottom. Everything here is derived from the box instead.
static bool ProfileIcon(ImDrawList* dl,const std::string& icon,ImVec2 c,float px,ImU32 col);   // fwd (Profile.h)
static void WProfile(ImDrawList* dl,ImVec2 o,ImVec2 s){
    Card(dl,o,s);
    float pulse=0.5f+0.5f*sinf((float)ImGui::GetTime()*2.2f);
    LoadAvatar();
    float R  = std::clamp(std::min(s.y*0.34f, s.x*0.16f), 14.0f, 34.0f);
    float acx= o.x + 14.0f + R, acy = o.y + s.y*0.5f;
    if(g_avatar){ dl->PushClipRect(V(acx-R,acy-R),V(acx+R,acy+R),true);
        dl->AddImageRounded((ImTextureID)g_avatar,V(acx-R,acy-R),V(acx+R,acy+R),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,255),R);
        dl->PopClipRect(); dl->AddCircle(V(acx,acy),R,WithA(COL_GOLD,(int)(120+pulse*90)),0,2.4f);
    } else { dl->AddCircleFilled(V(acx,acy),R,COL_CARD2);
        dl->AddCircleFilled(V(acx,acy-R*0.29f),R*0.35f,COL_INK2);
        dl->PathArcTo(V(acx,acy+R*0.65f),R*0.50f,3.1416f,6.2832f,20); dl->PathStroke(COL_INK2,0,R*0.41f);
        dl->AddCircle(V(acx,acy),R,WithA(COL_GOLD,120),0,2.4f); }

    static std::string user; if(user.empty()){ if(!g_profileName.empty()) user=g_profileName;
        else { wchar_t u[UNLEN+1]; DWORD n=UNLEN+1; if(GetUserNameW(u,&n))user=W2U8(u); } }
    unsigned long long mins=GetTickCount64()/60000ULL; char up[64];
    if(mins<60) snprintf(up,64,"up %llu minutes",mins);
    else snprintf(up,64,"up %llu hour%s, %llu min",mins/60,(mins/60==1?"":"s"),mins%60);

    float fs   = std::clamp(s.y*0.115f, 11.0f, 15.0f);
    float rowH = fs*1.95f;
    float fx   = acx + R + 14.0f, fw = (o.x+s.x) - fx - 14.0f;
    float fy   = acy - rowH*1.5f + (rowH-fs)*0.5f;
    if(fw < 40.0f) return;                                  // too narrow for the rows: avatar only
    std::string osn=ProfileOsName(), wmn=ProfileWmName();
    const char* vals[3] = { osn.c_str(), wmn.c_str(), up };
    static const char* ICN[3] = { "os", "select_window", "clock_arrow_up" };
    for(int i=0;i<3;i++){
        float ry=fy+i*rowH; ImVec2 ic=V(fx+7,ry+fs*0.55f);
        ProfileIcon(dl,ICN[i],ic,fs*1.2f,i==0? COL_GOLD : i==1? M3Secondary() : M3Tertiary());
        TextAt(dl,g_fSml,fs,V(fx+22,ry),COL_INK,Clip(g_fSml,fs,vals[i],fw-30).c_str());
    }
    // the name, where the built-in card has room for it
    if(s.y>110.0f) TextAt(dl,g_fSml,fs*0.92f,V(fx+22,fy+3*rowH+2),WithA(COL_INK2,190),
                          Clip(g_fSml,fs*0.92f,user,fw-30).c_str());
}
static void WVolume(ImDrawList* dl,ImVec2 o,ImVec2 s){
    Card(dl,o,s); float vol=g_volCache>=0?g_volCache:0.0f;
    float bw=10, top=o.y+22, bot=o.y+s.y-44, colx=o.x+s.x*0.5f;
    dl->AddRectFilled(V(colx-bw/2,top),V(colx+bw/2,bot),COL_TRACK,bw*0.5f);
    float fy=bot-(bot-top)*std::clamp(vol,0.0f,1.0f);
    dl->AddRectFilled(V(colx-bw/2,fy),V(colx+bw/2,bot),COL_GOLD,bw*0.5f);
    SpeakerIcon(dl,V(colx,o.y+s.y-24),COL_INK2);
    char vp[8]; snprintf(vp,8,"%d",(int)(vol*100)); TextAt(dl,g_fSml,12,V(colx-TextW(g_fSml,12,vp)/2,top-16),COL_INK2,vp);
}
// The arc gauge the built-in Dashboard page draws: a 300-degree track with the accent sweeping
// round it, the reading inside, the label and the absolute figure UNDER it. It only ever existed
// as a lambda inside DrawDashboard, so breaking that page apart could not hand it back and you got
// a shape gauge in its place instead.
static void RingGauge(ImDrawList* dl,ImVec2 o,ImVec2 s,int which){
    Card(dl,o,s);
    float pulse=0.5f+0.5f*sinf((float)ImGui::GetTime()*2.2f);
    float frac = (which==0)? (float)g_st.cpuUsage
               : (which==1)? (g_st.memTotal ? (float)g_st.memUsed /g_st.memTotal  : 0.0f)
               :             (g_st.diskTotal? (float)g_st.diskUsed/g_st.diskTotal : 0.0f);
    frac=std::clamp(frac,0.0f,1.0f);
    // reserve a band under the ring for the label + figure, so text never lands on the arc
    const float labelH = (which==0)? 18.0f : 30.0f;
    float R=std::min(s.x*0.5f-10.0f,(s.y-labelH)*0.5f-8.0f);
    if(R<8.0f) return;
    float cx=o.x+s.x*0.5f, cy=o.y+8.0f+R, th=std::max(5.0f,R*0.22f);
    const float a1=-2.618f, sweep=5.236f;              // 300 degrees, gap at the bottom
    dl->PathArcTo(V(cx,cy),R,a1,a1+sweep,48); dl->PathStroke(WithA(COL_INK2,72),0,th);
    if(frac>0.001f){ float ae=a1+sweep*frac;
        dl->PathArcTo(V(cx,cy),R,a1,ae,48); dl->PathStroke(COL_GOLD,0,th);
        dl->AddCircleFilled(V(cx+cosf(ae)*R,cy+sinf(ae)*R),th*0.55f+pulse,COL_GOLD); }
    float shownPct=Cael::anim(9420+which,frac*100.0f,Cael::DUR_DEFAULT_SPATIAL,Cael::DEFAULT_SPATIAL);
    char pv[12]; snprintf(pv,12,"%d%%",(int)lroundf(shownPct));
    float ps=std::max(11.0f,R*0.62f);
    TextAt(dl,g_fMed,ps,V(cx-TextW(g_fMed,ps,pv)/2,cy-ps*0.62f),COL_INK,pv);
    const char* lbl=(which==0)?"CPU":(which==1)?"RAM":"DISK";
    TextAt(dl,g_fSml,10,V(cx-TextW(g_fSml,10,lbl)/2,cy+R+4),WithA(COL_INK2,215),lbl);
    if(which!=0){ char ab[28];
        double used=(which==1)?g_st.memUsed:g_st.diskUsed, tot=(which==1)?g_st.memTotal:g_st.diskTotal;
        if(tot>0){ snprintf(ab,28,"%.0f/%.0fG",used/1073741824.0,tot/1073741824.0);
            TextAt(dl,g_fSml,9,V(cx-TextW(g_fSml,9,ab)/2,cy+R+15),WithA(COL_INK2,165),ab); } }
}
static void WMediaCard(ImDrawList* dl,ImVec2 o,ImVec2 s){
    Card(dl,o,s); float acx=o.x+s.x*0.5f, acy=o.y+70;
    ImU32 tn=g_mdTint.load(); if(!tn||!g_md.has) tn=COL_GOLD;
    if(g_mdStyle==MDSTYLE_CIRCLE){
        if(g_mdRays) MediaBars(dl,V(acx,acy),42,tn,g_md.playing);
        if(g_mdArt){ ImVec2 uv0,uv1; CoverUV(g_mdArtW,g_mdArtH,84,84,uv0,uv1);
            dl->AddImageRounded((ImTextureID)g_mdArt,V(acx-42,acy-42),V(acx+42,acy+42),uv0,uv1,
                                IM_COL32(255,255,255,255),42); }
        else { dl->AddCircleFilled(V(acx,acy),42,COL_CARD2,0);
               dl->AddRectFilled(V(acx-16,acy-12),V(acx+2,acy+8),COL_INK2,3);
               dl->AddRectFilled(V(acx+8,acy-16),V(acx+11,acy+10),COL_INK2,1.5f); }
        dl->AddCircle(V(acx,acy),42,WithA(COL_INK2,90),0,1.4f);
    } else {
        if(g_mdArt) M3ShapeImage(dl,V(acx,acy),42,g_mdArt,g_mdArtW,g_mdArtH,g_mdMediaShape,0.0f);
        else { M3Shape(dl,V(acx,acy),42,COL_CARD2,g_mdMediaShape,0.0f);
               dl->AddRectFilled(V(acx-16,acy-12),V(acx+2,acy+8),COL_INK2,3);
               dl->AddRectFilled(V(acx+8,acy-16),V(acx+11,acy+10),COL_INK2,1.5f); }
        { double dur=g_md.dur; float pf=(float)(dur>0? std::clamp(MediaPos()/dur,0.0,1.0) : 0.0);
          float wph = g_md.playing? -ShellPhase()*2.4f : 0.0f;
          WavyArc(dl,V(acx,acy),56,pf,WithA(tn,255),WithA(COL_INK2,110),3.6f,wph); }
    }
    bool has=g_md.has&&!g_md.title.empty(); float tw=s.x-28;
    std::string t1=has?Clip(g_fMed,17,g_md.title,tw):"No media";
    std::string t2=has?Clip(g_fSml,15,g_md.artist,tw):"";
    TextAt(dl,g_fMed,17,V(acx-TextW(g_fMed,17,t1.c_str())/2,acy+58),COL_INK,t1.c_str());
    TextAt(dl,g_fSml,15,V(acx-TextW(g_fSml,15,t2.c_str())/2,acy+82),COL_INK2,t2.c_str());
    if(!DrawMediaImage(dl,V(acx,o.y+s.y-62),s.x-28,86.0f,has&&g_md.playing))
        BongoCat(dl,V(acx,o.y+s.y-64),1.15f,has&&g_md.playing);
}
static void WText(ImDrawList* dl,ImVec2 o,ImVec2 s,const std::string& t){
    Card(dl,o,s);
    float fs=std::min(30.0f,s.y*0.5f);
    TextAt(dl,g_fMed,fs,V(o.x+s.x*0.5f-TextW(g_fMed,fs,t.c_str())/2,o.y+s.y*0.5f-fs*0.6f),COL_INK,
           t.empty()? "Text":t.c_str());
}

// dispatch one widget into a pixel rect
static void WrapLines(ImFont* f,float sz,const std::string& in,float w,int maxLines,std::vector<std::string>& out);   // fwd
#include "src/modules/dashboard/CustomWidgets.h"
#include "src/modules/dashboard/Profile.h"         // the Profile widget: avatar, presence, custom status
#include "src/modules/dashboard/CaelV2.h"          // the newer Caelestia Dashboard / Media / Performance cards
#include "src/modules/dashboard/Terminal.h"        // the Terminal tab: a real shell (ConPTY) in the dashboard
struct WStyle { std::string bg, image, shape, color, border; float radius=-1, opacity=1, pad=0; };
static WStyle ParseWStyle(const std::string& s){
    static std::unordered_map<std::string,WStyle> cache;
    auto it=cache.find(s); if(it!=cache.end()) return it->second;
    WStyle st; size_t i=0;
    while(i<s.size()){
        size_t e=s.find(';',i); std::string kv=s.substr(i,e==std::string::npos? std::string::npos : e-i);
        size_t eq=kv.find('=');
        if(eq!=std::string::npos){ std::string k=Tml::Trim(kv.substr(0,eq)), v=Tml::Trim(kv.substr(eq+1));
            if(k=="bg"||k=="background") st.bg=v; else if(k=="image") st.image=v; else if(k=="shape") st.shape=v;
            else if(k=="color") st.color=v; else if(k=="border") st.border=v;
            else if(k=="radius") st.radius=(float)atof(v.c_str()); else if(k=="opacity") st.opacity=std::clamp((float)atof(v.c_str()),0.0f,1.0f);
            else if(k=="padding") st.pad=std::max(0.0f,(float)atof(v.c_str())); }
        if(e==std::string::npos) break; i=e+1;
    }
    if(cache.size()>512) cache.clear();
    cache[s]=st; return st;
}
static std::string BuildWStyle(const WStyle& st){
    std::string o; char b[64];
    auto add=[&](const char* k,const std::string& v){ if(v.empty()) return; if(!o.empty()) o+="; "; o+=k; o+="="; o+=v; };
    add("bg",st.bg); add("image",st.image); add("shape",st.shape); add("color",st.color); add("border",st.border);
    if(st.radius>=0){ snprintf(b,64,"%g",st.radius); add("radius",b); }
    if(st.opacity<0.999f){ snprintf(b,64,"%.2f",st.opacity); add("opacity",b); }
    if(st.pad>0){ snprintf(b,64,"%g",st.pad); add("padding",b); }
    return o;
}
static int g_widgetUid=0;
static void DrawWidgetInner(ImDrawList* dl,ImGuiIO& io,Widget& w,ImVec2 o,ImVec2 s);
static void DrawWidget(ImDrawList* dl,ImGuiIO& io,Widget& w,ImVec2 o,ImVec2 s){
    if(w.uid==0) w.uid=++g_widgetUid;
    if(w.style.empty()){ DrawWidgetInner(dl,io,w,o,s); return; }
    WStyle st=ParseWStyle(w.style);
    int vtx0=dl->VtxBuffer.Size;
    float keepR=g_cardRound; if(st.radius>=0) g_cardRound=st.radius;
    int keepMode=g_cardMode; ImU32 keepCol=g_cardCol;
    std::string bgl=st.bg; for(auto& ch:bgl) ch=(char)tolower((unsigned char)ch);
    if(!st.shape.empty() || !st.image.empty()){
        CwBackground(dl,o,s,"card",st.shape,st.image,st.color,st.radius);
        g_cardMode=1;
    } else if(bgl=="none"||bgl=="transparent") g_cardMode=1;
    else if(!st.color.empty() || (!bgl.empty() && bgl!="card")){ g_cardMode=2; g_cardCol=CwColor(!st.color.empty()? st.color : st.bg,COL_CARD); }
    if(!st.border.empty()) dl->AddRect(o,V(o.x+s.x,o.y+s.y),CwColor(st.border,COL_INK2),g_cardRound,0,1.5f);
    ImVec2 io2=V(o.x+st.pad,o.y+st.pad), is2=V(std::max(10.0f,s.x-st.pad*2),std::max(10.0f,s.y-st.pad*2));
    DrawWidgetInner(dl,io,w,io2,is2);
    g_cardMode=keepMode; g_cardCol=keepCol; g_cardRound=keepR;
    if(st.opacity<0.999f)
        for(int i=vtx0;i<dl->VtxBuffer.Size;i++){ ImU32& c=dl->VtxBuffer[i].col;
            ImU32 a=(c>>IM_COL32_A_SHIFT)&0xFF; a=(ImU32)(a*st.opacity); c=(c&~IM_COL32_A_MASK)|(a<<IM_COL32_A_SHIFT); }
}
static void DrawWidgetInner(ImDrawList* dl,ImGuiIO& io,Widget& w,ImVec2 o,ImVec2 s){
    switch(w.kind){
    case WK_CUSTOM: CwDraw(dl,io,w.arg,std::to_string(w.uid)+"|"+w.arg,o,s); break;
    case WK_USER:   WUser(dl,io,o,s,w.uid,true); break;
    case WK_V2_HOME:      DrawDashboardV2(dl,o,s); break;
    case WK_V2_MEDIAPAGE: LyricsMaybeFetch(); DrawMediaV2(dl,o,s,io); break;
    case WK_V2_PERFPAGE:  DrawPerformanceV2(dl,o,s); break;
    case WK_V2_WEATHER:   V2WeatherCard(dl,io,o,s); break;
    case WK_V2_USER:      V2UserCard(dl,io,o,s,w.uid); break;
    case WK_V2_CLOCK:     V2ClockCard(dl,io,o,s); break;
    case WK_V2_CALENDAR:  V2CalendarCard(dl,io,o,s,w.uid); break;
    case WK_V2_RINGS:     V2RingsCard(dl,io,o,s); break;
    case WK_V2_MEDIA:     V2MediaCard(dl,io,o,s,w.uid); break;
    case WK_V2_PLAYER:    V2PlayerCard(dl,io,o,s,true,w.uid); break;
    case WK_V2_LYRICS:    LyricsMaybeFetch(); V2LyricsCard(dl,io,o,s,true,w.uid); break;
    case WK_V2_CPU:       V2HeroCard(dl,io,o,s,0); break;
    case WK_V2_GPU:       V2HeroCard(dl,io,o,s,1); break;
    case WK_V2_STORAGE:   V2StorageCard(dl,io,o,s,w.uid); break;
    case WK_V2_NETWORK:   V2NetworkCard(dl,io,o,s); break;
    case WK_V2_MEMORY:    V2MemoryCard(dl,io,o,s); break;
    case WK_TERMINAL:     DrawTerminalCard(dl,io,o,s,true); break;
    case WK_MIXER:        DrawAudioMixer(dl,io,o,s,w.uid,true); break;
    case WK_PAGE_DASH:    DrawDashboard(dl,o,s); break;
    case WK_PAGE_MEDIA:   DrawMedia(dl,o,s,io); break;
    case WK_PAGE_PERF:    DrawPerformance(dl,o,s); break;
    case WK_PAGE_WEATHER: DrawWeather(dl,o,s); break;
    case WK_CLOCK:    WClock(dl,o,s); break;
    case WK_CALENDAR: WCalendar(dl,o,s); break;
    case WK_WEATHER:  WWeather(dl,o,s); break;
    case WK_PROFILE:  WProfile(dl,o,s); break;
    case WK_VOLUME:   WVolume(dl,o,s); break;
    case WK_CPU:      HeroCard(dl,o,s,"CPU",W2U8(g_st.cpuName),g_st.cpuUsage,g_st.cpuTemp,COL_GOLD,g_st.cpuHist); break;
    case WK_GPU:      HeroCard(dl,o,s,"GPU",W2U8(g_st.gpuName),g_st.gpuUsage,g_st.gpuTemp,COL_GOLD,g_st.gpuHist); break;
    case WK_MEMORY:   GaugeCard(dl,o,s,"Memory",g_st.memTotal? (float)g_st.memUsed/g_st.memTotal:0,
                                GiB(g_st.memUsed)+" / "+GiB(g_st.memTotal)+" GiB",nullptr,g_st.memHist); break;
    case WK_STORAGE:  GaugeCard(dl,o,s,"Storage",g_st.diskTotal? (float)g_st.diskUsed/g_st.diskTotal:0,
                                GiB(g_st.diskUsed)+" / "+GiB(g_st.diskTotal)+" GiB","C:",g_st.diskHist); break;
    case WK_NETWORK:  NetworkCard(dl,o,s); break;
    case WK_MEDIA:    WMediaCard(dl,o,s); break;
    case WK_IMAGE:      DrawImgWidget(dl,o,V(o.x+s.x,o.y+s.y),w.arg,false,g_cardRound); break;
    case WK_IMAGE_FILL: DrawImgWidget(dl,o,V(o.x+s.x,o.y+s.y),w.arg,true, g_cardRound); break;
    case WK_TEXT:     WText(dl,o,s,w.arg); break;
    case WK_M3_CPU:   M3Gauge(dl,o,s,"CPU",(float)g_st.cpuUsage,TempSub(g_st.cpuTemp),g_m3Shape,9440); break;
    case WK_M3_GPU:   M3Gauge(dl,o,s,"GPU",(float)g_st.gpuUsage,TempSub(g_st.gpuTemp),g_m3Shape,9441); break;
    case WK_M3_MEM:   M3Gauge(dl,o,s,"RAM",g_st.memTotal?(float)g_st.memUsed/g_st.memTotal:0,
                              GiB(g_st.memUsed)+" / "+GiB(g_st.memTotal),g_m3Shape,9442); break;
    case WK_RING_CPU:  RingGauge(dl,o,s,0); break;
    case WK_RING_MEM:  RingGauge(dl,o,s,1); break;
    case WK_RING_DISK: RingGauge(dl,o,s,2); break;
    case WK_M3_DISK:  M3Gauge(dl,o,s,"DISK",g_st.diskTotal?(float)g_st.diskUsed/g_st.diskTotal:0,
                              GiB(g_st.diskUsed)+" / "+GiB(g_st.diskTotal),g_m3Shape,9443); break;
    default: break;
    }
}

static float EaseOutCubic(float t){ float u=1-t; return 1-u*u*u; }
static float EaseOutBack(float t){ const float c=1.70158f; float u=t-1; return 1+ (c+1)*u*u*u + c*u*u; }   // slight overshoot pop
static float EaseInCubic(float t){ return t*t*t; }

// ---- drawer geometry: the Panel descriptor is now the ONE source of truth (the reveal logic in
//      the loop calls PanelRect too, so the two can never disagree) ----
static const float DRAWER_TABH = 54.0f;

// ---- edit mode: drag to move, corner to resize, X to delete, snap to a grid ----
static int   g_wDrag=-1;         // widget being dragged (-1 none)
static bool  g_wResizing=false;
static ImVec2 g_wGrab=V(0,0);    // cursor offset within the widget at grab time
static void EditPickImage(Widget& w){
    wchar_t file[MAX_PATH]={0};
    OPENFILENAMEW ofn={}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_hwnd;
    ofn.lpstrFilter=L"Images\0*.gif;*.png;*.jpg;*.jpeg;*.webp;*.bmp\0All files\0*.*\0";
    ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&ofn)){ w.arg=W2U8(file); SaveConfig(); }
}
static int g_wStyleOpen=-1;          // which widget's style popover is open in edit mode
static void EditOverlay(ImDrawList* dl,ImGuiIO& io,DashTab& t,ImVec2 o,ImVec2 area){
    bool down=io.MouseDown[0], click=io.MouseClicked[0], rel=io.MouseReleased[0];
    // ---- the style popover takes its clicks before anything underneath ----
    const float POPW=520.0f, POPH=std::min(236.0f,std::max(200.0f,area.y));   // two columns: fits a short dashboard
    ImVec2 popA(0,0), popB(0,0); bool popOn=false;
    if(g_wStyleOpen>=0 && g_wStyleOpen<(int)t.widgets.size()){
        Widget& w=t.widgets[g_wStyleOpen];
        ImVec2 wa=V(o.x+w.x*area.x,o.y+w.y*area.y), wb=V(wa.x+w.w*area.x,wa.y+w.h*area.y);
        float px = (wb.x+10+POPW < o.x+area.x)? wb.x+10 : std::max(o.x, wa.x-10-POPW);
        float py = std::clamp(wa.y, o.y, std::max(o.y, o.y+area.y-POPH));
        popA=V(px,py); popB=V(px+POPW,py+POPH); popOn=true;
    } else g_wStyleOpen=-1;
    bool inPop = popOn && io.MousePos.x>=popA.x&&io.MousePos.x<popB.x&&io.MousePos.y>=popA.y&&io.MousePos.y<popB.y;
    if(inPop){ click=false; }
    // faint grid
    const int GX=24, GY=12;
    for(int i=1;i<GX;i++){ float x=o.x+area.x*i/GX; dl->AddLine(V(x,o.y),V(x,o.y+area.y),WithA(COL_INK2,14),1); }
    for(int i=1;i<GY;i++){ float y=o.y+area.y*i/GY; dl->AddLine(V(o.x,y),V(o.x+area.x,y),WithA(COL_INK2,14),1); }
    auto snap=[&](float v,int n){ return roundf(v*n)/n; };

    // service an in-progress drag/resize first
    if(g_wDrag>=0 && g_wDrag<(int)t.widgets.size() && down){
        Widget& w=t.widgets[g_wDrag];
        if(g_wResizing){
            w.w=std::clamp((io.MousePos.x-(o.x+w.x*area.x))/area.x,0.05f,1.0f-w.x);
            w.h=std::clamp((io.MousePos.y-(o.y+w.y*area.y))/area.y,0.05f,1.0f-w.y);
        } else {
            w.x=std::clamp((io.MousePos.x-o.x-g_wGrab.x)/area.x,0.0f,1.0f-w.w);
            w.y=std::clamp((io.MousePos.y-o.y-g_wGrab.y)/area.y,0.0f,1.0f-w.h);
        }
    }
    if(rel && g_wDrag>=0){
        Widget& w=t.widgets[g_wDrag];
        w.x=std::clamp(snap(w.x,GX),0.0f,1.0f); w.y=std::clamp(snap(w.y,GY),0.0f,1.0f);
        w.w=std::clamp(snap(w.w,GX),0.05f,1.0f-w.x); w.h=std::clamp(snap(w.h,GY),0.05f,1.0f-w.y);
        g_wDrag=-1; g_wResizing=false; SaveConfig();
    }

    // draw handles per widget (top-most first for hit priority)
    bool consumed=false;
    for(int i=(int)t.widgets.size()-1;i>=0;i--){
        Widget& w=t.widgets[i];
        ImVec2 a=V(o.x+w.x*area.x,o.y+w.y*area.y), b=V(a.x+w.w*area.x,a.y+w.h*area.y);
        bool sel=(g_editSel==i);
        dl->AddRect(a,b,sel?COL_GOLD:WithA(COL_INK2,120),g_cardRound,0,sel?2.0f:1.2f);
        // resize grip (bottom-right)
        dl->AddTriangleFilled(V(b.x-14,b.y),V(b.x,b.y-14),V(b.x,b.y),sel?COL_GOLD:WithA(COL_INK2,150));
        // delete (top-right)
        ImVec2 xc=V(b.x-12,a.y+12);
        bool xhov=fabsf(io.MousePos.x-xc.x)<11&&fabsf(io.MousePos.y-xc.y)<11;
        dl->AddCircleFilled(xc,10,xhov?IM_COL32(226,86,86,255):WithA(COL_INK2,120));
        dl->AddLine(V(xc.x-4,xc.y-4),V(xc.x+4,xc.y+4),IM_COL32(255,255,255,235),1.8f);
        dl->AddLine(V(xc.x+4,xc.y-4),V(xc.x-4,xc.y+4),IM_COL32(255,255,255,235),1.8f);
        // label of the kind
        const char* kn = (w.kind>=0&&w.kind<WK_COUNT)? WREG[w.kind].label : "?";
        dl->AddRectFilled(V(a.x+6,a.y+6),V(a.x+12+TextW(g_fSml,12,kn),a.y+24),WithA(COL_INK,g_darkUI?150:120),6);
        TextAt(dl,g_fSml,12,V(a.x+9,a.y+8),IM_COL32(255,255,255,220),kn);

        if(consumed) continue;
        bool inBody=io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;
        bool inGrip=io.MousePos.x>b.x-16&&io.MousePos.x<b.x&&io.MousePos.y>b.y-16&&io.MousePos.y<b.y;
        // style (brush) button, left of the delete button
        ImVec2 sc=V(b.x-36,a.y+12);
        bool shov=fabsf(io.MousePos.x-sc.x)<11&&fabsf(io.MousePos.y-sc.y)<11;
        dl->AddCircleFilled(sc,10,(shov||g_wStyleOpen==i)? COL_GOLD : WithA(COL_INK2,120));
        MsIcon(dl,"palette",sc,14,IM_COL32(255,255,255,235));
        if(click && shov){ g_wStyleOpen = (g_wStyleOpen==i)? -1 : i; g_editSel=i; consumed=true; continue; }
        if(click && xhov){ t.widgets.erase(t.widgets.begin()+i); g_editSel=-1; g_wStyleOpen=-1; SaveConfig(); consumed=true; continue; }
        if(click && (inBody||inGrip)){
            g_editSel=i; g_wDrag=i; g_wResizing=inGrip;
            g_wGrab=V(io.MousePos.x-a.x, io.MousePos.y-a.y);
            // a double-ish click on an image/text widget lets you change its source
            consumed=true;
        }
        if(inBody) consumed=true;   // don't let clicks fall through to widgets below
    }
    // ---- the popover ----
    if(popOn){
        Widget& w=t.widgets[g_wStyleOpen];
        WStyle st=ParseWStyle(w.style); bool ch=false, commit=false;
        bool pc=io.MouseClicked[0] && inPop;
        dl->AddRectFilled(popA,popB,g_darkUI? IM_COL32(24,24,30,246) : IM_COL32(248,248,252,248),16);
        dl->AddRect(popA,popB,WithA(COL_INK2,50),16,0,1.0f);
        float px=popA.x+16, py=popA.y+12, pw=234.0f;              // left column
        TextAt(dl,g_fMed,16,V(px,py),COL_INK,"Widget style");
        { ImVec2 xc=V(popB.x-20,popA.y+22); bool xh=fabsf(io.MousePos.x-xc.x)<11&&fabsf(io.MousePos.y-xc.y)<11;
          MsIcon(dl,"close",xc,18,xh?COL_GOLD:COL_INK2); if(pc&&xh){ g_wStyleOpen=-1; return; } }
        py+=26;
        auto chips=[&](const char* label,const char** opts,int n,int cur)->int{
            TextAt(dl,g_fSml,12,V(px,py),COL_INK2,label); py+=16; int hit=-1; float cx=px;
            for(int k=0;k<n;k++){ float tw=TextW(g_fSml,13,opts[k])+18;
                bool h=io.MousePos.x>cx&&io.MousePos.x<cx+tw&&io.MousePos.y>py&&io.MousePos.y<py+24;
                dl->AddRectFilled(V(cx,py),V(cx+tw,py+24),k==cur? AccA(200) : WithA(COL_INK2,h?60:30),12);
                TextAt(dl,g_fSml,13,V(cx+9,py+4),k==cur? M3OnPrimary() : COL_INK,opts[k]);
                if(pc&&h) hit=k; cx+=tw+5; }
            py+=30; return hit; };
        auto slider=[&](const char* label,float& v,float lo,float hi,const char* fmt,bool allowOff)->void{
            char vb[32]; if(allowOff && v<0) snprintf(vb,32,"theme"); else snprintf(vb,32,fmt,v);
            TextAt(dl,g_fSml,12,V(px,py),COL_INK2,label); TextAt(dl,g_fSml,12,V(px+pw-TextW(g_fSml,12,vb),py),COL_GOLD,vb); py+=18;
            float t0=px, t1=px+pw, ty=py+6; float tv= (allowOff&&v<0)? 0.0f : std::clamp((v-lo)/(hi-lo),0.0f,1.0f);
            dl->AddRectFilled(V(t0,ty),V(t1,ty+5),WithA(COL_INK2,60),3);
            dl->AddRectFilled(V(t0,ty),V(t0+(t1-t0)*tv,ty+5),COL_GOLD,3);
            dl->AddCircleFilled(V(t0+(t1-t0)*tv,ty+2.5f),7,COL_GOLD);
            static int dragging=-1; int myId=(int)(py);
            if(io.MouseClicked[0] && io.MousePos.y>ty-10&&io.MousePos.y<ty+14&&io.MousePos.x>t0-8&&io.MousePos.x<t1+8) dragging=myId;
            if(dragging==myId && io.MouseDown[0]){ v=lo+(hi-lo)*std::clamp((io.MousePos.x-t0)/(t1-t0),0.0f,1.0f); ch=true; }
            if(dragging==myId && io.MouseReleased[0]){ dragging=-1; commit=true; }
            py+=22; };
        // background
        { static const char* BG[]={"Card","None","Colour","Image"};
          int cur = !st.image.empty()? 3 : (st.bg=="none"? 1 : (!st.color.empty()? 2 : 0));
          int hit=chips("Background",BG,4,cur);
          if(hit==0){ st.bg=""; st.color=""; st.image=""; ch=commit=true; }
          if(hit==1){ st.bg="none"; st.color=""; st.image=""; ch=commit=true; }
          if(hit==2){ st.bg=""; if(st.color.empty()) st.color="card2"; st.image=""; ch=commit=true; }   // a container tone: text on it stays readable
          if(hit==3){ wchar_t file[MAX_PATH]={0}; OPENFILENAMEW ofn={}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_hwnd;
              ofn.lpstrFilter=L"Images\0*.gif;*.png;*.jpg;*.jpeg;*.webp;*.bmp\0All files\0*.*\0"; ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
              ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
              if(GetOpenFileNameW(&ofn)){ st.image=W2U8(file); ch=commit=true; } } }
        if(!st.color.empty() && st.image.empty()){
            static const char* SW[]={"primary","secondary","tertiary","card2","ink","album","#00000055"};
            for(int k=0;k<7;k++){ ImVec2 cc=V(px+11+k*32,py+11);
                bool h=fabsf(io.MousePos.x-cc.x)<12&&fabsf(io.MousePos.y-cc.y)<12;
                dl->AddCircleFilled(cc,10,CwColor(SW[k],COL_CARD));
                dl->AddCircle(cc,11.5f,st.color==SW[k]? COL_GOLD : WithA(COL_INK2,h?160:60),0,2.0f);
                if(pc&&h){ st.color=SW[k]; ch=commit=true; } }
            py+=28;
        }
        // shape
        { int si=st.shape.empty()? -1 : M3ShapeFromName(st.shape);
          TextAt(dl,g_fSml,12,V(px,py),COL_INK2,"Shape"); py+=14;
          ImVec2 l=V(px+12,py+14), r=V(px+pw-12,py+14);
          bool lh=fabsf(io.MousePos.x-l.x)<14&&fabsf(io.MousePos.y-l.y)<14, rh=fabsf(io.MousePos.x-r.x)<14&&fabsf(io.MousePos.y-r.y)<14;
          MsIcon(dl,"chevron_left",l,22,lh?COL_GOLD:COL_INK2); MsIcon(dl,"chevron_right",r,22,rh?COL_GOLD:COL_INK2);
          if(si>=0) M3ShapeMorph(dl,V(px+48,py+14),13,COL_GOLD,si,si,0,0);
          const char* nm = si>=0? M3_SHAPE_LABEL[si] : "None (rounded card)";
          TextAt(dl,g_fSml,14,V(px+70,py+5),COL_INK,nm);
          if(pc&&lh){ si = si<0? M3_COUNT-1 : si-1; st.shape = si<0? "" : M3_SHAPE_ID[si]; ch=commit=true; }
          if(pc&&rh){ si = si+1>=M3_COUNT? -1 : si+1; st.shape = si<0? "" : M3_SHAPE_ID[si]; ch=commit=true; }
          py+=32; }
        px=popA.x+16+pw+22; py=popA.y+40;                         // right column
        slider("Corner radius",st.radius,0,64,"%.0f px",true);
        slider("Opacity",st.opacity,0.1f,1.0f,"%.2f",false);
        slider("Padding",st.pad,0,48,"%.0f px",false);
        // footer buttons
        { float bw=(pw-8)/2; py=popB.y-42;
          auto btn=[&](float x,const char* l,const char* ic)->bool{ bool h=io.MousePos.x>x&&io.MousePos.x<x+bw&&io.MousePos.y>py&&io.MousePos.y<py+30;
              dl->AddRectFilled(V(x,py),V(x+bw,py+30),WithA(COL_INK2,h?64:34),10);
              MsIcon(dl,ic,V(x+18,py+15),16,COL_INK); TextAt(dl,g_fSml,13,V(x+32,py+7),COL_INK,l); return pc&&h; };
          if(btn(px,"Reset style","restart_alt")){ st=WStyle(); ch=commit=true; }
          if(w.kind==WK_CUSTOM){ if(btn(px+bw+8,"Edit file","edit")) AetherShellExec(nullptr,L"open",L"notepad.exe",(L"\""+U82W(CwDir()+w.arg)+L"\"").c_str(),nullptr,SW_SHOWNORMAL); }
          else if(w.kind==WK_IMAGE||w.kind==WK_IMAGE_FILL){ if(btn(px+bw+8,"Change image","image")) EditPickImage(w); } }
        if(ch){ w.style=BuildWStyle(st); }
        if(commit) SaveConfig();
    }
}
// ---- break a built-in page apart into the individual cards it draws -------------------------
// The four stock tabs each held ONE page widget covering the whole tab. Edit mode could therefore
// move exactly one thing, which is why the dashboard only had a single possible arrangement: the
// layout lived in DrawDashboard's C++, not in the config. Exploding the page swaps that one widget
// for the real cards at the same coordinates the page draws them, so from then on every card can be
// dragged, resized, removed or replaced - Caelestia's model, where the layout is data.
static bool PageExplodes(int kind){
    return kind==WK_PAGE_DASH||kind==WK_PAGE_PERF||kind==WK_PAGE_MEDIA||kind==WK_PAGE_WEATHER;
}
static void ExplodePage(DashTab& t,int idx){
    if(idx<0||idx>=(int)t.widgets.size()) return;
    const int kind=t.widgets[idx].kind;
    if(!PageExplodes(kind)) return;
    std::vector<Widget> out;
    auto add=[&](int k,float x,float y,float w,float h){ Widget n; n.kind=k; n.x=x; n.y=y; n.w=w; n.h=h; out.push_back(n); };
    if(kind==WK_PAGE_DASH){
        // the same four zones DrawDashboard lays out, as fractions (its c1..c4 with a 0.012 gutter)
        const float g=0.012f;
        const float c1=0.185f, c2=0.350f, c3=0.110f;
        const float x1=0.0f, x2=x1+c1+g, x3=x2+c2+g, x4=x3+c3+g, c4=1.0f-x4;
        add(WK_WEATHER , x1,0.000f,c1,0.380f);
        add(WK_CLOCK   , x1,0.392f,c1,0.608f);
        add(WK_PROFILE , x2,0.000f,c2,0.300f);
        add(WK_CALENDAR, x2,0.312f,c2,0.688f);
        // the ARC gauges, which is what that column actually draws - shape gauges here made the
        // broken-apart page look like a different dashboard rather than the same one
        add(WK_RING_CPU , x3,0.000f,c3,0.327f);
        add(WK_RING_MEM , x3,0.337f,c3,0.327f);
        add(WK_RING_DISK, x3,0.673f,c3,0.327f);
        add(WK_MEDIA   , x4,0.000f,c4,1.000f);
    } else if(kind==WK_PAGE_PERF){
        add(WK_CPU,0.00f,0.00f,0.49f,0.48f);  add(WK_GPU    ,0.51f,0.00f,0.49f,0.48f);
        add(WK_MEMORY,0.00f,0.52f,0.32f,0.48f); add(WK_STORAGE,0.34f,0.52f,0.32f,0.48f);
        add(WK_NETWORK,0.68f,0.52f,0.32f,0.48f);
    } else if(kind==WK_PAGE_MEDIA){
        add(WK_MEDIA,0.28f,0.04f,0.44f,0.92f);
    } else {
        add(WK_WEATHER,0.30f,0.10f,0.40f,0.50f);
    }
    t.widgets.erase(t.widgets.begin()+idx);
    t.widgets.insert(t.widgets.end(),out.begin(),out.end());
    // The desktop can MIRROR one tab onto the wallpaper, and it skips full-page widgets - so
    // pointing that mirror at a stock tab showed nothing at all, and picking it was a no-op. Break
    // the tab apart and that no-op silently becomes a wallpaper covered in giant duplicates of
    // everything in the drawer. Detach the mirror instead; Settings > Desktop can re-point it.
    if(!g_deskTabName.empty() && g_deskTabName==t.name){ g_deskTabName.clear(); g_deskDirty=true; }
}
static bool EditBtn(ImDrawList* dl,ImGuiIO& io,ImVec2 a,ImVec2 b,const char* label,bool accent){
    bool hov=io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;
    ImU32 fill = accent? AccA(hov?70:52) : WithA(COL_INK2,hov?70:40);
    dl->AddRectFilled(a,b,fill,9);
    if(accent) dl->AddRect(a,b,WithA(COL_GOLD,120),9,0,1.2f);
    TextAt(dl,g_fSml,14,V((a.x+b.x)/2-TextW(g_fSml,14,label)/2,(a.y+b.y)/2-8),accent?COL_GOLD:COL_INK,label);
    return hov && io.MouseClicked[0];
}
static void EditToolbar(ImDrawList* dl,ImGuiIO& io,ImVec2 dorg,ImVec2 dsz){
    float h=44, y=dorg.y+dsz.y-h-10, x=dorg.x+18;
    float bw=130, gap=8;
    if(EditBtn(dl,io,V(x,y),V(x+bw,y+h),"+ Add widget",true)) g_wPaletteOpen=true; x+=bw+gap;
    if(g_tab>=0&&g_tab<(int)g_tabs.size()){
        DashTab& t=g_tabs[g_tab];
        char icb[32]; snprintf(icb,32,"Icon: %d",t.icon+1);
        if(EditBtn(dl,io,V(x,y),V(x+96,y+h),icb,false)){ t.icon=(t.icon+1)%4; SaveConfig(); } x+=96+gap;
        if(g_editSel>=0 && g_editSel<(int)t.widgets.size()){
            int wk=t.widgets[g_editSel].kind;
            if(wk==WK_IMAGE||wk==WK_IMAGE_FILL){ if(EditBtn(dl,io,V(x,y),V(x+150,y+h),"Choose image\xE2\x80\xA6",false)) EditPickImage(t.widgets[g_editSel]); x+=150+gap; }
        }
        // A stock page is one indivisible widget, so there is nothing to arrange until it is broken
        // up. Offer that on the page itself - selected, or as the tab's only widget.
        { int pi=-1;
          if(g_editSel>=0 && g_editSel<(int)t.widgets.size() && PageExplodes(t.widgets[g_editSel].kind)) pi=g_editSel;
          else if(t.widgets.size()==1 && PageExplodes(t.widgets[0].kind)) pi=0;
          if(pi>=0){
              if(EditBtn(dl,io,V(x,y),V(x+150,y+h),"Break apart",true)){
                  ExplodePage(t,pi); g_editSel=-1; SaveConfig(); }
              x+=150+gap;
          } else if(t.builtin){
              // the way back, so breaking a stock tab apart is not a one-way door. The icon is what
              // DefaultTabs pairs each stock page with, so it names the page to put back.
              if(EditBtn(dl,io,V(x,y),V(x+150,y+h),"Restore page",false)){
                  t.widgets.clear();
                  Widget p; p.kind=WK_PAGE_DASH+std::clamp(t.icon,0,3); p.x=p.y=0.0f; p.w=p.h=1.0f;
                  t.widgets.push_back(p); g_editSel=-1; SaveConfig(); }
              x+=150+gap;
          } }
        if(g_tabs.size()>1 && !t.builtin){
            if(EditBtn(dl,io,V(x,y),V(x+110,y+h),"Delete tab",false)){
                g_tabs.erase(g_tabs.begin()+g_tab); if(g_tab>=(int)g_tabs.size())g_tab=(int)g_tabs.size()-1;
                g_editSel=-1; SaveConfig(); } x+=110+gap;
        }
    }
    float dw2=90; float dx2=dorg.x+dsz.x-dw2-18;
    if(EditBtn(dl,io,V(dx2,y),V(dx2+dw2,y+h),"Done",true)){ g_editTab=false; g_wPaletteOpen=false; g_editSel=-1; SaveConfig(); }
    TextAt(dl,g_fSml,12,V(dorg.x+18,y-18),COL_INK2,"Drag to move \xE2\x80\xA2 corner to resize \xE2\x80\xA2 X to remove");
}
static void WidgetPalette(ImDrawList* dl,ImGuiIO& io,ImVec2 dorg,ImVec2 dsz,ImVec2 carea){
    // dim + a centred grid of the available widgets, grouped by category
    dl->AddRectFilled(dorg,V(dorg.x+dsz.x,dorg.y+dsz.y),IM_COL32(0,0,0,120));
    float pw=std::min(dsz.x-80,760.0f), ph=std::min(dsz.y-40,560.0f);
    ImVec2 a=V(dorg.x+(dsz.x-pw)/2, dorg.y+(dsz.y-ph)/2), b=V(a.x+pw,a.y+ph);
    GlassPanel(dl,a,b,18,0,nullptr,1.0f);
    TextAt(dl,g_fMed,20,V(a.x+22,a.y+16),COL_INK,"Add a widget");
    ImVec2 xc=V(b.x-24,a.y+24); bool xh=fabsf(io.MousePos.x-xc.x)<14&&fabsf(io.MousePos.y-xc.y)<14;
    dl->AddLine(V(xc.x-6,xc.y-6),V(xc.x+6,xc.y+6),COL_INK,2); dl->AddLine(V(xc.x+6,xc.y-6),V(xc.x-6,xc.y+6),COL_INK,2);
    if(xh&&io.MouseClicked[0]) g_wPaletteOpen=false;

    const char* cats[]={"Pages","Widgets","Caelestia","Performance","Media"};
    float y=a.y+54, x0=a.x+22; int col=0; const int COLS=3; float cw=(pw-44-(COLS-1)*10)/COLS, chh=44;
    static float s_palScroll=0;
    if(io.MouseWheel!=0 && io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y) s_palScroll=std::max(0.0f,s_palScroll-io.MouseWheel*40.0f);
    y-=s_palScroll;
    dl->PushClipRect(V(a.x,a.y+46),V(b.x,b.y-6),true);
    for(const char* cat:cats){
        TextAt(dl,g_fSml,13,V(x0,y),COL_GOLD,cat); y+=22; col=0;
        for(int i=0;i<WK_COUNT;i++){
            if(strcmp(WREG[i].cat,cat)!=0) continue;
            float cx=x0+col*(cw+10), cyy=y;
            bool hov=io.MousePos.x>cx&&io.MousePos.x<cx+cw&&io.MousePos.y>cyy&&io.MousePos.y<cyy+chh;
            dl->AddRectFilled(V(cx,cyy),V(cx+cw,cyy+chh),WithA(COL_INK2,hov?60:32),9);
            TextAt(dl,g_fSml,14,V(cx+12,cyy+13),COL_INK,WREG[i].label);
            if(hov&&io.MouseClicked[0] && g_tab>=0 && g_tab<(int)g_tabs.size()){
                Widget w; w.kind=i; w.w=WREG[i].dw; w.h=WREG[i].dh;
                w.x=std::clamp(0.30f,0.0f,1.0f-w.w); w.y=std::clamp(0.30f,0.0f,1.0f-w.h);
                if(i==WK_TEXT) w.arg="Text";
                g_tabs[g_tab].widgets.push_back(w);
                g_editSel=(int)g_tabs[g_tab].widgets.size()-1;
                if(WREG[i].wantsArg && (i==WK_IMAGE||i==WK_IMAGE_FILL)) EditPickImage(g_tabs[g_tab].widgets.back());
                SaveConfig(); g_wPaletteOpen=false;
            }
            if(++col>=COLS){ col=0; y+=chh+8; }
        }
        if(col!=0) y+=chh+8;
        y+=6;
    }
    // ---- Custom: every .toml / .py in config\widgets ----
    { TextAt(dl,g_fSml,13,V(x0,y),COL_GOLD,"Custom  \xC2\xB7  config\\widgets  (TOML and Python)"); y+=22; col=0;
      std::vector<std::string> files=CwList();
      std::vector<std::string> entries=files; entries.push_back("\x01open"); entries.push_back("\x01new");
      for(const std::string& f:entries){
          float cx=x0+col*(cw+10), cyy=y;
          bool inView = cyy>a.y+46 && cyy+chh<b.y-6;
          bool hov=inView && io.MousePos.x>cx&&io.MousePos.x<cx+cw&&io.MousePos.y>cyy&&io.MousePos.y<cyy+chh;
          dl->AddRectFilled(V(cx,cyy),V(cx+cw,cyy+chh),WithA(f[0]=='\x01'? COL_GOLD : COL_INK2,hov?60:32),9);
          std::string label; const char* icon="widgets";
          if(f=="\x01open"){ label="Open widgets folder"; icon="folder_open"; }
          else if(f=="\x01new"){ label="New widget\xE2\x80\xA6"; icon="add"; }
          else { CwDef* d=CwGet(f); label=d&&!d->name.empty()? d->name : f; icon= (d&&d->py)? "code" : "tune"; }
          MsIcon(dl,icon,V(cx+22,cyy+chh*0.5f),20,COL_INK2);
          TextAt(dl,g_fSml,14,V(cx+40,cyy+7),COL_INK,Clip(g_fSml,14,label,cw-48).c_str());
          if(f[0]!='\x01') TextAt(dl,g_fSml,11,V(cx+40,cyy+25),COL_INK2,Clip(g_fSml,11,f,cw-48).c_str());
          if(hov&&io.MouseClicked[0]){
              if(f=="\x01open") AetherShellExec(nullptr,L"explore",U82W(CwDir()).c_str(),nullptr,nullptr,SW_SHOWNORMAL);
              else if(f=="\x01new"){
                  // copy the TOML template to a free name and open it for editing
                  char nm[64]; int k=1; std::string path;
                  do{ snprintf(nm,64,"my-widget-%d.toml",k++); path=CwDir()+nm; } while(GetFileAttributesA(path.c_str())!=INVALID_FILE_ATTRIBUTES && k<500);
                  std::ifstream src(CwDir()+"example-clock.toml",std::ios::binary); std::ofstream dst(path,std::ios::binary);
                  if(src&&dst) dst<<src.rdbuf();
                  dst.close();
                  AetherShellExec(nullptr,L"open",L"notepad.exe",(L"\""+U82W(path)+L"\"").c_str(),nullptr,SW_SHOWNORMAL);
              } else if(g_tab>=0 && g_tab<(int)g_tabs.size()){
                  CwDef* d=CwGet(f);
                  Widget w; w.kind=WK_CUSTOM; w.arg=f; w.w=d? std::clamp(d->w,0.05f,1.0f) : 0.3f; w.h=d? std::clamp(d->h,0.05f,1.0f) : 0.4f;
                  w.x=std::clamp(0.30f,0.0f,1.0f-w.w); w.y=std::clamp(0.30f,0.0f,1.0f-w.h);
                  g_tabs[g_tab].widgets.push_back(w);
                  g_editSel=(int)g_tabs[g_tab].widgets.size()-1;
                  SaveConfig(); g_wPaletteOpen=false;
              }
          }
          if(++col>=COLS){ col=0; y+=chh+8; }
      }
      if(col!=0) y+=chh+8; }
    dl->PopClipRect();
}

// The dashboard drawer HANGS FROM THE TRUE SCREEN TOP and overlays everything, so it reads as
// being outside/above the desktop bubble: rounded BOTTOM corners only, flush with y=0 when open.
// How tall the drawer wants to be for the active tab, as a fraction of its full height. Caelestia
// morphs the panel to fit each tab's content (Dashboard tall, Media short, Weather/Perf medium).
static float DrawerHeightFrac(){
    int t=TabSlot(); if(t<0||t>=(int)g_tabs.size()) return 1.0f;
    const DashTab& tab=g_tabs[t];
    if(!tab.widgets.empty()){
        switch(tab.widgets[0].kind){
            // Measured off clear1.png: the rice's Media panel is ~455px of a 1079px screen — i.e. the
            // FULL configured drawer height, not the 58% I had it at, which squashed the cover art and
            // pushed the source row out of view.
            case WK_PAGE_MEDIA:   return 1.00f;
            case WK_PAGE_PERF:    return 0.82f;
            case WK_PAGE_WEATHER: return 1.00f;
            case WK_PAGE_DASH:    return 1.00f;
            default: break;
        }
    }
    return 1.00f;   // custom widget tabs use the full height
}
// Caelestia's Wrapper.qml springs implicitWIDTH as well as implicitHeight - the Media popout is
// visibly wider than the others. We only ever morphed the height, so every tab sat at the same
// width and the narrow ones (Weather especially) had a lot of dead panel either side. These are
// fractions of the CONFIGURED drawer span, so the user's own width setting still decides the scale.
static float DrawerWidthFrac(){
    int t=TabSlot(); if(t<0||t>=(int)g_tabs.size()) return 1.0f;
    const DashTab& tab=g_tabs[t];
    if(!tab.widgets.empty()){
        switch(tab.widgets[0].kind){
            case WK_PAGE_MEDIA:   return 1.00f;   // the widest page: art + text column + transport
            case WK_PAGE_PERF:    return 1.00f;   // four gauge cards across
            case WK_PAGE_DASH:    return 0.88f;
            case WK_PAGE_WEATHER: return 0.74f;   // one column of forecast rows
            default: break;
        }
    }
    return 1.00f;
}
