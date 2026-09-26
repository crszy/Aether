// src/services/Wifi.h  —  Aether shell
// Wi-Fi that actually works from the shell: scan, connect (saved, open and password networks), disconnect, forget.
//
// What was broken:
//   * "Rescan" never scanned. It read WlanGetAvailableNetworkList, which is only what Windows last cached - without
//     WlanScan the list is stale or empty until Explorer (not running) happens to ask for a scan.
//   * Connect only worked for a network that already had a profile named exactly like its SSID. A network you had
//     never joined has no profile, so WlanConnect failed silently, and there was no way to type a password.
//   * There was no disconnect at all, and every call ran on the render thread.
//
// Now everything runs on a worker thread and reports back on the UI thread (RunOnUi):
//   scan       WlanScan, then wait for wlan_notification_acm_scan_complete (or 4 s) and read the list
//   connect    a saved profile connects directly; an open network gets a profile made for it; a secured one gets a
//              WPA2 / WPA3 / WPA personal profile built from the password (WlanSetProfile). Then wait for
//              connection_complete / connection_attempt_fail and say which. A profile Aether just made for a
//              failed attempt is deleted again, so a wrong password is not remembered.
//   disconnect WlanDisconnect      forget   WlanDeleteProfile
// Typing a password needs a window that takes the keyboard, which the bar and sidebar deliberately do not - so a
// secured network asks for its password in Settings > Network.
#pragma once

static std::atomic<bool> g_wifiScanning{false}, g_wifiBusy{false};
static std::string g_wifiStatus; static ULONGLONG g_wifiStatusAt=0; static int g_wifiStatusKind=0;   // 0 info 1 good 2 bad  (UI thread)
static std::string g_wifiPwFor;          // Settings > Network asks for this network's password
static std::string g_wifiConnecting;     // UI thread: the network a connect is in progress for
static ULONGLONG   g_wifiLastScan=0;
// Windows 11 only shows Wi-Fi networks (even the one you are on) to programs when location is allowed. A privacy /
// debloat tool that sets the LocationAndSensors policy makes every WLAN query fail with ERROR_ACCESS_DENIED - which is
// what "Wi-Fi doesn't scan" was on the machine this was written on (netsh wlan fails the same way).
static std::atomic<int> g_wifiLocBlocked{0};          // 1: Windows is refusing Wi-Fi information (location is off)
static bool WifiLocationPolicyOff(){
    HKEY k; bool off=false;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Policies\\Microsoft\\Windows\\LocationAndSensors",0,KEY_READ,&k)==ERROR_SUCCESS){
        for(const wchar_t* n: {L"DisableLocation",L"DisableWindowsLocationProvider"}){ DWORD v=0, sz=sizeof(v), t=0;
            if(RegQueryValueExW(k,n,nullptr,&t,(BYTE*)&v,&sz)==ERROR_SUCCESS && t==REG_DWORD && v) off=true; }
        RegCloseKey(k); }
    return off;
}
static void WifiScanAsync();
static void WifiSetStatus(const std::string& s,int kind);   // fwd
// turn location back on for this PC (UAC): remove the policy values, allow location, start the service, rescan
static void WifiAllowLocation(){
    const wchar_t* args =
        L"/c reg delete \"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\LocationAndSensors\" /v DisableLocation /f"
        L" & reg delete \"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\LocationAndSensors\" /v DisableWindowsLocationProvider /f"
        L" & reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location\" /v Value /t REG_SZ /d Allow /f"
        L" & reg add \"HKCU\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location\" /v Value /t REG_SZ /d Allow /f"
        L" & reg add \"HKCU\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location\\NonPackaged\" /v Value /t REG_SZ /d Allow /f"
        L" & sc config lfsvc start= demand & net start lfsvc";
    AetherShellExec(nullptr,L"runas",L"cmd.exe",args,nullptr,SW_HIDE);
    WifiSetStatus("Turning location on\xE2\x80\xA6 approve the Windows prompt, then Aether rescans.",0);
    std::thread([]{ for(int i=0;i<10;i++){ Sleep(2000); WifiScanAsync(); Sleep(3000); if(!g_wifiLocBlocked.load()) break; } }).detach();
}

static void WifiSetStatus(const std::string& s,int kind){ RunOnUi([s,kind]{ g_wifiStatus=s; g_wifiStatusKind=kind; g_wifiStatusAt=GetTickCount64(); }); }

struct WlanSession {
    HANDLE h=nullptr; GUID iface{}; bool ok=false;
    HANDLE evScan=nullptr, evConn=nullptr; std::atomic<DWORD> connReason{0}; std::atomic<int> connResult{0};   // 1 ok, 2 failed
    WlanSession(){
        DWORD ver=0; if(WlanOpenHandle(2,nullptr,&ver,&h)!=ERROR_SUCCESS){ h=nullptr; return; }
        WLAN_INTERFACE_INFO_LIST* il=nullptr;
        if(WlanEnumInterfaces(h,nullptr,&il)==ERROR_SUCCESS && il && il->dwNumberOfItems>0){ iface=il->InterfaceInfo[0].InterfaceGuid; ok=true; }
        if(il) WlanFreeMemory(il);
        evScan=CreateEventW(nullptr,TRUE,FALSE,nullptr); evConn=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(ok) WlanRegisterNotification(h,WLAN_NOTIFICATION_SOURCE_ACM,FALSE,&WlanSession::Cb,this,nullptr,nullptr);
    }
    ~WlanSession(){
        if(h){ WlanRegisterNotification(h,WLAN_NOTIFICATION_SOURCE_NONE,FALSE,nullptr,nullptr,nullptr,nullptr); WlanCloseHandle(h,nullptr); }
        if(evScan) CloseHandle(evScan); if(evConn) CloseHandle(evConn);
    }
    static VOID WINAPI Cb(PWLAN_NOTIFICATION_DATA d,PVOID ctx){
        WlanSession* s=(WlanSession*)ctx; if(!d || d->NotificationSource!=WLAN_NOTIFICATION_SOURCE_ACM) return;
        switch(d->NotificationCode){
        case wlan_notification_acm_scan_complete: case wlan_notification_acm_scan_fail: SetEvent(s->evScan); break;
        case wlan_notification_acm_connection_complete:
        case wlan_notification_acm_connection_attempt_fail: {
            DWORD reason=0;
            if(d->dwDataSize>=sizeof(WLAN_CONNECTION_NOTIFICATION_DATA) && d->pData) reason=((WLAN_CONNECTION_NOTIFICATION_DATA*)d->pData)->wlanReasonCode;
            s->connReason=reason;
            s->connResult = (d->NotificationCode==wlan_notification_acm_connection_complete && reason==WLAN_REASON_CODE_SUCCESS)? 1 : 2;
            SetEvent(s->evConn); } break;
        }
    }
};
static std::string WlanReasonText(DWORD reason){
    wchar_t buf[512]={0}; if(WlanReasonCodeToString(reason,512,buf,nullptr)==ERROR_SUCCESS && buf[0]) return W2U8(buf);
    char b[48]; snprintf(b,48,"reason %lu",reason); return b;
}
// read the list (no scan) - worker thread
static std::vector<WifiNet> WifiReadList(WlanSession& s){
    std::vector<WifiNet> out;
    WLAN_AVAILABLE_NETWORK_LIST* nl=nullptr;
    DWORD lr=WlanGetAvailableNetworkList(s.h,&s.iface,0,nullptr,&nl);
    if(lr==ERROR_ACCESS_DENIED) g_wifiLocBlocked=1;
    if(lr==ERROR_SUCCESS && nl){
        int hidden=0;
        for(DWORD i=0;i<nl->dwNumberOfItems;i++){ const WLAN_AVAILABLE_NETWORK& n=nl->Network[i];
            WifiNet w; w.ssid=std::string((char*)n.dot11Ssid.ucSSID,n.dot11Ssid.uSSIDLength);
            if(w.ssid.empty()){ hidden++; continue; }
            w.signal=(int)n.wlanSignalQuality; w.connected=(n.dwFlags&WLAN_AVAILABLE_NETWORK_CONNECTED)!=0;
            w.saved=(n.dwFlags&WLAN_AVAILABLE_NETWORK_HAS_PROFILE)!=0; w.profile=n.strProfileName;
            w.secured=n.bSecurityEnabled!=FALSE; w.auth=(int)n.dot11DefaultAuthAlgorithm; w.cipher=(int)n.dot11DefaultCipherAlgorithm;
            // one row per SSID: keep the connected entry, then the saved one, then the strongest
            bool merged=false;
            for(auto& o:out) if(o.ssid==w.ssid){ merged=true;
                if(w.connected||(!o.connected&&w.saved&&!o.saved)){ int sig=std::max(o.signal,w.signal); o=w; o.signal=sig; }
                else o.signal=std::max(o.signal,w.signal);
                break; }
            if(!merged) out.push_back(w);
        }
        g_wifiLocBlocked = (out.empty() && hidden>0 && WifiLocationPolicyOff())? 1 : 0;   // names withheld: the same block
        WlanFreeMemory(nl);
    }
    std::sort(out.begin(),out.end(),[](const WifiNet&a,const WifiNet&b){ if(a.connected!=b.connected) return a.connected; if(a.saved!=b.saved) return a.saved; return a.signal>b.signal; });
    return out;
}
static void WifiPublish(std::vector<WifiNet> v){ RunOnUi([v]{ g_wifi=v; }); }
static void WifiScanAsync(){
    if(g_wifiScanning.exchange(true)) return;
    std::thread([]{
        WlanSession s;
        if(!s.ok){ WifiSetStatus("No Wi-Fi adapter found",2); WifiPublish({}); g_wifiScanning=false; return; }
        WifiPublish(WifiReadList(s));                          // show what Windows has straight away
        ResetEvent(s.evScan);
        DWORD r=WlanScan(s.h,&s.iface,nullptr,nullptr,nullptr);
        if(r==ERROR_SUCCESS) WaitForSingleObject(s.evScan,4000);
        WifiPublish(WifiReadList(s));
        RunOnUi([]{ g_wifiLastScan=GetTickCount64(); });
        g_wifiScanning=false;
    }).detach();
}
static std::string XmlEsc(const std::string& in){ std::string o; for(char c:in){ switch(c){ case '&': o+="&amp;"; break; case '<': o+="&lt;"; break; case '>': o+="&gt;"; break; case '"': o+="&quot;"; break; case '\'': o+="&apos;"; break; default: o.push_back(c); } } return o; }
// build a personal-security profile; returns "" for kinds a shell cannot set up (enterprise / 802.1X)
static std::string WifiProfileXml(const WifiNet& n,const std::string& pass){
    const char* auth=nullptr; const char* enc="AES";
    switch(n.auth){
    case DOT11_AUTH_ALGO_80211_OPEN: auth="open"; enc = n.cipher==DOT11_CIPHER_ALGO_WEP40||n.cipher==DOT11_CIPHER_ALGO_WEP104||n.cipher==DOT11_CIPHER_ALGO_WEP? "WEP" : "none"; break;
    case DOT11_AUTH_ALGO_RSNA_PSK: auth="WPA2PSK"; break;
    case DOT11_AUTH_ALGO_WPA_PSK:  auth="WPAPSK"; enc = n.cipher==DOT11_CIPHER_ALGO_TKIP? "TKIP" : "AES"; break;
    case 9 /*DOT11_AUTH_ALGO_WPA3_SAE*/: auth="WPA3SAE"; break;
    case 10 /*DOT11_AUTH_ALGO_OWE*/: auth="OWE"; break;
    default: return "";
    }
    char hex[80]={0}; for(size_t i=0;i<n.ssid.size() && i<32;i++) snprintf(hex+i*2,3,"%02X",(unsigned char)n.ssid[i]);
    std::string x="<?xml version=\"1.0\"?><WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\"><name>"+XmlEsc(n.ssid)+"</name>"
        "<SSIDConfig><SSID><hex>"+std::string(hex)+"</hex><name>"+XmlEsc(n.ssid)+"</name></SSID></SSIDConfig><connectionType>ESS</connectionType><connectionMode>auto</connectionMode>"
        "<MSM><security><authEncryption><authentication>"+auth+"</authentication><encryption>"+enc+"</encryption><useOneX>false</useOneX></authEncryption>";
    if(strcmp(enc,"none")!=0 && strcmp(auth,"OWE")!=0)
        x+="<sharedKey><keyType>"+std::string(strcmp(enc,"WEP")==0? "networkKey" : "passPhrase")+"</keyType><protected>false</protected><keyMaterial>"+XmlEsc(pass)+"</keyMaterial></sharedKey>";
    x+="</security></MSM></WLANProfile>";
    return x;
}
static bool WifiNeedsPassword(const WifiNet& n){ return !n.saved && n.secured && n.auth!=DOT11_AUTH_ALGO_80211_OPEN && n.auth!=10; }
static void WifiConnectAsync(const WifiNet& net,const std::string& pass){
    if(g_wifiBusy.exchange(true)) return;
    g_wifiConnecting=net.ssid;
    WifiSetStatus("Connecting to "+net.ssid+"\xE2\x80\xA6",0);
    std::thread([net,pass]{
        WlanSession s;
        struct Done{ ~Done(){ g_wifiBusy=false; RunOnUi([]{ g_wifiConnecting.clear(); }); } } done;
        if(!s.ok){ WifiSetStatus("No Wi-Fi adapter found",2); return; }
        std::wstring profile=net.profile; bool madeProfile=false;
        if(!net.saved){
            std::string xml=WifiProfileXml(net,pass);
            if(xml.empty()){ WifiSetStatus(net.ssid+" uses a sign-in Aether can't set up (enterprise). Opening Windows Wi-Fi settings.",2);
                AetherShellExec(nullptr,L"open",L"ms-settings:network-wifi",nullptr,nullptr,SW_SHOWNORMAL); return; }
            DWORD reason=0; std::wstring wx=U82W(xml);
            DWORD r=WlanSetProfile(s.h,&s.iface,0,wx.c_str(),nullptr,TRUE,nullptr,&reason);
            if(r!=ERROR_SUCCESS){ WifiSetStatus("Couldn't save "+net.ssid+": "+(reason? WlanReasonText(reason) : std::string("the password isn't valid for this network")),2); return; }
            profile=U82W(net.ssid); madeProfile=true;
        }
        WLAN_CONNECTION_PARAMETERS cp={}; cp.wlanConnectionMode=wlan_connection_mode_profile; cp.strProfile=profile.c_str(); cp.dot11BssType=dot11_BSS_type_any;
        ResetEvent(s.evConn); s.connResult=0;
        DWORD r=WlanConnect(s.h,&s.iface,&cp,nullptr);
        if(r!=ERROR_SUCCESS){ if(madeProfile) WlanDeleteProfile(s.h,&s.iface,profile.c_str(),nullptr);
            WifiSetStatus("Couldn't connect to "+net.ssid+" (error "+std::to_string(r)+")",2); WifiScanAsync(); return; }
        WaitForSingleObject(s.evConn,20000);
        if(s.connResult==1) WifiSetStatus("Connected to "+net.ssid,1);
        else {
            if(madeProfile) WlanDeleteProfile(s.h,&s.iface,profile.c_str(),nullptr);   // do not remember a wrong password
            DWORD reason=s.connReason.load();
            std::string why = s.connResult==0? std::string("it didn't answer") : WlanReasonText(reason);
            bool pw = net.secured && madeProfile;
            WifiSetStatus("Couldn't connect to "+net.ssid+(pw? " \xE2\x80\x94 check the password." : ".")+" ("+why+")",2);
        }
        Sleep(300); WifiPublish(WifiReadList(s));
    }).detach();
}
static void WifiDisconnectAsync(){
    std::thread([]{
        WlanSession s; if(!s.ok) return;
        if(WlanDisconnect(s.h,&s.iface,nullptr)==ERROR_SUCCESS) WifiSetStatus("Disconnected",0);
        else WifiSetStatus("Couldn't disconnect",2);
        Sleep(600); WifiPublish(WifiReadList(s));
    }).detach();
}
static void WifiForgetAsync(const std::wstring& profile,const std::string& ssid){
    std::thread([profile,ssid]{
        WlanSession s; if(!s.ok) return;
        if(WlanDeleteProfile(s.h,&s.iface,profile.c_str(),nullptr)==ERROR_SUCCESS) WifiSetStatus("Forgot "+ssid,0);
        Sleep(300); WifiPublish(WifiReadList(s));
    }).detach();
}
static void OpenSettingsPage(const std::string& name);   // fwd
// a row was chosen anywhere in the shell
static void WifiChoose(const WifiNet& n){
    if(n.connected) return;
    if(WifiNeedsPassword(n)){ g_wifiPwFor=n.ssid; OpenSettingsPage("Network"); return; }
    WifiConnectAsync(n,"");
}
// the old names, now asynchronous (callers across the shell)
static void RefreshWifi(){ WifiScanAsync(); }
static void WifiConnect(const std::string& ssid){ for(auto& n:g_wifi) if(n.ssid==ssid){ WifiChoose(n); return; } }

// ---- a shared list: signal, name, lock, state, and Connect / Disconnect ----
// returns the height it used. Rows are `rowH` tall; `S` scales everything (the caelestia sidebar's metric).
static float DrawWifiList(ImDrawList* dl,ImGuiIO& io,float x0,float y0,float x1,float S,int maxRows,float rowH,bool click,int al,int idBase){
    float y=y0;
    // status line
    if(!g_wifiStatus.empty() && GetTickCount64()-g_wifiStatusAt<9000){
        ImU32 sc = g_wifiStatusKind==1? IM_COL32(120,210,140,255) : g_wifiStatusKind==2? IM_COL32(236,120,110,255) : COL_INK2;
        std::vector<std::string> lines; WrapLines(g_fSml,13*S,g_wifiStatus,x1-x0-8,2,lines);
        for(auto& l:lines){ TextAt(dl,g_fSml,13*S,V(x0+4,y),WithA(sc,al),l.c_str()); y+=18*S; }
        y+=4*S; }
    if(g_wifiLocBlocked.load()){                                    // Windows is withholding Wi-Fi information
        ImVec2 ca=V(x0,y), cb=V(x1,y+148*S);
        dl->AddRectFilled(ca,cb,WithA(Mix(COL_CARD2,IM_COL32(236,184,72,255),0.14f),al),12*S);
        MsIcon(dl,"location_off",V(x0+20*S,y+22*S),20*S,WithA(IM_COL32(236,184,72,255),al));
        TextAt(dl,g_fMed,15*S,V(x0+40*S,y+11*S),WithA(COL_INK,al),"Windows is hiding Wi-Fi networks");
        std::string why = std::string("Windows 11 only shows Wi-Fi networks to apps when location is on")+(WifiLocationPolicyOff()? ", and a policy on this PC (usually set by a privacy tool) turns location off." : ", and it is off.");
        std::vector<std::string> wl; WrapLines(g_fSml,13*S,why,x1-x0-24*S,3,wl);
        float wy=y+36*S; for(auto& l:wl){ TextAt(dl,g_fSml,13*S,V(x0+12*S,wy),WithA(COL_INK2,al),l.c_str()); wy+=18*S; }
        const char* bl="Allow Wi-Fi scanning (asks for admin)"; float bw=TextW(g_fSml,13.5f*S,bl)+28*S;
        ImVec2 ba=V(x0+12*S,cb.y-40*S), bb=V(ba.x+bw,cb.y-12*S); bool bh=io.MousePos.x>=ba.x&&io.MousePos.x<bb.x&&io.MousePos.y>=ba.y&&io.MousePos.y<bb.y;
        dl->AddRectFilled(ba,bb,WithA(Mix(COL_GOLD,IM_COL32(255,255,255,255),bh? 0.15f:0.0f),al),14*S);
        TextAt(dl,g_fSml,13.5f*S,V(ba.x+14*S,ba.y+6*S),WithA(M3OnPrimary(),al),bl);
        if(bh&&click) WifiAllowLocation();
        return cb.y+8*S-y0; }
    if(g_wifi.empty()){
        const char* m = g_wifiScanning? "Looking for networks\xE2\x80\xA6" : "No networks found";
        TextAt(dl,g_fSml,14*S,V(x0+4,y+6*S),WithA(COL_INK2,al),m); return y+30*S-y0; }
    int n=std::min((int)g_wifi.size(),maxRows);
    for(int i=0;i<n;i++){
        const WifiNet w=g_wifi[i];
        ImVec2 a=V(x0,y), b=V(x1,y+rowH-4*S);
        bool hov=io.MousePos.x>=a.x&&io.MousePos.x<b.x&&io.MousePos.y>=a.y&&io.MousePos.y<b.y;
        float ha=HoverAnim(idBase+i,hov);
        bool busyThis = g_wifiBusy && g_wifiConnecting==w.ssid;
        if(w.connected) dl->AddRectFilled(a,b,WithA(Mix(COL_CARD2,COL_GOLD,0.22f),al),10*S);
        else if(ha>0.01f) dl->AddRectFilled(a,b,WithA(COL_INK2,(int)(ha*36*al/255)),10*S);
        float cy=(a.y+b.y)*0.5f;
        const char* sig = w.signal>75? "signal_wifi_4_bar" : w.signal>50? "network_wifi_3_bar" : w.signal>25? "network_wifi_2_bar" : "network_wifi_1_bar";
        MsIcon(dl,sig,V(a.x+16*S,cy),20*S,WithA(w.connected? COL_GOLD : COL_INK,al));
        float tx=a.x+34*S;
        // right side: action
        float bx=b.x-8*S;
        const char* act = w.connected? "Disconnect" : busyThis? "Connecting\xE2\x80\xA6" : "Connect";
        float aw=TextW(g_fSml,13*S,act)+20*S; ImVec2 ba=V(bx-aw,cy-13*S), bb=V(bx,cy+13*S);
        bool ah=io.MousePos.x>=ba.x&&io.MousePos.x<bb.x&&io.MousePos.y>=ba.y&&io.MousePos.y<bb.y;
        if(hov||w.connected||busyThis){
            dl->AddRectFilled(ba,bb,WithA(w.connected? Mix(COL_CARD2,COL_INK2,ah? 0.4f:0.2f) : Mix(COL_GOLD,IM_COL32(255,255,255,255),ah? 0.15f:0.0f),al),13*S);
            TextAt(dl,g_fSml,13*S,V(ba.x+10*S,ba.y+5*S),WithA(w.connected? COL_INK : M3OnPrimary(),al),act); }
        float right=ba.x-8*S;
        // forget (saved, not connected) on hover
        bool fh=false;
        if(hov && w.saved && !w.connected && !busyThis){ ImVec2 fc=V(right-12*S,cy); fh=fabsf(io.MousePos.x-fc.x)<12*S&&fabsf(io.MousePos.y-fc.y)<12*S;
            if(fh) dl->AddCircleFilled(fc,12*S,WithA(COL_INK2,(int)(50*al/255)),16);
            MsIcon(dl,"delete",fc,16*S,WithA(COL_INK2,al)); right=fc.x-16*S;
            if(fh&&click){ WifiForgetAsync(w.profile,w.ssid); } }
        std::string sub = w.connected? "Connected" : w.saved? "Saved" : w.secured? "Secured" : "Open";
        float nameW=right-tx-(w.secured? 18*S : 0);
        TextAt(dl,g_fMed,15*S,V(tx,cy-17*S),WithA(w.connected? COL_GOLD : COL_INK,al),Clip(g_fMed,15*S,w.ssid,nameW).c_str());
        if(w.secured){ float lw=std::min(TextW(g_fMed,15*S,w.ssid.c_str()),nameW); MsIcon(dl,"lock",V(tx+lw+10*S,cy-9*S),12*S,WithA(COL_INK2,al)); }
        TextAt(dl,g_fSml,12*S,V(tx,cy+2*S),WithA(COL_INK2,al),sub.c_str());
        if(click && !fh){
            if(ah && w.connected) WifiDisconnectAsync();
            else if(hov && !w.connected && !g_wifiBusy) WifiChoose(w);
        }
        y+=rowH;
    }
    return y-y0;
}
