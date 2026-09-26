// Aether - wWinMain: startup, the render loop, shutdown.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

int APIENTRY wWinMain(HINSTANCE hInst,HINSTANCE,LPWSTR cmd,int){
    if(wcsstr(cmd,L"--dumptest")) return DumpSelfTest();
    // Runs before anything else: the config layer has to be provably correct before it owns the
    // settings, and "your comments survive a save" is a claim that needs a test, not a promise.
    if(wcsstr(cmd,L"--tomltest")){
        char ex[MAX_PATH]={0}; GetModuleFileNameA(nullptr,ex,MAX_PATH);
        std::string op=ex; { size_t s=op.find_last_of("\\/"); if(s!=std::string::npos) op=op.substr(0,s+1); }
        op+="tomltest.txt";
        FILE* fp=nullptr; freopen_s(&fp,op.c_str(),"w",stdout);       // results land next to the exe
        int bad=TomlSelfTest();
        if(fp) fclose(fp);
        return bad;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // IPC send runs BEFORE any window, device or crash guard is created - this process is a courier,
    // not a shell, and must leave no trace behind.
    { const wchar_t* k=wcsstr(cmd,L"--cmd"); size_t klen=5;
      if(!k){ k=wcsstr(cmd,L"-s "); klen=2; }
      if(k){ const wchar_t* a=k+klen;
          while(*a==L' '||*a==L'=') a++;
          std::wstring w;
          if(*a==L'"'){ a++; while(*a && *a!=L'"') w+=*a++; }            // -s "status=grinding roblox"
          else if(!wcsncmp(a,L"status=",7)){ while(*a && *a!=L'"') w+=*a++; while(!w.empty()&&w.back()==L' ') w.pop_back(); }
          else while(*a && *a!=L' ' && *a!=L'"') w+=*a++;
          if(!w.empty()){
              bool ok=SendShellCommand(W2U8(w));
              return ok?0:1; } } }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // for WIC image/gif decoding
    InstallCrashGuards();   // before ANY system-wide change, so every exit path hands the desktop back
    // silent healer: put the taskbar and work areas back after a run that never got to clean up.
    // This is what the RunOnce entry invokes at the next sign-in.
    if (wcsstr(cmd,L"--recover")){
        if(!AetherShellRunning()) RecoveryHeal();   // never fight a live shell
        return 0;
    }
    // one-shot recovery: undo "run as the Windows shell" without needing the UI
    if (wcsstr(cmd,L"--restore-shell")){
        SetShellReplaced(false);
        if(!AetherShellRunning()) RecoveryHeal();
        MessageBoxW(nullptr,L"Aether is no longer registered as the Windows shell.\nExplorer will start normally at your next sign-in.",
                    L"Aether",MB_OK|MB_ICONINFORMATION);
        return 0;
    }
    // Cairo's --shell / --noshell overrides; otherwise we auto-detect
    g_forceShell   = wcsstr(cmd,L"--shell")!=nullptr && wcsstr(cmd,L"--noshell")==nullptr;
    g_forceNoShell = wcsstr(cmd,L"--noshell")!=nullptr;
    DetectShellMode();
    // Single instance, like Cairo's "CairoShell" mutex. Debug/screenshot launches pass flags and
    // are allowed to run alongside; a plain launch (or a WinLogon shell launch) must be unique.
    bool soleShell;
    {
        bool plainLaunch = (cmd==nullptr || *cmd==L'\0' ||
                            (wcsstr(cmd,L"--")==nullptr) ||
                            (wcsstr(cmd,L"--shell")!=nullptr || wcsstr(cmd,L"--noshell")!=nullptr));
        // ask BEFORE taking the mutex, or we would just find our own
        soleShell = !AetherShellRunning();
        if(plainLaunch){
            HANDLE mtx=CreateMutexW(nullptr,TRUE,L"Local\\AetherShell");
            if(mtx && GetLastError()==ERROR_ALREADY_EXISTS){
                // a shell is already up — hand the session over to it rather than fighting for the
                // tray host, the hotkeys and the shell window
                return 0;
            }
        }
    }
    // Nothing else is running: heal anything a previous run left behind BEFORE we measure the work
    // areas ourselves, otherwise we would save the already-inset rects as the "originals" and bake
    // the damage in permanently. (A screenshot/debug launch alongside a live shell must NOT do this.)
    if(soleShell) RecoveryHeal();
    // crash-loop breaker: three bad starts in a row -> come up touching nothing system-wide.
    // Only a real solo run counts; a screenshot/debug launch must not spend a strike.
    if(soleShell){ int strikes=StartupStrikes();
      g_safeMode = (strikes>=3);
      SetStartupStrikes(strikes+1);
      if(g_safeMode){
          bool wasShell=IsShellReplaced();
          if(wasShell) SetShellReplaced(false);   // guarantee the next sign-in reaches Explorer
          MessageBoxW(nullptr,
              wasShell? L"Aether failed to start three times in a row, so it has started in SAFE MODE.\n\n"
                        L"The Windows taskbar is left alone and Aether has UN-REGISTERED itself as the Windows "
                        L"shell, so your next sign-in will load Explorer normally.\n\n"
                        L"Nothing on your system has been left modified."
                      : L"Aether failed to start three times in a row, so it has started in SAFE MODE.\n\n"
                        L"The Windows taskbar and the desktop work area are left completely alone this run.\n\n"
                        L"Close and reopen Aether to try a normal start again.",
              L"Aether - Safe Mode", MB_OK|MB_ICONWARNING|MB_SETFOREGROUND);
      } }
    LoadConfig();
    if (g_bgMode==1) LoadBgImage(g_bgPath);
    bool probe = wcsstr(cmd,L"--probe")!=nullptr;   // show on left monitor, no focus steal
    if (wchar_t* tp=wcsstr(cmd,L"--tab")) { int t=_wtoi(tp+5); if(t>=0&&t<4) g_tab=t; }
    if (wcsstr(cmd,L"--session")) g_sessShow=true;   // for screenshot verification
    if (wcsstr(cmd,L"--wifi")) g_sideView=1;
    if (wcsstr(cmd,L"--bt"))   g_sideView=2;
    if (wcsstr(cmd,L"--launcher")) g_launShow=true;
    bool testnotif = wcsstr(cmd,L"--testnotif")!=nullptr;   // inject fake toasts (no consent/package needed)
    bool swshot    = wcsstr(cmd,L"--swshot")!=nullptr;      // hold the window switcher open for a still capture
    g_swFpsHud     = wcsstr(cmd,L"--swfps")!=nullptr;       // frame-rate readout inside the switcher
    bool testmedia = wcsstr(cmd,L"--testmedia")!=nullptr;   // fake now-playing + pop the flyout
    if (wcsstr(cmd,L"--perflog")) perf::on=true;                          // frame accounting -> perflog.txt
    if (wcsstr(cmd,L"--barshot")) g_forceBar=true;                       // pin the bar open
    if (wcsstr(cmd,L"--qsshot")){ g_forceSide=true; g_forceBar=true; }   // pin quick settings open
    if (wcsstr(cmd,L"--deskshot")){ g_deskTop=true; g_forceBar=true; }   // float the desktop layer for a screenshot
    if (wcsstr(cmd,L"--uishot")) g_forceDrawer=true;                     // pin the drawer open (with --tab N)
    if (wcsstr(cmd,L"--menushot")){ g_forceBar=true; g_barMenu=true; g_barMenuAt=ImVec2(88,300); }
    if (wcsstr(cmd,L"--settings")) g_setShow=true;                        // screenshot the Settings app
    if (wcsstr(cmd,L"--launchshot")){ g_launShow=true; g_lmode=0; }        // screenshot the app launcher
    if (wcsstr(cmd,L"--schemeshot")){ g_launShow=true; g_lmode=3; g_schemeSel=std::clamp(g_scheme,0,NSCHEMES-1); }
    if (wchar_t* sp=wcsstr(cmd,L"--setpage")){ int v=_wtoi(sp+10); if(v>=0&&v<SP_COUNT) g_setPage=v; }
    if (wcsstr(cmd,L"--wallshot")){ g_launShow=true; g_lmode=2; WallScan(); }   // wallpaper carousel
    if (wcsstr(cmd,L"--dimshot")) g_forceDim=true;                              // idle-dim veil
    // a verification instance must not steal the live shell's tray icons (registering a second
    // Shell_TrayWnd makes every app re-add its icon to us and hands them back only on exit)
    bool noTray = wcsstr(cmd,L"--notray")!=nullptr;
    g_plStatusLog = wcsstr(cmd,L"--pluginstatus")!=nullptr;   // dump plugin state to plugins\status.log
    // screenshot instances must not be hidden by whatever is fullscreen on the user's screen
    if(wcsstr(cmd,L"--nofs")) g_hideOnFullscreen=false;
    if(wcsstr(cmd,L"--edittab")){ g_forceDrawer=true; g_editTab=true; }   // pin the tab editor
    if(wcsstr(cmd,L"--files")) FmStart();   // open the file manager (verification)
    bool snipnow = wcsstr(cmd,L"--snip")!=nullptr;   // open the snipper at startup (verification)
    bool sniptest = wcsstr(cmd,L"--sniptest")!=nullptr;   // headless: capture + save a fixed crop, then quit
    if(wcsstr(cmd,L"--wpalette")){ g_forceDrawer=true; g_editTab=true; g_wPaletteOpen=true; }
    // --freeze N : pin the open animations at N% so a mid-flight frame can be screenshotted
    float freeze=-1.0f;
    if (wchar_t* fz=wcsstr(cmd,L"--freeze")) { int v=_wtoi(fz+9); if(v>=0&&v<=100) freeze=v/100.0f; }
    // --transtest <name>: loop a transition between the current wallpaper and another image WITHOUT
    // changing the real wallpaper, so the shapes can be verified from a passive screenshot.
    std::string transTest;
    if (wchar_t* tt=wcsstr(cmd,L"--transtest")){ wchar_t* s=tt+12; wchar_t buf[64]={0};
        for(int i=0;i<63&&s[i]&&s[i]!=L' ';i++) buf[i]=s[i];
        transTest = buf[0]? W2U8(buf) : "grow"; g_deskTop=true; }

    // ---- isolated tray-host experiment: does the Cairo/Shell_NotifyIcon technique deliver icons on Win11? ----
    if (wcsstr(cmd,L"--traytest")){
        bool hid=false; HWND rtb=FindWindowW(L"Shell_TrayWnd",nullptr);
        // hide explorer's real taskbar so our Shell_TrayWnd is the one apps talk to
        if(rtb){ ShowWindow(rtb,SW_HIDE); hid=true; }
        g_trayRaw=true;
        InitTrayHost();
        ULONGLONG t0=GetTickCount64(); ULONGLONG lastPing=0;
        while(GetTickCount64()-t0 < 6000){
            MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){ TranslateMessage(&msg); DispatchMessage(&msg); }
            if(GetTickCount64()-lastPing>1500){ lastPing=GetTickCount64(); SendNotifyMessageW(HWND_BROADCAST,g_msgTaskbarCreated,0,0); }
            Sleep(15);
        }
        std::wstring rep; { std::lock_guard<std::mutex> lk(g_systrayMtx);
            rep=L"tray icons received: "+std::to_wstring(g_systray.size())+L"\r\n";
            for(auto&s:g_systray) rep+=L"  ["+std::to_wstring(s.id)+L"] hwnd="+std::to_wstring((uintptr_t)s.hwnd)+L" ver="+std::to_wstring(s.ver)+L" tip='"+s.tip+L"'\r\n"; }
        { rep+=L"\r\n--- RAW ---\r\n"; for(auto&d:g_trayRawDump){ std::wstring w(d.begin(),d.end()); rep+=w+L"\r\n"; } }
        HANDLE f=CreateFileW(U82W(ExeDir()+"traytest.log").c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
        if(f!=INVALID_HANDLE_VALUE){ int len=WideCharToMultiByte(CP_UTF8,0,rep.c_str(),-1,nullptr,0,nullptr,nullptr);
            std::string u8(len>0?len-1:0,'\0'); WideCharToMultiByte(CP_UTF8,0,rep.c_str(),-1,&u8[0],len,nullptr,nullptr);
            DWORD wr; WriteFile(f,u8.data(),(DWORD)u8.size(),&wr,nullptr); CloseHandle(f); }
        if(hid&&rtb) ShowWindow(rtb,SW_SHOW);   // restore explorer's taskbar
        return 0;
    }
    // every monitor is enumerated; the pop-up panels start on the primary and follow the cursor,
    // while the desktop layer and the taskbar span the whole virtual screen and draw on each screen
    RefreshMonitors();
    g_actMon=0;
    { const RECT& pr0=MonRect(0); g_mx=pr0.left; g_my=pr0.top; g_mw=pr0.right-pr0.left; g_mh=pr0.bottom-pr0.top; }
    if (probe && g_mons.size()>1){ g_actMon=1; const RECT& r1=MonRect(1);
        g_mx=r1.left; g_my=r1.top; g_mw=r1.right-r1.left; g_mh=r1.bottom-r1.top; }
    const int VSW=g_vs.right-g_vs.left, VSH=g_vs.bottom-g_vs.top;   // the monitors the shell paints on
    const int DVW=g_dvs.right-g_dvs.left, DVH=g_dvs.bottom-g_dvs.top;   // every monitor (decorations)
    g_bars.resize(std::max<size_t>(1,g_mons.size()));
    if (g_hideTaskbar && !probe && !g_safeMode) SetWindowsTaskbar(true);   // Cairo-style: take over from the Windows taskbar
    // fixed-size overlays are authored in logical px; their windows must be physical
    auto PX=[&](float v){ return (int)(v*g_uiScale+0.5f); };
    WNDCLASSEXW wc={sizeof(wc),CS_CLASSDC,WndProc,0,0,hInst,nullptr,LoadCursor(nullptr,IDC_ARROW),nullptr,nullptr,L"AetherClass",nullptr};
    RegisterClassExW(&wc);
    // LAYOUT ENGINE: the drawer / bar / quick-settings windows are FULL-MONITOR, because their panels
    // can be moved to any edge. ApplyHitRegion clips each one to what it actually draws every frame,
    // so the empty area still never eats a click, and every panel now shares one coordinate space.
    g_hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"Aether", WS_POPUP, g_mx, g_my, g_mw, g_mh, nullptr,nullptr,hInst,nullptr);
    // Clipboard history feed. Delivered as WM_CLIPBOARDUPDATE to this window whenever ANY process
    // changes the clipboard, which is the only way to observe other apps' copies without polling.
    if(g_hwnd) AddClipboardFormatListener(g_hwnd);
    g_dockHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherDock", WS_POPUP, g_mx, g_my+g_mh-DOCKWINH, g_mw, DOCKWINH, nullptr,nullptr,hInst,nullptr);
    // the BAR spans every monitor: one window, one strip drawn per screen, region = union of them
    g_barHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherBar", WS_POPUP, g_vs.left, g_vs.top, VSW, VSH, nullptr,nullptr,hInst,nullptr);
    g_sideHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherSide", WS_POPUP, g_mx, g_my, g_mw, g_mh, nullptr,nullptr,hInst,nullptr);
    g_osdHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherOsd", WS_POPUP, g_mx+g_mw-PX(OSDW), g_my+(g_mh-PX(OSDH))/2, PX(OSDW), PX(OSDH), nullptr,nullptr,hInst,nullptr);
    g_sessHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherSess", WS_POPUP, g_mx, g_my, g_mw, g_mh, nullptr,nullptr,hInst,nullptr);
    // launcher: ACTIVATABLE (needs keyboard focus for the search box)
    g_launHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherLaunch", WS_POPUP, g_mx, g_my, g_mw-1, g_mh, nullptr,nullptr,hInst,nullptr);
    // lock screen: ACTIVATABLE (password field needs keyboard focus), full monitor
    g_lockHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherLock", WS_POPUP, g_mx, g_my, g_mw, g_mh, nullptr,nullptr,hInst,nullptr);
    g_notifHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherNotif", WS_POPUP, g_mx+g_mw-PX(NOTIFWINW), g_my, PX(NOTIFWINW), g_mh, nullptr,nullptr,hInst,nullptr);
    g_medHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherMedia", WS_POPUP, g_mx+g_mw-PX(MEDIAWINW), g_my, PX(MEDIAWINW), PX(MEDIAWINH), nullptr,nullptr,hInst,nullptr);
    // window switcher: NOACTIVATE, because the LL keyboard hook drives it and the app the user is
    // Alt+Tabbing away from must keep focus until they let go of Alt
    // workspace overview: NOACTIVATE like the switcher, driven by its hotkey and the keyboard hook
    g_ovHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherOverview", WS_POPUP, g_mx, g_my, g_mw, g_mh, nullptr,nullptr,hInst,nullptr);
    // It draws a picture of the screen, so it must never appear in the picture.
    Cap::ExcludeFromCapture(g_ovHwnd);
    g_swHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherSwitcher", WS_POPUP, g_mx, g_my, g_mw, g_mh, nullptr,nullptr,hInst,nullptr);
    // settings app: ACTIVATABLE (Esc / keyboard)
    g_setHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherSettings", WS_POPUP, g_mx, g_my, g_mw-1, g_mh, nullptr,nullptr,hInst,nullptr);
    // idle-dim veil: always click-through, above everything
    g_dimHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherDim", WS_POPUP, g_mx, g_my, g_mw, g_mh, nullptr,nullptr,hInst,nullptr);
    // desktop layer: NOT topmost — sits at the bottom of the z-order, behind every app window
    // the DESKTOP layer also spans every monitor, so each screen gets its own bubble
    g_deskHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherDesk", WS_POPUP, g_vs.left, g_vs.top, VSW, VSH, nullptr,nullptr,hInst,nullptr);
    Ws2CreateWindow(hInst,g_deskHwnd,g_barHwnd);   // the workspace-slide layer (hidden until a switch)
    // snipping tool: ACTIVATABLE (Esc), spans the whole virtual screen, topmost
    g_snipHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherSnip", WS_POPUP, g_vs.left, g_vs.top, VSW, VSH, nullptr,nullptr,hInst,nullptr);
    // file manager: ACTIVATABLE (keyboard), full active-monitor overlay hosting a floating window
    g_fmHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherFiles", WS_POPUP, g_mx, g_my, g_mw, g_mh, nullptr,nullptr,hInst,nullptr);
    // window decorations: NOACTIVATE (never steal focus from the app), spans the whole virtual screen
    g_decoHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"AetherDeco", WS_POPUP, g_dvs.left, g_dvs.top, DVW, DVH, nullptr,nullptr,hInst,nullptr);

    // one D3D11 device (BGRA), shared by both overlays; each gets its own composition swapchain + DComp visual
    UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL fls[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0};
    if (FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,fls,2,D3D11_SDK_VERSION,&g_dev,nullptr,&g_ctx))) return 1;
    IDXGIDevice* dxdev=nullptr; g_dev->QueryInterface(IID_PPV_ARGS(&dxdev));
    IDXGIAdapter* ad=nullptr; dxdev->GetAdapter(&ad);
    ad->GetParent(IID_PPV_ARGS(&g_fac));
    DCompositionCreateDevice(dxdev, IID_PPV_ARGS(&g_dcDev));
    auto makeChain=[&](HWND hw,int w,int h,IDXGISwapChain1** sc,ID3D11RenderTargetView** rtv,IDCompositionTarget** tgt,IDCompositionVisual** vis){
        DXGI_SWAP_CHAIN_DESC1 scd={}; scd.Width=w; scd.Height=h; scd.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.SampleDesc.Count=1; scd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; scd.BufferCount=2;
        scd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; scd.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
        g_fac->CreateSwapChainForComposition(g_dev,&scd,nullptr,sc);
        ID3D11Texture2D* bb=nullptr; (*sc)->GetBuffer(0,IID_PPV_ARGS(&bb)); if(bb){g_dev->CreateRenderTargetView(bb,nullptr,rtv);bb->Release();}
        g_dcDev->CreateTargetForHwnd(hw,TRUE,tgt); g_dcDev->CreateVisual(vis); (*vis)->SetContent(*sc); (*tgt)->SetRoot(*vis);
    };
    makeChain(g_hwnd, g_mw, g_mh, &g_sc, &g_rtv, &g_dcTarget, &g_dcVisual);
    makeChain(g_dockHwnd, g_mw, DOCKWINH, &g_dockSc, &g_dockRtv, &g_dockTgt, &g_dockVis);
    makeChain(g_barHwnd, VSW, VSH, &g_barSc, &g_barRtv, &g_barTgt, &g_barVis);
    makeChain(g_sideHwnd, g_mw, g_mh, &g_sideSc, &g_sideRtv, &g_sideTgt, &g_sideVis);
    makeChain(g_osdHwnd, PX(OSDW), PX(OSDH), &g_osdSc, &g_osdRtv, &g_osdTgt, &g_osdVis);
    makeChain(g_sessHwnd, g_mw, g_mh, &g_sessSc, &g_sessRtv, &g_sessTgt, &g_sessVis);
    makeChain(g_launHwnd, g_mw-1, g_mh, &g_launSc, &g_launRtv, &g_launTgt, &g_launVis);
    makeChain(g_lockHwnd, g_mw, g_mh, &g_lockSc, &g_lockRtv, &g_lockTgt, &g_lockVis);
    makeChain(g_notifHwnd, PX(NOTIFWINW), g_mh, &g_notifSc, &g_notifRtv, &g_notifTgt, &g_notifVis);
    makeChain(g_swHwnd,    g_mw,           g_mh, &g_swSc,    &g_swRtv,    &g_swTgt,    &g_swVis);
    makeChain(g_ovHwnd,    g_mw,           g_mh, &g_ovSc,    &g_ovRtv,    &g_ovTgt,    &g_ovVis);
    makeChain(g_medHwnd, PX(MEDIAWINW), PX(MEDIAWINH), &g_medSc, &g_medRtv, &g_medTgt, &g_medVis);
    makeChain(g_setHwnd, g_mw-1, g_mh, &g_setSc, &g_setRtv, &g_setTgt, &g_setVis);
    makeChain(g_dimHwnd, g_mw, g_mh, &g_dimSc, &g_dimRtv, &g_dimTgt, &g_dimVis);
    makeChain(g_deskHwnd, VSW, VSH, &g_deskSc, &g_deskRtv, &g_deskTgt, &g_deskVis);
    makeChain(g_snipHwnd, VSW, VSH, &g_snipSc, &g_snipRtv, &g_snipTgt, &g_snipVis);
    makeChain(g_fmHwnd, g_mw, g_mh, &g_fmSc, &g_fmRtv, &g_fmTgt, &g_fmVis);
    makeChain(g_decoHwnd, DVW, DVH, &g_decoSc, &g_decoRtv, &g_decoTgt, &g_decoVis);
    g_dcDev->Commit();
    ad->Release(); dxdev->Release();

    // shared font atlas -> fonts valid in both contexts; one ImGui context per window
    IMGUI_CHECKVERSION();
    g_atlas = IM_NEW(ImFontAtlas)();
    g_ctxDrawer = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxDrawer);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f; AddFonts();
    ImGui_ImplWin32_Init(g_hwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxDock = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxDock);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;     // fonts already in the shared atlas
    ImGui_ImplWin32_Init(g_dockHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxBar = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxBar);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_barHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxSide = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxSide);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_sideHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxOsd = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxOsd);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_osdHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_osdWake=g_osdHwnd; InitOSD();   // Core Audio volume-change callback -> OSD popup
    g_ctxSess = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxSess);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_sessHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxLaunch = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxLaunch);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_launHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxLock = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxLock);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_lockHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxSw = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxSw);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_swHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxOv = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxOv);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_ovHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxNotif = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxNotif);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_notifHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxSnip = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxSnip);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_snipHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxFm = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxFm);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_fmHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxDeco = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxDeco);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_decoHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxMed = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxMed);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_medHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_medWake=g_medHwnd;
    g_ctxSet = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxSet);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_setHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxDim = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxDim);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_dimHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    g_ctxDesk = ImGui::CreateContext(g_atlas); ImGui::SetCurrentContext(g_ctxDesk);
    ImGui::GetIO().IniFilename=nullptr; ImGui::StyleColorsDark(); ImGui::GetStyle().CircleTessellationMaxError=0.01f;
    ImGui_ImplWin32_Init(g_deskHwnd); ImGui_ImplDX11_Init(g_dev,g_ctx);
    LoadDeskWallpaper();
    if(!transTest.empty()){   // build a second image so the transition has something to reveal
        g_transNow=ParseTrans(transTest);
        wchar_t cur[MAX_PATH]={0}; SystemParametersInfoW(SPI_GETDESKWALLPAPER,MAX_PATH,cur,0);
        std::vector<Wall> pool;
        wchar_t pic[MAX_PATH];
        if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_MYPICTURES,nullptr,0,pic))) WallScanDir(pool,pic,0);
        for(auto& w:pool){ if(cur[0]&&_wcsicmp(w.path.c_str(),cur)==0) continue;
            std::vector<uint8_t> px; int iw=0,ih=0;
            if(!DecodeFirstFrame(w.path,2560,px,iw,ih)) continue;
            ID3D11ShaderResourceView* t=MakeTextureBGRA(px.data(),iw,ih); if(!t) continue;
            g_deskWallOld=g_deskWall; g_deskWallOldW=g_deskWallW; g_deskWallOldH=g_deskWallH;
            g_deskWall=t; g_deskWallW=iw; g_deskWallH=ih;
            g_wipe=0.0f; g_wipeAt=DeskPt(0.42f,0.59f); break; }
    }
    ShowWindow(g_deskHwnd,SW_SHOWNOACTIVATE);
    PlaceDeskLayer(g_deskHwnd);

    RegisterHotkeys();       // user-chosen; whatever Windows refuses is reported in Settings
    if(snipnow){ /* fired after the loop starts so the window exists */ }
    g_flyoutHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, FlyoutWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    BarCustomLoad();                               // configaritems.toml -> the six custom bar items
    Ws2TileHooks(true);                            // smooth komorebi tiling moves (hooks live on the event thread)
    SweepNativeFlyout();   // suppress the native volume/brightness flyout (our OSD replaces it)
    g_swWake=g_swHwnd;
    g_ovWake=g_ovHwnd;
    g_notifWake=g_notifHwnd; if(!testnotif) std::thread(NotifThread).detach();   // WinRT toast listener (packaged launch only)
    std::thread(WallWatchThread).detach();   // notice wallpaper changes and re-derive the palette
    // --testnotif alone exercises the REAL lifecycle (auto-pop on arrival -> auto-hide), which is what
    // needs testing; add --notifpin to hold the stack open for a still capture.
    if(testnotif){ g_forceNotif = wcsstr(cmd,L"--notifpin")!=nullptr;
        std::lock_guard<std::mutex> lk(g_notifMtx);
        ULONGLONG tn=GetTickCount64();
        g_notifs.push_back({1,"Discord","badbo","hey are the notifications working yet? this one runs long on purpose so the two-line wrap gets exercised","Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",tn});
        g_notifs.push_back({2,"Spotify","Now playing","Bonobo \xE2\x80\x94 Kerala","Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",tn>400000?tn-400000:1});
        g_notifs.push_back({3,"Windows Update","Updates available","Restart to finish installing","Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",tn>7200000?tn-7200000:1});
        g_notifs.push_back({4,"Steam","Download complete","Hollow Knight: Silksong is ready to play","Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",0});
        // stamp "arrived" a few seconds in, once the render loop is actually up, so the auto-pop is
        // observable instead of half-expired before the first frame draws
        std::thread([]{ Sleep(4000); g_notifChanged=GetTickCount64();
                        if(g_notifWake) PostMessageW(g_notifWake,WM_NULL,0,0); }).detach(); }
    // --traysim: fake tray icons so the chevron + flyout can be verified WITHOUT InitTrayHost, which
    // would broadcast TaskbarCreated and pull the user's real tray icons out of their live shell.
    // --helloprompt: fire the REAL interop verification against a throwaway window and log what
    // came back. This is the part that classically fails for an unpackaged Win32 process, so it
    // is worth proving separately from the availability probe.
    if(wcsstr(cmd,L"--helloprompt")){
        HWND w=CreateWindowExW(WS_EX_TOPMOST,L"STATIC",L"Aether Hello test",WS_POPUP|WS_VISIBLE,
                               400,300,320,120,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        SetForegroundWindow(w);
        HelloVerify(w);
        for(int i=0;i<600 && g_helloState.load()==HELLO_ASKING;i++){
            MSG m; while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){ TranslateMessage(&m); DispatchMessage(&m); }
            Sleep(50); }
        int st=g_helloState.load();
        FILE* lf=fopen("helloprompt.log","w");
        if(lf){ fprintf(lf,"state=%d (%s) msg=%s",st,
                        st==HELLO_OK?"VERIFIED":(st==HELLO_ASKING?"TIMED OUT":"not verified"),
                        g_helloMsg.c_str()); fclose(lf); }
        DestroyWindow(w);
        return 0;
    }
    // --hellotest: probe Windows Hello availability and report, without touching the lock screen.
    if(wcsstr(cmd,L"--hellotest")){
        HelloProbe();
        for(int i=0;i<60 && g_helloAvail.load()<0 && g_helloAvail.load()!=0;i++) Sleep(50);
        for(int i=0;i<60 && g_helloAvail.load()==-1;i++) Sleep(50);
        int av=g_helloAvail.load();
        FILE* lf=fopen("hellotest.log","w");
        if(lf){ fprintf(lf,"avail=%d %s",av, av==1?"AVAILABLE":(av==0?"threw/unknown":HelloWhyNot(-av-2))); fclose(lf); }
        return 0;
    }
    if(wcsstr(cmd,L"--traysim")){
        // shell32.dll by index, not real exes: System32\notepad.exe and friends are execution-alias
        // stubs on Win11 with no icon resource at all, so ExtractIconEx there yields nothing.
        static const int   SIMIDX[]={ 13, 22, 44, 47, 77, 132, 167, 238 };
        static const wchar_t* SIMTIP[]={ L"Network", L"Printer", L"Backup service", L"Sync client",
                                         L"Security centre", L"Audio device", L"Update agent", L"VPN" };
        wchar_t sysd[MAX_PATH]; GetSystemDirectoryW(sysd,MAX_PATH);
        std::wstring dll=std::wstring(sysd)+L"\\shell32.dll";
        std::lock_guard<std::mutex> lk(g_systrayMtx);
        for(int i=0;i<8;i++){
            HICON big=nullptr,sml=nullptr;
            if(ExtractIconExW(dll.c_str(),SIMIDX[i],&big,&sml,1)==0 && !big && !sml) continue;
            HICON use=big?big:sml; if(!use) continue;
            // a REAL hwnd, or the render loop's dead-owner pruner deletes these within 2s
            // (IsWindow(nullptr) is false). cbMsg stays 0, so a forwarded click is a harmless WM_NULL.
            SysTrayIcon s; s.hwnd=GetDesktopWindow(); s.id=9000+i; s.tip=SIMTIP[i];
            s.tex=IconTex(use,32); if(!s.tex) continue;
            g_systray.push_back(s);
            if(big)DestroyIcon(big); if(sml)DestroyIcon(sml); }
    }
    if(wcsstr(cmd,L"--caelpreset")){ ApplyCaelestiaBarPreset(); return 0; }   // apply + exit, for testing
    if(wcsstr(cmd,L"--trayshot")){ g_forceBar=true; g_trayOpen=true; g_trayFlyMon=0; }
    // --macrotest: arm the engine, enable one named macro, and accept injected triggers so a script
    // can prove the whole path (hook -> mode -> step executor -> SendInput) actually fires.
    if(wchar_t* mt=wcsstr(cmd,L"--macrotest")){
        g_macroAllowInjected=true; g_macroArmed=true;
        std::wstring want(mt+11);
        size_t sp=want.find(L' '); if(sp!=std::wstring::npos) want=want.substr(0,sp);
        if(!want.empty() && want[0]==L'=') want.erase(0,1);
        if(g_macros.empty()) MacroLoadPresets();
        std::string w=W2U8(want);
        for(auto& m:g_macros){
            std::string n2=m.name; for(auto&ch:n2) ch=(char)tolower((unsigned char)ch);
            std::string w2=w;      for(auto&ch:w2) ch=(char)tolower((unsigned char)ch);
            if(w2.empty()||n2.find(w2)!=std::string::npos) m.enabled=true;
        }
    }
    if(g_macroAllowInjected){
        // dump the counters a few seconds in, then quit: the script only has to read one file
        std::thread([]{
            Sleep(9000);
            FILE* lf=fopen("macrotest.log","w");
            if(lf){ fprintf(lf,"armed=%d triggers=%d steps=%d sendinput_accepted=%d last=%s",
                            (int)g_macroArmed, g_macroTrigSeen.load(), g_macroSteps.load(),
                            g_macroSent.load(), g_macroLog.c_str()); fclose(lf); }
        }).detach();
    }
    if(wcsstr(cmd,L"--komolog")) g_komoLog=true;   // append komorebi service events to komorebi.log
    if(wcsstr(cmd,L"--lockshot")){ g_helloNoAuto=true; g_useLockScreen=true; DoLock(); }   // lock UI, no real Hello prompt
    if(testmedia){ g_nowPlaying=true; g_mediaFaked=true;   // the flag exists to show the toast, so never let config hide it
        g_md.has=true; g_md.playing=true; g_md.title="Kerala"; g_md.artist="Bonobo"; g_md.album="Migration";
        g_md.pos=42; g_md.dur=222; g_medUntil=GetTickCount64()+600000;   // long window for screenshotting
        // AETHER_TESTMEDIA="Artist|Title|duration|position" stages any song (e.g. one with synced lyrics)
        { char ev[512]; DWORD en=GetEnvironmentVariableA("AETHER_TESTMEDIA",ev,sizeof(ev));
          if(en>0 && en<sizeof(ev)){ std::string e(ev); std::vector<std::string> pr; size_t i=0;
              while(true){ size_t k=e.find('|',i); pr.push_back(e.substr(i,k==std::string::npos?std::string::npos:k-i)); if(k==std::string::npos) break; i=k+1; }
              if(pr.size()>=2){ g_md.artist=pr[0]; g_md.title=pr[1]; g_md.album.clear(); }
              if(pr.size()>=3) g_md.dur=atof(pr[2].c_str());
              if(pr.size()>=4) g_md.pos=atof(pr[3].c_str()); } }
        g_md.posTick=GetTickCount64(); }
    // sensors.sidecar: launch it UNELEVATED at startup. It is enabled from a Settings button that
    // does the one elevated run, and a shell that fires a UAC prompt every login would be awful.
    if(g_sensorSidecar && SidecarInstalled() && !SidecarRunning()) SidecarStart(false);
    InitStats(); std::thread([]{ FetchWeather(); }).detach();   // don't block startup on the network
    CwEnsureExamples();                          // config\widgets: examples, aether.py, README
    std::thread(MediaThread).detach(); std::thread(SensorThread).detach();   // temps (LHM / ACPI)
    std::thread(AudioSpectrumThread).detach();   // real spectrum from the system audio mix
    std::thread(SwWorker).detach();              // Alt+Tab live window thumbnails, off the render thread
    // komorebi's event stream. The thread doubles as the "is komorebi up yet" poll, so it is safe
    // to start on a machine that does not have it - it just never reports live.
    if(g_wsSource!=WSSRC_VDESK){ EnsureExplorerForKomorebi(); KomorebiStart(); }
    // Launch komorebi itself if asked to. Deferred to a worker: `komorebic start` shells out and
    // waits on process creation, and the shell must not stall its own startup on that. The poll
    // thread above is what notices it come up.
    if(g_komoAutoStart && g_wsSource!=WSSRC_VDESK && !g_safeMode){
        std::thread([]{
            Sleep(1200);                                  // let the shell finish coming up first
            if(g_komoLive.load()) return;                 // already running: nothing to do
            if(!KomoInstalled()) return;
            EnsureExplorerForKomorebi();
            Sleep(2500);                          // explorer needs to register its COM classes first
            KomorebiLaunch(g_komoMasir,g_komoWhkd);
            if(g_niriMode){
                // the layout can only be set once komorebi answers; wait for the poll to see it
                for(int i=0;i<40 && !g_komoLive.load();i++) Sleep(250);
                if(g_komoLive.load()){ KomoSetLayoutAll(L"scrolling"); Sleep(400); KomoApplyScrollCols(g_niriCols); }
            }
        }).detach();
    }
    RefreshDock(); LScanApps();
    g_micMuted=GetMicMute(g_micPresent);
    if(g_keepAwake) ApplyKeepAwake();
    if(g_slideSec>0) WallScan();                     // the slideshow needs the list up front
    if(g_restoreOnLaunch && !g_lastWall.empty()){    // waypaper --restore
        std::wstring lw=U82W(g_lastWall);
        if(GetFileAttributesW(lw.c_str())!=INVALID_FILE_ATTRIBUTES){
            SystemParametersInfoW(SPI_SETDESKWALLPAPER,0,(void*)lw.c_str(),SPIF_UPDATEINIFILE|SPIF_SENDCHANGE);
            LoadDeskWallpaper(); g_deskDirty=true; } }
    ULONGLONG lastSlide=GetTickCount64();
    if(!noTray && !g_safeMode) InitTrayHost();   // become the system-tray host (Cairo technique) so our bar shows real tray icons
    InitThumbHost(hInst);                      // DWM window-preview host for the taskbar buttons
    CloseHandle(CreateThread(nullptr,0,KbHookThread,nullptr,0,nullptr));   // Super key -> launcher (its own thread - see KbHookThread)
    SyncDeepCornerHook();   // only hooks window moves when the clipping corners are actually on
    if(!g_safeMode){ SetMinimizedMetrics(); SweepMinimizedBubbles(); InstallRestoreNudge(); Ws2InstallHooks(); }   // minimized windows vanish (not tile bottom-left); Chromium repaints after a restore
    // BEFORE ClaimShellRoles on purpose: that calls SetShellWindow, and once a shell window is
    // registered Explorer can decide a shell already exists and never build the desktop tree. Let
    // it create Progman/WorkerW first, then take the roles we want.
    if(g_isShell && !probe && !g_safeMode) EnsureDesktopHost();
    // The shell-window role goes to an invisible 1px window, NOT the desk layer: Windows pins the shell
    // window to the bottom of the z-order, and the desk layer must be able to sit above Explorer's
    // desktop (live wallpapers render inside it). Outside shell mode nothing is registered.
    if(g_isShell){
        g_shellHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED,
            wc.lpszClassName, L"AetherShellWindow", WS_POPUP, g_vs.left, g_vs.top, 1, 1, nullptr,nullptr,hInst,nullptr);
        if(g_shellHwnd){ SetLayeredWindowAttributes(g_shellHwnd,0,0,LWA_ALPHA); ShowWindow(g_shellHwnd,SW_SHOWNOACTIVATE); }
    }
    ClaimShellRoles(g_shellHwnd? g_shellHwnd : g_deskHwnd,g_barHwnd);     // SetShellWindow / SetTaskmanWindow / RegisterShellHookWindow
    PlaceDeskLayer(g_deskHwnd);
    if(g_isShell && !probe && !g_safeMode){
        RunLoginItems();                       // Explorer is not here to run them
        // There is no Explorer taskbar to hide, but the work area must still be the bubble so that
        // maximized windows land inside it and leave the bar's margin free. This used to call
        // SetWindowsTaskbar(false), which RESTORED whatever the previous run left behind — a stale
        // rect that only looked right while the bar never moved.
        // Journal it exactly like the taskbar takeover does: this changes the work area, and a work
        // area left inset after a crash is the damage nobody can diagnose.
        if(!g_taskbarHidden){
            std::vector<RECT> orig;
            for(auto& m:g_mons){ MONITORINFO mi={sizeof(mi)};
                HMONITOR h=MonitorFromPoint(POINT{m.rc.left+2,m.rc.top+2},MONITOR_DEFAULTTONEAREST);
                orig.push_back(GetMonitorInfoW(h,&mi)? mi.rcWork : m.rc); }
            g_savedWorkAreas=orig; g_taskbarHidden=true;   // "we own the work area" — drives cleanup
            RecoveryWrite(orig);
        }
        ApplyWorkAreas();       // every monitor, not just the primary
    }
    RefreshWorkspaces();
    WriteExamplePlugin(false);                 // first run: leave an example + the API reference
    PluginsScan();                             // load plugins\*.lua (Lua, sandboxed, hot-reloaded)
    if(g_sideView==1)RefreshWifi(); if(g_sideView==2)RefreshBt();
    ULONGLONG lastSlow=GetTickCount64(), lastWx=GetTickCount64(), lastDock=GetTickCount64();
    LARGE_INTEGER freq,prev; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&prev);
    g_tabX=(float)g_tab;
    bool shown=false;                                    // is the overlay window currently visible?

    bool quit=false;
    if(snipnow){ Sleep(300); StartSnip(); }   // verification: open the snipper once the windows exist
    if(sniptest){ Sleep(300);
        bool cap=CaptureVirtualScreen();
        int pw=400,ph=300,px=10,py=10; bool saved=false;
        if(cap){
            std::vector<uint8_t> crop((size_t)pw*ph*4);
            for(int y=0;y<ph;y++) memcpy(crop.data()+(size_t)y*pw*4,
                g_snipPixels.data()+(((size_t)(py+y)*g_snipW+px)*4),(size_t)pw*4);
            saved=SavePng(U82W(ExeDir()+"sniptest.png").c_str(),crop.data(),pw,ph);
        }
        FILE* fl=fopen((ExeDir()+"sniptest.log").c_str(),"w");
        if(fl){ fprintf(fl,"capture=%d vs=%dx%d saved=%d\n",(int)cap,g_snipW,g_snipH,(int)saved); fclose(fl); }
        PostQuitMessage(0); }
    // The FIRST Present on a fresh composition swapchain is expensive (DWM has to realise the
    // surface). Left until the switcher opens, that cost landed on the very frame the entrance
    // animation starts and showed up as a 250ms stall. Pay it once, here, drawing nothing.
    { ImGui::SetCurrentContext(g_ctxSw); NewFrameL(); RenderL(g_swRtv,g_swSc); }

    // Surviving this long means the start was good: wipe the crash-loop strikes so a single bad day
    // (a driver reset, a forced kill) can never accumulate into a safe-mode boot.
    ULONGLONG healthyAt=GetTickCount64()+20000; bool healthy=false;
    while(!quit){
        MSG msg; { STALL(message_pump); while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){ TranslateMessage(&msg);
            DispatchMessage(&msg); if(msg.message==WM_QUIT)quit=true; } }   // slow messages are timed inside WndProc / TrayWndProc
        HookDrain();                                   // keyboard-hook work queued by KbHookThread
        SaveConfigFlush();                             // the last save of a slider drag, once the button is up
        StvTick();                                     // Strive: workspace banner watch, click-to-skip
        { static ULONGLONG bootT=GetTickCount64(); static bool introDone=false;   // Strive intro on startup (opt-in)
          if(!introDone && GetTickCount64()-bootT>1800){ introDone=true; if(g_stvIntroStart && !ShellLocked()) StvIntroStart(); } }
        { static bool wasLocked=false;                 // ...and after an unlock: the lock only hides on a good password / Hello
          if(wasLocked && !g_lockShow && g_lockArm==0 && g_stvIntroUnlock) StvIntroStart();
          wasLocked=g_lockShow; }
        { static bool wd=false; if(!wd){ wd=true; StartHangWatchdog(); } }
        LoopHeartbeat(); DrainUiQueue(); SlTick();          // SafeLaunch.h
        { static ULONGLONG bootAt=GetTickCount64(); static bool offered=false;   // a new install: open Settings on the tour
          if(!offered && !g_tourSeen && !g_safeMode && GetTickCount64()-bootAt>3000 && !ShellLocked()){ offered=true; OpenSettingsPage("aether"); } }
        { static ULONGLONG mmAt=0; ULONGLONG nowMm=GetTickCount64();   // keep minimized windows hidden, whatever reset it
          if(!g_safeMode && nowMm-mmAt>2000){ mmAt=nowMm; SetMinimizedMetrics(); SweepMinimizedBubbles(); } }
        if(quit)break;
        if(!healthy && GetTickCount64()>healthyAt){ healthy=true; SetStartupStrikes(0); }

        ULONGLONG now=GetTickCount64();
        if (now-lastSlow>=1000){ double dt=(now-lastSlow)/1000.0; lastSlow=now; SampleCpu();SampleGpu();SampleNet(dt);SampleSlow();
            g_volCache=GetVolume();   // keep the dashboard/bar volume readout live, not just when the sidebar opens
            g_micMuted=GetMicMute(g_micPresent);
            auto ph=[](std::vector<float>&v,float x){ v.push_back(x); if(v.size()>90)v.erase(v.begin()); };
            ph(g_st.cpuHist,(float)g_st.cpuUsage); ph(g_st.gpuHist,(float)g_st.gpuUsage);
            ph(g_st.memHist, g_st.memTotal?(float)g_st.memUsed/g_st.memTotal:0);
            ph(g_st.diskHist,g_st.diskTotal?(float)g_st.diskUsed/g_st.diskTotal:0);
        }
        if (now-lastWx>=3600000ULL){ lastWx=now; std::thread([]{ FetchWeather(); }).detach(); }
        if(g_catDirty){ g_catDirty=false; LoadCatImage(g_catPath); }   // media-card image
        AdvanceImages();                                   // widget image/GIF frames
        if(g_snipPending>0){ if(--g_snipPending==0) StartSnip(); }
        // plugins: pick up new/edited/removed .lua files (hot reload)
        if (g_pluginsOn && now>=g_plNextScan){ g_plNextScan=now+1500; PluginsScan(); PluginsWriteStatus(); }
        // desktop-surface plugins animate, so the otherwise-static desktop layer must repaint
        if (PluginsActive(PSURF_DESKTOP)) g_deskDirty=true;
        // The desktop layer is deliberately static and only repaints when marked dirty. Mirrored
        // widgets need it moving: twice a second is enough for a clock and a changing number, but
        // idle motion (turning shapes, the gauge surfaces) has to be a real frame rate or it stutters
        // visibly. 30fps rather than 60 - these are slow morphs, and this repaints EVERY monitor.
        if(!g_deskTabName.empty()){
            static ULONGLONG lastDw=0;
            ULONGLONG every = g_idleMotion ? 33ULL : 500ULL;
            if(now-lastDw>=every){ lastDw=now; g_deskDirty=true; }
        }
        // slideshow: auto-advance the wallpaper (waypaperd / pyprland wallpapers plugin)
        if (g_slideSec>0 && now-lastSlide >= (ULONGLONG)g_slideSec*1000ULL){
            lastSlide=now;
            SlideshowAdvance(+1); }
        // the cheap "which desktop am I on" read, four times a second
        { static ULONGLONG lastWs=0; if(now-lastWs>=250ULL){ lastWs=now; STALL(RefreshWorkspacesFast); RefreshWorkspacesFast(); } }
        if (g_taskListDirty || now-lastDock>=2000ULL){ lastDock=now; g_taskListDirty=false;
            { STALL(RefreshDock); RefreshDock(); } { STALL(RefreshWorkspaces); RefreshWorkspaces(); }   // apps + workspaces
            { std::lock_guard<std::mutex> lk(g_systrayMtx);           // drop tray icons whose owner died
                for(size_t i=0;i<g_systray.size();){ if(!IsWindow(g_systray[i].hwnd)){ if(g_systray[i].tex)g_systray[i].tex->Release(); g_systray.erase(g_systray.begin()+i);} else i++; } }
            if(g_taskbarHidden){ HWND tb=FindWindowW(L"Shell_TrayWnd",nullptr);   // Explorer's, not ours
                while(tb==g_trayHwnd&&tb) tb=FindWindowExW(nullptr,tb,L"Shell_TrayWnd",nullptr);
                if(tb&&IsWindowVisible(tb)) ShowWindowAsync(tb,SW_HIDE); } }

        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        float dt=(float)(t.QuadPart-prev.QuadPart)/freq.QuadPart; prev=t; if(dt>0.1f)dt=0.1f;
        g_frameDt=dt;

        // advance animated-gif background frame
        if (g_bgMode==1 && g_bgFrames.size()>1){ g_bgClock+=dt*1000.0;
            while(g_bgFrame<(int)g_bgDelays.size() && g_bgClock>=g_bgDelays[g_bgFrame]){ g_bgClock-=g_bgDelays[g_bgFrame]; g_bgFrame=(g_bgFrame+1)%(int)g_bgFrames.size(); } }

        // wallpaper circular-wipe reveal (drives the otherwise-static desktop layer)
        if (g_wipe<1.0f){ g_wipe=std::min(1.0f,g_wipe+dt*(1000.0f/std::max(80,g_transMs))); g_deskDirty=true;
            if(g_wipe>=1.0f && g_deskWallOld && transTest.empty()){ g_deskWallOld->Release(); g_deskWallOld=nullptr; } }
        if (!transTest.empty() && g_wipe>=1.0f){ g_wipe=0.0f; g_deskDirty=true; }   // loop the preview
        // ---- per-monitor Wallpaper Engine hand-overs ----
        for(int wm=0; wm<16; wm++){
            WeTrans& T=g_weTr[wm]; if(!T.phase) continue;
            ULONGLONG nowT=GetTickCount64();
            g_deskDirty=true;
            if(T.scrim){
                if(T.phase==1){                                   // fade the interior out over the old wallpaper
                    T.fade=std::min(1.0f,T.fade+dt/(g_liveTransStyle==0? 0.60f : 0.22f));
                    if(T.fade>=1.0f){ if(!T.cmd.empty()) WeControl(T.cmd); T.phase=2; T.t0=nowT; T.liveAt=0; T.sawDrop=false; }
                } else if(T.phase==2){                            // hold while Wallpaper Engine swaps
                    bool live=g_monLive[wm];
                    if(!live) T.sawDrop=true;
                    bool done;
                    if(T.toImage){ g_monLive[wm]=false; done = nowT-T.t0>250; }
                    else {
                        if(live && (T.sawDrop || nowT-T.t0>250) && !T.liveAt) T.liveAt=nowT;
                        done = (T.liveAt && nowT-T.liveAt>900) || nowT-T.t0>8000;
                    }
                    if(done){ T.phase=3; T.fade=1.0f; }
                } else if(T.phase==3){                            // and back in over what is there now
                    T.fade-=dt/(g_liveTransStyle==0? 0.70f : 0.45f);
                    if(T.fade<=0.0f){ T.fade=0; WeTransEnd(T); T.scrim=false; }
                }
            } else if(T.phase==2){                                // still image held until the live one is up
                if(g_monLive[wm] && !T.liveAt) T.liveAt=nowT;
                if((T.liveAt && nowT-T.liveAt>700) || nowT-T.t0>8000){
                    T.L.tex=T.L.under; T.L.tw=T.L.uw; T.L.th=T.L.uh; T.L.under=nullptr;   // fade that same image off
                    T.phase=3; T.fade=1.0f; }
            } else if(T.phase==3){
                T.fade-=dt/0.45f;
                if(T.fade<=0.0f) WeTransEnd(T);
            } else WeTransEnd(T);
        }
        // ---- advance every running transition, each on its own duration ----
        if(!g_wipeStack.empty()){
            for(auto& L:g_wipeStack) L.prog=std::min(1.0f,L.prog+dt*(1000.0f/std::max(80.0f,L.ms)));
            g_deskDirty=true;
            // Retire only from the FRONT: layers composite in order, so a finished layer can only
            // be folded into the base once everything older than it has finished too.
            while(!g_wipeStack.empty() && g_wipeStack.front().prog>=1.0f){
                WipeLayer f=g_wipeStack.front();
                if(g_deskBase) g_deskBase->Release();
                g_deskBase=f.tex; g_deskBaseW=f.tw; g_deskBaseH=f.th;      // ref transfers
                if(f.under) f.under->Release();
                g_wipeStack.erase(g_wipeStack.begin());
            }
        }

        // ---- monitors changed (plugged, unplugged, rearranged): rebuild everything that is
        //      sized to the screen layout ----
        if(g_monsDirty){
            g_monsDirty=false;
            RefreshMonitors();
            g_bars.resize(std::max<size_t>(1,g_mons.size()));
            int nvw=g_vs.right-g_vs.left, nvh=g_vs.bottom-g_vs.top;
            MoveOverlay(g_barHwnd, g_barSc, &g_barRtv, g_vs.left,g_vs.top,nvw,nvh);
            MoveOverlay(g_deskHwnd,g_deskSc,&g_deskRtv,g_vs.left,g_vs.top,nvw,nvh);
            // The snipper spans the virtual screen too, and was the ONE layer never resized here -
            // its swapchain kept the size it had at startup, so taking a screenshot after plugging
            // a monitor in drew into a buffer the wrong shape.
            MoveOverlay(g_snipHwnd,g_snipSc,&g_snipRtv,g_vs.left,g_vs.top,nvw,nvh);
            // the decoration layer tracks the FULL virtual screen, not g_vs - and it was never
            // resized here at all, so plugging a monitor in left it stuck at the old extent
            MoveOverlay(g_decoHwnd,g_decoSc,&g_decoRtv,g_dvs.left,g_dvs.top,
                        g_dvs.right-g_dvs.left, g_dvs.bottom-g_dvs.top);
            PlaceDeskLayer(g_deskHwnd);
            HostLayout();
            POINT cpm; GetCursorPos(&cpm); g_actMon=MonIndexAt(cpm);
            { const RECT& r=MonRect(g_actMon); g_mx=r.left; g_my=r.top; g_mw=r.right-r.left; g_mh=r.bottom-r.top; }
            PlaceOverlaysOnActive();
            if(g_taskbarHidden) ApplyWorkAreas();
            g_rgnCache.clear(); g_rgnMultiCache.clear();
            // every cached window rect and every animation in flight describes the layout that just went away
            Ws2MonitorsChanged();
            T2CancelAll();
            LoadDeskWallpaper(); g_deskDirty=true;
        }
        // ---- bubble geometry changed: re-inset the work area, then re-fit the open windows ----
        // Deferred until the mouse button is up: SPI_SETWORKAREA broadcasts WM_SETTINGCHANGE to
        // every window on the system, and re-maximizing apps mid-drag would fight the slider.
        if(g_bubbleGeomDirty && !(GetAsyncKeyState(VK_LBUTTON)&0x8000)){
            g_bubbleGeomDirty=false;
            if(g_taskbarHidden) ApplyWorkAreas();
            ReflowWindows();
        }
        // A window opened or was restored since last time: give it the bubble too. Cheap enough at
        // this cadence, and it is the only way something launched later lands inside.
        { static ULONGLONG lastFit=0;
          if(GetTickCount64()-lastFit>1500){ lastFit=GetTickCount64();
              { STALL(EnsureOverlayGeometry); EnsureOverlayGeometry(); }   // repairs a stuck layer without needing a restart
              if(!(GetAsyncKeyState(VK_LBUTTON)&0x8000)){
                  { STALL(EnsureWorkAreas); EnsureWorkAreas(); }                 // re-assert it if Windows has drifted back
                  if(g_confineApps && g_bubble){ STALL(ReflowWindows); ReflowWindows(); }
              } } }
        // ---- the pop-up panels follow the monitor the cursor is on ----
        UpdateActiveMonitor();
        // ---- hover-reveal (Caelestia's inTopPanel edge detection), all in LOGICAL px ----
        float LW=g_mw/g_uiScale, LH=g_mh/g_uiScale;
        // drawer geometry comes from the SAME Panel descriptor the draw code uses
        PRect dr=PanelRect(g_pn[PN_DRAWER],LW,LH);
        // morph the drawer height toward the active tab's content — a spring with slight overshoot,
        // mimicking Caelestia's expressiveDefaultSpatial curve (cubic-bezier 0.38,1.21,0.22,1 ~500ms)
        { float th=dr.h*DrawerHeightFrac();
          if(g_drawerMorphH<=8.0f){ g_drawerMorphH=th; g_drawerMorphV=0; }   // seed first frame (no jump)
          else { float sdt=std::min(dt,0.033f); const float k=100.0f, damp=15.0f;  // underdamped -> ~3% overshoot, ~0.5s settle
              g_drawerMorphV += ((th-g_drawerMorphH)*k - g_drawerMorphV*damp)*sdt;
              g_drawerMorphH += g_drawerMorphV*sdt; } }
        // same spring for the width, then RE-ANCHOR: dr.w=... alone would leave dr.x at the position
        // computed for the old width, so a centred drawer would visibly drift left as it widened.
        { float tw=dr.w*DrawerWidthFrac();
          if(g_drawerMorphW<=8.0f){ g_drawerMorphW=tw; g_drawerMorphWV=0; }
          else { float sdt=std::min(dt,0.033f); const float k=100.0f, damp=15.0f;
              g_drawerMorphWV += ((tw-g_drawerMorphW)*k - g_drawerMorphWV*damp)*sdt;
              g_drawerMorphW += g_drawerMorphWV*sdt; } }
        dr=PanelRect(g_pn[PN_DRAWER],LW,LH,g_drawerMorphW);
        dr.h=g_drawerMorphH;
        float DW=dr.w, DH=dr.h, dx=dr.x;
        float slide=g_reveal;   // already curved by Cael::anim; easing again would flatten it
        PRect drs=dr; PanelSlide(g_pn[PN_DRAWER],drs,slide); float dy=drs.y;
        POINT cp; GetCursorPos(&cp); float lx=(cp.x-g_mx)/g_uiScale, ly=(cp.y-g_my)/g_uiScale;
        bool fsApp = g_hideOnFullscreen && FullscreenAppActive();   // pull the shell off fullscreen apps/games
        // A window is rounded once, when it first appears - so a game that goes full-screen AFTER that
        // kept the corners and the border line. Re-decide for the foreground window whenever
        // full-screen starts or stops.
        { static bool s_fsWas=false; bool fsNow=FullscreenAppActive();
          if(fsNow!=s_fsWas){ s_fsWas=fsNow; ApplyWindowRound(GetForegroundWindow()); } }
        // Locked: nothing of ours may be open over the lock screen. Arming already snaps these shut
        // once; this keeps them shut, whatever tries to reopen them (hotkeys, the Win-key hook, IPC,
        // a hover on a screen edge).
        if(ShellLocked()){
            g_launShow=false; g_sessShow=false; g_setShow=false; g_fmShow=false; g_swShow=false;
            g_trayOpen=false; g_dockMenuOpen=false; g_wPaletteOpen=false; g_barMenu=false; g_barFly=0;
            g_drawerForceUntil=0; g_sideForceUntil=0; g_notifForceUntil=0;
        }
        // Clip every overlay to what it draws, so its transparent area stops eating clicks.
        // The launcher and settings dim the whole screen on purpose, so they stay unclipped.
        // Windows move and resize without the taskbar list changing, so the shaping cannot wait for
        // the next RefreshDock - it needs its own beat. Cheap: it early-outs per window unless the
        // frame actually moved.
        { static ULONGLONG lastRgn=0; ULONGLONG nowRgn=GetTickCount64();
          if(nowRgn-lastRgn>250){ lastRgn=nowRgn; SweepWindowRegions(); } }
        ApplyHitRegion(g_sideHwnd,  g_sideRect,  36);
        ApplyHitRegion(g_notifHwnd, g_notifRect, 30);
        ApplyHitRegion(g_swHwnd,    g_swRect,    0);
        ApplyHitRegion(g_medHwnd,   g_medRect,   26);
        ApplyHitRegion(g_fmHwnd,    g_fmRect,    0);
        // the region clips RENDERING too: the neck toward the notifications must be inside it
        ApplyHitRegions(g_hwnd, std::vector<RECT>{ g_drawerRect, g_dashNeckRect }, 40);
        ApplyHitRegion(g_dockHwnd,  g_dockRect,  30);
        bool nearTop  = g_pn[PN_DRAWER].visible && PanelEdgeHot(g_pn[PN_DRAWER],lx,ly,LW,LH,30.0f);
        bool overDraw = g_reveal>0.12f && PanelOver(drs,lx,ly,24.0f);
        bool calPop   = GetTickCount64()<g_drawerForceUntil;
        // while editing a tab the drawer stays open on its own; pressing Done clears g_editTab and
        // it closes normally (the old 10-minute force is what left it stuck on screen)
        float target = ((probe||g_forceDrawer||calPop||nearTop||overDraw||g_editTab)&&!fsApp&&!ShellLocked()&&g_pn[PN_DRAWER].visible)?1.0f:0.0f;
        // Was an exponential lerp, then eased AGAIN at the draw site and clamped to [0,1] - so it
        // could never overshoot and always landed soft. One Material spatial curve instead: it
        // springs slightly past its resting position and settles, which is the Caelestia feel. The
        // draw sites now use g_reveal RAW (no second easing) or the overshoot would be thrown away.
        g_reveal = MotionAnim(MP_DASHBOARD,820001,target);
        // content settles a beat after the panel (staged open animation, defect D5)
        float ct=(g_reveal>0.55f)?1.0f:0.0f;
        g_drawerContent += (ct-g_drawerContent)*std::min(1.0f,dt*10.0f);
        if (fabsf(ct-g_drawerContent)<0.002f) g_drawerContent=ct;
        // WS_EX_NOACTIVATE blocks activation outright, so it has to come OFF while a calendar note
        // is being edited - otherwise the drawer never receives WM_CHAR and the field is dead.
        { static bool typingWas=false; bool typing=DrawerTyping();      // a calendar note or the profile status editor
          if(typing!=typingWas){
              typingWas=typing;
              LONG_PTR ex=GetWindowLongPtrW(g_hwnd,GWL_EXSTYLE);
              if(typing){
                  SetWindowLongPtrW(g_hwnd,GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_NOACTIVATE);
                  SetForegroundWindow(g_hwnd); SetActiveWindow(g_hwnd); SetFocus(g_hwnd);
              } else {
                  SetWindowLongPtrW(g_hwnd,GWL_EXSTYLE, ex | (LONG_PTR)WS_EX_NOACTIVATE);
              } } }
        // ---- wallpaper changed underneath us: restage the desktop and cross-fade the palette ----
        if(g_wallChanged.exchange(false)){
            // Only RESTAGE when somebody else changed it. SetWallpaperFile hands the path to Windows
            // on a background thread, and that lands right here - so our own apply was firing this,
            // and the plain loader resets the base and drops every running transition. That is what
            // cut the animation off after a single frame. Comparing against the path we applied
            // tells the two apart; an external change still restages exactly as before.
            bool ours=false;
            { wchar_t cur[MAX_PATH]={0};
              if(SystemParametersInfoW(SPI_GETDESKWALLPAPER,MAX_PATH,cur,0) && cur[0] && !g_lastWall.empty())
                  ours = _wcsicmp(cur,U82W(g_lastWall).c_str())==0; }
            if(!ours) LoadDeskWallpaper();
            if(g_dynamicColor && g_wallSeedReady.exchange(false))
                ApplySeedPalette(g_wallSeedVal.load());   // PalCommit turns this into a fade
            g_deskDirty=true;
        }
        if(PalTick(dt)) g_deskDirty=true;                 // drive the colour cross-fade
        // A zoom/defocus jump must NOT also slide sideways, or you get both at once. Snap g_tabX for
        // those styles and let g_tabFxT carry the whole animation; a plain tab-strip click still slides.
        // Opening on a different tab than it closed on used to play the page SWIPE while the panel was still
        // growing out of the border: the cards slid in sideways, cut off at both edges, with the neighbouring
        // page poking in on the right. While the drawer is (nearly) closed the page just snaps to its tab.
        if(g_reveal<0.5f || (g_tabFx!=TT_SLIDE && g_tabFxT<0.999f)) g_tabX=(float)TabSlot();
        else g_tabX += ((float)TabSlot()-g_tabX)*std::min(1.0f,dt*13.0f);
        g_tabFxT += (1.0f-g_tabFxT)*std::min(1.0f,dt*5.5f);      // ~350ms settle
        if(g_tabFxT>0.998f) g_tabFxT=1.0f;

        // show the window only while the drawer is out; hidden otherwise so NOTHING
        // underneath is blocked (a hidden window is excluded from hit-testing entirely)
        bool drawerShown = (g_reveal>0.003f) || (target>0.5f);
        if (drawerShown!=shown){ shown=drawerShown;
            if (drawerShown){ if(g_bgMode==0){ if(g_acrylicTex)g_acrylicTex->Release(); g_acrylicTex=CaptureL(dr.x,dr.y,DW,DH); } ShowWindow(g_hwnd,SW_SHOWNOACTIVATE); }
            else ShowWindow(g_hwnd,SW_HIDE); }
        if (g_reveal<0.004f || !g_pn[PN_DRAWER].visible) g_drawerRect = RECT{0,0,0,0};
        else g_drawerRect = PanelHitRect(drs);

        // ---- modern bottom dock: always shown (or reveal-on-bottom-edge when autohide) ----
        bool dockNearBottom = cp.x>=g_mx && cp.x<g_mx+g_mw && cp.y>=g_my+g_mh-3;
        bool overDockBand   = cp.x>=g_mx && cp.x<g_mx+g_mw && cp.y>=g_my+g_mh-(LONG)(DOCKWINH*g_uiScale);
        bool dockWant = g_dockOn && !fsApp && !ShellLocked() && (!g_dockAutohide || dockNearBottom || (g_dockReveal>0.4f && overDockBand));
        float dockTarget = dockWant?1.0f:0.0f;
        g_dockReveal += (dockTarget-g_dockReveal)*std::min(1.0f,dt*13.0f);
        if(fabsf(dockTarget-g_dockReveal)<0.001f) g_dockReveal=dockTarget;
        bool dockShown = g_dockOn && (g_dockReveal>0.003f || dockTarget>0.5f);
        { static bool dshown=false; if(dockShown!=dshown){ dshown=dockShown;
            if(dockShown) SetWindowPos(g_dockHwnd,HWND_TOPMOST,g_mx,g_my+g_mh-DOCKWINH,g_mw,DOCKWINH,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            else ShowWindow(g_dockHwnd,SW_HIDE); } }

        // ---- bar hover-reveal, PER MONITOR (each screen has its own strip and its own state) ----
        if((int)g_bars.size()<(int)g_mons.size()) g_bars.resize(std::max<size_t>(1,g_mons.size()));
        bool anyBarShown=false;
        std::vector<RECT> barRects;
        for(size_t bi=0; bi<std::max<size_t>(1,g_mons.size()); bi++){
            BarState& BS=g_bars[bi];
            if(!BarOnMon((int)bi)){ BS.reveal=0; BS.rect=RECT{0,0,0,0}; continue; }
            RECT mr = g_mons.empty()? RECT{g_mx,g_my,g_mx+g_mw,g_my+g_mh} : g_mons[bi].rc;
            float MW=(mr.right-mr.left)/g_uiScale, MH=(mr.bottom-mr.top)/g_uiScale;
            float mlx=(cp.x-mr.left)/g_uiScale, mly=(cp.y-mr.top)/g_uiScale;   // cursor on THIS monitor
            bool onThis = cp.x>=mr.left&&cp.x<mr.right&&cp.y>=mr.top&&cp.y<mr.bottom;
            PRect br=PanelRect(g_pn[PN_BAR],MW,MH);
            PRect brs=br; PanelSlide(g_pn[PN_BAR],brs,EaseOutCubic(std::clamp(BS.reveal,0.0f,1.0f)));
            // the hot zone reaches past the strip so the cursor can travel onto tooltips / the menu
            bool menuHere = (g_barMenu && g_barMenuMon==(int)bi) || (g_appMenu && g_appMenuMon==(int)bi) || (g_barFly && g_barFlyMon==(int)bi) || (g_appPrev && g_appPrevMon==(int)bi)
                          || (g_trayOpen && g_trayFlyMon==(int)bi);
            float barPad = menuHere? 300.0f : g_pn[PN_BAR].gap+18.0f;
            bool nearEdge = onThis && PanelEdgeHot(g_pn[PN_BAR],mlx,mly,MW,MH,8.0f);
            bool overBar; { int be=g_pn[PN_BAR].edge;
                if(be==EDGE_LEFT)        overBar = mlx<brs.x+brs.w+barPad && mly>0 && mly<MH;
                else if(be==EDGE_RIGHT)  overBar = mlx>brs.x-barPad       && mly>0 && mly<MH;
                else if(be==EDGE_TOP)    overBar = mly<brs.y+brs.h+barPad && mlx>0 && mlx<MW;
                else                     overBar = mly>brs.y-barPad       && mlx>0 && mlx<MW;
                overBar = onThis && BS.reveal>0.12f && overBar; }
            bool fsHere = g_hideOnFullscreen && FullscreenOnMon(mr);
            float barTarget=((probe||g_forceBar||!g_barAutoHide||menuHere||nearEdge||overBar)&&!fsHere)?1.0f:0.0f;
            BS.reveal += (barTarget-BS.reveal)*std::min(1.0f,dt*13.0f);
            if (fabsf(barTarget-BS.reveal)<0.001f) BS.reveal=barTarget;
            bool showIt=(BS.reveal>0.003f)||(barTarget>0.5f);
            if(showIt){
                anyBarShown=true;
                if(!BS.shown && g_bgMode==0){    // frost capture, in this monitor's own coordinates
                    if(BS.acrylic) BS.acrylic->Release();
                    BS.acrylic=CaptureRegionBlur((int)(mr.left+br.x*g_uiScale),(int)(mr.top+br.y*g_uiScale),
                                                 (int)(br.w*g_uiScale),(int)(br.h*g_uiScale)); }
                barRects.push_back(BS.rect);
            } else BS.rect=RECT{0,0,0,0};
            BS.shown=showIt;
        }
        if(RecHudActive()) anyBarShown=true;
        { static bool alertsWere=false; bool al2=AlertsActive(); if(al2||alertsWere) anyBarShown=true; alertsWere=al2; }
        if(g_alertRect.right>g_alertRect.left) barRects.push_back(g_alertRect);
        if(TourCoachActive()) anyBarShown=true;
        if(g_tourCoachRect.right>g_tourCoachRect.left) barRects.push_back(g_tourCoachRect);        // the recording checker draws in the bar window, even with no bar out
        if(g_recHudRect.right>g_recHudRect.left) barRects.push_back(g_recHudRect);
        { static bool bshown=false;
          if(anyBarShown!=bshown){ bshown=anyBarShown;
              ShowWindow(g_barHwnd, anyBarShown?SW_SHOWNOACTIVATE:SW_HIDE); } }
        if(g_appMenuRect.right>g_appMenuRect.left) barRects.push_back(g_appMenuRect);   // the app menu is clickable too
        if(g_barTipRect.right>g_barTipRect.left) barRects.push_back(g_barTipRect);      // ...and the tooltip must not be clipped
        if(g_barPopRect.right>g_barPopRect.left) barRects.push_back(g_barPopRect);      // caelestia bar popouts + layout toast
        ApplyHitRegions(g_barHwnd,barRects,36);

        // ---- quick settings: hover the BOTTOM-RIGHT CORNER (deliberately not the corners the
        //      taskbar occupies), then stay while the cursor is over the panel ----
        const Panel& QP=g_pn[PN_QS];
        float qsPanelW = PanelVert(QP)? QP.size : (QP.span>1.0f? QP.span : 384.0f);
        float qsPanelH = std::max(g_qsRows,200.0f);
        Panel qq=QP; if(!PanelVert(QP)) qq.size=qsPanelH;
        PRect qr=PanelRect(qq,LW,LH, PanelVert(QP)? qsPanelH : qsPanelW);
        // reveal from the corner the panel is anchored into, not a hardcoded bottom-right
        bool qsSecond;   // the OTHER edge of the corner it is anchored into also triggers it
        if(PanelVert(QP)){ bool nx=lx>=qr.x-40 && lx<=qr.x+qsPanelW+40;
            qsSecond = nx && ((QP.anchor>0.66f && ly>=LH-6) || (QP.anchor<0.34f && ly<=6)); }
        else             { bool ny=ly>=qr.y-40 && ly<=qr.y+qsPanelH+40;
            qsSecond = ny && ((QP.anchor>0.66f && lx>=LW-6) || (QP.anchor<0.34f && lx<=6)); }
        bool nearCorner = PanelEdgeHot(qq,lx,ly,LW,LH,60.0f, PanelVert(QP)? qsPanelH : qsPanelW) || qsSecond;
        bool overSide   = g_sideReveal>0.12f && PanelOver(qr,lx,ly,14.0f);
        if(g_sideStyle==1 && g_sideView==0){        // the merged sidebar: the whole right edge, full height
            nearCorner = nearCorner || (lx>=LW-4 && ly>LH*0.25f && ly<LH*0.80f);
            // the width is in screen px, and the action column sticks out to the LEFT of the panel: both have to count,
            // or reaching for those buttons left the "keep open" zone and the sidebar closed under the cursor
            { const float Sx=1.0f/std::max(0.5f,g_uiScale);
              float reach=(float)g_sideWidth*Sx + (g_sideActions.empty()? 0.0f : 106.0f*Sx) + 48.0f;
              overSide = g_sideReveal>0.12f && lx>=LW-reach; }
        }
        float sideTarget=((probe||g_forceSide||nearCorner||overSide||GetTickCount64()<g_sideForceUntil)
                          &&!fsApp&&!ShellLocked()&&QP.visible)?1.0f:0.0f;
        g_sideReveal = MotionAnim(MP_QS,830001,sideTarget);
        bool sideShown=(g_sideReveal>0.003f)||(sideTarget>0.5f);
        static bool sshown=false;
        if (sideShown!=sshown){ sshown=sideShown;
            if (sideShown){ if(g_bgMode==0){ if(g_sideAcrylic)g_sideAcrylic->Release();
                    g_sideAcrylic=CaptureL(qr.x,qr.y,qsPanelW,qsPanelH); }
                ShowWindow(g_sideHwnd,SW_SHOWNOACTIVATE); }
            else { ShowWindow(g_sideHwnd,SW_HIDE); g_sideView=0; g_qsRows=0; } }

        // ---- idle dim: fade the screen down after a spell with no input ----
        { LASTINPUTINFO li={sizeof(li)};
          ULONGLONG idleMs = GetLastInputInfo(&li)? (ULONGLONG)(GetTickCount()-li.dwTime) : 0;
          bool wantDim = g_forceDim ||
              (g_autoDim && idleMs > (ULONGLONG)g_dimAfter*1000ULL && !fsApp && !(g_md.has&&g_md.playing));
          Approach(g_dimAnim, wantDim?1.0f:0.0f, wantDim?1.6f:9.0f);   // fades in slowly, clears fast
          // idle lock (opt-in): after a longer idle spell with no fullscreen app or media, lock.
          // Latched so it fires once per idle period, not every frame.
          static bool lockedThisIdle=false;
          bool lockable = g_idleLock && !fsApp && !(g_md.has&&g_md.playing);
          if(lockable && idleMs > (ULONGLONG)g_idleLockAfter*1000ULL){
              if(!lockedThisIdle){ lockedThisIdle=true;
                  DoLock(); }
          } else if(idleMs < 2000ULL) lockedThisIdle=false;
          // ---- idle display-off (the DPMS equivalent Caelestia has and we did not) ----
          // Latched like the idle lock so it fires ONCE per idle spell: SC_MONITORPOWER is a
          // request, and re-sending it every frame would fight the first real input trying to wake
          // the panel back up. Skipped while a fullscreen app or media is running, same as dimming.
          static bool dpmsThisIdle=false;
          bool dpmsable = g_idleDisplayOff && !fsApp && !(g_md.has&&g_md.playing);
          if(dpmsable && idleMs > (ULONGLONG)g_idleDisplayAfter*1000ULL){
              if(!dpmsThisIdle){ dpmsThisIdle=true;
                  SendMessageTimeoutW(HWND_BROADCAST,WM_SYSCOMMAND,SC_MONITORPOWER,(LPARAM)2,
                                      SMTO_ABORTIFHUNG,200,nullptr); }
          } else if(idleMs < 2000ULL) dpmsThisIdle=false;
          // ---- idle hibernate: the last rung of the idle ladder (dim -> lock -> displays -> here) ----
          // Latched the same way, and LOCKS FIRST when the lock screen is on, so the machine never
          // comes back out of hibernation straight onto an unlocked desktop.
          static bool hibThisIdle=false;
          bool hibable = g_idleHibernate && !fsApp && !(g_md.has&&g_md.playing);
          if(hibable && idleMs > (ULONGLONG)g_idleHibernateAfter*1000ULL){
              if(!hibThisIdle){ hibThisIdle=true;
                  if(g_useLockScreen && !g_lockShow) DoLock();
                  DoHibernate(); }
          } else if(idleMs < 2000ULL) hibThisIdle=false;
          // pre-lock countdown: the last few seconds before an idle-lock show a lock logo + ticking
          // ring. Any input cancels it (unless it's a forced preview). When it hits 0 the lock opens
          // and this smoothly hands off. g_preLockForceEnd drives the manual "Lock countdown" preview.
          if(g_preLockForceEnd && GetTickCount64()>=g_preLockForceEnd){ g_preLockForceEnd=0; if(!g_lockShow) DoLock(); }
          bool forcing = g_preLockForceEnd && GetTickCount64()<g_preLockForceEnd;
          double toLock = lockable ? (g_idleLockAfter - (double)idleMs/1000.0) : 1e9;
          bool inPre = !g_lockShow && (forcing || (lockable && !lockedThisIdle && toLock>0 && toLock<=g_preLockSecs));
          if(inPre) g_preLockRemain = forcing ? (double)((long long)g_preLockForceEnd-(long long)GetTickCount64())/1000.0 : toLock;
          Approach(g_preLock, inPre?1.0f:0.0f, inPre?12.0f:14.0f);
        }
        bool dimShown = g_dimAnim>0.002f || g_preLock>0.002f || StvDimActive();   // the veil window also carries the countdown and the Strive banner/intro
        static bool dshown=false;
        if (dimShown!=dshown){ dshown=dimShown;
            if(dimShown) SetWindowPos(g_dimHwnd,HWND_TOPMOST,g_mx,g_my,g_mw,g_mh,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            else ShowWindow(g_dimHwnd,SW_HIDE); }

        // ---- settings app (modal, animated, frosted backdrop) ----
        float setT=g_setShow?1.0f:0.0f;
        g_setAnim += (setT-g_setAnim)*std::min(1.0f,dt*14.0f);
        if(fabsf(setT-g_setAnim)<0.002f) g_setAnim=setT;
        g_setReveal = MotionAnim(MP_SETTINGS,830006,setT);
        if(freeze>=0.0f){ g_setAnim=freeze; g_launAnim=freeze; g_sideReveal=freeze; g_setReveal=freeze; }
        bool setShown = g_setShow || g_setAnim>0.004f || (FrameBornOn() && g_setReveal>0.004f);
        float fmT=g_fmShow?1.0f:0.0f; g_fmAnim += (fmT-g_fmAnim)*std::min(1.0f,dt*14.0f);
        if(fabsf(fmT-g_fmAnim)<0.002f) g_fmAnim=fmT;
        bool fmShown = g_fmShow || g_fmAnim>0.004f;
        static bool fmwas=false;
        if(fmShown!=fmwas){ fmwas=fmShown;
            if(fmShown){ SetWindowPos(g_fmHwnd,HWND_TOPMOST,g_mx,g_my,g_mw,g_mh,SWP_NOACTIVATE);
                ShowWindow(g_fmHwnd,SW_SHOW); SetForegroundWindow(g_fmHwnd); SetActiveWindow(g_fmHwnd); }
            else ShowWindow(g_fmHwnd,SW_HIDE); }
        static bool stshown=false;
        if (setShown!=stshown){ stshown=setShown;
            if(!setShown) ShowWindow(g_setHwnd,SW_HIDE); }
        // Keyed on g_setShow, not on setShown: closing fades out over several frames, and reopening
        // inside that fade left setShown true the whole time - so this block was skipped and the
        // panel came back without focus and without a fresh backdrop grab.
        static bool stwas=false;
        if(g_setShow!=stwas){ stwas=g_setShow;
            if(g_setShow){ if(g_setBg)g_setBg->Release(); g_setBg=CaptureRegionBlur(g_mx,g_my,g_mw,g_mh);
                g_setOpenAt=GetTickCount64(); g_setArmed=false;   // ignore the click that opened it
                ShowWindow(g_setHwnd,SW_SHOW); SetForegroundWindow(g_setHwnd); SetActiveWindow(g_setHwnd); } }

        // ---- OSD (volume popup) show/hide, driven by the audio callback timer ----
        g_osdReveal = MotionAnim(MP_OSD,830003,(GetTickCount64() < g_osdShowUntil)? 1.0f : 0.0f);
        bool osdShown = GetTickCount64() < g_osdShowUntil || (FrameBornOn() && g_osdReveal>0.004f);
        static bool oshown=false;
        if (osdShown!=oshown){ oshown=osdShown; ShowWindow(g_osdHwnd, osdShown?SW_SHOWNOACTIVATE:SW_HIDE); }
        if (osdShown && g_suppressFlyout) SweepNativeFlyout();   // keep the native flyout off-screen while ours shows

        // ---- session/power screen (modal, animated; triggered by the bar power button) ----
        float sessT=g_sessShow?1.0f:0.0f;
        g_sessAnim = MotionAnim(MP_SESSION,830004,sessT);
        bool sessShown = g_sessShow || g_sessAnim>0.004f;
        static bool seshown=false;
        if (sessShown!=seshown){ seshown=sessShown;
            if(sessShown){ if(g_sessBg)g_sessBg->Release(); g_sessBg=CaptureRegionBlur(g_mx,g_my,g_mw,g_mh);
                SetWindowPos(g_sessHwnd,HWND_TOPMOST,g_mx,g_my,g_mw,g_mh,SWP_NOACTIVATE|SWP_SHOWWINDOW); }
            else ShowWindow(g_sessHwnd,SW_HIDE); }
        g_sessRect = g_sessShow ? RECT{0,0,(LONG)g_mw,(LONG)g_mh} : RECT{0,0,0,0};   // click-through while fading out

        // ---- lock screen (activatable; freezes a blurred capture behind it) ----
        // ---- arming the lock: clear the screen of our own overlays, THEN capture the backdrop ----
        if(g_lockArm>0){
            if(g_lockArm==3){   // first tick: snap the summoning overlay away instead of fading it
                g_launShow=false; g_launAnim=0.0f; ShowWindow(g_launHwnd,SW_HIDE); g_launRect=RECT{0,0,0,0};
                g_sessShow=false; g_sessAnim=0.0f; ShowWindow(g_sessHwnd,SW_HIDE); g_sessRect=RECT{0,0,0,0};
                g_setShow =false;                  ShowWindow(g_setHwnd, SW_HIDE); g_setRect =RECT{0,0,0,0};
                g_barFly=0; g_barMenu=false; g_appPrev=0; g_trayOpen=false;
            }
            if(--g_lockArm==0) g_lockShow=true;    // clean frame composed -> now the grab is the desktop
        }
        float lockT=g_lockShow?1.0f:0.0f;          // read AFTER arming, so the anim starts on the same frame
        g_lockAnim += (lockT-g_lockAnim)*std::min(1.0f,dt*6.5f);   // smooth, so the staggered widget anims read
        if(fabsf(lockT-g_lockAnim)<0.002f) g_lockAnim=lockT;
        bool lockShown = g_lockShow || g_lockAnim>0.004f;
        static bool lkshown=false;
        if(lockShown!=lkshown){ lkshown=lockShown;
            if(lockShown){ if(g_lockBg)g_lockBg->Release(); g_lockBg=CaptureRegionBlur(g_mx,g_my,g_mw,g_mh);
                SetWindowPos(g_lockHwnd,HWND_TOPMOST,g_mx,g_my,g_mw,g_mh,SWP_SHOWWINDOW);
                ShowWindow(g_lockHwnd,SW_SHOW); ActivateWindow(g_lockHwnd); }   // AttachThreadInput trick -> reliable keyboard focus
            else { ShowWindow(g_lockHwnd,SW_HIDE); } }
        if(!g_lockShow) g_lockRect=RECT{0,0,0,0};

        // ---- launcher (Alt+Space, activatable window for the search box) ----
        // Was one exponential lerp for both directions, so opening and closing took the same time
        // and neither could be tuned. Now each phase runs on its own duration in milliseconds.
        { float launT=g_launShow?1.0f:0.0f;
          int   ms   = g_launShow? g_launOpenMs : g_launCloseMs;
          if(g_launAnimStyle==LANIM_NONE || ms<=8) g_launAnim=launT;
          else {
              float step=dt*(1000.0f/(float)ms);
              g_launAnim += (launT>g_launAnim)? step : -step;
              g_launAnim = std::clamp(g_launAnim,0.0f,1.0f);
          } }
        g_launReveal = MotionAnim(g_lmode==2||g_lmode==3? MP_PICKER : MP_LAUNCHER,830005,g_launShow?1.0f:0.0f);
        bool launShown = g_launShow || g_launAnim>0.004f || (FrameBornOn() && g_launReveal>0.004f);
        ImagePreviewTick();
        { static ULONGLONG s_reap=0; if(GetTickCount64()-s_reap>5000){ s_reap=GetTickCount64(); CwReap(); } }
        static bool lshown=false;
        if(g_lb.active && !(g_launShow && g_lmode==2 && g_wallSource==WSRC_LIVE)) LiveBrowseEnd();
        if(launShown!=lshown){ lshown=launShown;
            if(!launShown){ ShowWindow(g_launHwnd,SW_HIDE); g_lmode=0; g_wallJustApplied=false; } }
        // The open work is keyed on g_launShow, NOT on launShown. Closing fades for closeMs - 600ms
        // as configured - and launShown stays true for that whole stretch, so re-opening inside it
        // skipped this block entirely: no SetForegroundWindow (so typing went nowhere), no re-arm,
        // stale search text, and the old g_lmode still set, meaning a tap right after closing the
        // wallpaper carousel reopened the carousel instead of the app list.
        static bool lwas=false;
        // Self-correcting: the flag saying "open" while the window is not on screen is an
        // impossible state, and it stranded the launcher until the shell was restarted. Re-arm the
        // edge so the next frame opens it properly instead of leaving it stuck.
        if(g_launShow && g_launHwnd && !IsWindowVisible(g_launHwnd)) lwas=false;
        if(g_launShow!=lwas){ lwas=g_launShow;
            if(g_launShow){ if(g_lmode!=2&&g_lmode!=3&&g_lmode!=4){ g_lsearch[0]=0; LRebuild(); } g_launFocus=true;
                if(g_launBlur||g_wallBackdrop){
                    for(auto& t:g_launBgL) if(t){ t->Release(); t=nullptr; }
                    static const int RAD[LAUNBG_N]={0,10,26,52};   // screen px, cumulative
                    CaptureBlurLevels(g_mx,g_my,g_mw,g_mh,g_launBgL,RAD,LAUNBG_N);
                    g_launBg=g_launBgL[LAUNBG_N-1];                // panels use the deepest one
                }
                g_launOpenAt=GetTickCount64(); g_launArmed=false;   // ignore the keystroke/click that opened it
                g_lmodeAtOpen=g_lmode;                             // Escape closes a view that was opened directly
                // the pointer has NOT moved yet - remember where it is so the first result keeps
                // the selection no matter what the launcher opens underneath
                g_launMouseLive=false;
                { POINT cp0; if(GetCursorPos(&cp0)) g_launMousePos=ImVec2((float)(cp0.x-g_mx)/g_uiScale,
                                                                          (float)(cp0.y-g_my)/g_uiScale); }
                { HWND fg=GetForegroundWindow(); DWORD fpid=0;
                  if(fg) GetWindowThreadProcessId(fg,&fpid);
                  if(fg && fpid!=GetCurrentProcessId()) g_launPrevFg=fg; }   // never one of our own overlays
                ShowWindow(g_launHwnd,SW_SHOW);
                // ActivateWindow, not a bare SetForegroundWindow: Windows ignores that unless the request
                // comes from the app already in front, so opening from IPC, a hotkey arriving while another
                // app is active, or the Super hook left the panel on screen with typing going elsewhere.
                ActivateWindow(g_launHwnd); } }
        if(!g_launShow) g_launRect=RECT{0,0,0,0};

        // ---- notifications (top-right): auto-pop ~6s on a NEW toast, or reach the top-right corner ----
        // The panel used to also open whenever the cursor entered the rightmost 400px of the screen
        // for the panel's FULL height — with anything sitting in the notification centre that meant
        // it flew out constantly while working on the right of the screen. Now:
        //   summon  = a thin strip at the very screen edge, only next to where the stack actually is
        //   keep    = hovering the cards themselves (once open), so it never closes under the cursor
        bool notifAny; { std::lock_guard<std::mutex> lk(g_notifMtx); notifAny=!g_notifs.empty(); }
        bool notifRecent = notifAny && !g_dnd && (GetTickCount64()-g_notifChanged) < 6000;
        float stackBot   = (g_notifStackBot>8.0f? g_notifStackBot : 300.0f);
        bool notifOpen   = g_notifReveal>0.35f;
        bool notifKeep   = notifAny && notifOpen &&                            // already open: stay while pointed at
                           lx>=LW-NOTIFWINW && ly>=0 && ly<stackBot+24.0f;
        bool notifCorner = notifAny && lx>=LW-5 && ly>=0 && ly<std::min(stackBot+24.0f,420.0f);
        bool notifBell   = notifAny && GetTickCount64()<g_notifForceUntil;     // bar bell was clicked
        float notifTarget = (notifAny && !fsApp && (notifRecent||notifKeep||notifCorner||notifBell||g_forceNotif)) ? 1.0f : 0.0f;
        if(g_sideStyle==1){
            // merged sidebar: the bell and the corner open the sidebar; a NEW notification still pops the panel as a toast
            if(GetTickCount64()<g_notifForceUntil || notifCorner) g_sideForceUntil=std::max(g_sideForceUntil,GetTickCount64()+450);
            notifTarget = (notifAny && !fsApp && (notifRecent||g_forceNotif||(notifKeep&&g_notifReveal>0.3f)) && g_sideReveal<0.05f)? 1.0f : 0.0f;
        }
        // time-based Material spatial curve: springy on the way in, quick accelerate on the way out.
        // (The old exp-lerp was framerate-dependent AND had EaseOutCubic applied on top of it, which
        //  double-eased into a mushy start — this is the "smoother slide-in".)
        g_notifReveal = MotionAnim(MP_NOTIF,818000,notifTarget);
        bool notifShown=(g_notifReveal>0.003f)||(notifTarget>0.5f);
        static bool nshown=false;
        if (notifShown!=nshown){ nshown=notifShown; ShowWindow(g_notifHwnd, notifShown?SW_SHOWNOACTIVATE:SW_HIDE); }

        // ---- window switcher (Alt+Tab) -----------------------------------------------------
        // The hook only leaves a request behind; everything real happens here on the main thread.
        { int req=g_swReq.exchange(0);
          if(req==1||req==2){
              if(!g_swShow){
                  // park the capture worker before rebuilding the list it is reading
                  g_swActive.store(false);
                  for(int i=0;i<60 && g_swBusy.load();i++) Sleep(2);
                  SwBuild();
                  if(g_swWins.size()<2){ SwFree(); }        // nothing to switch to
                  else { g_swShow=true; g_swActive.store(true);
                         // GNOME semantics: the first Alt+Tab lands on the PREVIOUS window, and
                         // Shift+Tab on the least-recent one. Index 0 is the current foreground.
                         g_swSel = (req==1)? 1 : (int)g_swWins.size()-1; }
              } else {
                  int n=(int)g_swWins.size();
                  if(n>0) g_swSel = (req==1)? (g_swSel+1)%n : (g_swSel+n-1)%n;
              }
          } else if(req==3){                                 // Alt released / Enter / clicked
              if(g_swShow){ HWND pick = (g_swSel>=0&&g_swSel<(int)g_swWins.size())? g_swWins[g_swSel].hwnd : nullptr;
                            g_swShow=false; if(pick && IsWindow(pick)) ActivateWindow(pick); }
          } else if(req==4){ g_swShow=false; }               // Escape
          // --swshot: pin it open so it can be photographed without holding a key down
          // g_swActive is what un-parks the capture worker, and the real open path above sets it.
          // This one never did, so --swshot has always photographed a switcher with NO thumbnails
          // and NO icons - both are produced lazily by that worker. Every switcher screenshot taken
          // through this flag was of empty cards.
          if(swshot && !g_swShow && g_swWins.empty()){ SwBuild();
              if(g_swWins.size()>1){ g_swShow=true; g_swSel=1; g_swActive.store(true); } }
          // safety net: if Alt somehow came up without the hook seeing it, don't strand the panel
          if(!swshot && g_swShow && !(GetAsyncKeyState(VK_MENU)&0x8000) && g_swAnim>0.9f) g_swReq=3;
          float swT=g_swShow?1.0f:0.0f;
          g_swAnim = Cael::anim(823000, swT, swT>0.5f?Cael::DUR_FAST_SPATIAL:Cael::DUR_FAST_EFFECTS,
                                             swT>0.5f?Cael::FAST_SPATIAL:Cael::EMPHASIZED_ACCEL);
          // eyelid sweep: its own longer curve, ticked here from startup so it is initialised at 0
          // and actually animates the first time the switcher opens
          // EMPHASIZED, not EMPHASIZED_DECEL: the "decel" curve puts ~70% of its travel in the first
          // 50ms, so the lids were effectively shut before you could see them move even at 100fps.
          // EMPHASIZED is the slow-in / fast-middle / slow-out Material curve — it reads as a blink.
          g_swLid  = Cael::anim(823010, swT, swT>0.5f? 460 : 260,
                                             swT>0.5f? Cael::EMPHASIZED : Cael::EMPHASIZED_ACCEL);
          // coverflow glide. Exponential smoothing, not Cael::anim: the target MOVES every time you
          // tap Tab, and a duration-based animator restarts its curve on each change, which reads as
          // a stutter rather than a carousel spinning past. Snapped when the switcher is not up so
          // the next open starts on the right card instead of gliding in from the last one.
          if(g_swShow){ float d2=(float)g_swSel-g_swScroll;
                        g_swScroll += d2*std::min(1.0f,dt*14.0f);
                        if(fabsf(d2)<0.002f) g_swScroll=(float)g_swSel; }
          else g_swScroll=(float)g_swSel;
          // tear down only once the worker is out of the list
          if(!g_swShow && g_swAnim<0.01f && !g_swWins.empty()){
              g_swActive.store(false);
              if(!g_swBusy.load()) SwFree();
          }
        }
        // The overview's layer goes up and down with it. Like every other overlay it is a hidden
        // window until it has something to show; without this it drew into a swapchain nobody could see.
        { static bool ovWas=false;
          if(g_ovShow!=ovWas){ ovWas=g_ovShow;
              if(g_ovShow){ SetWindowPos(g_ovHwnd,HWND_TOPMOST,g_mx,g_my,g_mw,g_mh,SWP_SHOWWINDOW|SWP_NOACTIVATE);
                  RECT orc{}; GetWindowRect(g_ovHwnd,&orc);
                  WsTrace("overview layer up: visible=%d rect=%ld,%ld %ldx%ld",(int)IsWindowVisible(g_ovHwnd),
                          orc.left,orc.top,orc.right-orc.left,orc.bottom-orc.top); }
              else ShowWindow(g_ovHwnd,SW_HIDE); } }
        bool swShown = g_swShow || g_swAnim>0.004f;
        { static bool swWas=false;
          if(swShown!=swWas){ swWas=swShown;
              if(swShown) SetWindowPos(g_swHwnd,HWND_TOPMOST,g_mx,g_my,g_mw,g_mh,SWP_SHOWWINDOW|SWP_NOACTIVATE);
              else ShowWindow(g_swHwnd,SW_HIDE); } }

        // ---- "Now Playing" flyout: Medal-style pop in on a new track, slide out after ~4.5s ----
        // The Now Playing toast and the notification panel both grow out of the top-right corner. A track change
        // while the panel was open drew the toast straight over it (and took the clicks meant for the cards), so
        // while notifications are showing the toast waits; it pops once the panel has closed.
        if(g_notifReveal>0.02f && GetTickCount64()<g_medUntil) g_medUntil=std::max(g_medUntil.load(),GetTickCount64()+1500);
        g_medOpening = (GetTickCount64() < g_medUntil) && !fsApp && g_nowPlaying && g_notifReveal<=0.02f;
        float medT = g_medOpening ? 1.0f : 0.0f;
        g_medAnim = MotionAnim(MP_TOAST,830007,medT);
        bool medShown=(g_medAnim>0.004f)||g_medOpening; g_medShowing=medShown;
        static bool mshown=false;
        if (medShown!=mshown){ mshown=medShown; ShowWindow(g_medHwnd, medShown?SW_SHOWNOACTIVATE:SW_HIDE); }

        // while a bar popout is open/animating, redraw the desktop each frame so its carved notch tracks
        // the popout's morph; a one-frame latch forces the final clean redraw once it fully closes.
        { static bool flyWas=false; bool flyNow=(g_barFly!=0)||g_flyCut||g_thumbCut||g_prevCut||g_appPrev;
          if(flyNow||flyWas) g_deskDirty=true; flyWas=flyNow; }
        // explorer/WE desktop host coming or going flips live<->static — redraw the desktop when it changes
        { static bool hostWas=true; static ULONGLONG hostChk=0; ULONGLONG nowt=GetTickCount64();
          if(nowt-hostChk>500){ hostChk=nowt; bool h=DesktopHostPresent();
              if(h!=hostWas){ hostWas=h; g_deskDirty=true; }
              // A non-shell Explorer (ours, or one the user started) paints Progman solid black, and it
              // always sits ABOVE Aether's desktop layer - Windows pins the registered shell window to the
              // bottom, so re-ordering does not stick (tried: SetWindowPos left Progman on top). Unless the
              // live mode wants to show that desktop through, hide it so the wallpaper is visible.
              // ...EXCEPT when a live wallpaper is rendering in there: then Explorer's desktop IS the
              // wallpaper, and hiding it is what left Wallpaper Engine showing only its still snapshot.
              // (Superseded: the desk layer now sits ABOVE Explorer's desktop - see PlaceDeskLayer - so
              // there is nothing to hide. A Progman an earlier build hid is shown again.)
              if(h){
                  HWND pm=FindWindowW(L"Progman",nullptr);
                  if(pm && !IsWindowVisible(pm) && AnyMonLive()) ShowWindowAsync(pm,SW_SHOWNA);
                  PlaceDeskLayer(g_deskHwnd); }
              if(UpdateLiveMonitors()) g_deskDirty=true;
              { static bool anyWas=false; bool anyNow=AnyMonLive() || h;
                if(anyNow && !anyWas){ HWND pm=FindWindowW(L"Progman",nullptr); if(pm && !IsWindowVisible(pm)) ShowWindowAsync(pm,SW_SHOWNA); }
                anyWas=anyNow; }
              if(!h) EnsureDesktopHost();          // host died (or never started): bring it back
              else if(g_taskbarHidden) HideExplorerTaskbars(); } }
        // the desk layer only repaints when marked dirty, so the wallpaper clock needs a nudge
        // whenever the displayed minute rolls over (never per frame — that would defeat the point)
        if(g_deskClock){ static int lastMin=-1; time_t tn=time(nullptr); struct tm lm; localtime_s(&lm,&tn);
            if(lm.tm_min!=lastMin){ lastMin=lm.tm_min; g_deskDirty=true; } }
        DeskMusicTick();      // desktop lyrics / visualiser keep the desk layer redrawing while music plays
        // desktop layer: static, so only redraw when marked dirty (must happen before the idle bail-out)
        if (g_deskDirty){ g_deskDirty=false;
            { PERF(DESK); ImGui::SetCurrentContext(g_ctxDesk); NewFrameL(); DrawDesk(); RenderL(g_deskRtv,g_deskSc); }
            PlaceDeskLayer(g_deskHwnd); }

        perf::Tick();
        // ---- window decorations: track & draw the focused window's titlebar (before the idle bail) ----
        { static bool decoWasOn=true; if(!g_decoOn && decoWasOn){ ShowWindow(g_decoHwnd,SW_HIDE); g_decoRect=RECT{0,0,0,0}; } decoWasOn=g_decoOn; }
        if(g_decoOn){
            static ULONGLONG decoLast=0; ULONGLONG dn=GetTickCount64();
            if(dn-decoLast>=15){ decoLast=dn;
                { PERF(DECO); ImGui::SetCurrentContext(g_ctxDeco); NewFrameL(); DrawDeco(); RenderL(g_decoRtv,g_decoSc); }
                bool ds=g_decoRect.right>g_decoRect.left;
                static bool dwas=false; if(ds!=dwas){ dwas=ds; ShowWindow(g_decoHwnd, ds?SW_SHOWNOACTIVATE:SW_HIDE); }
                ApplyHitRegion(g_decoHwnd, g_decoRect, 0);
            }
        }
        // idle only when ALL panels are hidden; hidden windows never block clicks
        if (!drawerShown && !dockShown && !anyBarShown && !sideShown && !osdShown && !sessShown && !lockShown && !launShown && !notifShown && !medShown && !setShown && !dimShown && !g_snipActive && !fmShown && !swShown){ MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE); continue; }

        if (drawerShown){ PERF(DRAWER); ImGui::SetCurrentContext(g_ctxDrawer); NewFrameL(); DrawUI(); RenderL(g_rtv,g_sc); }
        if (anyBarShown){ PERF(BAR); STALL(bar_layer); ImGui::SetCurrentContext(g_ctxBar);    { STALL(bar_newframe); NewFrameL(); } DrawBar(); { STALL(bar_render); RenderL(g_barRtv,g_barSc); } g_uiDumpBarFrames++; }
        if (dockShown)  { PERF(DOCK);   ImGui::SetCurrentContext(g_ctxDock);   NewFrameL(); DrawDock(); RenderL(g_dockRtv,g_dockSc); }
        if (sideShown)  { PERF(SIDE);   ImGui::SetCurrentContext(g_ctxSide);   NewFrameL(); DrawSidebar(); RenderL(g_sideRtv,g_sideSc); }
        if (osdShown)   { ImGui::SetCurrentContext(g_ctxOsd);    NewFrameL(); DrawOSD(); RenderL(g_osdRtv,g_osdSc); }
        if (sessShown)  { ImGui::SetCurrentContext(g_ctxSess);   NewFrameL(); DrawSession(); RenderL(g_sessRtv,g_sessSc); }
        if (lockShown)  { ImGui::SetCurrentContext(g_ctxLock);   NewFrameL(); DrawLock(); RenderL(g_lockRtv,g_lockSc); }
        if (launShown){ ImGui::SetCurrentContext(g_ctxLaunch); NewFrameL();
            { ImDrawList* bdl=ImGui::GetBackgroundDrawList(); float la=std::clamp(g_launAnim,0.0f,1.0f);
              ImGuiIO& lio=ImGui::GetIO();
              // Caelestia dims nothing: the panels come out of the border over the live desktop
              bool bornMode = g_frameBorn && g_bubble && (g_lmode==3 || (g_lmode==2 && (g_wallSource==WSRC_LIVE || g_wallPos!=LPOS_CENTRE)) ||
                                                           ((g_lmode==0||g_lmode==1||g_lmode==4) && g_launPos!=LPOS_CENTRE));
              if(!bornMode) bdl->AddRectFilled(V(0,0),V(lio.DisplaySize.x,lio.DisplaySize.y),IM_COL32(8,7,11,(int)(150*la))); }
            DrawLauncher(); RenderL(g_launRtv,g_launSc); }
        if (setShown)   { ImGui::SetCurrentContext(g_ctxSet);    NewFrameL(); DrawSettings(); RenderL(g_setRtv,g_setSc); }
        else if (g_ghostSet>0){                        // snapshot: Settings drawn into its hidden window, never shown
            float sa=g_setAnim, sr=g_setReveal; g_setAnim=1.0f; g_setReveal=1.0f;
            ImGui::SetCurrentContext(g_ctxSet); NewFrameL(); DrawSettings();
            if(--g_ghostSet==0) g_snapSc=g_setSc;
            RenderL(g_setRtv,g_setSc); g_setAnim=sa; g_setReveal=sr; }
        if (g_snipActive){ ImGui::SetCurrentContext(g_ctxSnip);  NewFrameL(); DrawSnip(); RenderL(g_snipRtv,g_snipSc); }
        if (fmShown)    { ImGui::SetCurrentContext(g_ctxFm);     NewFrameL(); DrawFileManager(); RenderL(g_fmRtv,g_fmSc); }
        if (notifShown) { ImGui::SetCurrentContext(g_ctxNotif);  NewFrameL(); DrawNotifications(); RenderL(g_notifRtv,g_notifSc); }
        if (medShown)   { ImGui::SetCurrentContext(g_ctxMed);    NewFrameL(); DrawMediaFlyout(); RenderL(g_medRtv,g_medSc); }
        if (swShown)    { ImGui::SetCurrentContext(g_ctxSw);     NewFrameL(); DrawSwitcher(); RenderL(g_swRtv,g_swSc); }
        // The overview's frames come from the compositor, and CopyResource is not free-threaded, so
        // this is the one place they can be pulled. It is cheap while the overview is closed.
        // the keyboard hook cannot open the overview itself - everything it touches is this thread's
        if(g_ovReq.load()==5){ g_ovReq.store(0); if(g_ovWant) OvClose(); else OvOpen(); }
        OvCaptureTick();
        if (g_ovShow)   { ImGui::SetCurrentContext(g_ctxOv);     NewFrameL(); DrawOverview(); RenderL(g_ovRtv,g_ovSc); }
        if (dimShown)   { ImGui::SetCurrentContext(g_ctxDim);    NewFrameL(); DrawDim(); RenderL(g_dimRtv,g_dimSc); }
        // Pace the whole active frame with ONE compositor wait (not one vsync per layer). This gives a
        // steady ~refresh-rate cap with responsive input, instead of 60/N fps when several layers show.
        { STALL(DwmFlush); pace::Wait(); }
    }

    g_running=false;
    SaveConfigFlush(true);                                // a drag still in flight when the shell was told to quit
    HostDetachForeign();                                  // before our windows go - see HostDetachForeign
    // Reaching the cleanup path at all means this run shut down properly, so it can never count
    // toward the crash-loop. (Age alone is the wrong test: opening and closing Aether three times
    // in a row is perfectly normal and used to trip safe mode.)
    if (soleShell) SetStartupStrikes(0);
    if (g_keepAwake){ g_keepAwake=false; ApplyKeepAwake(); }
    if (g_flyoutHook) UnhookWinEvent(g_flyoutHook);
    Ws2TileHooks(false);
    if (g_trayHwnd){ DestroyWindow(g_trayHwnd); g_trayHwnd=nullptr;
        if(g_msgTaskbarCreated) SendNotifyMessageW(HWND_BROADCAST,g_msgTaskbarCreated,0,0); }   // let Explorer reclaim tray icons
    PluginsShutdown();                              // close every plugin's lua_State
    g_loopPaused=true;                              // shutting down: long waits here are not hangs
    if(g_ffProc) V2RecStop();                       // finish an ffmpeg recording (it writes the file index on "q") instead of orphaning it
    CwShutdown();                                   // stop Python widgets
    TermStop(g_term);                               // the dashboard terminal's shell
    FreeImageCache();
    FreeSvgCache();
    ClearAllWindowRegions();                        // un-clip every window we shaped
    if (g_taskbarHidden) SetWindowsTaskbar(false);   // give the Windows taskbar back
    if (g_epv && g_volCb) g_epv->UnregisterControlChangeNotify(g_volCb);
    if (g_epv) g_epv->Release();
    if (g_pdhQ) PdhCloseQuery(g_pdhQ);
    ClearWipes(); if(g_deskBase)g_deskBase->Release();
    if(g_deskWall)g_deskWall->Release(); if(g_deskWallOld)g_deskWallOld->Release();
    if(g_setBg)g_setBg->Release(); if(g_deskFrost)g_deskFrost->Release();
    for(auto& b:g_bars) if(b.acrylic) b.acrylic->Release();   // one frost capture per monitor
    if(g_setVis)g_setVis->Release(); if(g_setTgt)g_setTgt->Release(); if(g_setRtv)g_setRtv->Release(); if(g_setSc)g_setSc->Release();
    if(g_dimVis)g_dimVis->Release(); if(g_dimTgt)g_dimTgt->Release(); if(g_dimRtv)g_dimRtv->Release(); if(g_dimSc)g_dimSc->Release();
    if(g_deskVis)g_deskVis->Release(); if(g_deskTgt)g_deskTgt->Release(); if(g_deskRtv)g_deskRtv->Release(); if(g_deskSc)g_deskSc->Release();
    if(g_medVis)g_medVis->Release(); if(g_medTgt)g_medTgt->Release(); if(g_medRtv)g_medRtv->Release(); if(g_medSc)g_medSc->Release();
    if(g_notifVis)g_notifVis->Release(); if(g_notifTgt)g_notifTgt->Release(); if(g_notifRtv)g_notifRtv->Release(); if(g_notifSc)g_notifSc->Release();
    if(g_snipTex)g_snipTex->Release();
    if(g_snipVis)g_snipVis->Release(); if(g_snipTgt)g_snipTgt->Release(); if(g_snipRtv)g_snipRtv->Release(); if(g_snipSc)g_snipSc->Release();
    if(g_fmVis)g_fmVis->Release(); if(g_fmTgt)g_fmTgt->Release(); if(g_fmRtv)g_fmRtv->Release(); if(g_fmSc)g_fmSc->Release();
    if(g_decoVis)g_decoVis->Release(); if(g_decoTgt)g_decoTgt->Release(); if(g_decoRtv)g_decoRtv->Release(); if(g_decoSc)g_decoSc->Release();
    if(g_launVis)g_launVis->Release(); if(g_launTgt)g_launTgt->Release(); if(g_launRtv)g_launRtv->Release(); if(g_launSc)g_launSc->Release();
    if(g_sessVis)g_sessVis->Release(); if(g_sessTgt)g_sessTgt->Release(); if(g_sessRtv)g_sessRtv->Release(); if(g_sessSc)g_sessSc->Release();
    if(g_osdVis)g_osdVis->Release(); if(g_osdTgt)g_osdTgt->Release(); if(g_osdRtv)g_osdRtv->Release(); if(g_osdSc)g_osdSc->Release();
    if(g_sideVis)g_sideVis->Release(); if(g_sideTgt)g_sideTgt->Release(); if(g_sideRtv)g_sideRtv->Release(); if(g_sideSc)g_sideSc->Release();
    if(g_barVis)g_barVis->Release(); if(g_barTgt)g_barTgt->Release(); if(g_barRtv)g_barRtv->Release(); if(g_barSc)g_barSc->Release();
    if(g_dockVis)g_dockVis->Release(); if(g_dockTgt)g_dockTgt->Release(); if(g_dockRtv)g_dockRtv->Release(); if(g_dockSc)g_dockSc->Release();
    if(g_dcVisual)g_dcVisual->Release(); if(g_dcTarget)g_dcTarget->Release(); if(g_dcDev)g_dcDev->Release();
    if(g_rtv)g_rtv->Release(); if(g_sc)g_sc->Release(); if(g_fac)g_fac->Release(); if(g_ctx)g_ctx->Release(); if(g_dev)g_dev->Release();
    HideThumb(); ReleaseShellRoles(g_barHwnd); ClearMinimizedMetrics();
    if(g_restoreHook){ UnhookWinEvent(g_restoreHook); g_restoreHook=nullptr; }
    // hand komorebi its screen back and drop the event subscription
    if(g_komoLive.load() && g_komoReserve)
        for(int i=0;i<(int)g_mons.size()&&i<16;i++)
            if(g_wsRing[i].komoMon>=0){ wchar_t a[96];
                _snwprintf_s(a,96,L"monitor-work-area-offset %d 0 0 0 0",g_wsRing[i].komoMon);
                KomoRun(a,nullptr); }
    KomorebiStop();
    // WinLogon relaunches the shell when the shell process exits with code 0. A deliberate quit
    // must therefore exit non-zero, or we would immediately be restarted (Cairo does the same).
    // A deliberate RESTART wants exactly the opposite, and gets it by exiting 0.
    return (g_isShell && !g_restartOnExit) ? 1 : 0;
}
