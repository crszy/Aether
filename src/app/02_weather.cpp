// Aether - weather (Open-Meteo).
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ================================================================= weather (Open-Meteo, like Caelestia)
struct Weather {
    bool ok=false; double temp=0,feels=0,wind=0; int hum=0,code=0,isDay=1;
    std::string city, sunrise, sunset; std::vector<double> dMax,dMin; std::vector<int> dCode;
    std::vector<double> hourly;   // next-24h temperature for the graph
} g_wx;

static std::string HttpGet(const wchar_t* host, const wchar_t* path, bool https) {
    std::string out;
    HINTERNET s=WinHttpOpen(L"Aether/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if(!s) return out;
    HINTERNET c=WinHttpConnect(s, host, https?INTERNET_DEFAULT_HTTPS_PORT:INTERNET_DEFAULT_HTTP_PORT, 0);
    if(c){
        HINTERNET r=WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, https?WINHTTP_FLAG_SECURE:0);
        if(r){
            if(WinHttpSendRequest(r,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)
               && WinHttpReceiveResponse(r,nullptr)){
                DWORD avail=0;
                do{ avail=0; WinHttpQueryDataAvailable(r,&avail);
                    if(avail){ std::string b(avail,0); DWORD rd=0; WinHttpReadData(r,&b[0],avail,&rd); b.resize(rd); out+=b; }
                }while(avail>0);
            }
            WinHttpCloseHandle(r);
        }
        WinHttpCloseHandle(c);
    }
    WinHttpCloseHandle(s);
    return out;
}

// tiny JSON field extractors (enough for these known API shapes)
static size_t jkey(const std::string& s,const std::string& k,size_t f=0){ return s.find("\""+k+"\"",f); }
static double jnum(const std::string& s,const std::string& k){ size_t p=jkey(s,k); if(p==std::string::npos)return NAN; p=s.find(':',p); if(p==std::string::npos)return NAN; return atof(s.c_str()+p+1); }
// reads a JSON string value and UNESCAPES it. It used to return the raw span, so a Windows path
// round-tripped through SaveConfig gained a backslash on every single save.
static std::string jstr(const std::string& s,const std::string& k){
    size_t p=jkey(s,k); if(p==std::string::npos)return"";
    p=s.find(':',p); p=s.find('"',p); if(p==std::string::npos)return"";
    std::string out;
    for(size_t i=p+1;i<s.size();i++){
        char c=s[i];
        if(c=='"') break;
        if(c=='\\' && i+1<s.size()){
            char n=s[++i];
            switch(n){ case 'n': out+='\n'; break; case 't': out+='\t'; break; case 'r': out+='\r'; break;
                       default: out+=n; }                 // \\ \" \/ and anything else: literal
        } else out+=c;
    }
    return out;
}
// Finds the {...} / [...] value for a key. It keeps looking when a match turns out to hold a plain
// value instead of a scope — "bar": { "monitors": "all" } used to shadow the top-level "monitors"
// object, so the per-display settings silently never loaded.
static std::string jscope(const std::string& s,const std::string& k){
    size_t from=0;
    while(true){
        size_t p=jkey(s,k,from); if(p==std::string::npos) return "";
        size_t c=s.find(':',p);  if(c==std::string::npos) return "";
        size_t q=c+1; while(q<s.size()&&(s[q]==' '||s[q]=='\t'||s[q]=='\r'||s[q]=='\n')) q++;
        if(q<s.size() && (s[q]=='{'||s[q]=='[')){
            char o=s[q], cl=(o=='{')?'}':']'; int d=0; size_t st=q;
            for(;q<s.size();q++){ if(s[q]==o)d++; else if(s[q]==cl){ d--; if(!d) return s.substr(st,q-st+1); } }
            return "";
        }
        from=p+1;                       // that one was a plain value: keep searching
    }
}
// A TOP-LEVEL scope, i.e. a key at brace depth 1. Nested scopes reuse names — "layout" holds its
// own "bar" and "dashboard" objects — and a plain search finds whichever appears first in the file.
// That silently made the per-display block and the media-card image read from the wrong object.
static std::string jscope1(const std::string& s,const std::string& k){
    const std::string want="\"" + k + "\"";
    int depth=0; bool instr=false;
    for(size_t i=0;i<s.size();i++){
        char c=s[i];
        if(instr){ if(c=='\\'){ i++; continue; } if(c=='"') instr=false; continue; }
        if(c=='"'){
            if(depth==1 && s.compare(i,want.size(),want)==0){
                size_t col=s.find(':',i+want.size());
                if(col!=std::string::npos){
                    size_t q=col+1; while(q<s.size() && isspace((unsigned char)s[q])) q++;
                    if(q<s.size() && (s[q]=='{'||s[q]=='[')){
                        char o=s[q], cl=(o=='{')?'}':']'; int d=0; size_t st=q;
                        for(;q<s.size();q++){ if(s[q]==o)d++; else if(s[q]==cl){ d--; if(!d) return s.substr(st,q-st+1); } }
                        return "";
                    }
                }
            }
            instr=true; continue;
        }
        if(c=='{'||c=='[') depth++;
        else if(c=='}'||c==']') depth--;
    }
    return "";
}
static std::vector<double> jarr(const std::string& s){ std::vector<double> v; size_t p=s.find('['); if(p==std::string::npos)return v; p++; while(p<s.size()&&s[p]!=']'){ while(p<s.size()&&(s[p]==' '||s[p]==','))p++; if(p>=s.size()||s[p]==']')break; v.push_back(atof(s.c_str()+p)); while(p<s.size()&&s[p]!=','&&s[p]!=']')p++; } return v; }

static const char* WxText(int c){
    if(c==0)return"Clear"; if(c<=2)return"Partly cloudy"; if(c==3)return"Overcast";
    if(c<=48)return"Fog"; if(c<=57)return"Drizzle"; if(c<=67)return"Rain"; if(c<=77)return"Snow";
    if(c<=82)return"Showers"; if(c<=86)return"Snow showers"; return"Thunderstorm";
}

// weather.location: 0 off (the default - nothing is looked up until the user chooses), 1 my location (an IP lookup at
// ip-api.com), 2 a city the user typed (open-meteo's geocoder). A new install used to send the IP address away on its
// first run without saying so.
static const char* WXSRC_NAMES[] = { "off","auto","city" };
static int  g_wxSource=0;
static std::string g_wxCity;
static void FetchWeather() {
    std::thread([]{
        if(g_wxSource==0){ g_wx=Weather(); return; }
        double lat=NAN, lon=NAN; std::string city;
        if(g_wxSource==2){
            if(g_wxCity.empty()){ g_wx=Weather(); return; }
            std::string enc; char hx[4];
            for(unsigned char ch:g_wxCity){ if(isalnum(ch)||ch=='-'||ch=='.'||ch=='_') enc.push_back((char)ch); else { snprintf(hx,4,"%%%02X",ch); enc+=hx; } }
            std::wstring path=L"/v1/search?count=1&language=en&format=json&name="+U82W(enc);
            std::string geo=HttpGet(L"geocoding-api.open-meteo.com", path.c_str(), true);
            lat=jnum(geo,"latitude"); lon=jnum(geo,"longitude"); city=jstr(geo,"name");
        } else {
            std::string geo=HttpGet(L"ip-api.com", L"/json", false);
            lat=jnum(geo,"lat"); lon=jnum(geo,"lon"); city=jstr(geo,"city");
        }
        if(std::isnan(lat)||std::isnan(lon)) return;
        wchar_t path[600];
        swprintf(path,600,L"/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m&hourly=temperature_2m&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset&timezone=auto&forecast_days=7",lat,lon);
        std::string j=HttpGet(L"api.open-meteo.com", path, true);
        std::string cur=jscope(j,"current");
        Weather w; w.city=city;
        w.temp=jnum(cur,"temperature_2m"); w.feels=jnum(cur,"apparent_temperature");
        w.hum=(int)jnum(cur,"relative_humidity_2m"); w.wind=jnum(cur,"wind_speed_10m");
        w.code=(int)jnum(cur,"weather_code"); w.isDay=(int)jnum(cur,"is_day");
        std::string daily=jscope(j,"daily");
        w.dMax=jarr(jscope(daily,"temperature_2m_max"));
        w.dMin=jarr(jscope(daily,"temperature_2m_min"));
        for(double c:jarr(jscope(daily,"weather_code"))) w.dCode.push_back((int)c);
        // sunrise/sunset are arrays of ISO strings ("...T04:59"); grab today's HH:MM
        auto firstTime=[&](const char* key){ std::string a=jscope(daily,key); size_t t=a.find('T');
            if(t!=std::string::npos && t+6<=a.size()) return a.substr(t+1,5); return std::string(); };
        w.sunrise=firstTime("sunrise"); w.sunset=firstTime("sunset");
        std::string hourly=jscope(j,"hourly");
        auto ht=jarr(jscope(hourly,"temperature_2m"));
        for(size_t i=0;i<ht.size() && i<24;i++) w.hourly.push_back(ht[i]);
        w.ok=!std::isnan(w.temp);
        g_wx=w;
    }).detach();
}
