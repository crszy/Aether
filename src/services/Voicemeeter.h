// src/services/Voicemeeter.h  —  Aether shell
// Voicemeeter (Standard / Banana / Potato) through its own remote API, VoicemeeterRemote64.dll, found through
// the installer's uninstall entry. Aether logs in once, then reads strips, buses, gains, mutes, routing and live
// levels every frame the mixer is on screen, and writes changes straight back - the same thing Voicemeeter's
// window shows, controllable from the dashboard, Settings or the sidebar.
#pragma once

struct VmApi {
    HMODULE dll=nullptr;
    long (__stdcall *Login)()=nullptr;
    long (__stdcall *Logout)()=nullptr;
    long (__stdcall *RunVoicemeeter)(long)=nullptr;
    long (__stdcall *GetVoicemeeterType)(long*)=nullptr;
    long (__stdcall *IsParametersDirty)()=nullptr;
    long (__stdcall *GetParameterFloat)(char*,float*)=nullptr;
    long (__stdcall *GetParameterStringW)(char*,wchar_t*)=nullptr;
    long (__stdcall *SetParameterFloat)(char*,float)=nullptr;
    long (__stdcall *SetParameters)(char*)=nullptr;
    long (__stdcall *GetLevel)(long,long,float*)=nullptr;
};
struct VmStrip { std::string label; float gain=0; bool mute=false; bool route[8]={}; float level=0; bool virt=false; };
struct VmBus   { std::string label; float gain=0; bool mute=false; float level=0; };
struct VmState {
    VmApi api; bool tried=false, loggedIn=false; int type=0;     // 1 Voicemeeter, 2 Banana, 3 Potato
    std::vector<VmStrip> strips; std::vector<VmBus> buses; int hwIn=0, hwOut=0;
    ULONGLONG lastRead=0; std::string err;
};
static VmState g_vm;

static std::wstring VmFindDll(){
    const wchar_t* keys[]={ L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VB:Voicemeeter {17359A74-1236-5467}",
                            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VB:Voicemeeter {17359A74-1236-5467}" };
    for(auto k:keys){ wchar_t buf[MAX_PATH]={0}; DWORD n=sizeof(buf);
        if(RegGetValueW(HKEY_LOCAL_MACHINE,k,L"UninstallString",RRF_RT_REG_SZ,nullptr,buf,&n)==ERROR_SUCCESS){
            std::wstring p=buf; size_t s=p.find_last_of(L"\\/"); if(s!=std::wstring::npos) p=p.substr(0,s);
            if(!p.empty() && p.front()==L'"') p.erase(0,1);
            std::wstring dll=p+L"\\VoicemeeterRemote64.dll";
            if(GetFileAttributesW(dll.c_str())!=INVALID_FILE_ATTRIBUTES) return dll; } }
    std::wstring def=L"C:\\Program Files (x86)\\VB\\Voicemeeter\\VoicemeeterRemote64.dll";
    return GetFileAttributesW(def.c_str())!=INVALID_FILE_ATTRIBUTES? def : L"";
}
static bool VmConnect(){
    VmState& v=g_vm;
    if(v.loggedIn) return true;
    if(v.tried && !v.api.dll) return false;
    v.tried=true;
    if(!v.api.dll){
        std::wstring p=VmFindDll(); if(p.empty()){ v.err="Voicemeeter is not installed"; return false; }
        v.api.dll=LoadLibraryW(p.c_str()); if(!v.api.dll){ v.err="VoicemeeterRemote64.dll would not load"; return false; }
        auto G=[&](const char* n){ return GetProcAddress(v.api.dll,n); };
        v.api.Login=(decltype(v.api.Login))G("VBVMR_Login"); v.api.Logout=(decltype(v.api.Logout))G("VBVMR_Logout");
        v.api.RunVoicemeeter=(decltype(v.api.RunVoicemeeter))G("VBVMR_RunVoicemeeter");
        v.api.GetVoicemeeterType=(decltype(v.api.GetVoicemeeterType))G("VBVMR_GetVoicemeeterType");
        v.api.IsParametersDirty=(decltype(v.api.IsParametersDirty))G("VBVMR_IsParametersDirty");
        v.api.GetParameterFloat=(decltype(v.api.GetParameterFloat))G("VBVMR_GetParameterFloat");
        v.api.GetParameterStringW=(decltype(v.api.GetParameterStringW))G("VBVMR_GetParameterStringW");
        v.api.SetParameterFloat=(decltype(v.api.SetParameterFloat))G("VBVMR_SetParameterFloat");
        v.api.SetParameters=(decltype(v.api.SetParameters))G("VBVMR_SetParameters");
        v.api.GetLevel=(decltype(v.api.GetLevel))G("VBVMR_GetLevel");
        if(!v.api.Login||!v.api.GetVoicemeeterType||!v.api.GetParameterFloat){ v.err="VoicemeeterRemote64.dll is missing functions"; return false; }
    }
    long r=v.api.Login();
    if(r<0){ v.err="Voicemeeter refused the connection"; return false; }
    v.loggedIn=true;
    long t=0; if(v.api.GetVoicemeeterType(&t)!=0) t=0;
    v.type=(int)t;
    switch(v.type){ case 1: v.hwIn=2; v.hwOut=2; v.strips.assign(3,{}); v.buses.assign(2,{}); break;
                    case 2: v.hwIn=3; v.hwOut=3; v.strips.assign(5,{}); v.buses.assign(5,{}); break;
                    case 3: v.hwIn=5; v.hwOut=5; v.strips.assign(8,{}); v.buses.assign(8,{}); break;
                    default: v.err="Voicemeeter is not running"; break; }
    for(size_t i=0;i<v.strips.size();i++) v.strips[i].virt=(int)i>=v.hwIn;
    v.lastRead=0;
    return true;
}
static void VmDisconnect(){ if(g_vm.loggedIn && g_vm.api.Logout) g_vm.api.Logout(); g_vm.loggedIn=false; }
static int VmBusCount(){ return (int)g_vm.buses.size(); }
static const char* VmBusName(int i){           // A1..An then B1..Bn
    static char b[8]; int hw=g_vm.hwOut;
    if(i<hw) snprintf(b,8,"A%d",i+1); else snprintf(b,8,"B%d",i-hw+1); return b; }
static float VmGetF(const std::string& p){ float f=0; if(g_vm.api.GetParameterFloat){ std::string s=p; g_vm.api.GetParameterFloat((char*)s.c_str(),&f); } return f; }
static std::string VmGetS(const std::string& p){ wchar_t w[512]={0}; if(g_vm.api.GetParameterStringW){ std::string s=p; g_vm.api.GetParameterStringW((char*)s.c_str(),w); } return W2U8(w); }
static void VmSetF(const std::string& p,float f){ if(g_vm.api.SetParameterFloat){ std::string s=p; g_vm.api.SetParameterFloat((char*)s.c_str(),f); } }
static void VmCommand(const char* script){ if(g_vm.api.SetParameters){ std::string s=script; g_vm.api.SetParameters((char*)s.c_str()); } }

// refresh parameters (when Voicemeeter says they changed) and the meters (always)
static void VmTick(){
    VmState& v=g_vm;
    if(!VmConnect()) return;
    if(v.type==0){ long t=0; if(v.api.GetVoicemeeterType && v.api.GetVoicemeeterType(&t)==0 && t>0){ v.loggedIn=false; v.tried=false; VmDisconnect(); VmConnect(); } return; }
    bool dirty = v.api.IsParametersDirty? v.api.IsParametersDirty()!=0 : true;
    ULONGLONG now=GetTickCount64();
    if(dirty || now-v.lastRead>2000){
        v.lastRead=now;
        char p[64];
        for(size_t i=0;i<v.strips.size();i++){ VmStrip& s=v.strips[i];
            snprintf(p,64,"Strip[%zu].Label",i); s.label=VmGetS(p);
            snprintf(p,64,"Strip[%zu].Gain",i);  s.gain=VmGetF(p);
            snprintf(p,64,"Strip[%zu].Mute",i);  s.mute=VmGetF(p)>0.5f;
            for(int b=0;b<VmBusCount() && b<8;b++){ snprintf(p,64,"Strip[%zu].%s",i,VmBusName(b)); s.route[b]=VmGetF(p)>0.5f; } }
        for(size_t i=0;i<v.buses.size();i++){ VmBus& b=v.buses[i];
            snprintf(p,64,"Bus[%zu].Label",i); b.label=VmGetS(p);
            snprintf(p,64,"Bus[%zu].Gain",i);  b.gain=VmGetF(p);
            snprintf(p,64,"Bus[%zu].Mute",i);  b.mute=VmGetF(p)>0.5f; }
    }
    if(v.api.GetLevel){
        // input levels (post-fader): hardware strips carry 2 channels, virtual strips 8; buses 8 each
        long ch=0;
        for(size_t i=0;i<v.strips.size();i++){ int n=v.strips[i].virt? 8 : 2; float m=0;
            for(int c=0;c<n;c++){ float f=0; if(v.api.GetLevel(1,ch+c,&f)==0) m=std::max(m,f); }
            ch+=n; float& lv=v.strips[i].level; lv = m>lv? m : lv+(m-lv)*std::min(1.0f,g_frameDt*8.0f); }
        for(size_t i=0;i<v.buses.size();i++){ float m=0;
            for(int c=0;c<8;c++){ float f=0; if(v.api.GetLevel(3,(long)(i*8+c),&f)==0) m=std::max(m,f); }
            float& lv=v.buses[i].level; lv = m>lv? m : lv+(m-lv)*std::min(1.0f,g_frameDt*8.0f); }
    }
}
static const char* VmTypeName(){ return g_vm.type==1? "Voicemeeter" : g_vm.type==2? "Voicemeeter Banana" : g_vm.type==3? "Voicemeeter Potato" : "Voicemeeter"; }
