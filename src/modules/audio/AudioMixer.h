// src/modules/audio/AudioMixer.h  —  Aether shell
// Aether replaces the Windows shell, so it needs its own sound mixer. One card, four views:
//   Outputs     - every playback device with a live level meter (you can SEE which one is getting sound),
//                 volume, mute, make default, and "send all audio here" (default + every app routed back to it)
//   Inputs      - every recording device with its live input level, volume, mute, make default
//   Apps        - every app playing sound: volume, mute, level, and WHICH OUTPUT it plays through
//   Voicemeeter - strips and buses of a running Voicemeeter / Banana / Potato: faders, mutes, A/B routing, meters
// plus Reset audio (all apps back on the default output at full volume) and a service restart.
// It can live in the dashboard (widget or its own tab), the caelestia sidebar and Settings > Audio.
#pragma once
#include <functional>
#include <functiondiscoverykeys_devpkey.h>
#include <roapi.h>
#include <winstring.h>
#include "src/services/Voicemeeter.h"

// ---- undocumented but long-stable Windows audio policy interfaces (the ones EarTrumpet / SoundSwitch use) ----
static const CLSID CLSID_AmPolicyConfig = {0x870af99c,0x171d,0x4f9e,{0xaf,0x0d,0xe6,0x3d,0xf4,0x0c,0x2b,0xc9}};
static const IID   IID_AmPolicyConfig   = {0xf8679f50,0x850a,0x41cf,{0x9c,0x72,0x43,0x0f,0x29,0x02,0x90,0xc8}};
struct IAmPolicyConfig : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR,WAVEFORMATEX**)=0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR,INT,WAVEFORMATEX**)=0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR)=0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR,WAVEFORMATEX*,WAVEFORMATEX*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR,INT,PINT64,PINT64)=0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR,PINT64)=0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR,void*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR,void*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR,const PROPERTYKEY&,PROPVARIANT*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR,const PROPERTYKEY&,PROPVARIANT*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR,ERole)=0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR,INT)=0;
};
// per-app output routing (Windows.Media.Internal.AudioPolicyConfig); the IID changed in build 21390
static void* AmPolicyFactory(){
    static void* f=nullptr; static bool tried=false;
    if(tried) return f; tried=true;
    const wchar_t* cls=L"Windows.Media.Internal.AudioPolicyConfig";
    HSTRING hs=nullptr; if(FAILED(WindowsCreateString(cls,(UINT32)wcslen(cls),&hs))) return nullptr;
    static const IID iidNew={0xab3d4648,0xe242,0x459f,{0xb0,0x2f,0x54,0x1c,0x70,0x30,0x63,0x24}};
    static const IID iidOld={0x2a59116d,0x6c4f,0x45e0,{0xa7,0x4f,0x70,0x7e,0x3f,0xef,0x92,0x58}};
    if(FAILED(RoGetActivationFactory(hs,iidNew,&f))) RoGetActivationFactory(hs,iidOld,&f);
    WindowsDeleteString(hs);
    return f;
}
static std::wstring AmPersistId(const std::wstring& devId,bool capture){
    return L"\\\\?\\SWD#MMDEVAPI#"+devId+(capture? L"#{2eef81be-33fa-4800-9670-1cd474972c3f}" : L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}");
}
static std::wstring AmUnpersist(const std::wstring& s){
    size_t a=s.find(L"MMDEVAPI#"); if(a==std::wstring::npos) return s; a+=9;
    size_t b=s.find(L'#',a); return s.substr(a,b==std::wstring::npos? std::wstring::npos : b-a);
}

struct AmDev { std::wstring id; std::string name; bool capture=false, def=false, defComm=false; float vol=1; bool mute=false; float peak=0; };
struct AmState {
    std::mutex mtx;
    std::vector<AmDev> outs, ins;
    std::unordered_map<DWORD,std::wstring> route;     // pid -> output device id ("" = default)
    std::vector<std::function<void()>> jobs;           // COM work for the worker thread
    std::atomic<bool> running{false};
    std::atomic<ULONGLONG> lastDraw{0};
};
static AmState g_am;
static void AmJob(std::function<void()> fn){ std::lock_guard<std::mutex> lk(g_am.mtx); g_am.jobs.push_back(std::move(fn)); }

static void AmWorker(){
    try{ winrt::init_apartment(winrt::apartment_type::multi_threaded); }catch(...){ CoInitializeEx(nullptr,COINIT_MULTITHREADED); }
    IMMDeviceEnumerator* en=nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),(void**)&en);
    struct Live { IMMDevice* dev=nullptr; IAudioMeterInformation* meter=nullptr; IAudioEndpointVolume* vol=nullptr; IAudioClient* keep=nullptr; };
    std::unordered_map<std::wstring,Live> live;
    ULONGLONG lastEnum=0, lastRoute=0;
    while(en && GetTickCount64()-g_am.lastDraw.load()<3000){
        std::vector<std::function<void()>> jobs; { std::lock_guard<std::mutex> lk(g_am.mtx); jobs.swap(g_am.jobs); }
        for(auto& j:jobs){ try{ j(); }catch(...){} lastEnum=0; }
        ULONGLONG now=GetTickCount64();
        if(now-lastEnum>1500){
            lastEnum=now;
            std::vector<AmDev> outs, ins;
            std::wstring defR, defRC, defC;
            auto defId=[&](EDataFlow f,ERole r)->std::wstring{ IMMDevice* d=nullptr; std::wstring s;
                if(SUCCEEDED(en->GetDefaultAudioEndpoint(f,r,&d))&&d){ LPWSTR id=nullptr; if(SUCCEEDED(d->GetId(&id))&&id){ s=id; CoTaskMemFree(id); } d->Release(); } return s; };
            defR=defId(eRender,eMultimedia); defRC=defId(eRender,eCommunications); defC=defId(eCapture,eMultimedia);
            std::unordered_map<std::wstring,bool> seen;
            for(int flow=0;flow<2;flow++){
                IMMDeviceCollection* col=nullptr;
                if(FAILED(en->EnumAudioEndpoints(flow==0? eRender : eCapture,DEVICE_STATE_ACTIVE,&col))||!col) continue;
                UINT n=0; col->GetCount(&n);
                for(UINT i=0;i<n;i++){
                    IMMDevice* d=nullptr; if(FAILED(col->Item(i,&d))||!d) continue;
                    AmDev a; a.capture=flow==1;
                    LPWSTR id=nullptr; if(SUCCEEDED(d->GetId(&id))&&id){ a.id=id; CoTaskMemFree(id); }
                    IPropertyStore* ps=nullptr;
                    if(SUCCEEDED(d->OpenPropertyStore(STGM_READ,&ps))&&ps){ PROPVARIANT pv; PropVariantInit(&pv);
                        if(SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName,&pv)) && pv.vt==VT_LPWSTR) a.name=W2U8(pv.pwszVal);
                        PropVariantClear(&pv); ps->Release(); }
                    a.def = a.id==(a.capture? defC : defR); a.defComm = !a.capture && a.id==defRC;
                    Live& L=live[a.id]; seen[a.id]=true;
                    if(!L.dev){ L.dev=d; d->AddRef();
                        d->Activate(__uuidof(IAudioMeterInformation),CLSCTX_ALL,nullptr,(void**)&L.meter);
                        d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,(void**)&L.vol);
                        if(a.capture){   // an input only reports a level while something is capturing: keep a quiet stream open
                            if(SUCCEEDED(d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,(void**)&L.keep))&&L.keep){
                                WAVEFORMATEX* wf=nullptr;
                                if(SUCCEEDED(L.keep->GetMixFormat(&wf))&&wf){
                                    if(SUCCEEDED(L.keep->Initialize(AUDCLNT_SHAREMODE_SHARED,0,2000000,0,wf,nullptr))) L.keep->Start();
                                    CoTaskMemFree(wf); } } } }
                    if(L.vol){ L.vol->GetMasterVolumeLevelScalar(&a.vol); BOOL m=FALSE; L.vol->GetMute(&m); a.mute=m!=FALSE; }
                    (a.capture? ins : outs).push_back(a);
                    d->Release();
                }
                col->Release();
            }
            for(auto it=live.begin(); it!=live.end();){ if(!seen.count(it->first)){ Live& L=it->second;
                    if(L.keep){ L.keep->Stop(); L.keep->Release(); } if(L.meter) L.meter->Release(); if(L.vol) L.vol->Release(); if(L.dev) L.dev->Release();
                    it=live.erase(it); } else ++it; }
            std::lock_guard<std::mutex> lk(g_am.mtx);
            // keep the smoothed peaks across the rebuild
            for(auto& o:outs) for(auto& p:g_am.outs) if(p.id==o.id) o.peak=p.peak;
            for(auto& o:ins)  for(auto& p:g_am.ins)  if(p.id==o.id) o.peak=p.peak;
            g_am.outs=outs; g_am.ins=ins;
        }
        if(now-lastRoute>2000){
            lastRoute=now;
            void* f=AmPolicyFactory();
            std::vector<AppVol> mix; { std::lock_guard<std::mutex> lk(g_mixerMtx); mix=g_mixer; }
            std::unordered_map<DWORD,std::wstring> route;
            if(f){ void** vt=*(void***)f; auto get=(HRESULT(__stdcall*)(void*,UINT,int,int,HSTRING*))vt[26];
                for(auto& a:mix){ HSTRING h=nullptr;
                    if(SUCCEEDED(get(f,a.pid,(int)eRender,(int)eMultimedia,&h)) && h){ UINT32 len=0; const wchar_t* w=WindowsGetStringRawBuffer(h,&len);
                        route[a.pid]= w&&len? AmUnpersist(std::wstring(w,len)) : std::wstring(); WindowsDeleteString(h); }
                    else route[a.pid]=L""; } }
            std::lock_guard<std::mutex> lk(g_am.mtx); g_am.route=route;
        }
        { std::lock_guard<std::mutex> lk(g_am.mtx);
          for(auto* list:{&g_am.outs,&g_am.ins}) for(auto& d:*list){ auto it=live.find(d.id); float pk=0;
              if(it!=live.end() && it->second.meter) it->second.meter->GetPeakValue(&pk);
              d.peak = pk>d.peak? pk : d.peak*0.86f; } }
        Sleep(45);
    }
    for(auto& kv:live){ Live& L=kv.second; if(L.keep){ L.keep->Stop(); L.keep->Release(); } if(L.meter) L.meter->Release(); if(L.vol) L.vol->Release(); if(L.dev) L.dev->Release(); }
    if(en) en->Release();
    g_am.running=false;
}
static void AmEnsure(){ g_am.lastDraw=GetTickCount64(); if(!g_am.running.exchange(true)) std::thread(AmWorker).detach(); }

// ---- actions (queued onto the worker) ----
static void AmSetDefault(const std::wstring& id,bool includeComms){
    AmJob([id,includeComms]{ IAmPolicyConfig* pc=nullptr;
        if(SUCCEEDED(CoCreateInstance(CLSID_AmPolicyConfig,nullptr,CLSCTX_ALL,IID_AmPolicyConfig,(void**)&pc))&&pc){
            pc->SetDefaultEndpoint(id.c_str(),eConsole); pc->SetDefaultEndpoint(id.c_str(),eMultimedia);
            if(includeComms) pc->SetDefaultEndpoint(id.c_str(),eCommunications); pc->Release(); } });
}
static void AmSetDeviceVolume(const std::wstring& id,float v,int mute /*-1 keep*/){
    AmJob([id,v,mute]{ IMMDeviceEnumerator* en=nullptr;
        if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),(void**)&en))&&en){
            IMMDevice* d=nullptr; if(SUCCEEDED(en->GetDevice(id.c_str(),&d))&&d){ IAudioEndpointVolume* ev=nullptr;
                if(SUCCEEDED(d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,(void**)&ev))&&ev){
                    if(mute<0) ev->SetMasterVolumeLevelScalar(std::clamp(v,0.0f,1.0f),nullptr); else ev->SetMute(mute? TRUE:FALSE,nullptr);
                    ev->Release(); } d->Release(); } en->Release(); } });
}
static void AmRouteApp(DWORD pid,const std::wstring& devId){          // "" = back to the default output
    AmJob([pid,devId]{ void* f=AmPolicyFactory(); if(!f) return;
        void** vt=*(void***)f; auto set=(HRESULT(__stdcall*)(void*,UINT,int,int,HSTRING))vt[25];
        HSTRING h=nullptr; std::wstring full= devId.empty()? L"" : AmPersistId(devId,false);
        if(!full.empty()) WindowsCreateString(full.c_str(),(UINT32)full.size(),&h);
        set(f,pid,(int)eRender,(int)eMultimedia,h); set(f,pid,(int)eRender,(int)eConsole,h);
        if(h) WindowsDeleteString(h); });
    std::lock_guard<std::mutex> lk(g_am.mtx); g_am.route[pid]=devId;
}
static void AmClearRoutes(){
    AmJob([]{ void* f=AmPolicyFactory(); if(!f) return; void** vt=*(void***)f;
        auto clr=(HRESULT(__stdcall*)(void*))vt[27]; clr(f); });
    std::lock_guard<std::mutex> lk(g_am.mtx); for(auto& kv:g_am.route) kv.second.clear();
}
static void AmResetAudio(){
    AmClearRoutes();
    std::vector<AppVol> mix; { std::lock_guard<std::mutex> lk(g_mixerMtx); mix=g_mixer; }
    for(auto& a:mix){ SetAppVolume(a.pid,1.0f,false,false); SetAppVolume(a.pid,0,true,false); }
    std::vector<AmDev> outs; { std::lock_guard<std::mutex> lk(g_am.mtx); outs=g_am.outs; }
    for(auto& d:outs) if(d.def) AmSetDeviceVolume(d.id,0,0);
}

// ============================================================================================ UI
static bool AmHSlider(ImDrawList* dl,ImGuiIO& io,ImVec2 a,ImVec2 b,float& v,int id){
    static int active=-1;
    float cy=(a.y+b.y)*0.5f, th=6;
    bool hov=io.MousePos.x>=a.x-6&&io.MousePos.x<b.x+6&&io.MousePos.y>=a.y-6&&io.MousePos.y<b.y+6;
    if(hov&&io.MouseClicked[0]) active=id;
    if(active==id && !io.MouseDown[0]) active=-1;
    bool ch=false;
    if(active==id){ float nv=std::clamp((io.MousePos.x-a.x)/std::max(1.0f,b.x-a.x),0.0f,1.0f); if(fabsf(nv-v)>0.001f){ v=nv; ch=true; } }
    float x=a.x+(b.x-a.x)*std::clamp(v,0.0f,1.0f);
    dl->AddRectFilled(V(a.x,cy-th*0.5f),V(b.x,cy+th*0.5f),V2Track(),th*0.5f);
    dl->AddRectFilled(V(a.x,cy-th*0.5f),V(std::max(a.x+th,x),cy+th*0.5f),COL_GOLD,th*0.5f);
    float ha=HoverAnim(0x6C000+id,hov||active==id);
    dl->AddRectFilled(V(x-2.5f,cy-9-ha*2),V(x+2.5f,cy+9+ha*2),COL_INK,3);
    return ch;
}
static void AmMeter(ImDrawList* dl,ImVec2 a,ImVec2 b,float peak,bool vertical=false){
    float p=std::clamp(peak,0.0f,1.0f);
    float db = p>0.00001f? 20.0f*log10f(p) : -60.0f; float f=std::clamp((db+60.0f)/60.0f,0.0f,1.0f);
    dl->AddRectFilled(a,b,V2Track(),vertical? (b.x-a.x)*0.5f : (b.y-a.y)*0.5f);
    ImU32 c = f>0.92f? COL_ERR : f>0.75f? IM_COL32(240,190,70,255) : Mix(COL_GOLD,COL_INK,0.25f);
    if(vertical){ float y=b.y-(b.y-a.y)*f; if(f>0.01f) dl->AddRectFilled(V(a.x,y),b,c,(b.x-a.x)*0.5f); }
    else { float x=a.x+(b.x-a.x)*f; if(f>0.01f) dl->AddRectFilled(a,V(x,b.y),c,(b.y-a.y)*0.5f); }
}
// g_amView (0 outputs 1 inputs 2 apps 3 voicemeeter) lives with the globals in main.cpp

static void DrawAudioMixer(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,int uid,bool card){
    AmEnsure();
    static ULONGLONG mixAt=0; if(GetTickCount64()-mixAt>700){ mixAt=GetTickCount64(); RefreshMixer(); }
    if(card) Card(dl,o,s);
    bool click=io.MouseClicked[0];
    const float P=14;
    float x0=o.x+P, x1=o.x+s.x-P, y=o.y+P;
    // header: title + segmented views + actions
    MsIcon(dl,"tune",V(x0+11,y+14),22,COL_INK);
    TextAt(dl,g_fMed,18,V(x0+28,y+3),COL_INK,"Audio");
    static const char* VIEWS[4]={"Outputs","Inputs","Apps","Voicemeeter"};
    static const char* VICON[4]={"speaker","mic","apps","graphic_eq"};
    { float segW=0; for(int i=0;i<4;i++) segW+=TextW(g_fSml,13.5f,VIEWS[i])+40;
      const bool iconsOnly = segW > (x1-x0)-190;                           // narrow (sidebar): icon tabs, tooltips by name
      auto tabW=[&](int i){ return iconsOnly? 38.0f : TextW(g_fSml,13.5f,VIEWS[i])+40; };
      if(iconsOnly) segW=4*38.0f;
      float sx=std::max(x0+100,std::min(x1-segW-90,o.x+(s.x-segW)*0.5f)), sh=30;
      dl->AddRectFilled(V(sx,y),V(sx+segW,y+sh),Mix(COL_CARD2,COL_INK2,0.12f),sh*0.5f);
      float xx=sx;
      for(int i=0;i<4;i++){ float w=tabW(i); bool sel=g_amView==i;
          bool h=io.MousePos.x>=xx&&io.MousePos.x<xx+w&&io.MousePos.y>=y&&io.MousePos.y<y+sh;
          if(sel) dl->AddRectFilled(V(xx+2,y+2),V(xx+w-2,y+sh-2),Mix(COL_INK,COL_GOLD,0.2f),(sh-4)*0.5f);
          else if(h) dl->AddRectFilled(V(xx+2,y+2),V(xx+w-2,y+sh-2),WithA(COL_INK2,40),(sh-4)*0.5f);
          ImU32 c=sel? M3OnPrimary() : COL_INK;
          if(iconsOnly) MsIcon(dl,VICON[i],V(xx+w*0.5f,y+sh*0.5f),17,c);
          else { MsIcon(dl,VICON[i],V(xx+16,y+sh*0.5f),16,c); TextAt(dl,g_fSml,13.5f,V(xx+28,y+7),c,VIEWS[i]); }
          if(h&&click) g_amView=i;
          xx+=w; } }
    { float bx=x1-14;
      if(V2Btn(dl,io,V(bx,y+15),30,30,"settings",false,0x6C100+uid,click)) AetherShellExec(nullptr,L"open",L"mmsys.cpl",nullptr,nullptr,SW_SHOWNORMAL);
      if(V2Btn(dl,io,V(bx-38,y+15),30,30,"restart_alt",false,0x6C101+uid,click)) AmResetAudio(); }
    y+=44;
    ImVec2 la=V(o.x+6,y), lb=V(o.x+s.x-6,o.y+s.y-(g_amView==3? 8 : 44));
    dl->PushClipRect(la,lb,true);
    static std::unordered_map<int,float> scroll; float& sc=scroll[uid*8+g_amView];
    bool inList=io.MousePos.x>=la.x&&io.MousePos.x<lb.x&&io.MousePos.y>=la.y&&io.MousePos.y<lb.y;
    if(inList && io.MouseWheel!=0 && g_amView!=3) sc=std::max(0.0f,sc-io.MouseWheel*60);
    float ry=y-sc;
    const float RH=64;
    std::vector<AmDev> outs, ins; std::unordered_map<DWORD,std::wstring> route;
    { std::lock_guard<std::mutex> lk(g_am.mtx); outs=g_am.outs; ins=g_am.ins; route=g_am.route; }
    auto rowBg=[&](float yy,int id){ ImVec2 a=V(x0-4,yy), b=V(x1+4,yy+RH-6);
        bool h=io.MousePos.x>=a.x&&io.MousePos.x<b.x&&io.MousePos.y>=a.y&&io.MousePos.y<b.y;
        float ha=HoverAnim(0x6C200+id,h);
        dl->AddRectFilled(a,b,Mix(Mix(COL_CARD,COL_CARD2,0.45f),COL_CARD2,0.5f*ha),14); };
    if(g_amView==0||g_amView==1){
        const std::vector<AmDev>& L = g_amView==0? outs : ins;
        if(L.empty()) TextAt(dl,g_fSml,14,V(x0,ry+10),COL_INK2,"Looking for devices\xE2\x80\xA6");
        int k=0;
        for(const AmDev& d:L){
            rowBg(ry,uid*100+k+g_amView*50);
            ImVec2 ic=V(x0+18,ry+(RH-6)*0.5f);
            dl->AddCircleFilled(ic,17,d.def? Mix(COL_INK,COL_GOLD,0.2f) : Mix(COL_CARD2,COL_INK2,0.2f),24);
            MsIcon(dl,g_amView==0? (d.mute? "volume_off" : "speaker") : (d.mute? "mic_off" : "mic"),ic,20,d.def? M3OnPrimary() : COL_INK);
            float tx=ic.x+28, right=x1-150;
            std::string sub = d.def? (d.defComm? "Default \xC2\xB7 calls" : "Default") : (d.defComm? "Calls" : "");
            float subW = sub.empty()? 0.0f : TextW(g_fSml,12,sub.c_str())+22;
            std::string nm=Clip(g_fMed,15,d.name,right-tx-10-subW);
            TextAt(dl,g_fMed,15,V(tx,ry+8),COL_INK,nm.c_str());
            float sw2=0; if(!sub.empty()){ sw2=TextW(g_fSml,12,sub.c_str())+14; float sxp=tx+TextW(g_fMed,15,nm.c_str())+8;
                dl->AddRectFilled(V(sxp,ry+9),V(sxp+sw2,ry+27),WithA(COL_GOLD,60),9); TextAt(dl,g_fSml,12,V(sxp+7,ry+11),COL_INK,sub.c_str()); }
            AmMeter(dl,V(tx,ry+33),V(right-10,ry+39),d.peak);
            float v=d.vol;
            if(AmHSlider(dl,io,V(tx,ry+48),V(right-10,ry+50),v,uid*100+k*4+g_amView*1000+1)) AmSetDeviceVolume(d.id,v,-1);
            char pc[8]; snprintf(pc,8,"%d",(int)std::round(d.vol*100)); TextAt(dl,g_fSml,12,V(right-4,ry+41),COL_INK2,pc);
            float bxx=x1-16;
            if(V2Btn(dl,io,V(bxx,ry+(RH-6)*0.5f),30,30,d.mute? (g_amView==0? "volume_off":"mic_off") : (g_amView==0? "volume_up":"mic"),d.mute,0x6C300+uid*64+k*4+g_amView,click)) AmSetDeviceVolume(d.id,0,d.mute? 0 : 1);
            if(!d.def && V2Btn(dl,io,V(bxx-38,ry+(RH-6)*0.5f),30,30,"star",false,0x6C301+uid*64+k*4+g_amView,click)) AmSetDefault(d.id,true);
            if(g_amView==0 && V2Btn(dl,io,V(bxx-76,ry+(RH-6)*0.5f),30,30,"call_merge",false,0x6C302+uid*64+k*4,click)){ AmSetDefault(d.id,true); AmClearRoutes(); }
            ry+=RH; k++;
        }
    } else if(g_amView==2){
        std::vector<AppVol> mix; { std::lock_guard<std::mutex> lk(g_mixerMtx); mix=g_mixer; }
        if(mix.empty()) TextAt(dl,g_fSml,14,V(x0,ry+10),COL_INK2,"Nothing is playing sound right now");
        static int menuFor=-1; static bool menuOpen=false; static float menuAnim=0; static ImVec2 menuA, menuB; static DWORD menuPid=0;
        bool toggled=false;
        int k=0;
        for(const AppVol& a:mix){
            rowBg(ry,uid*100+k+200);
            ImVec2 ic=V(x0+18,ry+(RH-6)*0.5f);
            if(a.icon) dl->AddImage((ImTextureID)a.icon,V(ic.x-14,ic.y-14),V(ic.x+14,ic.y+14));
            else MsIcon(dl,"apps",ic,22,COL_INK);
            float tx=ic.x+28, right=x1-190;
            TextAt(dl,g_fMed,15,V(tx,ry+8),COL_INK,Clip(g_fMed,15,a.name,right-tx).c_str());
            AmMeter(dl,V(tx,ry+33),V(right-10,ry+39),a.peak);
            float v=a.vol;
            if(AmHSlider(dl,io,V(tx,ry+48),V(right-10,ry+50),v,uid*100+k*4+3000)) SetAppVolume(a.pid,v,false,false);
            // output picker
            std::wstring cur; auto it=route.find(a.pid); if(it!=route.end()) cur=it->second;
            std::string lab="Default"; for(auto& d:outs) if(d.id==cur) lab=d.name;
            ImVec2 sa=V(right,ry+15); float sw3=x1-right-44;
            int hit=V2Split(dl,io,sa,sw3,28,"speaker",lab,0x6C400+uid*64+k*2,click);
            if(hit){ menuOpen = !(menuOpen && menuPid==a.pid); menuPid=a.pid; menuA=sa; menuB=V(sa.x+sw3,sa.y+28); toggled=true; }
            if(V2Btn(dl,io,V(x1-16,ry+(RH-6)*0.5f),30,30,a.mute? "volume_off" : "volume_up",a.mute,0x6C500+uid*64+k,click)) SetAppVolume(a.pid,0,true,!a.mute);
            ry+=RH; k++;
        }
        (void)menuFor;
        if(menuOpen||menuAnim>0.01f){
            std::vector<std::string> items={"Default output"}; for(auto& d:outs) items.push_back(d.name);
            std::wstring cur; auto it=route.find(menuPid); if(it!=route.end()) cur=it->second;
            int sel=0; for(size_t i=0;i<outs.size();i++) if(outs[i].id==cur) sel=(int)i+1;
            int p=V2Menu(dl,io,menuOpen,menuAnim,menuA,menuB,items,sel,menuA.y>(la.y+lb.y)*0.5f,toggled);
            if(p>=0) AmRouteApp(menuPid, p==0? std::wstring() : outs[p-1].id);
        }
        if(!AmPolicyFactory()) TextAt(dl,g_fSml,12,V(x0,lb.y-18),COL_INK2,"Per-app output needs Windows 10 1803 or newer");
    } else {
        // ---------------- Voicemeeter ----------------
        VmTick();
        if(!g_vm.loggedIn || g_vm.type==0){
            const char* m = g_vm.err.empty()? "Voicemeeter is not running" : g_vm.err.c_str();
            TextAt(dl,g_fMed,16,V(x0,ry+10),COL_INK,m);
            if(g_vm.api.dll){ ImVec2 ba=V(x0,ry+44); bool h=io.MousePos.x>=ba.x&&io.MousePos.x<ba.x+170&&io.MousePos.y>=ba.y&&io.MousePos.y<ba.y+34;
                dl->AddRectFilled(ba,V(ba.x+170,ba.y+34),h? Mix(COL_INK,COL_GOLD,0.3f) : Mix(COL_INK,COL_GOLD,0.2f),17);
                TextAt(dl,g_fMed,14,V(ba.x+22,ba.y+8),M3OnPrimary(),"Start Voicemeeter");
                if(h&&click && g_vm.api.RunVoicemeeter){ g_vm.api.RunVoicemeeter(3); g_vm.tried=false; g_vm.loggedIn=false; } }
        } else {
            float colW=std::max(92.0f,std::min(120.0f,(x1-x0)/(float)(g_vm.strips.size()+1)));
            static std::unordered_map<int,float> hs; float& hsc=hs[uid];
            const float busW=std::max(112.0f,(x1-x0)/(float)std::max(1,VmBusCount()));   // buses scroll sideways with the strips when narrow
            float totalW=std::max(g_vm.strips.size()*colW,VmBusCount()*busW);
            if(inList && io.MouseWheel!=0) hsc=std::clamp(hsc-io.MouseWheel*60,0.0f,std::max(0.0f,totalW-(x1-x0)));
            float top=ry, busH=86, bot=lb.y-busH-10;
            TextAt(dl,g_fSml,12,V(x0,top-2),COL_INK2,VmTypeName());
            top+=16;
            char p[64];
            for(size_t i=0;i<g_vm.strips.size();i++){
                VmStrip& st=g_vm.strips[i];
                float cx0=x0+i*colW-hsc, cxm=cx0+colW*0.5f;
                if(cx0>x1 || cx0+colW<x0) continue;
                dl->AddRectFilled(V(cx0+3,top),V(cx0+colW-3,bot),Mix(Mix(COL_CARD,COL_CARD2,0.45f),st.virt? M3Tertiary() : COL_CARD2,0.12f),14);
                std::string lab=st.label.empty()? (st.virt? "Virtual "+std::to_string(i-g_vm.hwIn+1) : "Input "+std::to_string(i+1)) : st.label;
                TextAt(dl,g_fMed,13,V(cxm-TextW(g_fMed,13,Clip(g_fMed,13,lab,colW-12).c_str())*0.5f,top+6),COL_INK,Clip(g_fMed,13,lab,colW-12).c_str());
                // fader + meter
                float fy0=top+30, fy1=bot-96;
                AmMeter(dl,V(cxm-20,fy0),V(cxm-14,fy1),st.level,true);
                float frac=std::clamp((st.gain+60.0f)/72.0f,0.0f,1.0f);
                float fx=cxm+4;
                dl->AddRectFilled(V(fx-2,fy0),V(fx+2,fy1),V2Track(),2);
                float ky=fy1-(fy1-fy0)*frac;
                static int drag=-1;
                bool kh=io.MousePos.x>=fx-16&&io.MousePos.x<fx+16&&io.MousePos.y>=fy0-6&&io.MousePos.y<fy1+6;
                if(kh&&click) drag=(int)i*10+uid;
                if(drag==(int)i*10+uid){ if(!io.MouseDown[0]) drag=-1; else { float nf=std::clamp((fy1-io.MousePos.y)/std::max(1.0f,fy1-fy0),0.0f,1.0f);
                    float g=nf*72.0f-60.0f; if(fabsf(g)<1.2f) g=0; snprintf(p,64,"Strip[%zu].Gain",i); VmSetF(p,g); st.gain=g; } }
                if(kh && io.MouseDoubleClicked[0]){ snprintf(p,64,"Strip[%zu].Gain",i); VmSetF(p,0); st.gain=0; }
                dl->AddRectFilled(V(fx-12,ky-6),V(fx+12,ky+6),kh? COL_INK : Mix(COL_INK,COL_GOLD,0.2f),4);
                char gv[16]; snprintf(gv,16,"%.1f dB",st.gain); TextAt(dl,g_fSml,11.5f,V(cxm-TextW(g_fSml,11.5f,gv)*0.5f,fy1+6),COL_INK2,gv);
                // mute
                if(V2Btn(dl,io,V(cxm,fy1+36),colW-24,24,st.mute? "volume_off" : "volume_up",st.mute,0x6C600+uid*64+(int)i,click,8)){
                    snprintf(p,64,"Strip[%zu].Mute",i); VmSetF(p,st.mute? 0.0f : 1.0f); st.mute=!st.mute; }
                // routing A1..B3
                int nb=VmBusCount(); int perRow=(nb+1)/2; float bw=(colW-16)/(float)perRow;
                for(int b2=0;b2<nb && b2<8;b2++){
                    int r=b2/perRow, c=b2%perRow;
                    ImVec2 ba=V(cx0+8+c*bw,fy1+54+r*22), bb=V(ba.x+bw-3,ba.y+19);
                    bool h=io.MousePos.x>=ba.x&&io.MousePos.x<bb.x&&io.MousePos.y>=ba.y&&io.MousePos.y<bb.y;
                    bool on=st.route[b2];
                    dl->AddRectFilled(ba,bb,on? Mix(COL_INK,COL_GOLD,0.25f) : (h? Mix(COL_CARD2,COL_INK2,0.35f) : Mix(COL_CARD2,COL_INK2,0.18f)),6);
                    const char* bn=VmBusName(b2); TextAt(dl,g_fSml,11,V((ba.x+bb.x)*0.5f-TextW(g_fSml,11,bn)*0.5f,ba.y+3),on? M3OnPrimary() : COL_INK,bn);
                    if(h&&click){ snprintf(p,64,"Strip[%zu].%s",i,bn); VmSetF(p,on? 0.0f : 1.0f); st.route[b2]=!on; }
                }
            }
            // buses along the bottom
            float by0=lb.y-busH, bwid=busW;
            for(int b2=0;b2<VmBusCount();b2++){
                VmBus& bu=g_vm.buses[b2]; float bx0=x0+b2*bwid-hsc;
                if(bx0>x1 || bx0+bwid<x0) continue;
                dl->AddRectFilled(V(bx0+3,by0),V(bx0+bwid-3,lb.y-4),Mix(Mix(COL_CARD,COL_CARD2,0.45f),COL_CARD2,0.3f),12);
                std::string lab=std::string(VmBusName(b2))+(bu.label.empty()? "" : "  "+bu.label);
                TextAt(dl,g_fMed,12.5f,V(bx0+10,by0+6),COL_INK,Clip(g_fMed,12.5f,lab,bwid-50).c_str());
                AmMeter(dl,V(bx0+10,by0+28),V(bx0+bwid-12,by0+33),bu.level);
                float v=std::clamp((bu.gain+60.0f)/72.0f,0.0f,1.0f);
                if(AmHSlider(dl,io,V(bx0+10,by0+46),V(bx0+bwid-44,by0+48),v,uid*100+b2+7000)){ snprintf(p,64,"Bus[%d].Gain",b2); float g=v*72.0f-60.0f; if(fabsf(g)<1.2f) g=0; VmSetF(p,g); bu.gain=g; }
                if(V2Btn(dl,io,V(bx0+bwid-24,by0+47),26,26,bu.mute? "volume_off" : "volume_up",bu.mute,0x6C700+uid*16+b2,click,7)){
                    snprintf(p,64,"Bus[%d].Mute",b2); VmSetF(p,bu.mute? 0.0f : 1.0f); bu.mute=!bu.mute; }
                char gv[16]; snprintf(gv,16,"%.1f dB",bu.gain); TextAt(dl,g_fSml,11,V(bx0+10,by0+60),COL_INK2,gv);
            }
            // actions in the header row area
            float ax=x1-120;
            if(V2Btn(dl,io,V(ax,top-10),28,24,"open_in_new",false,0x6C800+uid,click,8)) VmCommand("Command.Show=1;");
            if(V2Btn(dl,io,V(ax+34,top-10),28,24,"refresh",false,0x6C801+uid,click,8)) VmCommand("Command.Restart=1;");
        }
    }
    dl->PopClipRect();
    if(g_amView!=3){
        float content=(ry+sc)-y; float viewH=lb.y-la.y; sc=std::clamp(sc,0.0f,std::max(0.0f,content-viewH));
        // footer
        float fy=o.y+s.y-36;
        const char* hint = g_amView==2? "Pick an output per app \xC2\xB7 \xE2\x86\xBA resets every app to the default output"
                         : g_amView==0? "\xE2\x98\x85 make default \xC2\xB7 merge = send ALL audio to that output"
                         : "Inputs show their live level while this is open";
        TextAt(dl,g_fSml,12,V(x0,fy+8),COL_INK2,Clip(g_fSml,12,hint,x1-x0-170).c_str());
        ImVec2 ra=V(x1-160,fy+2); bool h=io.MousePos.x>=ra.x&&io.MousePos.x<x1&&io.MousePos.y>=ra.y&&io.MousePos.y<ra.y+28;
        dl->AddRectFilled(ra,V(x1,ra.y+28),h? Mix(COL_CARD2,COL_INK2,0.35f) : Mix(COL_CARD2,COL_INK2,0.18f),14);
        TextAt(dl,g_fSml,12.5f,V(ra.x+12,ra.y+6),COL_INK,"Restart audio service");
        if(h&&click) AetherShellExec(nullptr,L"runas",L"cmd.exe",L"/c net stop audiosrv /y & net start audiosrv",nullptr,SW_HIDE);
    }
}
