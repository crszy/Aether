// Aether - temperatures: the sensor sidecar and native AMD GPU temperature.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ================================================================= temperatures
// LibreHardwareMonitor is C#/.NET and cannot be linked into a native shell, and reading its
// kernel driver directly is fragile. Instead we talk to its *web server* over HTTP when it is
// running (zero dependencies, no .NET in-process), and fall back to the ACPI thermal zone via
// WMI — which needs no driver at all but only reports a board sensor on many machines.
static std::string g_sensorSrc="none";
static std::string g_lhmUrl="http://localhost:8085/data.json";   // LibreHardwareMonitor web server
static std::atomic<bool> g_running{true};
static std::wstring U82W(const std::string& s);   // fwd (defined with the drawing helpers)

static double LhmFind(const std::string& j, const char* label){
    size_t p=j.find(std::string("\"")+label+"\"");
    if(p==std::string::npos) return -1;
    size_t v=j.find("\"Value\"",p); if(v==std::string::npos) return -1;
    v=j.find('"',v+7); if(v==std::string::npos) return -1;          // opening quote of the value string
    size_t e=j.find('"',v+1); if(e==std::string::npos) return -1;
    std::string s=j.substr(v+1,e-v-1);
    for(auto& ch:s) if(ch==',') ch='.';                              // LHM uses the locale decimal mark
    if(s.find("°C")==std::string::npos && s.find("C")==std::string::npos) return -1;
    double d=atof(s.c_str()); return (d>0&&d<130)? d : -1;
}
static bool ReadLhmTemps(double& cpu,double& gpu){
    // split the configured URL into host / path (only http:// is supported by the web server)
    std::string u=g_lhmUrl; if(u.rfind("http://",0)==0) u=u.substr(7); else return false;
    size_t slash=u.find('/'); std::string hostport=(slash==std::string::npos)?u:u.substr(0,slash);
    std::string path=(slash==std::string::npos)?"/":u.substr(slash);
    int port=8085; size_t colon=hostport.find(':');
    std::string host=hostport;
    if(colon!=std::string::npos){ host=hostport.substr(0,colon); port=atoi(hostport.c_str()+colon+1); }
    std::wstring wh=U82W(host), wp=U82W(path);
    std::string out;
    HINTERNET s=WinHttpOpen(L"Aether/1.0",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!s) return false;
    WinHttpSetTimeouts(s,600,600,900,900);   // never stall the UI on a missing server
    HINTERNET c=WinHttpConnect(s,wh.c_str(),(INTERNET_PORT)port,0);
    if(c){ HINTERNET r=WinHttpOpenRequest(c,L"GET",wp.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
        if(r){ if(WinHttpSendRequest(r,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0) && WinHttpReceiveResponse(r,nullptr)){
                DWORD avail=0; do{ avail=0; WinHttpQueryDataAvailable(r,&avail);
                    if(avail){ std::string b(avail,0); DWORD rd=0; WinHttpReadData(r,&b[0],avail,&rd); b.resize(rd); out+=b; }
                }while(avail>0 && out.size()<4000000); }
            WinHttpCloseHandle(r); }
        WinHttpCloseHandle(c); }
    WinHttpCloseHandle(s);
    if(out.size()<32) return false;
    const char* cpuKeys[]={"CPU Package","Core (Tctl/Tdie)","Core Average","CPU Cores","Core (Tavg)","CPU Total"};
    const char* gpuKeys[]={"GPU Core","GPU Temperature","GPU Hot Spot","GPU"};
    cpu=-1; for(auto k:cpuKeys){ double v=LhmFind(out,k); if(v>0){cpu=v;break;} }
    gpu=-1; for(auto k:gpuKeys){ double v=LhmFind(out,k); if(v>0){gpu=v;break;} }
    return cpu>0||gpu>0;
}
// ACPI thermal zone through WMI (root\WMI, MSAcpi_ThermalZoneTemperature) — deci-Kelvin
static double ReadWmiThermalZone(){
    double best=-1; IWbemLocator* loc=nullptr;
    if(FAILED(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_IWbemLocator,(void**)&loc))||!loc) return -1;
    IWbemServices* svc=nullptr; BSTR ns=SysAllocString(L"root\\WMI");
    HRESULT hr=loc->ConnectServer(ns,nullptr,nullptr,nullptr,0,nullptr,nullptr,&svc);
    SysFreeString(ns);
    if(SUCCEEDED(hr)&&svc){
        CoSetProxyBlanket(svc,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE);
        BSTR lang=SysAllocString(L"WQL"), q=SysAllocString(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");
        IEnumWbemClassObject* en=nullptr;
        if(SUCCEEDED(svc->ExecQuery(lang,q,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&en))&&en){
            IWbemClassObject* obj=nullptr; ULONG got=0;
            while(en->Next(1200,1,&obj,&got)==S_OK && got){
                VARIANT v; VariantInit(&v);
                if(SUCCEEDED(obj->Get(L"CurrentTemperature",0,&v,nullptr,nullptr)) && v.vt==VT_I4){
                    double c=v.lVal/10.0-273.15; if(c>0&&c<130) best=std::max(best,c); }
                VariantClear(&v); obj->Release(); obj=nullptr;
            }
            en->Release();
        }
        SysFreeString(lang); SysFreeString(q); svc->Release();
    }
    loc->Release(); return best;
}


// ================== the temperature SIDECAR (sensors\AetherSensors.exe) ==========================
// An Intel/AMD CPU's temperature lives in MSRs, which are ring 0. LibreHardwareMonitorLib carries
// the signed driver that reads them, and it is C#/.NET, so it CANNOT link into this native shell -
// hence a separate process. It writes one small JSON to %LOCALAPPDATA%\Aether\sensors.json; we read
// that. No socket, no port, nothing for the user to configure.
//
// It needs ADMIN to load the driver. Unelevated it still starts and still reports the GPU, but
// "cpu" comes back null and "elevated" false - which we surface rather than papering over. That is
// also why starting it is a Settings BUTTON and not something the shell does behind your back:
// loading a kernel driver is the user's call, and it is their UAC prompt to accept.
static std::string W2U8(const std::wstring&);   // fwd - defined with the other string helpers
static std::wstring U82W(const std::string&);  // fwd
static std::string ExeDir();                   // fwd
// Stall log: any main-loop section that blocks for more than 150 ms is written to stall.txt with its
// name. Cheap enough to leave on - it is one QueryPerformanceCounter pair per section - and it is what
// finds a freeze nobody can see in a profiler because it only happens on a desktop switch.
namespace stall {
    static double Now(){ static LARGE_INTEGER f{}; if(!f.QuadPart) QueryPerformanceFrequency(&f);
                         LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart*1000.0/f.QuadPart; }
    struct Scope{ const char* n; double t0;
        Scope(const char* name):n(name),t0(Now()){}
        ~Scope(){ double d=Now()-t0;
            if(d>150.0){ if(FILE* f=fopen((ExeDir()+"stall.txt").c_str(),"a")){
                SYSTEMTIME st; GetLocalTime(&st);
                fprintf(f,"%02d:%02d:%02d.%03d  %-22s %7.0f ms\n",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,n,d); fclose(f); } } } };
    // One dispatched message that took long: which message, to which window class. message_pump only
    // says the pump as a whole was slow; this says what was slow inside it.
    static void Msg(const MSG& m,double d){
        if(d<=100.0) return;
        char cls[64]="?"; if(m.hwnd) GetClassNameA(m.hwnd,cls,sizeof(cls));
        if(FILE* f=fopen((ExeDir()+"stall.txt").c_str(),"a")){
            SYSTEMTIME st; GetLocalTime(&st);
            fprintf(f,"%02d:%02d:%02d.%03d    msg 0x%04x wp=%llx -> %-22s %7.0f ms\n",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,
                    m.message,(unsigned long long)m.wParam,cls,d); fclose(f); } }
}
#define STALL(name) stall::Scope _st_##name(#name)
static bool  g_sensorSidecar=false;          // user preference (sensors.sidecar)
static std::string g_sidecarNote;            // what the sidecar last told us about itself

static std::string SidecarJsonPath(){
    wchar_t* base=nullptr; std::string out;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&base)) && base)
        out=W2U8(std::wstring(base)+L"\\Aether\\sensors.json");
    if(base) CoTaskMemFree(base);
    return out;
}
static std::wstring SidecarExe(){ return U82W(ExeDir())+L"sensors\\AetherSensors.exe"; }
static bool SidecarInstalled(){ DWORD a=GetFileAttributesW(SidecarExe().c_str());
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY); }
static bool SidecarRunning(){
    // a full process snapshot is far too expensive to take once per rendered frame
    static bool cached=false; static ULONGLONG at=0;
    if(GetTickCount64()-at < 1000) return cached;
    at=GetTickCount64();
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof(pe)}; bool found=false;
    if(Process32FirstW(snap,&pe)) do{
        if(_wcsicmp(pe.szExeFile,L"AetherSensors.exe")==0){ found=true; break; }
    }while(Process32NextW(snap,&pe));
    CloseHandle(snap); cached=found; return found;
}
// "runas" so Windows raises its own consent dialog - we never try to fake or bypass elevation.
static void SidecarStart(bool elevated){
    if(!SidecarInstalled() || SidecarRunning()) return;
    std::wstring exe=SidecarExe();
    std::wstring dir=exe.substr(0,exe.find_last_of(L'\\'));
    AetherShellExec(nullptr, elevated? L"runas":L"open", exe.c_str(), nullptr, dir.c_str(), SW_HIDE);
}
static void SidecarStop(){
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{sizeof(pe)};
    if(Process32FirstW(snap,&pe)) do{
        if(_wcsicmp(pe.szExeFile,L"AetherSensors.exe")==0){
            if(HANDLE h=OpenProcess(PROCESS_TERMINATE,FALSE,pe.th32ProcessID)){ TerminateProcess(h,0); CloseHandle(h); } }
    }while(Process32NextW(snap,&pe));
    CloseHandle(snap);
}
// Reads the sidecar's JSON. A file older than 15s is IGNORED: a sidecar that died must degrade to
// "no reading", never to a number frozen at whatever it was when it stopped.
static bool ReadSidecarTemps(double& cpu,double& gpu,std::string& note){
    cpu=-1; gpu=-1;
    std::string path=SidecarJsonPath(); if(path.empty()) return false;
    FILE* f=fopen(path.c_str(),"rb"); if(!f) return false;
    char buf[1024]; size_t n=fread(buf,1,sizeof(buf)-1,f); fclose(f);
    if(!n) return false; buf[n]=0; std::string j(buf,n);
    if(j.find("\"ok\":true")==std::string::npos){
        note = (j.find("\"stopped\":true")!=std::string::npos)? "sidecar stopped" : "sidecar could not start";
        return false; }
    { size_t p2=j.find("\"ts\":"); if(p2!=std::string::npos){
        long long ts=_atoi64(j.c_str()+p2+5);
        long long now=(long long)time(nullptr);
        if(now-ts>15){ note="sidecar not responding"; return false; } } }
    bool elev = j.find("\"elevated\":true")!=std::string::npos;
    auto num=[&](const char* key,double& out)->bool{
        size_t p2=j.find(key); if(p2==std::string::npos) return false;
        p2+=strlen(key); while(p2<j.size()&&(j[p2]==' ')) p2++;
        if(j.compare(p2,4,"null")==0) return false;
        out=atof(j.c_str()+p2); return out>0&&out<150; };
    bool okc=num("\"cpu\":",cpu), okg=num("\"gpu\":",gpu);
    note = elev ? "sidecar" : (okc? "sidecar" : "sidecar (not elevated - no CPU temp)");
    return okc||okg;
}
// ================= AMD GPU temperature, natively (no LibreHardwareMonitor needed) ================
// LHM can read temps at all because it ships a KERNEL DRIVER for MSR/SMBus access - which is why the
// CPU side genuinely needs it. The GPU side does not: AMD's display library ships with every Radeon
// driver as atiadlxx.dll and reports the ASIC temperature from user space. So a Radeon box gets a
// real GPU temp with nothing installed, and only the CPU still falls back to LHM/ACPI.
//
// RDNA cards (this box is an RX 6400) are Overdrive 8 and answer ADL2_New_QueryPMLogData_Get;
// Overdrive 6 and OverdriveN are kept as fallbacks for older GCN/Polaris/Vega parts.
namespace adl {
    typedef void* ADL_CONTEXT_HANDLE;
    typedef void* (__stdcall *MallocCb)(int);
    static void* __stdcall Alloc(int sz){ return malloc(sz); }

    struct AdapterInfo {
        int iSize, iAdapterIndex; char strUDID[256];
        int iBusNumber, iDeviceNumber, iFunctionNumber, iVendorID;
        char strAdapterName[256], strDisplayName[256];
        int iPresent, iExist;
        char strDriverPath[256], strDriverPathExt[256], strPNPString[256];
        int iOSDisplayIndex;
    };
    struct SingleSensor { int supported, value; };
    struct PMLogDataOutput { int size; SingleSensor sensors[256]; };
    struct ODNTemperature { int iSize, iTemperature; };

    // sensor ids from AMD's ADLSensorType (same table LibreHardwareMonitor uses)
    enum { PMLOG_TEMPERATURE_EDGE=8, PMLOG_TEMPERATURE_HOTSPOT=27 };

    typedef int (*FnCreate)(MallocCb,int,ADL_CONTEXT_HANDLE*);
    typedef int (*FnDestroy)(ADL_CONTEXT_HANDLE);
    typedef int (*FnNumAdapters)(ADL_CONTEXT_HANDLE,int*);
    typedef int (*FnAdapterInfo)(ADL_CONTEXT_HANDLE,AdapterInfo*,int);
    typedef int (*FnPMLog)(ADL_CONTEXT_HANDLE,int,PMLogDataOutput*);
    typedef int (*FnOD6Temp)(ADL_CONTEXT_HANDLE,int,int*);
    typedef int (*FnODNTemp)(ADL_CONTEXT_HANDLE,int,int,int*);

    static HMODULE      g_lib=nullptr;
    static ADL_CONTEXT_HANDLE g_ctx=nullptr;
    static int          g_adapter=-1;
    static bool         g_tried=false;
    static FnPMLog      g_pmlog=nullptr;
    static FnOD6Temp    g_od6=nullptr;
    static FnODNTemp    g_odn=nullptr;

    static bool Init(){
        if(g_tried) return g_ctx!=nullptr && g_adapter>=0;
        g_tried=true;
        g_lib=LoadLibraryW(L"atiadlxx.dll");
        if(!g_lib) g_lib=LoadLibraryW(L"atiadlxy.dll");     // 32-bit name, harmless to try
        if(!g_lib) return false;
        auto create=(FnCreate)GetProcAddress(g_lib,"ADL2_Main_Control_Create");
        auto nAd   =(FnNumAdapters)GetProcAddress(g_lib,"ADL2_Adapter_NumberOfAdapters_Get");
        auto info  =(FnAdapterInfo)GetProcAddress(g_lib,"ADL2_Adapter_AdapterInfo_Get");
        g_pmlog=(FnPMLog)GetProcAddress(g_lib,"ADL2_New_QueryPMLogData_Get");
        g_od6  =(FnOD6Temp)GetProcAddress(g_lib,"ADL2_Overdrive6_Temperature_Get");
        g_odn  =(FnODNTemp)GetProcAddress(g_lib,"ADL2_OverdriveN_Temperature_Get");
        if(!create||!nAd||!info) return false;
        // 1 = enumerate only adapters that are actually connected
        if(create(Alloc,1,&g_ctx)!=0 || !g_ctx) return false;
        int n=0; if(nAd(g_ctx,&n)!=0 || n<=0) return false;
        std::vector<AdapterInfo> ai(n); memset(ai.data(),0,sizeof(AdapterInfo)*n);
        if(info(g_ctx,ai.data(),(int)(sizeof(AdapterInfo)*n))!=0) return false;
        // ADL lists one entry per OUTPUT, so a single card appears several times; the first present
        // AMD entry is the card, and its iAdapterIndex is what every other call wants.
        for(int i=0;i<n;i++)
            if(ai[i].iVendorID==1002 && ai[i].iPresent){ g_adapter=ai[i].iAdapterIndex; break; }
        return g_adapter>=0;
    }
    // returns degrees C, or -1
    static double Temp(){
        if(!Init()) return -1;
        if(g_pmlog){
            PMLogDataOutput out{};
            if(g_pmlog(g_ctx,g_adapter,&out)==0){
                if(out.sensors[PMLOG_TEMPERATURE_EDGE].supported){
                    double v=out.sensors[PMLOG_TEMPERATURE_EDGE].value;
                    if(v>1000) v/=1000.0;                       // some drivers report millidegrees
                    if(v>0&&v<150) return v; }
                if(out.sensors[PMLOG_TEMPERATURE_HOTSPOT].supported){
                    double v=out.sensors[PMLOG_TEMPERATURE_HOTSPOT].value;
                    if(v>1000) v/=1000.0;
                    if(v>0&&v<150) return v; }
            }
        }
        if(g_odn){ int t=0; if(g_odn(g_ctx,g_adapter,1,&t)==0){    // 1 = ADLNI_TEMPERATURE_TYPE_CORE
            double v=t; if(v>1000) v/=1000.0; if(v>0&&v<150) return v; } }
        if(g_od6){ int t=0; if(g_od6(g_ctx,g_adapter,&t)==0){      // always millidegrees
            double v=t/1000.0; if(v>0&&v<150) return v; } }
        return -1;
    }
    static void Shutdown(){
        if(g_lib&&g_ctx){ if(auto d=(FnDestroy)GetProcAddress(g_lib,"ADL2_Main_Control_Destroy")) d(g_ctx); }
        g_ctx=nullptr; if(g_lib){ FreeLibrary(g_lib); g_lib=nullptr; }
    }
}
static void SensorThread(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    bool wmiTried=false, wmiOk=false;
    while(g_running){
        // CPU and GPU are reported SEPARATELY now: this box has a Radeon (native temp, always) and an
        // Intel CPU (needs LHM's ring-0 driver, so it may well have no source at all). One combined
        // "Sensors: none" line hid the fact that half of it was working.
        const char* cpuSrc="none"; const char* gpuSrc="none";
        double c=-1,g=-1;
        // our own sidecar first - it is the only source that can give a CPU number
        { double sc=-1,sg=-1; std::string note;
          if(ReadSidecarTemps(sc,sg,note)){
              if(sc>0){ g_st.cpuTemp=sc; cpuSrc="sensor sidecar"; }
              if(sg>0){ g_st.gpuTemp=sg; gpuSrc="sensor sidecar"; } }
          g_sidecarNote=note; }
        if(!strcmp(cpuSrc,"none") && ReadLhmTemps(c,g)){
            if(c>0){ g_st.cpuTemp=c; cpuSrc="LibreHardwareMonitor"; }
            if(g>0 && !strcmp(gpuSrc,"none")){ g_st.gpuTemp=g; gpuSrc="LibreHardwareMonitor"; }
        }
        if(!strcmp(cpuSrc,"none")){
            if(!wmiTried){ wmiTried=true; wmiOk=ReadWmiThermalZone()>0; }
            if(wmiOk){ double t=ReadWmiThermalZone(); if(t>0){ g_st.cpuTemp=t; cpuSrc="ACPI thermal zone"; } }
        }
        if(!strcmp(gpuSrc,"none")){
            double gt=adl::Temp();
            if(gt>0){ g_st.gpuTemp=gt; gpuSrc="AMD driver (ADL)"; }
        }
        { std::string s2;
          if(!strcmp(cpuSrc,gpuSrc)) s2 = std::string("CPU + GPU: ")+cpuSrc;
          else s2 = std::string("CPU: ")+cpuSrc+"  \xE2\x80\xA2  GPU: "+gpuSrc;
          g_sensorSrc=s2; }
        for(int i=0;i<30 && g_running;i++) Sleep(100);
    }
    adl::Shutdown();
    CoUninitialize();
}
