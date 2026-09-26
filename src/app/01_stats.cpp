// Aether - CPU / GPU / memory / disk / network / battery sampling.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ================================================================= stats
struct Stats {
    std::wstring cpuName, gpuName;
    double cpuUsage=0, gpuUsage=0;          // 0..1
    double cpuTemp=-1, gpuTemp=-1;          // -1 = unknown
    unsigned long long memUsed=0, memTotal=0;
    unsigned long long diskUsed=0, diskTotal=0;
    double netDown=0, netUp=0;              // bytes/sec
    unsigned long long netTotalIn=0, netTotalOut=0;
    bool  hasBattery=false; int battPct=0; bool charging=false;
    bool  online=false;                     // any non-loopback iface up
    std::vector<float> netHist;             // KB/s down history for the graph
    std::vector<float> cpuHist,gpuHist,memHist,diskHist;   // 0..1 usage history
} g_st;

static std::wstring RegString(HKEY root, const wchar_t* sub, const wchar_t* val) {
    wchar_t buf[512]; DWORD sz=sizeof(buf), type=0;
    if (RegGetValueW(root, sub, val, RRF_RT_REG_SZ, &type, buf, &sz)==ERROR_SUCCESS) return buf;
    return L"";
}

// CPU usage from GetSystemTimes deltas (the Windows analogue of /proc/stat)
static ULARGE_INTEGER g_lastIdle{}, g_lastKernel{}, g_lastUser{};
static void SampleCpu() {
    FILETIME fi, fk, fu;
    if (!GetSystemTimes(&fi,&fk,&fu)) return;
    ULARGE_INTEGER i,k,u; i.LowPart=fi.dwLowDateTime;i.HighPart=fi.dwHighDateTime;
    k.LowPart=fk.dwLowDateTime;k.HighPart=fk.dwHighDateTime;
    u.LowPart=fu.dwLowDateTime;u.HighPart=fu.dwHighDateTime;
    unsigned long long idleD = i.QuadPart-g_lastIdle.QuadPart;
    unsigned long long kernD = k.QuadPart-g_lastKernel.QuadPart;
    unsigned long long userD = u.QuadPart-g_lastUser.QuadPart;
    unsigned long long total = kernD+userD;                 // kernel already includes idle
    if (g_lastKernel.QuadPart && total>0) g_st.cpuUsage = 1.0 - (double)idleD/(double)total;
    g_lastIdle=i; g_lastKernel=k; g_lastUser=u;
}

// GPU usage via PDH "GPU Engine" counters (any vendor, Win10 1709+)
static PDH_HQUERY  g_pdhQ = nullptr;
static PDH_HCOUNTER g_pdhGpu = nullptr;
static void InitGpuCounter() {
    if (PdhOpenQueryW(nullptr,0,&g_pdhQ)!=ERROR_SUCCESS) return;
    PdhAddEnglishCounterW(g_pdhQ, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_pdhGpu);
    PdhCollectQueryData(g_pdhQ);
}
static void SampleGpu() {
    if (!g_pdhQ) return;
    if (PdhCollectQueryData(g_pdhQ)!=ERROR_SUCCESS) return;
    DWORD bufSize=0, count=0;
    PDH_FMT_COUNTERVALUE_ITEM_W* items=nullptr;
    PDH_STATUS s = PdhGetFormattedCounterArrayW(g_pdhGpu, PDH_FMT_DOUBLE, &bufSize, &count, nullptr);
    if (s==PDH_MORE_DATA && bufSize) {
        items=(PDH_FMT_COUNTERVALUE_ITEM_W*)malloc(bufSize);
        if (PdhGetFormattedCounterArrayW(g_pdhGpu, PDH_FMT_DOUBLE, &bufSize, &count, items)==ERROR_SUCCESS) {
            double sum=0;
            for (DWORD i=0;i<count;i++) if (items[i].FmtValue.CStatus==PDH_CSTATUS_VALID_DATA) sum+=items[i].FmtValue.doubleValue;
            g_st.gpuUsage = std::min(1.0, sum/100.0);
        }
        free(items);
    }
}

static void QueryGpuName() {
    IDXGIFactory1* f=nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) return;
    IDXGIAdapter1* a=nullptr; SIZE_T best=0;
    for (UINT i=0; f->EnumAdapters1(i,&a)!=DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d; a->GetDesc1(&d);
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory>=best) {
            best=d.DedicatedVideoMemory; g_st.gpuName=d.Description;
        }
        a->Release();
    }
    f->Release();
}

// network totals via GetIfTable2 (analogue of /proc/net/dev)
static void SampleNet(double dt) {
    MIB_IF_TABLE2* t=nullptr;
    if (GetIfTable2(&t)!=NO_ERROR || !t) return;
    unsigned long long in=0,out=0; bool up=false;
    for (ULONG i=0;i<t->NumEntries;i++) {
        auto& r=t->Table[i];
        if (r.Type==IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (r.OperStatus!=IfOperStatusUp) continue;
        in+=r.InOctets; out+=r.OutOctets; up=true;
    }
    g_st.online=up;
    FreeMibTable(t);
    if (g_st.netTotalIn && dt>0) {
        g_st.netDown = (double)(in-g_st.netTotalIn)/dt;
        g_st.netUp   = (double)(out-g_st.netTotalOut)/dt;
    }
    g_st.netTotalIn=in; g_st.netTotalOut=out;
    g_st.netHist.push_back((float)(g_st.netDown/1024.0));
    if (g_st.netHist.size()>120) g_st.netHist.erase(g_st.netHist.begin());
}

static void SampleSlow() {
    MEMORYSTATUSEX m{sizeof(m)};
    if (GlobalMemoryStatusEx(&m)) { g_st.memTotal=m.ullTotalPhys; g_st.memUsed=m.ullTotalPhys-m.ullAvailPhys; }
    ULARGE_INTEGER freeAvail, total, freeTotal;
    if (GetDiskFreeSpaceExW(L"C:\\", &freeAvail, &total, &freeTotal)) {
        g_st.diskTotal=total.QuadPart; g_st.diskUsed=total.QuadPart-freeTotal.QuadPart;
    }
    SYSTEM_POWER_STATUS ps;
    if (GetSystemPowerStatus(&ps)) {
        g_st.hasBattery = !(ps.BatteryFlag & 128) && ps.BatteryLifePercent!=255;
        g_st.battPct    = ps.BatteryLifePercent;
        g_st.charging   = (ps.ACLineStatus==1);
    }
}

static void InitStats() {
    g_st.cpuName = RegString(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    QueryGpuName();
    InitGpuCounter();
    SampleCpu(); SampleSlow(); SampleNet(0);
}
