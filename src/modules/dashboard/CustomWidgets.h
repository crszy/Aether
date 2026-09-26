// src/modules/dashboard/CustomWidgets.h  —  Aether shell
// User-made dashboard widgets: declarative TOML files and live Python scripts, drawn by one renderer.
//
// Caelestia's widgets are Quickshell QML. Quickshell is a Qt/Wayland program with no Windows build, so its
// widgets cannot run here; this is the same idea rebuilt for Aether: a widget is a list of ITEMS (text,
// shapes, rings, bars, images/GIFs, icons, buttons...) whose properties can be bound to live data -
// "{cpu}", "{time:%H:%M}", "{media.title}" - and to expressions - "{cpu}*360". A TOML widget declares its
// items; a Python widget PRINTS its items as JSON whenever it wants and receives ticks and clicks on stdin,
// so it can compute anything Python can. Both are drawn with the shell's own theme colours and the full
// Material 3 shape set, and both can be placed, sized and restyled like any built-in widget.
//
// Files live in config\widgets\ (examples and a reference are written there on first run).
#pragma once
#include <deque>
#include <condition_variable>
#include <memory>

static void RunShellCommand(const std::string& n);   // fwd (IPC command dispatcher)

// =========================================================================================== tiny JSON
struct JV {
    int t=0;                       // 0 null, 1 bool, 2 number, 3 string, 4 array, 5 object
    bool b=false; double n=0; std::string s;
    std::vector<JV> a; std::vector<std::pair<std::string,JV>> o;
    const JV* get(const char* k) const { for(auto& kv:o) if(kv.first==k) return &kv.second; return nullptr; }
};
static void JSkipWs(const char*& p){ while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++; }
static void JUtf8(std::string& out,unsigned cp){
    if(cp<0x80) out+=(char)cp;
    else if(cp<0x800){ out+=(char)(0xC0|(cp>>6)); out+=(char)(0x80|(cp&0x3F)); }
    else if(cp<0x10000){ out+=(char)(0xE0|(cp>>12)); out+=(char)(0x80|((cp>>6)&0x3F)); out+=(char)(0x80|(cp&0x3F)); }
    else { out+=(char)(0xF0|(cp>>18)); out+=(char)(0x80|((cp>>12)&0x3F)); out+=(char)(0x80|((cp>>6)&0x3F)); out+=(char)(0x80|(cp&0x3F)); }
}
static bool JHex4(const char* p,unsigned& v){ v=0; for(int i=0;i<4;i++){ char c=p[i]; int d;
    if(c>='0'&&c<='9') d=c-'0'; else if(c>='a'&&c<='f') d=c-'a'+10; else if(c>='A'&&c<='F') d=c-'A'+10; else return false;
    v=v*16+(unsigned)d; } return true; }
static bool JStr(const char*& p,std::string& out){
    if(*p!='"') return false; p++;
    while(*p && *p!='"'){
        if(*p=='\\'){
            p++;
            switch(*p){
            case 'n': out+='\n'; break; case 't': out+='\t'; break; case 'r': out+='\r'; break;
            case 'b': out+='\b'; break; case 'f': out+='\f'; break;
            case 'u': { unsigned cp=0; if(!JHex4(p+1,cp)) return false; p+=4;
                        if(cp>=0xD800 && cp<0xDC00 && p[1]=='\\' && p[2]=='u'){ unsigned lo=0;
                            if(JHex4(p+3,lo) && lo>=0xDC00 && lo<0xE000){ cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00); p+=6; } }
                        JUtf8(out,cp); } break;
            case 0: return false;
            default: out+=*p;
            }
            p++;
        } else out+=*p++;
    }
    if(*p!='"') return false; p++; return true;
}
static bool JVal(const char*& p,JV& v,int depth){
    if(depth>64) return false;
    JSkipWs(p);
    if(*p=='{'){ v.t=5; p++; JSkipWs(p); if(*p=='}'){ p++; return true; }
        for(;;){ JSkipWs(p); std::string k; if(!JStr(p,k)) return false; JSkipWs(p); if(*p!=':') return false; p++;
            JV c; if(!JVal(p,c,depth+1)) return false; v.o.emplace_back(std::move(k),std::move(c)); JSkipWs(p);
            if(*p==','){ p++; continue; } if(*p=='}'){ p++; return true; } return false; } }
    if(*p=='['){ v.t=4; p++; JSkipWs(p); if(*p==']'){ p++; return true; }
        for(;;){ JV c; if(!JVal(p,c,depth+1)) return false; v.a.push_back(std::move(c)); JSkipWs(p);
            if(*p==','){ p++; continue; } if(*p==']'){ p++; return true; } return false; } }
    if(*p=='"'){ v.t=3; return JStr(p,v.s); }
    if(!strncmp(p,"true",4)){ v.t=1; v.b=true; p+=4; return true; }
    if(!strncmp(p,"false",5)){ v.t=1; v.b=false; p+=5; return true; }
    if(!strncmp(p,"null",4)){ v.t=0; p+=4; return true; }
    char* e=nullptr; double d=strtod(p,&e); if(e==p) return false; v.t=2; v.n=d; p=e; return true;
}
static bool JParse(const std::string& s,JV& out){ const char* p=s.c_str(); out=JV(); return JVal(p,out,0); }
static std::string JEsc(const std::string& s){
    std::string o; o.reserve(s.size()+8);
    for(unsigned char c:s){ switch(c){ case '"': o+="\\\""; break; case '\\': o+="\\\\"; break; case '\n': o+="\\n"; break;
        case '\r': o+="\\r"; break; case '\t': o+="\\t"; break;
        default: if(c<0x20){ char b[8]; snprintf(b,8,"\\u%04x",c); o+=b; } else o+=(char)c; } }
    return o;
}

// =========================================================================================== model
struct CwItem {
    std::string type, id;
    std::vector<std::pair<std::string,std::string>> p;     // property -> raw text (may hold {bindings})
    const std::string* get(const char* k) const { for(auto& kv:p) if(kv.first==k) return &kv.second; return nullptr; }
};
struct CwSource { std::string name, cmd; int intervalMs=5000; ULONGLONG last=0; std::shared_ptr<std::string> out=std::make_shared<std::string>(); std::shared_ptr<std::atomic<bool>> busy=std::make_shared<std::atomic<bool>>(false); };
struct CwDef {
    std::string file, path, name, desc, err;
    bool py=false; FILETIME mtime{}; ULONGLONG checked=0; bool loaded=false;
    float w=0.30f, h=0.40f;                               // default size in the tab, as fractions
    int refresh=1000;
    std::string bg="card", shape, image, color;
    float radius=-1, padding=0;
    std::vector<CwItem> items;
    std::vector<CwSource> sources;
};
static std::unordered_map<std::string,CwDef> g_cwDefs;
static std::string CwDir(){ return CfgDir()+"widgets\\"; }
static std::mutex g_cwSrcMtx;

// =========================================================================================== data bindings
struct CwCtx { ImVec2 o, s; float t=0; bool hover=false; CwDef* def=nullptr; const std::unordered_map<std::string,double>* vars=nullptr; };
static std::string CwNowStr(const char* fmt){
    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn); char b[128]; if(!strftime(b,sizeof(b),fmt,&lt)) b[0]=0; return b;
}
static bool CwNumBinding(const std::string& n,const CwCtx& c,double& v){
    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
    auto frac=[](unsigned long long a,unsigned long long b){ return b? (double)a/(double)b : 0.0; };
    if(n=="cpu"){ v=g_st.cpuUsage; return true; }
    if(n=="gpu"){ v=g_st.gpuUsage; return true; }
    if(n=="mem"||n=="memory"){ v=frac(g_st.memUsed,g_st.memTotal); return true; }
    if(n=="disk"||n=="storage"){ v=frac(g_st.diskUsed,g_st.diskTotal); return true; }
    if(n=="cpu_temp"){ v=g_st.cpuTemp; return true; }
    if(n=="gpu_temp"){ v=g_st.gpuTemp; return true; }
    if(n=="mem_used"){ v=g_st.memUsed/1073741824.0; return true; }
    if(n=="mem_total"){ v=g_st.memTotal/1073741824.0; return true; }
    if(n=="disk_used"){ v=g_st.diskUsed/1073741824.0; return true; }
    if(n=="disk_total"){ v=g_st.diskTotal/1073741824.0; return true; }
    if(n=="net_down"){ v=g_st.netDown/1024.0; return true; }
    if(n=="net_up"){ v=g_st.netUp/1024.0; return true; }
    if(n=="battery"){ v=g_st.hasBattery? g_st.battPct/100.0 : -1; return true; }
    if(n=="charging"){ v=g_st.charging?1:0; return true; }
    if(n=="online"){ v=g_st.online?1:0; return true; }
    if(n=="t"){ v=c.t; return true; }
    if(n=="w"){ v=c.s.x; return true; }
    if(n=="h"){ v=c.s.y; return true; }
    if(n=="hover"){ v=c.hover?1:0; return true; }
    if(n=="hour"){ v=lt.tm_hour; return true; }
    if(n=="minute"){ v=lt.tm_min; return true; }
    if(n=="second"){ v=lt.tm_sec; return true; }
    if(n=="day"){ v=lt.tm_mday; return true; }
    if(n=="month"){ v=lt.tm_mon+1; return true; }
    if(n=="year"){ v=lt.tm_year+1900; return true; }
    if(n=="weekday"){ v=lt.tm_wday; return true; }
    if(n=="pi"){ v=3.14159265358979; return true; }
    if(n=="media.playing"){ v=g_md.playing?1:0; return true; }
    if(n=="media.has"){ v=(g_md.has&&!g_md.title.empty())?1:0; return true; }
    if(n=="media.pos"){ v=MediaPos(); return true; }
    if(n=="media.dur"){ v=g_md.dur; return true; }
    if(n=="media.progress"){ v=g_md.dur>0? std::clamp(MediaPos()/g_md.dur,0.0,1.0) : 0.0; return true; }
    if(n=="weather.temp"){ v=g_wx.ok? g_wx.temp : 0; return true; }
    if(n=="weather.feels"){ v=g_wx.ok? g_wx.feels : 0; return true; }
    if(n=="weather.humidity"){ v=g_wx.ok? g_wx.hum : 0; return true; }
    if(n=="weather.wind"){ v=g_wx.ok? g_wx.wind : 0; return true; }
    if(c.vars){ auto it=c.vars->find(n); if(it!=c.vars->end()){ v=it->second; return true; } }
    if(n.rfind("src.",0)==0 && c.def){
        std::lock_guard<std::mutex> lk(g_cwSrcMtx);
        for(auto& sc:c.def->sources) if(sc.name==n.substr(4)){ v=atof(sc.out->c_str()); return true; } }
    return false;
}
static bool CwStrBinding(const std::string& n,const std::string& fmt,const CwCtx& c,std::string& out){
    // the profile (Profile.h)
    if(n=="name"){ out=ProfileDisplayName(); return true; }
    if(n=="status"){ out=g_status; return true; }
    if(n=="status_icon"){ out=g_statusIcon; return true; }
    if(n=="presence"){ out=ProfilePresenceLabel(ProfilePresenceNow()); return true; }
    if(n=="os"){ out=ProfileOsName(); return true; }
    if(n=="wm"){ out=ProfileWmName(); return true; }
    if(n=="shell"){ out="Aether"; return true; }
    if(n=="uptime_long"){ out=ProfileUptimeLong(); return true; }
    if(n=="time"){ out=CwNowStr(fmt.empty()? (g_clock24? "%H:%M" : "%I:%M %p") : fmt.c_str()); return true; }
    if(n=="date"){ out=CwNowStr(fmt.empty()? "%A, %d %B" : fmt.c_str()); return true; }
    if(n=="media.title"){ out=g_md.title; return true; }
    if(n=="media.artist"){ out=g_md.artist; return true; }
    if(n=="media.album"){ out=g_md.album; return true; }
    if(n=="media.source"){ out=g_mdSource; return true; }
    auto mmss=[](double s){ char b[16]; snprintf(b,16,"%d:%02d",(int)s/60,(int)s%60); return std::string(b); };
    if(n=="media.pos_str"){ out=mmss(MediaPos()); return true; }
    if(n=="media.dur_str"){ out=mmss(g_md.dur); return true; }
    if(n=="lyric"){ std::lock_guard<std::mutex> lk(g_lyricsMtx); float p=0; int i=LyricCurrent(g_lyrics,MediaPos(),p);
        out = (i>=0 && i<(int)g_lyrics.size())? g_lyrics[i].text : std::string(); return true; }
    if(n=="weather.text"){ out=g_wx.ok? WxText(g_wx.code) : ""; return true; }
    if(n=="weather.city"){ out=g_wx.city; return true; }
    if(n=="weather.icon"){ out=WxMaterial(g_wx.code,!g_wx.isDay); return true; }
    if(n=="cpu_name"){ out=W2U8(g_st.cpuName); return true; }
    if(n=="gpu_name"){ out=W2U8(g_st.gpuName); return true; }
    if(n=="user"){ wchar_t u[256]; DWORD k=256; out = GetUserNameW(u,&k)? W2U8(u) : ""; return true; }
    if(n=="host"){ wchar_t u[256]; DWORD k=256; out = GetComputerNameW(u,&k)? W2U8(u) : ""; return true; }
    if(n=="uptime"){ ULONGLONG sec=GetTickCount64()/1000; char b[48];
        if(sec>=86400) snprintf(b,48,"%llud %lluh",sec/86400,(sec%86400)/3600); else snprintf(b,48,"%lluh %llum",sec/3600,(sec%3600)/60);
        out=b; return true; }
    if(n.rfind("src.",0)==0 && c.def){
        std::lock_guard<std::mutex> lk(g_cwSrcMtx);
        for(auto& sc:c.def->sources) if(sc.name==n.substr(4)){ out=*sc.out; while(!out.empty()&&(out.back()=='\n'||out.back()=='\r')) out.pop_back(); return true; }
        out.clear(); return true; }
    return false;
}

// ---- expressions: numbers, bindings, + - * / % ( ), comparisons, ?: and a few functions ------------------
struct CwExpr {
    const char* p; const CwCtx& c; bool ok=true;
    CwExpr(const char* s,const CwCtx& cc):p(s),c(cc){}
    void ws(){ while(*p==' '||*p=='\t') p++; }
    double prim(){
        ws();
        if(*p=='('){ p++; double v=tern(); ws(); if(*p==')') p++; return v; }
        if(*p=='-'){ p++; return -prim(); }
        if(*p=='+'){ p++; return prim(); }
        if(*p=='!'){ p++; return prim()==0?1:0; }
        if(*p=='{'){ p++; double v=tern(); ws(); if(*p=='}') p++; return v; }
        if(isdigit((unsigned char)*p)||*p=='.'){ char* e=nullptr; double v=strtod(p,&e); p=e; return v; }
        if(isalpha((unsigned char)*p)||*p=='_'){
            std::string id; while(isalnum((unsigned char)*p)||*p=='_'||*p=='.') id+=*p++;
            ws();
            if(*p=='('){ p++; std::vector<double> args; ws();
                if(*p!=')') for(;;){ args.push_back(tern()); ws(); if(*p==','){ p++; continue; } break; }
                if(*p==')') p++;
                auto A=[&](size_t i){ return i<args.size()? args[i] : 0.0; };
                if(id=="sin") return sin(A(0)); if(id=="cos") return cos(A(0)); if(id=="abs") return fabs(A(0));
                if(id=="min") return std::min(A(0),A(1)); if(id=="max") return std::max(A(0),A(1));
                if(id=="clamp") return std::clamp(A(0),std::min(A(1),A(2)),std::max(A(1),A(2)));
                if(id=="floor") return floor(A(0)); if(id=="round") return std::round(A(0)); if(id=="sqrt") return sqrt(std::max(0.0,A(0)));
                if(id=="mix"||id=="lerp") return A(0)+(A(1)-A(0))*A(2);
                if(id=="pulse") return 0.5+0.5*sin(A(0));
                if(id=="deg") return A(0)*57.2957795; if(id=="rad") return A(0)/57.2957795;
                return 0; }
            double v=0; if(CwNumBinding(id,c,v)) return v; ok=false; return 0;
        }
        ok=false; return 0;
    }
    double mul(){ double v=prim(); for(;;){ ws(); if(*p=='*'){ p++; v*=prim(); } else if(*p=='/'){ p++; double d=prim(); v= d!=0? v/d : 0; }
                  else if(*p=='%'){ p++; double d=prim(); v= d!=0? fmod(v,d) : 0; } else return v; } }
    double add(){ double v=mul(); for(;;){ ws(); if(*p=='+'){ p++; v+=mul(); } else if(*p=='-'){ p++; v-=mul(); } else return v; } }
    double cmp(){ double v=add(); ws();
        if(p[0]=='>'&&p[1]=='='){ p+=2; return v>=add(); } if(p[0]=='<'&&p[1]=='='){ p+=2; return v<=add(); }
        if(p[0]=='='&&p[1]=='='){ p+=2; return v==add(); } if(p[0]=='!'&&p[1]=='='){ p+=2; return v!=add(); }
        if(*p=='>'){ p++; return v>add(); } if(*p=='<'){ p++; return v<add(); } return v; }
    double tern(){ double v=cmp(); ws(); if(*p=='?'){ p++; double a=tern(); ws(); if(*p==':') p++; double b=tern(); return v!=0? a : b; } return v; }
};
static double CwEval(const std::string& s,const CwCtx& c,bool* okOut=nullptr){
    CwExpr e(s.c_str(),c); double v=e.tern(); if(okOut) *okOut=e.ok; return std::isfinite(v)? v : 0.0;
}
// "{cpu%}  {time:%H:%M}  {mem_used:%.1f} GiB" -> text
static std::string CwText(const std::string& tpl,const CwCtx& c){
    std::string out; out.reserve(tpl.size()+16);
    for(size_t i=0;i<tpl.size();i++){
        if(tpl[i]=='{' && i+1<tpl.size() && tpl[i+1]=='{'){ out+='{'; i++; continue; }
        if(tpl[i]!='{'){ out+=tpl[i]; continue; }
        size_t e=tpl.find('}',i); if(e==std::string::npos){ out+=tpl.substr(i); break; }
        std::string inner=tpl.substr(i+1,e-i-1); i=e;
        std::string name=inner, fmt; size_t colon=inner.find(':');
        if(colon!=std::string::npos){ name=inner.substr(0,colon); fmt=inner.substr(colon+1); }
        bool pct=false; if(!name.empty() && name.back()=='%'){ pct=true; name.pop_back(); }
        std::string sv;
        if(!pct && CwStrBinding(name,fmt,c,sv)){ out+=sv; continue; }
        bool ok=true; double v=CwEval(name,c,&ok);
        char b[64];
        if(pct) snprintf(b,64,fmt.empty()? "%.0f%%" : (fmt+"%%").c_str(),v*100.0);
        else if(!fmt.empty()) snprintf(b,64,fmt.c_str(),v);
        else if(fabs(v-std::round(v))<1e-9) snprintf(b,64,"%.0f",v);
        else snprintf(b,64,"%.2f",v);
        out+=b;
    }
    return out;
}
static std::string CwUnq(std::string s){ s=Tml::Trim(s);
    if(s.size()>=2 && ((s.front()=='"'&&s.back()=='"')||(s.front()=='\''&&s.back()=='\''))) return Tml::File::Unquote(s);
    return s; }
// a numeric property: 12, "50%" (of `base`), "{cpu}*100", "w/2 - 10"
static float CwNum(const CwItem& it,const char* k,float def,const CwCtx& c,float base=0){
    const std::string* r=it.get(k); if(!r) return def;
    std::string s=Tml::Trim(*r); if(s.empty()) return def;
    if(s.back()=='%'){ char* e=nullptr; double v=strtod(s.c_str(),&e); if(e && *e=='%') return (float)(base*v/100.0); }
    return (float)CwEval(s,c);
}
static bool CwBool(const CwItem& it,const char* k,bool def,const CwCtx& c){
    const std::string* r=it.get(k); if(!r) return def; std::string s=Tml::Trim(*r);
    if(s=="true"||s=="yes") return true; if(s=="false"||s=="no") return false; return CwEval(s,c)!=0; }
static std::string CwStr(const CwItem& it,const char* k,const std::string& def,const CwCtx& c){
    const std::string* r=it.get(k); return r? CwText(*r,c) : def; }

// ---- colours: theme roles, #hex, rgb(a) ------------------------------------------------------------------
static ImU32 CwColor(const std::string& s0,ImU32 def){
    std::string s=Tml::Trim(s0); if(s.empty()) return def;
    std::string l=s; for(auto& ch:l) ch=(char)tolower((unsigned char)ch);
    if(l=="primary"||l=="accent") return COL_GOLD;
    if(l=="ink"||l=="on_surface"||l=="text") return COL_INK;
    if(l=="ink2"||l=="on_surface_variant"||l=="subtext") return COL_INK2;
    if(l=="card"||l=="surface_container") return COL_CARD;
    if(l=="card2"||l=="surface_container_high") return COL_CARD2;
    if(l=="surface"||l=="panel") return PanelCol(255);
    if(l=="secondary") return M3Secondary();
    if(l=="tertiary") return M3Tertiary();
    if(l=="on_primary") return M3OnPrimary();
    if(l=="error") return COL_ERR;
    if(l=="album"){ ImU32 t=g_mdTint.load(); return t? t : COL_GOLD; }
    if(l=="white") return IM_COL32(255,255,255,255);
    if(l=="black") return IM_COL32(0,0,0,255);
    if(l=="transparent"||l=="none") return IM_COL32(0,0,0,0);
    if(l[0]=='#'){ std::string h=l.substr(1);
        if(h.size()==3){ std::string e; for(char ch:h){ e+=ch; e+=ch; } h=e; }
        unsigned long v=strtoul(h.c_str(),nullptr,16);
        if(h.size()==6) return IM_COL32((v>>16)&255,(v>>8)&255,v&255,255);
        if(h.size()==8) return IM_COL32((v>>24)&255,(v>>16)&255,(v>>8)&255,v&255);
        return def; }
    if(l.rfind("rgb",0)==0){ int r=0,g=0,b=0; float a=1; const char* q=strchr(l.c_str(),'(');
        if(q){ int n=sscanf(q+1,"%d , %d , %d , %f",&r,&g,&b,&a); if(n>=3) return IM_COL32(r,g,b,(int)(std::clamp(a<=1.0f? a*255.0f : a,0.0f,255.0f))); } }
    return def;
}
static ImU32 CwItemColor(const CwItem& it,const char* k,ImU32 def,const CwCtx& c,float alphaMul){
    const std::string* r=it.get(k); ImU32 col = r? CwColor(CwText(*r,c),def) : def;
    float a=CwNum(it,"alpha",1.0f,c)*alphaMul;
    return MulA(col,std::clamp(a,0.0f,1.0f));
}
static ImFont* CwFont(const std::string& f){
    if(f=="medium"||f=="semibold"||f=="bold") return g_fMed;
    if(f=="big"||f=="display") return g_fBig;
    if(f=="huge"||f=="light") return g_fHuge? g_fHuge : g_fBig;
    if(f=="mono") return g_fMono? g_fMono : g_fReg;
    if(f=="clock") return g_fClock? g_fClock : g_fBig;
    if(f=="small") return g_fSml;
    return g_fReg;
}

// =========================================================================================== actions
static void CwAction(const std::string& a0){
    std::string a=Tml::Trim(a0); if(a.empty()) return;
    if(a.rfind("shell:",0)==0){ RunShellCommand(a.substr(6)); return; }
    if(a.rfind("open:",0)==0){ AetherShellExec(nullptr,L"open",U82W(a.substr(5)).c_str(),nullptr,nullptr,SW_SHOWNORMAL); return; }
    if(a.rfind("run:",0)==0){
        std::wstring cmd=U82W(a.substr(4));
        std::thread([cmd]{ STARTUPINFOW si{}; si.cb=sizeof(si); PROCESS_INFORMATION pi{};
            std::vector<wchar_t> b(cmd.begin(),cmd.end()); b.push_back(0);
            if(CreateProcessW(nullptr,b.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)){ CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
            else AetherShellExec(nullptr,L"open",cmd.c_str(),nullptr,nullptr,SW_SHOWNORMAL); }).detach();
        return; }
    if(a=="media:play"||a=="media:toggle"){ g_reqPlay=1; return; }
    if(a=="media:next"){ g_reqNext=1; return; }
    if(a=="media:prev"||a=="media:previous"){ g_reqPrev=1; return; }
    if(a.rfind("tab:",0)==0){ std::string n=a.substr(4);
        for(int i=0;i<(int)g_tabs.size();i++) if(_stricmp(g_tabs[i].name.c_str(),n.c_str())==0){ g_tab=i; break; }
        return; }
    RunShellCommand(a);          // a bare word is an IPC command ("lock", "launcher", "wallpaper=live"...)
}

// =========================================================================================== rendering
// Draws `items` inside c.o/c.s. Returns the id of an item with no on_click that was clicked (for Python).
static std::string CwDrawItems(ImDrawList* dl,ImGuiIO& io,const std::vector<CwItem>& items,const CwCtx& c,float alphaMul){
    std::string clickedId;
    const float W=c.s.x, H=c.s.y;
    bool click=io.MouseClicked[0];
    for(const CwItem& it:items){
        if(it.get("visible") && !CwBool(it,"visible",true,c)) continue;
        const std::string& ty=it.type;
        float x=c.o.x+CwNum(it,"x",0,c,W), y=c.o.y+CwNum(it,"y",0,c,H);
        float iw=CwNum(it,"w",0,c,W), ih=CwNum(it,"h",0,c,H);
        ImU32 col=CwItemColor(it,"color",COL_INK,c,alphaMul);
        bool hovered=false;
        auto hitRect=[&](ImVec2 a,ImVec2 b){ hovered = io.MousePos.x>=a.x&&io.MousePos.x<b.x&&io.MousePos.y>=a.y&&io.MousePos.y<b.y; };
        if(ty=="text"){
            std::string txt=CwStr(it,"text","",c);
            float fs=CwNum(it,"size",15,c); ImFont* f=CwFont(CwStr(it,"font","regular",c));
            std::string al=CwStr(it,"align","left",c), va=CwStr(it,"valign","top",c);
            float maxw=CwNum(it,"max_width",0,c,W);
            std::vector<std::string> lines;
            if(CwBool(it,"wrap",false,c) && maxw>10){ WrapLines(f,fs,txt,maxw,(int)CwNum(it,"max_lines",6,c),lines); }
            else { if(maxw>10) txt=Clip(f,fs,txt,maxw); lines.push_back(txt); }
            float lh=fs*CwNum(it,"line_height",1.25f,c), th=lh*lines.size();
            if(va=="middle"||va=="center") y-=th*0.5f; else if(va=="bottom") y-=th;
            float bx0=x, bx1=x;
            for(size_t li=0;li<lines.size();li++){
                float tw=TextW(f,fs,lines[li].c_str()), tx=x;
                if(al=="center") tx=x-tw*0.5f; else if(al=="right") tx=x-tw;
                if(li==0){ bx0=tx; bx1=tx+tw; } else { bx0=std::min(bx0,tx); bx1=std::max(bx1,tx+tw); }
                if(it.get("glow")){ ImU32 gc=CwItemColor(it,"glow",COL_GOLD,c,alphaMul*0.35f);
                    for(int d=0;d<8;d++){ float an=d*0.785398f; TextAt(dl,f,fs,V(tx+cosf(an)*2.0f,y+li*lh+sinf(an)*2.0f),gc,lines[li].c_str()); } }
                TextAt(dl,f,fs,V(tx,y+li*lh),col,lines[li].c_str());
            }
            hitRect(V(bx0,y),V(bx1,y+th));
        } else if(ty=="rect"||ty=="button"){
            float rr=CwNum(it,"radius",0,c);
            ImVec2 a=V(x,y), b=V(x+iw,y+ih); hitRect(a,b);
            ImU32 fill = it.get("color")? col : (ty=="button"? IM_COL32(0,0,0,0) : col);
            if(hovered && it.get("hover_color")) fill=CwItemColor(it,"hover_color",fill,c,alphaMul);
            if((fill>>24)) dl->AddRectFilled(a,b,fill,rr);
            if(it.get("border_color")) dl->AddRect(a,b,CwItemColor(it,"border_color",COL_INK2,c,alphaMul),rr,0,CwNum(it,"border",1.0f,c));
            if(ty=="button" && it.get("text")){ float fs=CwNum(it,"size",14,c); ImFont* f=CwFont(CwStr(it,"font","medium",c));
                std::string t=CwStr(it,"text","",c); ImU32 tc=CwItemColor(it,"text_color",COL_INK,c,alphaMul);
                TextAt(dl,f,fs,V(x+iw*0.5f-TextW(f,fs,t.c_str())*0.5f,y+ih*0.5f-fs*0.62f),tc,t.c_str()); }
            if(ty=="button" && it.get("icon")){ float is=CwNum(it,"icon_size",std::min(iw,ih)*0.6f,c);
                MsIcon(dl,CwStr(it,"icon","",c),V(x+iw*0.5f,y+ih*0.5f),is,CwItemColor(it,"text_color",COL_INK,c,alphaMul)); }
        } else if(ty=="circle"){
            float r=CwNum(it,"radius",10,c,std::min(W,H)); float th=CwNum(it,"thickness",0,c);
            if(th>0) dl->AddCircle(V(x,y),r,col,0,th); else dl->AddCircleFilled(V(x,y),r,col);
            hovered = (io.MousePos.x-x)*(io.MousePos.x-x)+(io.MousePos.y-y)*(io.MousePos.y-y) <= r*r;
        } else if(ty=="shape"){
            float r=CwNum(it,"size",30,c,std::min(W,H));
            int sa=M3ShapeFromName(CwStr(it,"shape","circle",c)); if(sa<0) sa=0;
            int sb=it.get("morph_to")? M3ShapeFromName(CwStr(it,"morph_to","",c)) : sa; if(sb<0) sb=sa;
            float mt=std::clamp(CwNum(it,"morph",0,c),0.0f,1.0f);
            float spin=CwNum(it,"spin",0,c)*0.01745329f;
            if(it.get("image")){                                 // a picture cut to the shape
                M3ShapeMorph(dl,V(x,y),r,col,sa,sb,mt,spin);
                int vs=dl->VtxBuffer.Size;
                DrawImgWidget(dl,V(x-r,y-r),V(x+r,y+r),CwStr(it,"image","",c),true,r);
                (void)vs;
            } else M3ShapeMorph(dl,V(x,y),r,col,sa,sb,mt,spin);
            hovered = (io.MousePos.x-x)*(io.MousePos.x-x)+(io.MousePos.y-y)*(io.MousePos.y-y) <= r*r;
        } else if(ty=="ring"||ty=="arc"){
            float r=CwNum(it,"radius",30,c,std::min(W,H)), th=CwNum(it,"thickness",6,c);
            float v=std::clamp(CwNum(it,"value",0,c),0.0f,1.0f);
            float st=CwNum(it,"start",-90,c)*0.01745329f, sw=CwNum(it,"sweep",360,c)*0.01745329f;
            ImU32 track=CwItemColor(it,"track",WithA(COL_INK2,60),c,alphaMul);
            const int seg=std::max(12,(int)(fabsf(sw)*r/6.0f));
            if((track>>24)){ dl->PathArcTo(V(x,y),r,st,st+sw,seg); dl->PathStroke(track,0,th); }
            if(v>0.001f){ dl->PathArcTo(V(x,y),r,st,st+sw*v,std::max(4,(int)(seg*v))); dl->PathStroke(col,0,th);
                if(CwBool(it,"round_cap",true,c)){ dl->AddCircleFilled(V(x+cosf(st)*r,y+sinf(st)*r),th*0.5f,col);
                    dl->AddCircleFilled(V(x+cosf(st+sw*v)*r,y+sinf(st+sw*v)*r),th*0.5f,col); } }
            hovered = (io.MousePos.x-x)*(io.MousePos.x-x)+(io.MousePos.y-y)*(io.MousePos.y-y) <= (r+th)*(r+th);
        } else if(ty=="bar"){
            float v=std::clamp(CwNum(it,"value",0,c),0.0f,1.0f), rr=CwNum(it,"radius",std::min(iw,ih)*0.5f,c);
            ImU32 track=CwItemColor(it,"track",WithA(COL_INK2,60),c,alphaMul);
            ImVec2 a=V(x,y), b=V(x+iw,y+ih); hitRect(a,b);
            dl->AddRectFilled(a,b,track,rr);
            if(CwBool(it,"vertical",false,c)){ if(v>0) dl->AddRectFilled(V(a.x,b.y-ih*v),b,col,rr); }
            else if(v>0) dl->AddRectFilled(a,V(a.x+iw*v,b.y),col,rr);
        } else if(ty=="image"||ty=="gif"){
            ImVec2 a=V(x,y), b=V(x+(iw>0?iw:W),y+(ih>0?ih:H)); hitRect(a,b);
            DrawImgWidget(dl,a,b,CwStr(it,"path","",c),CwStr(it,"fit","cover",c)!="contain",CwNum(it,"radius",0,c));
        } else if(ty=="icon"){
            float sz=CwNum(it,"size",24,c);
            MsIcon(dl,CwStr(it,"name","help",c),V(x,y),sz,col);
            hitRect(V(x-sz*0.5f,y-sz*0.5f),V(x+sz*0.5f,y+sz*0.5f));
        } else if(ty=="line"){
            float x2=c.o.x+CwNum(it,"x2",0,c,W), y2=c.o.y+CwNum(it,"y2",0,c,H);
            dl->AddLine(V(x,y),V(x2,y2),col,CwNum(it,"thickness",1.5f,c));
        } else if(ty=="graph"){
            std::string src=CwStr(it,"source","cpu",c);
            const std::vector<float>* hv = src=="gpu"? &g_st.gpuHist : src=="mem"||src=="memory"? &g_st.memHist
                                         : src=="disk"? &g_st.diskHist : src=="net"? &g_st.netHist : &g_st.cpuHist;
            if(hv->size()>=2){
                float mx=1.0f; if(src=="net"){ mx=1.0f; for(float f:*hv) mx=std::max(mx,f); }
                std::vector<ImVec2> pts; size_t n=hv->size();
                for(size_t i=0;i<n;i++) pts.push_back(V(x+iw*(float)i/(n-1), y+ih-ih*std::clamp((*hv)[i]/mx,0.0f,1.0f)));
                if(CwBool(it,"fill",true,c)){ ImU32 fc=MulA(col,0.25f);
                    for(size_t i=0;i+1<n;i++){ ImVec2 q[4]={pts[i],pts[i+1],V(pts[i+1].x,y+ih),V(pts[i].x,y+ih)}; dl->AddConvexPolyFilled(q,4,fc); } }
                dl->AddPolyline(pts.data(),(int)n,col,0,CwNum(it,"thickness",2,c));
            }
            hitRect(V(x,y),V(x+iw,y+ih));
        }
        if(hovered && click){
            if(it.get("on_click")) CwAction(CwStr(it,"on_click","",c));
            else if(!it.id.empty()) clickedId=it.id;
        }
    }
    return clickedId;
}

// =========================================================================================== TOML widgets
static bool CwFileTime(const std::string& path,FILETIME& ft){
    WIN32_FILE_ATTRIBUTE_DATA d{}; if(!GetFileAttributesExW(U82W(path).c_str(),GetFileExInfoStandard,&d)) return false;
    ft=d.ftLastWriteTime; return true;
}
static void CwLoadToml(CwDef& d){
    Tml::File f; if(!f.load(d.path)){ d.err="cannot read "+d.file; return; }
    d.err.clear(); d.items.clear(); d.sources.clear();
    d.name=f.str("widget.name",d.file); d.desc=f.str("widget.description","");
    { std::vector<std::string> sz=f.array("widget.size");
      if(sz.size()==2){ d.w=(float)atof(sz[0].c_str()); d.h=(float)atof(sz[1].c_str()); if(d.w>1.0f) d.w/=100.0f; if(d.h>1.0f) d.h/=100.0f; } }
    d.refresh=(int)f.num("widget.refresh_ms",1000);
    d.bg=f.str("widget.background","card"); d.shape=f.str("widget.shape",""); d.image=f.str("widget.image","");
    d.color=f.str("widget.color",""); d.radius=(float)f.num("widget.radius",-1); d.padding=(float)f.num("widget.padding",0);
    for(int i=0;i<256;i++){
        std::string pre="item."+std::to_string(i)+".";
        bool any=false; CwItem it;
        for(auto it2=f.raw.lower_bound(pre); it2!=f.raw.end() && it2->first.compare(0,pre.size(),pre)==0; ++it2){
            any=true; std::string k=it2->first.substr(pre.size()); std::string v=CwUnq(it2->second);
            if(k=="type") it.type=v; else if(k=="id") it.id=v; else it.p.emplace_back(k,v); }
        if(!any) break;
        if(it.type.empty()) it.type="text";
        d.items.push_back(std::move(it));
    }
    for(int i=0;i<32;i++){
        std::string pre="source."+std::to_string(i)+".";
        if(!f.has(pre+"name")) break;
        CwSource sc; sc.name=f.str(pre+"name"); sc.cmd=f.str(pre+"command"); sc.intervalMs=(int)f.num(pre+"interval_ms",5000);
        d.sources.push_back(std::move(sc));
    }
    if(d.items.empty()) d.err="no [[item]] entries in "+d.file;
}
static void CwRunSource(CwSource& sc){
    if(sc.cmd.empty() || sc.busy->exchange(true)) return;
    auto out=sc.out; auto busy=sc.busy; std::wstring cmd=L"cmd.exe /c "+U82W(sc.cmd);
    std::thread([cmd,out,busy]{
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE}; HANDLE rd=nullptr,wr=nullptr;
        if(CreatePipe(&rd,&wr,&sa,1<<16)){
            SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0);
            STARTUPINFOW si{}; si.cb=sizeof(si); si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
            si.hStdOutput=wr; si.hStdError=wr; PROCESS_INFORMATION pi{};
            std::vector<wchar_t> b(cmd.begin(),cmd.end()); b.push_back(0);
            if(CreateProcessW(nullptr,b.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
                CloseHandle(wr); wr=nullptr; std::string acc; char buf[4096]; DWORD n=0;
                while(ReadFile(rd,buf,sizeof(buf),&n,nullptr) && n>0 && acc.size()<65536) acc.append(buf,n);
                WaitForSingleObject(pi.hProcess,5000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                std::lock_guard<std::mutex> lk(g_cwSrcMtx); *out=acc;
            }
            if(wr) CloseHandle(wr); CloseHandle(rd);
        }
        busy->store(false);
    }).detach();
}

// =========================================================================================== Python widgets
struct CwProc {
    HANDLE proc=nullptr, in=nullptr, out=nullptr;
    std::mutex m; std::vector<CwItem> items; std::deque<std::string> log; std::string status;
    std::unordered_map<std::string,std::string> cfg;           // background / shape / radius / padding from the script
    std::atomic<bool> alive{false}; std::atomic<int> refresh{500};
    ULONGLONG lastDraw=0, lastTick=0, started=0;
    std::mutex wm; std::condition_variable wcv; std::deque<std::string> wq; bool stop=false;
    std::thread rd, wr;
};
static std::unordered_map<std::string,std::unique_ptr<CwProc>> g_cwProcs;
static HANDLE g_cwJob=nullptr;
static std::wstring CwFindPython(){
    if(!g_cwPython.empty()) return U82W(g_cwPython);
    static std::wstring found; static bool done=false; if(done) return found; done=true;
    wchar_t pathv[32767]; DWORD n=GetEnvironmentVariableW(L"PATH",pathv,32767); if(!n||n>=32767) return found;
    std::wstring P(pathv);
    for(const wchar_t* exe : {L"python.exe",L"python3.exe",L"py.exe"}){
        size_t i=0;
        while(i<=P.size()){ size_t e=P.find(L';',i); std::wstring dir=P.substr(i,e==std::wstring::npos? std::wstring::npos : e-i);
            if(!dir.empty()){ if(dir.back()!=L'\\') dir+=L'\\'; std::wstring cand=dir+exe;
                // the Store's "App execution alias" stub opens the Microsoft Store instead of running anything
                bool stub = cand.find(L"\\WindowsApps\\")!=std::wstring::npos;
                if(!stub && GetFileAttributesW(cand.c_str())!=INVALID_FILE_ATTRIBUTES){ found=cand; return found; } }
            if(e==std::wstring::npos) break; i=e+1; }
    }
    return found;
}
static void CwProcStop(CwProc& p){
    { std::lock_guard<std::mutex> lk(p.wm); p.stop=true; } p.wcv.notify_all();
    if(p.proc){ TerminateProcess(p.proc,0); }
    if(p.in){ CloseHandle(p.in); p.in=nullptr; }
    if(p.rd.joinable()) p.rd.join();
    if(p.wr.joinable()) p.wr.join();
    if(p.out){ CloseHandle(p.out); p.out=nullptr; }
    if(p.proc){ CloseHandle(p.proc); p.proc=nullptr; }
    p.alive=false;
}
static void CwSend(CwProc& p,const std::string& line){
    std::lock_guard<std::mutex> lk(p.wm); if(p.wq.size()>64) p.wq.pop_front(); p.wq.push_back(line+"\n"); p.wcv.notify_one();
}
static void CwItemsFromJson(const JV& arr,std::vector<CwItem>& out){
    out.clear();
    for(const JV& o:arr.a){ if(o.t!=5) continue; CwItem it;
        for(auto& kv:o.o){
            std::string v;
            if(kv.second.t==3) v=kv.second.s;
            else if(kv.second.t==2){ char b[48]; snprintf(b,48,"%.6g",kv.second.n); v=b; }
            else if(kv.second.t==1) v=kv.second.b? "true" : "false";
            else continue;
            if(kv.first=="type") it.type=v; else if(kv.first=="id") it.id=v; else it.p.emplace_back(kv.first,v);
        }
        if(it.type.empty()) it.type="text";
        // literal text from Python must not be read as a {binding}
        if(it.type=="text"||it.type=="button") for(auto& kv:it.p) if(kv.first=="text"){
            std::string e; for(char ch:kv.second){ if(ch=='{') e+="{{"; else e+=ch; } kv.second=e; }
        out.push_back(std::move(it)); }
}
static bool CwProcStart(CwProc& p,const CwDef& d){
    std::wstring py=CwFindPython();
    if(py.empty()){ p.status="Python was not found. Install it from python.org, or set widgets.python to python.exe."; return false; }
    if(!g_cwJob){ g_cwJob=CreateJobObjectW(nullptr,nullptr);
        if(g_cwJob){ JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{}; li.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(g_cwJob,JobObjectExtendedLimitInformation,&li,sizeof(li)); } }
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE inR=nullptr,inW=nullptr,outR=nullptr,outW=nullptr;
    if(!CreatePipe(&inR,&inW,&sa,1<<16)) return false;
    if(!CreatePipe(&outR,&outW,&sa,1<<20)){ CloseHandle(inR); CloseHandle(inW); return false; }
    SetHandleInformation(inW,HANDLE_FLAG_INHERIT,0); SetHandleInformation(outR,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOW si{}; si.cb=sizeof(si); si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
    si.hStdInput=inR; si.hStdOutput=outW; si.hStdError=outW;
    PROCESS_INFORMATION pi{};
    std::wstring cmd=L"\""+py+L"\" -u \""+U82W(d.path)+L"\"";
    SetEnvironmentVariableW(L"PYTHONIOENCODING",L"utf-8");
    SetEnvironmentVariableW(L"PYTHONUNBUFFERED",L"1");
    SetEnvironmentVariableW(L"AETHER_WIDGETS",U82W(CwDir()).c_str());
    std::vector<wchar_t> b(cmd.begin(),cmd.end()); b.push_back(0);
    BOOL ok=CreateProcessW(nullptr,b.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,U82W(CwDir()).c_str(),&si,&pi);
    CloseHandle(inR); CloseHandle(outW);
    if(!ok){ CloseHandle(inW); CloseHandle(outR); p.status="could not start Python"; return false; }
    if(g_cwJob) AssignProcessToJobObject(g_cwJob,pi.hProcess);
    ResumeThread(pi.hThread); CloseHandle(pi.hThread);
    p.proc=pi.hProcess; p.in=inW; p.out=outR; p.alive=true; p.stop=false; p.started=GetTickCount64(); p.status="starting";
    CwProc* pp=&p;
    p.rd=std::thread([pp]{
        std::string acc; char buf[8192]; DWORD n=0;
        while(ReadFile(pp->out,buf,sizeof(buf),&n,nullptr) && n>0){
            acc.append(buf,n);
            size_t nlp;
            while((nlp=acc.find('\n'))!=std::string::npos){
                std::string line=acc.substr(0,nlp); acc.erase(0,nlp+1);
                if(!line.empty()&&line.back()=='\r') line.pop_back();
                if(line.empty()) continue;
                JV v;
                if(line[0]=='{' && JParse(line,v) && v.t==5){
                    std::lock_guard<std::mutex> lk(pp->m);
                    if(const JV* its=v.get("items")){ if(its->t==4){ CwItemsFromJson(*its,pp->items); pp->status="ok"; } }
                    if(const JV* cf=v.get("config")){ if(cf->t==5) for(auto& kv:cf->o){
                        if(kv.first=="refresh_ms" && kv.second.t==2) pp->refresh=std::clamp((int)kv.second.n,16,60000);
                        else if(kv.second.t==3) pp->cfg[kv.first]=kv.second.s;
                        else if(kv.second.t==2){ char nb[32]; snprintf(nb,32,"%g",kv.second.n); pp->cfg[kv.first]=nb; } } }
                    if(const JV* lg=v.get("log")){ if(lg->t==3){ pp->log.push_back(lg->s); if(pp->log.size()>40) pp->log.pop_front(); } }
                } else {
                    std::lock_guard<std::mutex> lk(pp->m);
                    pp->log.push_back(line); if(pp->log.size()>40) pp->log.pop_front();
                }
            }
            if(acc.size()>4*1024*1024) acc.clear();
        }
        pp->alive=false;
        std::lock_guard<std::mutex> lk(pp->m); if(pp->status!="stopped") pp->status="exited";
    });
    p.wr=std::thread([pp]{
        for(;;){
            std::string line;
            { std::unique_lock<std::mutex> lk(pp->wm);
              pp->wcv.wait(lk,[pp]{ return pp->stop || !pp->wq.empty(); });
              if(pp->stop) return;
              line=std::move(pp->wq.front()); pp->wq.pop_front(); }
            DWORD w=0; if(!pp->in || !WriteFile(pp->in,line.data(),(DWORD)line.size(),&w,nullptr)) return;
        }
    });
    return true;
}
static std::string CwThemeJson(){
    auto hex=[](ImU32 c){ char b[16]; snprintf(b,16,"#%02x%02x%02x%02x",(c>>IM_COL32_R_SHIFT)&255,(c>>IM_COL32_G_SHIFT)&255,(c>>IM_COL32_B_SHIFT)&255,(c>>IM_COL32_A_SHIFT)&255); return std::string(b); };
    return "{\"primary\":\""+hex(COL_GOLD)+"\",\"ink\":\""+hex(COL_INK)+"\",\"ink2\":\""+hex(COL_INK2)+"\",\"card\":\""+hex(COL_CARD)+
           "\",\"secondary\":\""+hex(M3Secondary())+"\",\"tertiary\":\""+hex(M3Tertiary())+"\",\"dark\":"+(g_darkUI?"true":"false")+"}";
}
static double CwFin(double v){ return std::isfinite(v)? v : 0.0; }   // json has no nan/inf: one of them made Python drop every tick
static std::string CwTickJson(const CwCtx& c,ImGuiIO& io){
    char b[1024];
    snprintf(b,sizeof(b),"{\"type\":\"tick\",\"w\":%.1f,\"h\":%.1f,\"t\":%.3f,\"mouse\":{\"x\":%.1f,\"y\":%.1f,\"inside\":%s,\"down\":%s},"
        "\"stats\":{\"cpu\":%.4f,\"gpu\":%.4f,\"mem\":%.4f,\"disk\":%.4f,\"cpu_temp\":%.1f,\"gpu_temp\":%.1f,\"mem_used_gb\":%.2f,\"mem_total_gb\":%.2f,"
        "\"net_down_kbps\":%.1f,\"net_up_kbps\":%.1f,\"battery\":%d,\"charging\":%s},",
        CwFin(c.s.x),CwFin(c.s.y),CwFin(c.t), CwFin(io.MousePos.x-c.o.x), CwFin(io.MousePos.y-c.o.y), c.hover?"true":"false", io.MouseDown[0]?"true":"false",
        CwFin(g_st.cpuUsage),CwFin(g_st.gpuUsage), g_st.memTotal? (double)g_st.memUsed/g_st.memTotal : 0.0, g_st.diskTotal? (double)g_st.diskUsed/g_st.diskTotal : 0.0,
        CwFin(g_st.cpuTemp),CwFin(g_st.gpuTemp),g_st.memUsed/1073741824.0,g_st.memTotal/1073741824.0,CwFin(g_st.netDown/1024.0),CwFin(g_st.netUp/1024.0),
        g_st.hasBattery? g_st.battPct : -1, g_st.charging?"true":"false");
    std::string s=b;
    char m2[160]; snprintf(m2,sizeof(m2),",\"playing\":%s,\"pos\":%.2f,\"dur\":%.2f}",g_md.playing?"true":"false",CwFin(MediaPos()),CwFin(g_md.dur));
    s+="\"media\":{\"title\":\""+JEsc(g_md.title)+"\",\"artist\":\""+JEsc(g_md.artist)+"\",\"album\":\""+JEsc(g_md.album)+"\""+m2+",";
    char w2[160]; snprintf(w2,sizeof(w2),",\"temp\":%.1f,\"code\":%d,\"ok\":%s}",CwFin(g_wx.temp),g_wx.code,g_wx.ok?"true":"false");
    s+="\"weather\":{\"city\":\""+JEsc(g_wx.city)+"\",\"text\":\""+JEsc(g_wx.ok? WxText(g_wx.code) : "")+"\""+w2+",";
    s+="\"theme\":"+CwThemeJson()+"}";
    return s;
}

// =========================================================================================== definitions
static CwDef* CwGet(const std::string& file){
    if(file.empty()) return nullptr;
    CwDef& d=g_cwDefs[file];
    ULONGLONG now=GetTickCount64();
    if(d.loaded && now-d.checked<1000) return &d;
    d.checked=now;
    d.file=file; d.path=CwDir()+file;
    std::string low=file; for(auto& ch:low) ch=(char)tolower((unsigned char)ch);
    d.py = low.size()>3 && low.compare(low.size()-3,3,".py")==0;
    FILETIME ft{};
    if(!CwFileTime(d.path,ft)){ d.err="missing: config\\widgets\\"+file; d.loaded=true; return &d; }
    if(!d.loaded || CompareFileTime(&ft,&d.mtime)!=0){
        d.mtime=ft;
        if(d.py){ d.name=file; d.err.clear();
            // a Python widget is restarted when its file changes, so edits show up immediately
            for(auto& kv:g_cwProcs) if(kv.first.find("|"+file)!=std::string::npos && kv.second) { CwProcStop(*kv.second); kv.second->status="reloading"; }
            // the header comment may carry a name/size:  # aether: name="CPU orb" size=[0.2,0.4]
            FILE* fp=_wfopen(U82W(d.path).c_str(),L"rb");
            if(fp){ char line[512]; for(int li=0;li<12 && fgets(line,sizeof(line),fp);li++){
                    const char* k=strstr(line,"aether:"); if(!k) continue;
                    const char* nm=strstr(k,"name=\""); if(nm){ const char* e=strchr(nm+6,'"'); if(e) d.name.assign(nm+6,e); }
                    const char* sz=strstr(k,"size=["); if(sz){ float a=0,b=0; if(sscanf(sz+6,"%f , %f",&a,&b)==2){ d.w=a>1?a/100:a; d.h=b>1?b/100:b; } } }
                fclose(fp); }
        }
        else CwLoadToml(d);
        d.loaded=true;
    }
    return &d;
}
static std::vector<std::string> CwList(){
    std::vector<std::string> out;
    WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW(U82W(CwDir()+"*").c_str(),&fd);
    if(h==INVALID_HANDLE_VALUE) return out;
    do{ if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string n=W2U8(fd.cFileName); std::string l=n; for(auto& ch:l) ch=(char)tolower((unsigned char)ch);
        bool toml = l.size()>5 && l.compare(l.size()-5,5,".toml")==0;
        bool py   = l.size()>3 && l.compare(l.size()-3,3,".py")==0 && l!="aether.py";
        if(toml||py) out.push_back(n);
    } while(FindNextFileW(h,&fd));
    FindClose(h); std::sort(out.begin(),out.end()); return out;
}
// stop Python widgets nobody has drawn for a while (dashboard closed, widget removed)
static void CwReap(){
    ULONGLONG now=GetTickCount64();
    for(auto it=g_cwProcs.begin(); it!=g_cwProcs.end();){
        if(it->second && now-it->second->lastDraw>45000){ CwProcStop(*it->second); it=g_cwProcs.erase(it); }
        else ++it;
    }
}
static void CwShutdown(){ for(auto& kv:g_cwProcs) if(kv.second) CwProcStop(*kv.second); g_cwProcs.clear(); if(g_cwJob){ CloseHandle(g_cwJob); g_cwJob=nullptr; } }

// background of a custom widget: card | none | #colour | image path | a shape
static void CwBackground(ImDrawList* dl,ImVec2 o,ImVec2 s,const std::string& bg,const std::string& shape,const std::string& image,
                         const std::string& color,float radius){
    float rnd = radius>=0? radius : g_cardRound;
    if(!shape.empty()){ int si=M3ShapeFromName(shape); if(si>=0){
        float R=std::min(s.x,s.y)*0.5f; ImU32 c=color.empty()? COL_CARD : CwColor(color,COL_CARD);
        M3ShapeMorph(dl,V(o.x+s.x*0.5f,o.y+s.y*0.5f),R,c,si,si,0,0);
        if(!image.empty()){ DrawImgWidget(dl,V(o.x+s.x*0.5f-R,o.y+s.y*0.5f-R),V(o.x+s.x*0.5f+R,o.y+s.y*0.5f+R),image,true,R); }
        return; } }
    if(!image.empty()){ DrawImgWidget(dl,o,V(o.x+s.x,o.y+s.y),image,true,rnd); return; }
    std::string b=bg; for(auto& ch:b) ch=(char)tolower((unsigned char)ch);
    if(b=="none"||b=="transparent") return;
    if(b=="card"||b.empty()){ float keep=g_cardRound; g_cardRound=rnd; Card(dl,o,s, color.empty()? COL_CARD : CwColor(color,COL_CARD)); g_cardRound=keep; return; }
    dl->AddRectFilled(o,V(o.x+s.x,o.y+s.y),CwColor(bg,COL_CARD),rnd);
}

// Draw a placed custom widget. `inst` identifies this placement (so two copies get two Python processes).
static void CwDraw(ImDrawList* dl,ImGuiIO& io,const std::string& file,const std::string& inst,ImVec2 o,ImVec2 s){
    CwDef* d=CwGet(file);
    CwCtx c; c.o=o; c.s=s; c.t=(float)((double)GetTickCount64()/1000.0); c.def=d;
    c.hover = io.MousePos.x>=o.x&&io.MousePos.x<o.x+s.x&&io.MousePos.y>=o.y&&io.MousePos.y<o.y+s.y;
    dl->PushClipRect(o,V(o.x+s.x,o.y+s.y),true);
    if(!d || (!d->err.empty() && !d->py)){
        Card(dl,o,s);
        TextAt(dl,g_fMed,15,V(o.x+14,o.y+12),COL_INK,"Custom widget");
        std::vector<std::string> ls; WrapLines(g_fSml,13,d? d->err : std::string("no file"),s.x-28,6,ls);
        for(size_t i=0;i<ls.size();i++) TextAt(dl,g_fSml,13,V(o.x+14,o.y+36+i*17),COL_ERR,ls[i].c_str());
        dl->PopClipRect(); return;
    }
    if(!d->py){
        ULONGLONG now=GetTickCount64();
        for(auto& sc:d->sources) if(now-sc.last>=(ULONGLONG)std::max(250,sc.intervalMs)){ sc.last=now; CwRunSource(sc); }
        CwBackground(dl,o,s,d->bg,d->shape,d->image,d->color,d->radius);
        CwCtx ci=c; ci.o=V(o.x+d->padding,o.y+d->padding); ci.s=V(s.x-d->padding*2,s.y-d->padding*2);
        CwDrawItems(dl,io,d->items,ci,1.0f);
        dl->PopClipRect(); return;
    }
    // ---- Python ----
    std::unique_ptr<CwProc>& up=g_cwProcs[inst];
    if(!up) up.reset(new CwProc());
    CwProc& p=*up;
    ULONGLONG now=GetTickCount64();
    p.lastDraw=now;
    if(!p.alive && (p.started==0 || now-p.started>4000)){           // start, or restart a crashed script after a pause
        CwProcStop(p); { std::lock_guard<std::mutex> lk(p.m); p.items.clear(); }
        CwProcStart(p,*d);
        if(p.alive){ char b[96]; snprintf(b,96,"{\"type\":\"init\",\"w\":%.1f,\"h\":%.1f,\"theme\":",s.x,s.y); CwSend(p,std::string(b)+CwThemeJson()+"}"); p.lastTick=0; }
    }
    std::vector<CwItem> items; std::unordered_map<std::string,std::string> cfg; std::string status; std::string lastLog;
    { std::lock_guard<std::mutex> lk(p.m); items=p.items; cfg=p.cfg; status=p.status; if(!p.log.empty()) lastLog=p.log.back(); }
    auto cfgS=[&](const char* k,const std::string& dv){ auto it=cfg.find(k); return it==cfg.end()? dv : it->second; };
    float pad=(float)atof(cfgS("padding","0").c_str());
    CwBackground(dl,o,s,cfgS("background","card"),cfgS("shape",""),cfgS("image",""),cfgS("color",""),(float)atof(cfgS("radius","-1").c_str()));
    if(p.alive && now-p.lastTick>=(ULONGLONG)p.refresh.load()){ p.lastTick=now; CwSend(p,CwTickJson(c,io)); }
    CwCtx ci=c; ci.o=V(o.x+pad,o.y+pad); ci.s=V(s.x-pad*2,s.y-pad*2);
    std::string hitId=CwDrawItems(dl,io,items,ci,1.0f);
    if(p.alive){
        if(c.hover && (io.MouseClicked[0]||io.MouseClicked[1]||io.MouseClicked[2])){
            char b[200]; snprintf(b,sizeof(b),"{\"type\":\"click\",\"x\":%.1f,\"y\":%.1f,\"button\":%d,\"id\":\"%s\"}",
                io.MousePos.x-ci.o.x,io.MousePos.y-ci.o.y, io.MouseClicked[0]?0:io.MouseClicked[1]?1:2, JEsc(hitId).c_str());
            CwSend(p,b); p.lastTick=0; }
        if(c.hover && io.MouseWheel!=0){ char b[96]; snprintf(b,96,"{\"type\":\"scroll\",\"dy\":%.2f}",io.MouseWheel); CwSend(p,b); p.lastTick=0; }
    }
    if(items.empty()){
        std::string msg = !p.alive? (status.empty()? "not running" : status) : "waiting for the script\xE2\x80\xA6";
        TextAt(dl,g_fMed,15,V(o.x+14,o.y+12),COL_INK,d->name.c_str());
        std::vector<std::string> ls; WrapLines(g_fSml,13,msg+(lastLog.empty()? "" : "\n"+lastLog),s.x-28,8,ls);
        for(size_t i=0;i<ls.size();i++) TextAt(dl,g_fSml,13,V(o.x+14,o.y+36+i*17),(!p.alive? COL_ERR : COL_INK2),ls[i].c_str());
    }
    dl->PopClipRect();
}

static void CwEnsureExamples(){
    std::string dir=CwDir(); CreateDirectoryA(dir.c_str(),nullptr);
    std::string marker=dir+".examples-installed";
    bool first = GetFileAttributesA(marker.c_str())==INVALID_FILE_ATTRIBUTES;
    std::string src=ExeDir()+"assets\\widget-examples\\";
    WIN32_FIND_DATAA fd; HANDLE fh=FindFirstFileA((src+"*").c_str(),&fd);
    if(fh==INVALID_HANDLE_VALUE) return;
    do{ if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name=fd.cFileName, to=dir+name;
        // aether.py and the README always track the shipped version; examples only arrive once
        bool always = (name=="aether.py" || name=="README.md");
        if(always || (first && GetFileAttributesA(to.c_str())==INVALID_FILE_ATTRIBUTES))
            CopyFileA((src+name).c_str(),to.c_str(),FALSE);
    } while(FindNextFileA(fh,&fd));
    FindClose(fh);
    if(first){ FILE* f=fopen(marker.c_str(),"wb"); if(f){ fputs("examples copied - delete this file to get them again\n",f); fclose(f); } }
}
