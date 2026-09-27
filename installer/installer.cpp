// =============================================================================================
// Aether setup — one self-contained, self-extracting installer.  (V1.5)
//
// Aether is a GPL-3.0 derivative of Caelestia Shell.
//
// WHAT CHANGED FROM V1
// --------------------
// V1 embedded three files as RCDATA. Aether now ships a font, an icon theme, wallpapers, the
// sensor sidecar and the visual styles — about 200 MB across 28,000 files — and rc.exe is the wrong
// tool for that: one resource per file means a 28,000-line .rc and a link step that takes minutes.
//
// So the payload is APPENDED to this exe instead, the way a self-extracting archive works, and it
// is compressed with the Windows Compression API (Cabinet.dll, MSZIP). Both halves of that — the
// packer in build-installer.ps1 and the unpacker here — call the same OS API, so there is no
// third-party compression library to vendor and no version skew to get wrong.
//
// The payload is cut into chunks that never straddle a file, so unpacking needs one chunk in memory
// at a time rather than the whole 200 MB, and the progress bar has something honest to report.
//
// Layout, reading from the end of the file:
//     [ PE image ][ chunk 0 ][ chunk 1 ]...[ chunk table ][ footer, 32 bytes ]
//   footer      : magic "AETHPAY1", offset of the chunk table, chunk count, file count, raw total
//   table entry : { u64 file offset, u32 compressed size, u32 uncompressed size }
//   chunk       : u32 record count, then { u16 path bytes, path (UTF-8), u32 size, bytes } * count
//
// Install is per-user into %LOCALAPPDATA%\Programs\Aether: no admin rights, and — the reason it
// matters — the shell keeps config.json next to its own exe, and Program Files is not writable.
//
// The same binary is copied into the install folder as uninstall.exe and re-run with /uninstall.
// =============================================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include <commctrl.h>
#include <compressapi.h>
#include <string>
#include <vector>
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"shlwapi.lib")
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"comdlg32.lib")
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"cabinet.lib")

static const wchar_t* APPNAME   = L"Aether";
static const wchar_t* VERSION   = L"1.6.2";
static const wchar_t* EXENAME   = L"Aether.exe";
static const wchar_t* SHELLCLS  = L"AetherClass";   // must match main.cpp's RegisterClassExW
static const wchar_t* UNINSTKEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Aether";
static const wchar_t* RUNKEY    = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* WINLOGON  = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
// V1 installed under this name; an upgrade has to find and retire it.
static const wchar_t* OLDKEY    = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CaelestiaWin";

// ---- payload format ---------------------------------------------------------------------------
#pragma pack(push,1)
struct PayFooter {
    char               magic[8];     // "AETHPAY1"
    unsigned long long tableOff;
    unsigned int       chunks;
    unsigned int       files;
    unsigned long long rawTotal;
};
struct PayChunk { unsigned long long off; unsigned int comp; unsigned int raw; };
#pragma pack(pop)
static const char PAY_MAGIC[8] = { 'A','E','T','H','P','A','Y','1' };

// ---- small helpers ---------------------------------------------------------------------------
static std::wstring KnownDir(REFKNOWNFOLDERID id){
    PWSTR p=nullptr; std::wstring out;
    if(SUCCEEDED(SHGetKnownFolderPath(id,0,nullptr,&p))&&p){ out=p; CoTaskMemFree(p); }
    return out;
}
static std::wstring DefaultDir(){ return KnownDir(FOLDERID_LocalAppData)+L"\\Programs\\Aether"; }
static std::wstring SelfPath(){ wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr,p,MAX_PATH); return p; }
static std::wstring SelfDir(){
    std::wstring m=SelfPath(); size_t s=m.find_last_of(L"\\");
    return s==std::wstring::npos ? L"" : m.substr(0,s);
}
static bool MakeDirs(const std::wstring& path){
    return SHCreateDirectoryExW(nullptr,path.c_str(),nullptr)==ERROR_SUCCESS ||
           GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;
}
static std::wstring U82W(const char* s,int n){
    if(n<=0) return L"";
    int w=MultiByteToWideChar(CP_UTF8,0,s,n,nullptr,0);
    std::wstring out(w,L'\0');
    MultiByteToWideChar(CP_UTF8,0,s,n,&out[0],w);
    return out;
}
static void RegSetStr(HKEY root,const wchar_t* sub,const wchar_t* name,const std::wstring& val){
    HKEY k; if(RegCreateKeyExW(root,sub,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS) return;
    RegSetValueExW(k,name,0,REG_SZ,(const BYTE*)val.c_str(),(DWORD)((val.size()+1)*sizeof(wchar_t)));
    RegCloseKey(k);
}
static void RegSetDword(HKEY root,const wchar_t* sub,const wchar_t* name,DWORD v){
    HKEY k; if(RegCreateKeyExW(root,sub,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS) return;
    RegSetValueExW(k,name,0,REG_DWORD,(const BYTE*)&v,sizeof(v)); RegCloseKey(k);
}
static std::wstring RegGetStr(HKEY root,const wchar_t* sub,const wchar_t* name){
    wchar_t buf[1024]={0}; DWORD sz=sizeof(buf);
    if(RegGetValueW(root,sub,name,RRF_RT_REG_SZ,nullptr,buf,&sz)!=ERROR_SUCCESS) return L"";
    return buf;
}
static void RegDelValue(HKEY root,const wchar_t* sub,const wchar_t* name){
    HKEY k; if(RegOpenKeyExW(root,sub,0,KEY_SET_VALUE,&k)!=ERROR_SUCCESS) return;
    RegDeleteValueW(k,name); RegCloseKey(k);
}
static bool MakeShortcut(const std::wstring& lnk,const std::wstring& target,const std::wstring& workdir,
                         const std::wstring& desc){
    IShellLinkW* sl=nullptr;
    if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&sl)))) return false;
    sl->SetPath(target.c_str()); sl->SetWorkingDirectory(workdir.c_str());
    sl->SetDescription(desc.c_str()); sl->SetIconLocation(target.c_str(),0);
    IPersistFile* pf=nullptr; bool ok=false;
    if(SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf)))){ ok=SUCCEEDED(pf->Save(lnk.c_str(),TRUE)); pf->Release(); }
    sl->Release(); return ok;
}
// Close only the instance we are about to overwrite. Matching on the path matters: a shell running
// from somewhere else is not ours to kill, and closing it would leave that session with no shell.
static void KillRunningShell(const std::wstring& dir){
    bool asked=false;
    // Aether registers ONE class for every layer, so the same sweep catches the bar, the desktop
    // layer and every drawer; WM_CLOSE on any of them takes the whole shell down cleanly.
    HWND h=nullptr;
    while((h=FindWindowExW(nullptr,h,SHELLCLS,nullptr))!=nullptr){
        DWORD pid=0; GetWindowThreadProcessId(h,&pid); if(!pid) continue;
        HANDLE pr=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!pr) continue;
        wchar_t path[MAX_PATH]={0}; DWORD n=MAX_PATH;
        bool mine=false;
        if(QueryFullProcessImageNameW(pr,0,path,&n)){
            std::wstring p2=path; size_t s2=p2.find_last_of(L"\\");
            if(s2!=std::wstring::npos) mine = _wcsicmp(p2.substr(0,s2).c_str(),dir.c_str())==0;
        }
        CloseHandle(pr);
        // WM_CLOSE, not a kill: the shell restores the Windows taskbar and work areas on the way out
        if(mine){ PostMessageW(h,WM_CLOSE,0,0); asked=true; }
    }
    if(asked) Sleep(1500);
}
static bool DeleteTree(const std::wstring& dir){
    std::wstring from=dir; from.push_back(L'\0');
    SHFILEOPSTRUCTW op={}; op.wFunc=FO_DELETE; op.pFrom=from.c_str();
    op.fFlags=FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT;
    return SHFileOperationW(&op)==0;
}

// ---- unpacking --------------------------------------------------------------------------------
static HWND  gProgWnd=nullptr;          // progress bar, or null in silent mode
// Where the appended payload starts, i.e. how big the bare stub is. uninstall.exe is a copy of
// setup with everything past this point left off: it never unpacks anything, and a 47 MB copy of
// the archive sitting in the install folder forever would be half the install for nothing.
static unsigned long long gStubSize=0;
static void  Progress(int done,int total){
    if(gProgWnd && total>0) SendMessageW(gProgWnd,PBM_SETPOS,(WPARAM)((done*100LL)/total),0);
}

// Write one extracted file. Directories are created lazily: 28,000 files share a few hundred
// folders, so remembering the last one turns almost every SHCreateDirectoryEx call into nothing.
static bool WriteOut(const std::wstring& dest,const unsigned char* data,unsigned int size){
    static std::wstring lastDir;
    size_t s=dest.find_last_of(L"\\/");
    if(s!=std::wstring::npos){
        std::wstring d=dest.substr(0,s);
        if(d!=lastDir){ MakeDirs(d); lastDir=d; }
    }
    HANDLE f=CreateFileW(dest.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE){
        // a previous install may still have it locked (the shell we just closed, an antivirus scan);
        // renaming a locked file aside is allowed where deleting it is not
        std::wstring old=dest+L".old";
        DeleteFileW(old.c_str()); MoveFileW(dest.c_str(),old.c_str());
        f=CreateFileW(dest.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f==INVALID_HANDLE_VALUE) return false;
    }
    bool ok=true;
    if(size){ DWORD wr=0; ok = WriteFile(f,data,size,&wr,nullptr) && wr==size; }
    CloseHandle(f);
    return ok;
}

static bool Unpack(const std::wstring& dir,std::wstring& err){
    HANDLE f=CreateFileW(SelfPath().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE){ err=L"Could not open setup to read its payload."; return false; }
    struct Closer { HANDLE h; ~Closer(){ CloseHandle(h); } } closer{f};

    LARGE_INTEGER sz{}; GetFileSizeEx(f,&sz);
    if(sz.QuadPart < (LONGLONG)sizeof(PayFooter)){ err=L"Setup is truncated."; return false; }

    PayFooter foot{};
    LARGE_INTEGER at; at.QuadPart = sz.QuadPart - (LONGLONG)sizeof(PayFooter);
    SetFilePointerEx(f,at,nullptr,FILE_BEGIN);
    DWORD rd=0;
    if(!ReadFile(f,&foot,sizeof(foot),&rd,nullptr) || rd!=sizeof(foot) ||
       memcmp(foot.magic,PAY_MAGIC,8)!=0){
        err=L"This setup has no payload attached - the download is incomplete or was modified.";
        return false;
    }

    std::vector<PayChunk> table(foot.chunks);
    at.QuadPart=(LONGLONG)foot.tableOff;
    SetFilePointerEx(f,at,nullptr,FILE_BEGIN);
    DWORD want=(DWORD)(foot.chunks*sizeof(PayChunk));
    if(!ReadFile(f,table.data(),want,&rd,nullptr) || rd!=want){ err=L"Payload index is unreadable."; return false; }

    gStubSize = foot.chunks ? table[0].off : foot.tableOff;

    DECOMPRESSOR_HANDLE dec=nullptr;
    if(!CreateDecompressor(COMPRESS_ALGORITHM_MSZIP,nullptr,&dec)){
        err=L"Windows could not start its decompressor (Cabinet.dll).";
        return false;
    }
    struct DecCloser { DECOMPRESSOR_HANDLE h; ~DecCloser(){ CloseDecompressor(h); } } dcl{dec};

    std::vector<unsigned char> comp, raw;
    int wrote=0;
    for(const PayChunk& c:table){
        comp.resize(c.comp); raw.resize(c.raw);
        at.QuadPart=(LONGLONG)c.off;
        SetFilePointerEx(f,at,nullptr,FILE_BEGIN);
        if(!ReadFile(f,comp.data(),c.comp,&rd,nullptr) || rd!=c.comp){ err=L"Payload is truncated."; return false; }
        SIZE_T got=0;
        if(!Decompress(dec,comp.data(),c.comp,raw.data(),c.raw,&got) || got!=c.raw){
            err=L"Payload did not decompress - the download is damaged."; return false;
        }
        // walk the records in this chunk
        const unsigned char* p=raw.data(); const unsigned char* end=p+raw.size();
        if((size_t)(end-p) < 4){ err=L"Malformed payload chunk."; return false; }
        unsigned int recs=*(const unsigned int*)p; p+=4;
        for(unsigned int i=0;i<recs;i++){
            if((size_t)(end-p) < 2) { err=L"Malformed payload record."; return false; }
            unsigned short plen=*(const unsigned short*)p; p+=2;
            if((size_t)(end-p) < (size_t)plen+4){ err=L"Malformed payload record."; return false; }
            std::wstring rel=U82W((const char*)p,plen); p+=plen;
            unsigned int fsz=*(const unsigned int*)p; p+=4;
            if((size_t)(end-p) < fsz){ err=L"Malformed payload record."; return false; }
            std::wstring dest=dir+L"\\"+rel;
            if(!WriteOut(dest,p,fsz)){ err=L"Could not write "+dest; return false; }
            p+=fsz;
            if((++wrote % 64)==0) Progress(wrote,(int)foot.files);
        }
    }
    Progress((int)foot.files,(int)foot.files);
    return true;
}

// Copy ourselves without the appended archive. Returns false if anything at all goes wrong, so the
// caller can fall back to a plain full copy rather than leave the user with no uninstaller.
static bool CopyStub(const std::wstring& dest){
    if(!gStubSize) return false;
    HANDLE in=CreateFileW(SelfPath().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(in==INVALID_HANDLE_VALUE) return false;
    HANDLE out=CreateFileW(dest.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(out==INVALID_HANDLE_VALUE){ CloseHandle(in); return false; }
    std::vector<unsigned char> buf(1<<20);
    unsigned long long left=gStubSize; bool ok=true;
    while(left && ok){
        DWORD want=(DWORD)(left < buf.size() ? left : buf.size()), rd=0, wr=0;
        ok = ReadFile(in,buf.data(),want,&rd,nullptr) && rd==want &&
             WriteFile(out,buf.data(),rd,&wr,nullptr) && wr==rd;
        left-=want;
    }
    CloseHandle(in); CloseHandle(out);
    if(!ok) DeleteFileW(dest.c_str());
    return ok;
}

// ---- the dialog ------------------------------------------------------------------------------
enum { ID_PATH=1001, ID_BROWSE, ID_START_MENU, ID_DESKTOP, ID_AUTOSTART, ID_SHELL, ID_GO, ID_CANCEL,
       ID_STATUS, ID_LAUNCH, ID_PROG };
static HWND hPath=nullptr,hStartMenu=nullptr,hDesktop=nullptr,hAutostart=nullptr,hShell=nullptr,
            hGo=nullptr,hCancel=nullptr,hStatus=nullptr,hLaunch=nullptr;
static HFONT gFont,gFontBold;
static bool  gUninstall=false, gDone=false;

static void SetStatus(const wchar_t* s){
    if(!hStatus) return;
    SetWindowTextW(hStatus,s); UpdateWindow(hStatus);
    // extraction runs on this thread; without a pump the window would grey out for a minute
    MSG m; while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){ TranslateMessage(&m); DispatchMessageW(&m); }
}

static void WriteRestoreBat(){
    std::wstring bat=KnownDir(FOLDERID_Desktop)+L"\\RESTORE-EXPLORER.bat";
    HANDLE f=CreateFileW(bat.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return;
    std::string t="@echo off\r\n"
        "reg delete \"HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\" /v Shell /f\r\n"
        "start explorer.exe\r\n"
        "echo Explorer is back. Sign out and in to finish.\r\n"
        "pause\r\n";
    DWORD wr; WriteFile(f,t.data(),(DWORD)t.size(),&wr,nullptr); CloseHandle(f);
}

static bool DoInstall(std::wstring dir,bool startMenu,bool desktop,bool autostart,bool asShell,
                      std::wstring& err){
    if(dir.empty()){ err=L"Choose a folder to install into."; return false; }
    while(dir.size()>3 && (dir.back()==L'\\'||dir.back()==L'/')) dir.pop_back();
    if(!MakeDirs(dir)){ err=L"Could not create "+dir; return false; }
    KillRunningShell(dir);

    SetStatus(L"Unpacking\u2026");
    if(!Unpack(dir,err)) return false;

    // the installer doubles as the uninstaller - minus the payload it no longer needs
    if(!CopyStub(dir+L"\\uninstall.exe"))
        CopyFileW(SelfPath().c_str(),(dir+L"\\uninstall.exe").c_str(),FALSE);

    std::wstring exe=dir+L"\\"+EXENAME;
    SetStatus(L"Creating shortcuts\u2026");
    if(startMenu){
        std::wstring sm=KnownDir(FOLDERID_Programs);
        if(!sm.empty()) MakeShortcut(sm+L"\\Aether.lnk",exe,dir,L"Aether shell");
    }
    if(desktop){
        std::wstring dk=KnownDir(FOLDERID_Desktop);
        if(!dk.empty()) MakeShortcut(dk+L"\\Aether.lnk",exe,dir,L"Aether shell");
    }
    if(autostart) RegSetStr(HKEY_CURRENT_USER,RUNKEY,APPNAME,L"\""+exe+L"\"");
    else          RegDelValue(HKEY_CURRENT_USER,RUNKEY,APPNAME);

    if(asShell){
        // per-user only: HKLM keeps explorer.exe, so Safe Mode and other accounts are unaffected
        RegSetStr(HKEY_CURRENT_USER,WINLOGON,L"Shell",exe);
        WriteRestoreBat();   // put the escape hatch where it can be found without a shell
    }
    SetStatus(L"Registering\u2026");
    RegSetStr  (HKEY_CURRENT_USER,UNINSTKEY,L"DisplayName",L"Aether");
    RegSetStr  (HKEY_CURRENT_USER,UNINSTKEY,L"DisplayVersion",VERSION);
    RegSetStr  (HKEY_CURRENT_USER,UNINSTKEY,L"Publisher",L"Aether");
    RegSetStr  (HKEY_CURRENT_USER,UNINSTKEY,L"InstallLocation",dir);
    RegSetStr  (HKEY_CURRENT_USER,UNINSTKEY,L"DisplayIcon",exe);
    RegSetStr  (HKEY_CURRENT_USER,UNINSTKEY,L"UninstallString",L"\""+dir+L"\\uninstall.exe\" /uninstall");
    RegSetDword(HKEY_CURRENT_USER,UNINSTKEY,L"NoModify",1);
    RegSetDword(HKEY_CURRENT_USER,UNINSTKEY,L"NoRepair",1);
    RegSetDword(HKEY_CURRENT_USER,UNINSTKEY,L"EstimatedSize",240000);   // KB
    // V1 shipped as "CaelestiaWin". Leaving its Apps & features entry behind would offer the user an
    // uninstaller for a program that is no longer there.
    RegDeleteKeyW(HKEY_CURRENT_USER,OLDKEY);
    RegDelValue(HKEY_CURRENT_USER,RUNKEY,L"CaelestiaWin");
    return true;
}

static bool DoUninstall(bool keepSettings,std::wstring& err){
    std::wstring dir=RegGetStr(HKEY_CURRENT_USER,UNINSTKEY,L"InstallLocation");
    if(dir.empty()) dir=SelfDir();
    if(dir.empty()){ err=L"Could not find the installation."; return false; }
    SetStatus(L"Stopping the shell\u2026");
    // if it is the user's shell, put Explorer back FIRST — otherwise sign-in has no shell at all.
    // Only if the shell key points at THIS installation: another copy elsewhere is not ours to undo.
    std::wstring cur=RegGetStr(HKEY_CURRENT_USER,WINLOGON,L"Shell");
    std::wstring mineExe=dir+L"\\"+EXENAME;
    if(!cur.empty() && StrStrIW(cur.c_str(),mineExe.c_str())){
        RegDelValue(HKEY_CURRENT_USER,WINLOGON,L"Shell");
        ShellExecuteW(nullptr,L"open",L"explorer.exe",nullptr,nullptr,SW_SHOWNORMAL);
    }
    KillRunningShell(dir);
    { std::wstring run=RegGetStr(HKEY_CURRENT_USER,RUNKEY,APPNAME);
      if(!run.empty() && StrStrIW(run.c_str(),mineExe.c_str())) RegDelValue(HKEY_CURRENT_USER,RUNKEY,APPNAME); }
    SetStatus(L"Removing shortcuts\u2026");
    DeleteFileW((KnownDir(FOLDERID_Programs)+L"\\Aether.lnk").c_str());
    DeleteFileW((KnownDir(FOLDERID_Desktop)+L"\\Aether.lnk").c_str());
    DeleteFileW((KnownDir(FOLDERID_Desktop)+L"\\RESTORE-EXPLORER.bat").c_str());
    SetStatus(L"Removing files\u2026");
    if(keepSettings){
        // leave config.json / presets / plugins so a reinstall picks the setup back up
        DeleteFileW((dir+L"\\"+EXENAME).c_str());
        DeleteFileW((dir+L"\\"+EXENAME+L".old").c_str());
        DeleteTree(dir+L"\\linux");
        DeleteTree(dir+L"\\assets");
        DeleteTree(dir+L"\\theme");
        DeleteTree(dir+L"\\sensors");
    } else {
        DeleteTree(dir);
    }
    RegDeleteKeyW(HKEY_CURRENT_USER,UNINSTKEY);
    // uninstall.exe is running from inside that folder: delete it after we exit
    { std::wstring me=SelfPath();
      // uninstall.exe is still locked while it runs, and SHFileOperation stops at it — so the last
      // sweep happens after we exit, and it must be recursive or the folder survives
      std::wstring cmd=L"cmd /c ping 127.0.0.1 -n 3 >nul & del \""+me+L"\"";
      if(!keepSettings) cmd += L" & rmdir /s /q \""+dir+L"\"";
      STARTUPINFOW si={sizeof(si)}; si.dwFlags=STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
      PROCESS_INFORMATION pi={};
      std::vector<wchar_t> buf(cmd.begin(),cmd.end()); buf.push_back(0);
      if(CreateProcessW(nullptr,buf.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
          CloseHandle(pi.hProcess); CloseHandle(pi.hThread); } }
    return true;
}

static HWND Mk(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,HWND parent,int id,
               HFONT font){
    HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,(HMENU)(INT_PTR)id,
                           GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);
    return c;
}

static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE: {
        gFont    =CreateFontW(-15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        gFontBold=CreateFontW(-22,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        int y=18;
        Mk(L"STATIC", gUninstall? L"Remove Aether" : L"Install Aether",
           0,24,y,460,30,h,0,gFontBold); y+=32;
        if(!gUninstall){
            Mk(L"STATIC",L"Version 1.5  \u2014  desktop, taskbar, dashboard, launcher and quick settings.",
               0,24,y,470,20,h,0,gFont); y+=28;
            Mk(L"STATIC",L"Install folder",0,24,y,120,18,h,0,gFont); y+=20;
            hPath  =Mk(L"EDIT",DefaultDir().c_str(),WS_BORDER|ES_AUTOHSCROLL,24,y,380,26,h,ID_PATH,gFont);
            Mk(L"BUTTON",L"Browse\u2026",BS_PUSHBUTTON,412,y,84,26,h,ID_BROWSE,gFont); y+=40;
            hStartMenu=Mk(L"BUTTON",L"Add a Start Menu shortcut",BS_AUTOCHECKBOX,24,y,300,22,h,ID_START_MENU,gFont); y+=26;
            hDesktop  =Mk(L"BUTTON",L"Add a desktop shortcut",BS_AUTOCHECKBOX,24,y,300,22,h,ID_DESKTOP,gFont); y+=26;
            hAutostart=Mk(L"BUTTON",L"Start Aether when I sign in",BS_AUTOCHECKBOX,24,y,340,22,h,ID_AUTOSTART,gFont); y+=26;
            hShell    =Mk(L"BUTTON",L"Replace the Windows shell (advanced)",BS_AUTOCHECKBOX,24,y,340,22,h,ID_SHELL,gFont); y+=22;
            Mk(L"STATIC",L"Starts instead of explorer.exe. RESTORE-EXPLORER.bat on your desktop undoes it.",
               0,44,y,452,40,h,0,gFont); y+=46;
            SendMessageW(hStartMenu,BM_SETCHECK,BST_CHECKED,0);
            SendMessageW(hAutostart,BM_SETCHECK,BST_CHECKED,0);
            hLaunch=Mk(L"BUTTON",L"Run Aether when setup finishes",BS_AUTOCHECKBOX,24,y,340,22,h,ID_LAUNCH,gFont);
            SendMessageW(hLaunch,BM_SETCHECK,BST_CHECKED,0); y+=30;
        } else {
            Mk(L"STATIC",L"This removes Aether and puts Explorer back if it replaced it.",
               0,24,y,470,20,h,0,gFont); y+=30;
            hShell=Mk(L"BUTTON",L"Keep my settings, presets and plugins",BS_AUTOCHECKBOX,24,y,340,22,h,ID_SHELL,gFont);
            SendMessageW(hShell,BM_SETCHECK,BST_CHECKED,0); y+=40;
        }
        // ~200 MB across 28,000 files takes long enough that a frozen window would look like a hang
        gProgWnd=Mk(PROGRESS_CLASSW,L"",0,24,y,470,12,h,ID_PROG,gFont); y+=18;
        SendMessageW(gProgWnd,PBM_SETRANGE32,0,100);
        hStatus=Mk(L"STATIC",L"",0,24,y,470,20,h,ID_STATUS,gFont);
        hGo    =Mk(L"BUTTON",gUninstall?L"Remove":L"Install",BS_DEFPUSHBUTTON,300,y+34,90,30,h,ID_GO,gFont);
        hCancel=Mk(L"BUTTON",L"Close",BS_PUSHBUTTON,404,y+34,90,30,h,ID_CANCEL,gFont);
        SetWindowPos(h,nullptr,0,0,540,y+34+30+64,SWP_NOMOVE|SWP_NOZORDER);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc=(HDC)w; SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(28,28,32));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_COMMAND: {
        int id=LOWORD(w);
        if(id==ID_CANCEL){ PostQuitMessage(0); return 0; }
        if(id==ID_BROWSE){
            BROWSEINFOW bi={}; bi.hwndOwner=h; bi.lpszTitle=L"Install Aether into";
            bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pidl=SHBrowseForFolderW(&bi);
            if(pidl){ wchar_t buf[MAX_PATH]; if(SHGetPathFromIDListW(pidl,buf)) SetWindowTextW(hPath,buf);
                      CoTaskMemFree(pidl); }
            return 0;
        }
        if(id==ID_GO){
            EnableWindow(hGo,FALSE);
            std::wstring err;
            bool ok;
            if(gUninstall){
                ok=DoUninstall(SendMessageW(hShell,BM_GETCHECK,0,0)==BST_CHECKED,err);
                if(ok){ SetStatus(L"Aether has been removed."); gDone=true; }
            } else {
                wchar_t buf[MAX_PATH]={0}; GetWindowTextW(hPath,buf,MAX_PATH);
                bool asShell=SendMessageW(hShell,BM_GETCHECK,0,0)==BST_CHECKED;
                if(asShell && MessageBoxW(h,
                        L"Replacing the Windows shell means Aether starts instead of explorer.exe "
                        L"when you sign in.\n\nIf it ever fails to start you can press Ctrl+Shift+Esc, choose "
                        L"File \u2192 Run new task, and type explorer.exe.\n\nA RESTORE-EXPLORER.bat will also be "
                        L"placed on your desktop.\n\nContinue?",
                        L"Replace the Windows shell?",MB_ICONWARNING|MB_YESNO)!=IDYES){
                    SendMessageW(hShell,BM_SETCHECK,BST_UNCHECKED,0); asShell=false;
                }
                ok=DoInstall(buf,
                             SendMessageW(hStartMenu,BM_GETCHECK,0,0)==BST_CHECKED,
                             SendMessageW(hDesktop,BM_GETCHECK,0,0)==BST_CHECKED,
                             SendMessageW(hAutostart,BM_GETCHECK,0,0)==BST_CHECKED,
                             asShell,err);
                if(ok){
                    SetStatus(L"Installed. Enjoy.");
                    gDone=true;
                    if(SendMessageW(hLaunch,BM_GETCHECK,0,0)==BST_CHECKED){
                        std::wstring dir=buf; while(!dir.empty()&&(dir.back()==L'\\')) dir.pop_back();
                        ShellExecuteW(nullptr,L"open",(dir+L"\\"+EXENAME).c_str(),nullptr,dir.c_str(),SW_SHOWNORMAL);
                    }
                }
            }
            if(!ok){ SetStatus(err.c_str()); EnableWindow(hGo,TRUE); }
            else   { SetWindowTextW(hCancel,L"Finish"); }
            return 0;
        }
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

int APIENTRY wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR cmd,int){
    SetProcessDPIAware();
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    { INITCOMMONCONTROLSEX ic={sizeof(ic),ICC_PROGRESS_CLASS}; InitCommonControlsEx(&ic); }
    gUninstall = (cmd && StrStrIW(cmd,L"/uninstall")!=nullptr);
    if(gUninstall && cmd && StrStrIW(cmd,L"/S")!=nullptr){
        std::wstring err; return DoUninstall(false,err)?0:1;   // scripted removal, no window
    }
    // silent install for scripted deployments: setup.exe /S [/D=<dir>]
    if(cmd && StrStrIW(cmd,L"/S")!=nullptr && !gUninstall){
        std::wstring dir=DefaultDir();
        if(const wchar_t* d=StrStrIW(cmd,L"/D=")) dir=d+3;
        std::wstring err;
        return DoInstall(dir,true,false,true,false,err)?0:1;
    }
    WNDCLASSEXW wc={sizeof(wc)}; wc.lpfnWndProc=WndProc; wc.hInstance=hi;
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName=L"AetherSetup";
    wc.hIcon=wc.hIconSm=LoadIcon(nullptr,IDI_APPLICATION);
    RegisterClassExW(&wc);
    HWND h=CreateWindowExW(0,wc.lpszClassName,gUninstall?L"Aether \u2014 Uninstall":L"Aether Setup",
        (WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX|WS_THICKFRAME))|WS_VISIBLE,
        CW_USEDEFAULT,CW_USEDEFAULT,540,470,nullptr,nullptr,hi,nullptr);
    if(!h) return 1;
    MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)){
        if(!IsDialogMessageW(h,&msg)){ TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return 0;
}
