// Aether - shell mode (Cairo model), komorebi / workspaces.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =========================================================================================
// SHELL MODE — the Cairo Shell model, ported.
//
// Cairo decides it is the shell exactly like this (CairoApplicationInitializationService):
//     IsAppRunningAsShell = GetShellWindow() == IntPtr.Zero
// i.e. if nobody has claimed the shell window, WinLogon launched *us* instead of explorer.exe.
// Once that is true a proper shell has to do four things Explorer would otherwise do:
//   1. claim the shell window        -> SetShellWindow, so GetShellWindow()/Show-Desktop work
//   2. claim the taskbar window      -> SetTaskmanWindow, so Win+Tab and the taskbar API find us
//   3. subscribe to the shell hook   -> RegisterShellHookWindow, the live window-list feed
//   4. run the login items           -> Run / RunOnce / the Startup folders
// Exiting also matters: WinLogon restarts the shell when the shell process exits with code 0,
// so a deliberate quit must exit non-zero (Cairo sets ApplicationExitCode = 1).
// =========================================================================================
static bool g_forceShell=false, g_forceNoShell=false;
static UINT g_shellHookMsg=0;                // RegisterWindowMessage("SHELLHOOK")
static bool g_taskListDirty=true;            // the shell hook flags the task list for a rebuild

typedef BOOL (WINAPI *PFN_SetShellWindow)(HWND);
typedef BOOL (WINAPI *PFN_SetTaskmanWindow)(HWND);
typedef BOOL (WINAPI *PFN_RegisterShellHookWindow)(HWND);
typedef BOOL (WINAPI *PFN_DeregisterShellHookWindow)(HWND);

// Stop Windows tiling minimized windows' title bars across the bottom-left of the desktop.
// This is ManagedShell's SetMinimizedMetrics (ManagedShell.WindowsTasks): without Explorer, the
// 3.1-era arrangement comes back, and the documented cure is ARW_HIDE in MINIMIZEDMETRICS —
// not moving the windows by hand, which is what I tried first and why it did nothing.
#ifndef ARW_HIDE
#define ARW_HIDE 0x0008
#endif
//
// Testers still saw a little title-bar "bubble" of a minimized app at the bottom left. Three holes, all fixed here:
//   * it was only set when Aether is registered as THE shell - run alongside a killed / hidden Explorer, never set;
//   * it was only set once at startup - anything that resets it later (Explorer restarting, a game, a tweak tool)
//     brought the bubbles back for the rest of the session. Now re-checked every 2 s;
//   * windows ALREADY minimized when it goes on stay where they were, so they are re-arranged (ArrangeIconicWindows
//     applies the new metrics to them) - komorebi's "Minimize" hiding means every other workspace is full of them;
//   * and on exit it cleared the flag unconditionally - with Explorer running that switched the bubbles ON for the
//     rest of the session. Exit now puts back whatever was there before Aether touched it.
static int g_minHideOrig=-1;                                   // ARW_HIDE before Aether changed it (-1 = not read yet)
static void SweepMinimizedBubbles(){
    struct Ctx{ int onScreen=0; } c;
    EnumWindows([](HWND h,LPARAM lp)->BOOL{
        if(!IsWindowVisible(h) || !IsIconic(h)) return TRUE;
        DWORD pid=0; GetWindowThreadProcessId(h,&pid); if(pid==GetCurrentProcessId()) return TRUE;
        RECT r; if(GetWindowRect(h,&r) && r.left>-20000 && r.top>-20000) ((Ctx*)lp)->onScreen++;
        return TRUE; },(LPARAM)&c);
    if(!c.onScreen) return;
    ArrangeIconicWindows(GetDesktopWindow());                  // re-place them under ARW_HIDE: off the visible area
    // anything still showing (a window that ignores the arrangement) is parked the same way Windows parks them
    EnumWindows([](HWND h,LPARAM)->BOOL{
        if(!IsWindowVisible(h) || !IsIconic(h)) return TRUE;
        DWORD pid=0; GetWindowThreadProcessId(h,&pid); if(pid==GetCurrentProcessId()) return TRUE;
        RECT r; if(GetWindowRect(h,&r) && r.left>-20000 && r.top>-20000)
            SetWindowPos(h,nullptr,-32000,-32000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_ASYNCWINDOWPOS);
        return TRUE; },0);
}
static void SetMinimizedMetrics(){
    MINIMIZEDMETRICS mm{}; mm.cbSize=sizeof(mm);
    if(!SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS,sizeof(mm),&mm,0)) return;
    if(g_minHideOrig<0) g_minHideOrig=(mm.iArrange&ARW_HIDE)==ARW_HIDE? 1 : 0;
    if((mm.iArrange&ARW_HIDE)==ARW_HIDE) return;              // already set
    mm.iArrange|=ARW_HIDE;
    SystemParametersInfoW(SPI_SETMINIMIZEDMETRICS,sizeof(mm),&mm,0);
    SweepMinimizedBubbles();
}
static void ClearMinimizedMetrics(){                          // on exit: put back what was there before Aether
    if(g_minHideOrig!=0) return;                               // it was already on (Explorer / the user) or never read
    MINIMIZEDMETRICS mm{}; mm.cbSize=sizeof(mm);
    if(!SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS,sizeof(mm),&mm,0)) return;
    if((mm.iArrange&ARW_HIDE)!=ARW_HIDE) return;
    mm.iArrange&=~ARW_HIDE;
    SystemParametersInfoW(SPI_SETMINIMIZEDMETRICS,sizeof(mm),&mm,0);
}
// ---- grey Chromium / Electron windows after a restore ----
// komorebi hides other workspaces by MINIMIZING their windows (its Cloak mode crashes on Windows 11 26100), and
// Chromium suspends its compositor while minimized. Coming back it sometimes shows a grey or stale frame until
// something makes it paint. When such a window leaves the minimized state it gets a nudge: an invalidate and a
// frame-change notification, both asynchronous, so a hung app can never stall the shell.
static HWINEVENTHOOK g_restoreHook=nullptr;
// ---- frozen (grey) Chromium windows ----
// Switching workspaces very fast minimizes and restores the same windows many times a second, and a Chromium app can
// lose its compositor surface on the way: the window comes back as one flat grey rectangle and nothing short of an
// app restart brought it back - a redraw, a resize, hide/show, minimize/restore, moving it to another monitor,
// WM_DWMCOMPOSITIONCHANGED and WM_DISPLAYCHANGE were all tried on a stuck Spotify and did nothing. What did work:
// ending the app's GPU helper process. Chromium starts a new one at once and rebuilds every window's surface; the
// app itself (and its playback) never stops. So when a restored Chromium window is still flat after ~4.5 s, that is
// what Aether does - once per app per two minutes, because Chromium turns GPU acceleration off after repeated
// GPU-process crashes.
static bool FrozenLooksFlat(HWND h){
    if(!IsWindow(h) || IsIconic(h) || !IsWindowVisible(h)) return false;
    // This reads the SCREEN. While a workspace slide is running, an opaque layer of ours covers the monitor, so
    // every sample would come back the layer's flat backdrop and any window under it would be declared frozen -
    // and "repaired" by having its GPU helper killed. Do not guess at what we cannot see.
    if(g_wsSlideBusy.load() || GetTickCount64()-g_wsSlideEndAt.load()<200) return false;
    if(g_t2Busy.load() || GetTickCount64()-g_t2EndAt.load()<200) return false;
    RECT r; if(FAILED(DwmGetWindowAttribute(h,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof(r)))) GetWindowRect(h,&r);
    int W=r.right-r.left, H=r.bottom-r.top; if(W<240 || H<180 || r.left<-20000) return false;
    HDC scr=GetDC(nullptr); if(!scr) return false;
    std::map<COLORREF,int> hist; int vis=0;
    const int GX=14, GY=9;
    for(int gy=0;gy<GY;gy++) for(int gx=0;gx<GX;gx++){
        POINT pt{ r.left+(LONG)(W*(0.08+0.84*(gx+0.5)/GX)), r.top+(LONG)(H*(0.10+0.82*(gy+0.5)/GY)) };
        HWND at=WindowFromPoint(pt); if(!at) continue;
        HWND root=GetAncestor(at,GA_ROOT);
        if(root!=h){ if(Ws2IsLayer(root)||T2IsLayer(root)) return false;                 // one of our layers went up mid-sample
                     DWORD pid=0; GetWindowThreadProcessId(root,&pid); if(pid!=GetCurrentProcessId()) continue; }   // covered by another app: skip
        COLORREF c=GetPixel(scr,pt.x,pt.y); if(c==CLR_INVALID) continue;
        c = RGB(GetRValue(c)>>3,GetGValue(c)>>3,GetBValue(c)>>3);
        hist[c]++; vis++;
    }
    ReleaseDC(nullptr,scr);
    if(vis<50) return false;
    int top=0; for(auto& kv:hist) top=std::max(top,kv.second);
    return top >= vis*97/100;
}
static bool RestartGpuHelper(DWORD browserPid){
    typedef LONG (NTAPI *NtQIP)(HANDLE,ULONG,PVOID,ULONG,PULONG);
    NtQIP q=(NtQIP)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess"); if(!q) return false;
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0); if(snap==INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{}; pe.dwSize=sizeof(pe); bool done=false;
    if(Process32FirstW(snap,&pe)) do{
        if(pe.th32ParentProcessID!=browserPid) continue;
        HANDLE ph=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE,FALSE,pe.th32ProcessID); if(!ph) continue;
        std::vector<BYTE> buf(8192); ULONG ret=0;
        LONG st=q(ph,60 /*ProcessCommandLineInformation*/,buf.data(),(ULONG)buf.size(),&ret);
        if(st==(LONG)0xC0000004 && ret>buf.size()){ buf.resize(ret); st=q(ph,60,buf.data(),(ULONG)buf.size(),&ret); }
        if(st>=0){
            struct US{ USHORT Length, MaximumLength; PWSTR Buffer; };
            US* u=(US*)buf.data();
            std::wstring cmd(u->Buffer? u->Buffer : L"", u->Buffer? u->Length/sizeof(wchar_t) : 0);
            if(cmd.find(L"--type=gpu-process")!=std::wstring::npos){ done = TerminateProcess(ph,1)!=FALSE; }
        }
        CloseHandle(ph);
    }while(!done && Process32NextW(snap,&pe));
    CloseHandle(snap);
    return done;
}
static std::mutex g_reviveMtx; static std::map<DWORD,ULONGLONG> g_reviveAt;   // browser pid -> last revive
static std::string ReviveAppName(DWORD pid){
    std::string nm="the app";
    if(HANDLE ph=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)){ wchar_t path[MAX_PATH]; DWORD n=MAX_PATH;
        if(QueryFullProcessImageNameW(ph,0,path,&n)){ nm=W2U8(SlLeaf(path)); } CloseHandle(ph); }
    return nm;
}
// returns true if it restarted something
static bool ReviveIfFrozen(HWND h,bool force){
    DWORD pid=0; GetWindowThreadProcessId(h,&pid); if(!pid) return false;
    { std::lock_guard<std::mutex> lk(g_reviveMtx); auto it=g_reviveAt.find(pid);
      if(!force && it!=g_reviveAt.end() && GetTickCount64()-it->second<120000) return false; }
    if(!RestartGpuHelper(pid)) { AetherLog("frozen window: %s (pid %lu) looked grey but no GPU helper was found",ReviveAppName(pid).c_str(),pid); return false; }
    { std::lock_guard<std::mutex> lk(g_reviveMtx); g_reviveAt[pid]=GetTickCount64(); }
    std::string nm=ReviveAppName(pid);
    AetherLog("frozen window: %s (pid %lu) was a flat grey rectangle after a restore - restarted its GPU helper",nm.c_str(),pid);
    ShowAetherMessage("Fixed a frozen "+nm+" window","It came back grey after a workspace switch, so Aether restarted its graphics helper. The app kept running.",2,"auto_fix_high",7000);
    return true;
}
static void CALLBACK RestoreNudgeProc(HWINEVENTHOOK,DWORD,HWND h,LONG idObj,LONG,DWORD,DWORD){
    if(idObj!=OBJID_WINDOW || !h) return;
    DWORD pid=0; GetWindowThreadProcessId(h,&pid); if(pid==GetCurrentProcessId()) return;
    wchar_t cls[64]={0}; GetClassNameW(h,cls,64);
    if(wcsncmp(cls,L"Chrome_WidgetWin",16)!=0) return;         // Chromium, Electron (Discord, Spotify, VS Code...)
    std::thread([h]{
        // WHY THIS WAITS, AND WHY IT ASKS FIRST.
        //
        // It used to fire unconditionally on every Chromium/Electron restore: two forced repaints, at 90 ms and
        // ~490 ms, each with RDW_ERASE (paint the background over the window) and SWP_FRAMECHANGED (recalculate
        // the frame). A workspace switch restores three or four such windows at once, so that is six to eight
        // wipe-and-repaint cycles landing in the middle of the slide - which is what "the apps glitch out and
        // flash for a couple of seconds" was. The slide was showing live previews of those same windows, so the
        // flashing appeared in the preview and then again on the real window as the layer lifted.
        //
        // Now: wait until the slide is over, ask whether the window is actually broken, and only then poke it -
        // without erasing, which is the part that flashes. A window that came back drawing fine is left alone.
        for(int tries=0; tries<40 && (g_wsSlideBusy.load() || GetTickCount64()-g_wsSlideEndAt.load()<150); tries++) Sleep(25);
        for(int d: {90, 400}){
            Sleep(d); if(!IsWindow(h) || IsIconic(h)) return;
            if(!FrozenLooksFlat(h)) return;                  // it is drawing: nothing to fix, nothing to flash
            RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN);
            SetWindowPos(h,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_FRAMECHANGED|SWP_ASYNCWINDOWPOS);
        }
        // still a flat grey rectangle three times over ~4.5 s (and still shown)? its compositor is gone: revive it
        if(!g_reviveFrozen) return;
        static std::mutex busyMtx; static std::unordered_set<HWND> busy;
        { std::lock_guard<std::mutex> lk(busyMtx); if(!busy.insert(h).second) return; }
        struct Done{ HWND h; ~Done(){ std::lock_guard<std::mutex> lk(busyMtx); busy.erase(h); } } done{h};
        for(int i=0;i<3;i++){ Sleep(1500); if(!FrozenLooksFlat(h)) return; }
        ReviveIfFrozen(h,false);
    }).detach();
}
// one line in errors.log with the komorebi settings that decide how windows hide and whether they can grey out -
// so a tester's log says what their setup is without anyone having to ask
static void KomoConfigDiag(){
    std::thread([]{
        wchar_t dir[MAX_PATH]={0}; std::wstring path;
        if(GetEnvironmentVariableW(L"KOMOREBI_CONFIG_HOME",dir,MAX_PATH)) path=std::wstring(dir)+L"\\komorebi.json";
        else if(GetEnvironmentVariableW(L"USERPROFILE",dir,MAX_PATH)) path=std::wstring(dir)+L"\\komorebi.json";
        FILE* f=_wfopen(path.c_str(),L"rb"); if(!f) return;
        std::string c; char buf[8192]; size_t r; while((r=fread(buf,1,sizeof(buf),f))>0) c.append(buf,r); fclose(f);
        auto val=[&](const char* key,size_t from=0)->std::string{ size_t k=c.find(key,from); if(k==std::string::npos) return "(default)";
            size_t v=c.find_first_not_of(" \t\r\n:",k+strlen(key)); if(v==std::string::npos) return "?";
            size_t e=c.find_first_of(",}\r\n",v); return c.substr(v,e==std::string::npos? std::string::npos : e-v); };
        size_t an=c.find("\"animation\"");
        AetherLog("komorebi config: window_hiding_behaviour=%s animation.enabled=%s transparency=%s transparency_alpha=%s",
                  val("\"window_hiding_behaviour\"").c_str(), an==std::string::npos? "(default)" : val("\"enabled\"",an).c_str(),
                  val("\"transparency\"").c_str(), val("\"transparency_alpha\"").c_str());
    }).detach();
}
static void InstallRestoreNudge(){
    KomoConfigDiag();
    if(!g_restoreHook) g_restoreHook=SetWinEventHook(EVENT_SYSTEM_MINIMIZEEND,EVENT_SYSTEM_MINIMIZEEND,nullptr,RestoreNudgeProc,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
}

// Last chance to hand the desktop back when this process is going down the hard way (unhandled
// exception, std::terminate, abrupt exit). Deliberately tiny and allocation-free where it can be:
// a crashing process must not need a working heap to give the user their taskbar back. It does NOT
// clear the journal — if this only half-worked, the next launch or the RunOnce entry finishes it.
static std::atomic<bool> g_emergencyRan{false};
static void EmergencyRestore(){
    if(g_emergencyRan.exchange(true)) return;
    if(g_taskbarHidden){
        ShowRealTaskbars();
        for(size_t i=0;i<g_savedWorkAreas.size();i++)
            SystemParametersInfoW(SPI_SETWORKAREA,0,&g_savedWorkAreas[i],SPIF_SENDCHANGE);
        g_taskbarHidden=false;
    }
    ClearMinimizedMetrics();
}
// There was an unhandled-exception filter but it wrote NO dump, so a tester's crash report was
// unactionable - all we could do was read code and guess. dbghelp is loaded on demand rather than
// linked, so this costs nothing until something actually goes wrong.
static void WriteDumpNamed(const wchar_t* prefix,EXCEPTION_POINTERS* ep){
    HMODULE dbg=LoadLibraryW(L"dbghelp.dll"); if(!dbg) return;
    // dbghelp.h declares MINIDUMP_EXCEPTION_INFORMATION under #pragma pack(4). Without the same packing
    // the pointer lands at offset 8 instead of 4 on x64, MiniDumpWriteDump rejects the argument, and
    // the "crash dump" was a 0-byte file - found by --dumptest, which is why that test exists.
#pragma pack(push,4)
    struct MDEI { DWORD ThreadId; EXCEPTION_POINTERS* Ptrs; BOOL ClientPointers; };
#pragma pack(pop)
    typedef BOOL (WINAPI *PFN)(HANDLE,DWORD,HANDLE,DWORD,void*,void*,void*);
    PFN dump=(PFN)GetProcAddress(dbg,"MiniDumpWriteDump");
    if(dump){
        wchar_t ex[MAX_PATH]={0}; GetModuleFileNameW(nullptr,ex,MAX_PATH);
        std::wstring p=ex; size_t sl=p.find_last_of(L"\\/"); if(sl!=std::wstring::npos) p=p.substr(0,sl+1);
        SYSTEMTIME t; GetLocalTime(&t); wchar_t fn[96];
        swprintf(fn,96,L"%s %04d-%02d-%02d %02d%02d%02d.dmp",prefix? prefix : L"crash",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
        HANDLE f=CreateFileW((p+fn).c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f!=INVALID_HANDLE_VALUE){
            MDEI mi{ GetCurrentThreadId(), ep, FALSE };
            const DWORD TYPE=0x1|0x4|0x1000;   // DataSegs | IndirectlyReferencedMemory | ThreadInfo
            BOOL ok=dump(GetCurrentProcess(),GetCurrentProcessId(),f,TYPE, ep? &mi:nullptr, nullptr,nullptr);
            DWORD err=ok?0:GetLastError();
            CloseHandle(f);
            if(!ok){   // leave the reason next to the empty file rather than a silent 0-byte dump
                std::wstring why=p+fn; why+=L".txt";
                if(FILE* w=_wfopen(why.c_str(),L"w")){ fprintf(w,"MiniDumpWriteDump failed, GetLastError=0x%08lX\n",err); fclose(w); } }
        }
    }
    FreeLibrary(dbg);
}
static void WriteCrashDump(EXCEPTION_POINTERS* ep){ WriteDumpNamed(L"crash",ep); }
static LONG WINAPI AetherCrashFilter(EXCEPTION_POINTERS* ep){
    EmergencyRestore();                 // give the user their taskbar/work area back first
    WriteCrashDump(ep);
    return EXCEPTION_CONTINUE_SEARCH;   // let WER still report/handle it normally
}
// Installed once at startup, before anything system-wide is touched.
static void InstallCrashGuards(){
    SetUnhandledExceptionFilter(AetherCrashFilter);
    std::set_terminate([]{ EmergencyRestore(); _Exit(3); });
    atexit([]{ EmergencyRestore(); });
}

// ---- crash-loop breaker -------------------------------------------------------------------
// Worst case for somebody else's machine: Aether is set to autostart (or IS the shell), it dies
// during startup, and every sign-in reproduces a desktop with no taskbar. A counter bumped at
// launch and cleared once we have survived a while turns that into a self-limiting problem: after
// three consecutive bad starts we come up in SAFE MODE, which touches nothing system-wide and
// un-registers the shell replacement so the machine is guaranteed to boot to Explorer next time.
static bool g_safeMode=false;
static int  StartupStrikes(){
    DWORD n=0, sz=sizeof(n);
    RegGetValueW(HKEY_CURRENT_USER,RECKEY,L"Strikes",RRF_RT_REG_DWORD,nullptr,&n,&sz);
    return (int)n;
}
static void SetStartupStrikes(int n){
    HKEY k; if(RegCreateKeyExW(HKEY_CURRENT_USER,RECKEY,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS) return;
    if(n<=0) RegDeleteValueW(k,L"Strikes");
    else { DWORD v=(DWORD)n; RegSetValueExW(k,L"Strikes",0,REG_DWORD,(const BYTE*)&v,sizeof(v)); }
    RegCloseKey(k);
}

static void DetectShellMode(){
    // GetShellWindow() is NULL when explorer.exe is not the shell for this session
    g_isShell = (GetShellWindow()==nullptr && !g_forceNoShell) || g_forceShell;
}
// Claim the shell + taskbar roles. `desk` must be a plain top-level window with no owner and
// must NOT be topmost, or SetShellWindow refuses it.
static void ClaimShellRoles(HWND desk,HWND taskbar){
    HMODULE u32=GetModuleHandleW(L"user32.dll"); if(!u32) return;
    auto setShell   =(PFN_SetShellWindow)  GetProcAddress(u32,"SetShellWindow");
    auto setTaskman =(PFN_SetTaskmanWindow)GetProcAddress(u32,"SetTaskmanWindow");
    auto regHook    =(PFN_RegisterShellHookWindow)GetProcAddress(u32,"RegisterShellHookWindow");
    if(g_isShell && setShell && desk)      setShell(desk);
    if(setTaskman && taskbar)              setTaskman(taskbar);       // useful even alongside Explorer
    g_shellHookMsg=RegisterWindowMessageW(L"SHELLHOOK");
    if(regHook && taskbar)                 regHook(taskbar);          // live window-list events
}
static void ReleaseShellRoles(HWND taskbar){
    HMODULE u32=GetModuleHandleW(L"user32.dll"); if(!u32) return;
    auto dereg=(PFN_DeregisterShellHookWindow)GetProcAddress(u32,"DeregisterShellHookWindow");
    if(dereg && taskbar) dereg(taskbar);
}
