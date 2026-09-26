// src/services/SafeLaunch.h  —  Aether shell
// Opening things without ever freezing the shell.
//
// Every app, file, folder and URI Aether opens used to go through ShellExecuteW ON THE RENDER THREAD. That call
// does not return until Windows is done with it, and "done" can be a long time:
//   * the target is gone        -> Windows pops a modal "cannot find" box and waits for it (often BEHIND our topmost
//                                  layers, so the user sees a shell that stopped responding and no dialog at all)
//   * the app needs elevation   -> it waits on the UAC prompt
//   * a DDE-registered app      -> it waits for the app to answer
//   * a shortcut on a dead drive / network share -> it waits for the resolve to time out
// so one click could stop the bar, the dock, every panel and every animation.
//
// Now the call is queued to its own thread with SEE_MASK_FLAG_NO_UI (no Windows dialogs), the render loop never
// waits on it, and anything that goes wrong comes back as an Aether popup (src/components/AlertCard.h) with a
// plain-language reason and something to do about it. A program that dies the instant it starts (a missing DLL,
// a damaged install) is caught too. Every failure is also written to errors.log next to Aether.exe.
//
// Also here: RunOnUi (hand a result back to the render thread), async file / folder pickers (a modal dialog on the
// render thread froze the shell the whole time it was open), and the hang watchdog (a render-loop stall writes a
// dump and a log line, so a freeze on somebody else's machine can actually be diagnosed).
#pragma once
#include <functional>

static std::string W2U8(const std::wstring&);   // fwd
static std::wstring U82W(const std::string&);   // fwd
static std::string ExeDir();                    // fwd

// ---------------------------------------------------------------------------------------------- run on the UI thread
static std::mutex g_uiQMtx;
static std::vector<std::function<void()>> g_uiQ;
static void RunOnUi(std::function<void()> fn){ std::lock_guard<std::mutex> lk(g_uiQMtx); g_uiQ.push_back(std::move(fn)); }
static void DrainUiQueue(){
    std::vector<std::function<void()>> q; { std::lock_guard<std::mutex> lk(g_uiQMtx); q.swap(g_uiQ); }
    for(auto& f:q){ try{ f(); } catch(...){} }
}

// ---------------------------------------------------------------------------------------------- error log
static std::mutex g_errLogMtx;
static void AetherLog(const char* fmt,...){
    char msg[1024]; va_list ap; va_start(ap,fmt); vsnprintf(msg,sizeof(msg),fmt,ap); va_end(ap);
    std::lock_guard<std::mutex> lk(g_errLogMtx);
    std::string p=ExeDir()+"errors.log";
    // keep it from growing forever: past 1 MB the old log is set aside once
    WIN32_FILE_ATTRIBUTE_DATA fa; if(GetFileAttributesExA(p.c_str(),GetFileExInfoStandard,&fa) && fa.nFileSizeLow>1024*1024){
        std::string old=p+".old"; DeleteFileA(old.c_str()); MoveFileA(p.c_str(),old.c_str()); }
    if(FILE* f=fopen(p.c_str(),"a")){ SYSTEMTIME st; GetLocalTime(&st);
        fprintf(f,"%04d-%02d-%02d %02d:%02d:%02d  %s\n",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,msg); fclose(f); }
}

// ---------------------------------------------------------------------------------------------- popups (drawn by AlertCard.h)
struct AetherAlert {
    uint32_t id=0; int kind=0;               // 0 error, 1 warning, 2 info / progress
    std::string title, body, icon;
    std::wstring file, params, dir;          // what failed, so the card can offer Try again / Run as admin / Show in folder
    bool canRetry=false, canAdmin=false, canReveal=false;
    ULONGLONG at=0, until=0;                 // until: 0 = stays until dismissed
    uint32_t pendingId=0;                    // a "still opening" card closes itself when its launch finishes
};
static std::mutex g_alertMtx;
static std::vector<AetherAlert> g_alerts;
static std::atomic<uint32_t> g_alertSeq{1};
static uint32_t PushAlert(AetherAlert a){
    a.id=g_alertSeq++; a.at=GetTickCount64();
    std::lock_guard<std::mutex> lk(g_alertMtx);
    if(g_alerts.size()>=5) g_alerts.erase(g_alerts.begin());
    g_alerts.push_back(std::move(a)); return g_alerts.back().id;
}
static void CloseAlertsForPending(uint32_t pid){
    std::lock_guard<std::mutex> lk(g_alertMtx);
    g_alerts.erase(std::remove_if(g_alerts.begin(),g_alerts.end(),[&](const AetherAlert& x){ return x.pendingId==pid; }),g_alerts.end());
}
static void ShowAetherMessage(const std::string& title,const std::string& body,int kind=1,const char* icon="info",ULONGLONG ms=8000){
    AetherAlert a; a.kind=kind; a.title=title; a.body=body; a.icon=icon; a.until=ms? GetTickCount64()+ms : 0; PushAlert(a);
}

// ---------------------------------------------------------------------------------------------- launching
static std::wstring SlLeaf(const std::wstring& p){
    std::wstring s=p; while(!s.empty()&&(s.back()==L'\\'||s.back()==L'/')) s.pop_back();
    size_t k=s.find_last_of(L"\\/"); if(k!=std::wstring::npos) s=s.substr(k+1);
    size_t d=s.find_last_of(L'.'); if(d!=std::wstring::npos && d>0 && s.size()-d<=5) s=s.substr(0,d);
    return s.empty()? p : s;
}
// a plain file-system path we can check before handing it to the shell (not ms-settings:, shell:, http:, ::{guid})
static bool SlIsFsPath(const std::wstring& p){
    if(p.size()>=3 && iswalpha(p[0]) && p[1]==L':' && (p[2]==L'\\'||p[2]==L'/')) return true;
    if(p.size()>=2 && p[0]==L'\\' && p[1]==L'\\') return true;
    return false;
}
static std::string SlReason(DWORD err,const std::wstring& file,bool& retry,bool& admin,bool& reveal){
    std::string nm=W2U8(SlLeaf(file));
    retry=true; admin=false; reveal=SlIsFsPath(file);
    switch(err){
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_NAME:
        retry=false; reveal=false;
        return "\xE2\x80\x9C"+nm+"\xE2\x80\x9D isn't there any more. It may have been moved, renamed or uninstalled.";
    case ERROR_ACCESS_DENIED:
        admin=true; return "Windows wouldn't let it start (access denied). Running it as administrator may work.";
    case ERROR_NO_ASSOCIATION:
        retry=false; return "No app is set up to open this kind of file.";
    case ERROR_BAD_EXE_FORMAT: case ERROR_EXE_MACHINE_TYPE_MISMATCH:
        retry=false; return "This isn't a program this PC can run. It may be damaged or built for a different kind of processor.";
    case ERROR_DLL_NOT_FOUND: case ERROR_MOD_NOT_FOUND:
        return "A file it needs is missing. Reinstalling the app usually fixes this.";
    case ERROR_DDE_FAIL:
        return "The app didn't respond when Windows asked it to open. It may be busy or stuck.";
    case ERROR_ACCESS_DISABLED_BY_POLICY:
        retry=false; return "A system policy blocks this app.";
    case ERROR_VIRUS_INFECTED: case ERROR_VIRUS_DELETED:
        retry=false; reveal=false; return "Windows Security blocked it because it looks unsafe.";
    case ERROR_NOT_ENOUGH_MEMORY: case ERROR_OUTOFMEMORY:
        return "There wasn't enough memory to start it. Close something and try again.";
    case ERROR_SHARING_VIOLATION:
        return "Another program has the file locked.";
    case ERROR_BAD_NETPATH: case ERROR_NETNAME_DELETED: case ERROR_UNEXP_NET_ERR: case ERROR_BAD_NET_NAME:
        return "The network location it lives on can't be reached right now.";
    case ERROR_NOT_READY:
        return "The drive it's on isn't ready (a removed USB stick or disc?).";
    }
    wchar_t* buf=nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,err,0,(LPWSTR)&buf,0,nullptr);
    std::string s = buf? W2U8(buf) : std::string();
    if(buf) LocalFree(buf);
    while(!s.empty() && (s.back()=='\n'||s.back()=='\r'||s.back()==' '||s.back()=='.')) s.pop_back();
    char code[48]; snprintf(code,48," (error %lu)",err);
    return (s.empty()? std::string("Windows couldn't open it") : s)+code+".";
}
// NTSTATUS exit codes that mean "died while starting", with what they mean in plain words
static const char* SlCrashReason(DWORD code){
    switch(code){
    case 0xC0000135: return "a DLL it needs is missing";
    case 0xC000007B: return "its files are mismatched (32/64-bit) or damaged";
    case 0xC0000142: return "a DLL it loads failed to start";
    case 0xC0000005: return "it crashed (access violation)";
    case 0xC0000409: return "it crashed (stack buffer overrun)";
    case 0xC0000139: return "a DLL it loads is the wrong version";
    case 0xC000001D: return "it uses processor instructions this CPU doesn't have";
    case 0xC00000FD: return "it crashed (stack overflow)";
    case 0xC0000017: return "it ran out of memory";
    case 0xC0000022: return "it was denied access to something it needs";
    }
    return nullptr;
}

struct SlJob { std::wstring verb, file, params, dir; int show=SW_SHOWNORMAL; bool quiet=false; };
struct SlPending { uint32_t id; std::wstring file; ULONGLONG at; bool slowShown=false; };
static std::mutex g_slMtx;
static std::vector<SlPending> g_slPending;
static std::atomic<uint32_t> g_slSeq{1};

static void SlWorker(SlJob j){
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    uint32_t pid=g_slSeq++;
    { std::lock_guard<std::mutex> lk(g_slMtx); g_slPending.push_back({pid,j.file,GetTickCount64()}); }
    auto fail=[&](DWORD err,const std::string& why){
        AetherLog("open failed: \"%s\" %s verb=%s err=%lu: %s",W2U8(j.file).c_str(),W2U8(j.params).c_str(),W2U8(j.verb).c_str(),err,why.c_str());
        if(j.quiet) return;
        bool retry=false, admin=false, reveal=false; std::string body = why.empty()? SlReason(err,j.file,retry,admin,reveal) : why;
        if(!why.empty()){ retry=true; reveal=SlIsFsPath(j.file); }
        AetherAlert a; a.kind=0; a.icon="error"; a.title="Couldn't open "+W2U8(SlLeaf(j.file)); a.body=body;
        a.file=j.file; a.params=j.params; a.dir=j.dir; a.canRetry=retry; a.canAdmin=admin; a.canReveal=reveal;
        a.until=GetTickCount64()+15000; PushAlert(a);
    };
    bool preflightFailed=false;
    // pre-flight a real path: a missing file is the most common failure and needs no shell round trip
    if(SlIsFsPath(j.file) && _wcsicmp(j.verb.c_str(),L"explore")!=0){
        DWORD at=GetFileAttributesW(j.file.c_str());
        if(at==INVALID_FILE_ATTRIBUTES){ DWORD e=GetLastError(); fail(e,""); preflightFailed=true; }
        else {
            // a shortcut whose target is gone: Windows would offer to "search" for it in a modal dialog
            size_t dot=j.file.find_last_of(L'.');
            if(dot!=std::wstring::npos && _wcsicmp(j.file.c_str()+dot,L".lnk")==0){
                IShellLinkW* sl=nullptr;
                if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,(void**)&sl)) && sl){
                    IPersistFile* pf=nullptr;
                    if(SUCCEEDED(sl->QueryInterface(IID_IPersistFile,(void**)&pf)) && pf){
                        if(SUCCEEDED(pf->Load(j.file.c_str(),STGM_READ))){
                            wchar_t tgt[MAX_PATH]={0};
                            if(SUCCEEDED(sl->GetPath(tgt,MAX_PATH,nullptr,SLGP_RAWPATH)) && tgt[0]){
                                wchar_t ex[MAX_PATH]={0}; ExpandEnvironmentStringsW(tgt,ex,MAX_PATH);
                                std::wstring t=ex[0]? ex : tgt;
                                // only local drives: probing a sleeping network share is exactly the wait we are avoiding
                                if(SlIsFsPath(t) && t[0]!=L'\\'){
                                    wchar_t root[4]={t[0],L':',L'\\',0};
                                    UINT dt=GetDriveTypeW(root);
                                    if((dt==DRIVE_FIXED||dt==DRIVE_RAMDISK) && GetFileAttributesW(t.c_str())==INVALID_FILE_ATTRIBUTES){
                                        fail(ERROR_FILE_NOT_FOUND,"The shortcut points to \xE2\x80\x9C"+W2U8(t)+"\xE2\x80\x9D, which isn't there any more. The app may have been uninstalled or moved.");
                                        preflightFailed=true; } } } }
                        pf->Release(); }
                    sl->Release(); } } }
    }
    if(!preflightFailed){
        // A URI (ms-settings:, shell:, http:...) activates a protocol handler - often a packaged app. Asking for a
        // process handle on those made Windows wait ~7 s and then fail with 1155 (seen with ms-settings:windowsupdate),
        // so URIs are launched without one and never crash-watched.
        const bool isUri = !SlIsFsPath(j.file) && j.file.find(L':')!=std::wstring::npos && j.file.find(L':')>1;
        SHELLEXECUTEINFOW si{sizeof(si)};
        si.fMask=SEE_MASK_FLAG_NO_UI|SEE_MASK_NOASYNC|SEE_MASK_FLAG_LOG_USAGE|(isUri? 0 : SEE_MASK_NOCLOSEPROCESS);
        si.lpVerb=j.verb.empty()? nullptr : j.verb.c_str();
        si.lpFile=j.file.c_str();
        si.lpParameters=j.params.empty()? nullptr : j.params.c_str();
        si.lpDirectory=j.dir.empty()? nullptr : j.dir.c_str();
        si.nShow=j.show;
        BOOL ok=ShellExecuteExW(&si); DWORD err=ok? 0 : GetLastError();
        if(!ok && err==ERROR_ELEVATION_REQUIRED){ si.lpVerb=L"runas"; ok=ShellExecuteExW(&si); err=ok? 0 : GetLastError(); }
        // last resort for anything the strict call refused: the plain ShellExecute the shell always used (still on
        // this worker thread, so even if Windows shows its own box, the shell keeps running)
        if(!ok && err!=ERROR_CANCELLED && err!=ERROR_FILE_NOT_FOUND && err!=ERROR_PATH_NOT_FOUND){
            INT_PTR r=(INT_PTR)ShellExecuteW(nullptr,j.verb.empty()? nullptr : j.verb.c_str(),j.file.c_str(),j.params.empty()? nullptr : j.params.c_str(),j.dir.empty()? nullptr : j.dir.c_str(),j.show);
            if(r>32){ ok=TRUE; si.hProcess=nullptr; AetherLog("open: strict launch failed (err=%lu), plain launch worked: \"%s\"",err,W2U8(j.file).c_str()); } }
        if(!ok){
            if(err!=ERROR_CANCELLED) fail(err,"");        // ERROR_CANCELLED = the user said no to UAC: not an error
        } else if(si.hProcess){
            // died straight away with a crash status? say so instead of "nothing happened"
            if(WaitForSingleObject(si.hProcess,2500)==WAIT_OBJECT_0){
                DWORD code=0; GetExitCodeProcess(si.hProcess,&code);
                // 0xFFFFFFFF is exit(-1), not an NTSTATUS: single-instance apps (Voicemeeter) exit with it when
                // a copy is already running, and it was being reported as a crash every time.
                if(code>=0xC0000000 && code!=0xFFFFFFFF){
                    const char* r=SlCrashReason(code);
                    char body[256]; snprintf(body,256,"It closed as soon as it started: %s (0x%08lX).",r? r : "it crashed",code);
                    AetherLog("launched then crashed: \"%s\" exit=0x%08lX",W2U8(j.file).c_str(),code);
                    if(!j.quiet){
                        AetherAlert a; a.kind=0; a.icon="report"; a.title=W2U8(SlLeaf(j.file))+" stopped working"; a.body=body;
                        a.file=j.file; a.params=j.params; a.dir=j.dir; a.canRetry=true; a.canAdmin=true; a.canReveal=SlIsFsPath(j.file);
                        a.until=GetTickCount64()+15000; PushAlert(a); }
                }
            }
            CloseHandle(si.hProcess);
        }
    }
    { std::lock_guard<std::mutex> lk(g_slMtx);
      g_slPending.erase(std::remove_if(g_slPending.begin(),g_slPending.end(),[&](const SlPending& p){ return p.id==pid; }),g_slPending.end()); }
    CloseAlertsForPending(pid);
    if(SUCCEEDED(hr)) CoUninitialize();
}
// Drop-in for ShellExecuteW. Always returns "success" (>32) immediately; failures arrive as a popup.
static HINSTANCE AetherShellExec(HWND,LPCWSTR verb,LPCWSTR file,LPCWSTR params,LPCWSTR dir,INT show){
    SlJob j; j.verb=verb? verb : L""; j.file=file? file : L""; j.params=params? params : L""; j.dir=dir? dir : L""; j.show=show;
    j.quiet = (show==SW_HIDE);           // background helpers (shutdown.exe, cmd /c ...) only log
    if(j.file.empty()) return (HINSTANCE)(INT_PTR)2;
    // Control Panel applets have no "open" association of their own (ShellExecuteEx fails with 1155 under
    // SEE_MASK_FLAG_NO_UI) - they are opened by control.exe
    if(j.file.size()>4 && _wcsicmp(j.file.c_str()+j.file.size()-4,L".cpl")==0){
        j.params = j.file + (j.params.empty()? L"" : L","+j.params); j.file=L"control.exe"; j.verb.clear(); }
    try{ std::thread(SlWorker,std::move(j)).detach(); } catch(...){ return (HINSTANCE)(INT_PTR)8; }
    return (HINSTANCE)(INT_PTR)42;
}
static void AetherOpen(const std::wstring& file,const std::wstring& params=L"",const std::wstring& dir=L"",const wchar_t* verb=nullptr){
    AetherShellExec(nullptr,verb,file.c_str(),params.empty()? nullptr : params.c_str(),dir.empty()? nullptr : dir.c_str(),SW_SHOWNORMAL);
}
// called once a frame: a launch that is taking long gets a "still opening" card that closes itself when it lands
static void SlTick(){
    std::vector<std::pair<uint32_t,std::wstring>> slow;
    { std::lock_guard<std::mutex> lk(g_slMtx); ULONGLONG now=GetTickCount64();
      for(auto& p:g_slPending) if(!p.slowShown && now-p.at>7000){ p.slowShown=true; slow.push_back({p.id,p.file}); } }
    for(auto& s:slow){
        AetherLog("slow open (>7s): \"%s\"",W2U8(s.second).c_str());
        AetherAlert a; a.kind=2; a.icon="hourglass_top"; a.title="Still opening "+W2U8(SlLeaf(s.second))+"\xE2\x80\xA6";
        a.body="Windows is taking a while with this one. Aether keeps working in the meantime."; a.pendingId=s.first; a.until=GetTickCount64()+30000;
        PushAlert(a); }
}

// ---------------------------------------------------------------------------------------------- async pickers
// The dialog runs on its own thread, owned by an invisible TOPMOST window so it shows above every Aether layer.
static HWND SlDialogOwner(){
    HWND o=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,0,0,0,0,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(o){ ShowWindow(o,SW_SHOWNOACTIVATE); SetForegroundWindow(o); }
    return o;
}
static void PickFileAsync(const wchar_t* filter,const wchar_t* title,std::function<void(const std::string&)> done){
    std::wstring flt; if(filter){ const wchar_t* p=filter; while(*p){ size_t n=wcslen(p); flt.append(p,n+1); p+=n+1; } } flt.push_back(0);
    std::wstring ttl=title? title : L"";
    std::thread([flt,ttl,done]{
        HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
        HWND owner=SlDialogOwner();
        wchar_t file[MAX_PATH]={0};
        OPENFILENAMEW ofn={}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=owner;
        ofn.lpstrFilter=flt.size()>1? flt.c_str() : nullptr; ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
        ofn.lpstrTitle=ttl.empty()? nullptr : ttl.c_str();
        ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_NODEREFERENCELINKS;
        std::string got; if(GetOpenFileNameW(&ofn)) got=W2U8(file);
        if(owner) DestroyWindow(owner);
        if(SUCCEEDED(hr)) CoUninitialize();
        if(!got.empty()) RunOnUi([done,got]{ done(got); });
    }).detach();
}
static void PickFolderAsync(const wchar_t* title,std::function<void(const std::string&)> done){
    std::wstring ttl=title? title : L"";
    std::thread([ttl,done]{
        HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
        HWND owner=SlDialogOwner();
        wchar_t disp[MAX_PATH]={0}; BROWSEINFOW bi={};
        bi.hwndOwner=owner; bi.pszDisplayName=disp; bi.lpszTitle=ttl.c_str();
        bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
        std::string got;
        LPITEMIDLIST pidl=SHBrowseForFolderW(&bi);
        if(pidl){ wchar_t folder[MAX_PATH]={0}; if(SHGetPathFromIDListW(pidl,folder)) got=W2U8(folder); CoTaskMemFree(pidl); }
        if(owner) DestroyWindow(owner);
        if(SUCCEEDED(hr)) CoUninitialize();
        if(!got.empty()) RunOnUi([done,got]{ done(got); });
    }).detach();
}

// ---------------------------------------------------------------------------------------------- hang watchdog
// The render loop stamps g_loopBeat every frame. If it stops for 5 s the watchdog writes "hang <time>.dmp" (every
// thread's stack - the main thread's shows exactly what it is stuck in) and a line in errors.log. The interrupt time
// used here stops while the PC sleeps, so waking from sleep is not a "hang".
static std::atomic<ULONGLONG> g_loopBeat{0};
static std::atomic<bool> g_loopPaused{false};     // set around deliberate long waits (shutdown)
static void WriteDumpNamed(const wchar_t* prefix,EXCEPTION_POINTERS* ep);   // fwd (main.cpp, with the crash guards)
static ULONGLONG SlNowMs(){ ULONGLONG t=0; QueryUnbiasedInterruptTime(&t); return t/10000ULL; }
static void LoopHeartbeat(){ g_loopBeat.store(SlNowMs(),std::memory_order_relaxed); }
static void StartHangWatchdog(){
    static std::atomic<bool> started{false}; if(started.exchange(true)) return;
    LoopHeartbeat();
    std::thread([]{
        int dumps=0; bool inStall=false; ULONGLONG stallStart=0;
        for(;;){
            Sleep(1000);
            if(g_loopPaused) { inStall=false; continue; }
            ULONGLONG now=SlNowMs(), beat=g_loopBeat.load(std::memory_order_relaxed);
            ULONGLONG gap = now>beat? now-beat : 0;
            if(gap>=5000){
                if(!inStall){ inStall=true; stallStart=beat;
                    AetherLog("HANG: the render loop has not drawn a frame for %llu ms", gap);
                    if(dumps<3){ dumps++; WriteDumpNamed(L"hang",nullptr); } }
            } else if(inStall){
                inStall=false;
                AetherLog("HANG over: the render loop came back after about %llu ms", now-stallStart);
            }
        }
    }).detach();
}
