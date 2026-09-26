// Aether - the Settings app (shell).
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ================================================================= settings app
// Layout taken from the reference video (t=30.6s): left icon rail, middle section list with
// collapsible groups + colour-scheme swatch rows, right wallpaper thumbnail grid.
// ---- presets: a named snapshot of config.json ------------------------------------------
// Layer 1 of the customization system. SaveConfig/LoadConfig already serialise every knob, so a
// preset is simply a copy of config.json parked under presets\<name>.json. "Default" is written
// once from the shipped configuration and is never overwritten.
static std::string PresetDir(){ return ExeDir()+"presets\\"; }
static std::vector<std::string> g_presets; static bool g_presetsRead=false; static int g_presetSel=-1;
static void RefreshPresets(){
    g_presets.clear(); g_presetsRead=true;
    CreateDirectoryA(PresetDir().c_str(),nullptr);
    WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA((PresetDir()+"*.json").c_str(),&fd);
    if(h!=INVALID_HANDLE_VALUE){
        do{ std::string n=fd.cFileName;
            if(n.size()>5) n=n.substr(0,n.size()-5);       // strip .json
            g_presets.push_back(n);
        } while(FindNextFileA(h,&fd));
        FindClose(h);
    }
}
static void SavePresetAs(const std::string& name){
    if(name.empty()) return;
    SaveConfig();                                          // flush live state first
    CreateDirectoryA(PresetDir().c_str(),nullptr);
    CopyFileA((ExeDir()+"config.json").c_str(),(PresetDir()+name+".json").c_str(),FALSE);
    RefreshPresets();
}
static void ApplyPreset(const std::string& name){
    std::string src=PresetDir()+name+".json";
    if(GetFileAttributesA(src.c_str())==INVALID_FILE_ATTRIBUTES) return;
    CopyFileA(src.c_str(),(ExeDir()+"config.json").c_str(),FALSE);
    LoadConfig();                                          // re-read every knob
    ApplyThemeMode(); g_deskDirty=true;
}
static void DeletePreset(const std::string& name){
    if(name=="Default") return;                            // the shipped baseline stays
    DeleteFileA((PresetDir()+name+".json").c_str());
    RefreshPresets();
}
static void EnsureDefaultPreset(){
    std::string d=PresetDir()+"Default.json";
    if(GetFileAttributesA(d.c_str())==INVALID_FILE_ATTRIBUTES){
        CreateDirectoryA(PresetDir().c_str(),nullptr);
        CopyFileA((ExeDir()+"config.json").c_str(),d.c_str(),FALSE);
    }
}

// Page set mirrors CachyOS' System Settings sidebar (Plasma's categories), mapped onto the
// Windows equivalents. Grouped exactly like Plasma: Workspace / Hardware / System.
enum SetPage {
    SP_APPEARANCE=0, SP_WALLPAPER, SP_EFFECTS, SP_TASKBAR, SP_DASHBOARD, SP_LAUNCHER,
    SP_NOTIF, SP_WORKSPACES, SP_WINDOWS, SP_SHORTCUTS,
    SP_DISPLAY, SP_AUDIO, SP_INPUT, SP_NETWORK, SP_BLUETOOTH, SP_POWER, SP_STORAGE, SP_PRINTERS,
    SP_STARTUP, SP_REGION, SP_ACCESS, SP_USERS, SP_UPDATES, SP_FIREWALL, SP_ABOUT, SP_PRESETS,
    SP_PLUGINS, SP_MONITORS, SP_TUNEUP, SP_MACROS, SP_COUNT
};

static void MacroSendKey(int vk,bool down){
    INPUT in{}; in.type=INPUT_KEYBOARD;
    UINT sc=MapVirtualKeyW((UINT)vk,MAPVK_VK_TO_VSC);
    in.ki.wScan=(WORD)sc;
    in.ki.dwFlags=KEYEVENTF_SCANCODE | (down?0:KEYEVENTF_KEYUP);
    // extended keys carry a prefix byte; without this the arrows/nav cluster land as numpad keys
    switch(vk){ case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_HOME: case VK_END:
                case VK_PRIOR: case VK_NEXT: case VK_INSERT: case VK_DELETE: case VK_RCONTROL:
                case VK_RMENU: in.ki.dwFlags|=KEYEVENTF_EXTENDEDKEY; break; default: break; }
    if(SendInput(1,&in,sizeof(INPUT))==1) g_macroSent++;
}
static void MacroSendMouse(int button,bool down){
    INPUT in{}; in.type=INPUT_MOUSE;
    switch(button){
        case 1:  in.mi.dwFlags = down?MOUSEEVENTF_RIGHTDOWN :MOUSEEVENTF_RIGHTUP;  break;
        case 2:  in.mi.dwFlags = down?MOUSEEVENTF_MIDDLEDOWN:MOUSEEVENTF_MIDDLEUP; break;
        default: in.mi.dwFlags = down?MOUSEEVENTF_LEFTDOWN  :MOUSEEVENTF_LEFTUP;   break;
    }
    if(SendInput(1,&in,sizeof(INPUT))==1) g_macroSent++;
}
static void MacroSendWheel(int delta){
    INPUT in{}; in.type=INPUT_MOUSE; in.mi.dwFlags=MOUSEEVENTF_WHEEL; in.mi.mouseData=(DWORD)delta;
    SendInput(1,&in,sizeof(INPUT));
}
// One pass over a macro's steps. Runs on its own thread so a Sleep step never stalls the shell.
static void MacroRunPass(const Macro& m,int idx){
    for(const MacroStep& st:m.steps){
        if(g_macroStop.load() || !g_macroArmed) return;
        g_macroSteps++;
        switch(st.kind){
        case MS_KEY_DOWN:    MacroSendKey(st.code,true); break;
        case MS_KEY_UP:      MacroSendKey(st.code,false); break;
        case MS_KEY_TAP:     MacroSendKey(st.code,true); Sleep(st.arg>0?st.arg:12); MacroSendKey(st.code,false); break;
        case MS_MOUSE_DOWN:  MacroSendMouse(st.code,true); break;
        case MS_MOUSE_UP:    MacroSendMouse(st.code,false); break;
        case MS_MOUSE_CLICK: MacroSendMouse(st.code,true); Sleep(st.arg>0?st.arg:12); MacroSendMouse(st.code,false); break;
        case MS_WHEEL:       MacroSendWheel(st.arg); break;
        case MS_SLEEP:       { int left=st.arg; while(left>0 && !g_macroStop.load()){ int c=std::min(left,15); Sleep(c); left-=c; } } break;
        case MS_WAIT_RELEASE:
            // block while the trigger is still held - the step that makes a "drag" macro follow the
            // user rather than run for a fixed time
            while(!g_macroStop.load() && g_macroArmed && (GetAsyncKeyState(m.trigger)&0x8000)) Sleep(8);
            break;
        }
    }
    (void)idx;
}
static void MacroFire(int idx){
    if(idx<0 || idx>=(int)g_macros.size()) return;
    if(g_macroRunning.load()>=0) return;              // one macro at a time, like the original
    Macro m=g_macros[idx];                            // copy: the settings list can be edited while it runs
    g_macroRunning.store(idx); g_macroStop.store(false);
    std::thread([m,idx]{
        do {
            MacroRunPass(m,idx);
            if(m.mode==MM_PRESS) break;
            if(g_macroStop.load() || !g_macroArmed) break;
            if(m.mode==MM_HOLD && !(GetAsyncKeyState(m.trigger)&0x8000)) break;
            if(m.mode==MM_TOGGLE && !g_macroToggled[idx]) break;
            int left=m.interval; while(left>0 && !g_macroStop.load()){ int c=std::min(left,15); Sleep(c); left-=c; }
        } while(g_macroArmed && !g_macroStop.load());
        g_macroRunning.store(-1);
    }).detach();
}
static void MacroStopAll(){ g_macroStop.store(true);
    for(auto& t:g_macroToggled) t=0;
    for(int i=0;i<40 && g_macroRunning.load()>=0;i++) Sleep(5); }

// Called from the shell's existing low-level keyboard hook. Returns true to SWALLOW the key.
static bool MacroOnKey(DWORD vk,bool down){
    if(vk==VK_F8){ if(down){ g_macroArmed=!g_macroArmed;
                             if(!g_macroArmed) MacroStopAll();
                             g_macroLog = g_macroArmed? "Armed (F8)":"Disarmed (F8)"; }
                   return false; }        // never swallow F8 itself
    if(!g_macroArmed) return false;
    if(g_macroHeld.size()!=g_macros.size()){ g_macroHeld.assign(g_macros.size(),0); g_macroToggled.assign(g_macros.size(),0); }
    bool swallow=false;
    for(size_t i=0;i<g_macros.size();i++){
        Macro& m=g_macros[i];
        if(!m.enabled || m.trigger==0 || (DWORD)m.trigger!=vk) continue;
        if(down) g_macroTrigSeen++;
        if(m.suppress) swallow=true;
        if(down){
            if(g_macroHeld[i]) continue;              // auto-repeat, not a fresh press
            g_macroHeld[i]=1;
            if(m.mode==MM_TOGGLE){
                g_macroToggled[i]=!g_macroToggled[i];
                if(g_macroToggled[i]){ g_macroLog="Started "+m.name; MacroFire((int)i); }
                else { g_macroLog="Stopped "+m.name; g_macroStop.store(true); }
            } else { g_macroLog="Ran "+m.name; MacroFire((int)i); }
        } else {
            g_macroHeld[i]=0;
            if(m.mode==MM_HOLD) g_macroStop.store(true);
        }
    }
    return swallow;
}
// The preset library, same set the Python build shipped. Binds are Fortnite defaults and are meant
// to be re-bound per user keybinds; everything arrives DISABLED.
static void MacroLoadPresets(){
    g_macros.clear();
    auto K=[](int kind,int code,int arg){ MacroStep s; s.kind=kind; s.code=code; s.arg=arg; return s; };
    { Macro m; m.name="Drag Edit"; m.desc="Wheel-down reset, hold the edit key, release with the trigger";
      m.trigger='Q'; m.mode=MM_PRESS; m.suppress=true; m.interval=20;
      m.steps={ K(MS_WHEEL,0,-120), K(MS_SLEEP,0,20), K(MS_KEY_DOWN,'G',0),
                K(MS_WAIT_RELEASE,0,0), K(MS_KEY_UP,'G',0), K(MS_SLEEP,0,20), K(MS_KEY_TAP,'Q',0) };
      g_macros.push_back(m); }
    { Macro m; m.name="Reset Edit"; m.desc="Wheel-down reset then confirm";
      m.trigger='R'; m.mode=MM_PRESS; m.interval=20;
      m.steps={ K(MS_WHEEL,0,-120), K(MS_SLEEP,0,20), K(MS_KEY_TAP,'Q',0) };
      g_macros.push_back(m); }
    { Macro m; m.name="Piece Control"; m.desc="Place, edit, confirm";
      m.trigger='C'; m.mode=MM_PRESS; m.interval=25;
      m.steps={ K(MS_MOUSE_CLICK,0,15), K(MS_SLEEP,0,25), K(MS_KEY_TAP,'G',0), K(MS_SLEEP,0,25), K(MS_KEY_TAP,'Q',0) };
      g_macros.push_back(m); }
    { Macro m; m.name="Jump Reset"; m.desc="Jump then rebuild under yourself";
      m.trigger=VK_SPACE; m.mode=MM_PRESS; m.interval=30;
      m.steps={ K(MS_KEY_TAP,VK_SPACE,0), K(MS_SLEEP,0,60), K(MS_KEY_TAP,'Z',0), K(MS_MOUSE_CLICK,0,15) };
      g_macros.push_back(m); }
    { Macro m; m.name="Auto Clicker"; m.desc="Left-clicks on a timer until you press it again";
      m.trigger=VK_F6; m.mode=MM_TOGGLE; m.interval=60;
      m.steps={ K(MS_MOUSE_CLICK,0,12) };
      g_macros.push_back(m); }
    { Macro m; m.name="Rapid Fire"; m.desc="Clicks while the trigger is held";
      m.trigger=VK_XBUTTON1; m.mode=MM_HOLD; m.interval=70;
      m.steps={ K(MS_MOUSE_CLICK,0,12) };
      g_macros.push_back(m); }
    { Macro m; m.name="Key Spammer"; m.desc="Repeats one key until you press it again";
      m.trigger=VK_F7; m.mode=MM_TOGGLE; m.interval=80;
      m.steps={ K(MS_KEY_TAP,'E',0) };
      g_macros.push_back(m); }
    g_macroHeld.assign(g_macros.size(),0); g_macroToggled.assign(g_macros.size(),0);
}
// pretty name for a virtual key, for the settings rows
static std::string MacroKeyName(int vk){
    if(vk==0) return "(none)";
    if(vk==VK_SPACE) return "Space";
    if(vk==VK_XBUTTON1) return "Mouse 4";
    if(vk==VK_XBUTTON2) return "Mouse 5";
    if(vk>=VK_F1&&vk<=VK_F24){ char b[8]; snprintf(b,8,"F%d",vk-VK_F1+1); return b; }
    UINT sc=MapVirtualKeyW((UINT)vk,MAPVK_VK_TO_VSC);
    wchar_t nm[64]={0};
    if(sc && GetKeyNameTextW((LONG)(sc<<16),nm,63)>0) return W2U8(nm);
    if(vk>=0x20&&vk<0x7F){ char b[2]={(char)vk,0}; return b; }
    char b[16]; snprintf(b,16,"VK %d",vk); return b;
}
static const char* SET_PAGES[SP_COUNT]={
    "Appearance","Wallpaper","Effects","Taskbar","Dashboard","Launcher",
    "Notifications","Workspaces","Window Behavior","Shortcuts",
    "Display","Audio","Input Devices","Network","Bluetooth","Power","Storage","Printers",
    "Autostart","Region","Accessibility","Users","Updates","Firewall","Aether","Presets",
    "Plugins","Monitors","Tune-up","Macros"
};
static const int NSETPAGES=SP_COUNT;
static const char* SET_SUBS[SP_COUNT]={
    "Theme, colours, fonts","Wallpaper, desktop lyrics, visualiser","Motion, blur, animations","Style, position, components",
    "Tabs, layouts, profile","Style, search, commands","Sidebar, toasts, cards","komorebi, slide, tiling",
    "Corners, snapping, focus","Keybinds","Resolution, scale, night light","App volumes, sound devices",
    "Mouse, keyboard, touchpad","Wi-Fi, ethernet","Pairing, devices","Sleep, idle, lock screen","Drives, cleanup",
    "Print queues","Startup apps","Language, time, formats","Contrast, text size","Accounts","System updates",
    "Rules, profiles","Tour, about this system","Save and load looks","Manage plugins","Per-display options",
    "Cleanup and health","Automations" };
static void OpenSettingsPage(const std::string& name){
    for(int i=0;i<SP_COUNT;i++) if(_stricmp(SET_PAGES[i],name.c_str())==0){ g_setPage=i; break; }
    if(_stricmp(name.c_str(),"about")==0 || _stricmp(name.c_str(),"home")==0) g_setPage=SP_ABOUT;
    g_setShow=true;
}
// group headers: a label appears above the page whose index is the key
struct SetGroup{ int at; const char* label; };
static const SetGroup SET_GROUPS[3]={ {SP_APPEARANCE,"Workspace"}, {SP_DISPLAY,"Hardware"}, {SP_STARTUP,"System"} };
static float g_railScroll=0, g_railScrollT=0, g_railContentH=0;   // the sidebar scrolls now that it holds 25 pages
static int g_wallFolderSel=-1;   // selection in the folder list
static float g_setScroll=0, g_setScrollT=0, g_setContentH=0;   // middle-column scrolling
static int g_hkCapture=-1, g_hkLast=-1; static ULONGLONG g_hkCaptureStart=0;   // shortcut rebinding
// Appearance sections: Theme mode / Color variant / Color scheme / Wallpaper transition
static bool g_setSec[4]={false,false,true,false};

// --- shell replacement (Cairo-style). Per-user key, so it is easy to revert. ---
static const wchar_t* WINLOGON=L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
static bool IsShellReplaced(){
    wchar_t buf[MAX_PATH]={0}; DWORD sz=sizeof(buf);
    if(RegGetValueW(HKEY_CURRENT_USER,WINLOGON,L"Shell",RRF_RT_REG_SZ,nullptr,buf,&sz)!=ERROR_SUCCESS) return false;
    wchar_t me[MAX_PATH]; GetModuleFileNameW(nullptr,me,MAX_PATH);
    return _wcsicmp(buf,me)==0;
}
static void SetShellReplaced(bool on){
    HKEY k; if(RegCreateKeyExW(HKEY_CURRENT_USER,WINLOGON,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS) return;
    if(on){ wchar_t me[MAX_PATH]; GetModuleFileNameW(nullptr,me,MAX_PATH);
        RegSetValueExW(k,L"Shell",0,REG_SZ,(BYTE*)me,(DWORD)((wcslen(me)+1)*sizeof(wchar_t))); }
    else RegDeleteValueW(k,L"Shell");
    RegCloseKey(k);
}
