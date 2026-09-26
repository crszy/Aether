// Aether - the bar.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ================================================================= bar (left vertical strip)
static const int BARWINW=300;   // window is wide so hover tooltips can draw to the right of the 66px strip
static HWND g_barHwnd=nullptr; static IDXGISwapChain1* g_barSc=nullptr; static ID3D11RenderTargetView* g_barRtv=nullptr;
static IDCompositionTarget* g_barTgt=nullptr; static IDCompositionVisual* g_barVis=nullptr; static ImGuiContext* g_ctxBar=nullptr;
// One bar PER MONITOR. The bar window spans the whole virtual screen and draws a strip on each
// monitor, so each screen gets its own reveal state, hit rect and acrylic capture.
struct BarState { float reveal=0.0f; RECT rect={0,0,0,0}; ID3D11ShaderResourceView* acrylic=nullptr; bool shown=false; };
static std::vector<BarState> g_bars;
static BarState& Bar(int mi){ if(g_bars.empty()) g_bars.resize(1);
    return g_bars[std::min(std::max(mi,0),(int)g_bars.size()-1)]; }
static bool g_thumbWanted=false;      // set by whichever monitor's bar is hovering a task button
static int  g_barMenuMon=0, g_powerMenuMon=0;   // which monitor's bar owns the open menu
// system-tray flyout: the chevron's popout. g_trayFlyAt is the chevron centre (logical bar coords)
// and g_trayFlyDir says which way the panel grows out of the strip (0 up, 1 down, 2 right, 3 left).
static bool g_trayOpen=false; static float g_trayAnim=0.0f;
static int  g_trayFlyMon=0, g_trayFlyDir=0; static ImVec2 g_trayFlyAt=ImVec2(0,0);
static void TrayFlyout(ImDrawList* dl, ImGuiIO& io, int mi, RECT& hit);   // defined next to SysFlyout
// The tray handle. `dir` is the way the panel OPENS (0 up, 1 down, 2 right, 3 left), so the arrow
// points that way while closed and flips back toward the bar once open - exactly what Plasma's
// system-tray expander does. The glyph is Breeze's own arrow-*-symbolic, i.e. the icon CachyOS
// really shows on its panel, crossfaded between the two states; the hand-drawn chevron is the
// fallback for when the icon theme is not on disk.
static void ChevronGlyph(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float openT, int dir=0){
    static const char* ARROW[4]={"arrow-up","arrow-down","arrow-right","arrow-left"};
    static const int   OPP  [4]={1,0,3,2};
    float t=std::clamp(openT,0.0f,1.0f);
    int box=(int)(r*3.2f+0.5f); if(box<12) box=12;
    bool drew=false;
    for(int pass=0;pass<2;pass++){
        float a = pass? t : 1.0f-t;
        if(a<0.004f){ drew=true; continue; }                 // fully faded out still counts as handled
        const char* nm = ARROW[pass? OPP[dir&3] : (dir&3)];
        std::string path=g_iconRoot+"\\breeze\\actions\\22\\"+nm+"-symbolic.svg";
        ImU32 tc=WithA(col,(int)(((col>>24)&0xFF)*a));
        SvgTex tx=GetSvgIcon(path,box*2,tc);
        if(!tx.srv){ drew=false; break; }
        float hw=box*0.5f, hh=box*0.5f*(tx.h/(float)(tx.w?tx.w:box));
        dl->AddImage((ImTextureID)tx.srv,V(c.x-hw,c.y-hh),V(c.x+hw,c.y+hh));
        drew=true;
    }
    if(drew) return;
    float d = 1.0f - 2.0f*t;                                  // +1 closed .. -1 open
    bool vert=(dir<2);
    if(vert){ dl->AddLine(V(c.x-r,c.y+r*0.45f*d),V(c.x,c.y-r*0.45f*d),col,1.9f);
              dl->AddLine(V(c.x,c.y-r*0.45f*d),V(c.x+r,c.y+r*0.45f*d),col,1.9f); }
    else    { dl->AddLine(V(c.x+r*0.45f*d,c.y-r),V(c.x-r*0.45f*d,c.y),col,1.9f);
              dl->AddLine(V(c.x-r*0.45f*d,c.y),V(c.x+r*0.45f*d,c.y+r),col,1.9f); }
}
// bottom-most desktop layer (shell surface + wallpaper bubble)
static HWND g_deskHwnd=nullptr; static IDXGISwapChain1* g_deskSc=nullptr; static ID3D11RenderTargetView* g_deskRtv=nullptr;
static IDCompositionTarget* g_deskTgt=nullptr; static IDCompositionVisual* g_deskVis=nullptr; static ImGuiContext* g_ctxDesk=nullptr;
static bool g_forceBar=false;   // --barshot: pin the bar open for passive screenshots
static bool g_barMenu=false; static ImVec2 g_barMenuAt=ImVec2(0,0); static float g_barMenuAnim=0.0f;
static ULONGLONG g_barMenuOpenedAt=0;   // re-arms the outside-click dismisser (see MenuOutsideClick)
static int  g_barFly=0;            // system-icon flyout: 0 none, 1 Wi-Fi, 2 Bluetooth, 3 Ethernet
static float g_barFlyAnim=0.0f; static ImVec2 g_barFlyAt=ImVec2(0,0); static int g_barFlyMon=0;
static ULONGLONG g_barFlyHoverTick=0;   // last tick the cursor was over a sys-icon OR the flyout (hover-open + grace)
static float g_barStripL=0, g_barStripR=0;   // the active monitor's bar strip left/right edge (logical) — so a
                                             // popout can start flush at the strip's LEFT edge and merge as ONE shape
static int  g_barFlyPin=0;               // a click PINS the flyout open (survives the hover grace); 0=not pinned
// tray hover-dwell: after settling on a tray icon, pop the app's OWN native context menu (Windows never
// exposes tray-menu ITEMS to us, unlike Linux SNI, so we trigger the app's real menu on hover instead).
static void* g_trayDwellKey=nullptr; static ULONGLONG g_trayDwellSince=0; static bool g_trayDwellFired=false;
static void* g_trayMenuKey=nullptr;      // which tray app's scraped menu the popout (g_barFly==5) is showing
static bool g_forceDrawer=false;   // --uishot: pin the dashboard drawer open for comparison shots
static int  g_snipPending=0;       // frames to wait before opening the snipper (lets the bar menu hide)
static void FmStart();            // file manager (defined below)
// g_drawerForceUntil: declared with the profile globals (the status editor holds the drawer open)
// Jump to a tab with a named transition. `origin` only matters for TT_ZOOM (the point it grows from).
static void GoTab(int tab,int fx,ImVec2 origin){
    if(tab==g_tab) return;
    g_tabFxFrom=g_tab; g_tab=tab; g_tabFx=fx; g_tabFxOrigin=origin; g_tabFxT=0.0f;
    g_drawerForceUntil=GetTickCount64()+4000;        // don't let the drawer close mid-transition
}
// settings app (own fullscreen overlay, opened from the bar menu / launcher ">settings")
static HWND g_setHwnd=nullptr; static IDXGISwapChain1* g_setSc=nullptr; static ID3D11RenderTargetView* g_setRtv=nullptr;
static IDCompositionTarget* g_setTgt=nullptr; static IDCompositionVisual* g_setVis=nullptr; static ImGuiContext* g_ctxSet=nullptr;
static bool g_setShow=false; static float g_setAnim=0.0f; static RECT g_setRect={0,0,0,0}; static int g_setPage=0;
// The gesture that OPENS Settings must not also be seen BY Settings. The panel's window is hidden
// until it opens, so its input layer sees the still-held opening click as a fresh press on its very
// first live frame - landing on the bar, i.e. outside the panel, i.e. straight into the click-away
// close. Guarding that on the animation being 60% done was a race: a quick click got through, a
// normal ~100ms one did not, which is exactly why it "worked when it wanted to". Arming on an
// actual button RELEASE is not a race.
static ULONGLONG g_setOpenAt=0; static bool g_setArmed=false;
static ID3D11ShaderResourceView* g_setBg=nullptr;
static float g_setPageAnim=1.0f; static int g_setPagePrev=0;   // page-change slide/fade
// auto-dim: after a spell of no input the screen fades down (Settings > Display)
static HWND g_dimHwnd=nullptr; static IDXGISwapChain1* g_dimSc=nullptr; static ID3D11RenderTargetView* g_dimRtv=nullptr;
static IDCompositionTarget* g_dimTgt=nullptr; static IDCompositionVisual* g_dimVis=nullptr; static ImGuiContext* g_ctxDim=nullptr;
static float g_dimAnim=0.0f; static bool g_forceDim=false;
static float g_preLock=0.0f;              // 0..1 fade of the pre-lock countdown overlay
static double g_preLockRemain=0.0;        // seconds left until the lock fires
static const int g_preLockSecs=6;         // countdown window before an idle-lock
static ULONGLONG g_preLockForceEnd=0;     // >0 while a manual countdown preview is running

// ---- quick-settings sidebar (right edge) globals + backend ----
static const int SIDEWINW=440;   // wide enough for the 384px quick-settings panel + its shadow
static bool g_forceSide=false;   // --qsshot: pin quick settings open for passive screenshots
static HWND g_sideHwnd=nullptr; static IDXGISwapChain1* g_sideSc=nullptr; static ID3D11RenderTargetView* g_sideRtv=nullptr;
static IDCompositionTarget* g_sideTgt=nullptr; static IDCompositionVisual* g_sideVis=nullptr; static ImGuiContext* g_ctxSide=nullptr;
static float g_sideReveal=0.0f; static RECT g_sideRect={0,0,0,0}; static ID3D11ShaderResourceView* g_sideAcrylic=nullptr;
static int   g_sideView=0;   // 0 main, 1 wifi list, 2 bluetooth list
static ULONGLONG g_sideForceUntil=0;   // bar wifi/bt click force-opens the sidebar for a bit
struct WifiNet{ std::string ssid; int signal=0; bool connected=false; bool saved=false, secured=false; int auth=0, cipher=0; std::wstring profile; };
struct BtDev{ std::string name; bool connected; };
static std::vector<WifiNet> g_wifi; static std::vector<BtDev> g_bt;
// session/power screen (fullscreen overlay, triggered by the bar power button)
static HWND g_sessHwnd=nullptr; static IDXGISwapChain1* g_sessSc=nullptr; static ID3D11RenderTargetView* g_sessRtv=nullptr;
static IDCompositionTarget* g_sessTgt=nullptr; static IDCompositionVisual* g_sessVis=nullptr; static ImGuiContext* g_ctxSess=nullptr;
static bool g_sessShow=false; static RECT g_sessRect={0,0,0,0};
static float g_sessAnim=0.0f; static ID3D11ShaderResourceView* g_sessBg=nullptr;
// lock screen (Caelestia-style, activatable so the password field gets keystrokes)
static HWND g_lockHwnd=nullptr; static IDXGISwapChain1* g_lockSc=nullptr; static ID3D11RenderTargetView* g_lockRtv=nullptr;
static IDCompositionTarget* g_lockTgt=nullptr; static IDCompositionVisual* g_lockVis=nullptr; static ImGuiContext* g_ctxLock=nullptr;
static bool g_lockShow=false; static RECT g_lockRect={0,0,0,0};
static float g_lockAnim=0.0f; static ID3D11ShaderResourceView* g_lockBg=nullptr;
static char g_lockPw[256]={0}; static int g_lockFails=0; static ULONGLONG g_lockErrUntil=0;
static ULONGLONG g_lockOpenAt=0;     // ignore the keystroke that opened it
static bool g_lockUbh=false;         // unlock-arrow hovered (set in DrawLock, read by its input block)
static bool g_lockHelloBh=false;     // Windows Hello button hovered (same split)
static bool g_helloNoAuto=false;     // --lockshot: draw the Hello row but never fire the real prompt
// Validate a plaintext password against the current interactive user via LogonUser. Returns true
// only on a confirmed match. Used to dismiss the lock overlay. Ctrl+Alt+Del is always the OS escape.
static bool LockValidatePw(const char* pw){
    if(!pw||!pw[0]) return false;
    wchar_t user[256]={0}; DWORD un=256; GetUserNameW(user,&un);
    wchar_t dom[256]={0}; DWORD dn=256; GetComputerNameW(dom,&dn);
    std::wstring wpw=U82W(pw);
    HANDLE tok=nullptr;
    BOOL ok=LogonUserW(user,L".",wpw.c_str(),LOGON32_LOGON_INTERACTIVE,LOGON32_PROVIDER_DEFAULT,&tok);
    if(ok){ if(tok)CloseHandle(tok); return true; }
    // network logon is sometimes granted when interactive is not (e.g. some policies)
    ok=LogonUserW(user,L".",wpw.c_str(),LOGON32_LOGON_NETWORK,LOGON32_PROVIDER_DEFAULT,&tok);
    if(ok){ if(tok)CloseHandle(tok); return true; }
    return false;
}
// ---- Windows Hello: the biometric unlock Caelestia does with fprint/Howdy ----------------------
// UserConsentVerifier is the whole public surface for this - it asks for face/fingerprint/PIN and
// answers verified-or-not. It never hands back a token or a password, so it CANNOT replace
// LogonUser as a credential check; it is an attestation that the person at the machine is the
// signed-in user, which is exactly what dismissing our own overlay needs (and is all Windows' own
// lock screen gets from it either).
// NOTE the desktop entry point is the INTEROP one: RequestVerificationAsync with no HWND throws
// on an unpackaged Win32 process because there is no CoreWindow to parent the prompt to.
enum { HELLO_IDLE=0, HELLO_ASKING, HELLO_OK, HELLO_FAIL };
static std::atomic<int> g_helloState{HELLO_IDLE};
static std::atomic<int> g_helloAvail{-1};        // -1 unknown, 0 no, 1 yes  (probed once, off-thread)
// CheckAvailability answers "Available" whenever a Hello CREDENTIAL exists - a PIN counts - even on
// a box with no sensor at all, where the verification then dies with ERROR_GEN_FAILURE ("a device
// attached to the system is not functioning"). Verified on this desktop: NgcSvc running, zero
// biometric devices, no camera, WbioSrvc stopped. So availability is a hint, not a promise: the
// FIRST hard error retires Hello for the session rather than leaving a button that always fails.
static std::atomic<bool> g_helloBroken{false};
static std::string g_helloBrokenWhy;
static ULONGLONG g_helloMsgUntil=0;              // how long the failure line stays up
static std::string g_helloMsg;

static const char* HelloWhyNot(int st){
    using namespace winrt::Windows::Security::Credentials::UI;
    switch((UserConsentVerifierAvailability)st){
    case UserConsentVerifierAvailability::DeviceNotPresent:      return "No Hello device on this PC";
    case UserConsentVerifierAvailability::NotConfiguredForUser:  return "Hello is not set up for this account";
    case UserConsentVerifierAvailability::DisabledByPolicy:      return "Hello is disabled by policy";
    case UserConsentVerifierAvailability::DeviceBusy:            return "Hello device is busy";
    default:                                                     return "Windows Hello unavailable";
    }
}
// Probe availability once, on a worker - CheckAvailabilityAsync talks to the biometric service and
// can take a beat, and the render loop must never wait on it.
static void HelloProbe(){
    if(g_helloAvail.load()>=0) return;
    static std::atomic<bool> running{false};
    if(running.exchange(true)) return;
    std::thread([]{
        int av=0;
        try{
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            using namespace winrt::Windows::Security::Credentials::UI;
            auto r=UserConsentVerifier::CheckAvailabilityAsync().get();
            av = (r==UserConsentVerifierAvailability::Available)? 1 : -(int)r-2;   // <0 carries the reason
        }catch(...){ av=0; }
        g_helloAvail.store(av);
        running.store(false);
    }).detach();
}
// Ask for face / fingerprint / PIN. Runs off-thread; the lock screen polls g_helloState.
static void HelloVerify(HWND owner){
    if(g_helloState.load()==HELLO_ASKING) return;
    g_helloState.store(HELLO_ASKING);
    std::thread([owner]{
        int out=HELLO_FAIL; std::string why="Hello could not verify you";
        try{
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            using namespace winrt::Windows::Security::Credentials::UI;
            auto interop=winrt::get_activation_factory<UserConsentVerifier,IUserConsentVerifierInterop>();
            winrt::hstring msg{L"Unlock Aether"};
            winrt::Windows::Foundation::IAsyncOperation<UserConsentVerificationResult> op{nullptr};
            winrt::check_hresult(interop->RequestVerificationForWindowAsync(
                owner, static_cast<HSTRING>(winrt::get_abi(msg)),
                winrt::guid_of<winrt::Windows::Foundation::IAsyncOperation<UserConsentVerificationResult>>(),
                winrt::put_abi(op)));
            auto res=op.get();
            if(res==UserConsentVerificationResult::Verified) out=HELLO_OK;
            else if(res==UserConsentVerificationResult::Canceled) why="Hello cancelled";
            else if(res==UserConsentVerificationResult::RetriesExhausted){ why="Too many Hello attempts"; g_helloBrokenWhy=why; g_helloBroken.store(true); }
            else if(res==UserConsentVerificationResult::DeviceBusy) why="Hello device is busy";
        }catch(winrt::hresult_error const& e){
            why = (e.code()==HRESULT_FROM_WIN32(ERROR_GEN_FAILURE))
                ? "No working Windows Hello device on this PC"
                : "Windows Hello failed to start";
            g_helloBrokenWhy=why; g_helloBroken.store(true);
        }catch(...){ why="Windows Hello is not available";
            g_helloBrokenWhy=why; g_helloBroken.store(true); }
        if(out!=HELLO_OK){ g_helloMsg=why; g_helloMsgUntil=GetTickCount64()+2600; }
        g_helloState.store(out);
    }).detach();
}

// SetSuspendState(TRUE,...) is the ONLY hibernate call, and it silently degrades to SLEEP when
// hibernation is turned off for the machine - so ask IsPwrHibernateAllowed first and say so rather
// than promising a hibernate the box will not do. SE_SHUTDOWN is needed either way.
static bool HibernateAllowed(){ return IsPwrHibernateAllowed()!=FALSE; }
// SetSuspendState needs SE_SHUTDOWN_NAME ENABLED in the token. Every user holds it but it is disabled by
// default, so without this the call fails and nothing happens.
static void EnableShutdownPrivilege(){
    HANDLE tok; TOKEN_PRIVILEGES tp{};
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&tok)){
        LookupPrivilegeValueW(nullptr,SE_SHUTDOWN_NAME,&tp.Privileges[0].Luid);
        tp.PrivilegeCount=1; tp.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(tok,FALSE,&tp,0,nullptr,nullptr); CloseHandle(tok); }
}
static bool DoHibernate(){
    if(!HibernateAllowed()) return false;
    EnableShutdownPrivilege();
    return SetSuspendState(TRUE,FALSE,FALSE)!=FALSE;
}
// Sleep went straight to SetSuspendState(FALSE,...) from four places without the privilege, so it only
// worked if a hibernate had happened to enable it earlier in the session - otherwise the button did nothing.
static bool DoSleep(){
    EnableShutdownPrivilege();
    bool ok=SetSuspendState(FALSE,FALSE,FALSE)!=FALSE;
    if(!ok) AetherLog("sleep: SetSuspendState failed (error %lu)",GetLastError());
    return ok;
}

// The ONE lock entry point. Every "lock" trigger (session menu, launcher, >lock command, account
// pane) routes through here so the g_useLockScreen preference is always honoured. When on, shows
// the Caelestia overlay; when off, the native Windows lock.
// The lock screen freezes a BLURRED SCREEN GRAB as its backdrop. Every way of reaching it goes
// through another full-screen overlay first — `>lock` through the launcher, the Lock button through
// the session screen — so grabbing the screen on the frame it opens photographed our own dark scrim
// and the "wallpaper showing through" was a black sheet with the launcher/session faintly blurred
// into it. So arm it instead: hide the overlay that summoned it, let DWM compose a clean frame, and
// only then capture and show. g_lockArm counts those frames down in the render loop.
static int g_lockArm=0;
// With the shell's own lock screen enabled, IT is the lock - Windows' LockWorkStation is never called.
// So anything that can still open on top of it (the launcher via Alt+Space or `-s launcher`, the
// dashboard from a screen-edge hover, the dock, Alt+Tab, Settings) walks straight past the password.
// Seen for real: `-s launcher` drew the app list over the lock screen. Everything checks this.
static bool ShellLocked(){ return g_lockShow || g_lockArm>0; }
static void DoLock(){
    if(g_useLockScreen){ g_lockPw[0]=0; g_lockFails=0; g_lockOpenAt=GetTickCount64(); g_lockArm=3;
        g_helloState.store(HELLO_IDLE); g_helloMsgUntil=0; HelloProbe(); }
    else LockWorkStation();
}
// notifications overlay (top-right)
static const int NOTIFWINW=400;
static HWND g_notifHwnd=nullptr; static IDXGISwapChain1* g_notifSc=nullptr; static ID3D11RenderTargetView* g_notifRtv=nullptr;
static IDCompositionTarget* g_notifTgt=nullptr; static IDCompositionVisual* g_notifVis=nullptr; static ImGuiContext* g_ctxNotif=nullptr;
static float g_notifReveal=0.0f; static RECT g_notifRect={0,0,0,0};
// "Now Playing" media flyout (Medal-style pop in/out, top-right)
static const int MEDIAWINW=420, MEDIAWINH=330;   // hosts the toast stack (now playing + recording)
static HWND g_medHwnd=nullptr; static IDXGISwapChain1* g_medSc=nullptr; static ID3D11RenderTargetView* g_medRtv=nullptr;
static IDCompositionTarget* g_medTgt=nullptr; static IDCompositionVisual* g_medVis=nullptr; static ImGuiContext* g_ctxMed=nullptr;
static float g_medAnim=0.0f; static bool g_medShowing=false;

static float GetVolume(){ float v=0; IMMDeviceEnumerator* e=nullptr;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),(void**)&e))){
        IMMDevice* d=nullptr; if(SUCCEEDED(e->GetDefaultAudioEndpoint(eRender,eConsole,&d))){
            IAudioEndpointVolume* ev=nullptr; if(SUCCEEDED(d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,(void**)&ev))){ ev->GetMasterVolumeLevelScalar(&v); ev->Release(); } d->Release(); } e->Release(); }
    return v; }
static void SetVolume(float v){ v=std::clamp(v,0.0f,1.0f); IMMDeviceEnumerator* e=nullptr;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),(void**)&e))){
        IMMDevice* d=nullptr; if(SUCCEEDED(e->GetDefaultAudioEndpoint(eRender,eConsole,&d))){
            IAudioEndpointVolume* ev=nullptr; if(SUCCEEDED(d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,(void**)&ev))){ ev->SetMasterVolumeLevelScalar(v,nullptr); ev->Release(); } d->Release(); } e->Release(); } }
static bool GetBrightness(int& cur,int& mx){ POINT p={g_mx+10,g_my+10}; HMONITOR mon=MonitorFromPoint(p,MONITOR_DEFAULTTOPRIMARY);
    DWORD n=0; if(!GetNumberOfPhysicalMonitorsFromHMONITOR(mon,&n)||!n)return false;
    std::vector<PHYSICAL_MONITOR> pm(n); if(!GetPhysicalMonitorsFromHMONITOR(mon,n,pm.data()))return false;
    DWORD mn=0,cu=0,ma=0; bool ok=GetMonitorBrightness(pm[0].hPhysicalMonitor,&mn,&cu,&ma)!=0; cur=(int)cu; mx=(int)ma;
    DestroyPhysicalMonitors(n,pm.data()); return ok; }
static void SetBrightness(int val){ POINT p={g_mx+10,g_my+10}; HMONITOR mon=MonitorFromPoint(p,MONITOR_DEFAULTTOPRIMARY);
    DWORD n=0; if(!GetNumberOfPhysicalMonitorsFromHMONITOR(mon,&n)||!n)return;
    std::vector<PHYSICAL_MONITOR> pm(n); if(!GetPhysicalMonitorsFromHMONITOR(mon,n,pm.data()))return;
    for(DWORD i=0;i<n;i++) SetMonitorBrightness(pm[i].hPhysicalMonitor,val);
    DestroyPhysicalMonitors(n,pm.data()); }
static bool IsLightTheme(){ DWORD v=1,sz=4; RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&v,&sz); return v!=0; }
static void SetLightTheme(bool light){ HKEY k;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",0,KEY_SET_VALUE,&k)==ERROR_SUCCESS){
        DWORD v=light?1:0; RegSetValueExW(k,L"AppsUseLightTheme",0,REG_DWORD,(BYTE*)&v,4); RegSetValueExW(k,L"SystemUsesLightTheme",0,REG_DWORD,(BYTE*)&v,4); RegCloseKey(k);
        SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,(LPARAM)L"ImmersiveColorSet",SMTO_ABORTIFHUNG,100,nullptr); } }

// ---- per-application volume mixer (Core Audio sessions) --------------------------------
// "a volume checker for apps": enumerate the render endpoint's audio sessions, one row per app.
struct AppVol{ DWORD pid=0; std::string name; float vol=1.0f; bool mute=false; float peak=0;
               ID3D11ShaderResourceView* icon=nullptr; };
// RefreshMixer enumerates audio sessions over COM and resolves shell icons. Measured at up to 45ms
// in a single frame, fired every 700ms from the quick-settings draw - a visible hitch every time the
// panel was open. It runs on a worker now. MakeTextureBGRA only touches ID3D11Device (free-threaded
// for resource creation) and never the device CONTEXT, so building the icon textures there is safe.
static std::vector<AppVol> g_mixer;                  // guarded by g_mixerMtx
static std::unordered_map<DWORD,ID3D11ShaderResourceView*> g_mixIcons;   // ditto
static std::mutex g_mixerMtx;
static std::atomic<bool> g_mixBusy{false};

static std::wstring ProcExePath(DWORD pid){
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid); if(!h) return L"";
    wchar_t buf[MAX_PATH]={0}; DWORD n=MAX_PATH;
    std::wstring out; if(QueryFullProcessImageNameW(h,0,buf,&n)) out=buf;
    CloseHandle(h); return out;
}
// ---- stable identity for a tray icon, so "always show this one" survives a restart -------------
// HWND and the SysTrayIcon* both change every launch, and uID alone collides across apps. A GUID is
// the app's own promise of stability, so prefer it; otherwise fall back to the owner's exe basename
// plus uID, which is stable for every tray app that does not renumber its icons.
static std::string TrayKey(const SysTrayIcon& s){
    if(s.hasGuid){ char b[48]; const GUID& g=s.guid;
        snprintf(b,sizeof(b),"{%08lX-%04X-%04X-%02X%02X}",(unsigned long)g.Data1,g.Data2,g.Data3,g.Data4[0],g.Data4[1]);
        return b; }
    DWORD pid=0; if(s.hwnd) GetWindowThreadProcessId(s.hwnd,&pid);
    std::wstring p=pid?ProcExePath(pid):L"";
    size_t sl=p.find_last_of(L"\\/"); std::wstring base = (sl==std::wstring::npos)? p : p.substr(sl+1);
    for(auto&ch:base) ch=towlower(ch);
    char b[24]; snprintf(b,sizeof(b),"#%u",s.id);
    return W2U8(base)+b;
}
// cached: see the note on SysTrayIcon::key
static const std::string& TrayKeyCached(const SysTrayIcon& s){
    if(!s.keyed){ s.key=TrayKey(s); s.keyed=true; }
    return s.key;
}
static bool TrayIsShown(const SysTrayIcon& s){
    if(!g_trayCollapse) return true;                    // collapse off: everything sits in the bar
    if(g_trayShown.empty()) return false;               // nothing promoted: skip the key entirely
    const std::string& k=TrayKeyCached(s);
    for(auto& e:g_trayShown) if(e==k) return true;
    return false;
}
static void TrayToggleShown(const SysTrayIcon& s){
    std::string k=TrayKeyCached(s);
    for(size_t i=0;i<g_trayShown.size();i++) if(g_trayShown[i]==k){ g_trayShown.erase(g_trayShown.begin()+i); SaveConfig(); return; }
    g_trayShown.push_back(k); SaveConfig();
}
// run `fn` against the session control for `pid` (nullptr pid = every session)
template<typename F> static void ForEachSession(F fn){
    IMMDeviceEnumerator* e=nullptr;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),(void**)&e))||!e) return;
    IMMDevice* d=nullptr;
    if(SUCCEEDED(e->GetDefaultAudioEndpoint(eRender,eConsole,&d))&&d){
        IAudioSessionManager2* sm=nullptr;
        if(SUCCEEDED(d->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,(void**)&sm))&&sm){
            IAudioSessionEnumerator* se=nullptr;
            if(SUCCEEDED(sm->GetSessionEnumerator(&se))&&se){
                int n=0; se->GetCount(&n);
                for(int i=0;i<n;i++){
                    IAudioSessionControl* sc=nullptr; if(FAILED(se->GetSession(i,&sc))||!sc) continue;
                    IAudioSessionControl2* sc2=nullptr;
                    if(SUCCEEDED(sc->QueryInterface(__uuidof(IAudioSessionControl2),(void**)&sc2))&&sc2){
                        fn(sc2); sc2->Release(); }
                    sc->Release();
                }
                se->Release();
            }
            sm->Release();
        }
        d->Release();
    }
    e->Release();
}
static void MixerScan(){          // worker only
    std::vector<AppVol> out;
    ForEachSession([&](IAudioSessionControl2* sc2){
        DWORD pid=0; sc2->GetProcessId(&pid);
        if(!pid || sc2->IsSystemSoundsSession()==S_OK) return;
        AudioSessionState st=AudioSessionStateExpired; sc2->GetState(&st);
        if(st==AudioSessionStateExpired) return;
        AppVol a; a.pid=pid;
        LPWSTR disp=nullptr;                                   // prefer the app's own label
        if(SUCCEEDED(sc2->GetDisplayName(&disp))&&disp){ if(disp[0]) a.name=W2U8(disp); CoTaskMemFree(disp); }
        if(a.name.empty()){                                    // else the exe's base name
            std::wstring p=ProcExePath(pid); size_t s=p.find_last_of(L"\\/");
            std::wstring base=(s==std::wstring::npos)?p:p.substr(s+1);
            size_t dot=base.find_last_of(L'.'); if(dot!=std::wstring::npos) base=base.substr(0,dot);
            a.name=W2U8(base); }
        if(a.name.empty()) return;
        ISimpleAudioVolume* sv=nullptr;
        if(SUCCEEDED(sc2->QueryInterface(__uuidof(ISimpleAudioVolume),(void**)&sv))&&sv){
            sv->GetMasterVolume(&a.vol); BOOL m=FALSE; sv->GetMute(&m); a.mute=m!=FALSE; sv->Release(); }
        IAudioMeterInformation* mi=nullptr;                    // live level, for the "checker" bar
        if(SUCCEEDED(sc2->QueryInterface(__uuidof(IAudioMeterInformation),(void**)&mi))&&mi){
            mi->GetPeakValue(&a.peak); mi->Release(); }
        for(auto& e2:out) if(e2.pid==pid) return;              // one row per process
        out.push_back(std::move(a));
    });
    for(auto& a:out){
        ID3D11ShaderResourceView* cached=nullptr; bool have=false;
        { std::lock_guard<std::mutex> lk(g_mixerMtx);
          auto it=g_mixIcons.find(a.pid); if(it!=g_mixIcons.end()){ cached=it->second; have=true; } }
        if(have){ a.icon=cached; continue; }
        std::wstring p=ProcExePath(a.pid); ID3D11ShaderResourceView* t=nullptr;
        if(!p.empty()){ SHFILEINFOW sfi={};
            if(SHGetFileInfoW(p.c_str(),0,&sfi,sizeof(sfi),SHGFI_ICON|SHGFI_LARGEICON)){
                t=IconTex(sfi.hIcon,32); if(sfi.hIcon)DestroyIcon(sfi.hIcon); } }
        { std::lock_guard<std::mutex> lk(g_mixerMtx); g_mixIcons[a.pid]=t; }
        a.icon=t;
    }
    std::sort(out.begin(),out.end(),[](const AppVol&a,const AppVol&b){ return a.name<b.name; });
    { std::lock_guard<std::mutex> lk(g_mixerMtx); g_mixer=std::move(out); }
}
// what the UI calls: kicks a worker and returns immediately
static void RefreshMixer(){
    if(g_mixBusy.exchange(true)) return;
    std::thread([]{
        try { CoInitializeEx(nullptr,COINIT_MULTITHREADED); MixerScan(); CoUninitialize(); } catch(...) {}
        g_mixBusy.store(false);
    }).detach();
}
static void SetAppVolumeSync(DWORD pid,float v,bool setMute,bool mute){
    ForEachSession([&](IAudioSessionControl2* sc2){
        DWORD p=0; sc2->GetProcessId(&p); if(p!=pid) return;
        ISimpleAudioVolume* sv=nullptr;
        if(SUCCEEDED(sc2->QueryInterface(__uuidof(ISimpleAudioVolume),(void**)&sv))&&sv){
            if(setMute) sv->SetMute(mute?TRUE:FALSE,nullptr); else sv->SetMasterVolume(std::clamp(v,0.0f,1.0f),nullptr);
            sv->Release(); }
    });
}
// clicking a slider must not block the shell while every audio session is walked
static void SetAppVolume(DWORD pid,float v,bool setMute,bool mute){
    std::thread([pid,v,setMute,mute]{
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        SetAppVolumeSync(pid,v,setMute,mute);
        CoUninitialize();
    }).detach();
}

// ---- microphone mute (Core Audio capture endpoint) ----
static bool GetMicMute(bool& present){ present=false; BOOL m=FALSE; IMMDeviceEnumerator* e=nullptr;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),(void**)&e))){
        IMMDevice* d=nullptr; if(SUCCEEDED(e->GetDefaultAudioEndpoint(eCapture,eConsole,&d))&&d){ present=true;
            IAudioEndpointVolume* ev=nullptr; if(SUCCEEDED(d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,(void**)&ev))){ ev->GetMute(&m); ev->Release(); } d->Release(); } e->Release(); }
    return m!=FALSE; }
static void SetMicMute(bool mute){ IMMDeviceEnumerator* e=nullptr;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),(void**)&e))){
        IMMDevice* d=nullptr; if(SUCCEEDED(e->GetDefaultAudioEndpoint(eCapture,eConsole,&d))&&d){
            IAudioEndpointVolume* ev=nullptr; if(SUCCEEDED(d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,(void**)&ev))){ ev->SetMute(mute?TRUE:FALSE,nullptr); ev->Release(); } d->Release(); } e->Release(); } }
static bool g_micPresent=false, g_micMuted=false;

// ---- utilities: keep-awake, screen recorder, recordings folder ----
static bool g_keepAwake=false;
static void ApplyKeepAwake(){
    SetThreadExecutionState(g_keepAwake ? (ES_CONTINUOUS|ES_SYSTEM_REQUIRED|ES_DISPLAY_REQUIRED) : ES_CONTINUOUS);
}
// ---- what the recording toast reports -------------------------------------------------------
static ULONGLONG g_recStart=0;          // when this take began
static std::string g_recMonName;        // which display Windows is capturing
static bool  g_recAudio=false;          // is audio being captured
static std::string g_recAudioWhat;      // "system + microphone" / "system audio" / "no audio"
static std::string g_recDir;            // where the file lands
static std::string CapturesDir(){
    wchar_t p[MAX_PATH]={0};
    if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_MYVIDEO,nullptr,0,p))){
        std::wstring d=std::wstring(p)+L"\\Captures";
        if(GetFileAttributesW(d.c_str())!=INVALID_FILE_ATTRIBUTES) return W2U8(d);
        return W2U8(std::wstring(p));
    }
    return "";
}
// Game DVR's own settings say whether the take will have sound
static void ReadRecordingSettings(){
    auto dw=[](HKEY root,const wchar_t* sub,const wchar_t* val,DWORD def)->DWORD{
        DWORD v=def, sz=sizeof(v);
        if(RegGetValueW(root,sub,val,RRF_RT_REG_DWORD,nullptr,&v,&sz)!=ERROR_SUCCESS) return def;
        return v; };
    DWORD sys = dw(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",L"AudioCaptureEnabled",1);
    DWORD mic = dw(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",L"MicrophoneCaptureEnabled",0);
    g_recAudio = sys!=0 || mic!=0;
    g_recAudioWhat = (sys&&mic)? "system audio + microphone" : sys? "system audio" : mic? "microphone only" : "no audio";
    g_recDir = CapturesDir();
    // the display Windows will capture is the one holding the window you are recording
    POINT cp; GetCursorPos(&cp);
    int mi=MonIndexAt(cp);
    const RECT& r=MonRect(mi);
    char b[160];
    snprintf(b,160,"%s (%ldx%ld)", (mi>=0&&mi<(int)g_mons.size())? g_mons[mi].dev.c_str() : "display",
             r.right-r.left, r.bottom-r.top);
    g_recMonName=b;
}
static void ShowRecordingToast();   // fwd (defined with the toast host)
// Windows' own capture engine owns screen recording; drive it with its public Win+Alt+R shortcut
// rather than shipping an encoder. Toggling it flips our indicator too.
static void ToggleRecording(){
    INPUT in[6]={}; for(auto&i:in) i.type=INPUT_KEYBOARD;
    in[0].ki.wVk=VK_LWIN; in[1].ki.wVk=VK_MENU; in[2].ki.wVk='R';
    in[3].ki.wVk='R';        in[3].ki.dwFlags=KEYEVENTF_KEYUP;
    in[4].ki.wVk=VK_MENU;    in[4].ki.dwFlags=KEYEVENTF_KEYUP;
    in[5].ki.wVk=VK_LWIN;    in[5].ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(6,in,sizeof(INPUT)); g_recording=!g_recording;
    if(g_recording){ ReadRecordingSettings(); g_recStart=GetTickCount64(); }
    ShowRecordingToast();
}
static void OpenRecordings(){
    wchar_t p[MAX_PATH]={0};
    if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_MYVIDEO,nullptr,0,p))){
        std::wstring d=std::wstring(p)+L"\\Captures";
        if(GetFileAttributesW(d.c_str())==INVALID_FILE_ATTRIBUTES) d=p;
        AetherShellExec(nullptr,L"open",d.c_str(),nullptr,nullptr,SW_SHOWNORMAL); }
}

// ---- OSD (on-screen volume popup, driven by a Core Audio change callback) ----
static const int OSDW=120, OSDH=380;   // Caelestia: a tall slider coming out of the right border
static HWND g_osdHwnd=nullptr; static IDXGISwapChain1* g_osdSc=nullptr; static ID3D11RenderTargetView* g_osdRtv=nullptr;
static IDCompositionTarget* g_osdTgt=nullptr; static IDCompositionVisual* g_osdVis=nullptr; static ImGuiContext* g_ctxOsd=nullptr;
static RECT g_osdRect={0,0,0,0};                       // always empty -> OSD is click-through
static volatile float g_osdVol=0; static volatile bool g_osdMuted=false; static volatile ULONGLONG g_osdShowUntil=0;
static volatile int g_osdMode=0;      // 0 = volume, 1 = brightness
static volatile float g_osdBrt=0;     // brightness 0..1 for mode 1
static HWND g_osdWake=nullptr;
// pop the OSD showing a brightness level (called when brightness is changed from the shell)
static void OsdShowBrightness(float v){
    g_osdMode=1; g_osdBrt=std::clamp(v,0.0f,1.0f); g_osdShowUntil=GetTickCount64()+1600;
    if(g_osdWake) PostMessageW(g_osdWake,WM_NULL,0,0);
}
struct VolCb : public IAudioEndpointVolumeCallback {
    LONG rc=1;
    HRESULT __stdcall QueryInterface(REFIID iid,void**p) override {
        if(IsEqualGUID(iid,__uuidof(IUnknown))||IsEqualGUID(iid,__uuidof(IAudioEndpointVolumeCallback))){ *p=static_cast<IAudioEndpointVolumeCallback*>(this); AddRef(); return S_OK; }
        *p=nullptr; return E_NOINTERFACE; }
    ULONG __stdcall AddRef() override { return InterlockedIncrement(&rc); }
    ULONG __stdcall Release() override { LONG x=InterlockedDecrement(&rc); if(!x)delete this; return x; }
    HRESULT __stdcall OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA d) override {
        g_osdMode=0; g_osdVol=d->fMasterVolume; g_osdMuted=d->bMuted; g_osdShowUntil=GetTickCount64()+1600;
        if(g_osdWake) PostMessageW(g_osdWake,WM_NULL,0,0); return S_OK; }
};
static IAudioEndpointVolume* g_epv=nullptr; static VolCb* g_volCb=nullptr;
static void InitOSD(){
    IMMDeviceEnumerator* e=nullptr;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),(void**)&e))){
        IMMDevice* d=nullptr; if(SUCCEEDED(e->GetDefaultAudioEndpoint(eRender,eConsole,&d))){
            if(SUCCEEDED(d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,(void**)&g_epv))){
                g_volCb=new VolCb(); g_epv->RegisterControlChangeNotify(g_volCb); } d->Release(); } e->Release(); } }

// ---- suppress the native Windows volume/brightness flyout (they share one shell window:
//      class "NativeHWNDHost" hosting a "DirectUIHWND"). Move it off-screen when it shows. ----
static bool g_suppressFlyout=true; static HWINEVENTHOOK g_flyoutHook=nullptr;
static bool IsNativeFlyout(HWND h){ if(!h)return false; wchar_t cls[64]={0}; GetClassNameW(h,cls,64);
    if(wcscmp(cls,L"NativeHWNDHost")!=0) return false;
    return FindWindowExW(h,nullptr,L"DirectUIHWND",nullptr)!=nullptr; }
static void ShoveOffscreen(HWND h){ SetWindowPos(h,nullptr,-32000,-32000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_ASYNCWINDOWPOS); }
// Windows' own toast popups: a CoreWindow owned by ShellExperienceHost, titled "New notification".
// We only hide them once OUR notifications module is actually reading toasts, otherwise suppressing
// them would simply lose the notification.
static bool IsNativeToast(HWND h){ if(!h)return false; wchar_t cls[80]={0}; GetClassNameW(h,cls,80);
    if(wcscmp(cls,L"Windows.UI.Core.CoreWindow")!=0) return false;
    wchar_t t[128]={0}; GetWindowTextW(h,t,127);
    return wcsstr(t,L"New notification")!=nullptr || wcsstr(t,L"Notification Center")!=nullptr; }
static void CALLBACK FlyoutWinEvent(HWINEVENTHOOK,DWORD,HWND h,LONG idObj,LONG idChild,DWORD,DWORD){
    if(idObj!=OBJID_WINDOW || idChild!=CHILDID_SELF) return;
    if(g_suppressFlyout && IsNativeFlyout(h)) ShoveOffscreen(h);
    if(g_suppressToasts && g_notifAllowed && IsNativeToast(h)) ShowWindowAsync(h,SW_HIDE);
}
static void SweepNativeFlyout(){   // catch one already on-screen
    HWND h=nullptr; while((h=FindWindowExW(nullptr,h,L"NativeHWNDHost",nullptr))!=nullptr){
        if(FindWindowExW(h,nullptr,L"DirectUIHWND",nullptr)){ RECT r; if(GetWindowRect(h,&r)&&r.left>-30000) ShoveOffscreen(h); } } }

// ---- Wi-Fi (WLAN API) + Bluetooth (Bluetooth API) enumeration ----
static void RadioToggleAsync(int kind);   // fwd (SidebarV2.h)
#include "src/services/Wifi.h"   // scan / connect / disconnect / forget, off the render thread
#include "src/modules/overview/Overview.h"   // the 3-D workspace overview (cube / plane)
static void RefreshBt(){
    g_bt.clear();
    BLUETOOTH_DEVICE_SEARCH_PARAMS sp={sizeof(sp)}; sp.fReturnAuthenticated=TRUE; sp.fReturnRemembered=TRUE; sp.fReturnConnected=TRUE; sp.fReturnUnknown=FALSE; sp.fIssueInquiry=FALSE; sp.cTimeoutMultiplier=1;
    BLUETOOTH_DEVICE_INFO di={sizeof(di)};
    HBLUETOOTH_DEVICE_FIND f=BluetoothFindFirstDevice(&sp,&di);
    if(f){ do{ BtDev d; d.name=W2U8(di.szName); d.connected=di.fConnected!=0; if(!d.name.empty())g_bt.push_back(d); ZeroMemory(&di,sizeof(di)); di.dwSize=sizeof(di); }while(BluetoothFindNextDevice(f,&di)); BluetoothFindDeviceClose(f); }
    std::sort(g_bt.begin(),g_bt.end(),[](const BtDev&a,const BtDev&b){ if(a.connected!=b.connected)return a.connected; return a.name<b.name; });
}
struct EthDev{ std::string name; bool up; };
static std::vector<EthDev> g_eth;
static void RefreshEth(){
    g_eth.clear();
    ULONG sz=0; GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,nullptr,&sz);
    if(!sz) return; std::vector<BYTE> buf(sz);
    auto* aa=(IP_ADAPTER_ADDRESSES*)buf.data();
    if(GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,aa,&sz)!=NO_ERROR) return;
    for(auto* p=aa;p;p=p->Next){
        if(p->IfType!=IF_TYPE_ETHERNET_CSMACD) continue;
        EthDev d; d.name=p->FriendlyName? W2U8(p->FriendlyName):std::string(); d.up=(p->OperStatus==IfOperStatusUp);
        if(!d.name.empty()) g_eth.push_back(d);
    }
    std::sort(g_eth.begin(),g_eth.end(),[](const EthDev&a,const EthDev&b){ if(a.up!=b.up)return a.up; return a.name<b.name; });
}


// ---- VPN (the Wireguard-provider slot Caelestia has and we did not) -----------------------------
// Windows' own VPN story is RAS: the phonebook holds the entries, RasEnumConnections says which are
// up. Dialling one may need credentials/MFA, and RasDial with a credential prompt is a whole modal
// UI we do not own - so CONNECT hands off to rasdial/rasphone (which Windows already knows how to
// prompt for) while DISCONNECT is done properly in-process with RasHangUp, which needs nothing.
struct VpnEntry{ std::wstring name; bool connected=false; };
static std::vector<VpnEntry> g_vpn;             // render-thread copy, swapped in from the worker
static std::mutex g_vpnMtx;
static bool g_vpnOn=false;            // any VPN up right now (drives the quick-settings chip)
static ULONGLONG g_vpnChecked=0;
static std::atomic<bool> g_vpnBusy{false};

// The blocking half. ONLY ever called from a worker thread.
static void VpnScan(std::vector<VpnEntry>& out,bool& anyUp){
    out.clear(); anyUp=false;
    // which connections are currently up
    std::vector<std::wstring> live;
    { DWORD sz=sizeof(RASCONNW), n=0; std::vector<BYTE> buf(sz);
      auto* c=(RASCONNW*)buf.data(); c->dwSize=sizeof(RASCONNW);
      DWORD r=RasEnumConnectionsW(c,&sz,&n);
      if(r==ERROR_BUFFER_TOO_SMALL && sz){ buf.assign(sz,0); c=(RASCONNW*)buf.data(); c->dwSize=sizeof(RASCONNW);
          r=RasEnumConnectionsW(c,&sz,&n); }
      if(r==ERROR_SUCCESS) for(DWORD i=0;i<n;i++) live.push_back(((RASCONNW*)buf.data())[i].szEntryName);
    }
    // every phonebook entry, filtered to the VPN ones (the book also holds dial-up/broadband)
    DWORD sz=sizeof(RASENTRYNAMEW), n=0; std::vector<BYTE> buf(sz);
    auto* e=(RASENTRYNAMEW*)buf.data(); e->dwSize=sizeof(RASENTRYNAMEW);
    DWORD r=RasEnumEntriesW(nullptr,nullptr,e,&sz,&n);
    if(r==ERROR_BUFFER_TOO_SMALL && sz){ buf.assign(sz,0); e=(RASENTRYNAMEW*)buf.data(); e->dwSize=sizeof(RASENTRYNAMEW);
        r=RasEnumEntriesW(nullptr,nullptr,e,&sz,&n); }
    if(r==ERROR_SUCCESS){
        for(DWORD i=0;i<n;i++){
            const wchar_t* nm=((RASENTRYNAMEW*)buf.data())[i].szEntryName;
            RASENTRYW re{}; re.dwSize=sizeof(re); DWORD rsz=sizeof(re);
            if(RasGetEntryPropertiesW(nullptr,nm,&re,&rsz,nullptr,nullptr)!=ERROR_SUCCESS) continue;
            if(re.dwType!=RASET_Vpn) continue;
            VpnEntry v; v.name=nm;
            for(auto& l:live) if(_wcsicmp(l.c_str(),nm)==0){ v.connected=true; break; }
            if(v.connected) anyUp=true;
            out.push_back(std::move(v));
        }
    }
    std::sort(out.begin(),out.end(),[](const VpnEntry&a,const VpnEntry&b){
        if(a.connected!=b.connected) return a.connected; return _wcsicmp(a.name.c_str(),b.name.c_str())<0; });
}
// What the UI calls: kicks a worker if one is not already running and returns immediately. The
// panel keeps drawing last known state until the worker lands, which is what you want anyway -
// a chip that blanks for a moment while it re-reads is worse than a slightly stale chip.
static void RefreshVpn(){
    if(g_vpnBusy.exchange(true)) return;
    g_vpnChecked=GetTickCount64();
    std::thread([]{
        std::vector<VpnEntry> tmp; bool up=false;
        try { VpnScan(tmp,up); } catch(...) {}
        { std::lock_guard<std::mutex> lk(g_vpnMtx); g_vpn.swap(tmp); g_vpnOn=up; }
        g_vpnChecked=GetTickCount64();
        g_vpnBusy.store(false);
    }).detach();
}
static void VpnConnect(const std::wstring& name){
    std::wstring args=L"\"" + name + L"\"";
    AetherShellExec(nullptr,L"open",L"rasdial.exe",args.c_str(),nullptr,SW_HIDE);
}
static void VpnDisconnect(const std::wstring& name){
    DWORD sz=sizeof(RASCONNW), n=0; std::vector<BYTE> buf(sz);
    auto* c=(RASCONNW*)buf.data(); c->dwSize=sizeof(RASCONNW);
    DWORD r=RasEnumConnectionsW(c,&sz,&n);
    if(r==ERROR_BUFFER_TOO_SMALL && sz){ buf.assign(sz,0); c=(RASCONNW*)buf.data(); c->dwSize=sizeof(RASCONNW);
        r=RasEnumConnectionsW(c,&sz,&n); }
    if(r!=ERROR_SUCCESS) return;
    auto* arr=(RASCONNW*)buf.data();
    for(DWORD i=0;i<n;i++)
        if(name.empty() || _wcsicmp(arr[i].szEntryName,name.c_str())==0) RasHangUpW(arr[i].hrasconn);
}
// The chip's one-click behaviour: down if anything is up, otherwise dial the single entry. With
// several entries there is nothing sensible to pick, so it opens the list instead.
static void VpnQuickToggle(){
    // decide from the state we already have, and do everything that can block on a worker
    bool up; size_t n;
    { std::lock_guard<std::mutex> lk(g_vpnMtx); up=g_vpnOn; n=g_vpn.size(); }
    if(up){ std::thread([]{ VpnDisconnect(L""); }).detach(); RefreshVpn(); return; }
    if(n==1){ std::wstring nm; { std::lock_guard<std::mutex> lk(g_vpnMtx); nm=g_vpn[0].name; }
              std::thread([nm]{ VpnConnect(nm); }).detach(); return; }
    if(n==0){ AetherShellExec(nullptr,L"open",L"ms-settings:network-vpn",nullptr,nullptr,SW_SHOWNORMAL); return; }
    g_sideView=6; g_sideForceUntil=GetTickCount64()+4000;
}
static void EthIcon2(ImDrawList* dl,ImVec2 c,ImU32 col){
    if(ThemedSym(dl,c,10.0f,col,"network-wired-activated")) return;
    dl->AddRect(V(c.x-9,c.y-2),V(c.x+9,c.y+7),col,2,0,1.8f);
    dl->AddLine(V(c.x,c.y-2),V(c.x,c.y-9),col,1.8f);
    dl->AddLine(V(c.x-7,c.y-9),V(c.x+7,c.y-9),col,1.8f);
}
static void LockIcon2(ImDrawList* dl,ImVec2 c,ImU32 col){
    if(ThemedSym(dl,c,10.0f,col,"system-lock-screen")) return;
    dl->AddRectFilled(V(c.x-7,c.y-1),V(c.x+7,c.y+9),col,2);
    dl->PathArcTo(V(c.x,c.y-1),5,3.1416f,6.2832f,12); dl->PathStroke(col,0,2.0f);
}
static void VpnIcon(ImDrawList* dl,ImVec2 c,ImU32 col){
    if(ThemedSym(dl,c,10.0f,col,"network-vpn")) return;
    if(ThemedSym(dl,c,10.0f,col,"security-high")) return;
    // shield with a keyhole
    dl->PathLineTo(V(c.x-8,c.y-7)); dl->PathLineTo(V(c.x,c.y-10)); dl->PathLineTo(V(c.x+8,c.y-7));
    dl->PathLineTo(V(c.x+8,c.y+1)); dl->PathLineTo(V(c.x,c.y+10)); dl->PathLineTo(V(c.x-8,c.y+1));
    dl->PathStroke(col,ImDrawFlags_Closed,1.9f);
    dl->AddCircleFilled(V(c.x,c.y-2),2.2f,col);
    dl->AddLine(V(c.x,c.y-1),V(c.x,c.y+4),col,1.8f);
}

static void WifiIcon(ImDrawList* dl, ImVec2 c, ImU32 col){
    if(ThemedSym(dl,c,10.0f,col, g_st.online? "network-wireless-connected-100":"network-wireless-disconnected")) return;
    for(int k=0;k<3;k++){ float r=3.5f+k*3.0f; dl->PathArcTo(V(c.x,c.y+3), r, 3.9269908f, 5.4977871f, 16); dl->PathStroke(col,0,1.8f); }
    dl->AddCircleFilled(V(c.x,c.y+3),1.7f,col);
}
static void BtIcon(ImDrawList* dl, ImVec2 c, ImU32 col){
    if(ThemedSym(dl,c,10.0f,col,"network-bluetooth")) return;
    float s=9,w=6; ImVec2 T(c.x,c.y-s),B(c.x,c.y+s),M(c.x,c.y),R1(c.x+w,c.y-s*0.5f),R2(c.x+w,c.y+s*0.5f);
    dl->AddLine(M,T,col,2); dl->AddLine(M,B,col,2);
    dl->AddLine(T,R1,col,2); dl->AddLine(R1,M,col,2);   // upper right triangle
    dl->AddLine(B,R2,col,2); dl->AddLine(R2,M,col,2);   // lower right triangle
}
static void PowerIcon(ImDrawList* dl, ImVec2 c, ImU32 col){
    if(ThemedSym(dl,c,11.0f,col,"system-shutdown")) return;
    dl->PathArcTo(c,9,-1.0208f,4.1616f,28); dl->PathStroke(col,0,2.3f);  // ring with a gap at the top
    dl->AddLine(V(c.x,c.y-11),V(c.x,c.y-2),col,2.4f);                    // top stem
}
static bool g_powerMenu=false;
static void OpenSettings(const wchar_t* uri){ AetherShellExec(nullptr,L"open",uri,nullptr,nullptr,SW_SHOWNORMAL); }
static void RunCmd(const wchar_t* exe,const wchar_t* args){ AetherShellExec(nullptr,L"open",exe,args,nullptr,SW_HIDE); }

static bool g_launShow=false;   // launcher visibility (also used by the bar's Start button)
// True while one of our own panels is taking input. Nothing that reorders or re-activates other
// people's windows may run in that window, or it pulls focus out from under the panel the user is
// actually using.
static bool ShellUiBusy(){
    return g_setShow || g_launShow || g_sessShow || g_lockShow || g_editTab;
}
// little rounded tooltip that renders to the right of the strip (deferred so it draws on top)
// dir = +1 -> tip sits to the RIGHT of x (bar on the left edge), -1 -> to its LEFT
// Bar tooltip. It now FADES AND GLIDES: one shared animator follows the hovered item down the
// strip instead of the label snapping between icons, and it carries a drop shadow + capsule shape
// so it reads as the same material family as the popouts.
// The bar window is 300px wide precisely so a tooltip can overhang the 66px strip - but the
// window's REGION is only the strip plus a 36px pad, and SetWindowRgn clips rendering, not just
// hit-testing. Anything longer than that pad was chopped off; the bottom cluster has the longest
// labels ("Do not disturb (right-click to allow)"), which is why testers saw it there first.
// Publishing the rect lets the region take it in, the same way the app menu already does.
static RECT g_barTipRect={0,0,0,0};
static void BarTip(ImDrawList* dl,float x,float cy,const char* t,float dir=1.0f){
    if(!t||!*t)return;
    static std::string last; static ULONGLONG changed=0;
    if(last!=t){ last=t; changed=GetTickCount64(); }
    float fade=std::clamp((GetTickCount64()-changed)/110.0f,0.0f,1.0f);   // quick cross-fade on change
    float ty=Cael::anim(819500,cy,Cael::DUR_FAST_SPATIAL,Cael::FAST_SPATIAL);
    if(fabsf(ty-cy)>260.0f) ty=cy;                                        // don't fly the length of the bar
    float w=TextW(g_fSml,15,t)+24,h=30, r=h*0.5f;
    if(dir<0) x-=w;
    ImVec2 a=V(x,ty-h/2),b=V(x+w,ty+h/2);
    ImU32 body=IM_COL32(30,28,36,(int)(248*fade)), rim=IM_COL32(255,255,255,(int)(26*fade));
    for(int i=5;i>0;i--) dl->AddRect(V(a.x-i,a.y-i+2),V(b.x+i,b.y+i+2),IM_COL32(0,0,0,(int)(13*fade)),r+i,0,(float)i*0.9f);
    if(dir<0) dl->AddTriangleFilled(V(b.x+6,ty),V(b.x-2,ty-7),V(b.x-2,ty+7),body);
    else      dl->AddTriangleFilled(V(a.x-6,ty),V(a.x+2,ty-7),V(a.x+2,ty+7),body);
    dl->AddRectFilled(a,b,body,r); dl->AddRect(a,b,rim,r,0,1.0f);
    TextAt(dl,g_fSml,15,V(a.x+12,ty-9),IM_COL32(232,230,238,(int)(255*fade)),t);
    // inflated past the drop shadow and the little arrow, both of which sit outside a..b
    g_barTipRect=RECT{(LONG)(a.x-10),(LONG)(a.y-10),(LONG)(b.x+10),(LONG)(b.y+10)};
}
static void MoonIcon(ImDrawList* dl,ImVec2 c,ImU32 col,ImU32 bgc){
    if(ThemedSym(dl,c,10.0f,col,"weather-clear-night")) return;
    dl->AddCircleFilled(c,8,col); dl->AddCircleFilled(V(c.x+5,c.y-4),7,bgc);
}
static void MicIcon(ImDrawList* dl,ImVec2 c,ImU32 col,bool muted){
    // the muted mark is its own icon in Breeze, so no hand-drawn slash on top of a themed glyph
    if(ThemedSym(dl,c,10.0f,col, muted? "microphone-sensitivity-muted":"audio-input-microphone")) return;
    dl->AddRectFilled(V(c.x-3.5f,c.y-9),V(c.x+3.5f,c.y+2),col,3.5f);
    dl->PathArcTo(V(c.x,c.y),7.5f,0.0f,3.1416f,16); dl->PathStroke(col,0,1.8f);
    dl->AddLine(V(c.x,c.y+7.5f),V(c.x,c.y+11),col,1.8f);
    if(muted) dl->AddLine(V(c.x-9,c.y-10),V(c.x+9,c.y+10),COL_ERR,2.2f);
}
static void RecIcon(ImDrawList* dl,ImVec2 c,ImU32 col,bool on){
    dl->AddCircle(c,8,col,0,1.8f);
    if(on) dl->AddRectFilled(V(c.x-3.5f,c.y-3.5f),V(c.x+3.5f,c.y+3.5f),COL_ERR,1.5f);
    else   dl->AddCircleFilled(c,3.5f,col);
}
static void CalIcon(ImDrawList* dl,ImVec2 c,ImU32 col){
    if(ThemedSym(dl,c,10.0f,col,"view-calendar-month")) return;
    dl->AddRect(V(c.x-8,c.y-7),V(c.x+8,c.y+8),col,2.5f,0,1.7f);
    dl->AddLine(V(c.x-8,c.y-2),V(c.x+8,c.y-2),col,1.5f);
    dl->AddLine(V(c.x-4,c.y-10),V(c.x-4,c.y-6),col,1.8f);
    dl->AddLine(V(c.x+4,c.y-10),V(c.x+4,c.y-6),col,1.8f);
    dl->AddRectFilled(V(c.x-1.5f,c.y+1),V(c.x+1.5f,c.y+4),col,0.5f);
}
static void GearIcon(ImDrawList* dl,ImVec2 c,ImU32 col){
    if(ThemedSym(dl,c,10.0f,col,"configure")) return;
    for(int i=0;i<8;i++){ float a=i*0.7853982f; ImVec2 d(cosf(a),sinf(a));
        dl->AddLine(V(c.x+d.x*5.5f,c.y+d.y*5.5f),V(c.x+d.x*9.0f,c.y+d.y*9.0f),col,2.6f); }
    dl->AddCircle(c,5.5f,col,0,2.0f);
}
