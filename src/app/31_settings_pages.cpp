// Aether - Settings pages and their system back-ends.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =========================================================================================
// System back-ends for the settings pages that mirror CachyOS / Plasma System Settings.
// Everything here talks to a PUBLIC Win32 API, so nothing depends on an undocumented shell
// interface: power schemes via powrprof, input via SystemParametersInfo, autostart via the
// Run keys, firewall via the netfw COM policy, printers via the spooler.
// =========================================================================================

// ---- power schemes (the powercfg model) ----
struct PowerPlan{ GUID g; std::string name; };
static std::vector<PowerPlan> g_plans; static int g_planCur=-1; static bool g_plansRead=false;
static std::string g_tuneMsg;          // what the last Tune-up action actually did
static void RefreshPlans(){
    g_plans.clear(); g_planCur=-1; g_plansRead=true;
    for(DWORD i=0;i<32;i++){
        GUID g; DWORD sz=sizeof(g);
        if(PowerEnumerate(nullptr,nullptr,nullptr,ACCESS_SCHEME,i,(UCHAR*)&g,&sz)!=ERROR_SUCCESS) break;
        DWORD n=0; PowerReadFriendlyName(nullptr,&g,nullptr,nullptr,nullptr,&n);
        std::wstring nm; nm.resize(n/sizeof(wchar_t)+2,L'\0');
        if(n && PowerReadFriendlyName(nullptr,&g,nullptr,nullptr,(UCHAR*)&nm[0],&n)==ERROR_SUCCESS)
             g_plans.push_back({g,W2U8(nm.c_str())});
        else g_plans.push_back({g,"Power plan"});
    }
    GUID* act=nullptr;
    if(PowerGetActiveScheme(nullptr,&act)==ERROR_SUCCESS && act){
        for(size_t i=0;i<g_plans.size();i++) if(memcmp(&g_plans[i].g,act,sizeof(GUID))==0){ g_planCur=(int)i; break; }
        LocalFree(act);
    }
}
// documented power-setting GUIDs (declared locally so we never rely on the SDK's DEFINE_GUID pulls)
static const GUID PS_SUB_VIDEO ={0x7516b95f,0xf776,0x4464,{0x8c,0x53,0x06,0x16,0x7f,0x40,0xcc,0x99}};
static const GUID PS_VIDEOIDLE ={0x3c0bc021,0xc8a8,0x4e07,{0xa9,0x73,0x6b,0x14,0xcb,0xcb,0x2b,0x7e}};
static const GUID PS_SUB_SLEEP ={0x238c9fa8,0x0aad,0x41ed,{0x83,0xf4,0x97,0xbe,0x24,0x2c,0x8f,0x20}};
static const GUID PS_STANDBY   ={0x29f6c1db,0x86da,0x48c5,{0x9f,0xdb,0xf2,0xb6,0x7b,0x1f,0x44,0xda}};
static DWORD PowerGet(const GUID& sub,const GUID& set,bool ac){
    GUID* a=nullptr; DWORD v=0;
    if(PowerGetActiveScheme(nullptr,&a)!=ERROR_SUCCESS||!a) return 0;
    if(ac) PowerReadACValueIndex(nullptr,a,&sub,&set,&v); else PowerReadDCValueIndex(nullptr,a,&sub,&set,&v);
    LocalFree(a); return v;
}
static void PowerSet(const GUID& sub,const GUID& set,bool ac,DWORD secs){
    GUID* a=nullptr; if(PowerGetActiveScheme(nullptr,&a)!=ERROR_SUCCESS||!a) return;
    if(ac) PowerWriteACValueIndex(nullptr,a,&sub,&set,secs); else PowerWriteDCValueIndex(nullptr,a,&sub,&set,secs);
    PowerSetActiveScheme(nullptr,a); LocalFree(a);
}

// ---- input devices ----
static int  InMouseSpeed(){ int v=10; SystemParametersInfoW(SPI_GETMOUSESPEED,0,&v,0); return v; }
static void InSetMouseSpeed(int v){ SystemParametersInfoW(SPI_SETMOUSESPEED,0,(void*)(INT_PTR)std::clamp(v,1,20),SPIF_SENDCHANGE); }
static bool InMouseAccel(){ int m[3]={0,0,0}; SystemParametersInfoW(SPI_GETMOUSE,0,m,0); return m[2]!=0; }
static void InSetMouseAccel(bool on){ int m[3]={6,10,on?1:0}; SystemParametersInfoW(SPI_SETMOUSE,0,m,SPIF_SENDCHANGE); }
static int  InKbRepeat(){ DWORD v=31; SystemParametersInfoW(SPI_GETKEYBOARDSPEED,0,&v,0); return (int)v; }
static void InSetKbRepeat(int v){ SystemParametersInfoW(SPI_SETKEYBOARDSPEED,std::clamp(v,0,31),nullptr,SPIF_SENDCHANGE); }
static int  InKbDelay(){ int v=1; SystemParametersInfoW(SPI_GETKEYBOARDDELAY,0,&v,0); return v; }
static void InSetKbDelay(int v){ SystemParametersInfoW(SPI_SETKEYBOARDDELAY,std::clamp(v,0,3),nullptr,SPIF_SENDCHANGE); }
static int  InScrollLines(){ UINT v=3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&v,0); return (int)v; }
static void InSetScrollLines(int v){ SystemParametersInfoW(SPI_SETWHEELSCROLLLINES,std::clamp(v,1,20),nullptr,SPIF_SENDCHANGE); }

// ---- window management (the Plasma "Window Behavior" knobs that Windows actually exposes) ----
static bool WmGetBool(UINT get){ BOOL b=FALSE; SystemParametersInfoW(get,0,&b,0); return b!=FALSE; }
// ---- the two Windows switches a shell replacement has to carry itself --------------------------
// Once Aether stands in for explorer.exe, Settings > Personalization > Colors and
// Settings > Accessibility > Visual effects are awkward to reach, so the toggles live here too.
// TRANSPARENCY is a plain registry flag the DWM and every WinUI app watch.
static bool WinTransparency(){
    DWORD v=1,sz=4;
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"EnableTransparency",RRF_RT_REG_DWORD,nullptr,&v,&sz);
    return v!=0;
}
static void SetWinTransparency(bool on){
    HKEY k;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                       0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS) return;
    DWORD v=on?1:0; RegSetValueExW(k,L"EnableTransparency",0,REG_DWORD,(BYTE*)&v,4); RegCloseKey(k);
    // same broadcast the theme switch uses - this is what makes running apps repaint
    SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,(LPARAM)L"ImmersiveColorSet",SMTO_ABORTIFHUNG,100,nullptr);
}
// ANIMATION EFFECTS is not one flag: Windows' single switch drives the client-area animations
// (SPI) AND the window minimise/restore animation (MinAnimate, a REG_SZ "0"/"1" under WindowMetrics).
// Flipping only the SPI one leaves windows still genie-ing, which reads as "the toggle did nothing".
static bool WinAnimations(){ return WmGetBool(SPI_GETCLIENTAREAANIMATION); }
static void SetWinAnimations(bool on){
    SystemParametersInfoW(SPI_SETCLIENTAREAANIMATION,0,(void*)(INT_PTR)(on?TRUE:FALSE),SPIF_SENDCHANGE);
    HKEY k;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Control Panel\\Desktop\\WindowMetrics",
                       0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)==ERROR_SUCCESS){
        const wchar_t* v=on?L"1":L"0";
        RegSetValueExW(k,L"MinAnimate",0,REG_SZ,(const BYTE*)v,(DWORD)(2*sizeof(wchar_t)));
        RegCloseKey(k);
    }
    ANIMATIONINFO ai={sizeof(ai),on?TRUE:FALSE};
    SystemParametersInfoW(SPI_SETANIMATION,sizeof(ai),&ai,SPIF_SENDCHANGE);
    SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,SPI_SETNONCLIENTMETRICS,0,SMTO_ABORTIFHUNG,100,nullptr);
}
static void WmSetPv(UINT set,bool v){ SystemParametersInfoW(set,0,(void*)(INT_PTR)(v?TRUE:FALSE),SPIF_SENDCHANGE); }
static void WmSetUi(UINT set,bool v){ SystemParametersInfoW(set,v?TRUE:FALSE,nullptr,SPIF_SENDCHANGE); }

// ---- autostart (Run keys). Disabling parks the value in our own key so it can be restored. ----
static const wchar_t* RUNKEY =L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* RUNPARK=L"Software\\Aether\\RunDisabled";
struct StartupItem{ std::string name, cmd; bool enabled; bool machine; };
static std::vector<StartupItem> g_startup; static bool g_startupRead=false;
static void ScanRunKey(HKEY root,const wchar_t* sub,bool enabled,bool machine){
    HKEY k; if(RegOpenKeyExW(root,sub,0,KEY_READ,&k)!=ERROR_SUCCESS) return;
    for(DWORD i=0;;i++){
        wchar_t nm[256]; DWORD nsz=256; BYTE val[1024]; DWORD vsz=sizeof(val), type=0;
        if(RegEnumValueW(k,i,nm,&nsz,nullptr,&type,val,&vsz)!=ERROR_SUCCESS) break;
        if(type!=REG_SZ && type!=REG_EXPAND_SZ) continue;
        g_startup.push_back({W2U8(nm),W2U8((wchar_t*)val),enabled,machine});
    }
    RegCloseKey(k);
}
static void RefreshStartup(){
    g_startup.clear(); g_startupRead=true;
    ScanRunKey(HKEY_CURRENT_USER,RUNKEY,true,false);
    ScanRunKey(HKEY_LOCAL_MACHINE,RUNKEY,true,true);
    ScanRunKey(HKEY_CURRENT_USER,RUNPARK,false,false);
}
static void StartupSetEnabled(const StartupItem& it,bool on){
    if(it.machine) return;                                   // HKLM entries need elevation; left read-only
    std::wstring nm=U82W(it.name), cmd=U82W(it.cmd);
    HKEY dst,src; const wchar_t* dk = on?RUNKEY:RUNPARK; const wchar_t* sk = on?RUNPARK:RUNKEY;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,dk,0,nullptr,0,KEY_SET_VALUE,nullptr,&dst,nullptr)!=ERROR_SUCCESS) return;
    RegSetValueExW(dst,nm.c_str(),0,REG_SZ,(const BYTE*)cmd.c_str(),(DWORD)((cmd.size()+1)*sizeof(wchar_t)));
    RegCloseKey(dst);
    if(RegOpenKeyExW(HKEY_CURRENT_USER,sk,0,KEY_SET_VALUE,&src)==ERROR_SUCCESS){
        RegDeleteValueW(src,nm.c_str()); RegCloseKey(src); }
    RefreshStartup();
}
static bool ShellRunsAtLogin(){
    wchar_t me[MAX_PATH]; GetModuleFileNameW(nullptr,me,MAX_PATH);
    return !RegString(HKEY_CURRENT_USER,RUNKEY,L"Aether").empty();
}
static void SetShellRunsAtLogin(bool on){
    HKEY k; if(RegCreateKeyExW(HKEY_CURRENT_USER,RUNKEY,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS) return;
    if(on){ wchar_t me[MAX_PATH]; GetModuleFileNameW(nullptr,me,MAX_PATH);
        std::wstring q=L"\""+std::wstring(me)+L"\"";
        RegSetValueExW(k,L"Aether",0,REG_SZ,(const BYTE*)q.c_str(),(DWORD)((q.size()+1)*sizeof(wchar_t))); }
    else RegDeleteValueW(k,L"Aether");
    RegCloseKey(k); RefreshStartup();
}

// ---- region / locale ----
static std::string LocaleStr(LCTYPE t){
    wchar_t b[128]={0};
    if(GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT,t,b,128)>0) return W2U8(b);
    return "-";
}
static std::string TimeZoneName(){
    TIME_ZONE_INFORMATION tz{}; DWORD r=GetTimeZoneInformation(&tz);
    const wchar_t* n=(r==TIME_ZONE_ID_DAYLIGHT)?tz.DaylightName:tz.StandardName;
    LONG bias=-(tz.Bias + (r==TIME_ZONE_ID_DAYLIGHT?tz.DaylightBias:tz.StandardBias));
    char buf[192]; snprintf(buf,192,"%s  (UTC%+03d:%02d)",W2U8(n).c_str(),bias/60,abs(bias)%60);
    return buf;
}

// ---- accessibility ----
static bool AxHighContrast(){ HIGHCONTRASTW h{sizeof(h)}; SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(h),&h,0); return (h.dwFlags&HCF_HIGHCONTRASTON)!=0; }
static bool AxSticky(){ STICKYKEYS s{sizeof(s)}; SystemParametersInfoW(SPI_GETSTICKYKEYS,sizeof(s),&s,0); return (s.dwFlags&SKF_STICKYKEYSON)!=0; }
static void AxSetSticky(bool on){ STICKYKEYS s{sizeof(s)}; SystemParametersInfoW(SPI_GETSTICKYKEYS,sizeof(s),&s,0);
    if(on) s.dwFlags|=SKF_STICKYKEYSON; else s.dwFlags&=~SKF_STICKYKEYSON; SystemParametersInfoW(SPI_SETSTICKYKEYS,sizeof(s),&s,SPIF_SENDCHANGE); }
static bool AxFilter(){ FILTERKEYS f{sizeof(f)}; SystemParametersInfoW(SPI_GETFILTERKEYS,sizeof(f),&f,0); return (f.dwFlags&FKF_FILTERKEYSON)!=0; }
static void AxSetFilter(bool on){ FILTERKEYS f{sizeof(f)}; SystemParametersInfoW(SPI_GETFILTERKEYS,sizeof(f),&f,0);
    if(on) f.dwFlags|=FKF_FILTERKEYSON; else f.dwFlags&=~FKF_FILTERKEYSON; SystemParametersInfoW(SPI_SETFILTERKEYS,sizeof(f),&f,SPIF_SENDCHANGE); }
static bool AxToggleKeys(){ TOGGLEKEYS t{sizeof(t)}; SystemParametersInfoW(SPI_GETTOGGLEKEYS,sizeof(t),&t,0); return (t.dwFlags&TKF_TOGGLEKEYSON)!=0; }
static void AxSetToggleKeys(bool on){ TOGGLEKEYS t{sizeof(t)}; SystemParametersInfoW(SPI_GETTOGGLEKEYS,sizeof(t),&t,0);
    if(on) t.dwFlags|=TKF_TOGGLEKEYSON; else t.dwFlags&=~TKF_TOGGLEKEYSON; SystemParametersInfoW(SPI_SETTOGGLEKEYS,sizeof(t),&t,SPIF_SENDCHANGE); }
static bool AxMouseKeys(){ MOUSEKEYS m{sizeof(m)}; SystemParametersInfoW(SPI_GETMOUSEKEYS,sizeof(m),&m,0); return (m.dwFlags&MKF_MOUSEKEYSON)!=0; }
static void AxSetMouseKeys(bool on){ MOUSEKEYS m{sizeof(m)}; SystemParametersInfoW(SPI_GETMOUSEKEYS,sizeof(m),&m,0);
    if(on) m.dwFlags|=MKF_MOUSEKEYSON; else m.dwFlags&=~MKF_MOUSEKEYSON; SystemParametersInfoW(SPI_SETMOUSEKEYS,sizeof(m),&m,SPIF_SENDCHANGE); }
static int  AxCursorSize(){ DWORD v=32,sz=sizeof(v);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Control Panel\\Cursors",L"CursorBaseSize",RRF_RT_REG_DWORD,nullptr,&v,&sz)!=ERROR_SUCCESS) v=32;
    return (int)v; }
static void AxSetCursorSize(int px){
    HKEY k; if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Control Panel\\Cursors",0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS) return;
    DWORD v=(DWORD)std::clamp(px,32,256); RegSetValueExW(k,L"CursorBaseSize",0,REG_DWORD,(const BYTE*)&v,sizeof(v));
    RegCloseKey(k); SystemParametersInfoW(SPI_SETCURSORS,0,nullptr,SPIF_SENDCHANGE);
}

// ---- users ----
static std::string CurrentUser(){ wchar_t b[UNLEN+1]={0}; DWORD n=UNLEN+1; GetUserNameW(b,&n); return W2U8(b); }
static std::string CurrentHost(){ wchar_t b[MAX_COMPUTERNAME_LENGTH+1]={0}; DWORD n=MAX_COMPUTERNAME_LENGTH+1; GetComputerNameW(b,&n); return W2U8(b); }
static bool IsAdminUser(){
    SID_IDENTIFIER_AUTHORITY nt=SECURITY_NT_AUTHORITY; PSID grp=nullptr; BOOL member=FALSE;
    if(AllocateAndInitializeSid(&nt,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&grp)){
        CheckTokenMembership(nullptr,grp,&member); FreeSid(grp); }
    return member!=FALSE;
}

// ---- windows update ----
static bool UpdateRebootPending(){
    HKEY k; if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired",0,KEY_READ,&k)==ERROR_SUCCESS){
        RegCloseKey(k); return true; }
    return false;
}
static std::string UpdateLastInstall(){
    std::wstring s=RegString(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\Results\\Install",L"LastSuccessTime");
    return s.empty()? "unknown" : W2U8(s);
}

// ---- firewall (netfw COM; reading works unelevated, writing needs admin) ----
static bool g_fwRead=false, g_fwOk=false, g_fwOn[3]={false,false,false};
static ULONGLONG g_fwErr=0;                    // "access denied" banner deadline
static const NET_FW_PROFILE_TYPE2 FW_PROF[3]={NET_FW_PROFILE2_DOMAIN,NET_FW_PROFILE2_PRIVATE,NET_FW_PROFILE2_PUBLIC};
static INetFwPolicy2* FwPolicy(){
    INetFwPolicy2* p=nullptr;
    if(FAILED(CoCreateInstance(__uuidof(NetFwPolicy2),nullptr,CLSCTX_INPROC_SERVER,__uuidof(INetFwPolicy2),(void**)&p))) return nullptr;
    return p;
}
static void RefreshFirewall(){
    g_fwRead=true; g_fwOk=false;
    INetFwPolicy2* p=FwPolicy(); if(!p) return;
    for(int i=0;i<3;i++){ VARIANT_BOOL b=VARIANT_FALSE; if(SUCCEEDED(p->get_FirewallEnabled(FW_PROF[i],&b))) g_fwOn[i]=(b!=VARIANT_FALSE); }
    p->Release(); g_fwOk=true;
}
static bool SetFirewall(int prof,bool on){
    INetFwPolicy2* p=FwPolicy(); if(!p) return false;
    bool ok=SUCCEEDED(p->put_FirewallEnabled(FW_PROF[prof],on?VARIANT_TRUE:VARIANT_FALSE));
    p->Release(); if(ok) RefreshFirewall(); return ok;
}

// ---- storage ----
struct DriveInfo{ std::string letter,label; unsigned long long used,total; UINT type; };
static std::vector<DriveInfo> g_drives; static ULONGLONG g_drivesAt=0;
static void RefreshDrives(){
    g_drives.clear(); g_drivesAt=GetTickCount64();
    DWORD mask=GetLogicalDrives();
    for(int i=0;i<26;i++){
        if(!(mask&(1u<<i))) continue;
        wchar_t root[4]={ (wchar_t)(L'A'+i), L':', L'\\', 0 };
        UINT t=GetDriveTypeW(root);
        if(t!=DRIVE_FIXED && t!=DRIVE_REMOVABLE && t!=DRIVE_REMOTE) continue;
        ULARGE_INTEGER freeAvail{},total{},freeTotal{};
        if(!GetDiskFreeSpaceExW(root,&freeAvail,&total,&freeTotal) || total.QuadPart==0) continue;
        wchar_t lbl[MAX_PATH]={0}; GetVolumeInformationW(root,lbl,MAX_PATH,nullptr,nullptr,nullptr,nullptr,0);
        DriveInfo d; d.letter=std::string(1,(char)('A'+i))+":"; d.label=lbl[0]?W2U8(lbl):"Local Disk";
        d.total=total.QuadPart; d.used=total.QuadPart-freeTotal.QuadPart; d.type=t;
        g_drives.push_back(d);
    }
}
static void EjectDrive(const std::string& letter){
    std::wstring path=L"\\\\.\\"+U82W(letter);
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    if(h==INVALID_HANDLE_VALUE) return;
    DWORD br=0;
    DeviceIoControl(h,FSCTL_LOCK_VOLUME,nullptr,0,nullptr,0,&br,nullptr);
    DeviceIoControl(h,FSCTL_DISMOUNT_VOLUME,nullptr,0,nullptr,0,&br,nullptr);
    DeviceIoControl(h,IOCTL_STORAGE_EJECT_MEDIA,nullptr,0,nullptr,0,&br,nullptr);
    CloseHandle(h);
}

// ---- printers ----
struct PrinterInfo{ std::string name,port,status; bool isDefault; };
static std::vector<PrinterInfo> g_printers; static bool g_printersRead=false;
static void RefreshPrinters(){
    g_printers.clear(); g_printersRead=true;
    DWORD need=0,got=0;
    EnumPrintersW(PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS,nullptr,2,nullptr,0,&need,&got);
    if(!need) return;
    std::vector<BYTE> buf(need);
    if(!EnumPrintersW(PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS,nullptr,2,buf.data(),need,&need,&got)) return;
    wchar_t def[512]={0}; DWORD dn=512; GetDefaultPrinterW(def,&dn);
    PRINTER_INFO_2W* pi=(PRINTER_INFO_2W*)buf.data();
    for(DWORD i=0;i<got;i++){
        PrinterInfo p; p.name=W2U8(pi[i].pPrinterName?pi[i].pPrinterName:L"");
        p.port=pi[i].pPortName? W2U8(pi[i].pPortName):"";
        DWORD st=pi[i].Status;
        p.status = st==0? "Ready" : (st&PRINTER_STATUS_OFFLINE)? "Offline" :
                   (st&PRINTER_STATUS_ERROR)? "Error" : (st&PRINTER_STATUS_PAPER_OUT)? "Out of paper" : "Busy";
        if(pi[i].cJobs) p.status += "  \xE2\x80\xA2  " + std::to_string(pi[i].cJobs) + " job(s)";
        p.isDefault = def[0] && pi[i].pPrinterName && _wcsicmp(pi[i].pPrinterName,def)==0;
        g_printers.push_back(p);
    }
}

// ---- about ----
static std::string OsVersionLine(){
    std::wstring pn=RegString(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"ProductName");
    std::wstring dv=RegString(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"DisplayVersion");
    std::wstring cb=RegString(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"CurrentBuild");
    DWORD ubr=0,sz=sizeof(ubr);
    RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"UBR",RRF_RT_REG_DWORD,nullptr,&ubr,&sz);
    std::string s=W2U8(pn);
    // the ProductName key still says "Windows 10" on 11; the build number is the honest signal
    if(!cb.empty() && _wtoi(cb.c_str())>=22000){ size_t p=s.find("Windows 10"); if(p!=std::string::npos) s.replace(p,10,"Windows 11"); }
    if(!dv.empty()) s += "  " + W2U8(dv);
    if(!cb.empty()) s += "  (build " + W2U8(cb) + "." + std::to_string(ubr) + ")";
    return s;
}
static std::string UptimeLine(){
    ULONGLONG ms=GetTickCount64(); int d=(int)(ms/86400000ULL), h=(int)((ms/3600000ULL)%24), m=(int)((ms/60000ULL)%60);
    char b[64]; if(d) snprintf(b,64,"%dd %dh %dm",d,h,m); else snprintf(b,64,"%dh %dm",h,m); return b;
}

static void SetRailIcon(ImDrawList* dl,int i,ImVec2 c,ImU32 col){
    switch(i){
    case SP_TUNEUP: {       // a wrench
        if(ThemedSym(dl,c,10.0f,col,"run-build-clean")) break;
        dl->AddLine(V(c.x-7,c.y+7),V(c.x+3,c.y-3),col,2.6f);
        dl->AddCircle(V(c.x+5,c.y-5),4.5f,col,0,2.2f);
        dl->AddLine(V(c.x+7,c.y-8),V(c.x+10,c.y-9),col,2.2f); } break;
    case SP_MACROS: {       // a key with a repeat arrow
        if(ThemedSym(dl,c,10.0f,col,"input-keyboard")) break;
        dl->AddRect(V(c.x-10,c.y-6),V(c.x+10,c.y+6),col,2,0,1.7f);
        dl->AddLine(V(c.x-5,c.y-1),V(c.x+5,c.y-1),col,1.6f);
        dl->AddLine(V(c.x-5,c.y+3),V(c.x+5,c.y+3),col,1.6f); } break;
    case SP_APPEARANCE: {   // palette
        dl->AddCircle(c,9,col,0,2.0f);
        dl->AddCircleFilled(V(c.x-3,c.y-4),2.0f,col); dl->AddCircleFilled(V(c.x+4,c.y-2),2.0f,col);
        dl->AddCircleFilled(V(c.x+1,c.y+5),2.0f,col); } break;
    case SP_WALLPAPER: {    // framed picture
        dl->AddRect(V(c.x-10,c.y-8),V(c.x+10,c.y+8),col,2,0,1.7f);
        dl->AddCircleFilled(V(c.x-4,c.y-3),1.8f,col);
        dl->AddTriangleFilled(V(c.x-8,c.y+6),V(c.x-1,c.y-2),V(c.x+6,c.y+6),col);
        dl->AddTriangleFilled(V(c.x+1,c.y+6),V(c.x+5,c.y+1),V(c.x+9,c.y+6),col); } break;
    case SP_EFFECTS: {      // sparkle
        auto star=[&](ImVec2 p,float r){
            dl->AddLine(V(p.x-r,p.y),V(p.x+r,p.y),col,1.7f);
            dl->AddLine(V(p.x,p.y-r),V(p.x,p.y+r),col,1.7f); };
        star(V(c.x-1,c.y-1),8); star(V(c.x+7,c.y+6),3.5f); star(V(c.x-8,c.y+7),2.6f); } break;
    case SP_TASKBAR: {      // vertical bar + pane
        dl->AddRect(V(c.x-9,c.y-9),V(c.x-3,c.y+9),col,2,0,1.8f);
        dl->AddRect(V(c.x-1,c.y-9),V(c.x+9,c.y+9),col,2,0,1.4f); } break;
    case SP_DASHBOARD: {    // 2x2 tiles
        dl->AddRect(V(c.x-9,c.y-9),V(c.x-1,c.y-1),col,2,0,1.6f);
        dl->AddRect(V(c.x+1,c.y-9),V(c.x+9,c.y-1),col,2,0,1.6f);
        dl->AddRect(V(c.x-9,c.y+1),V(c.x-1,c.y+9),col,2,0,1.6f);
        dl->AddRectFilled(V(c.x+1,c.y+1),V(c.x+9,c.y+9),col,2); } break;
    case SP_LAUNCHER: {     // magnifier
        dl->AddCircle(V(c.x-2,c.y-2),6.5f,col,0,2.0f); dl->AddLine(V(c.x+3,c.y+3),V(c.x+8,c.y+8),col,2.0f); } break;
    case SP_NOTIF: {        // bell
        dl->PathArcTo(V(c.x,c.y+2),7,3.1416f,6.2832f,14); dl->PathStroke(col,0,1.8f);
        dl->AddLine(V(c.x-7,c.y+2),V(c.x-7,c.y+5),col,1.8f); dl->AddLine(V(c.x+7,c.y+2),V(c.x+7,c.y+5),col,1.8f);
        dl->AddLine(V(c.x-9,c.y+5),V(c.x+9,c.y+5),col,1.8f); dl->AddCircleFilled(V(c.x,c.y+8),2.0f,col); } break;
    case SP_WORKSPACES: {   // 2x2 desktops, current one filled
        dl->AddRectFilled(V(c.x-9,c.y-8),V(c.x-1,c.y-2),col,1.5f);
        dl->AddRect(V(c.x+1,c.y-8),V(c.x+9,c.y-2),col,1.5f,0,1.5f);
        dl->AddRect(V(c.x-9,c.y+2),V(c.x-1,c.y+8),col,1.5f,0,1.5f);
        dl->AddRect(V(c.x+1,c.y+2),V(c.x+9,c.y+8),col,1.5f,0,1.5f); } break;
    case SP_WINDOWS: {      // overlapping windows
        dl->AddRect(V(c.x-9,c.y-8),V(c.x+2,c.y+2),col,2,0,1.6f);
        dl->AddLine(V(c.x-9,c.y-5),V(c.x+2,c.y-5),col,1.4f);
        dl->AddRectFilled(V(c.x-2,c.y-2),V(c.x+9,c.y+8),PanelCol(255),2);
        dl->AddRect(V(c.x-2,c.y-2),V(c.x+9,c.y+8),col,2,0,1.6f);
        dl->AddLine(V(c.x-2,c.y+1),V(c.x+9,c.y+1),col,1.4f); } break;
    case SP_SHORTCUTS: {    // keycap
        dl->AddRect(V(c.x-10,c.y-7),V(c.x+10,c.y+7),col,3,0,1.7f);
        dl->AddLine(V(c.x-6,c.y-2),V(c.x+6,c.y-2),col,1.5f);
        dl->AddLine(V(c.x-3,c.y+3),V(c.x+3,c.y+3),col,1.5f); } break;
    case SP_DISPLAY: {      // monitor
        dl->AddRect(V(c.x-10,c.y-8),V(c.x+10,c.y+5),col,2,0,1.7f);
        dl->AddLine(V(c.x-5,c.y+9),V(c.x+5,c.y+9),col,1.8f);
        dl->AddCircleFilled(c,2.6f,col); } break;
    case SP_AUDIO: SpeakerIcon(dl,c,col); break;
    case SP_INPUT: {        // mouse
        dl->AddRect(V(c.x-6,c.y-9),V(c.x+6,c.y+9),col,6,0,1.7f);
        dl->AddLine(V(c.x,c.y-7),V(c.x,c.y-2),col,1.7f); } break;
    case SP_NETWORK: WifiIcon(dl,V(c.x,c.y-3),col); break;
    case SP_BLUETOOTH: BtIcon(dl,c,col); break;
    case SP_POWER: {        // battery
        dl->AddRect(V(c.x-10,c.y-5),V(c.x+7,c.y+5),col,2,0,1.7f);
        dl->AddRectFilled(V(c.x+7,c.y-2),V(c.x+10,c.y+2),col,1);
        dl->AddRectFilled(V(c.x-8,c.y-3),V(c.x-1,c.y+3),col,1); } break;
    case SP_STORAGE: {      // disk platters (a cylinder drawn from squashed arcs)
        auto oval=[&](float cy){
            dl->PathClear();
            for(int s=0;s<=24;s++){ float t=s/24.0f*6.2832f; dl->PathLineTo(V(c.x+cosf(t)*9.0f,cy+sinf(t)*3.2f)); }
            dl->PathStroke(col,ImDrawFlags_Closed,1.6f); };
        oval(c.y-6.0f);
        dl->AddLine(V(c.x-9,c.y-6),V(c.x-9,c.y+5),col,1.6f);
        dl->AddLine(V(c.x+9,c.y-6),V(c.x+9,c.y+5),col,1.6f);
        oval(c.y+5.0f); } break;
    case SP_PRINTERS: {     // printer
        dl->AddRect(V(c.x-9,c.y-2),V(c.x+9,c.y+6),col,2,0,1.7f);
        dl->AddRect(V(c.x-6,c.y-9),V(c.x+6,c.y-2),col,1,0,1.5f);
        dl->AddRect(V(c.x-5,c.y+4),V(c.x+5,c.y+10),col,1,0,1.5f); } break;
    case SP_STARTUP: {      // power symbol
        dl->PathArcTo(c,8,-2.15f,-0.99f+3.1416f*2,20); dl->PathStroke(col,0,1.8f);
        dl->AddLine(V(c.x,c.y-10),V(c.x,c.y-2),col,1.8f); } break;
    case SP_REGION: {       // globe
        dl->AddCircle(c,9,col,0,1.7f);
        dl->AddLine(V(c.x-9,c.y),V(c.x+9,c.y),col,1.3f);
        dl->PathClear();
        for(int s=0;s<=24;s++){ float t=s/24.0f*6.2832f; dl->PathLineTo(V(c.x+cosf(t)*4.2f,c.y+sinf(t)*9.0f)); }
        dl->PathStroke(col,ImDrawFlags_Closed,1.3f); } break;
    case SP_ACCESS: {       // accessibility figure
        dl->AddCircle(c,9,col,0,1.4f);
        dl->AddCircleFilled(V(c.x,c.y-5.2f),1.9f,col);
        dl->AddLine(V(c.x-5,c.y-1.5f),V(c.x+5,c.y-1.5f),col,1.7f);
        dl->AddLine(V(c.x,c.y-2),V(c.x-3.5f,c.y+5.5f),col,1.7f);
        dl->AddLine(V(c.x,c.y-2),V(c.x+3.5f,c.y+5.5f),col,1.7f); } break;
    case SP_USERS: {        // person bust
        dl->AddCircle(V(c.x,c.y-4),4.4f,col,0,1.7f);
        dl->PathArcTo(V(c.x,c.y+11),8.4f,3.1416f,6.2832f,16); dl->PathStroke(col,0,1.7f); } break;
    case SP_UPDATES: {      // circular arrow
        dl->PathArcTo(c,8,-1.1f,3.6f,22); dl->PathStroke(col,0,1.8f);
        dl->AddTriangleFilled(V(c.x+4,c.y-9),V(c.x+10,c.y-6.5f),V(c.x+4.5f,c.y-3),col); } break;
    case SP_FIREWALL: {     // shield
        dl->PathLineTo(V(c.x-8,c.y-7)); dl->PathLineTo(V(c.x,c.y-9)); dl->PathLineTo(V(c.x+8,c.y-7));
        dl->PathLineTo(V(c.x+8,c.y+1)); dl->PathLineTo(V(c.x,c.y+9)); dl->PathLineTo(V(c.x-8,c.y+1));
        dl->PathStroke(col,ImDrawFlags_Closed,1.7f); } break;
    case SP_ABOUT: {        // info
        dl->AddCircle(c,9,col,0,1.7f);
        dl->AddCircleFilled(V(c.x,c.y-4),1.6f,col);
        dl->AddLine(V(c.x,c.y-1),V(c.x,c.y+5),col,1.9f); } break;
    case SP_PRESETS: {      // sliders
        dl->AddLine(V(c.x-9,c.y-5),V(c.x+9,c.y-5),col,1.7f);
        dl->AddLine(V(c.x-9,c.y+4),V(c.x+9,c.y+4),col,1.7f);
        dl->AddCircleFilled(V(c.x-3,c.y-5),3.0f,col);
        dl->AddCircleFilled(V(c.x+4,c.y+4),3.0f,col); } break;
    case SP_MONITORS: {     // two screens
        dl->AddRect(V(c.x-10,c.y-8),V(c.x+3,c.y+1),col,2.0f,0,1.6f);
        dl->AddRect(V(c.x-3,c.y-1),V(c.x+10,c.y+8),col,2.0f,0,1.6f); } break;
    case SP_PLUGINS: {      // puzzle piece
        dl->AddRect(V(c.x-8,c.y-8),V(c.x+8,c.y+8),col,3.0f,0,1.7f);
        dl->AddCircleFilled(V(c.x+8,c.y-2),3.4f,col);
        dl->AddCircleFilled(V(c.x-2,c.y+8),3.4f,col); } break;
    default: TabIcon(dl,0,c,col); break;
    }
}

#include "src/modules/settings/SettingsFx.h"   // About card, nebula, stripe transition, search index
static void DrawSettings(){
    { static ULONGLONG lastDraw=0; ULONGLONG nowD=GetTickCount64();
      if(g_setShow && nowD-lastDraw>600 && !g_tourSeen){ g_setPage=SP_ABOUT; g_setScroll=0; g_setScrollT=0; TourStart(); g_tourSeen=true; SaveConfig(); }
      lastDraw=nowD; }
    if(g_tourOn && g_setHiliteHold && !g_setHilite.empty()){ ULONGLONG nh=GetTickCount64(); if(nh-g_setHiliteAt>2400) g_setHiliteAt=nh-600; }   // the tour's highlight keeps pulsing
    if(g_tourDo && g_setShow && GetTickCount64()-g_tourDoAt>900) g_tourDo=false;   // Settings opened by hand during a "try it" step
    if(g_ipcTour>=-1){ if(g_ipcTour<0) TourEnd(); else { g_tourOn=true; TourGo(std::clamp(g_ipcTour,0,TourCount()-1)); } g_ipcTour=-2; }
    if(g_ipcSys>=0){ g_sysInfoOpen=g_ipcSys!=0; g_ipcSys=-1; }
    if(g_ipcTourTry){ g_ipcTourTry=false; if(g_tourOn) TourBeginDo(); }
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x,H=io.DisplaySize.y;
    float a=std::clamp(std::max(g_setAnim, FrameBornOn()? std::min(g_setReveal,1.0f) : 0.0f),0.0f,1.0f); if(a<0.004f){ g_setRect=RECT{0,0,0,0}; return; }
    float e=EaseOutCubic(a); int al=(int)(e*255);
    if(!io.MouseDown[0] && GetTickCount64()-g_setOpenAt>150) g_setArmed=true;
    bool click=io.MouseClicked[0] && g_setArmed, down=io.MouseDown[0], rel=io.MouseReleased[0];
    auto A=[&](ImU32 c){ return MulA(c,e); };

    // Caelestia: Settings comes out of the bottom border like every other panel - one surface with the frame
    // holding both cards, over the live desktop (no screen-wide blur/scrim, no warp; the reveal is the motion)
    const bool setBorn = FrameBornOn();
    if(!setBorn){
    if(g_setBg) dl->AddImage((ImTextureID)g_setBg,V(0,0),V(W,H),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(e*255)));
    dl->AddRectFilled(V(0,0),V(W,H),IM_COL32(8,8,12,(int)(e*150)));
    }

    // the panel scales up slightly as it opens, so it reads as coming toward the viewer
    float grow= setBorn? 1.0f : 0.965f+0.035f*(g_setShow?EaseOutBack(a):e);
    float pw0=std::min(W-120.0f,g_setStyle==1? 1300.0f : 1180.0f), ph0=std::min(H-140.0f,g_setStyle==1? 740.0f : 690.0f);
    float pw=pw0*grow, ph=ph0*grow;
    // bottom-weighted and rising, not planted dead centre
    float px=(W-pw)/2, py=H-ph-((float)g_gap+52.0f) + (1.0f-e)*(ph*0.22f+60.0f);
    if(setBorn){
        const float PADS=14.0f;
        float bB=FrameInset(EDGE_BOTTOM);
        float rv=std::min(g_setReveal,1.3f);
        float depth=ph+PADS*2.0f;
        float top=H-bB-depth*rv;
        py=top+PADS;
        FrameEdgePanel(dl,FrameMon(),V(0,0),V(W,H),EDGE_BOTTOM,px-PADS,px+pw+PADS,bB+depth*rv,bB,34.0f,1.0f);
        dl->PushClipRect(V(px-PADS,std::min(top,H-bB)),V(px+pw+PADS,H-bB),true);
    }
    int vwarp=dl->VtxBuffer.Size;
    // TWO detached cards, as in the reference: the settings card, and the wallpaper card beside it
    const float cardGapC=16.0f;
    float wallW = (pw>980 && g_setStyle!=1)? 372.0f : 0.0f;
    float mainW = pw - (wallW>0? wallW+cardGapC : 0.0f);
    const bool tourClick=click; if(TourMiniContains(io.MousePos,V(px,py),V(px+mainW,py+ph))) click=false;   // the tour card over the page takes its own clicks
    float wallX = px+mainW+cardGapC;
    GlassPanel(dl,V(px,py),V(px+mainW,py+ph),26,0,nullptr,e);
    if(wallW>0) GlassPanel(dl,V(wallX,py),V(wallX+wallW,py+ph),26,0,nullptr,e);

    // ---------- column 1: the sidebar (grouped + scrollable, like Plasma's category list) ----------
    float railW= g_setStyle==1? std::min(360.0f,mainW*0.32f) : 186.0f, railX=px;
    if(g_setStyle==1){
        // ---- caelestia: search field + category cards ----
        const float SR=20.0f;
        float aboutH = g_setAboutCard? 92.0f : 0.0f;
        if(aboutH>0 && g_setPage==SP_ABOUT){ float gl=0.5f+0.5f*sinf((float)ImGui::GetTime()*2.0f);
            dl->AddRect(V(railX+11,py+11),V(railX+railW-5,py+17+aboutH),WithA(COL_GOLD,(int)((120+80*gl)*e)),20,0,2.2f); }
        if(aboutH>0){ if(DrawAboutCard(dl,io,V(railX+14,py+14),V(railX+railW-8,py+14+aboutH),e,click,false)){
            g_setPagePrev=g_setPage; g_setPage=SP_ABOUT; g_setPageAnim=0; g_setScroll=0; g_setScrollT=0; } }
        ImVec2 sa=V(railX+14,py+14+(aboutH>0? aboutH+10 : 0)), sb=V(railX+railW-8,sa.y+46);
        bool sh=io.MousePos.x>=sa.x&&io.MousePos.x<sb.x&&io.MousePos.y>=sa.y&&io.MousePos.y<sb.y;
        if(click) g_setSearchFocus=sh;
        dl->AddRectFilled(sa,sb,A(Mix(COL_CARD,IM_COL32(0,0,0,255),g_darkUI? 0.35f : 0.05f)),23);
        dl->AddRect(sa,sb,WithA(g_setSearchFocus? COL_GOLD : COL_INK2,(int)((g_setSearchFocus? 180 : 50)*e)),23,0,1.3f);
        MsIcon(dl,"search",V(sa.x+24,(sa.y+sb.y)*0.5f),22,A(COL_INK));
        if(g_setSearchFocus){
            size_t len=strlen(g_setSearch);
            for(int k=0;k<io.InputQueueCharacters.Size;k++){ ImWchar c=io.InputQueueCharacters[k]; if(c>=32&&c<127&&len<sizeof(g_setSearch)-1){ g_setSearch[len++]=(char)c; g_setSearch[len]=0; } }
            if(ImGui::IsKeyPressed(ImGuiKey_Backspace)&&len>0) g_setSearch[--len]=0;
            if(ImGui::IsKeyPressed(ImGuiKey_Escape)){ if(g_setSearch[0]) g_setSearch[0]=0; else g_setSearchFocus=false; }
        }
        if(g_setSearch[0]) TextAt(dl,g_fReg,17,V(sa.x+46,(sa.y+sb.y)*0.5f-11),A(COL_INK),g_setSearch);
        else TextAt(dl,g_fReg,17,V(sa.x+46,(sa.y+sb.y)*0.5f-11),A(COL_INK),"Search settings");
        if(g_setSearchFocus && fmodf((float)GetTickCount64()/530.0f,2.0f)<1.0f){ float cw2=g_setSearch[0]? TextW(g_fReg,17,g_setSearch) : 0.0f;
            dl->AddRectFilled(V(sa.x+46+cw2+1,sa.y+13),V(sa.x+47.6f+cw2+1,sb.y-13),A(COL_INK)); }
        const float ROW=58.0f;
        float railTop=sb.y+14, railBot=py+ph-14, railViewH=railBot-railTop;
        { bool overRail = io.MousePos.x>railX && io.MousePos.x<railX+railW && io.MousePos.y>railTop && io.MousePos.y<railBot;
          if(overRail && io.MouseWheel!=0) g_railScrollT -= io.MouseWheel*70.0f;
          g_railScrollT=std::clamp(g_railScrollT,0.0f,std::max(0.0f,g_railContentH-railViewH+12.0f));
          Approach(g_railScroll,g_railScrollT,18.0f); }
        dl->PushClipRect(V(railX,railTop-2),V(railX+railW,railBot),true);
        float ry=railTop-g_railScroll;
        std::vector<int> shown;
        for(int i=0;i<NSETPAGES;i++){
            if(g_setSearch[0] && !StrStrIA(SET_PAGES[i],g_setSearch) && !StrStrIA(SET_SUBS[i],g_setSearch)) continue;
            if(i==SP_ABOUT && aboutH>0 && !g_setSearch[0]) continue;      // the Aether card at the top is this tab
            shown.push_back(i); }
        if(g_setSearch[0] && !shown.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter)){ g_setPagePrev=g_setPage; g_setPage=shown[0]; g_setPageAnim=0; g_setScroll=0; g_setScrollT=0; }
        for(size_t q=0;q<shown.size();q++){
            int i=shown[q];
            bool groupStart = q==0; for(const SetGroup& g:SET_GROUPS) if(g.at==i && q>0){ groupStart=true; ry+=10; }
            bool groupEnd = q+1==shown.size(); for(const SetGroup& g:SET_GROUPS) if(q+1<shown.size() && g.at==shown[q+1]) groupEnd=true;
            float sa2=EaseOutCubic(Stagger(a,(int)q,0.018f,0.55f));
            float ty=ry+(1.0f-sa2)*10.0f;
            ImVec2 ra=V(railX+14,ty), rb=V(railX+railW-8,ty+ROW-2);
            bool hov=io.MousePos.x>=ra.x&&io.MousePos.x<rb.x&&io.MousePos.y>=ra.y&&io.MousePos.y<rb.y && io.MousePos.y>railTop && io.MousePos.y<railBot;
            bool sel=(i==g_setPage);
            float ha=HoverAnim(8000+i,hov&&!sel);
            ImDrawFlags fl = (groupStart&&groupEnd)? ImDrawFlags_RoundCornersAll : groupStart? ImDrawFlags_RoundCornersTop : groupEnd? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersNone;
            ImU32 base=Mix(COL_CARD,COL_CARD2,0.35f);
            ImU32 bg = sel? Mix(COL_CARD2,COL_INK2,0.30f) : Mix(base,COL_CARD2,0.6f*ha);
            dl->AddRectFilled(ra,rb,WithA(bg,(int)(255*e*sa2)),SR,sel? ImDrawFlags_RoundCornersAll : fl);
            ImVec2 ic=V(ra.x+28,(ra.y+rb.y)*0.5f);
            dl->AddCircleFilled(ic,18,WithA(sel? Mix(COL_INK,COL_GOLD,0.15f) : Mix(COL_CARD2,COL_INK2,0.18f),(int)(255*e*sa2)),24);
            SetRailIcon(dl,i,ic,WithA(sel? M3OnPrimary() : COL_INK,(int)(al*sa2)));
            TextAt(dl,g_fMed,16.5f,V(ra.x+56,ra.y+9),WithA(COL_INK,(int)(al*sa2)),Clip(g_fMed,16.5f,SET_PAGES[i],rb.x-ra.x-66).c_str());
            TextAt(dl,g_fSml,13,V(ra.x+56,ra.y+31),WithA(COL_INK2,(int)(al*sa2)),Clip(g_fSml,13,SET_SUBS[i],rb.x-ra.x-66).c_str());
            if(click&&hov&&!sel){ g_setPagePrev=g_setPage; g_setPage=i; g_setPageAnim=0.0f; g_setScroll=0; g_setScrollT=0; }
            ry+=ROW;
        }
        bool anyHit=false;
        if(g_setSearch[0]){
            std::vector<int> hits; for(int k=0;k<SET_INDEX_N;k++) if(StrStrIA(SET_INDEX[k].label,g_setSearch)) hits.push_back(k);
            if(hits.empty()){ for(int k=0;k<SET_INDEX_N;k++){ std::string lab=SET_INDEX[k].label; std::string q=g_setSearch;
                    // every word of the query somewhere in the label ("linux title" finds "Linux-style title bars")
                    bool all=true; size_t i2=0; while(i2<q.size()){ size_t e2=q.find(' ',i2); std::string w=q.substr(i2,e2==std::string::npos? std::string::npos : e2-i2);
                        if(!w.empty() && !StrStrIA(lab.c_str(),w.c_str())){ all=false; break; } if(e2==std::string::npos) break; i2=e2+1; }
                    if(all) hits.push_back(k); } }
            anyHit=!hits.empty();
            if(anyHit){
                ry+=12; TextAt(dl,g_fMed,14,V(railX+22,ry),A(COL_INK2),"Settings"); ry+=26;
                if(shown.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter)){ const SetIndexEntry& en=SET_INDEX[hits[0]];
                    g_setPagePrev=g_setPage; g_setPage=en.page; g_setPageAnim=0; g_setScroll=0; g_setScrollT=0;
                    g_setHilite=en.label; g_setHiliteAt=GetTickCount64(); g_setHiliteScrolled=false; }
                for(size_t q=0;q<hits.size() && q<40;q++){
                    const SetIndexEntry& en=SET_INDEX[hits[q]];
                    ImVec2 ra=V(railX+14,ry), rb=V(railX+railW-8,ry+52);
                    bool hov=io.MousePos.x>=ra.x&&io.MousePos.x<rb.x&&io.MousePos.y>=ra.y&&io.MousePos.y<rb.y && io.MousePos.y>railTop && io.MousePos.y<railBot;
                    float ha=HoverAnim(8600+(int)q,hov);
                    dl->AddRectFilled(ra,rb,WithA(Mix(Mix(COL_CARD,COL_CARD2,0.35f),COL_CARD2,0.6f*ha),(int)(255*e)),14);
                    ImVec2 ic=V(ra.x+26,(ra.y+rb.y)*0.5f);
                    dl->AddCircleFilled(ic,16,A(Mix(COL_CARD2,COL_INK2,0.18f)),24);
                    SetRailIcon(dl,en.page,ic,A(COL_INK));
                    TextAt(dl,g_fMed,15,V(ra.x+52,ra.y+8),A(COL_INK),Clip(g_fMed,15,en.label,rb.x-ra.x-62).c_str());
                    std::string where=std::string("in ")+SET_PAGES[en.page];
                    TextAt(dl,g_fSml,12.5f,V(ra.x+52,ra.y+29),A(COL_INK2),Clip(g_fSml,12.5f,where,rb.x-ra.x-62).c_str());
                    if(click&&hov){ g_setPagePrev=g_setPage; g_setPage=en.page; g_setPageAnim=0; g_setScroll=0; g_setScrollT=0;
                        g_setHilite=en.label; g_setHiliteAt=GetTickCount64(); g_setHiliteScrolled=false; }
                    ry+=56;
                }
            }
        }
        if(shown.empty() && !anyHit) TextAt(dl,g_fSml,14,V(railX+26,railTop+10),A(COL_INK2),"No settings match");
        dl->PopClipRect();
        g_railContentH=(ry+g_railScroll)-railTop;
    } else {
    dl->AddRectFilled(V(railX,py),V(railX+railW,py+ph),A(Mix(COL_PANELL,COL_CARD,0.6f)),26,ImDrawFlags_RoundCornersLeft);
    const float ROWH=40.0f, HDRH=28.0f;
    float railTop=py+18, railBot=py+ph-14, railViewH=railBot-railTop;
    { bool overRail = io.MousePos.x>railX && io.MousePos.x<railX+railW && io.MousePos.y>railTop && io.MousePos.y<railBot;
      if(overRail && io.MouseWheel!=0) g_railScrollT -= io.MouseWheel*70.0f;
      g_railScrollT=std::clamp(g_railScrollT,0.0f,std::max(0.0f,g_railContentH-railViewH+12.0f));
      Approach(g_railScroll,g_railScrollT,18.0f); }
    dl->PushClipRect(V(railX,railTop-2),V(railX+railW,railBot),true);
    float ry=railTop-g_railScroll;
    for(int i=0;i<NSETPAGES;i++){
        for(const SetGroup& g:SET_GROUPS) if(g.at==i){
            TextAt(dl,g_fSml,11,V(railX+22,ry+9),WithA(COL_INK2,(int)(al*0.75f)),g.label);
            ry+=HDRH; }
        float sa=EaseOutCubic(Stagger(a,i,0.018f,0.55f));
        float ty=ry+(1.0f-sa)*10.0f;
        bool hov=io.MousePos.x>railX+8&&io.MousePos.x<railX+railW-8&&io.MousePos.y>ry&&io.MousePos.y<ry+ROWH-4
                 && io.MousePos.y>railTop && io.MousePos.y<railBot;
        bool sel=(i==g_setPage);
        float ha=HoverAnim(8000+i,hov&&!sel);
        if(sel) dl->AddRectFilled(V(railX+10,ty),V(railX+railW-10,ty+ROWH-5),A(COL_GOLD),11);
        else if(ha>0.01f) dl->AddRectFilled(V(railX+10,ty),V(railX+railW-10,ty+ROWH-5),WithA(COL_INK2,(int)(ha*40*e)),11);
        ImU32 fg = sel ? (g_darkUI?IM_COL32(12,20,14,255):IM_COL32(250,254,252,255)) : COL_INK;
        SetRailIcon(dl,i,V(railX+30,ty+(ROWH-5)*0.5f),WithA(sel?fg:COL_INK2,(int)(al*sa)));
        const char* lb=SET_PAGES[i];
        std::string ls=lb; while(!ls.empty()&&TextW(g_fSml,13,ls.c_str())>railW-72) ls.pop_back();
        TextAt(dl,g_fSml,13,V(railX+48,ty+9),WithA(fg,(int)(al*sa)),ls.c_str());
        if(click&&hov&&!sel){ g_setPagePrev=g_setPage; g_setPage=i; g_setPageAnim=0.0f; g_setScroll=0; g_setScrollT=0; }
        ry+=ROWH;
    }
    dl->PopClipRect();
    g_railContentH=(ry+g_railScroll)-railTop;
    if(g_railContentH>railViewH){
        float trackH=railViewH-8, th3=std::max(28.0f,trackH*railViewH/g_railContentH);
        float t1=railTop+4+(trackH-th3)*(g_railScroll/std::max(1.0f,g_railContentH-railViewH));
        dl->AddRectFilled(V(railX+railW-7,railTop+4),V(railX+railW-4,railTop+4+trackH),WithA(COL_INK2,(int)(34*e)),2);
        dl->AddRectFilled(V(railX+railW-7,t1),V(railX+railW-4,t1+th3),WithA(COL_GOLD,(int)(170*e)),2);
    }
    }   // classic rail
    // page transition: the middle column slides up and fades on every page change - or, with the stripes
    // transition, stripes sweep over the window and the new page waits underneath until they leave
    // The page you clicked is held back: the stripes sweep over the CURRENT page, the switch happens under full
    // cover halfway through, and the stripes sweep away off the new page.
    { static int lastPage=-1; if(lastPage<0) lastPage=g_setPage;
      if(g_setPage!=lastPage){
          bool open = GetTickCount64()-g_setOpenAt>300;
          if(g_setTransition==0 && open){
              g_setPagePending=g_setPage; g_setPage=lastPage;                 // keep showing the old page for now
              g_setScroll=g_setScrollKeep; g_setScrollT=g_setScrollTKeep;       // the click reset its scroll: undo that
              if(!g_setWipeAt || (float)(GetTickCount64()-g_setWipeAt)/(float)std::max(300,g_setTransMs)>0.45f) g_setWipeAt=GetTickCount64();
          } else lastPage=g_setPage;
      }
      if(g_setWipeAt && g_setPagePending>=0){
          float t0=(float)(GetTickCount64()-g_setWipeAt)/(float)std::max(300,g_setTransMs);
          if(t0>=0.45f){ g_setPage=g_setPagePending; lastPage=g_setPage; g_setPagePending=-1;
              g_setScroll=0; g_setScrollT=0; g_setPageAnim=0.0f;
              if(!g_setHilite.empty()){ g_setHiliteAt=GetTickCount64(); g_setHiliteScrolled=false; } }
      } }
    float wipeT = g_setWipeAt? (float)(GetTickCount64()-g_setWipeAt)/(float)std::max(300,g_setTransMs) : 1.0f;
    if(wipeT>=1.0f){ g_setWipeAt=0; wipeT=1.0f; }
    if(g_setWipeAt && wipeT>=0.45f && wipeT<0.60f) g_setPageAnim=0.0f;
    else if(g_setTransition==2 || (g_setWipeAt && wipeT<0.45f)) g_setPageAnim=1.0f;
    Approach(g_setPageAnim,1.0f,13.0f);
    float pgE=EaseOutCubic(std::clamp(g_setPageAnim,0.0f,1.0f));
    float pgSlide=(1.0f-pgE)*18.0f; int pgAl=(int)(al*pgE);

    // ---------- the detached wallpaper card ----------
    if(wallW>0){
        WallScan();
        { const char* wt="Wallpaper";
          TextAt(dl,g_fMed,20,V(wallX+wallW*0.5f-TextW(g_fMed,20,wt)/2,py+22),WithA(COL_INK,al),wt); }
        int cols=3; float pad=10, cw=(wallW-40-(cols-1)*pad)/cols, chh=cw*0.62f;
        float gx=wallX+20, gy=py+60;
        float bot=py+ph-20;
        int rows=(int)((bot-gy)/(chh+pad));
        // snapshot the cards under the lock, then draw without holding it - the click handler
        // below can run arbitrarily long work (SetWallpaperFile) and must not block the worker
        struct WallView{ ID3D11ShaderResourceView* tex; int w,h; std::wstring path; std::string name; };
        static std::vector<WallView> view; view.clear();
        if(g_wallReady.load(std::memory_order_acquire)){
            std::lock_guard<std::mutex> lk(g_wallMtx);
            int n=std::min((int)g_walls.size(),rows*cols);
            for(int i=0;i<n;i++) view.push_back({g_walls[i].tex,g_walls[i].w,g_walls[i].h,
                                                 g_walls[i].path,g_walls[i].name}); }
        int shown=(int)view.size();
        WallThumbs(shown);        // only the visible cards are worth decoding
        if(shown==0) TextAt(dl,g_fSml,13,V(wallX+20,py+64),WithA(COL_INK2,al),"Scanning\xE2\x80\xA6");
        wchar_t cur[MAX_PATH]={0}; SystemParametersInfoW(SPI_GETDESKWALLPAPER,MAX_PATH,cur,0);
        dl->PushClipRect(V(wallX+6,gy-4),V(wallX+wallW-6,bot),true);
        for(int i=0;i<shown;i++){
            int r=i/cols,c=i%cols; float x0=gx+c*(cw+pad), y0=gy+r*(chh+pad);
            ImVec2 a0=V(x0,y0),b0=V(x0+cw,y0+chh);
            bool hov=io.MousePos.x>a0.x&&io.MousePos.x<b0.x&&io.MousePos.y>a0.y&&io.MousePos.y<b0.y;
            float ha=HoverAnim(8100+i,hov);
            WallView& w=view[i];
            if(w.tex){ ImVec2 uv0,uv1; CoverUV(w.w,w.h,cw,chh,uv0,uv1);
                dl->AddImageRounded((ImTextureID)w.tex,a0,b0,uv0,uv1,IM_COL32(255,255,255,al),10); }
            else dl->AddRectFilled(a0,b0,A(COL_CARD2),10);
            bool isCur = cur[0] && _wcsicmp(w.path.c_str(),cur)==0;
            if(isCur||ha>0.01f) dl->AddRect(a0,b0,WithA(COL_GOLD,(int)(al*(isCur?1.0f:ha))),10,0,2.5f);
            dl->AddRectFilled(V(a0.x,b0.y-18),V(b0.x,b0.y),IM_COL32(0,0,0,(int)(al*0.5f)),10,ImDrawFlags_RoundCornersBottom);
            std::string nm=w.name; while(!nm.empty()&&TextW(g_fSml,11,nm.c_str())>cw-12)nm.pop_back();
            TextAt(dl,g_fSml,11,V(a0.x+6,b0.y-15),IM_COL32(248,250,249,al),nm.c_str());
            if(click&&hov) SetWallpaperFile(w.path,DeskFromActive(V((a0.x+b0.x)/2,(a0.y+b0.y)/2)));
        }
        dl->PopClipRect();
    }

    // ---------- column 2: the active page ----------
    float mx=railX+railW+26, mw=(px+mainW-30)-mx;
    float my=py+26+pgSlide;
    TextAt(dl,g_fBig,26,V(mx,my),WithA(COL_INK,al),SET_PAGES[g_setPage]);
    my+=46;
    // the longer pages (Wallpaper especially) scroll; wheel over the middle column drives it
    float viewTop=py+58, viewBot=py+ph-14, viewH=viewBot-viewTop;
    { bool overMid = io.MousePos.x>mx-16 && io.MousePos.x<mx+mw+16 && io.MousePos.y>viewTop && io.MousePos.y<viewBot;
      if(overMid && io.MouseWheel!=0) g_setScrollT -= io.MouseWheel*58.0f;
      g_setScrollT=std::clamp(g_setScrollT,0.0f,std::max(0.0f,g_setContentH-viewH+20.0f));
      Approach(g_setScroll,g_setScrollT,18.0f); }
    float myStart=my; my-=g_setScroll;
    float rowW=mw;
    int idBase=8200+g_setPage*100, idc=0;

    // small immediate-mode controls
    auto rowHit=[&](float y,float h){ return io.MousePos.x>mx&&io.MousePos.x<mx+rowW&&io.MousePos.y>y&&io.MousePos.y<y+h; };
    // a setting reached from the search: scroll it into view and flash it
    auto hl=[&](const char* label,float h){
        if(g_setHilite.empty() || !label || g_setHilite!=label) return;
        ULONGLONG age=GetTickCount64()-g_setHiliteAt; if(age>3400){ g_setHilite.clear(); return; }
        if(!g_setHiliteScrolled){ g_setScrollT=std::max(0.0f,(my+g_setScroll)-myStart-90.0f); g_setHiliteScrolled=true; }
        float pulse=0.5f+0.5f*sinf(age*0.012f), fade= age>2800? 1.0f-(age-2800)/600.0f : 1.0f;
        dl->AddRectFilled(V(mx-12,my-2),V(mx+rowW+2,my+h-1),WithA(COL_GOLD,(int)((40+50*pulse)*fade*e)),12);
        dl->AddRect(V(mx-12,my-2),V(mx+rowW+2,my+h-1),WithA(COL_GOLD,(int)(230*fade*e)),12,0,2.0f); };
    auto toggle=[&](const char* label,const char* sub,bool& val)->bool{
        float h=sub?52.0f:42.0f; bool hov=rowHit(my,h); float ha=HoverAnim(idBase+(idc++),hov);
        hl(label,h);
        if(g_setStyle==1){
            dl->AddRectFilled(V(mx-10,my),V(mx+rowW,my+h-3),WithA(Mix(Mix(COL_CARD,COL_CARD2,0.35f),COL_CARD2,0.6f*ha),(int)(255*e)),12);
            TextAt(dl,g_fMed,16,V(mx+4,my+(sub?7.0f:11.0f)),WithA(COL_INK,al),label);
            if(sub) TextAt(dl,g_fSml,13,V(mx+4,my+28),WithA(COL_INK2,al),sub);
            float sw=48,sh=28, sx=mx+rowW-sw-12, sy=my+(h-3-sh)/2;
            float t=Cael::anim(idBase*16+idc,val? 1.0f : 0.0f,Cael::DUR_FAST_SPATIAL,Cael::FAST_SPATIAL);
            ImU32 on=Mix(COL_INK,COL_GOLD,0.25f), off=Mix(COL_CARD2,COL_INK2,0.25f);
            dl->AddRectFilled(V(sx,sy),V(sx+sw,sy+sh),A(Mix(off,on,std::clamp(t,0.0f,1.0f))),sh*0.5f);
            if(t<0.5f) dl->AddRect(V(sx,sy),V(sx+sw,sy+sh),WithA(COL_INK2,(int)(150*(1-t*2)*e)),sh*0.5f,0,1.5f);
            float kr=9.0f+3.0f*t; ImVec2 kc=V(sx+sh*0.5f+(sw-sh)*t,sy+sh*0.5f);
            dl->AddCircleFilled(kc,kr,A(Mix(COL_INK2,M3OnPrimary(),std::clamp(t,0.0f,1.0f))),20);
            MsIcon(dl,t>0.5f? "check" : "close",kc,kr*1.35f,A(t>0.5f? on : off));
        } else {
        if(ha>0.01f) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-4),WithA(COL_INK2,(int)(ha*30*e)),10);
        TextAt(dl,g_fMed,17,V(mx,my+(sub?7.0f:11.0f)),WithA(COL_INK,al),label);
        if(sub) TextAt(dl,g_fSml,13,V(mx,my+29),WithA(COL_INK2,al),sub);
        float sw=44,sh=24, sx=mx+rowW-sw-6, sy=my+(h-4-sh)/2;
        dl->AddRectFilled(V(sx,sy),V(sx+sw,sy+sh),val?A(COL_GOLD):A(COL_TRACK),sh*0.5f);
        float kx=val? sx+sw-sh*0.5f : sx+sh*0.5f;
        dl->AddCircleFilled(V(kx,sy+sh*0.5f),sh*0.5f-3,IM_COL32(252,254,253,al)); }
        bool hit2=click&&hov; if(hit2) val=!val;
        my+=h; return hit2; };
    auto slider=[&](const char* label,float& val,float lo,float hi,const char* fmt)->bool{
        float h=52; bool hov=rowHit(my,h);
        hl(label,h);
        if(g_setStyle==1) dl->AddRectFilled(V(mx-10,my),V(mx+rowW,my+h-3),WithA(Mix(COL_CARD,COL_CARD2,0.35f),(int)(255*e)),12);
        TextAt(dl,g_fMed,17,V(mx,my+6),WithA(COL_INK,al),label);
        char vb[32]; snprintf(vb,32,fmt,val);
        TextAt(dl,g_fSml,14,V(mx+rowW-TextW(g_fSml,14,vb)-6,my+8),WithA(COL_GOLD,al),vb);
        float tx0=mx, tx1=mx+rowW-6, ty=my+36;
        dl->AddRectFilled(V(tx0,ty),V(tx1,ty+6),A(COL_TRACK),3);
        float t=std::clamp((val-lo)/(hi-lo),0.0f,1.0f);
        dl->AddRectFilled(V(tx0,ty),V(tx0+(tx1-tx0)*t,ty+6),A(COL_GOLD),3);
        dl->AddCircleFilled(V(tx0+(tx1-tx0)*t,ty+3),8,A(COL_GOLD));
        bool ch=false;
        if(down&&a>0.6f&&io.MousePos.y>ty-12&&io.MousePos.y<ty+18&&io.MousePos.x>tx0-10&&io.MousePos.x<tx1+10){
            val=lo+(hi-lo)*std::clamp((io.MousePos.x-tx0)/(tx1-tx0),0.0f,1.0f); ch=true; }
        my+=h; (void)hov; return ch; };
    // a labelled text field (click to focus, type to edit, backspace deletes). `mask` shows dots.
    auto textField=[&](const char* label,char* buf,size_t cap,int fid,bool mask)->void{
        float h=64; bool hov=rowHit(my,h);
        hl(label,h);
        TextAt(dl,g_fMed,17,V(mx,my+4),WithA(COL_INK,al),label);
        float fx=mx, fy=my+26, fw=rowW-6, fh=32;
        static int focusField=-1;
        if(click) focusField = hov? fid : (focusField==fid? -1 : focusField);
        bool foc=(focusField==fid);
        dl->AddRectFilled(V(fx,fy),V(fx+fw,fy+fh),A(COL_TRACK),8);
        dl->AddRect(V(fx,fy),V(fx+fw,fy+fh),foc?A(COL_GOLD):WithA(COL_INK2,(int)(al*0.5f)),8,0,foc?1.6f:1.0f);
        if(foc){ size_t len=strlen(buf);
            for(int i=0;i<io.InputQueueCharacters.Size;i++){ ImWchar c=io.InputQueueCharacters[i];
                if(c>=32&&c<127&&len<cap-1){ buf[len++]=(char)c; buf[len]=0; } }
            if(ImGui::IsKeyPressed(ImGuiKey_Backspace)&&len>0){ buf[--len]=0; }
            if(ImGui::IsKeyPressed(ImGuiKey_Enter)) focusField=-1;
        }
        size_t n=strlen(buf);
        if(n==0) TextAt(dl,g_fSml,15,V(fx+12,fy+8),WithA(COL_INK2,(int)(al*0.7f)),foc?"Type here\xE2\x80\xA6":"(not set)");
        else if(mask){ for(size_t i=0;i<n&&i<20;i++) dl->AddCircleFilled(V(fx+16+i*13,fy+fh*0.5f),4.0f,A(COL_INK)); }
        else TextAt(dl,g_fSml,15,V(fx+12,fy+8),A(COL_INK),buf);
        if(foc&&((GetTickCount64()/500)&1)==0){ float cx=fx+12+(mask?std::min(n,(size_t)20)*13.0f:TextW(g_fSml,15,buf)); dl->AddRectFilled(V(cx,fy+8),V(cx+2,fy+fh-8),A(COL_INK)); }
        my+=h;
    };
    auto section=[&](const char* label,bool& open){
        float h=40; bool hov=rowHit(my,h);
        hl(label,h);
        TextAt(dl,g_fMed,18,V(mx,my+9),WithA(COL_INK,al),label);
        ImVec2 cv=V(mx+rowW-18,my+20); float d=open?-1.0f:1.0f;
        dl->AddLine(V(cv.x-6,cv.y-3*d),V(cv.x,cv.y+3*d),WithA(COL_INK2,al),2.0f);
        dl->AddLine(V(cv.x,cv.y+3*d),V(cv.x+6,cv.y-3*d),WithA(COL_INK2,al),2.0f);
        dl->AddLine(V(mx,my+h-2),V(mx+rowW,my+h-2),WithA(COL_INK2,(int)(al*0.25f)),1.0f);
        if(click&&hov) open=!open;
        my+=h+6; };
    // ---- extra primitives the CachyOS-parity pages need ----
    auto header=[&](const char* t){ hl(t,26.0f); if(g_setStyle==1){ my+=6; TextAt(dl,g_fMed,15,V(mx,my),WithA(COL_INK2,al),t); my+=28; }
                                    else { TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),t); my+=24; } };
    auto note=[&](const char* t){ TextAt(dl,g_fSml,12,V(mx,my),WithA(COL_INK2,al),t); my+=20; };
    auto kv=[&](const char* k,const std::string& v){
        hl(k,26.0f);
        TextAt(dl,g_fSml,14,V(mx,my+4),WithA(COL_INK2,al),k);
        std::string vs=v; while(!vs.empty()&&TextW(g_fMed,15,vs.c_str())>rowW-160) vs.pop_back();
        TextAt(dl,g_fMed,15,V(mx+150,my+3),WithA(COL_INK,al),vs.c_str());
        my+=26; };
    // a row of push-buttons; returns the index clicked, or -1
    auto buttons=[&](std::initializer_list<const char*> labels,float bw)->int{
        int hit=-1, n=0; float bx4=mx;
        for(const char* l:labels){
            bool hov=io.MousePos.x>bx4&&io.MousePos.x<bx4+bw&&io.MousePos.y>my&&io.MousePos.y<my+30;
            float ha=HoverAnim(idBase+70+n,hov);
            dl->AddRectFilled(V(bx4,my),V(bx4+bw,my+30),WithA(COL_INK2,(int)((30+ha*46)*e)),9);
            TextAt(dl,g_fSml,13,V(bx4+bw/2-TextW(g_fSml,13,l)/2,my+7),WithA(COL_INK,al),l);
            if(click&&hov) hit=n;
            bx4+=bw+8; n++; }
        my+=40; return hit; };
    // a single-choice list of options; returns the newly picked index or -1
    auto choice=[&](const char** opts,int n,int cur)->int{
        int hit=-1;
        for(int i=0;i<n;i++){ float h=34; bool hov=rowHit(my,h); bool sel=(i==cur);
            if(sel) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-3),AccA((int)(38*e)),9);
            else if(hov) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-3),WithA(COL_INK2,(int)(26*e)),9);
            TextAt(dl,g_fSml,15,V(mx+4,my+8),WithA(sel?COL_GOLD:COL_INK,al),opts[i]);
            if(sel){ ImVec2 k=V(mx+rowW-18,my+16);
                dl->AddLine(V(k.x-6,k.y),V(k.x-1,k.y+5),A(COL_GOLD),2.2f);
                dl->AddLine(V(k.x-1,k.y+5),V(k.x+7,k.y-5),A(COL_GOLD),2.2f); }
            if(click&&hov) hit=i;
            my+=h; }
        return hit; };
        // A "< value >" cycler: one row, click the arrows (or the value) to step through the options.
        auto cycler=[&](const char* label,const char* sub,int& v,const char** names,int n)->bool{
            float h=sub?52.0f:42.0f; bool hov=rowHit(my,h); float ha=HoverAnim(idBase+(idc++),hov);
            hl(label,h);
            if(g_setStyle==1) dl->AddRectFilled(V(mx-10,my),V(mx+rowW,my+h-3),WithA(Mix(Mix(COL_CARD,COL_CARD2,0.35f),COL_CARD2,0.6f*ha),(int)(255*e)),12);
            else if(ha>0.01f) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-4),WithA(COL_INK2,(int)(ha*30*e)),10);
            TextAt(dl,g_fMed,17,V(mx,my+(sub?7.0f:11.0f)),WithA(COL_INK,al),label);
            if(sub) TextAt(dl,g_fSml,13,V(mx,my+29),WithA(COL_INK2,al),sub);
            const char* val=names[std::clamp(v,0,n-1)];
            float vw=TextW(g_fSml,15,val), bx1=mx+rowW-6, bx0=bx1-vw-64, cy=my+(h-4)*0.5f;
            dl->AddRectFilled(V(bx0,cy-15),V(bx1,cy+15),A(COL_TRACK),15);
            TextAt(dl,g_fSml,15,V(bx0+32,cy-9),WithA(COL_GOLD,al),val);
            bool lh=io.MousePos.x>bx0&&io.MousePos.x<bx0+28&&io.MousePos.y>cy-15&&io.MousePos.y<cy+15;
            bool rh=io.MousePos.x>bx1-28&&io.MousePos.x<bx1&&io.MousePos.y>cy-15&&io.MousePos.y<cy+15;
            bool mid=io.MousePos.x>=bx0+28&&io.MousePos.x<=bx1-28&&io.MousePos.y>cy-15&&io.MousePos.y<cy+15;
            ImU32 lc=WithA(lh?COL_GOLD:COL_INK2,al), rc=WithA(rh?COL_GOLD:COL_INK2,al);
            dl->AddLine(V(bx0+17,cy-5),V(bx0+12,cy),lc,2.0f); dl->AddLine(V(bx0+12,cy),V(bx0+17,cy+5),lc,2.0f);
            dl->AddLine(V(bx1-17,cy-5),V(bx1-12,cy),rc,2.0f); dl->AddLine(V(bx1-12,cy),V(bx1-17,cy+5),rc,2.0f);
            bool ch=false;
            if(click&&lh){ v=(v-1+n)%n; ch=true; }
            if(click&&(rh||mid)){ v=(v+1)%n; ch=true; }
            my+=h; return ch; };
    // ---- layout engine (layer 2): one control block that edits any Panel descriptor, so a panel's
    //      edge / thickness / length / position are user data instead of hardcoded pixels ----
    auto panelLayout=[&](int idx,const int* allow,int nAllow,
                         const char* sizeLabel,float sizeLo,float sizeHi,bool spanCtl)->bool{
        Panel& p=g_pn[idx]; bool ch=false;
        TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),"Screen edge"); my+=22;
        { float bw=(rowW-8*(nAllow-1))/(float)nAllow, bx4=mx;
          for(int k=0;k<nAllow;k++){
              bool sel=(p.edge==allow[k]);
              bool hov=io.MousePos.x>bx4&&io.MousePos.x<bx4+bw&&io.MousePos.y>my&&io.MousePos.y<my+32;
              float ha=HoverAnim(idBase+80+idx*8+k,hov&&!sel);
              dl->AddRectFilled(V(bx4,my),V(bx4+bw,my+32),
                                sel?AccA((int)(60*e)):WithA(COL_INK2,(int)((26+ha*40)*e)),9);
              if(sel) dl->AddRect(V(bx4,my),V(bx4+bw,my+32),A(COL_GOLD),9,0,1.6f);
              const char* cap=EDGE_CAP[allow[k]];
              TextAt(dl,g_fSml,14,V(bx4+bw/2-TextW(g_fSml,14,cap)/2,my+8),WithA(sel?COL_GOLD:COL_INK,al),cap);
              if(click&&hov&&!sel){ p.edge=allow[k]; ch=true; }
              bx4+=bw+8; }
          my+=42; }
        float sv=p.size; if(slider(sizeLabel,sv,sizeLo,sizeHi,"%.0f px")){ p.size=sv; ch=true; }
        if(spanCtl){ float lp = p.span<=1.0f? p.span*100.0f : 100.0f;
            if(slider("Length",lp,20,100,"%.0f%% of the edge")){
                p.span=lp/100.0f; p.spanMax=0; ch=true; } }   // manual length drops the built-in cap
        float an=p.anchor*100.0f;
        if(slider("Position along the edge",an,0,100,"%.0f%%")){ p.anchor=an/100.0f; ch=true; }
        float gp2=p.gap;
        if(slider("Edge margin",gp2,0,60,"%.0f px")){ p.gap=gp2; ch=true; }
        bool vis=p.visible;
        if(toggle("Show this panel","Turn it off entirely",vis)){ p.visible=vis; ch=true; SaveConfig(); }
        // The BAR is what carves the bubble's margin out of the work area, so any change to its
        // size, edge or margin has to re-inset that area and re-fit the open windows. This was
        // gated on g_hideTaskbar, which is off whenever Aether IS the shell (there is no Explorer
        // taskbar to hide) - so moving the bar updated nothing and apps kept their old bounds.
        if(ch){ g_deskDirty=true; if(idx==PN_BAR) g_bubbleGeomDirty=true; }
        return ch; };
    // a labelled fill bar (storage, battery)
    auto meter=[&](const std::string& label,const std::string& right,float frac,ImU32 fill){
        TextAt(dl,g_fMed,15,V(mx,my),WithA(COL_INK,al),label.c_str());
        TextAt(dl,g_fSml,13,V(mx+rowW-TextW(g_fSml,13,right.c_str()),my+2),WithA(COL_INK2,al),right.c_str());
        dl->AddRectFilled(V(mx,my+22),V(mx+rowW,my+30),A(COL_TRACK),4);
        dl->AddRectFilled(V(mx,my+22),V(mx+rowW*std::clamp(frac,0.0f,1.0f),my+30),A(fill),4);
        my+=44; };

    dl->PushClipRect(V(mx-12,py+56),V(mx+rowW+12,py+ph-16),true);
    switch(g_setPage){
    case SP_NETWORK: {   // Network
        TextAt(dl,g_fSml,14,V(mx,my),WithA(COL_INK2,al), g_st.online?"Connected":"Offline"); my+=28;
        if(!g_wifiScanning && GetTickCount64()-g_wifiLastScan>15000) WifiScanAsync();
        if(!g_wifiPwFor.empty()){                      // a secured network asked for its password
            header("Connect to a network");
            std::string t="Type the password for \xE2\x80\x9C"+g_wifiPwFor+"\xE2\x80\x9D (click the box first), then Connect.";
            note(t.c_str());
            static char pw[128]={0}; static bool showPw=false;
            textField("Password",pw,sizeof(pw),9931,!showPw);
            { bool v=showPw; if(toggle("Show the password",nullptr,v)) showPw=v; }
            int b=buttons({"Connect","Cancel"},150);
            if(b==0 || (pw[0] && ImGui::IsKeyPressed(ImGuiKey_Enter))){
                for(auto& w:g_wifi) if(w.ssid==g_wifiPwFor){ WifiConnectAsync(w,pw); break; }
                memset(pw,0,sizeof(pw)); g_wifiPwFor.clear(); }
            else if(b==1){ memset(pw,0,sizeof(pw)); g_wifiPwFor.clear(); }
            my+=8;
        }
        header("Wi-Fi networks");
        { int b=buttons({g_wifiScanning? "Scanning\xE2\x80\xA6" : "Scan for networks","Turn Wi-Fi on / off","Windows Wi-Fi settings"},190);
          if(b==0) WifiScanAsync(); else if(b==1) RadioToggleAsync(0); else if(b==2) AetherShellExec(nullptr,L"open",L"ms-settings:network-wifi",nullptr,nullptr,SW_SHOWNORMAL); }
        my+=DrawWifiList(dl,io,mx-8,my,mx+rowW,1.0f,12,54,click,al,idBase+40)+8;
        note("Click a network to connect. Saved networks show a bin on hover to forget them.");
    } break;
    case SP_BLUETOOTH: {   // Bluetooth
        if(g_bt.empty()) RefreshBt();
        int n=std::min((int)g_bt.size(),10);
        if(n==0) TextAt(dl,g_fSml,15,V(mx,my),WithA(COL_INK2,al),"No paired devices");
        for(int i=0;i<n;i++){ auto& d=g_bt[i]; float h=46; bool hov=rowHit(my,h);
            float ha=HoverAnim(idBase+40+i,hov);
            if(d.connected) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-4),AccA((int)(46*e)),10);
            else if(ha>0.01f) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-4),WithA(COL_INK2,(int)(ha*34*e)),10);
            BtIcon(dl,V(mx+12,my+20),WithA(d.connected?COL_GOLD:COL_INK2,al));
            TextAt(dl,g_fMed,16,V(mx+34,my+12),WithA(d.connected?COL_GOLD:COL_INK,al),d.name.c_str());
            if(d.connected) TextAt(dl,g_fSml,12,V(mx+rowW-72,my+15),WithA(COL_INK2,al),"Connected");
            my+=h; }
        if(rowHit(my,34)&&click) RefreshBt();
        TextAt(dl,g_fSml,14,V(mx,my+8),WithA(COL_GOLD,al),"Refresh devices");
    } break;
    case SP_AUDIO: {   // Audio
        if(g_mixerInSettings){ DrawAudioMixer(dl,io,V(mx-10,my),V(rowW+10,std::min(600.0f,viewH-30.0f)),700,true); my+=std::min(600.0f,viewH-30.0f)+16; }
        header("Where the mixer lives");
        { bool v=g_mixerInSettings; if(toggle("In Settings > Audio",nullptr,v)){ g_mixerInSettings=v; SaveConfig(); } }
        { bool v=g_mixerInSidebar; if(toggle("In the sidebar","A mixer button beside the notifications header (caelestia sidebar)",v)){ g_mixerInSidebar=v; SaveConfig(); } }
        if(buttons({"Add a Mixer tab to the dashboard"},280)==0) RunShellCommand("dashboard_mixer_tab");
        my+=8;
        header("Screen recording");
        { bool v=g_recHud; if(toggle("Recording checker","A card flies in while you record: time, fps, size, pause / stop, audio settings",v)){ g_recHud=v; SaveConfig(); } }
        { static const char* PN[8]={"Top left","Top right","Bottom left","Bottom right","Left","Right","Top","Bottom"};
          int v=g_recHudPos; if(cycler("Flies in from",nullptr,v,PN,8)){ g_recHudPos=v; SaveConfig(); } }
        { bool v=g_recHudPeekOn; if(toggle("Tuck into the edge","Hides as a small tab after a few seconds; touch the edge to bring it back",v)){ g_recHudPeekOn=v; SaveConfig(); } }
        { float f=(float)g_recHudStayMs/1000.0f; if(slider("Stays out for",f,1,30,"%.0f s")) g_recHudStayMs=(int)(f*1000); if(io.MouseReleased[0]) { static int last=0; if(last!=g_recHudStayMs){ last=g_recHudStayMs; SaveConfig(); } } }
        { static const char* FP[]={"30 fps","60 fps","120 fps"}; int v= g_recFps>=120? 2 : g_recFps>=60? 1 : 0;
          if(cycler("Recording frame rate",nullptr,v,FP,3)){ g_recFps= v==2? 120 : v==1? 60 : 30; SaveConfig(); } }
        { if(g_recDevState==0) RecListAudioDevices(); std::vector<std::string> devs; { std::lock_guard<std::mutex> lk(g_recDevMtx); devs=g_recAudioDevs; }
          static std::vector<std::string> names; names.clear(); names.push_back("None"); for(auto& d:devs) names.push_back(d);
          static std::vector<const char*> ptrs; ptrs.clear(); for(auto& n2:names) ptrs.push_back(n2.c_str());
          int v=0; for(size_t i=0;i<devs.size();i++) if(devs[i]==g_recAudioSys) v=(int)i+1;
          if(cycler("Record system audio from","e.g. Stereo Mix or a Voicemeeter output",v,ptrs.data(),(int)ptrs.size())){ g_recAudioSys= v==0? "" : devs[v-1]; SaveConfig(); }
          v=0; for(size_t i=0;i<devs.size();i++) if(devs[i]==g_recAudioMic) v=(int)i+1;
          if(cycler("Record microphone from",nullptr,v,ptrs.data(),(int)ptrs.size())){ g_recAudioMic= v==0? "" : devs[v-1]; SaveConfig(); } }
        my+=8;
        if(g_volCache<0) g_volCache=GetVolume();
        float v=g_volCache*100.0f;
        if(slider("Output volume",v,0,100,"%.0f%%")){ g_volCache=v/100.0f; SetVolume(g_volCache); }
        BrightnessProbe();
        if(g_brtOk){ float b=g_brtCache*100.0f; if(slider("Display brightness",b,0,100,"%.0f%%")) g_brtCache=b/100.0f;
            if(rel) SetBrightness((int)(g_brtCache*g_brtMax)); }
        bool mm=g_micMuted;
        if(g_micPresent && toggle("Mute microphone",nullptr,mm)){ g_micMuted=mm; SetMicMute(mm); }
        my+=8;
        TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),
               g_micPresent?"Volume changes also raise the on-screen display.":"No capture device detected.");
    } break;
    case SP_APPEARANCE: {   // Appearance
        { static const char* SS3[]={"Classic","Caelestia"};
          int v=g_setStyle; if(cycler("Settings window style","Caelestia: searchable category cards and grouped rows",v,SS3,2)){ g_setStyle=v; SaveConfig(); } }
        { static const char* PT[]={"Stripes + Loading!!!!!!","Fade","None"};
          int v=g_setTransition; if(cycler("Settings page transition",nullptr,v,PT,3)){ g_setTransition=v; SaveConfig(); } }
        if(g_setTransition==0){
            { float d=(float)g_setTransMs; if(slider("Transition length",d,300,3000,"%.0f ms")) g_setTransMs=(int)d; if(rel) SaveConfig(); }
            header("Stripes look  (also used by the live wallpaper switch)");
            { static char tb[64]; static std::string was="\x01"; if(was!=g_stripeText){ snprintf(tb,sizeof(tb),"%s",g_stripeText.c_str()); was=g_stripeText; }
              textField("Text",tb,sizeof(tb),9721,false); if(g_stripeText!=tb){ g_stripeText=tb; was=g_stripeText; SaveConfig(); } }
            { static char cb[160]; static std::string was="\x01"; if(was!=g_stripeColors){ snprintf(cb,sizeof(cb),"%s",g_stripeColors.c_str()); was=g_stripeColors; }
              textField("Colours  (primary, secondary, tertiary, #ff66aa ...)",cb,sizeof(cb),9722,false); if(g_stripeColors!=cb){ g_stripeColors=cb; was=g_stripeColors; SaveConfig(); } }
            { // quick colour sets
              int q=buttons({"Theme","Sunset","Ocean","Candy","Mono"},104);
              static const char* SETS[]={"primary, secondary, tertiary","#ff5f6d, #ffc371, #ff9a8b, #c471ed","#2193b0, #6dd5ed, #0f4c75, #3282b8",
                                         "#ff9ff3, #feca57, #48dbfb, #1dd1a1, #5f27cd","#ffffff, #b0b0b0, #606060, #202020"};
              if(q>=0){ g_stripeColors=SETS[q]; SaveConfig(); } }
            { bool v=g_stripeLiveWallColors; if(toggle("Live wallpapers use their own colours","The stripes take the new wallpaper's palette",v)){ g_stripeLiveWallColors=v; SaveConfig(); } }
            { static char kb[40]; static std::string was="\x01"; if(was!=g_stripeTextColor){ snprintf(kb,sizeof(kb),"%s",g_stripeTextColor.c_str()); was=g_stripeTextColor; }
              textField("Text colour",kb,sizeof(kb),9723,false); if(g_stripeTextColor!=kb){ g_stripeTextColor=kb; was=g_stripeTextColor; SaveConfig(); } }
            { float f=g_stripeTextSize; if(slider("Text size",f,16,160,"%.0f px")) g_stripeTextSize=f; if(rel) SaveConfig(); }
            { float f=(float)g_stripeCount; if(slider("Stripes",f,3,30,"%.0f")) g_stripeCount=(int)f; if(rel) SaveConfig(); }
            { float f=g_stripeSlant; if(slider("Angle",f,0,1.5f,"%.2f")) g_stripeSlant=f; if(rel) SaveConfig(); }
            { float f=g_stripeColorSpeed; if(slider("Colour flow speed",f,0,4,"%.2f")) g_stripeColorSpeed=f; if(rel) SaveConfig(); }
            { int v=M3ShapeFromName(g_stripeShape)+1; if(v<0) v=0;
              static std::vector<const char*> SN; if(SN.empty()){ SN.push_back("None"); for(int k=0;k<M3_COUNT;k++) SN.push_back(M3_SHAPE_LABEL[k]); }
              if(cycler("Spinning shape",nullptr,v,SN.data(),(int)SN.size())){ g_stripeShape = v==0? "none" : M3_SHAPE_ID[v-1]; SaveConfig(); } }
            { bool v=g_stripeBounce; if(toggle("Bouncing letters",nullptr,v)){ g_stripeBounce=v; SaveConfig(); } }
            { bool v=g_stripeEdge; if(toggle("Light stripe edges",nullptr,v)){ g_stripeEdge=v; SaveConfig(); } }
            { static const char* DN[]={"Sweep right","Sweep left"}; int v=g_stripeDir; if(cycler("Direction",nullptr,v,DN,2)){ g_stripeDir=v; SaveConfig(); } }
            if(buttons({"Preview"},140)==0) g_setWipeAt=GetTickCount64();
        }
        { bool v=g_setAboutCard; if(toggle("About card in the sidebar","Aether, the version and who made it, over a nebula",v)){ g_setAboutCard=v; SaveConfig(); } }
        section("Theme mode",g_setSec[0]);
        if(g_setSec[0]){
            const char* modes[4]={"Follow the scheme","Light","Dark","Follow Windows"};
            for(int i=0;i<4;i++){ float h=34; bool hov=rowHit(my,h);
                bool sel=(g_themeMode==i);
                if(sel) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-3),AccA((int)(38*e)),9);
                else if(hov) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-3),WithA(COL_INK2,(int)(26*e)),9);
                TextAt(dl,g_fSml,15,V(mx+4,my+8),WithA(sel?COL_GOLD:COL_INK,al),modes[i]);
                if(click&&hov){ g_themeMode=i; ApplyThemeMode(); SaveConfig(); g_deskDirty=true; }
                my+=h; }
            my+=6; }
        section("Color variant",g_setSec[1]);
        if(g_setSec[1]){
            bool dc=g_dynamicColor;
            if(toggle("Wallpaper colours","Derive the accent from the desktop wallpaper",dc)){
                g_dynamicColor=dc; g_customAccent=false;
                if(dc) ApplyDynamicAccent(); else ApplyThemeMode();
                SaveConfig(); g_deskDirty=true; }
            float uw=g_uiScale;
            if(slider("Interface scale",uw,0.75f,2.0f,"%.2fx")) g_uiScale=uw;
            if(rel&&fabsf(uw-g_uiScale)<0.001f) SaveConfig();
            float ts=g_textScale;   // Caelestia density: <1 shrinks text toward the Material-3 13px scale
            if(slider("Text density",ts,0.80f,1.15f,"%.2fx")) g_textScale=ts;
            if(rel&&fabsf(ts-g_textScale)<0.001f) SaveConfig();
            float rr=g_rounding;
            if(slider("Corner rounding",rr,0.4f,1.8f,"%.2f")){ g_rounding=rr; g_cardRound=16*rr; g_panelRound=22*rr; }
            float op=g_drawerAlpha/255.0f;
            if(slider("Panel opacity",op,0.5f,1.0f,"%.0f%%")) g_drawerAlpha=(int)(op*255);
            my+=6; }
        section("Color scheme",g_setSec[2]);
        if(g_setSec[2]){
            // ---- one-click "Linux mode": CachyOS scheme + bottom horizontal taskbar + Adwaita rounding ----
            { float bh=46; ImVec2 b0=V(mx,my),b1=V(mx+rowW,my+bh);
              bool hov=rowHit(my,bh); float ha=HoverAnim(idBase+300,hov);
              dl->AddRectFilled(b0,b1,WithA(Mix(COL_CARD2,COL_GOLD,0.10f+0.10f*ha),(int)(230*e)),12);
              dl->AddRect(b0,b1,WithA(COL_GOLD,(int)(al*0.6f)),12,0,1.4f);
              AdwSym(dl,V(mx+22,my+bh/2),10,A(COL_GOLD),"start-here");   // fall through if missing
              TextAt(dl,g_fMed,16,V(mx+42,my+8),WithA(COL_INK,al),"Turn on Linux mode");
              TextAt(dl,g_fSml,13,V(mx+42,my+27),WithA(COL_INK2,al),"CachyOS colours + bottom taskbar + Adwaita rounding");
              if(click&&hov){
                  g_customAccent=false; g_dynamicColor=false; g_themeMode=0;
                  g_scheme=SchemeIndex("cachyos"); ApplyScheme(g_scheme);
                  g_pn[PN_BAR].edge=EDGE_BOTTOM; g_pn[PN_BAR].visible=true;   // CachyOS-style bottom bar
                  g_rounding=1.0f; g_cardRound=16; g_panelRound=22;
                  g_textScale=0.88f;                                          // denser Material-3 type
                  ApplyWorkAreas(); SaveConfig(); g_deskDirty=true;
              }
              my+=bh+12; }
            TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),"Available color schemes"); my+=24;
            for(int i=0;i<NSCHEMES;i++){
                const Scheme& s=SCHEMES[i]; float h=44; bool hov=rowHit(my,h); bool sel=(i==g_scheme);
                float ha=HoverAnim(idBase+60+i,hov&&!sel);
                if(sel) dl->AddRect(V(mx-8,my),V(mx+rowW,my+h-4),A(COL_GOLD),12,0,1.6f);       // outlined pill
                else if(ha>0.01f) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-4),WithA(COL_INK2,(int)(ha*30*e)),12);
                // two-tone circular swatch
                ImVec2 sc=V(mx+12,my+20);
                dl->PathArcTo(sc,11,1.5708f,4.7124f,20); dl->PathFillConvex(WithA(s.panel,al));
                dl->PathArcTo(sc,11,-1.5708f,1.5708f,20); dl->PathFillConvex(WithA(s.accent,al));
                dl->AddCircle(sc,11,WithA(COL_INK2,(int)(al*0.5f)),0,1.0f);
                TextAt(dl,g_fMed,16,V(mx+34,my+11),WithA(sel?COL_GOLD:COL_INK,al),s.name);
                TextAt(dl,g_fSml,13,V(mx+34+TextW(g_fMed,16,s.name)+10,my+13),WithA(COL_INK2,al),s.family);
                if(sel){ ImVec2 k=V(mx+rowW-22,my+20);   // right-side checkmark
                    dl->AddLine(V(k.x-6,k.y),V(k.x-1,k.y+5),A(COL_GOLD),2.4f);
                    dl->AddLine(V(k.x-1,k.y+5),V(k.x+7,k.y-5),A(COL_GOLD),2.4f); }
                if(click&&hov){ g_customAccent=false; g_dynamicColor=false; g_scheme=i; g_themeMode=0;
                    ApplyScheme(i); SaveConfig(); g_deskDirty=true; }
                my+=h; }
        }
    } break;
    case SP_WALLPAPER: {   // Wallpaper — folders, transition, carousel (ported from WindowPaper's settings)
        { static const char* LT[]={"Stripes + Loading!!!!!!","Fade"};
          int v=g_liveTransStyle; if(cycler("Live wallpaper switch","Stripes take the new wallpaper's colours",v,LT,2)){ g_liveTransStyle=v; SaveConfig(); } }
        header("On the wallpaper");
        { bool v=g_deskLyrics; if(toggle("Desktop lyrics","The playing song's current line on the wallpaper",v)){ g_deskLyrics=v; SaveConfig(); g_deskDirty=true; } }
        if(g_deskLyrics){
            { bool v=g_deskLyricsAutoHide; if(toggle("Auto-hide lyrics","Hide lyrics when a window is open",v)){ g_deskLyricsAutoHide=v; SaveConfig(); } }
            { float f=g_deskLyricsSize; if(slider("Lyrics size",f,12,96,"%.0f px")){ g_deskLyricsSize=f; g_deskDirty=true; } if(rel) SaveConfig(); }
            { float f=g_deskLyricsY*100; if(slider("Lyrics position",f,5,95,"%.0f%% down")){ g_deskLyricsY=f/100; g_deskDirty=true; } if(rel) SaveConfig(); }
        }
        { bool v=g_deskViz; if(toggle("Background visualiser","Audio bars along the bottom of the wallpaper",v)){ g_deskViz=v; SaveConfig(); g_deskDirty=true; } }
        if(g_deskViz){
            { bool v=g_deskVizAutoHide; if(toggle("Auto-hide visualiser","Hide the visualiser when a window is open",v)){ g_deskVizAutoHide=v; SaveConfig(); } }
            { bool v=g_deskVizMirror; if(toggle("Mirrored","Bass at both edges",v)){ g_deskVizMirror=v; SaveConfig(); } }
            { float f=g_deskVizHeight*100; if(slider("Visualiser height",f,2,90,"%.0f%%")){ g_deskVizHeight=f/100; } if(rel) SaveConfig(); }
            { float f=g_deskVizBar; if(slider("Bar width",f,1,20,"%.0f px")){ g_deskVizBar=f; } if(rel) SaveConfig(); }
            { float f=g_deskVizGap; if(slider("Bar gap",f,0,20,"%.0f px")){ g_deskVizGap=f; } if(rel) SaveConfig(); }
        }
        my+=10;
        { bool v=g_wallPreviewImages;
          if(toggle("Preview while scrolling","The picker applies each wallpaper as you land on it and the shell recolours to match. Esc puts yours back",v)){ g_wallPreviewImages=v; SaveConfig(); } }
        if(g_wallPreviewImages){ float d=(float)g_wallPreviewDelay; if(slider("Preview delay",d,0,1500,"%.0f ms")) g_wallPreviewDelay=(int)d; if(rel) SaveConfig(); }
        my+=6;
        // ---- WALLPAPER FOLDERS ----
        TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),"Wallpaper folders"); my+=24;
        { float listH=112;
          dl->AddRectFilled(V(mx,my),V(mx+rowW,my+listH),WithA(COL_CARD2,(int)(200*e)),10);
          if(g_wallFolders.empty())
              TextAt(dl,g_fSml,14,V(mx+12,my+10),WithA(COL_INK2,al),"No folders added \xE2\x80\x94 the picker falls back to Pictures");
          float fy=my+6;
          for(int i=0;i<(int)g_wallFolders.size() && i<4;i++){
              bool hov=io.MousePos.x>mx+4&&io.MousePos.x<mx+rowW-4&&io.MousePos.y>fy&&io.MousePos.y<fy+25;
              bool sel=(i==g_wallFolderSel);
              if(sel) dl->AddRectFilled(V(mx+4,fy),V(mx+rowW-4,fy+25),AccA((int)(60*e)),7);
              else if(hov) dl->AddRectFilled(V(mx+4,fy),V(mx+rowW-4,fy+25),WithA(COL_INK2,(int)(30*e)),7);
              std::string fn=g_wallFolders[i];
              int cnt=0; for(auto& w:g_walls){ if(w.path.size()>=U82W(fn).size() &&
                    _wcsnicmp(w.path.c_str(),U82W(fn).c_str(),U82W(fn).size())==0) cnt++; }
              char row[400]; snprintf(row,400,"%s   (%d images)",fn.c_str(),cnt);
              std::string rs=row; while(!rs.empty()&&TextW(g_fSml,13,rs.c_str())>rowW-28) rs.pop_back();
              TextAt(dl,g_fSml,13,V(mx+12,fy+5),WithA(sel?COL_GOLD:COL_INK,al),rs.c_str());
              if(click&&hov) g_wallFolderSel=i;
              fy+=26;
          }
          if((int)g_wallFolders.size()>4){ char more[40]; snprintf(more,40,"+%d more\xE2\x80\xA6",(int)g_wallFolders.size()-4);
              TextAt(dl,g_fSml,12,V(mx+12,fy+4),WithA(COL_INK2,al),more); }
          my+=listH+10;
          // buttons
          struct B{const char* l; float w;} bs[3]={{"+ Add folder",132},{"Remove selected",148},{"Rescan",96}};
          float bx2=mx;
          for(int i=0;i<3;i++){
              bool hov=io.MousePos.x>bx2&&io.MousePos.x<bx2+bs[i].w&&io.MousePos.y>my&&io.MousePos.y<my+30;
              float ha=HoverAnim(idBase+90+i,hov);
              dl->AddRectFilled(V(bx2,my),V(bx2+bs[i].w,my+30),
                  i==0?AccA((int)((200+ha*40)*e)):WithA(COL_INK2,(int)((30+ha*44)*e)),9);
              ImU32 fc = i==0?(g_darkUI?IM_COL32(12,20,14,255):IM_COL32(250,254,252,255)):COL_INK;
              TextAt(dl,g_fSml,13,V(bx2+bs[i].w/2-TextW(g_fSml,13,bs[i].l)/2,my+7),WithA(fc,al),bs[i].l);
              if(click&&hov){
                  if(i==0){   // native folder browser - on its own thread, so the shell keeps drawing
                      PickFolderAsync(L"Select a wallpaper folder",[](const std::string& f2){
                          if(std::find(g_wallFolders.begin(),g_wallFolders.end(),f2)==g_wallFolders.end()){
                              g_wallFolders.push_back(f2); SaveConfig(); WallRescan(); } }); }
                  else if(i==1){ if(g_wallFolderSel>=0&&g_wallFolderSel<(int)g_wallFolders.size()){
                          g_wallFolders.erase(g_wallFolders.begin()+g_wallFolderSel);
                          g_wallFolderSel=-1; SaveConfig(); WallRescan(); } }
                  else WallRescan();
              }
              bx2+=bs[i].w+8;
          }
          my+=40;
          bool rc=g_wallRecursive;
          if(toggle("Scan folders recursively","Include sub-folders (waypaper-style)",rc)){ g_wallRecursive=rc; SaveConfig(); WallRescan(); }
          bool bd=g_wallBundled;
          if(toggle("Include the bundled wallpapers","The three CachyOS images that ship with Aether. Off, they only appear when you have added no folders at all.",bd)){
              g_wallBundled=bd; SaveConfig(); WallRescan(); }
        }
        // ---- TRANSITION EFFECT ----
        TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),"Transition effect"); my+=24;
        { const int COLS=4, NT=(int)WTrans::COUNT;
          float cw2=(rowW-(COLS-1)*8)/COLS, chh=28;
          for(int i=0;i<NT;i++){
              int r=i/COLS,c2=i%COLS; float cxp=mx+c2*(cw2+8), cyp=my+r*(chh+7);
              bool hov=io.MousePos.x>cxp&&io.MousePos.x<cxp+cw2&&io.MousePos.y>cyp&&io.MousePos.y<cyp+chh;
              bool sel=((int)g_transCfg==i);
              float ha=HoverAnim(idBase+80+i,hov&&!sel);
              dl->AddRectFilled(V(cxp,cyp),V(cxp+cw2,cyp+chh),
                  sel?AccA((int)(210*e)):WithA(COL_INK2,(int)((26+ha*40)*e)),9);
              ImU32 fc = sel?(g_darkUI?IM_COL32(12,20,14,255):IM_COL32(250,254,252,255)):COL_INK;
              const char* nm=WTRANS_NAME[i];
              TextAt(dl,g_fSml,12,V(cxp+cw2/2-TextW(g_fSml,12,nm)/2,cyp+7),WithA(fc,al),nm);
              if(click&&hov){ g_transCfg=(WTrans)i; SaveConfig();
                  if(g_deskWall){
                      bool rp=false; g_transNow=ResolveTrans(g_transCfg,rp);
                      g_wipeAt=DeskCursor();
                      g_wipe=(g_transNow==WTrans::None)?1.0f:0.0f; g_deskDirty=true;
                      // preview: same stacking path, so tapping several in a row layers them up
                      PushWipe(g_deskWall,g_deskWallW,g_deskWallH,g_transNow,g_wipeAt,g_transMs); } }
          }
          my+=((NT+COLS-1)/COLS)*(chh+7)+8;
          float tms=(float)g_transMs;
          if(slider("Transition duration",tms,120,3000,"%.0f ms")) g_transMs=(int)tms;
          bool pm=g_transPosMouse;
          if(toggle("Grow from the cursor","Otherwise radial transitions start at the centre",pm)){ g_transPosMouse=pm; SaveConfig(); }
        }
        // ---- KEYBIND TRANSITIONS ----
        TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),"Ctrl+Alt+[ and Ctrl+Alt+] step through wallpapers"); my+=24;
        { const char* labels[2]={"Previous","Next"};
          std::string* vals[2]={&g_transPrevName,&g_transNextName};
          for(int k=0;k<2;k++){
              TextAt(dl,g_fSml,14,V(mx,my+7),WithA(COL_INK,al),labels[k]);
              float bx3=mx+86;
              bool hov=io.MousePos.x>bx3&&io.MousePos.x<bx3+150&&io.MousePos.y>my&&io.MousePos.y<my+28;
              float ha=HoverAnim(idBase+120+k,hov);
              dl->AddRectFilled(V(bx3,my),V(bx3+150,my+28),WithA(COL_INK2,(int)((28+ha*40)*e)),8);
              TextAt(dl,g_fSml,13,V(bx3+10,my+6),WithA(COL_GOLD,al),vals[k]->c_str());
              if(click&&hov){   // cycle to the next transition name
                  int cur=(int)ParseTrans(*vals[k]); cur=(cur+1)%(int)WTrans::COUNT;
                  *vals[k]=WTRANS_NAME[cur]; SaveConfig(); }
              my+=34;
          }
        }
        // ---- PICKER PANEL / CARD SIZES / DISPLAY / MOTION / HOVER / SLIDESHOW / ENGINE ----
        TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),"Picker panel"); my+=22;
        { int wp=choice(LPOS_LABEL,LPOS_N,std::clamp(g_wallPos,0,LPOS_N-1));
          if(wp>=0 && wp!=g_wallPos){ g_wallPos=wp; SaveConfig(); } }
        { float v=g_wallScale*100.0f;
          if(slider("Size",v,60,220,"%.0f%%")) g_wallScale=v/100.0f; }
        { float v=g_wallRound; if(slider("Corner radius",v,0,48,"%.0f px")) g_wallRound=v; }
        { float v=g_wallOpacity*100.0f;
          if(slider("Background opacity",v,10,100,"%.0f%%")) g_wallOpacity=v/100.0f; }
        if(rel) SaveConfig();
        { bool b=g_wallBlur;
          if(toggle("Frost the background","Blurs what is behind the picker instead of showing it straight through",b)){
              g_wallBlur=b; SaveConfig(); } }
        { int wa=choice(LANIM_LABEL,LANIM_N,std::clamp(g_wallAnim,0,LANIM_N-1));
          if(wa>=0 && wa!=g_wallAnim){ g_wallAnim=wa; SaveConfig(); } }
        { bool b=g_wallBackdrop;
          if(toggle("Blur the screen behind it","The whole desktop eases out of focus as the picker opens",b)){
              g_wallBackdrop=b; SaveConfig(); } }
        { bool b=g_wallImmersive;
          if(toggle("Immersive (hide the panel)","No slab, no title, no hint line \xE2\x80\x94 just the wallpapers on the blurred desktop",b)){
              g_wallImmersive=b; SaveConfig(); } }
        note("Size scales the panel and the cover-flow cards together, so turning it up fills a big screen instead of stranding a small carousel in a large slab.");
        my+=10;
        TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),"Picker cards"); my+=22;
        { float v;
          v=(float)g_carCW; if(slider("Centre card width", v,180,560,"%.0f px")) g_carCW=(int)v;
          v=(float)g_carCH; if(slider("Centre card height",v,110,380,"%.0f px")) g_carCH=(int)v;
          v=(float)g_carNW; if(slider("Side card width",   v,90, 420,"%.0f px")) g_carNW=(int)v;
          v=(float)g_carNH; if(slider("Side card height",  v,60, 320,"%.0f px")) g_carNH=(int)v;
          v=(float)g_carGap;if(slider("Card gap",          v,0,  80, "%.0f px")) g_carGap=(int)v;
          if(rel) SaveConfig(); }
        { bool b;
          b=g_wallShowName; if(toggle("Show filename",nullptr,b)){ g_wallShowName=b; SaveConfig(); }
          b=g_wallShowExt;  if(toggle("Show file-type badge",nullptr,b)){ g_wallShowExt=b; SaveConfig(); }
          b=g_carShadow;    if(toggle("Depth shadow and vignette",nullptr,b)){ g_carShadow=b; SaveConfig(); }
          b=g_carHoverZoom; if(toggle("Hover zoom on the centre card",nullptr,b)){ g_carHoverZoom=b; SaveConfig(); }
          if(g_carHoverZoom){ float v=(float)g_carHoverAmt;
              if(slider("Zoom expansion",v,2,30,"%.0f px")) g_carHoverAmt=(int)v; if(rel) SaveConfig(); } }
        TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),"Slideshow"); my+=22;
        { float v=(float)g_slideSec;
          if(slider("Auto-advance (0 = off)",v,0,1800,"%.0f s")) g_slideSec=(int)v;
          bool sh=g_slideShuffle; if(toggle("Shuffle","Random order instead of sequential",sh)){ g_slideShuffle=sh; SaveConfig(); }
          if(rel) SaveConfig(); }
        TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),"Engine"); my+=22;
        { bool b;
          b=g_paletteExport;
          if(toggle("Export colour palette on apply","Writes ~/.caelestia_colors.json (wallust-style)",b)){ g_paletteExport=b; SaveConfig(); }
          b=g_restoreOnLaunch;
          if(toggle("Restore wallpaper on launch","Re-applies the last wallpaper at startup",b)){ g_restoreOnLaunch=b; SaveConfig(); }
          TextAt(dl,g_fSml,12,V(mx,my),WithA(COL_INK2,al),"Post-apply hook (edit \"postApply\" in config.json; {path} = wallpaper):");
          my+=20;
          std::string hk=g_postApplyCmd.empty()? "e.g.  wal -i \"{path}\" --backend colorthief" : g_postApplyCmd;
          while(!hk.empty()&&TextW(g_fSml,12,hk.c_str())>rowW) hk.pop_back();
          TextAt(dl,g_fSml,12,V(mx,my),WithA(g_postApplyCmd.empty()?COL_INK2:COL_GOLD,al),hk.c_str());
          my+=26; }
    } break;
    case SP_DISPLAY: {   // Display — auto-dim when inactive
        bool ad=g_autoDim;
        if(toggle("Dim the screen when inactive","Fades down after a spell with no input",ad)){ g_autoDim=ad; SaveConfig(); }
        float da=(float)g_dimAfter;
        if(slider("Dim after",da,15,900,"%.0f s")) g_dimAfter=(int)da;
        float dlv=g_dimLevel*100.0f;
        if(slider("Dim level",dlv,20,95,"%.0f%%")) g_dimLevel=dlv/100.0f;
        bool uls=g_useLockScreen;
        if(toggle("Caelestia lock screen","Custom lock overlay. Ctrl+Alt+Del always escapes.",uls)){ g_useLockScreen=uls; SaveConfig(); }
        if(g_useLockScreen){
            note("Panel is Caelestia's three-column container. Floating puts the same widgets as M3 shapes straight on the blurred wallpaper.");
            { int lp=choice(LOCKSTYLE_LABEL,3,std::clamp(g_lockStyle,0,2));
              if(lp>=0 && lp!=g_lockStyle){ g_lockStyle=lp; SaveConfig(); } }
            note("Unlock code works for PIN / Microsoft accounts that a password check can't verify.");
            textField("Unlock code",g_lockCode,sizeof(g_lockCode),42001,true);
            if(rel) SaveConfig();
            bool hw=g_helloOn;
            if(toggle("Windows Hello","Face / fingerprint / PIN as an alternative to typing the password",hw)){
                g_helloOn=hw; if(hw) HelloProbe(); SaveConfig(); }
            if(g_helloOn){ HelloProbe(); int av=g_helloAvail.load();
                note(g_helloBroken.load()? g_helloBrokenWhy.c_str()
                   : av==1? "Hello is ready on this PC."
                   : av==0? "Checkingâ¦" : HelloWhyNot(-av-2)); }
        }
        bool il=g_idleLock;
        if(toggle("Lock when inactive","Locks after a longer idle spell (custom overlay if enabled above, else native)",il)){ g_idleLock=il; SaveConfig(); }
        if(g_idleLock){ float la=(float)g_idleLockAfter;
            if(slider("Lock after",la,60,3600,"%.0f s")) g_idleLockAfter=(int)la; }
        bool doff=g_idleDisplayOff;
        if(toggle("Turn off the displays","Blanks the screens after a longer idle spell "
                  "\xE2\x80\x94 any key or mouse move wakes them",doff)){ g_idleDisplayOff=doff; SaveConfig(); }
        if(g_idleDisplayOff){ float dof=(float)g_idleDisplayAfter;
            if(slider("Displays off after",dof,60,3600,"%.0f s")) g_idleDisplayAfter=(int)dof; }
        { bool hib=g_idleHibernate;
          if(toggle("Hibernate when inactive","Writes the session to disk and powers down after a long idle spell",hib)){
              g_idleHibernate=hib; SaveConfig(); }
          if(g_idleHibernate){
              if(!HibernateAllowed()) note("Hibernation is turned OFF for this PC \xE2\x80\x94 enable it with \"powercfg /hibernate on\" (admin), or this does nothing.");
              else note("Locks first when the Aether lock screen is on, so waking never lands on an unlocked desktop.");
              float ha2=(float)g_idleHibernateAfter;
              if(slider("Hibernate after",ha2,300,14400,"%.0f s")) g_idleHibernateAfter=(int)ha2; } }
        if(rel) SaveConfig();
        BrightnessProbe();
        if(g_brtOk){ float b=g_brtCache*100.0f; if(slider("Display brightness",b,0,100,"%.0f%%")) g_brtCache=b/100.0f;
            if(rel) SetBrightness((int)(g_brtCache*g_brtMax)); }
        my+=10;
        TextAt(dl,g_fSml,12,V(mx,my),WithA(COL_INK2,al),
            "The veil is click-through: any key or mouse movement clears it instantly.");
        my+=20;
        TextAt(dl,g_fSml,12,V(mx,my),WithA(COL_INK2,al),
            "It is suppressed while media is playing or an app is fullscreen.");
        my+=26;
        bool prev=g_forceDim;
        if(rowHit(my,30)&&click) g_forceDim=!prev;
        TextAt(dl,g_fSml,14,V(mx,my+6),WithA(COL_GOLD,al), g_forceDim?"Previewing \xE2\x80\x94 click to stop":"Preview the dim");
    } break;
    case SP_TASKBAR: {   // Taskbar
        header("Panel presets");
        note("The shapes other desktops' panels have. The bar is already an ordered list of items with flexible spacers and alignment â the model waybar and polybar use â so most panels are not a different bar, they are a different ORDER. Picking one is a starting point: everything it sets stays editable, and items it does not use are turned off rather than lost.");
        for(int pi=0; pi<BAR_PRESET_N; pi++){
            const BarPreset& pr=BAR_PRESETS[pi];
            bool hv=rowHit(my,38); float ha=HoverAnim(4700+pi,hv);
            if(ha>0.01f) dl->AddRectFilled(V(mx-6,my),V(mx+rowW+6,my+38),WithA(COL_INK2,(int)(ha*34*(al/255.0f))),9.0f);
            if(hv&&click) ApplyBarPreset(pr);
            TextAt(dl,g_fMed,15,V(mx+2,my+4),WithA(hv?COL_GOLD:COL_INK,al),pr.name);
            TextAt(dl,g_fSml,12,V(mx+2,my+21),WithA(COL_INK2,al),Clip(g_fSml,12,pr.blurb,rowW-16).c_str());
            my+=42;
        }
        my+=8;
        header("Style");
        { static const char* BS2[]={"Classic (s11 Caelestia)","Caelestia (new)"};
          int v=g_barStyle; if(cycler("Bar style","Either works on any edge: left / right strip or top / bottom bar",v,BS2,2)){ g_barStyle=v; SaveConfig(); } }
        if(g_barStyle==1){
            { bool v=g_barV2Kbd;   if(toggle("Keyboard layout","Layout code in the status pill; click for the list",v)){ g_barV2Kbd=v; SaveConfig(); } }
            { bool v=g_barV2Power; if(toggle("Power profile","Windows power mode in the status pill",v)){ g_barV2Power=v; SaveConfig(); } }
            { bool v=g_barV2KbdToast; if(toggle("Layout change toast",nullptr,v)){ g_barV2KbdToast=v; SaveConfig(); } }
            header("Components");
            { struct C{ const char* l; const char* s; bool* v; } cs[]={ {"Logo","Opens the launcher",&g_bv2Logo},{"Workspaces","Indicators, click to switch",&g_bv2Ws},
                {"Running apps","Open apps, click to focus",&g_bv2Apps},{"Active window","Title display",&g_bv2Win},{"Tray","System tray icons behind an expander",&g_bv2Tray},
                {"Status icons","Volume, layout, power profile, notifications",&g_bv2Status},{"Clock","Date, icon, background",&g_bv2Clock},{"Power","Session menu",&g_bv2Power} };
              for(auto& c:cs){ bool v=*c.v; if(toggle(c.l,c.s,v)){ *c.v=v; SaveConfig(); } } }
        }
        my+=10;
        bool ah=g_barAutoHide; if(toggle("Auto-hide taskbar","Reveal the bar by touching the left screen edge",ah)){ g_barAutoHide=ah; SaveConfig(); }
        bool ht=g_hideTaskbar;
        if(toggle("Replace the Windows taskbar","Hide Explorer's taskbar and inset the work area",ht)){
            g_hideTaskbar=ht; SetWindowsTaskbar(ht); SaveConfig(); }
        bool sm2=g_barSameMonitor;
        if(toggle("Only this monitor's windows","Task buttons follow the monitor the bar is on",sm2)){ g_barSameMonitor=sm2; SaveConfig(); }
        if(g_mons.size()>1){
            bool allm=(g_barMonMode==0);
            if(toggle("Taskbar on every monitor","Off: only the primary screen gets one",allm)){
                g_barMonMode=allm?0:1; g_deskDirty=true;
                if(g_hideTaskbar) ApplyWorkAreas();
                SaveConfig(); }
        }
        bool pv=g_barPreviews;
        if(toggle("Live window previews","Hover a task button for a DWM thumbnail",pv)){ g_barPreviews=pv; if(!pv) HideThumb(); SaveConfig(); }
        bool bb=g_bubble;
        if(toggle("Desktop bubble","Draw the wallpaper as a rounded inset pane",bb)){ g_bubble=bb; SaveConfig(); g_deskDirty=true; g_bubbleGeomDirty=true; }
        bool bl=g_deskLive;
        if(toggle("Live wallpaper","Show the real (animated) wallpaper through the bubble instead of a static snapshot \xE2\x80\x94 keeps Wallpaper Engine, etc. running",bl)){ g_deskLive=bl; SaveConfig(); g_deskDirty=true; }
        bool dc=g_deskClock;
        if(toggle("Desktop clock","The big time and date block on the wallpaper, bottom-right",dc)){ g_deskClock=dc; SaveConfig(); g_deskDirty=true; }
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        TextAt(dl,g_fMed,18,V(mx,my),WithA(COL_INK,al),"Widgets on the wallpaper"); my+=26;
        // Which dashboard tab is mirrored onto the wallpaper. Chosen by name, and a tab that has
        // been renamed or deleted simply falls back to Off rather than pointing at a stranger.
        { std::vector<std::string> names; names.push_back("Off");
          for(auto& t:g_tabs) names.push_back(t.name);
          // Out of the box every tab is a built-in full-page one, and those are skipped on the
          // wallpaper - so without this the chooser would offer nothing that actually shows anything.
          names.push_back("Create a layout\xE2\x80\xA6");
          const int MAKE=(int)names.size()-1;
          std::vector<const char*> opts; opts.reserve(names.size());
          for(auto& n:names) opts.push_back(n.c_str());
          int cur=0;
          for(size_t i=1;i<names.size();i++) if(names[i]==g_deskTabName){ cur=(int)i; break; }
          note("Mirrors one dashboard tab onto the wallpaper. Rearrange it in the drawer's edit mode \xE2\x80\x94 same widgets, same drag and drop.");
          int dp=choice(opts.data(),(int)opts.size(),cur);
          if(dp>=0 && dp!=cur){
              if(dp==MAKE){
                  // a ready-made canvas in the rice's arrangement: clock top-left, the three shape
                  // gauges along the bottom-left, network and the player down the right
                  DashTab t; t.name="Desktop"; t.icon=0; t.builtin=false;
                  t.widgets.push_back({WK_CLOCK,   0.03f,0.06f,0.16f,0.26f,""});
                  t.widgets.push_back({WK_M3_CPU,  0.03f,0.68f,0.10f,0.24f,""});
                  t.widgets.push_back({WK_M3_GPU,  0.14f,0.68f,0.10f,0.24f,""});
                  t.widgets.push_back({WK_M3_MEM,  0.25f,0.68f,0.10f,0.24f,""});
                  t.widgets.push_back({WK_NETWORK, 0.80f,0.06f,0.17f,0.24f,""});
                  t.widgets.push_back({WK_MEDIA,   0.74f,0.60f,0.23f,0.34f,""});
                  // the mirror matches on NAME, so two tabs called Desktop would be ambiguous
                  { std::string base=t.name; int n2=1;
                    for(;;){ bool clash=false;
                             for(auto& e:g_tabs) if(e.name==t.name){ clash=true; break; }
                             if(!clash) break;
                             t.name=base+" "+std::to_string(++n2); } }
                  g_deskTabName=t.name; g_tabs.push_back(t);
              } else {
                  g_deskTabName = (dp==0)? std::string() : names[dp];
              }
              SaveConfig(); g_deskDirty=true; } }
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        TextAt(dl,g_fMed,18,V(mx,my),WithA(COL_INK,al),"Idle motion"); my+=26;
        note("Nothing in the shell sits still: card surfaces catch a travelling light, ring gauges sweep, graphs pulse at the newest sample, the Material 3 silhouettes turn and morph into one another, and every reading eases into place instead of stepping.");
        { bool im=g_idleMotion;
          if(toggle("Keep everything moving",
                    "Idle animation across the whole shell \xE2\x80\x94 dashboard, quick settings, taskbar, lock screen and the wallpaper widgets",im)){
              g_idleMotion=im; SaveConfig(); g_deskDirty=true; } }
        if(g_idleMotion){
            float ir=g_idleRate;
            if(slider("Idle motion speed",ir,0.25f,2.0f,"%.2fx")){ g_idleRate=ir; SaveConfig(); }
            note("Reduce motion (Accessibility) switches all of it off.");
        }
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        TextAt(dl,g_fMed,18,V(mx,my),WithA(COL_INK,al),"Shape gauges"); my+=26;
        note("The Material 3 silhouette the CPU / GPU / Memory / Storage shape widgets fill up. Add them from the drawer's widget palette, under Performance.");
        { int gp=choice(M3_SHAPE_LABEL,M3_COUNT,std::clamp(g_m3Shape,0,M3_COUNT-1));
          if(gp>=0 && gp!=g_m3Shape){ g_m3Shape=gp; SaveConfig(); g_deskDirty=true; } }
        { bool qg=g_qsGauges;
          if(toggle("Gauges in quick settings","A CPU / GPU / RAM row at the bottom of the quick-settings panel",qg)){
              g_qsGauges=qg; SaveConfig(); } }
        { bool qm=g_qsMedia;
          if(toggle("Player in quick settings","Now-playing and transport in the same panel instead of a separate flyout",qm)){
              g_qsMedia=qm; SaveConfig(); } }
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        TextAt(dl,g_fMed,18,V(mx,my),WithA(COL_INK,al),"Icons"); my+=26;
        note("Which marks the bar, the flyouts and the session screen use. Themes live in linux\\icons\\.");
        { int ip=choice(ICONSET_LABEL,ICONSET_N,std::clamp(g_iconSet,0,ICONSET_N-1));
          if(ip>=0 && ip!=g_iconSet){ g_iconSet=ip; g_themedPath.clear(); FreeSvgCache(); SaveConfig(); g_deskDirty=true; } }
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        TextAt(dl,g_fMed,18,V(mx,my),WithA(COL_INK,al),"Logo"); my+=26;
        note("The mark at the start of the bar. Real distro logos live in assets\\logos\\.");
        { const char* lopts[NBARLOGOS]; int lcur=0;
          for(int i=0;i<NBARLOGOS;i++){ lopts[i]=BAR_LOGOS[i].label; if(g_barLogo==BAR_LOGOS[i].key) lcur=i; }
          int lp=choice(lopts,NBARLOGOS,lcur);
          if(lp>=0 && lp!=lcur){ g_barLogo=BAR_LOGOS[lp].key; SaveConfig(); } }
        { bool lt=g_barLogoTint;
          if(toggle("Tint the logo with the accent","Off: the distro artwork keeps its own colours",lt)){
              g_barLogoTint=lt; SaveConfig(); } }
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        TextAt(dl,g_fMed,18,V(mx,my),WithA(COL_INK,al),"Workspaces"); my+=26;
        // Status first, because "komorebi is not installed" and "komorebi is installed but not
        // running" are completely different problems and the old note conflated them.
        bool kInst=KomoInstalled(), kLive=g_komoLive.load();
        note(!kInst ? "komorebi is not installed \xE2\x80\xA2 install it with: winget install LGUG2Z.komorebi"
             : kLive ? "komorebi is running \xE2\x80\xA2 the bar mirrors its workspace ring, names and windows"
                     : "komorebi is installed but NOT running \xE2\x80\xA2 nothing that talks to it will work until it is started");
        if(kInst){
            // Start / Stop. komorebic start returns before the socket is up, so the label follows
            // g_komoLive (which the poll thread owns) rather than anything this click knows.
            const char* lbl = kLive ? "Stop komorebi" : "Start komorebi";
            float bw=170.0f, bh=30.0f;
            bool bhov = io.MousePos.x>mx && io.MousePos.x<mx+bw && io.MousePos.y>my && io.MousePos.y<my+bh;
            float bha = HoverAnim(idBase+770, bhov);
            dl->AddRectFilled(V(mx,my),V(mx+bw,my+bh),
                              kLive ? WithA(IM_COL32(224,96,88,255),(int)((70+bha*60)*e))
                                    : AccA((int)((70+bha*60)*e)), 9);
            TextAt(dl,g_fSml,14,V(mx+bw*0.5f-TextW(g_fSml,14,lbl)*0.5f,my+7),WithA(COL_INK,al),lbl);
            if(click&&bhov){
                if(kLive) KomorebiQuit();
                else { EnsureExplorerForKomorebi();
                       // give explorer a moment to register its COM classes before komorebi asks
                       std::thread([]{ Sleep(2500); KomorebiLaunch(g_komoMasir,g_komoWhkd); }).detach();
                       KomorebiStart(); }
            }
            my+=bh+10;
            if(kLive){
                // The fix for "the same app shows on every workspace": those windows are simply not
                // managed, so komorebi never hides them.
                const char* al2 = g_adoptBusy.load() ? "Adopting\xE2\x80\xA6" : "Tile windows that were already open";
                float aw=260.0f, ah=30.0f;
                bool ahov = io.MousePos.x>mx && io.MousePos.x<mx+aw && io.MousePos.y>my && io.MousePos.y<my+ah;
                float aha = HoverAnim(idBase+771, ahov);
                dl->AddRectFilled(V(mx,my),V(mx+aw,my+ah),WithA(COL_INK2,(int)((34+aha*46)*e)),9);
                TextAt(dl,g_fSml,14,V(mx+aw*0.5f-TextW(g_fSml,14,al2)*0.5f,my+7),WithA(COL_INK,al),al2);
                if(click&&ahov&&!g_adoptBusy.load()) KomoAdoptExisting();
                my+=ah+6;
                note("komorebi only manages windows it saw open. Anything already running is never hidden, so it appears on every workspace. This focuses each one briefly to hand it over \xE2\x80\x94 your windows will flash.");
                if(g_adoptDone>0){ char ln[64]; snprintf(ln,sizeof(ln),"last sweep handed over %d window%s",g_adoptDone,g_adoptDone==1?"":"s"); note(ln); }
                { bool aa=g_komoAutoAdopt;
                  if(toggle("Adopt already-open windows","Runs the sweep by itself when komorebi starts, skipping anything it already manages",aa)){
                      g_komoAutoAdopt=aa; SaveConfig(); } }
            }
            { bool ka=g_komoAutoStart;
              if(toggle("Start komorebi with the shell","Launches it at sign-in if it is not already running",ka)){
                  g_komoAutoStart=ka; SaveConfig(); } }
            { bool km=g_komoMasir;
              if(toggle("Focus follows mouse","Starts komorebi with --masir, the way niri focuses",km)){
                  g_komoMasir=km; SaveConfig(); } }
            { bool kw=g_komoWhkd;
              if(toggle("Start the keybindings too","--whkd, so your ~/.config/whkdrc shortcuts run; without it komorebi has no keys at all",kw)){
                  g_komoWhkd=kw; SaveConfig(); } }
            { bool ke=g_keepExplorer;
              if(toggle("Keep Explorer running","OFF by default. Not required - Minimize hiding is what keeps komorebi alive. It only helps komorebi adopt windows that were already open before it started.",ke)){
                  g_keepExplorer=ke; SaveConfig(); if(ke) EnsureExplorerForKomorebi(); } }
        }
        { int wp=choice(WSSRC_LABEL,3,std::clamp(g_wsSource,0,2));
          if(wp>=0 && wp!=g_wsSource){ g_wsSource=wp; SaveConfig();
              if(g_wsSource!=WSSRC_VDESK) KomorebiStart(); RefreshWorkspaces(); } }
        { bool kr=g_komoReserve;
          if(toggle("Reserve the bar's strip in komorebi",
                    "Sets monitor-work-area-offset so tiled windows stop under the bar",kr)){
              g_komoReserve=kr; SaveConfig(); RefreshWorkspaces(); } }
        { bool fh=g_fastHide.load();
          if(toggle("Hide workspaces instantly",
                    "The shell cloaks the leaving workspace itself instead of waiting for komorebi's minimize, which animates and can leave Electron windows on screen for seconds",fh)){
              g_fastHide.store(fh); if(!fh) KomoUncloakAll(); SaveConfig(); } }
        { bool sl=g_wsSlide.load();
          if(toggle("Slide windows when switching workspaces",
                    "The niri-style slide: the old workspace leaves as the new one arrives",sl)){
              g_wsSlide.store(sl); SaveConfig(); } }
        if(g_wsSlide.load()){ static const char* ST[]={"Live previews (smooth)","Move the real windows"}; int v=g_wsSlideStyle;
          if(cycler("Slide style","Previews: both workspaces glide on a layer over the screen, in step with the display; the windows never move. Real windows: the older way, choppier",v,ST,2)){ g_wsSlideStyle=v; SaveConfig(); } }
        { bool v=g_reviveFrozen; if(toggle("Revive frozen app windows","If Spotify, Discord or a browser comes back from a switch as a flat grey rectangle, restart just its graphics helper so it draws again",v)){ g_reviveFrozen=v; SaveConfig(); } }
        { bool ta=g_tileAnim;
          if(toggle("Smooth window tiling",
                    "Windows glide to their new place when komorebi tiles, swaps or moves them. Keep komorebi's own animation off - it resizes windows every frame and turns Chromium apps grey",ta)){
              g_tileAnim=ta; SaveConfig(); } }
        if(g_tileAnim){ static const char* TS[]={"Live previews (moves and resizes)","Move the real windows"}; int v=g_tileStyle;
          if(cycler("Tiling style","Previews: the windows' live previews glide AND resize on a layer over the screen, exactly like the workspace slide, and no window is ever touched. Real windows: the older way - only where a window is can animate, so every resize still snaps",v,TS,2)){ g_tileStyle=v; SaveConfig(); } }
        { bool pk=g_wsPauseKomoAnim.load();
          if(toggle("Pause komorebi animations during the slide",
                    "komorebi's own window animations stay on, and turn off only while a workspace switch slides, so the two never fight",pk)){
              g_wsPauseKomoAnim.store(pk); SaveConfig(); } }
        if(g_wsSlide.load()){
            // Measured on this machine (Windows 11 build 26100, komorebi 0.1.41-nightly): with
            // "Cloak", komorebi hard-panics on EVERY workspace switch inside its virtual-desktop COM
            // (com/mod.rs, REGDB_E_CLASSNOTREG) and the process dies. "Minimize" survives. So the
            // hiding mode that gives the best slide is the one that kills the WM on this build.
            note(g_wsSlideStyle==0? "Live previews work with komorebi's \"Minimize\" hiding: both the leaving and the arriving workspace slide." : "Moving real windows is best with \"Cloak\" hiding, which crashes komorebi on Windows 11 26100 - with \"Minimize\" only the arriving windows slide.");
            { float d=(float)g_wsSlideMs.load();
              if(slider("Slide duration",d,100.0f,800.0f,"%.0f ms")){ g_wsSlideMs.store((int)d); SaveConfig(); } }
            { float fp=(float)g_wsSlideFps.load();
              if(slider("Slide frame rate",fp,30.0f,240.0f,"%.0f fps")){ g_wsSlideFps.store((int)fp); SaveConfig(); } }
            note("Switching again before a slide has finished does not cancel it: the previews keep the position they have reached and are re-aimed at the new workspace, on a shorter duration. Flick through five workspaces and you get five slides, each quicker than the last.");
            { float mn=(float)g_wsSlideMinMs;
              if(slider("Shortest slide (rapid switching)",mn,60.0f,400.0f,"%.0f ms")){ g_wsSlideMinMs=(int)mn; SaveConfig(); } }
            { bool vv=g_wsSlideVert.load();
              if(toggle("Slide vertically","Off: horizontal, the way niri moves between workspaces",vv)){
                  g_wsSlideVert.store(vv); SaveConfig(); } }
        }
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        TextAt(dl,g_fMed,18,V(mx,my),WithA(COL_INK,al),"workspace overview"); my+=26;
        note("A 3-D view of every workspace on the screen you are on â the desktop cube. The workspace you are on is captured live; the others show the last picture taken while they were showing. Drag to spin or slide, scroll to zoom, click a window to go straight to it.");
        { bool ov=g_ovEnable;
          if(toggle("Workspace overview","Needs komorebi and a Windows build that allows screen capture",ov)){ g_ovEnable=ov; SaveConfig(); } }
        if(g_ovEnable){
            { static const char* OS[]={"Cube","Flat plane"}; int v=g_ovStyle;
              if(cycler("Overview shape","Cube: the workspaces wrap around a vertical axis, like Compiz. Flat plane: they lie in a row and you slide along them",v,OS,2)){ g_ovStyle=v; SaveConfig(); } }
            { bool st=g_ovSuperTab;
              if(toggle("Open it with Super+Tab","Takes the gesture from Windows' Task View. Windows will not hand Super+Tab over as an ordinary hotkey, so the shell intercepts it the same way it intercepts Alt+Tab for the switcher",st)){ g_ovSuperTab=st; SaveConfig(); } }
            note(g_hk[HK_OVERVIEW].ok? "Its hotkey is also registered â see the Hotkeys page to change it."
                                     : "Its hotkey could not be registered â something else already owns that combination. Change it on the Hotkeys page.");
        }
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        TextAt(dl,g_fMed,18,V(mx,my),WithA(COL_INK,al),"niri mode"); my+=26;
        // Not an emulation: komorebi 0.1.41 has a `scrolling` layout, which is niri's model - one
        // row of columns with the screen as a viewport onto it, and focus scrolling the row.
        note("Switches every workspace to komorebi's scrolling layout: one row of columns, the screen a window onto it, focus scrolls the row \xE2\x80\x94 the way niri works.");
        { bool nm=g_niriMode;
          if(toggle("Scrolling layout","Off puts every workspace back to BSP",nm)){
              g_niriMode=nm; SaveConfig();
              if(g_komoLive.load()){
                  KomoSetLayoutAll(g_niriMode ? L"scrolling" : L"bsp");
                  if(g_niriMode) KomoApplyScrollCols(g_niriCols);
              } } }
        if(g_niriMode){
            float nc=(float)g_niriCols;
            if(slider("Visible columns",nc,1.0f,5.0f,"%.0f")){
                g_niriCols=(int)nc; SaveConfig();
                if(g_komoLive.load()) KomoApplyScrollCols(g_niriCols); }
            note("komorebi only takes a column count for the FOCUSED workspace, so the shell re-sends it whenever a workspace takes focus.");
            if(!g_komoLive.load()) note("Start komorebi above to apply it.");
        }
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        TextAt(dl,g_fMed,18,V(mx,my),WithA(COL_INK,al),"System tray"); my+=26;
        { bool tc=g_trayCollapse;
          if(toggle("Collapse the tray behind a chevron","Off: every tray icon sits in the bar, the way it used to",tc)){
              g_trayCollapse=tc; if(!tc) g_trayOpen=false; SaveConfig(); }
          note("Pin an icon in the flyout to keep it in the bar \xE2\x80\xA2 middle-click one in the bar to send it back");
          { std::lock_guard<std::mutex> lk(g_systrayMtx);
            int tot=0,shown=0; for(auto&s:g_systray){ if(s.hidden||!s.tex) continue; tot++; if(TrayIsShown(s)) shown++; }
            char ln[72]; snprintf(ln,sizeof(ln),"%d icon%s \xE2\x80\xA2 %d in the bar",tot,tot==1?"":"s",shown);
            TextAt(dl,g_fSml,14,V(mx,my),WithA(COL_INK2,al),ln); my+=24; }
          if(!g_trayShown.empty() && buttons({"Send them all back to the tray"},240.0f)==0){
              g_trayShown.clear(); SaveConfig(); } }
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        note("Caelestia's own bar is a 60px strip around a 40px inner column (innerWidth 40 + "
             "border thickness 10 either side), laid out logo \xE2\x80\xA2 workspaces \xE2\x80\xA2 tray "
             "\xE2\x80\xA2 clock \xE2\x80\xA2 status \xE2\x80\xA2 power. Works on either side edge.");
        if(buttons({"Match Caelestia's vertical bar"},250)==0) ApplyCaelestiaBarPreset();
        my+=6;
        { bool bp=g_barPills;
          if(toggle("Group backgrounds","The lighter rounded panel behind related items \xE2\x80\x94 "
                    "workspaces + apps, and the system cluster. Side edges only.",bp)){
              g_barPills=bp; SaveConfig(); } }
        my+=6;
        TextAt(dl,g_fMed,18,V(mx,my),WithA(COL_INK,al),"Bar items"); my+=26;
        note("Drag a row to reorder \xE2\x80\xA2 uncheck to hide it \xE2\x80\xA2 \"Flexible space\" soaks up the leftover room");
        {   // ---- the item list: drag to reorder, checkbox to hide ----
            // Immediate-mode drag: the rows ARE the model, so a drag reorders g_barItems live and the
            // next frame already draws the new order. There is no ghost row to keep in sync, and
            // letting go anywhere just stops - which is why a cancelled drag cannot corrupt the list.
            static int   dragFrom=-1;     // index being dragged, -1 = idle
            static float dragGrab=0;      // grab point inside the row, so it does not snap to the cursor
            static bool  dragMoved=false; // only save if the order actually changed
            const float RH=46.0f;
            float listTop=my;
            int nrow=(int)g_barItems.size();
            bool save=false;

            if(click){                     // begin a drag on the grip column only
                for(int r=0;r<nrow;r++){ float ry=listTop+r*RH;
                    if(io.MousePos.x>mx-8&&io.MousePos.x<mx+30&&io.MousePos.y>ry&&io.MousePos.y<ry+RH){
                        dragFrom=r; dragGrab=io.MousePos.y-ry; dragMoved=false; break; } } }
            if(dragFrom>=0&&down&&nrow>0){ // live reorder while held
                int want=(int)floorf((io.MousePos.y-dragGrab-listTop)/RH+0.5f);
                want=std::clamp(want,0,nrow-1);
                if(want!=dragFrom){ BarItemCfg mv=g_barItems[dragFrom];
                    g_barItems.erase(g_barItems.begin()+dragFrom);
                    g_barItems.insert(g_barItems.begin()+want,mv);
                    dragFrom=want; dragMoved=true; } }
            if(dragFrom>=0&&!down){ if(dragMoved) save=true; dragFrom=-1; }

            for(int r=0;r<nrow;r++){
                BarItemCfg& c=g_barItems[r];
                if(c.id<0||c.id>=BIT_COUNT) continue;
                const BarItemDef& d=BAR_ITEMS[c.id];
                float ry=listTop+r*RH;
                bool hov=io.MousePos.x>mx-8&&io.MousePos.x<mx+rowW&&io.MousePos.y>ry&&io.MousePos.y<ry+RH;
                bool dragging=(dragFrom==r);
                if(dragging) dl->AddRectFilled(V(mx-8,ry+1),V(mx+rowW,ry+RH-3),AccA((int)(e*34)),10);
                else if(hov) dl->AddRectFilled(V(mx-8,ry+1),V(mx+rowW,ry+RH-3),WithA(COL_INK2,(int)(e*24)),10);
                // grip: two columns of dots, the universal "drag me"
                { ImU32 gc=WithA(COL_INK2,(int)(al*(dragging?0.95f:0.55f)));
                  for(int gx=0;gx<2;gx++) for(int gy=0;gy<3;gy++)
                      dl->AddCircleFilled(V(mx+6+gx*7.0f,ry+RH*0.5f-7.0f+gy*7.0f),1.6f,gc); }
                // an item that is switched ON but has nothing to show right now says so, rather than
                // leaving the user hunting the strip for a control that is working exactly as designed
                bool inert = (c.id==BIT_MIC&&!g_micPresent) || (c.id==BIT_BATT&&!g_st.hasBattery) ||
                             false;
                int  la = (int)(al*(c.on?1.0f:0.45f));
                TextAt(dl,g_fMed,16,V(mx+30,ry+5),WithA(COL_INK,la),d.label);
                TextAt(dl,g_fSml,12,V(mx+30,ry+25),WithA(COL_INK2,(int)(al*(c.on?0.85f:0.4f))),
                       inert? "Not available on this PC right now" : d.desc);
                // checkbox
                { float bs=20, bx2=mx+rowW-bs-8, by2=ry+(RH-4-bs)*0.5f;
                  dl->AddRect(V(bx2,by2),V(bx2+bs,by2+bs),c.on?A(COL_GOLD):WithA(COL_INK2,(int)(al*0.6f)),5,0,1.6f);
                  if(c.on){ dl->AddRectFilled(V(bx2,by2),V(bx2+bs,by2+bs),A(COL_GOLD),5);
                      dl->AddLine(V(bx2+5,by2+10),V(bx2+9,by2+14),PanelCol(al),2.0f);
                      dl->AddLine(V(bx2+9,by2+14),V(bx2+15,by2+6),PanelCol(al),2.0f); }
                  bool chov=io.MousePos.x>bx2-6&&io.MousePos.x<bx2+bs+6&&io.MousePos.y>ry&&io.MousePos.y<ry+RH;
                  if(click&&chov){ c.on=!c.on; save=true; } }
            }
            my=listTop+nrow*RH+6;

            // ---- alignment ----
            int nsp=0; for(auto& c:g_barItems) if(c.on&&BarIsSpacer(c.id)) nsp++;
            TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),"Alignment"); my+=22;
            { static const char* AL[4]={"Start","Center","End","Spread"};
              float bw=(rowW-24)/4.0f;
              for(int k=0;k<4;k++){
                  float bx2=mx+k*(bw+8);
                  bool sel=(g_barAlign==k);
                  bool hov=io.MousePos.x>bx2&&io.MousePos.x<bx2+bw&&io.MousePos.y>my&&io.MousePos.y<my+30;
                  float ha=HoverAnim(idBase+80+k,hov);
                  dl->AddRectFilled(V(bx2,my),V(bx2+bw,my+30),
                                    sel?AccA((int)(e*210)):WithA(COL_INK2,(int)((26+ha*40)*e)),9);
                  TextAt(dl,g_fSml,13,V(bx2+bw/2-TextW(g_fSml,13,AL[k])/2,my+7),
                         sel?PanelCol(al):WithA(COL_INK,al),AL[k]);
                  if(click&&hov&&!sel){ g_barAlign=k; save=true; } }
              my+=38; }
            // Alignment and a spacer both answer "where does the slack go?", so only one can win.
            // Saying which is in charge beats a control that silently does nothing.
            if(nsp>0) note("A flexible space is in charge of the leftover room, so alignment is ignored. "
                           "Uncheck every flexible space to use it.");

            if(buttons({"Reset bar items"},170)==0){ BarItemsDefault(); g_barAlign=BALIGN_START; save=true; }
            if(save) SaveConfig();
        }
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        TextAt(dl,g_fMed,18,V(mx,my),WithA(COL_INK,al),"Dock"); my+=30;
        { bool dsave=false;
          bool don=g_dockOn;       if(toggle("Bottom dock","A modern macOS-style magnifying dock",don)){ g_dockOn=don; dsave=true; }
          bool dah=g_dockAutohide; if(toggle("Auto-hide","Reveal only when the cursor reaches the bottom edge",dah)){ g_dockAutohide=dah; dsave=true; }
          if(slider("Icon size",   g_dockIcon,    28.0f,80.0f,"%.0f"))  dsave=true;
          if(slider("Magnification",g_dockMag,    1.0f, 2.5f,"%.2fx")) dsave=true;
          if(slider("Zoom spread", g_dockMagRange,1.0f, 5.0f,"%.1f"))  dsave=true;
          if(slider("Icon gap",    g_dockGap,     4.0f, 30.0f,"%.0f")) dsave=true;
          if(slider("Rounding",    g_dockRound,   0.0f, 44.0f,"%.0f")) dsave=true;
          if(slider("Opacity",     g_dockOpacity, 0.3f, 1.0f,"%.2f"))  dsave=true;
          { char hint[96]; snprintf(hint,sizeof(hint),"Right-click a dock icon to pin it \xE2\x80\xA2 drag pinned icons to reorder \xE2\x80\xA2 %d pinned",(int)g_dockPins.size());
            my+=4; TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,(int)(al*0.85f)),hint); my+=20; }
          if(dsave) SaveConfig(); }
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        my+=8;
        // quick presets, then the full edge/size/length controls
        { int bp=buttons({"Left dock","Bottom taskbar","Floating bottom","Top bar"},150);
          Panel& B=g_pn[PN_BAR];
          if(bp==0){ B.edge=EDGE_LEFT;  B.span=1.0f; B.spanMax=0; B.anchor=0.5f; B.gap=(float)g_gap; }
          else if(bp==1){ B.edge=EDGE_BOTTOM; B.span=1.0f; B.spanMax=0; B.anchor=0.5f; B.gap=(float)g_gap; if(B.size<38)B.size=42; }
          else if(bp==2){ B.edge=EDGE_BOTTOM; B.span=0.62f; B.spanMax=0; B.anchor=0.5f; B.gap=(float)std::max(g_gap,14); if(B.size<38)B.size=44; }
          else if(bp==3){ B.edge=EDGE_TOP; B.span=1.0f; B.spanMax=0; B.anchor=0.5f; B.gap=(float)g_gap; if(B.size<38)B.size=42; }
          if(bp>=0){ g_deskDirty=true; if(g_hideTaskbar) ApplyWorkAreas(); SaveConfig(); } }
        { static const int barEdges[4]={EDGE_LEFT,EDGE_RIGHT,EDGE_TOP,EDGE_BOTTOM};
          panelLayout(PN_BAR,barEdges,4,"Bar thickness",30,80,true); }
        my+=6;
        header("Quick settings panel");
        { static const int qsEdges[4]={EDGE_LEFT,EDGE_RIGHT,EDGE_TOP,EDGE_BOTTOM};
          panelLayout(PN_QS,qsEdges,4,"Panel width",300,520,false); }
        my+=6;
        float gpv=(float)g_gap;
        if(slider("Desktop gap",gpv,0,40,"%.0f px")){ g_gap=(int)gpv; g_deskDirty=true; g_bubbleGeomDirty=true; }
        float rdv=g_bubbleRound;
        if(slider("Corner radius",rdv,0,44,"%.0f px")){ g_bubbleRound=rdv; g_deskDirty=true; }
        if(rel) SaveConfig();
        { bool ca=g_confineApps;
          if(toggle("Keep apps inside the bubble","Maximized windows fill the bubble instead of the whole screen, and anything hanging over the edge is pulled back in",ca)){
              g_confineApps=ca; SaveConfig(); g_bubbleGeomDirty=true; } }
        { bool hd=g_hostDesktop;
          if(toggle("Desktop icons and Wallpaper Engine",
                    "Runs Explorer purely as the desktop host. Without it there is nothing to draw desktop icons or to host a live wallpaper â its taskbar stays hidden either way.",hd)){
              g_hostDesktop=hd; SaveConfig();
              if(g_hostDesktop) EnsureDesktopHost(); } }
        my+=10;
        bool sr=IsShellReplaced();
        bool was=sr;
        if(toggle("Run as the Windows shell","Replaces explorer.exe at sign-in for this user",sr) && sr!=was){
            SetShellReplaced(sr);
            if(sr) AetherShellExec(nullptr,L"open",L"explorer.exe",nullptr,nullptr,SW_SHOWNORMAL);   // keep desktop icons
        }
        TextAt(dl,g_fSml,13,V(mx,my),WithA(g_isShell?COL_GOLD:COL_INK2,al),
            g_isShell? "Running AS the shell this session \xE2\x80\x94 login items and the shell window are ours."
                     : "Running alongside explorer.exe this session.");
        my+=22;
        TextAt(dl,g_fSml,12,V(mx,my),WithA(COL_INK2,al),
            "Recovery: run  Aether.exe --restore-shell , or Ctrl+Shift+Esc \xE2\x86\x92 Run \xE2\x86\x92 explorer.exe");
    } break;
    case SP_NOTIF: {   // Notifications
        header("Sidebar");
        { static const char* SS[]={"Classic (separate panels)","Caelestia (one merged sidebar)"};
          int v=g_sideStyle; if(cycler("Sidebar style",nullptr,v,SS,2)){ g_sideStyle=v; SaveConfig(); } }
        if(g_sideStyle==1){
            { float w=(float)g_sideWidth; if(slider("Sidebar width",w,320,900,"%.0f px")){ g_sideWidth=(int)w; SaveConfig(); } }
            { static const char* TS[]={"Classic panel","Caelestia card"};
              int v=g_toastStyle; if(cycler("New notification pop-up",nullptr,v,TS,2)){ g_toastStyle=v; SaveConfig(); } }
            { static const char* RM[]={"Fullscreen (ffmpeg)","All screens (ffmpeg)","Game Bar (window)"};
              int v=g_recMode; if(cycler("Screen recorder",nullptr,v,RM,3)){ g_recMode=v; SaveConfig(); } }
            { static char tb[200]; static std::string was="\x01";
              if(was!=g_sideToggles){ snprintf(tb,sizeof(tb),"%s",g_sideToggles.c_str()); was=g_sideToggles; }
              textField("Quick toggles (wifi,bluetooth,mic,settings,gamemode,dnd,keepawake,theme,record,lock,vpn)",tb,sizeof(tb),9711,false);
              if(g_sideToggles!=tb){ g_sideToggles=tb; was=g_sideToggles; SaveConfig(); } }
            { static char ab[160]; static std::string was="\x01";
              if(was!=g_sideActions){ snprintf(ab,sizeof(ab),"%s",g_sideActions.c_str()); was=g_sideActions; }
              textField("Action buttons (logout,power,avatar,updates,reload,lock,settings,sleep)",ab,sizeof(ab),9712,false);
              if(g_sideActions!=ab){ g_sideActions=ab; was=g_sideActions; SaveConfig(); } }
            { int bt=buttons({"Avatar GIF / picture","Use profile picture"},200);
              if(bt==0) PickFileAsync(L"Images\0*.gif;*.png;*.jpg;*.jpeg;*.webp\0All files\0*.*\0",nullptr,[](const std::string& f){ g_sideAvatar=f; SaveConfig(); });
              else if(bt==1){ g_sideAvatar.clear(); SaveConfig(); } }
        }
        my+=10;
        bool sn=g_suppressToasts;
        if(toggle("Replace Windows toasts","Hide the system popup once our listener is active",sn)){ g_suppressToasts=sn; SaveConfig(); }
        bool dd=g_dnd;
        if(toggle("Do not disturb","Never auto-pop the notification panel",dd)){ g_dnd=dd; SaveConfig(); }
        bool np=g_nowPlaying;
        if(toggle("Now Playing toast","Pop a card in the corner when a new track starts",np)){ g_nowPlaying=np; SaveConfig(); }
        { bool tl=g_lyricsToast; if(toggle("Lyrics on the Now Playing toast","Sing along with the current line when the song has synced lyrics",tl)){ g_lyricsToast=tl; SaveConfig(); } }
        my+=10;
        header("Cards");
        { bool v=g_nfFlick;  if(toggle("Flick to dismiss","Drag a notification sideways and let go to throw it away",v)){ g_nfFlick=v; SaveConfig(); } }
        { bool v=g_nfMiddle; if(toggle("Middle-click to dismiss",nullptr,v)){ g_nfMiddle=v; SaveConfig(); } }
        { bool v=g_nfExpandable; if(toggle("Expandable","A chevron opens a notification to its full message",v)){ g_nfExpandable=v; SaveConfig(); } }
        if(g_nfExpandable){ bool v=g_nfExpandDefault; if(toggle("Open expanded","Show the full message without clicking the chevron",v)){ g_nfExpandDefault=v; SaveConfig(); } }
        { static const char* DA[]={"Slide away","Fade","Shrink"};
          int v=g_nfDismissAnim; if(cycler("Dismiss animation",nullptr,v,DA,3)){ g_nfDismissAnim=v; SaveConfig(); } }
        { float d=(float)g_nfDismissMs; if(slider("Dismiss duration",d,60,1500,"%.0f ms")) g_nfDismissMs=(int)d; }
        { static const char* AA[]={"Slide in","Fade","Pop","None"};
          int v=g_nfArriveAnim; if(cycler("Arrive animation",nullptr,v,AA,4)){ g_nfArriveAnim=v; SaveConfig(); } }
        { float d=(float)g_nfArriveMs; if(slider("Arrive duration",d,60,1500,"%.0f ms")) g_nfArriveMs=(int)d; }
        if(rel) SaveConfig();
        note("The panel's own open/close motion is under Effects > Panel motion.");
        my+=10;
        char st[160];
        snprintf(st,160,"Listener: %s", g_notifAllowed? "active" :
                 "unavailable (launch the packaged app to grant access)");
        TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),st); my+=24;
        size_t cnt; { std::lock_guard<std::mutex> lk(g_notifMtx); cnt=g_notifs.size(); }
        snprintf(st,160,"%zu notification%s currently held",cnt,cnt==1?"":"s");
        TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),st); my+=34;
        if(rowHit(my,34)&&click){ std::lock_guard<std::mutex> lk(g_notifMtx); g_notifs.clear(); g_reqClearAll=1; }
        TextAt(dl,g_fSml,14,V(mx,my+8),WithA(COL_GOLD,al),"Clear all notifications");
    } break;
    case SP_LAUNCHER: {   // Launcher
        { static const char* LS[]={"Classic (Aether)","Caelestia"};
          int v=g_launStyle; if(cycler("Launcher style","Caelestia: heart / hide buttons, command icons, pill search, accent dots in the wallpaper picker",v,LS,2)){ g_launStyle=v; SaveConfig(); } }
        if(g_launStyle==1){ bool v=g_wallDots; if(toggle("Accent colour dots in the wallpaper picker",nullptr,v)){ g_wallDots=v; SaveConfig(); } }
        bool ld=g_launchDesc; if(toggle("Show descriptions","Second line under each result",ld)){ g_launchDesc=ld; SaveConfig(); }
        { bool lw=g_launchWindows;
          if(toggle("Search open windows","Typing a window title switches to it instead of launching a second copy",lw)){
              g_launchWindows=lw; SaveConfig(); } }
        float mr=(float)g_launchMax;
        if(slider("Visible results",mr,3,20,"%.0f rows")) g_launchMax=(int)mr;
        if(rel) SaveConfig();
        my+=10;
        header("Panel");
        { int lp=choice(LPOS_LABEL,LPOS_N,std::clamp(g_launPos,0,LPOS_N-1));
          if(lp>=0 && lp!=g_launPos){ g_launPos=lp; SaveConfig(); } }
        { float lwd=g_launWidth; if(slider("Width",lwd,360,1400,"%.0f px")) g_launWidth=lwd; }
        { float lrd=g_launRound; if(slider("Corner radius",lrd,0,48,"%.0f px")) g_launRound=lrd; }
        { float lop=g_launOpacity*100.0f;
          if(slider("Background opacity",lop,10,100,"%.0f%%")) g_launOpacity=lop/100.0f; }
        { bool lb=g_launBlur;
          if(toggle("Frost the background","Blurs whatever is behind the panel instead of showing it straight through",lb)){
              g_launBlur=lb; SaveConfig(); } }
        if(rel) SaveConfig();
        header("Result animations");
        { static const char* SW[]={"Fade in","Slide up","None"};
          int v=g_launSwitchAnim; if(cycler("When results change",nullptr,v,SW,3)){ g_launSwitchAnim=v; SaveConfig(); } }
        { float d=(float)g_launSwitchMs; if(slider("Result change duration",d,0,1000,"%.0f ms")) g_launSwitchMs=(int)d; }
        { bool v=g_launResize; if(toggle("Resize smoothly","Grow and shrink to fit the results instead of jumping",v)){ g_launResize=v; SaveConfig(); } }
        if(rel) SaveConfig();

        my+=10;
        header("Motion");
        note("How the panel arrives and leaves. Most launchers have no transition at all \xE2\x80\x94 Walker, the GTK4 one, is themed entirely in CSS and never animates.");
        { int la=choice(LANIM_LABEL,LANIM_N,std::clamp(g_launAnimStyle,0,LANIM_N-1));
          if(la>=0 && la!=g_launAnimStyle){ g_launAnimStyle=la; SaveConfig(); } }
        if(g_launAnimStyle!=LANIM_NONE){
            { float om=(float)g_launOpenMs;  if(slider("Open time", om,40,600,"%.0f ms")) g_launOpenMs =(int)om; }
            { float cm=(float)g_launCloseMs; if(slider("Close time",cm,40,600,"%.0f ms")) g_launCloseMs=(int)cm; }
            if(rel) SaveConfig();
            { bool st=g_launStagger;
              if(toggle("Cascade the results","Each row slides up a beat after the one above it",st)){
                  g_launStagger=st; SaveConfig(); } }
        }

        my+=10;
        header("Layout & look");
        note("Shapes, glow, 3D and the other layouts make the launcher a floating card (it stops growing out of the frame).");
        { static const char* LL[]={"List","Grid","Horizontal strip","Vertical (screen edge)","Radial ring"};
          int v=g_launLayout; if(cycler("Layout",nullptr,v,LL,5)){ g_launLayout=v; SaveConfig(); } }
        if(g_launLayout==1){ float c=(float)g_launGridCols; if(slider("Grid columns",c,3,10,"%.0f")) g_launGridCols=(int)c; if(rel) SaveConfig(); }
        if(g_launLayout==3){ static const char* SD[]={"Left edge","Right edge"}; int v=g_launSide; if(cycler("Side",nullptr,v,SD,2)){ g_launSide=v; SaveConfig(); } }
        if(g_launLayout!=0){ float sz=g_launIconSize*100.0f; if(slider("Icon size",sz,60,180,"%.0f%%")) g_launIconSize=sz/100.0f; if(rel) SaveConfig(); }
        { static const char* SH[]={"Rounded","Pill","Square","Chamfered corners","Hexagon","Slanted"};
          int v=g_launShape; if(cycler("Panel shape",nullptr,v,SH,6)){ g_launShape=v; SaveConfig(); } }
        { static const char* BGX[]={"None","Nebula","Particles","Aurora"};
          int v=g_launBgFx; if(cycler("Background effect","Drawn inside the panel",v,BGX,4)){ g_launBgFx=v; SaveConfig(); } }
        my+=6;
        header("Glow & 3D");
        { bool v=g_launGlow; if(toggle("Glow","A soft halo around the panel",v)){ g_launGlow=v; SaveConfig(); } }
        if(g_launGlow){
            { static const char* GC[]={"Accent colour","Rainbow","Custom colour"}; int v=g_launGlowCol; if(cycler("Glow colour",nullptr,v,GC,3)){ g_launGlowCol=v; SaveConfig(); } }
            if(g_launGlowCol==2){ static char hx[16]={0}; static std::string seen="\x01"; if(seen!=g_launGlowHex){ snprintf(hx,sizeof(hx),"%s",g_launGlowHex.c_str()); seen=g_launGlowHex; }
                textField("Custom glow colour (#rrggbb)",hx,sizeof(hx),9811,false); if(g_launGlowHex!=hx){ g_launGlowHex=hx; seen=g_launGlowHex; SaveConfig(); } }
            { float gs=g_launGlowStr*100.0f; if(slider("Glow strength",gs,10,150,"%.0f%%")) g_launGlowStr=gs/100.0f; if(rel) SaveConfig(); }
            { bool v=g_launGlowPulse; if(toggle("Breathe","The glow slowly pulses",v)){ g_launGlowPulse=v; SaveConfig(); } }
        }
        { static const char* DP[]={"Flat","Tilt towards the pointer","Extruded slab","Tilt + extruded slab"};
          int v=g_launDepth; if(cycler("3D",nullptr,v,DP,4)){ g_launDepth=v; SaveConfig(); } }
        if(g_launDepth==1||g_launDepth==3){ float ta=g_launTilt*100.0f; if(slider("Tilt amount",ta,0,100,"%.0f%%")) g_launTilt=ta/100.0f; if(rel) SaveConfig(); }
        my+=6;
        header("Typing");
        { static const char* SA[]={"None","Pop","Wave","Bounce","Glitch","Rainbow","Typewriter"};
          int v=g_launSearchAnim; if(cycler("Letter animation","How each letter you type appears",v,SA,7)){ g_launSearchAnim=v; SaveConfig(); } }
        { static const char* CS[]={"Bar","Block","Underline"}; int v=g_launCaret; if(cycler("Caret",nullptr,v,CS,3)){ g_launCaret=v; SaveConfig(); } }
        { bool v=g_launSmear; if(toggle("Smear trail","The caret stretches and leaves a trail as it moves",v)){ g_launSmear=v; SaveConfig(); } }
        { bool v=g_launSparks; if(toggle("Sparks","Each new letter throws off a few sparks",v)){ g_launSparks=v; SaveConfig(); } }
        my+=6;
        header("Results");
        { static const char* RA[]={"Cascade","Fade","Scale","Slide in","Flip","None"}; int v=g_launResAnim; if(cycler("Result animation",nullptr,v,RA,6)){ g_launResAnim=v; SaveConfig(); } }
        { static const char* SLS[]={"Fill","Glide","Outline","Accent bar","Glow"}; int v=g_launSel; if(cycler("Selection",nullptr,v,SLS,5)){ g_launSel=v; SaveConfig(); } }
        { static const char* IH[]={"None","Bounce","Grow","Wiggle"}; int v=g_launIconHover; if(cycler("Icon on hover",nullptr,v,IH,4)){ g_launIconHover=v; SaveConfig(); } }
        { static const char* LFX[]={"None","Ripple","Zoom"}; int v=g_launLaunchFx; if(cycler("When something opens",nullptr,v,LFX,3)){ g_launLaunchFx=v; SaveConfig(); } }
        my+=6;
        header("Favourites tray");
        note("Right-click any app in the launcher to pin it. In the tray: click opens, drag reorders, right-click unpins.");
        { static const char* TR[]={"Off","Above the results","Below the results"}; int v=g_launTray; if(cycler("Tray",nullptr,v,TR,3)){ g_launTray=v; SaveConfig(); } }
        if(g_launTray){ bool v=g_launTrayLabels; if(toggle("Names under the icons",nullptr,v)){ g_launTrayLabels=v; SaveConfig(); } }
        { char ln[64]; snprintf(ln,64,"%zu app%s pinned",g_launFavs.size(),g_launFavs.size()==1? "" : "s"); note(ln); }
        { int b=buttons({"Pin an app\xE2\x80\xA6","Clear the tray"},170); if(b==0) LfxPinDialog(); else if(b==1){ g_launFavs.clear(); SaveConfig(); LRebuild(); } }
        my+=6;
        header("Keywords & hotkeys");
        note("Type a keyword in the launcher and press Enter to open its app. Give it a hotkey and it opens from anywhere.");
        LfxSettingsAliases(dl,io,mx,my,rowW,al,e,click);

        my+=10;
        header("Clipboard history");
        note("Every text copy is recorded so you can pull an older one back.");
        note("Ctrl+Alt+V, or >clip in the launcher. Click a row's marker to pin it.");
        note("Nothing leaves this PC, and the list is never written to disk.");
        { bool ce=g_clipEnable;
          if(toggle("Remember what I copy","Records text copies from every app",ce)){
              g_clipEnable=ce;
              if(!ce){ std::lock_guard<std::mutex> lk(g_clipMtx); g_clips.clear(); g_clipFilt.clear(); }
              SaveConfig(); } }
        if(g_clipEnable){
            { bool cp=g_clipPaste;
              if(toggle("Paste after picking","Sends Ctrl+V once the entry is on the clipboard",cp)){ g_clipPaste=cp; SaveConfig(); } }
            float cm=(float)g_clipMax;
            if(slider("Entries kept",cm,10,1000,"%.0f")) g_clipMax=(int)cm;
            if(rel) SaveConfig();
            { size_t n=0,p=0;
              { std::lock_guard<std::mutex> lk(g_clipMtx); n=g_clips.size();
                for(auto&it:g_clips) if(it.pinned) p++; }
              char ln[96]; snprintf(ln,96,"%zu held right now%s",n, p? "" : " ");
              if(p){ snprintf(ln,96,"%zu held right now, %zu pinned",n,p); }
              note(ln); }
            if(buttons({"Forget everything"},190)==0){
                std::lock_guard<std::mutex> lk(g_clipMtx); g_clips.clear(); g_clipFilt.clear(); }
        }
        my+=10;
        header("Extra app folders");
        note("Folders scanned for .exe files, so apps outside the Start Menu show in the launcher.");
        static int appDirSel=-1;
        if(g_launchDirs.empty()) note("None yet \xE2\x80\x94 add a folder to index its programs.");
        for(size_t i=0;i<g_launchDirs.size();i++){
            float h=32; bool hov=rowHit(my,h); bool sel=((int)i==appDirSel);
            if(sel) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-4),AccA((int)(40*e)),8);
            else if(hov) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-4),WithA(COL_INK2,(int)(26*e)),8);
            TextAt(dl,g_fSml,14,V(mx+6,my+7),WithA(sel?COL_GOLD:COL_INK,al),
                   Clip(g_fSml,14,g_launchDirs[i],rowW-20).c_str());
            if(click&&hov) appDirSel=(int)i;
            my+=h;
        }
        my+=6;
        { int b8=buttons({"Add folder\xE2\x80\xA6","Remove selected","Rescan"},160);
          if(b8==0){
              PickFolderAsync(L"Pick a folder of programs to index",[](const std::string& f2){
                  if(std::find(g_launchDirs.begin(),g_launchDirs.end(),f2)==g_launchDirs.end()){
                      g_launchDirs.push_back(f2); SaveConfig(); LScanApps(); } }); }
          else if(b8==1){ if(appDirSel>=0&&appDirSel<(int)g_launchDirs.size()){
              g_launchDirs.erase(g_launchDirs.begin()+appDirSel); appDirSel=-1; SaveConfig(); LScanApps(); } }
          else if(b8==2) LScanApps(); }
        my+=14;
        TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),"Alt+Space opens the launcher.  Type > for commands:"); my+=26;
        for(int i=0;i<NLCMDS && i<7;i++){
            TextAt(dl,g_fSml,13,V(mx+8,my),WithA(COL_GOLD,al),(std::string(">")+LCMDS[i].name).c_str());
            TextAt(dl,g_fSml,13,V(mx+130,my),WithA(COL_INK2,al),LCMDS[i].desc); my+=22; }
    } break;
    case SP_EFFECTS: {   // Plasma's "Desktop Effects" — our own compositor knobs
        header("Motion");
        float am=g_animMul;
        if(slider("Animation speed",am,0.4f,3.0f,"%.2fx")){ g_animMul=am; g_reduceMotion=false; }
        bool rm=g_reduceMotion;
        if(toggle("Reduce motion","Transitions land almost immediately",rm)){
            g_reduceMotion=rm; g_animMul=rm?9.0f:1.0f; SaveConfig(); }
        if(rel) SaveConfig();

        // ---- panel motion: style / duration / closing / overshoot / custom curve / per panel ----

        // a curve preview: the path of the motion over its duration, with a dot that plays it on a loop
        auto curvePreview=[&](int panel){
            MotionSpec mo=MotionResolve(panel,true), mc=MotionResolve(panel,false);
            float h=120, gx0=mx+8, gx1=mx+rowW*0.56f, gy0=my+14, gy1=my+h-18;
            dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h),WithA(COL_INK2,(int)(18*e)),12);
            float base=gy1, top=gy0+18;                               // y=0 at base, y=1 at top (room above for overshoot)
            dl->AddLine(V(gx0,base),V(gx1,base),WithA(COL_INK2,(int)(70*e)),1.0f);
            dl->AddLine(V(gx0,top),V(gx1,top),WithA(COL_INK2,(int)(40*e)),1.0f);
            ImVec2 pts[64];
            for(int i=0;i<64;i++){ float x=(float)i/63.0f;
                float y=Cael::evalC(mo.curve,x,mo.b[0],mo.b[1],mo.b[2],mo.b[3]); if(!g_motionOvershoot) y=std::clamp(y,0.0f,1.0f);
                pts[i]=V(gx0+(gx1-gx0)*x, base-(base-top)*y); }
            dl->AddPolyline(pts,64,A(COL_GOLD),0,2.2f);
            // play: open, hold, close, hold
            float total=(float)(mo.ms+mc.ms)+900.0f;
            float t=fmodf((float)GetTickCount64(),std::max(1.0f,total));
            float prog;
            if(t<mo.ms){ float x=t/std::max(1,mo.ms); prog=Cael::evalC(mo.curve,x,mo.b[0],mo.b[1],mo.b[2],mo.b[3]); }
            else if(t<mo.ms+450.0f) prog=1.0f;
            else if(t<mo.ms+450.0f+mc.ms){ float x=(t-mo.ms-450.0f)/std::max(1,mc.ms); prog=1.0f-Cael::evalC(mc.curve,x,mc.b[0],mc.b[1],mc.b[2],mc.b[3]); }
            else prog=0.0f;
            if(!g_motionOvershoot) prog=std::clamp(prog,0.0f,1.0f);
            // a little panel sliding out of an edge, driven by the same curve
            float px0=mx+rowW*0.64f, px1=mx+rowW-10, pyb=gy1;
            dl->AddRectFilled(V(px0,pyb),V(px1,pyb+4),WithA(COL_INK2,(int)(90*e)),2);
            float ph2=(gy1-gy0-10)*std::max(0.0f,prog);
            if(ph2>1) dl->AddRectFilled(V(px0+18,pyb-ph2),V(px1-18,pyb),A(COL_GOLD),8,ImDrawFlags_RoundCornersTop);
            char info[96]; snprintf(info,96,"open %d ms \xC2\xB7 close %d ms",mo.ms,mc.ms);
            TextAt(dl,g_fSml,12,V(gx0,gy1+2),WithA(COL_INK2,al),info);
            my+=h+10; };

        header("Panel motion");
        note("How the dashboard, launcher, pickers, notifications and every other panel open and close.");
        curvePreview(-1);
        { int v=g_motionStyle; if(cycler("Style",nullptr,v,MOTION_PRETTY,MSTY_N)){ g_motionStyle=v; SaveConfig(); } }
        { float d=(float)g_motionMs;
          char fmt[48]; snprintf(fmt,48,"%s",g_motionMs==0? "style default" : "%.0f ms");
          if(slider("Duration",d,0.0f,1500.0f,fmt)){ g_motionMs=(int)(d/10.0f+0.5f)*10; } }
        { static const char* CP[]={ "Same as opening","Expressive (Caelestia)","Expressive fast","Expressive slow","Emphasized",
                                   "Smooth","Standard","Linear","Bounce","Instant","Custom curve","Strive (slam + hit-stop)" };
          int v=g_motionCloseStyle; if(cycler("Closing style",nullptr,v,CP,12)){ g_motionCloseStyle=v; SaveConfig(); } }
        { float d=(float)g_motionCloseMs;
          if(slider("Closing duration",d,0.0f,1500.0f,g_motionCloseMs==0? "same" : "%.0f ms")){ g_motionCloseMs=(int)(d/10.0f+0.5f)*10; } }
        { bool ov=g_motionOvershoot; if(toggle("Overshoot","Springy styles travel past their size and settle back",ov)){ g_motionOvershoot=ov; SaveConfig(); } }
        { bool anyCustom = g_motionStyle==MSTY_CUSTOM || g_motionCloseStyle==MSTY_CUSTOM+1;
          for(int i=0;i<MP_COUNT;i++) if(g_mpStyle[i]==MSTY_CUSTOM+1) anyCustom=true;
          if(anyCustom){
              static char cbuf[64]={0}; static std::string lastSeen;
              if(lastSeen!=g_motionCustom){ lastSeen=g_motionCustom; strncpy_s(cbuf,g_motionCustom.c_str(),_TRUNCATE); }
              textField("Custom curve  (x1, y1, x2, y2)",cbuf,sizeof(cbuf),9101,false);
              float t4[4];
              if(g_motionCustom!=cbuf && ParseBezier(cbuf,t4)){ g_motionCustom=cbuf; lastSeen=g_motionCustom; SaveConfig(); }
              note("CSS cubic-bezier numbers. y above 1 overshoots. Caelestia: 0.38, 1.21, 0.22, 1.0");
          } }
        if(rel) SaveConfig();
        { static bool perOpen=false;
          section("Per-panel motion",perOpen);
          if(perOpen){
              static const char* PP[]={ "Global","Expressive (Caelestia)","Expressive fast","Expressive slow","Emphasized",
                                        "Smooth","Standard","Linear","Bounce","Instant","Custom curve","Strive (slam + hit-stop)" };
              static int previewPanel=MP_DASHBOARD;
              for(int i=0;i<MP_COUNT;i++){
                  int v=g_mpStyle[i];
                  if(cycler(MP_PRETTY[i],nullptr,v,PP,12)){ g_mpStyle[i]=v; previewPanel=i; SaveConfig(); }
                  if(g_mpStyle[i]>0 || g_mpMs[i]>0){
                      float d=(float)g_mpMs[i];
                      char lb[64]; snprintf(lb,64,"   %s duration",MP_PRETTY[i]);
                      if(slider(lb,d,0.0f,1500.0f,g_mpMs[i]==0? "global" : "%.0f ms")){ g_mpMs[i]=(int)(d/10.0f+0.5f)*10; previewPanel=i; }
                  }
              }
              if(rel) SaveConfig();
              { char lb[80]; snprintf(lb,80,"Preview: %s",MP_PRETTY[previewPanel]); header(lb); }
              curvePreview(previewPanel);
              if(buttons({"Reset all panels to global"},230)==0){ for(int i=0;i<MP_COUNT;i++){ g_mpStyle[i]=0; g_mpMs[i]=0; } SaveConfig(); }
          } }
        header("Guilty Gear Strive");
        note("Fighting-game motion, all optional. Pick \"Strive\" as a panel style above for the slam + hit-stop.");
        { float f=(float)Cael::g_striveFps;
          if(slider("Strive frame rate",f,0.0f,30.0f,Cael::g_striveFps==0? "smooth" : "%.0f fps")) Cael::g_striveFps=(int)(f+0.5f);
          if(rel) SaveConfig(); }
        { bool v=g_stvImpact; if(toggle("Impact frames","Flash, slash and shake when a panel opens",v)){ g_stvImpact=v; SaveConfig(); } }
        if(g_stvImpact){ float k=g_stvImpactStrength; if(slider("Impact strength",k,0.2f,2.0f,"%.1fx")) g_stvImpactStrength=k; if(rel) SaveConfig(); }
        { bool v=g_stvBanner; if(toggle("Workspace banner","A round-call banner slams across the screen on every switch",v)){ g_stvBanner=v; SaveConfig(); } }
        if(g_stvBanner){
            static char bt[64]={0}; static bool btInit=false;
            if(!btInit){ btInit=true; snprintf(bt,sizeof(bt),"%s",g_stvBannerText.c_str()); }
            textField("Banner text (%d = number, %s = name)",bt,sizeof(bt),9101,false);
            if(g_stvBannerText!=bt){ g_stvBannerText=bt; SaveConfig(); } }
        { bool v=g_stvIntroStart; if(toggle("Intro on startup","The fight opening when Aether starts",v)){ g_stvIntroStart=v; SaveConfig(); } }
        { bool v=g_stvIntroUnlock; if(toggle("Intro after unlocking","The fight opening when you get back in",v)){ g_stvIntroUnlock=v; SaveConfig(); } }
        if(g_stvIntroStart || g_stvIntroUnlock){
            static char il[160]={0}; static bool ilInit=false;
            if(!ilInit){ ilInit=true; snprintf(il,sizeof(il),"%s",g_stvIntroLines.c_str()); }
            textField("Intro beats (split with |, %n = count, %u = name)",il,sizeof(il),9102,false);
            if(g_stvIntroLines!=il){ g_stvIntroLines=il; SaveConfig(); } }
        { int b=buttons({"Preview intro","Preview banner"},160);
          if(b==0){ g_setShow=false; StvIntroStart(); }
          else if(b==1){ const WsRing& r=g_wsRing[std::clamp(g_actMon,0,15)]; StvBannerFire(r.cur+1,r.name[std::clamp(r.cur,0,63)]); } }
        header("Depth");
        float ti=g_tiltAmount;
        if(slider("Hover tilt",ti,0.0f,0.5f,"%.2f")) g_tiltAmount=ti;
        bool sh=g_carShadow; if(toggle("Card shadows and vignette",nullptr,sh)){ g_carShadow=sh; SaveConfig(); }
        // slider() formats the RAW value, so a 0.5..1.0 range with "%.0f%%" always printed "1%".
        // Drive it in actual percent instead.
        float op2=g_drawerAlpha*100.0f/255.0f;
        if(slider("Panel opacity",op2,50.0f,100.0f,"%.0f%%")) g_drawerAlpha=(int)(op2*255.0f/100.0f);
        float rr2=g_rounding;
        if(slider("Corner rounding",rr2,0.4f,1.8f,"%.2f")){ g_rounding=rr2; g_cardRound=16*rr2; g_panelRound=22*rr2; }
        if(rel) SaveConfig();
        header("Windows system effects");
        note("These are Windows' own switches, mirrored here because Settings is awkward to reach "
             "once Aether stands in for explorer.exe.");
        bool wa=WinAnimations();
        if(toggle("Animation effects","Windows' own window and control animations "
                  "(Settings > Accessibility > Visual effects)",wa)) SetWinAnimations(wa);
        bool wt=WinTransparency();
        if(toggle("Transparency effects","Acrylic and Mica in Windows apps and the taskbar "
                  "(Settings > Personalization > Colors)",wt)) SetWinTransparency(wt);
        bool dw=WmGetBool(SPI_GETDRAGFULLWINDOWS);
        if(toggle("Show window contents while dragging",nullptr,dw)) WmSetUi(SPI_SETDRAGFULLWINDOWS,dw);
        note("Aether's own motion uses the speed multiplier above; its panel translucency is "
             "\"Panel opacity\", which stays independent of the Windows switch.");
    } break;
    case SP_WORKSPACES: {   // virtual desktops
        RefreshWorkspaces();
        char ws[96]; snprintf(ws,96,"%d workspace%s \xE2\x80\xA2 currently on %d",g_wsCount,g_wsCount==1?"":"s",g_wsCur+1);
        header(ws);
        { float chipW=(rowW-(g_wsCount-1)*8.0f)/std::max(1,g_wsCount); chipW=std::min(chipW,96.0f);
          for(int i=0;i<g_wsCount;i++){
              float cx2=mx+i*(chipW+8);
              bool hov=io.MousePos.x>cx2&&io.MousePos.x<cx2+chipW&&io.MousePos.y>my&&io.MousePos.y<my+48;
              bool sel=(i==g_wsCur); float ha=HoverAnim(idBase+40+i,hov&&!sel);
              dl->AddRectFilled(V(cx2,my),V(cx2+chipW,my+48),sel?AccA((int)(210*e)):WithA(COL_INK2,(int)((28+ha*40)*e)),10);
              ImU32 fc=sel?(g_darkUI?IM_COL32(12,20,14,255):IM_COL32(250,254,252,255)):COL_INK;
              char n2[8]; snprintf(n2,8,"%d",i+1);
              TextAt(dl,g_fMed,20,V(cx2+chipW/2-TextW(g_fMed,20,n2)/2,my+13),WithA(fc,al),n2);
              if(click&&hov&&!sel) SwitchWorkspace(i);
          }
          my+=60; }
        int b=buttons({"New workspace","Close current"},150);
        if(b==0){   // Ctrl+Win+D / Ctrl+Win+F4 are the public shortcuts
            INPUT in[6]={}; for(auto&i2:in) i2.type=INPUT_KEYBOARD;
            in[0].ki.wVk=VK_LWIN; in[1].ki.wVk=VK_CONTROL; in[2].ki.wVk='D';
            in[3].ki.wVk='D'; in[3].ki.dwFlags=KEYEVENTF_KEYUP;
            in[4].ki.wVk=VK_CONTROL; in[4].ki.dwFlags=KEYEVENTF_KEYUP;
            in[5].ki.wVk=VK_LWIN; in[5].ki.dwFlags=KEYEVENTF_KEYUP;
            SendInput(6,in,sizeof(INPUT)); }   // no Sleep here (render thread): the hook predicts it and the 250 ms refresh confirms it
        else if(b==1){
            INPUT in[6]={}; for(auto&i2:in) i2.type=INPUT_KEYBOARD;
            in[0].ki.wVk=VK_LWIN; in[1].ki.wVk=VK_CONTROL; in[2].ki.wVk=VK_F4;
            in[3].ki.wVk=VK_F4; in[3].ki.dwFlags=KEYEVENTF_KEYUP;
            in[4].ki.wVk=VK_CONTROL; in[4].ki.dwFlags=KEYEVENTF_KEYUP;
            in[5].ki.wVk=VK_LWIN; in[5].ki.dwFlags=KEYEVENTF_KEYUP;
            SendInput(6,in,sizeof(INPUT)); }   // no Sleep here (render thread): the hook predicts it and the 250 ms refresh confirms it
        note("Workspaces are Windows' virtual desktops, driven through the public shortcuts.");
        note("The bar's workspace pips switch between them too.");
    } break;
    case SP_TUNEUP: {   // the Quick Fixes half of Z:\PCTuneUp (its Startup Manager is the Autostart page)
        note("Reversible maintenance. Nothing here uninstalls anything or edits the registry outside "
             "your own user, and every action says what it did.");
        my+=6;
        header("Displays");
        note("Sets every attached screen to its highest advertised refresh rate.");
        if(buttons({"Set all displays to max Hz"},240)==0){
            int changed=0, total=0;
            DISPLAY_DEVICEW dd{sizeof(dd)};
            for(DWORD di=0; EnumDisplayDevicesW(nullptr,di,&dd,0); di++, dd={sizeof(dd)}){
                if(!(dd.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
                total++;
                DEVMODEW cur{}; cur.dmSize=sizeof(DEVMODEW);
                if(!EnumDisplaySettingsW(dd.DeviceName,ENUM_CURRENT_SETTINGS,&cur)) continue;
                DWORD best=cur.dmDisplayFrequency;
                DEVMODEW m{}; m.dmSize=sizeof(DEVMODEW);
                for(DWORD i=0; EnumDisplaySettingsW(dd.DeviceName,i,&m); i++, m={}, m.dmSize=sizeof(DEVMODEW))
                    if(m.dmPelsWidth==cur.dmPelsWidth && m.dmPelsHeight==cur.dmPelsHeight &&
                       m.dmBitsPerPel==cur.dmBitsPerPel && m.dmDisplayFrequency>best) best=m.dmDisplayFrequency;
                if(best>cur.dmDisplayFrequency){
                    DEVMODEW set=cur; set.dmDisplayFrequency=best; set.dmFields=DM_DISPLAYFREQUENCY;
                    if(ChangeDisplaySettingsExW(dd.DeviceName,&set,nullptr,CDS_UPDATEREGISTRY,nullptr)==DISP_CHANGE_SUCCESSFUL) changed++;
                }
            }
            char b[120]; snprintf(b,120,"%d of %d display%s raised",changed,total,total==1?"":"s");
            g_tuneMsg=b;
        }
        my+=8;
        header("Disk");
        note("Temp files are deleted only where they are yours to delete; anything a running program "
             "still holds open is skipped.");
        { int b2=buttons({"Clean temp files","Empty the recycle bin","Disk Cleanup"},175);
          if(b2==0){
              wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH,tmp);
              long long freed=0; int gone=0;
              std::wstring pat=std::wstring(tmp)+L"*";
              WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW(pat.c_str(),&fd);
              if(h!=INVALID_HANDLE_VALUE){ do{
                  if(!wcscmp(fd.cFileName,L".")||!wcscmp(fd.cFileName,L"..")) continue;
                  if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;   // files only: safe and enough
                  std::wstring f=std::wstring(tmp)+fd.cFileName;
                  long long sz=((long long)fd.nFileSizeHigh<<32)|fd.nFileSizeLow;
                  if(DeleteFileW(f.c_str())){ freed+=sz; gone++; }
              }while(FindNextFileW(h,&fd)); FindClose(h); }
              char b[140]; snprintf(b,140,"%d temp file%s removed, %.1f MB freed",gone,gone==1?"":"s",freed/1048576.0);
              g_tuneMsg=b;
          }
          else if(b2==1){ SHEmptyRecycleBinW(nullptr,nullptr,SHERB_NOCONFIRMATION|SHERB_NOPROGRESSUI|SHERB_NOSOUND);
                          g_tuneMsg="Recycle bin emptied"; }
          else if(b2==2){ AetherShellExec(nullptr,L"open",L"cleanmgr.exe",nullptr,nullptr,SW_SHOWNORMAL);
                          g_tuneMsg="Disk Cleanup opened"; } }
        my+=8;
        header("Shell and background apps");
        note("Restarting Explorer is safe while Aether is your shell \xE2\x80\x94 it only owns the file "
             "windows and the tray host, both of which come straight back.");
        { int b3=buttons({"Restart Explorer","Close duplicate chat clients"},210);
          if(b3==0){
              HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0); int n=0;
              if(snap!=INVALID_HANDLE_VALUE){ PROCESSENTRY32W pe{sizeof(pe)};
                  if(Process32FirstW(snap,&pe)) do{
                      if(_wcsicmp(pe.szExeFile,L"explorer.exe")==0)
                          if(HANDLE ph=OpenProcess(PROCESS_TERMINATE,FALSE,pe.th32ProcessID)){
                              if(TerminateProcess(ph,0)) n++; CloseHandle(ph); }
                  }while(Process32NextW(snap,&pe));
                  CloseHandle(snap); }
              char b[80]; snprintf(b,80,"%d Explorer process%s restarted",n,n==1?"":"es"); g_tuneMsg=b;
          } else if(b3==1){
              // Only EXTRA copies are closed: the newest instance of each name is left running, so a
              // chat client you are actually using never disappears from under you.
              static const wchar_t* chat[]={L"Discord.exe",L"DiscordPTB.exe",L"DiscordCanary.exe",
                                            L"Vesktop.exe",L"Foracord.exe",L"SwagCord.exe"};
              int closed=0;
              for(const wchar_t* nm:chat){
                  std::vector<DWORD> pids;
                  HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
                  if(snap!=INVALID_HANDLE_VALUE){ PROCESSENTRY32W pe{sizeof(pe)};
                      if(Process32FirstW(snap,&pe)) do{
                          if(_wcsicmp(pe.szExeFile,nm)==0) pids.push_back(pe.th32ProcessID);
                      }while(Process32NextW(snap,&pe));
                      CloseHandle(snap); }
                  // a chat client is many processes (one per renderer), so this is a blunt tool -
                  // it only fires when there is more than one TOP-LEVEL window for that name
                  if(pids.size()>1){
                      for(size_t k=0;k+1<pids.size();k++)
                          if(HANDLE ph=OpenProcess(PROCESS_TERMINATE,FALSE,pids[k])){
                              if(TerminateProcess(ph,0)) closed++; CloseHandle(ph); } }
              }
              char b[90]; snprintf(b,90,"%d duplicate process%s closed",closed,closed==1?"":"es"); g_tuneMsg=b;
          } }
        my+=8;
        header("Power");
        { if(!g_plansRead) RefreshPlans();
          std::string pn = (g_planCur>=0&&g_planCur<(int)g_plans.size())? g_plans[g_planCur].name : std::string("(unknown)");
          char pl[200]; snprintf(pl,200,"Active plan \xE2\x80\xA2 %s",pn.c_str());
          TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),pl); my+=24; }
        if(buttons({"Open Windows power settings"},240)==0)
            AetherShellExec(nullptr,L"open",L"ms-settings:powersleep",nullptr,nullptr,SW_SHOWNORMAL);
        my+=10;
        if(!g_tuneMsg.empty()){
            dl->AddRectFilled(V(mx-8,my-4),V(mx+rowW,my+28),AccA((int)(40*e)),9);
            TextAt(dl,g_fSml,14,V(mx+6,my+4),WithA(COL_INK,al),g_tuneMsg.c_str()); my+=36; }
    } break;
    case SP_MACROS: {   // Z:\MacroMaker, native
        if(g_macros.empty()) MacroLoadPresets();
        // The arm switch is deliberately the FIRST thing and deliberately off by default. This page
        // synthesises real keyboard and mouse input; it should never be ambiguous whether it is live.
        { bool ar=g_macroArmed;
          if(toggle("Armed","Master switch \xE2\x80\x94 F8 toggles this from anywhere. Off, no macro can fire.",ar)){
              g_macroArmed=ar; if(!ar) MacroStopAll(); g_macroLog = ar?"Armed":"Disarmed"; } }
        note("Macros send ordinary Windows input (SendInput). Some games' anti-cheat treats "
             "synthesised input as a violation \xE2\x80\x94 that is your call to make, per game.");
        if(!g_macroLog.empty()){
            char lg[160]; snprintf(lg,160,"%s",g_macroLog.c_str());
            TextAt(dl,g_fSml,13,V(mx,my),WithA(g_macroArmed?COL_GOLD:COL_INK2,al),lg); my+=24; }
        my+=6;
        header("Macros");
        for(size_t i=0;i<g_macros.size();i++){
            Macro& m=g_macros[i];
            float h=64; bool hov=rowHit(my,h);
            float ha=HoverAnim(idBase+900+(int)i,hov);
            bool live=(g_macroRunning.load()==(int)i);
            if(live)         dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-6),AccA((int)(46*e)),10);
            else if(ha>0.01f)dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-6),WithA(COL_INK2,(int)(ha*30*e)),10);
            TextAt(dl,g_fMed,16,V(mx+2,my+6),WithA(m.enabled?COL_INK:COL_INK2,al),m.name.c_str());
            { static const char* MODE[3]={"press","hold","toggle"};
              char sub[200]; snprintf(sub,200,"%s \xE2\x80\xA2 %s \xE2\x80\xA2 %d ms \xE2\x80\xA2 %d step%s%s",
                       MacroKeyName(m.trigger).c_str(), MODE[std::clamp(m.mode,0,2)], m.interval,
                       (int)m.steps.size(), m.steps.size()==1?"":"s", m.suppress?" \xE2\x80\xA2 suppressed":"");
              TextAt(dl,g_fSml,12,V(mx+2,my+28),WithA(COL_INK2,al),sub); }
            TextAt(dl,g_fSml,11,V(mx+2,my+45),WithA(COL_INK2,(int)(al*0.75f)),m.desc.c_str());
            // enable pill on the right
            { float tw=44,th=24; ImVec2 t0=V(mx+rowW-tw-4,my+16),t1=V(t0.x+tw,t0.y+th);
              bool th2=io.MousePos.x>t0.x&&io.MousePos.x<t1.x&&io.MousePos.y>t0.y&&io.MousePos.y<t1.y;
              dl->AddRectFilled(t0,t1,m.enabled?A(COL_GOLD):WithA(COL_TRACK,al),th*0.5f);
              float kx=m.enabled? t1.x-th*0.5f : t0.x+th*0.5f;
              dl->AddCircleFilled(V(kx,t0.y+th*0.5f),th*0.5f-3,WithA(IM_COL32(252,254,253,255),al));
              if(click&&th2){ m.enabled=!m.enabled; SaveConfig(); } }
            my+=h;
        }
        my+=8;
        // The Python MacroMaker only exists on the machine Aether was written on: the button is offered when it
        // is there (checked once - a missing drive can take a while to answer) instead of opening nothing.
        { static const bool hasPy=GetFileAttributesW(L"Z:\\MacroMaker")!=INVALID_FILE_ATTRIBUTES;
          int b4=hasPy? buttons({"Reset to the preset library","Open the Python build"},220) : buttons({"Reset to the preset library"},220);
          if(b4==0){ MacroLoadPresets(); SaveConfig(); g_macroLog="Presets restored"; }
          else if(b4==1) AetherShellExec(nullptr,L"open",L"Z:\\MacroMaker",nullptr,nullptr,SW_SHOWNORMAL); }
        note("Binds are the original's Fortnite defaults \xE2\x80\x94 edit macros.* in config.json to "
             "re-bind them to your own keys. Step editing lives in the Python build for now.");
    } break;
    case SP_WINDOWS: {   // Plasma's "Window Behavior", mapped to the Windows knobs
        header("Corners");
        { bool rc=g_roundWindows;
          if(toggle("Round app-window corners","Uses DWM's own rounding â antialiased, and it never clips the window",rc)){
              g_roundWindows=rc; SaveConfig(); SweepWindowRounds(); } }
        { bool dc=g_deepCorners;
          if(toggle("Deeper corners (clips the window)",
                    "Matches the desktop bubble's radius by clipping each window to a rounded region. A region has no antialiasing, and while a window is being resized it can briefly show cut-off edges.",dc)){
              g_deepCorners=dc; SaveConfig(); SyncDeepCornerHook();
              if(!g_deepCorners) ClearAllWindowRegions(); else SweepWindowRegions(); } }
        if(g_deepCorners){ float cr=(float)g_winRoundPx;
          if(slider("Corner radius",cr,0,64,"%.0f px")){ g_winRoundPx=(int)cr; }
          if(rel){ SaveConfig(); SweepWindowRegions(); } }
        my+=8;
        header("Decorations");
        { bool d2=g_decoOn;
          if(toggle("Linux-style title bars","A GNOME headerbar on the focused window: centred title + min/max/close",d2)){
              g_decoOn=d2; SaveConfig(); } }
        note("Decorates the window you're using; drag the bar to move it, double-click to maximize.");
        my+=8;
        header("Window switcher");
        { bool at=g_swEnable;
          if(toggle("Linux-style Alt+Tab","A GNOME-style row of app icons instead of Windows' switcher",at)){
              g_swEnable=at; SaveConfig(); } }
        note("Hold Alt and tap Tab to step forward, Shift+Tab back; arrows work too, Esc cancels. "
             "Turn this off to get Windows' own switcher back.");
        if(g_swEnable){
            static const char* SWSTYLES[2]={"Icon row (GNOME)","Coverflow carousel"};
            int sp2=choice(SWSTYLES,2,std::clamp(g_swStyle,0,1));
            if(sp2>=0 && sp2!=g_swStyle){ g_swStyle=sp2; SaveConfig(); }
            if(g_swStyle==SWSTYLE_COVER){
                bool rf=g_swReflect;
                if(toggle("Reflections","Mirror each card under the carousel",rf)){ g_swReflect=rf; SaveConfig(); }
            }
        }
        my+=8;
        header("Snapping and arrangement");
        bool wa=WmGetBool(SPI_GETWINARRANGING);
        if(toggle("Snap windows to the screen edges","Windows' Snap Assist",wa)) WmSetPv(SPI_SETWINARRANGING,wa);
        bool sm=WmGetBool(SPI_GETSNAPSIZING);
        if(toggle("Resize the neighbour when snapping",nullptr,sm)) WmSetPv(SPI_SETSNAPSIZING,sm);
        bool dm=WmGetBool(SPI_GETDOCKMOVING);
        if(toggle("Maximize by dragging to the top edge",nullptr,dm)) WmSetPv(SPI_SETDOCKMOVING,dm);
        header("Focus");
        bool af=WmGetBool(SPI_GETACTIVEWINDOWTRACKING);
        if(toggle("Focus follows the mouse","Plasma's focus-follows-mouse policy",af)) WmSetPv(SPI_SETACTIVEWINDOWTRACKING,af);
        bool ar=WmGetBool(SPI_GETACTIVEWNDTRKZORDER);
        if(toggle("Raise the window that gains focus",nullptr,ar)) WmSetPv(SPI_SETACTIVEWNDTRKZORDER,ar);
        { DWORD d2=0; SystemParametersInfoW(SPI_GETACTIVEWNDTRKTIMEOUT,0,&d2,0);
          float ft=(float)d2; if(slider("Focus delay",ft,0,1000,"%.0f ms"))
              SystemParametersInfoW(SPI_SETACTIVEWNDTRKTIMEOUT,0,(void*)(INT_PTR)(DWORD)ft,SPIF_SENDCHANGE); }
        header("Shell");
        bool fs=g_hideOnFullscreen;
        if(toggle("Hide the shell over fullscreen apps","Games and video get the whole screen",fs)){ g_hideOnFullscreen=fs; SaveConfig(); }
        note("Window rules and KWin scripts have no Windows equivalent; snapping is the DWM's.");
    } break;
    case SP_SHORTCUTS: {   // the global keys the shell claims
        header("Shell shortcuts \xE2\x80\x94 click one to set it to whatever you like");
        for(int i=0;i<HK_COUNT;i++){
            Hotkey& hk=g_hk[i];
            bool capturing=(g_hkCapture==i);
            float h=52; bool hov=rowHit(my,h); float ha=HoverAnim(idBase+60+i,hov);
            if(capturing) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-6),AccA((int)(46*e)),10);
            else if(ha>0.01f) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-6),WithA(COL_INK2,(int)(ha*28*e)),10);
            TextAt(dl,g_fMed,16,V(mx+4,my+6),WithA(COL_INK,al),hk.label);
            std::string keys = capturing? std::string("Press the keys\xE2\x80\xA6") : HotkeyName(hk.mods,hk.vk);
            ImU32 kc = capturing? COL_GOLD : (hk.vk && !hk.ok)? IM_COL32(226,110,100,255) : COL_GOLD;
            TextAt(dl,g_fMed,15,V(mx+rowW-TextW(g_fMed,15,keys.c_str())-8,my+7),WithA(kc,al),keys.c_str());
            const char* st = !hk.vk? "disabled"
                           : hk.ok? "working"
                                  : "another app already owns this combination \xE2\x80\x94 pick a different one";
            TextAt(dl,g_fSml,12,V(mx+4,my+28),WithA(hk.ok||!hk.vk?COL_INK2:IM_COL32(226,110,100,255),al),st);
            if(click&&hov&&!capturing){ g_hkCapture=i; g_hkLast=i; g_hkCaptureStart=GetTickCount64();
                for(int k2=0;k2<HK_COUNT;k2++) UnregisterHotKey(g_hwnd,k2+1); }   // free the keys to be typed
            my+=h;
        }
        // capture: read the keyboard directly, so ANY key on ANY layout can be bound
        if(g_hkCapture>=0){
            UINT mods=0;
            if(GetAsyncKeyState(VK_CONTROL)&0x8000) mods|=MOD_CONTROL;
            if(GetAsyncKeyState(VK_MENU)   &0x8000) mods|=MOD_ALT;
            if(GetAsyncKeyState(VK_SHIFT)  &0x8000) mods|=MOD_SHIFT;
            if((GetAsyncKeyState(VK_LWIN)&0x8000)||(GetAsyncKeyState(VK_RWIN)&0x8000)) mods|=MOD_WIN;
            UINT got=0;
            for(UINT vk=0x08; vk<=0xFE; vk++){
                if(vk==VK_CONTROL||vk==VK_MENU||vk==VK_SHIFT||vk==VK_LWIN||vk==VK_RWIN) continue;
                if(vk==VK_LCONTROL||vk==VK_RCONTROL||vk==VK_LMENU||vk==VK_RMENU||vk==VK_LSHIFT||vk==VK_RSHIFT) continue;
                if(vk==VK_LBUTTON||vk==VK_RBUTTON||vk==VK_MBUTTON) continue;
                if(GetAsyncKeyState(vk)&0x8000){ got=vk; break; }
            }
            if(got==VK_ESCAPE){ g_hkCapture=-1; RegisterHotkeys(); }
            else if(got){
                Hotkey& hk=g_hk[g_hkCapture];
                hk.mods = mods | (g_hkCapture==HK_LAUNCHER? MOD_NOREPEAT : 0u);
                hk.vk   = got;
                g_hkCapture=-1; RegisterHotkeys(); SaveConfig();
            } else if(GetTickCount64()-g_hkCaptureStart>8000){ g_hkCapture=-1; RegisterHotkeys(); }
        }
        my+=6;
        { int b7=buttons({"Clear selected","Reset to defaults"},170);
          if(b7==0 && g_hkLast>=0){ g_hk[g_hkLast].vk=0; RegisterHotkeys(); SaveConfig(); }
          else if(b7==1){
              g_hk[HK_QUIT].mods=MOD_CONTROL|MOD_ALT;              g_hk[HK_QUIT].vk='Q';
              g_hk[HK_LAUNCHER].mods=MOD_ALT|MOD_NOREPEAT;         g_hk[HK_LAUNCHER].vk=VK_SPACE;
              g_hk[HK_WALLPREV].mods=MOD_CONTROL|MOD_ALT;          g_hk[HK_WALLPREV].vk=VK_OEM_4;
              g_hk[HK_WALLNEXT].mods=MOD_CONTROL|MOD_ALT;          g_hk[HK_WALLNEXT].vk=VK_OEM_6;
              RegisterHotkeys(); SaveConfig(); } }
        my+=4;
        { bool wk=g_winKeyLauncher;
          if(toggle("Super key opens the launcher","Tapping Windows on its own; combinations stay Windows'",wk)){
              g_winKeyLauncher=wk; SaveConfig(); } }
        note("Escape cancels a capture. A shortcut Windows refuses is shown in red \xE2\x80\x94 it belongs to another app.");
        my+=12;
        header("Fixed gestures");
        struct SC{ const char* k; const char* d; };
        static const SC SCS[]={
            {"> in the launcher",    "Launcher command mode"},
            {"Super + Ctrl + \xE2\x86\x90 / \xE2\x86\x92", "Previous / next workspace"},
            {"Super + Ctrl + D",     "New workspace"},
            {"Super + Ctrl + F4",    "Close the current workspace"},
            {"Super + Alt + R",      "Start / stop the screen recorder"},
            {"Touch the top edge",   "Reveal the dashboard drawer"},
            {"Touch the left edge",  "Reveal the taskbar"},
            {"Touch the right edge", "Reveal quick settings"},
            {"Escape",               "Close the focused shell panel"},
        };
        for(const SC& s2:SCS){
            float h=30; bool hov=rowHit(my,h);
            if(hov) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-3),WithA(COL_INK2,(int)(24*e)),8);
            TextAt(dl,g_fMed,14,V(mx+4,my+7),WithA(COL_GOLD,al),s2.k);
            TextAt(dl,g_fSml,13,V(mx+200,my+8),WithA(COL_INK2,al),s2.d);
            my+=h; }
        my+=10;
        note("Edge reveals are configurable per panel on the Taskbar and Dashboard pages.");
    } break;
    case SP_INPUT: {   // Plasma's "Input Devices"
        header("Mouse");
        { float sp=(float)InMouseSpeed();
          if(slider("Pointer speed",sp,1,20,"%.0f / 20")) InSetMouseSpeed((int)sp);
          bool ac=InMouseAccel();
          if(toggle("Pointer acceleration","Windows' \"enhance pointer precision\"",ac)) InSetMouseAccel(ac);
          bool sb=GetSystemMetrics(SM_SWAPBUTTON)!=0;
          if(toggle("Left-handed buttons","Swap the primary and secondary button",sb)) SwapMouseButton(sb?TRUE:FALSE);
          float sl=(float)InScrollLines();
          if(slider("Scroll lines per notch",sl,1,20,"%.0f lines")) InSetScrollLines((int)sl);
          float dc=(float)GetDoubleClickTime();
          if(slider("Double-click interval",dc,150,900,"%.0f ms")) SetDoubleClickTime((UINT)dc); }
        header("Keyboard");
        { float kr=(float)InKbRepeat();
          if(slider("Repeat rate",kr,0,31,"%.0f / 31")) InSetKbRepeat((int)kr);
          float kd=(float)InKbDelay();
          if(slider("Repeat delay",kd,0,3,"%.0f / 3")) InSetKbDelay((int)kd);
          float cb=(float)GetCaretBlinkTime(); if(cb>2000) cb=1200;
          if(slider("Cursor blink",cb,200,2000,"%.0f ms")) SetCaretBlinkTime((UINT)cb); }
        header("Layout");
        { wchar_t lay[KL_NAMELENGTH]={0}; GetKeyboardLayoutNameW(lay);
          kv("Active layout", LocaleStr(LOCALE_SLOCALIZEDDISPLAYNAME));
          kv("Layout id", W2U8(lay));
          HKL hkls[16]; int nh=GetKeyboardLayoutList(16,hkls);
          char nb[48]; snprintf(nb,48,"%d installed",nh); kv("Layouts",nb); }
        if(buttons({"Windows keyboard settings"},210)==0)
            AetherShellExec(nullptr,L"open",L"ms-settings:keyboard",nullptr,nullptr,SW_SHOWNORMAL);
    } break;
    case SP_POWER: {   // Plasma's "Power Management"
        if(!g_plansRead) RefreshPlans();
        SYSTEM_POWER_STATUS sps{}; GetSystemPowerStatus(&sps);
        if(sps.BatteryFlag!=128 && sps.BatteryFlag!=255){
            char rt[64]="";
            if(sps.BatteryLifeTime!=(DWORD)-1) snprintf(rt,64,"%luh %02lum remaining",sps.BatteryLifeTime/3600,(sps.BatteryLifeTime%3600)/60);
            else snprintf(rt,64,"%s",sps.ACLineStatus==1?"On AC power":"On battery");
            char pc[24]; snprintf(pc,24,"%d%%",(int)sps.BatteryLifePercent);
            meter(std::string("Battery  ")+pc,rt,sps.BatteryLifePercent/100.0f,
                  sps.BatteryLifePercent<20?COL_ERR:COL_GOLD);
        } else {
            kv("Power source", sps.ACLineStatus==1?"AC (no battery detected)":"Unknown");
            my+=8;
        }
        header("Power plan");
        { std::vector<const char*> names; for(auto& p2:g_plans) names.push_back(p2.name.c_str());
          if(!names.empty()){
              int pick=choice(names.data(),(int)names.size(),g_planCur);
              if(pick>=0){ PowerSetActiveScheme(nullptr,&g_plans[pick].g); RefreshPlans(); } }
          else note("No power schemes reported."); }
        my+=8;
        header("Turn off after (plugged in)");
        { float vs=(float)PowerGet(PS_SUB_VIDEO,PS_VIDEOIDLE,true)/60.0f;
          if(slider("Screen off",vs,0,120,"%.0f min")) PowerSet(PS_SUB_VIDEO,PS_VIDEOIDLE,true,(DWORD)(vs*60));
          float ss=(float)PowerGet(PS_SUB_SLEEP,PS_STANDBY,true)/60.0f;
          if(slider("Sleep",ss,0,240,"%.0f min")) PowerSet(PS_SUB_SLEEP,PS_STANDBY,true,(DWORD)(ss*60)); }
        note("0 = never. These write the active scheme, exactly as powercfg would.");
        my+=6;
        bool ka=g_keepAwake;
        if(toggle("Keep the system awake","Blocks sleep and the screen blanking while set",ka)){ g_keepAwake=ka; ApplyKeepAwake(); }
        if(buttons({"Sleep now","Windows power settings"},170)==0) DoSleep();
        else if(io.MouseClicked[0]&&false) {}
    } break;
    case SP_STORAGE: {   // Plasma's "Removable Storage" + disk usage
        if(GetTickCount64()-g_drivesAt>4000) RefreshDrives();
        for(size_t i=0;i<g_drives.size();i++){
            DriveInfo& d=g_drives[i];
            auto gb=[&](unsigned long long b){ char t[32]; double g=b/1073741824.0;
                if(g>=1024) snprintf(t,32,"%.2f TB",g/1024); else snprintf(t,32,"%.0f GB",g); return std::string(t); };
            std::string right=gb(d.used)+" of "+gb(d.total)+" used";
            const char* kind = d.type==DRIVE_REMOVABLE?"Removable": d.type==DRIVE_REMOTE?"Network":"Internal";
            float frac=(float)((double)d.used/(double)d.total);
            meter(d.letter+"  "+d.label+"   \xE2\x80\xA2  "+kind, right, frac, frac>0.9f?COL_ERR:COL_GOLD);
            my-=12;
            float bw2=88, bx5=mx+rowW-bw2*(d.type==DRIVE_REMOVABLE?2:1)-(d.type==DRIVE_REMOVABLE?8:0);
            if(d.type==DRIVE_REMOVABLE){
                bool hov=io.MousePos.x>bx5&&io.MousePos.x<bx5+bw2&&io.MousePos.y>my&&io.MousePos.y<my+26;
                dl->AddRectFilled(V(bx5,my),V(bx5+bw2,my+26),WithA(COL_INK2,(int)((28+HoverAnim(idBase+50+(int)i,hov)*44)*e)),8);
                TextAt(dl,g_fSml,12,V(bx5+bw2/2-TextW(g_fSml,12,"Eject")/2,my+6),WithA(COL_INK,al),"Eject");
                if(click&&hov) EjectDrive(d.letter);
                bx5+=bw2+8; }
            { bool hov=io.MousePos.x>bx5&&io.MousePos.x<bx5+bw2&&io.MousePos.y>my&&io.MousePos.y<my+26;
              dl->AddRectFilled(V(bx5,my),V(bx5+bw2,my+26),WithA(COL_INK2,(int)((28+HoverAnim(idBase+70+(int)i,hov)*44)*e)),8);
              TextAt(dl,g_fSml,12,V(bx5+bw2/2-TextW(g_fSml,12,"Open")/2,my+6),WithA(COL_INK,al),"Open");
              if(click&&hov){ std::wstring r=U82W(d.letter)+L"\\"; AetherShellExec(nullptr,L"open",r.c_str(),nullptr,nullptr,SW_SHOWNORMAL); } }
            my+=36;
        }
        if(g_drives.empty()) note("No drives reported.");
        if(buttons({"Rescan","Disk cleanup"},130)==0) RefreshDrives();
    } break;
    case SP_PRINTERS: {
        if(!g_printersRead) RefreshPrinters();
        if(g_printers.empty()) note("No printers installed.");
        for(size_t i=0;i<g_printers.size();i++){
            PrinterInfo& p2=g_printers[i]; float h=52; bool hov=rowHit(my,h);
            float ha=HoverAnim(idBase+40+(int)i,hov);
            if(p2.isDefault) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-6),AccA((int)(44*e)),10);
            else if(ha>0.01f) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-6),WithA(COL_INK2,(int)(ha*32*e)),10);
            SetRailIcon(dl,SP_PRINTERS,V(mx+14,my+22),WithA(p2.isDefault?COL_GOLD:COL_INK2,al));
            TextAt(dl,g_fMed,16,V(mx+36,my+6),WithA(p2.isDefault?COL_GOLD:COL_INK,al),p2.name.c_str());
            std::string sub=p2.status+"   \xE2\x80\xA2   "+p2.port;
            TextAt(dl,g_fSml,12,V(mx+36,my+27),WithA(COL_INK2,al),sub.c_str());
            if(p2.isDefault) TextAt(dl,g_fSml,11,V(mx+rowW-52,my+9),WithA(COL_GOLD,al),"default");
            if(click&&hov&&!p2.isDefault){ SetDefaultPrinterW(U82W(p2.name).c_str()); RefreshPrinters(); }
            my+=h; }
        my+=6;
        int b2=buttons({"Refresh","Add a printer","Print queue"},140);
        if(b2==0) RefreshPrinters();
        else if(b2==1) AetherShellExec(nullptr,L"open",L"ms-settings:printers",nullptr,nullptr,SW_SHOWNORMAL);
        else if(b2==2&&!g_printers.empty()){
            std::wstring c2=L"printui.dll,PrintUIEntry /o /n \""+U82W(g_printers[0].name)+L"\"";
            AetherShellExec(nullptr,L"open",L"rundll32.exe",c2.c_str(),nullptr,SW_SHOWNORMAL); }
        note("Click a printer to make it the default.");
    } break;
    case SP_STARTUP: {   // Plasma's "Autostart"
        if(!g_startupRead) RefreshStartup();
        bool sl=ShellRunsAtLogin();
        if(toggle("Start Aether at sign-in","Adds the shell to the per-user Run key",sl)) SetShellRunsAtLogin(sl);
        my+=6;
        header("Programs that run at sign-in");
        for(size_t i=0;i<g_startup.size();i++){
            StartupItem& it=g_startup[i]; float h=48; bool hov=rowHit(my,h);
            float ha=HoverAnim(idBase+40+(int)i,hov);
            if(ha>0.01f) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-6),WithA(COL_INK2,(int)(ha*30*e)),10);
            TextAt(dl,g_fMed,15,V(mx+2,my+5),WithA(it.enabled?COL_INK:COL_INK2,al),it.name.c_str());
            std::string cmd=it.cmd; while(!cmd.empty()&&TextW(g_fSml,11,cmd.c_str())>rowW-90) cmd.pop_back();
            TextAt(dl,g_fSml,11,V(mx+2,my+25),WithA(COL_INK2,al),cmd.c_str());
            if(it.machine){
                TextAt(dl,g_fSml,11,V(mx+rowW-58,my+14),WithA(COL_INK2,al),"system");
            } else {
                float sw2=38,sh2=20, sx2=mx+rowW-sw2-2, sy2=my+10;
                dl->AddRectFilled(V(sx2,sy2),V(sx2+sw2,sy2+sh2),it.enabled?A(COL_GOLD):A(COL_TRACK),sh2*0.5f);
                dl->AddCircleFilled(V(it.enabled?sx2+sw2-sh2*0.5f:sx2+sh2*0.5f,sy2+sh2*0.5f),sh2*0.5f-3,IM_COL32(252,254,253,al));
                if(click&&hov){ StartupSetEnabled(it,!it.enabled); break; }
            }
            my+=h; }
        if(g_startup.empty()) note("Nothing registered in the Run keys.");
        my+=6;
        int b3=buttons({"Refresh","Startup folder","Task Manager"},150);
        if(b3==0) RefreshStartup();
        else if(b3==1) AetherShellExec(nullptr,L"open",L"shell:startup",nullptr,nullptr,SW_SHOWNORMAL);
        else if(b3==2) AetherShellExec(nullptr,L"open",L"taskmgr.exe",nullptr,nullptr,SW_SHOWNORMAL);
        note("Disabling parks the entry under HKCU\\Software\\Aether\\RunDisabled, so it restores exactly.");
        note("Machine-wide (HKLM) entries are shown read-only \xE2\x80\x94 they need elevation to change.");
    } break;
    case SP_REGION: {   // Plasma's "Regional Settings"
        header("System locale");
        kv("Language",  LocaleStr(LOCALE_SLOCALIZEDDISPLAYNAME));
        kv("Country",   LocaleStr(LOCALE_SLOCALIZEDCOUNTRYNAME));
        kv("Short date",LocaleStr(LOCALE_SSHORTDATE));
        kv("Long date", LocaleStr(LOCALE_SLONGDATE));
        kv("Time",      LocaleStr(LOCALE_STIMEFORMAT));
        kv("Currency",  LocaleStr(LOCALE_SCURRENCY));
        kv("Time zone", TimeZoneName());
        my+=8;
        header("Shell clock");
        bool c24=g_clock24;
        if(toggle("24-hour clock","Everywhere the shell prints a time",c24)){ g_clock24=c24; SaveConfig(); }
        { const char* days[2]={"Sunday","Monday"};
          TextAt(dl,g_fMed,17,V(mx,my+4),WithA(COL_INK,al),"First day of the week"); my+=30;
          int pick=choice(days,2,g_firstDay);
          if(pick>=0){ g_firstDay=pick; SaveConfig(); } }
        my+=8;
        { time_t t=time(nullptr); struct tm lt2; localtime_s(&lt2,&t);
          char now[128]; strftime(now,128,g_clock24?"%A, %d %B %Y  \xE2\x80\xA2  %H:%M:%S":"%A, %d %B %Y  \xE2\x80\xA2  %I:%M:%S %p",&lt2);
          TextAt(dl,g_fMed,16,V(mx,my),WithA(COL_GOLD,al),now); my+=30; }
        if(buttons({"Windows date & time","Language settings"},178)==0)
            AetherShellExec(nullptr,L"open",L"ms-settings:dateandtime",nullptr,nullptr,SW_SHOWNORMAL);
    } break;
    case SP_ACCESS: {   // Plasma's "Accessibility"
        header("Motion and sight");
        bool rm2=g_reduceMotion;
        if(toggle("Reduce motion","Shell transitions resolve immediately",rm2)){
            g_reduceMotion=rm2; g_animMul=rm2?9.0f:1.0f; SaveConfig(); }
        float us=g_uiScale;
        if(slider("Interface scale",us,0.75f,2.0f,"%.2fx")) g_uiScale=us;
        if(rel) SaveConfig();
        { float cs=(float)AxCursorSize();
          if(slider("Cursor size",cs,32,144,"%.0f px")) { if(rel) AxSetCursorSize((int)cs); } }
        { bool hc=AxHighContrast();
          TextAt(dl,g_fMed,17,V(mx,my+6),WithA(COL_INK,al),"High contrast");
          TextAt(dl,g_fSml,14,V(mx+rowW-TextW(g_fSml,14,hc?"on":"off")-6,my+8),WithA(hc?COL_GOLD:COL_INK2,al),hc?"on":"off");
          my+=34; }
        header("Keyboard and pointer aids");
        { bool sk=AxSticky();      if(toggle("Sticky keys","Modifiers latch instead of being held",sk)) AxSetSticky(sk);
          bool fk=AxFilter();      if(toggle("Filter keys","Ignore brief or repeated keystrokes",fk)) AxSetFilter(fk);
          bool tk=AxToggleKeys();  if(toggle("Toggle keys","Beep on Caps / Num / Scroll Lock",tk)) AxSetToggleKeys(tk);
          bool mk=AxMouseKeys();   if(toggle("Mouse keys","Move the pointer with the numeric keypad",mk)) AxSetMouseKeys(mk); }
        my+=8;
        if(buttons({"Windows accessibility","Magnifier","Narrator"},170)==0)
            AetherShellExec(nullptr,L"open",L"ms-settings:easeofaccess",nullptr,nullptr,SW_SHOWNORMAL);
    } break;
    case SP_USERS: {
        { float r2=34; ImVec2 ac2=V(mx+r2,my+r2);
          dl->AddCircleFilled(ac2,r2,A(COL_GOLDBG));
          SetRailIcon(dl,SP_USERS,V(ac2.x,ac2.y-2),A(COL_GOLD));
          TextAt(dl,g_fBig,24,V(mx+2*r2+18,my+12),WithA(COL_INK,al),CurrentUser().c_str());
          std::string sub=std::string(IsAdminUser()?"Administrator":"Standard user")+"   \xE2\x80\xA2   "+CurrentHost();
          TextAt(dl,g_fSml,14,V(mx+2*r2+18,my+44),WithA(COL_INK2,al),sub.c_str());
          my+=2*r2+22; }
        header("Account");
        { wchar_t prof[MAX_PATH]={0}; DWORD n2=MAX_PATH; GetEnvironmentVariableW(L"USERPROFILE",prof,n2);
          kv("Profile folder", W2U8(prof));
          kv("Session", IsAdminUser()?"Elevated where required":"Unelevated");
          kv("Uptime", UptimeLine()); }
        my+=8;
        int b4=buttons({"Accounts settings","Sign out","Lock"},160);
        if(b4==0) AetherShellExec(nullptr,L"open",L"ms-settings:yourinfo",nullptr,nullptr,SW_SHOWNORMAL);
        else if(b4==1) ExitWindowsEx(EWX_LOGOFF,0);
        else if(b4==2) DoLock();
        note("Adding or removing accounts is delegated to the Windows Accounts pane.");
    } break;
    case SP_UPDATES: {   // Plasma's "Software Update"
        bool pending=UpdateRebootPending();
        { float h=64;
          dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h),pending?WithA(COL_ERR,(int)(40*e)):AccA((int)(40*e)),12);
          TextAt(dl,g_fMed,18,V(mx+14,my+12),WithA(pending?COL_ERR:COL_GOLD,al),
                 pending?"Restart required to finish updating":"No restart pending");
          TextAt(dl,g_fSml,13,V(mx+14,my+38),WithA(COL_INK2,al),
                 pending?"Windows has staged updates that need a reboot.":"The last update completed cleanly.");
          my+=h+16; }
        header("Status");
        kv("Last successful install", UpdateLastInstall());
        kv("Windows",  OsVersionLine());
        my+=10;
        int b5=buttons({"Check for updates","Update history","Restart now"},170);
        if(b5==0) AetherShellExec(nullptr,L"open",L"ms-settings:windowsupdate-action",nullptr,nullptr,SW_SHOWNORMAL);
        else if(b5==1) AetherShellExec(nullptr,L"open",L"ms-settings:windowsupdate-history",nullptr,nullptr,SW_SHOWNORMAL);
        else if(b5==2&&pending){
            HANDLE tok; TOKEN_PRIVILEGES tp{};
            if(OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&tok)){
                LookupPrivilegeValueW(nullptr,SE_SHUTDOWN_NAME,&tp.Privileges[0].Luid);
                tp.PrivilegeCount=1; tp.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
                AdjustTokenPrivileges(tok,FALSE,&tp,0,nullptr,nullptr); CloseHandle(tok); }
            ExitWindowsEx(EWX_REBOOT|EWX_FORCEIFHUNG,SHTDN_REASON_MAJOR_OPERATINGSYSTEM); }
        note("Windows Update is a system service; the shell reports its state and hands off to it.");
    } break;
    case SP_FIREWALL: {
        if(!g_fwRead) RefreshFirewall();
        if(!g_fwOk){ note("The firewall policy service did not answer."); }
        else {
            header("Windows Defender Firewall profiles");
            static const char* PROFN[3]={"Domain network","Private network","Public network"};
            static const char* PROFD[3]={"Workplace domains","Home and trusted networks","Cafes, airports, anything untrusted"};
            for(int i=0;i<3;i++){
                bool on=g_fwOn[i];
                if(toggle(PROFN[i],PROFD[i],on)){
                    if(!SetFirewall(i,on)){ RefreshFirewall(); g_fwErr=GetTickCount64()+4000; } }
            }
            if(GetTickCount64()<g_fwErr){
                TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_ERR,al),"Access denied \xE2\x80\x94 run the shell elevated to change a profile."); my+=24; }
            my+=8;
            bool allOn=g_fwOn[0]&&g_fwOn[1]&&g_fwOn[2];
            TextAt(dl,g_fMed,16,V(mx,my),WithA(allOn?COL_GOLD:COL_ERR,al),
                   allOn?"All profiles are protected.":"At least one profile is off.");
            my+=32;
        }
        int b6=buttons({"Refresh","Allowed apps","Advanced rules"},156);
        if(b6==0) RefreshFirewall();
        else if(b6==1) AetherShellExec(nullptr,L"open",L"control.exe",L"firewall.cpl",nullptr,SW_SHOWNORMAL);
        else if(b6==2) AetherShellExec(nullptr,L"open",L"wf.msc",nullptr,nullptr,SW_SHOWNORMAL);
        note("Reading the policy works unelevated; writing it needs an administrator token.");
    } break;
    case SP_ABOUT: {   // the Aether tab: hero card, getting-started tour, About this system (SettingsFx.h)
        { int hit=DrawAetherHero(dl,io,V(mx-10,my),V(mx+rowW,my+250),e,click); my+=266;
          if(hit==1) TourStart();
          if(hit==2){ g_sysInfoOpen=!g_sysInfoOpen; if(g_sysInfoOpen && g_tourOn) g_tourOn=false; } }
        { static float tourA=0; tourA+=((g_tourOn? 1.0f : 0.0f)-tourA)*std::min(1.0f,g_frameDt*9.0f);
          if(tourA>0.01f){ float te=Cael::eval(Cael::EMPHASIZED_DECEL,tourA); int v0=dl->VtxBuffer.Size;
              float h=DrawTour(dl,io,mx,my,rowW,e*te,click && g_tourOn);
              ScaleVerts(dl,v0,V(mx+rowW*0.5f,my),0.94f+0.06f*te);
              my+=(h+16)*te; }
          else if(!g_sysInfoOpen){
              header("Getting started");
              note("New here? The tour walks through the bar, the launcher, the dashboard, the sidebar and the keys to know.");
              if(buttons({"Take the tour"},170)==0) TourStart(); } }
        // "About this system" opens in place: the section grows to its full height and its rows slide in
        static float sysA=0, sysH=0; sysA+=((g_sysInfoOpen? 1.0f : 0.0f)-sysA)*std::min(1.0f,g_frameDt*8.0f);
        if(sysA<0.005f && !g_sysInfoOpen){ sysA=0; break; }
        float sysE=Cael::eval(Cael::EMPHASIZED_DECEL,std::clamp(sysA,0.0f,1.0f));
        float sysY0=my; int sysV0=dl->VtxBuffer.Size;
        dl->PushClipRect(V(mx-14,sysY0-4),V(mx+rowW+6,sysY0+std::max(1.0f,sysH*sysE)),true);
        struct SysEnd{ ImDrawList* dl; int v0; float& my; float y0; float& h; float e; ~SysEnd(){ h=my-y0; LfxFadeVerts(dl,v0,e); LfxMoveVerts(dl,v0,0,-(1.0f-e)*24.0f); dl->PopClipRect(); my=y0+h*e; } } sysEnd{dl,sysV0,my,sysY0,sysH,sysE};
        header("About this system");
        header("Operating system");
        kv("OS",        OsVersionLine());
        kv("Host",      CurrentHost());
        kv("User",      CurrentUser()+(IsAdminUser()?"  (administrator)":""));
        kv("Uptime",    UptimeLine());
        kv("Shell",     IsShellReplaced()? "Aether replaces explorer.exe" : "Running alongside explorer.exe");
        my+=10;
        header("Hardware");
        kv("Processor", W2U8(g_st.cpuName));
        kv("Graphics",  W2U8(g_st.gpuName));
        { char mb[64]; snprintf(mb,64,"%.1f GB",g_st.memTotal/1073741824.0); kv("Memory",mb); }
        { char db[64]; snprintf(db,64,"%.0f GB total",g_st.diskTotal/1073741824.0); kv("Storage",db); }
        { char rb[96]; snprintf(rb,96,"%dx%d  @  %.0f%% scale",g_mw,g_mh,g_uiScale*100.0f); kv("Display",rb);
          snprintf(rb,96,"%d connected%s",(int)g_mons.size(), g_mons.size()>1?"  (taskbar + desktop on each)":"");
          kv("Monitors",rb);
          for(size_t mi2=0;mi2<g_mons.size();mi2++){ const RECT& mr2=g_mons[mi2].rc;
              char lbl[24]; snprintf(lbl,24,"  Screen %d",(int)mi2+1);
              snprintf(rb,96,"%ldx%ld at %ld,%ld%s",mr2.right-mr2.left,mr2.bottom-mr2.top,mr2.left,mr2.top,
                       g_mons[mi2].primary?"  (primary)":"");
              kv(lbl,rb); } }
        kv("Sensors",   g_sensorSrc);
        my+=12;
        int b7=buttons({"Windows About","Device Manager","Copy summary"},160);
        if(b7==0) AetherShellExec(nullptr,L"open",L"ms-settings:about",nullptr,nullptr,SW_SHOWNORMAL);
        else if(b7==1) AetherShellExec(nullptr,L"open",L"devmgmt.msc",nullptr,nullptr,SW_SHOWNORMAL);
        else if(b7==2){
            std::string s2="Aether 2.0\n"+OsVersionLine()+"\n"+W2U8(g_st.cpuName)+"\n"+W2U8(g_st.gpuName)+"\n";
            if(OpenClipboard(g_setHwnd)){ EmptyClipboard();
                std::wstring w2=U82W(s2);
                HGLOBAL hg=GlobalAlloc(GMEM_MOVEABLE,(w2.size()+1)*sizeof(wchar_t));
                if(hg){ memcpy(GlobalLock(hg),w2.c_str(),(w2.size()+1)*sizeof(wchar_t)); GlobalUnlock(hg);
                        SetClipboardData(CF_UNICODETEXT,hg); }
                CloseClipboard(); } }
    } break;
    case SP_PRESETS: {
        if(!g_presetsRead){ EnsureDefaultPreset(); RefreshPresets(); }
        note("A preset is a full snapshot of every setting. Save one, then switch back any time.");
        my+=6;
        header("Saved presets");
        for(size_t i=0;i<g_presets.size();i++){
            const std::string& nm=g_presets[i];
            float h=44; bool hov=rowHit(my,h); bool sel=((int)i==g_presetSel);
            float ha=HoverAnim(idBase+40+(int)i,hov&&!sel);
            if(sel) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-5),AccA((int)(46*e)),11);
            else if(ha>0.01f) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-5),WithA(COL_INK2,(int)(ha*32*e)),11);
            TextAt(dl,g_fMed,16,V(mx+8,my+11),WithA(sel?COL_GOLD:COL_INK,al),nm.c_str());
            if(nm=="Default") TextAt(dl,g_fSml,11,V(mx+18+TextW(g_fMed,16,nm.c_str()),my+15),WithA(COL_INK2,al),"built-in");
            if(click&&hov) g_presetSel=(int)i;
            my+=h; }
        if(g_presets.empty()) note("No presets yet.");
        my+=8;
        { int b8=buttons({"Apply selected","Save current as\xE2\x80\xA6","Delete","Refresh"},150);
          if(b8==0 && g_presetSel>=0 && g_presetSel<(int)g_presets.size()) ApplyPreset(g_presets[g_presetSel]);
          else if(b8==1){
              // name it after the time, so repeated saves never collide
              time_t tt=time(nullptr); struct tm lt2; localtime_s(&lt2,&tt);
              char nb[64]; strftime(nb,64,"Preset %d %b %H-%M",&lt2);
              SavePresetAs(nb); }
          else if(b8==2 && g_presetSel>=0 && g_presetSel<(int)g_presets.size()){
              DeletePreset(g_presets[g_presetSel]); g_presetSel=-1; }
          else if(b8==3) RefreshPresets(); }
        my+=6;
        note("Presets live in the presets\\ folder next to the exe \xE2\x80\x94 copy one to share it.");
        note("Applying a preset re-reads every setting immediately; no restart needed.");
    } break;
    case SP_MONITORS: {
        // Screens are listed LEFT TO RIGHT as they sit on your desk, not in enumeration order —
        // "Screen 2" meaning something different from the second monitor you look at was the whole
        // reason this page was confusing.
        static std::vector<int> order; order.clear();
        for(size_t i=0;i<g_mons.size();i++) order.push_back((int)i);
        std::sort(order.begin(),order.end(),[&](int a,int b){
            if(g_mons[a].rc.left!=g_mons[b].rc.left) return g_mons[a].rc.left<g_mons[b].rc.left;
            return g_mons[a].rc.top<g_mons[b].rc.top; });
        // applies a whole configuration in one go, then rebuilds the layers once
        auto applyAll=[&](int onlyIdx /* -1 = every display */){
            for(size_t k=0;k<g_mons.size();k++){
                bool on = (onlyIdx<0) || ((int)k==onlyIdx);
                g_mons[k].cfg.desktop=on; g_mons[k].cfg.bar=on; g_mons[k].cfg.panels=on;
                g_monCfg[g_mons[k].dev]=g_mons[k].cfg;
            }
            if(onlyIdx>=0){ g_actMon=onlyIdx; const RECT& r2=g_mons[onlyIdx].rc;
                g_mx=r2.left; g_my=r2.top; g_mw=r2.right-r2.left; g_mh=r2.bottom-r2.top;
                PlaceOverlaysOnActive(); }
            SaveConfig(); g_monsDirty=true;
        };
        note("Turn off what you don't want on a screen. The shell's windows shrink to the screens");
        note("still using them, so a disabled screen costs nothing to draw \xE2\x80\x94 Windows owns it again.");
        my+=8;
        if(g_mons.size()<2){ note("Only one display is connected."); }
        else {
            int b6=buttons({"Use every display"},190);
            if(b6==0) applyAll(-1);
            my+=2;
        }
        for(size_t oi=0; oi<order.size(); oi++){
            int mi2=order[oi];
            MonInfo& M=g_mons[mi2];
            const char* where = (order.size()==1)? "" :
                                (oi==0)? "left" : (oi+1==order.size())? "right" : "middle";
            char head[160];
            snprintf(head,160,"Screen %d \xE2\x80\x94 %s%s%s%s",(int)oi+1,
                     where, *where?" \xE2\x80\xA2 ":"", M.dev.c_str(), M.primary?"  (primary)":"");
            TextAt(dl,g_fMed,17,V(mx,my),WithA(COL_INK,al),head); my+=23;
            bool anyOn = M.cfg.desktop||M.cfg.bar||M.cfg.panels;
            char geo[128];
            snprintf(geo,128,"%ldx%ld at %ld,%ld   \xE2\x80\xA2   %s",
                     M.rc.right-M.rc.left,M.rc.bottom-M.rc.top,M.rc.left,M.rc.top,
                     anyOn? "shell is on this screen" : "shell is off \xE2\x80\x94 Windows owns this screen");
            TextAt(dl,g_fSml,13,V(mx,my),WithA(anyOn?COL_INK2:COL_GOLD,al),geo); my+=22;
            if(g_mons.size()>1){
                float bw2=180, bx2=mx;
                bool hov2=io.MousePos.x>bx2&&io.MousePos.x<bx2+bw2&&io.MousePos.y>my&&io.MousePos.y<my+28;
                float ha2=HoverAnim(idBase+80+mi2,hov2);
                dl->AddRectFilled(V(bx2,my),V(bx2+bw2,my+28),WithA(COL_INK2,(int)((28+ha2*44)*e)),9);
                const char* bl="Use only this screen";
                TextAt(dl,g_fSml,13,V(bx2+bw2/2-TextW(g_fSml,13,bl)/2,my+6),WithA(COL_INK,al),bl);
                if(click&&hov2) applyAll(mi2);
                my+=36;
            }
            bool ch2=false;
            bool d2=M.cfg.desktop;
            if(toggle("Desktop","Wallpaper bubble and frosted surround",d2)){ M.cfg.desktop=d2; ch2=true; }
            bool b2=M.cfg.bar;
            if(toggle("Taskbar","The bar strip on this screen",b2)){ M.cfg.bar=b2; ch2=true; }
            bool p2=M.cfg.panels;
            if(toggle("Panels","Dashboard, quick settings, launcher and notifications may open here",p2)){
                M.cfg.panels=p2; ch2=true; }
            if(ch2){
                g_monCfg[M.dev]=M.cfg;
                // if the panels' own screen was just switched off, move them to one that allows them
                if(!M.cfg.panels && mi2==g_actMon){
                    for(size_t k=0;k<g_mons.size();k++) if(g_mons[k].cfg.panels){
                        g_actMon=(int)k; const RECT& r2=g_mons[k].rc;
                        g_mx=r2.left; g_my=r2.top; g_mw=r2.right-r2.left; g_mh=r2.bottom-r2.top;
                        PlaceOverlaysOnActive(); break; }
                }
                SaveConfig(); g_monsDirty=true;      // resize the shell layers + redo the work areas
            }
            my+=10;
            if(oi+1<order.size()){ dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.22f)),1.0f); my+=16; }
        }
        my+=6;
        note("Panels (dashboard, quick settings, launcher) live on one screen at a time and follow");
        note("your mouse \xE2\x80\x94 they only move to screens where Panels is on.");
    } break;
    case SP_PLUGINS: {
        note("Plugins are Lua files in the plugins\\ folder. They are sandboxed: no file, network or");
        note("process access \xE2\x80\x94 only the cw.* drawing and read-only system API. A plugin that errors is");
        note("switched off with its message shown here, and comes back when you save the file.");
        my+=8;
        bool on=g_pluginsOn;
        if(toggle("Enable plugins","Load and draw plugins\\*.lua",on)){ g_pluginsOn=on; SaveConfig(); }
        my+=6;
        header("Installed");
        if(g_plugins.empty()) note("No plugins found. Use \"Open folder\" \xE2\x80\x94 an example is written there on first run.");
        for(size_t i=0;i<g_plugins.size();i++){
            LuaPlugin& p=g_plugins[i];
            bool bad=!p.err.empty();
            float h = bad? 74.0f : 52.0f;
            bool hov=rowHit(my,h); float ha=HoverAnim(idBase+50+(int)i,hov);
            if(ha>0.01f) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-6),WithA(COL_INK2,(int)(ha*26*e)),10);
            TextAt(dl,g_fMed,16,V(mx,my+6),WithA(COL_INK,al),p.name.c_str());
            char sub[220];
            snprintf(sub,220,"%s  \xE2\x80\xA2  %s",p.file.c_str(), p.surface==PSURF_BAR?"taskbar":"desktop");
            TextAt(dl,g_fSml,12,V(mx,my+28),WithA(COL_INK2,al),sub);
            if(bad){ std::string em=p.err; if(em.size()>92) em=em.substr(0,92)+"\xE2\x80\xA6";
                TextAt(dl,g_fSml,12,V(mx,my+46),WithA(IM_COL32(226,110,100,255),al),em.c_str()); }
            // enable switch
            float sw=44,sh=24, sx=mx+rowW-sw-6, sy=my+8;
            dl->AddRectFilled(V(sx,sy),V(sx+sw,sy+sh),p.enabled?A(COL_GOLD):A(COL_TRACK),sh*0.5f);
            dl->AddCircleFilled(V(p.enabled? sx+sw-sh*0.5f : sx+sh*0.5f,sy+sh*0.5f),sh*0.5f-3,IM_COL32(252,254,253,al));
            bool onSw=io.MousePos.x>sx-6&&io.MousePos.x<sx+sw+6&&io.MousePos.y>sy-6&&io.MousePos.y<sy+sh+6;
            if(click&&onSw){
                p.enabled=!p.enabled;
                if(p.enabled) PluginLoad(p); else PluginClose(p);
                g_plDisabled.clear();
                for(auto& q:g_plugins) if(!q.enabled) g_plDisabled.push_back(q.file);
                SaveConfig();
            }
            my+=h; }
        my+=6;
        { int b9=buttons({"Reload all","Open folder","Write example"},150);
          if(b9==0){ for(auto& p:g_plugins) if(p.enabled) PluginLoad(p); }
          else if(b9==1){ CreateDirectoryW(U82W(PluginDir()).c_str(),nullptr);
                          AetherShellExec(nullptr,L"open",U82W(PluginDir()).c_str(),nullptr,nullptr,SW_SHOWNORMAL); }
          else if(b9==2){ WriteExamplePlugin(true); PluginsScan(); } }
        my+=6;
        note("Files are watched: save a plugin and it reloads within about a second.");
    } break;
    case SP_DASHBOARD: default: {  // Dashboard
        header("Weather");
        { static const char* WS[]={"Off","My location (IP lookup)","A city"};
          int v=g_wxSource; if(cycler("Weather location","Off until you choose. \"My location\" sends your IP address to ip-api.com to find your city.",v,WS,3)){ g_wxSource=v; SaveConfig(); FetchWeather(); } }
        if(g_wxSource==2){ static char cb[96]={0}; static std::string seen="\x01";
            if(seen!=g_wxCity){ snprintf(cb,sizeof(cb),"%s",g_wxCity.c_str()); seen=g_wxCity; }
            textField("City",cb,sizeof(cb),9921,false);
            if(g_wxCity!=cb){ g_wxCity=cb; seen=g_wxCity; }
            if(buttons({"Use this city"},170)==0){ SaveConfig(); FetchWeather(); } }
        if(g_wxSource!=0 && g_wx.ok){ char wl[128]; snprintf(wl,128,"Showing %s: %.0f\xC2\xB0, %s",g_wx.city.c_str(),g_wx.temp,WxText(g_wx.code)); note(wl); }
        my+=8;
        note("Build your own tabs and place widgets anywhere. Edit mode opens in the drawer:");
        note("drag to move, drag a corner to resize, X to remove, + Add widget to add.");
        my+=6;
        header("Layouts");
        { static const char* LL[]={"Classic (Aether)","Caelestia"};
          int v=g_homeLayout;  if(cycler("Dashboard tab",nullptr,v,LL,2)){ g_homeLayout=v; SaveConfig(); }
          v=g_mediaLayout;     if(cycler("Media tab",nullptr,v,LL,2)){ g_mediaLayout=v; SaveConfig(); }
          v=g_perfLayout;      if(cycler("Performance tab",nullptr,v,LL,2)){ g_perfLayout=v; SaveConfig(); } }
        note("Every Caelestia card is also a widget: edit a tab > + Add widget > Caelestia.");
        { int b=buttons({"Add a Terminal tab","Add a Mixer tab"},200); if(b==0) RunShellCommand("dashboard_terminal_tab"); else if(b==1) RunShellCommand("dashboard_mixer_tab"); }
        my+=10;
        header("Profile");
        note("The Profile widget (+ Add widget > Profile). Click your status on it to change it, click the dot to cycle presence.");
        { static char nb[64]; static std::string was="\x01";
          if(was!=g_profileName){ snprintf(nb,sizeof(nb),"%s",g_profileName.c_str()); was=g_profileName; }
          textField("Display name  (empty = Windows user name)",nb,sizeof(nb),9701,false);
          if(g_profileName!=nb){ g_profileName=nb; was=g_profileName; SaveConfig(); } }
        { static char sb[128]; static std::string was="\x01";
          if(was!=g_status){ snprintf(sb,sizeof(sb),"%s",g_status.c_str()); was=g_status; }
          textField("Status",sb,sizeof(sb),9702,false);
          if(g_status!=sb){ ProfileSetStatus(sb,"",g_statusClearMin); was=g_status; } }
        { static const char* PL[]={"Online","Idle","Do not disturb","Invisible","No dot"};
          int v=std::clamp(g_presence,0,4); if(cycler("Presence",nullptr,v,PL,5)) ProfileSetPresence(v); }
        { int v=0; for(int i=0;i<PROFILE_NICONS;i++) if(g_statusIcon==PROFILE_ICONS[i]) v=i;
          if(cycler("Status icon","Or any Material Symbols name in profile.toml",v,PROFILE_ICONS,PROFILE_NICONS)){ g_statusIcon=PROFILE_ICONS[v]; SaveConfig(); } }
        { int v=ProfileClearIndex(g_statusClearMin);
          if(cycler("Clear status after",nullptr,v,PROFILE_CLEAR_LABEL,5)){ g_statusClearMin=PROFILE_CLEAR_MIN[v]; if(!g_status.empty()) g_statusSetAt=(int)time(nullptr); SaveConfig(); } }
        { int v=M3ShapeFromName(g_avatarShape); if(v<0) v=M3_CIRCLE;
          if(cycler("Avatar shape",nullptr,v,M3_SHAPE_LABEL,M3_COUNT)){ g_avatarShape=M3_SHAPE_ID[v]; SaveConfig(); } }
        { int bt=buttons({"Choose picture / GIF","Use account picture"},200);
          if(bt==0) PickFileAsync(L"Images\0*.gif;*.png;*.jpg;*.jpeg;*.webp;*.bmp\0All files\0*.*\0",nullptr,[](const std::string& f){ g_avatarImage=f; SaveConfig(); });
          else if(bt==1){ g_avatarImage.clear(); SaveConfig(); } }
        { bool v=g_avatarRing; if(toggle("Ring around the avatar",nullptr,v)){ g_avatarRing=v; SaveConfig(); } }
        { bool v=g_profileShowName; if(toggle("Show name",nullptr,v)){ g_profileShowName=v; SaveConfig(); } }
        { bool v=g_presenceDndLink; if(toggle("Presence controls do not disturb","\"Do not disturb\" silences notifications, and DND from the bar turns the dot red",v)){ g_presenceDndLink=v; SaveConfig(); } }
        { static char rb[256]; static std::string was="\x01";
          if(was!=g_profileRows){ snprintf(rb,sizeof(rb),"%s",g_profileRows.c_str()); was=g_profileRows; }
          textField("Info rows  (icon|text ; icon|text|colour ...)",rb,sizeof(rb),9703,false);
          if(g_profileRows!=rb){ g_profileRows=rb; was=g_profileRows; SaveConfig(); } }
        note("Row text can use {os} {wm} {uptime_long} {host} {cpu%} {mem%} {battery%} {media.title} {weather.temp} {time} ...");
        my+=10;
        header("Media card");
        { bool rv=g_mdRays;
          if(toggle("Audio bars around the art","64 bars reacting to what is playing. Off leaves just the cover and the progress ring, which is how Caelestia draws it.",rv)){
              g_mdRays=rv; SaveConfig(); } }
        { static const char* MDSTYLE_LABEL[2]={"Circle + visualiser","Shaped + wavy arc"};
          int st=choice(MDSTYLE_LABEL,2,std::clamp(g_mdStyle,0,1));
          if(st>=0 && st!=g_mdStyle){ g_mdStyle=st; SaveConfig(); } }
        if(g_mdStyle==MDSTYLE_SHAPED){
          int ms=choice(M3_SHAPE_LABEL,M3_COUNT,std::clamp(g_mdMediaShape,0,M3_COUNT-1));
          if(ms>=0 && ms!=g_mdMediaShape){ g_mdMediaShape=ms; SaveConfig(); } }
        note("The cover is filled into that silhouette, and the wavy arc above it is the track position \xE2\x80\x94 the elapsed part is a travelling wave, the rest a thin track with a dot at the end.");
        header("Lyrics");
        { static const char* LL[]={"Beside the player","Separate Player / Lyrics sections"};
          int v=g_lyricsLayout; if(cycler("Lyrics layout",nullptr,v,LL,2)){ g_lyricsLayout=v; SaveConfig(); } }
        my+=10;
        header("Tab bar");
        { static const char* TS[]={"Caelestia (icon over label)","Pills","Segmented","Minimal (labels)"};
          int v=g_tabStyle; if(cycler("Style",nullptr,v,TS,4)){ g_tabStyle=v; SaveConfig(); } }
        if(g_tabStyle==0||g_tabStyle==3){ static const char* TI[]={"Sliding underline","Pill","Dot","None"};
          int v=g_tabIndicator; if(cycler("Current tab mark",nullptr,v,TI,4)){ g_tabIndicator=v; SaveConfig(); } }
        { bool v=g_tabIcons;  if(toggle("Icons",nullptr,v)){ g_tabIcons=v; SaveConfig(); } }
        { bool v=g_tabLabels; if(toggle("Labels",nullptr,v)){ g_tabLabels=v; SaveConfig(); } }
        { bool v=g_tabSeparator; if(toggle("Separator line",nullptr,v)){ g_tabSeparator=v; SaveConfig(); } }
        { bool v=g_tabFill; if(toggle("Share the full width","Off: tabs are as wide as they need, centred",v)){ g_tabFill=v; SaveConfig(); } }
        { bool v=g_tabWheel; if(toggle("Scroll to switch tabs","Mouse wheel over the tab bar",v)){ g_tabWheel=v; SaveConfig(); } }
        my+=10;
        header("Your tabs  \xC2\xB7  click an icon to change it");
        for(size_t i=0;i<g_tabs.size();i++){
            DashTab& t=g_tabs[i]; float h=40; bool hov=rowHit(my,h);
            float ha=HoverAnim(idBase+120+(int)i,hov);
            if(ha>0.01f) dl->AddRectFilled(V(mx-8,my),V(mx+rowW,my+h-4),WithA(COL_INK2,(int)(ha*28*e)),9);
            { static const char* ICONS[]={"dashboard","queue_music","speed","cloud","home","apps","widgets","monitoring",
                                          "music_note","lyrics","calendar_month","sports_esports","code","terminal","star",
                                          "favorite","bolt","grid_view","photo_library","wallpaper","palette","insights",
                                          "memory","notifications","schedule","public","rocket_launch","pets","bedtime","eco"};
              const int NI=(int)(sizeof(ICONS)/sizeof(ICONS[0]));
              ImVec2 ic=V(mx+14,my+18); bool ih=fabsf(io.MousePos.x-ic.x)<14&&fabsf(io.MousePos.y-ic.y)<14;
              if(ih) dl->AddCircleFilled(ic,15,WithA(COL_GOLD,40));
              std::string nm=TabIconName(t);
              if(!MsIcon(dl,nm,ic,20,COL_GOLD)) TabIcon(dl,t.icon,ic,COL_GOLD);
              if(ih&&click){ int idx=0; for(int k=0;k<NI;k++) if(nm==ICONS[k]){ idx=k+1; break; }
                  t.iconName=ICONS[idx%NI]; SaveConfig(); }
              if(ih&&io.MouseClicked[1]){ int idx=NI-1; for(int k=0;k<NI;k++) if(nm==ICONS[k]){ idx=(k+NI-1)%NI; break; }
                  t.iconName=ICONS[idx]; SaveConfig(); } }
            TextAt(dl,g_fMed,16,V(mx+34,my+10),WithA(COL_INK,al),t.name.c_str());
            if(t.builtin) TextAt(dl,g_fSml,11,V(mx+44+TextW(g_fMed,16,t.name.c_str()),my+14),WithA(COL_INK2,al),"built-in");
            // click the name to rename it inline
            if(g_renameTab==(int)i){
                for(ImWchar ch : io.InputQueueCharacters){ if(ch>=32&&ch<127){ if(t.name.size()<24) t.name+=(char)ch; } }
                if(ImGui::IsKeyPressed(ImGuiKey_Backspace)&&!t.name.empty()) t.name.pop_back();
                if(ImGui::IsKeyPressed(ImGuiKey_Enter)||ImGui::IsKeyPressed(ImGuiKey_Escape)){ g_renameTab=-1; SaveConfig(); }
                float cw2=TextW(g_fMed,16,t.name.c_str());
                if(fmodf((float)GetTickCount64()*0.002f,1.0f)<0.5f)
                    dl->AddRectFilled(V(mx+35+cw2,my+11),V(mx+37+cw2,my+29),COL_GOLD,1);
                dl->AddLine(V(mx+34,my+31),V(mx+34+std::max(60.0f,cw2),my+31),COL_GOLD,1.2f);
            } else {
                bool nhov=io.MousePos.x>mx+30&&io.MousePos.x<mx+40+TextW(g_fMed,16,t.name.c_str())&&io.MousePos.y>my+6&&io.MousePos.y<my+h-6;
                if(nhov&&click){ g_renameTab=(int)i; }
            }
            // edit / up / down
            float ex=mx+rowW-190;
            auto minibtn=[&](float bx,const char* lbl,float w2)->bool{
                bool bh=io.MousePos.x>bx&&io.MousePos.x<bx+w2&&io.MousePos.y>my+4&&io.MousePos.y<my+h-4;
                dl->AddRectFilled(V(bx,my+4),V(bx+w2,my+h-4),WithA(COL_INK2,bh?60:32),8);
                TextAt(dl,g_fSml,13,V(bx+w2/2-TextW(g_fSml,13,lbl)/2,my+11),WithA(COL_INK,al),lbl);
                return bh&&click; };
            if(minibtn(ex,"Edit",70)){ g_tab=(int)i; g_editTab=true; g_editSel=-1; g_setShow=false;
                g_drawerForceUntil=GetTickCount64()+800; }
            if(minibtn(ex+76,"\xE2\x86\x91",34) && i>0){ std::swap(g_tabs[i],g_tabs[i-1]); if(g_tab==(int)i)g_tab--; else if(g_tab==(int)i-1)g_tab++; SaveConfig(); }
            if(minibtn(ex+114,"\xE2\x86\x93",34) && i+1<g_tabs.size()){ std::swap(g_tabs[i],g_tabs[i+1]); if(g_tab==(int)i)g_tab++; else if(g_tab==(int)i+1)g_tab--; SaveConfig(); }
            my+=h;
        }
        my+=6;
        { int bt=buttons({"+ New tab","Reset to the four defaults"},210);
          if(bt==0){ DashTab nt; nt.name="Tab"; nt.icon=0; nt.builtin=false; g_tabs.push_back(nt);
                     g_tab=(int)g_tabs.size()-1; g_editTab=true; g_editSel=-1; g_setShow=false;
                     g_drawerForceUntil=GetTickCount64()+800; SaveConfig(); }
          else if(bt==1){ DefaultTabs(); g_tab=0; SaveConfig(); } }
        my+=14;
        header("Custom widgets (TOML and Python)");
        note("Every .toml and .py in config\\widgets appears under Custom in the dashboard's Add a widget palette.");
        note("They can bind to live data, draw Material shapes, rings, GIFs and icons, and react to clicks. See README.md there.");
        { int b=buttons({"Open widgets folder","Open the reference","Reload"},200);
          if(b==0) AetherShellExec(nullptr,L"explore",U82W(CwDir()).c_str(),nullptr,nullptr,SW_SHOWNORMAL);
          if(b==1) AetherShellExec(nullptr,L"open",L"notepad.exe",(L"\""+U82W(CwDir()+"README.md")+L"\"").c_str(),nullptr,SW_SHOWNORMAL);
          if(b==2){ CwShutdown(); g_cwDefs.clear(); CwEnsureExamples(); } }
        { static char pyb[260]={0}; static bool init=false; if(!init){ init=true; strncpy_s(pyb,g_cwPython.c_str(),_TRUNCATE); }
          textField("Python for widgets (empty = find it on PATH)",pyb,sizeof(pyb),9201,false);
          if(g_cwPython!=pyb){ g_cwPython=pyb; SaveConfig(); }
          std::wstring found=CwFindPython();
          note(found.empty()? "No Python found - Python widgets will say so; TOML widgets work without it." : (std::string("Using ")+W2U8(found)).c_str()); }
        my+=14;
        header("Write your own config (config.toml)");
        note("Export snapshots your current tabs to a documented, hand-editable config.toml.");
        note("Edit it, then Import to apply. Lives next to the app; delete it to ignore.");
        { static double s_tomlMsgUntil=0; static std::string s_tomlMsg;
          int bt2=buttons({"Export config.toml","Import config.toml","Open folder"},210);
          if(bt2==0){ SaveTomlLayout(); s_tomlMsg="Exported \xE2\x86\x92 config.toml"; s_tomlMsgUntil=ImGui::GetTime()+4.0;
              AetherShellExec(nullptr,L"open",L"notepad.exe",U82W(ExeDir()+"config.toml").c_str(),nullptr,SW_SHOWNORMAL); }
          else if(bt2==1){ if(LoadTomlLayout()){ SaveConfig(); g_tab=0; s_tomlMsg="Imported config.toml \xE2\x9C\x93"; }
              else s_tomlMsg="No valid config.toml found"; s_tomlMsgUntil=ImGui::GetTime()+4.0; }
          else if(bt2==2){ AetherShellExec(nullptr,L"open",L"explorer.exe",U82W("/select,\""+ExeDir()+"config.toml\"").c_str(),nullptr,SW_SHOWNORMAL); }
          if(ImGui::GetTime()<s_tomlMsgUntil) TextAt(dl,g_fSml,13,V(mx,my+4),WithA(COL_GOLD,al),s_tomlMsg.c_str()); }
        my+=26;
        header("Media card image");
        note("Drop in a GIF or PNG and it replaces the little drawn cat. Animated GIFs play while");
        note("music is playing and rest when it is paused.");
        { std::string cur = g_catPath.empty()? std::string("(the drawn cat)") : g_catPath;
          kv("Current", cur); }
        { int b5=buttons({"Choose an imageâ¦","Use the drawn cat"},190);
          if(b5==0){
              wchar_t file[MAX_PATH]={0};
              OPENFILENAMEW ofn={}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_setHwnd;
              ofn.lpstrFilter=L"Images *.gif;*.png;*.jpg;*.jpeg;*.webp All files *.* ";
              ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
              ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
              if(GetOpenFileNameW(&ofn)){ g_catPath=W2U8(file); g_catDirty=true; SaveConfig(); }
          } else if(b5==1){ g_catPath.clear(); g_catDirty=true; SaveConfig(); } }
        my+=14;
        header("Drawer layout");
        { static const int dEdges[2]={EDGE_TOP,EDGE_BOTTOM};   // cards are authored wide, not tall
          panelLayout(PN_DRAWER,dEdges,2,"Drawer height",240,640,true); }
        if(rel) SaveConfig();
        my+=10;
        my+=10; dl->AddLine(V(mx,my),V(mx+rowW,my),WithA(COL_INK2,(int)(al*0.25f)),1); my+=12;
        TextAt(dl,g_fMed,18,V(mx,my),WithA(COL_INK,al),"Temperatures"); my+=26;
        { char sb[200]; snprintf(sb,200,"Source \xE2\x80\xA2 %s",g_sensorSrc.c_str());
          TextAt(dl,g_fSml,13,V(mx,my),WithA(COL_INK2,al),sb); my+=22; }
        note("A Radeon reports its own temperature straight from the driver \xE2\x80\x94 nothing to install.");
        note("A CPU temperature lives in MSRs, which need a kernel driver, so it needs the sidecar below.");
        if(!SidecarInstalled()){
            note("sensors\\AetherSensors.exe is not present in this build.");
        } else {
            bool running=SidecarRunning();
            { char sb2[160]; snprintf(sb2,160,"Sensor sidecar \xE2\x80\xA2 %s%s%s",
                        running?"running":"stopped",
                        g_sidecarNote.empty()?"":" \xE2\x80\xA2 ", g_sidecarNote.c_str());
              TextAt(dl,g_fSml,13,V(mx,my),WithA(running?COL_GOLD:COL_INK2,al),sb2); my+=24; }
            note("Starting it asks Windows for administrator once, so it can load the signed driver "
                 "that reads the CPU. Without that it still runs \xE2\x80\x94 it just cannot report a CPU number.");
            int b6=buttons({running?"Stop the sidecar":"Start it (asks for admin)","Start without admin"},210);
            if(b6==0){ if(running){ SidecarStop(); g_sensorSidecar=false; }
                       else { SidecarStart(true); g_sensorSidecar=true; } SaveConfig(); }
            else if(b6==1){ SidecarStart(false); g_sensorSidecar=true; SaveConfig(); }
            bool au=g_sensorSidecar;
            if(toggle("Start it with the shell","Launches the sidecar when Aether starts",au)){ g_sensorSidecar=au; SaveConfig(); }
        }
        note("A running LibreHardwareMonitor with its web server on port 8085 also works, and wins "
             "over the sidecar if both are up.");
    } break;
    }
    g_setContentH = (my+g_setScroll) - myStart;
    dl->PopClipRect();
    // scroll indicator on the right edge of the middle column
    if(g_setContentH>viewH){
        float trackH=viewH-8, th2=std::max(28.0f,trackH*viewH/g_setContentH);
        float t0=viewTop+4+(trackH-th2)*(g_setScroll/std::max(1.0f,g_setContentH-viewH));
        dl->AddRectFilled(V(mx+rowW+6,viewTop+4),V(mx+rowW+9,viewTop+4+trackH),WithA(COL_INK2,(int)(40*e)),2);
        dl->AddRectFilled(V(mx+rowW+6,t0),V(mx+rowW+9,t0+th2),WithA(COL_GOLD,(int)(190*e)),2);
    }
    // cross-fade the middle column on a page change (content fades up out of the panel surface)
    if(pgE<0.999f) dl->AddRectFilled(V(mx-16,py+58),V(mx+rowW+16,py+ph-14),PanelCol((int)(252*(1.0f-pgE)*e)));
    (void)pgAl;

    if(g_setWipeAt){
        // 0 .. .42 sweep in, hold (the page switches at .45), .58 .. 1 sweep out
        float pin=std::clamp(wipeT/0.42f,0.0f,1.0f), pout=std::clamp((wipeT-0.58f)/0.42f,0.0f,1.0f);
        float textA=std::clamp((wipeT-0.22f)/0.14f,0.0f,1.0f)*(1.0f-std::clamp((wipeT-0.62f)/0.14f,0.0f,1.0f));
        DrawStripeCover(dl,V(px,py),V(px+mainW,py+ph),pin,pout,nullptr,0,e,textA);
    }
    DrawTourMini(dl,io,V(px,py),V(px+mainW,py+ph),e,tourClick);
    g_setScrollKeep=g_setScroll; g_setScrollTKeep=g_setScrollT;
    // close button + click-away
    { ImVec2 xc=V(px+pw-26,py+26); bool hov=fabsf(io.MousePos.x-xc.x)<15&&fabsf(io.MousePos.y-xc.y)<15;
      if(hov) dl->AddCircleFilled(xc,15,WithA(COL_INK2,(int)(40*e)));
      dl->AddLine(V(xc.x-6,xc.y-6),V(xc.x+6,xc.y+6),WithA(COL_INK,al),2.0f);
      dl->AddLine(V(xc.x+6,xc.y-6),V(xc.x-6,xc.y+6),WithA(COL_INK,al),2.0f);
      bool inPanel=io.MousePos.y>py&&io.MousePos.y<py+ph &&
                   ((io.MousePos.x>px&&io.MousePos.x<px+mainW) ||
                    (wallW>0&&io.MousePos.x>wallX&&io.MousePos.x<wallX+wallW));
      if(click&&(hov||!inPanel)) g_setShow=false; }
    // stand both cards up out of the desktop plane while opening
    if(setBorn) dl->PopClipRect();
    else Warp3D(dl,vwarp,V(px+pw*0.5f,py+ph),-(1.0f-e)*0.50f);
    if(ImGui::IsKeyPressed(ImGuiKey_Escape)) g_setShow=false;
    g_setRect = g_setShow? RECT{0,0,(LONG)W,(LONG)H} : RECT{0,0,0,0};
}
