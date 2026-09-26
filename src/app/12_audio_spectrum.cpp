// Aether - the audio spectrum (WASAPI loopback + FFT) and dashboard cards.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =============================================================================================
// REAL AUDIO SPECTRUM — WASAPI loopback + FFT
// The bars used to be random numbers that merely looked busy. This taps the default render
// endpoint in LOOPBACK mode (the same mix you hear, whatever app is playing it), windows the
// samples, runs a radix-2 FFT and folds the bins into log-spaced bands, so the bars are the
// actual spectrum. If the device is busy in exclusive mode or the capture fails, g_audioOk goes
// false and the drawing falls back to the old animation instead of freezing at zero.
// =============================================================================================
static const int FFT_N   = 1024;          // ~21ms at 48kHz
static const int NBANDS   = 28;
static std::mutex g_bandMtx;
static float g_bandsRaw[NBANDS]={0};      // published by the audio thread
static std::atomic<bool>  g_audioOk{false};
static std::atomic<float> g_audioLevel{0.0f};   // overall loudness 0..1 (used for the pulse)
// Which endpoint the loopback actually opened, for -s audio_diag. On a machine with Voicemeeter
// there are seven render endpoints and "loopback started fine but everything is zero" means we
// tapped the wrong one - without the name that is indistinguishable from "the PC is silent".
static std::mutex  g_audioDevMtx;
static std::string g_audioDevName = "(not opened)";
static int         g_audioRate = 0, g_audioCh = 0;

// Which endpoint should the spectrum listen on?
//
// On an ordinary PC the default render endpoint is where every app mixes, so it is both the right
// answer and the only one. Put Voicemeeter (or any virtual cable) on the machine and that stops
// being true: this box has SEVEN active render endpoints, apps are spread across VAIO / AUX /
// VAIO3, and the default is merely one of them. Tapping it then gives a perfectly healthy capture
// of silence - loopback reports OK, every band reads 0.00, and the bars fall back to the fake
// animation, which is indistinguishable from working unless you go looking.
//
// So: an explicit override wins; otherwise ask each active endpoint what its peak meter says and
// listen to whichever one is actually making noise. Falls back to the default endpoint when the
// whole machine is quiet, which is also the right answer - that is where sound will appear.
static IMMDevice* VizPickEndpoint(IMMDeviceEnumerator* en,std::string& nameOut){
    if(!en) return nullptr;
    auto friendly=[](IMMDevice* d)->std::string{
        std::string out; IPropertyStore* ps=nullptr;
        if(SUCCEEDED(d->OpenPropertyStore(STGM_READ,&ps)) && ps){
            PROPERTYKEY k; k.fmtid=GUID{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}}; k.pid=14;
            PROPVARIANT v; PropVariantInit(&v);
            if(SUCCEEDED(ps->GetValue(k,&v)) && v.vt==VT_LPWSTR && v.pwszVal) out=W2U8(v.pwszVal);
            PropVariantClear(&v); ps->Release(); }
        return out; };
    std::string want=g_vizDevice;
    for(auto& c:want) c=(char)tolower((unsigned char)c);

    IMMDeviceCollection* col=nullptr;
    IMMDevice* best=nullptr; std::string bestName; float bestPeak=-1.0f;
    if(SUCCEEDED(en->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&col)) && col){
        UINT n=0; col->GetCount(&n);
        for(UINT i=0;i<n;i++){
            IMMDevice* d=nullptr;
            if(FAILED(col->Item(i,&d)) || !d) continue;
            std::string nm=friendly(d);
            if(!want.empty()){
                std::string low=nm; for(auto& c:low) c=(char)tolower((unsigned char)c);
                if(low.find(want)!=std::string::npos){            // an explicit choice ends the search
                    col->Release(); nameOut=nm; return d; }
                d->Release(); continue;
            }
            float peak=0.0f;
            IAudioMeterInformation* mi=nullptr;
            if(SUCCEEDED(d->Activate(__uuidof(IAudioMeterInformation),CLSCTX_ALL,nullptr,(void**)&mi)) && mi){
                mi->GetPeakValue(&peak); mi->Release(); }
            if(peak>bestPeak){ if(best) best->Release(); best=d; bestName=nm; bestPeak=peak; }
            else d->Release();
        }
        col->Release();
    }
    // something is audibly playing on that endpoint: follow it
    if(best && bestPeak>0.0005f){ nameOut=bestName; return best; }
    if(best) best->Release();
    IMMDevice* def=nullptr;
    if(SUCCEEDED(en->GetDefaultAudioEndpoint(eRender,eConsole,&def)) && def){ nameOut=friendly(def); return def; }
    return nullptr;
}

static void FFTRadix2(float* re,float* im,int n){
    for(int i=1,j=0;i<n;i++){                       // bit-reversal permutation
        int bit=n>>1;
        for(;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if(i<j){ std::swap(re[i],re[j]); std::swap(im[i],im[j]); }
    }
    for(int len=2;len<=n;len<<=1){
        float ang=-6.28318530718f/len;
        float wr=cosf(ang), wi=sinf(ang);
        for(int i=0;i<n;i+=len){
            float cr=1.0f, ci=0.0f;
            for(int k=0;k<len/2;k++){
                float ur=re[i+k],       ui=im[i+k];
                float vr=re[i+k+len/2]*cr - im[i+k+len/2]*ci;
                float vi=re[i+k+len/2]*ci + im[i+k+len/2]*cr;
                re[i+k]=ur+vr;         im[i+k]=ui+vi;
                re[i+k+len/2]=ur-vr;   im[i+k+len/2]=ui-vi;
                float ncr=cr*wr-ci*wi; ci=cr*wi+ci*wr; cr=ncr;
            }
        }
    }
}
static void AudioSpectrumThread(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::vector<float> ring(FFT_N,0.0f);
    size_t rpos=0; size_t filled=0;
    float win[FFT_N];
    for(int i=0;i<FFT_N;i++) win[i]=0.5f*(1.0f-cosf(6.28318530718f*i/(FFT_N-1)));   // Hann
    float smooth[NBANDS]={0}; float autoMax=0.02f;
    int quietFrames=0;                 // consecutive silent FFT frames on the current endpoint
    while(g_running){
        IMMDeviceEnumerator* en=nullptr; IMMDevice* dev=nullptr; std::string pickedName;
        IAudioClient* ac=nullptr; IAudioCaptureClient* cap=nullptr; WAVEFORMATEX* wf=nullptr;
        bool started=false;
        if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator),(void**)&en)) &&
           (dev=VizPickEndpoint(en,pickedName))!=nullptr &&
           SUCCEEDED(dev->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,(void**)&ac)) &&
           SUCCEEDED(ac->GetMixFormat(&wf)) &&
           SUCCEEDED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK,
                                    2000000,0,wf,nullptr)) &&
           SUCCEEDED(ac->GetService(__uuidof(IAudioCaptureClient),(void**)&cap)) &&
           SUCCEEDED(ac->Start())){
            started=true; g_audioOk=true;
            { std::lock_guard<std::mutex> lk(g_audioDevMtx); g_audioDevName=pickedName; }
        }
        int ch = wf? wf->nChannels : 2;
        int bits = wf? wf->wBitsPerSample : 32;
        bool isFloat = wf && (wf->wFormatTag==WAVE_FORMAT_IEEE_FLOAT ||
                              (wf->wFormatTag==WAVE_FORMAT_EXTENSIBLE &&
                               ((WAVEFORMATEXTENSIBLE*)wf)->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
        int rate = wf? (int)wf->nSamplesPerSec : 48000;
        g_audioRate=rate; g_audioCh=ch;
        while(started && g_running){
            UINT32 packet=0;
            if(FAILED(cap->GetNextPacketSize(&packet))) break;
            if(packet==0){ Sleep(8); continue; }
            while(packet>0){
                BYTE* data=nullptr; UINT32 frames=0; DWORD flags=0;
                if(FAILED(cap->GetBuffer(&data,&frames,&flags,nullptr,nullptr))) { started=false; break; }
                bool silent=(flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0;
                for(UINT32 f=0; f<frames; f++){
                    float mono=0.0f;
                    if(!silent && data){
                        if(isFloat && bits==32){ const float* fp=(const float*)(data+(size_t)f*ch*4);
                            for(int c=0;c<ch;c++) mono+=fp[c]; mono/=ch; }
                        else if(bits==16){ const int16_t* ip=(const int16_t*)(data+(size_t)f*ch*2);
                            for(int c=0;c<ch;c++) mono+=ip[c]/32768.0f; mono/=ch; }
                    }
                    ring[rpos]=mono; rpos=(rpos+1)%FFT_N; if(filled<FFT_N) filled++;
                }
                cap->ReleaseBuffer(frames);
                if(FAILED(cap->GetNextPacketSize(&packet))) { started=false; break; }
            }
            if(!started) break;
            if(filled>=FFT_N){
                static float re[FFT_N], im[FFT_N];
                float rms=0.0f;
                for(int i=0;i<FFT_N;i++){
                    float v=ring[(rpos+i)%FFT_N];
                    rms+=v*v;
                    re[i]=v*win[i]; im[i]=0.0f;
                }
                g_audioLevel = std::min(1.0f, sqrtf(rms/FFT_N)*4.0f);
                // This endpoint has gone quiet. It may simply be paused - but it may also be the
                // WRONG endpoint, which on a Voicemeeter box looks exactly the same. Let go after a
                // few seconds so the picker gets another look at what is really playing; if this one
                // is still the loudest it is chosen straight back.
                if(g_audioLevel.load() > 0.004f) quietFrames=0; else quietFrames++;
                if(quietFrames > 220){ quietFrames=0; started=false; break; }
                FFTRadix2(re,im,FFT_N);
                // log-spaced bands from 45Hz to 15kHz
                float binHz=(float)rate/FFT_N;
                float lo=45.0f, hi=std::min(15000.0f,rate*0.45f);
                float bands[NBANDS];
                float frameMax=0.0f;
                for(int b=0;b<NBANDS;b++){
                    float f0=lo*powf(hi/lo,(float)b/NBANDS);
                    float f1=lo*powf(hi/lo,(float)(b+1)/NBANDS);
                    int i0=std::max(1,(int)(f0/binHz)), i1=std::min(FFT_N/2-1,(int)(f1/binHz));
                    if(i1<i0) i1=i0;
                    float peak=0.0f;
                    for(int i=i0;i<=i1;i++){
                        float m=sqrtf(re[i]*re[i]+im[i]*im[i]);
                        if(m>peak) peak=m;
                    }
                    peak *= 1.0f + 1.6f*((float)b/NBANDS);      // tilt: lift the quiet highs
                    bands[b]=peak;
                    if(peak>frameMax) frameMax=peak;
                }
                // adaptive normalisation so quiet tracks still fill the bars
                autoMax = std::max(frameMax, autoMax*0.995f);
                if(autoMax<0.02f) autoMax=0.02f;
                {
                    std::lock_guard<std::mutex> lk(g_bandMtx);
                    for(int b=0;b<NBANDS;b++){
                        float v=std::clamp(bands[b]/autoMax,0.0f,1.0f);
                        v=powf(v,0.62f);                         // perceptual curve
                        float target=v;
                        // fast attack, slow release reads as "musical"
                        smooth[b] += (target-smooth[b]) * (target>smooth[b]? 0.55f : 0.14f);
                        g_bandsRaw[b]=smooth[b];
                    }
                }
            }
        }
        g_audioOk=false;
        if(cap) cap->Release();
        if(ac){ ac->Stop(); ac->Release(); }
        if(wf) CoTaskMemFree(wf);
        if(dev) dev->Release();
        if(en) en->Release();
        if(g_running) Sleep(900);        // device switched or capture died: try again
    }
}
// copy the published bands out for drawing (n bars resampled from NBANDS)
static void AudioBands(float* out,int n,bool playing){
    static float fake[64]={0}, fakeT[64]={0}; static double clk=0;
    if(g_audioOk.load()){
        std::lock_guard<std::mutex> lk(g_bandMtx);
        for(int i=0;i<n;i++){
            float t=(float)i/std::max(1,n-1)*(NBANDS-1);
            int a=(int)t; int b=std::min(NBANDS-1,a+1); float fr=t-a;
            out[i]=g_bandsRaw[a]*(1-fr)+g_bandsRaw[b]*fr;
        }
        return;
    }
    // no loopback available: keep the old animation rather than a dead row of bars
    clk+=g_frameDt;
    if(clk>(playing?0.075:0.35)){ clk=0;
        for(int i=0;i<n&&i<64;i++){ float mid=1.0f-fabsf((i-(n-1)/2.0f)/((n-1)/2.0f));
            fakeT[i]= playing? (0.14f+(rand()%1000)/1000.0f*0.86f)*(0.38f+0.62f*mid) : 0.04f; } }
    for(int i=0;i<n&&i<64;i++){ fake[i]+=(fakeT[i]-fake[i])*std::min(1.0f,g_frameDt*(playing?16.0f:8.0f)); out[i]=fake[i]; }
}
// the animated spectrum shared by the media toast and the media tab
static void Spectrum(ImDrawList* dl,float x0,float x1,float base,float maxH,int n,float alpha,bool playing){
    n=std::min(n,64);
    float v[64]; AudioBands(v,n,playing);
    float slot=(x1-x0)/(float)n, bw=std::min(7.0f,slot*0.56f);
    for(int i=0;i<n;i++){ float h=std::max(2.5f,v[i]*maxH);
        float cx=x0+slot*i+slot*0.5f;
        ImU32 c=MulA(WithA(COL_GOLD,(int)(110+v[i]*145)),alpha);
        dl->AddRectFilled(V(cx-bw/2,base-h),V(cx+bw/2,base),c,bw*0.5f); }
}

static float g_toastStackH=0.0f;                 // how far down the toasts reach (notifications dodge it)

// ---- a user image (PNG / animated GIF) drawn in place of the drawn cat on the media card ----
static std::vector<ID3D11ShaderResourceView*> g_catFrames; static std::vector<int> g_catDelays;
static int g_catFrame=0; static double g_catClock=0; static int g_catW=0,g_catH=0;
static void LoadCatImage(const std::string& path){
    for(auto t:g_catFrames) if(t) t->Release();
    g_catFrames.clear(); g_catDelays.clear(); g_catFrame=0; g_catClock=0; g_catW=g_catH=0;
    if(path.empty()) return;
    IWICImagingFactory* fac=nullptr;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&fac)))) return;
    std::wstring wp=U82W(path); IWICBitmapDecoder* dec=nullptr;
    if(SUCCEEDED(fac->CreateDecoderFromFilename(wp.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec))){
        UINT n=0; dec->GetFrameCount(&n);
        for(UINT i=0;i<n && i<240;i++){
            IWICBitmapFrameDecode* fr=nullptr; if(FAILED(dec->GetFrame(i,&fr))) continue;
            IWICFormatConverter* conv=nullptr; fac->CreateFormatConverter(&conv);
            if(conv && SUCCEEDED(conv->Initialize(fr,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeMedianCut))){
                UINT w=0,h=0; conv->GetSize(&w,&h);
                if(w&&h){ std::vector<uint8_t> buf((size_t)w*h*4); conv->CopyPixels(nullptr,w*4,(UINT)buf.size(),buf.data());
                    ID3D11ShaderResourceView* tex=MakeTextureBGRA(buf.data(),w,h);
                    if(tex){ g_catFrames.push_back(tex); g_catW=(int)w; g_catH=(int)h; }
                    int delay=100; IWICMetadataQueryReader* mq=nullptr;
                    if(SUCCEEDED(fr->GetMetadataQueryReader(&mq))){ PROPVARIANT v; PropVariantInit(&v);
                        if(SUCCEEDED(mq->GetMetadataByName(L"/grctlext/Delay",&v)) && v.vt==VT_UI2){ delay=v.uiVal*10; if(delay<20) delay=100; }
                        PropVariantClear(&v); mq->Release(); }
                    g_catDelays.push_back(delay);
                }
            }
            if(conv) conv->Release(); fr->Release();
        }
        dec->Release();
    }
    fac->Release();
}
// draws the image centred in a box, keeping its aspect. Frames advance only while music plays,
// so a looping GIF behaves like the old drawn cat did.
static bool DrawMediaImage(ImDrawList* dl,ImVec2 c,float boxW,float boxH,bool playing,int alpha=255){
    if(g_catFrames.empty()||g_catW<=0||g_catH<=0) return false;
    if(playing && g_catFrames.size()>1){
        g_catClock += g_frameDt*1000.0;
        int guard=0;
        while(g_catFrame<(int)g_catDelays.size() && g_catClock>=g_catDelays[g_catFrame] && guard++<240){
            g_catClock-=g_catDelays[g_catFrame];
            g_catFrame=(g_catFrame+1)%(int)g_catFrames.size();
        }
    }
    float sc=std::min(boxW/(float)g_catW, boxH/(float)g_catH);
    float w=g_catW*sc, h=g_catH*sc;
    int idx=std::min(g_catFrame,(int)g_catFrames.size()-1);
    dl->AddImage((ImTextureID)g_catFrames[idx],V(c.x-w/2,c.y-h/2),V(c.x+w/2,c.y+h/2),
                 ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,alpha));
    return true;
}

static void BongoCat(ImDrawList* dl, ImVec2 c, float s, bool playing){
    // draw the REAL Caelestia bongocat.gif (assets/bongocat.gif) via the existing media-image loader;
    // only auto-loads it when no custom cat image is already set. Procedural cat is the fallback below.
    static bool bongoTried=false;
    if(!bongoTried && g_catFrames.empty()){ bongoTried=true; LoadCatImage((ExeDir()+"assets\\bongocat.gif").c_str()); }
    if(DrawMediaImage(dl,c,120.0f*s,84.0f*s,playing)) return;
    ImU32 line=IM_COL32(58,72,69,255), fill=IM_COL32(252,254,253,255), blush=IM_COL32(238,150,160,255);
    float t=(float)GetTickCount64()*0.008f;
    // body
    dl->AddRectFilled(V(c.x-22*s,c.y-10*s),V(c.x+26*s,c.y+14*s),fill,12*s);
    dl->AddRect      (V(c.x-22*s,c.y-10*s),V(c.x+26*s,c.y+14*s),line,12*s,0,2.0f*s);
    // ears
    dl->AddTriangleFilled(V(c.x-20*s,c.y-9*s),V(c.x-10*s,c.y-9*s),V(c.x-16*s,c.y-20*s),fill);
    dl->AddTriangle      (V(c.x-20*s,c.y-9*s),V(c.x-10*s,c.y-9*s),V(c.x-16*s,c.y-20*s),line,2.0f*s);
    dl->AddTriangleFilled(V(c.x+6*s,c.y-9*s),V(c.x+16*s,c.y-9*s),V(c.x+11*s,c.y-20*s),fill);
    dl->AddTriangle      (V(c.x+6*s,c.y-9*s),V(c.x+16*s,c.y-9*s),V(c.x+11*s,c.y-20*s),line,2.0f*s);
    // face
    dl->AddCircleFilled(V(c.x-8*s,c.y-1*s),2.2f*s,line);
    dl->AddCircleFilled(V(c.x+8*s,c.y-1*s),2.2f*s,line);
    dl->AddCircleFilled(V(c.x-15*s,c.y+3*s),3.0f*s,blush);
    dl->AddCircleFilled(V(c.x+16*s,c.y+3*s),3.0f*s,blush);
    // paws: alternate up/down while playing, both down when idle
    float p1 = playing ? (sinf(t)>0?-6.0f:0.0f) : 0.0f;
    float p2 = playing ? (sinf(t)>0? 0.0f:-6.0f) : 0.0f;
    dl->AddCircleFilled(V(c.x-14*s,c.y+(16+p1)*s),5.0f*s,fill);
    dl->AddCircle      (V(c.x-14*s,c.y+(16+p1)*s),5.0f*s,line,0,2.0f*s);
    dl->AddCircleFilled(V(c.x+16*s,c.y+(16+p2)*s),5.0f*s,fill);
    dl->AddCircle      (V(c.x+16*s,c.y+(16+p2)*s),5.0f*s,line,0,2.0f*s);
}

static int g_calMonthOff=0;   // months away from current month (wheel/chevrons navigate; middle-click resets)
static int   g_calYearView=0;         // 0 = month grid, 1 = year (12-month) grid
static float g_calAnim=1.0f;          // month-change slide (1 = settled)
static int   g_calDir=0;              // slide direction of the last change
static void CalGo(int deltaMonths){ g_calMonthOff+=deltaMonths; g_calAnim=0.0f; g_calDir=(deltaMonths>=0?1:-1); }
static bool CalMarked(int ymd){ for(int m:g_calMarks) if(m==ymd) return true; return false; }
// A reusable, INTERACTIVE calendar drawn inside [a,b]. Wheel / chevrons change month (animated slide),
// click the title for a year grid, click a day to mark it (dot). Spill days from adjacent months are faded.
static void DrawCalendar(ImDrawList* dl, ImVec2 a, ImVec2 b, ImGuiIO& io){
    bool click=io.MouseClicked[0];
    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
    int tyr=lt.tm_year+1900, tmo=lt.tm_mon, tday=lt.tm_mday;
    if(g_calAnim<1.0f) g_calAnim=std::min(1.0f,g_calAnim+g_frameDt*5.5f);
    float aw=b.x-a.x, ah=b.y-a.y, ix=a.x+18, iy=a.y+14, iw=aw-36, ih=ah-26;
    bool over=io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;
    int dy=tyr, dmo=tmo+g_calMonthOff; while(dmo<0){dmo+=12;dy--;} while(dmo>11){dmo-=12;dy++;}
    const char* mnF[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
    const char* mn3[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    // ---- header: title (click => year view) + chevrons ----
    char hdr[32]; if(g_calYearView) snprintf(hdr,32,"%d",dy); else snprintf(hdr,32,"%s %d",mnF[dmo],dy);
    float hw=TextW(g_fBig,21,hdr);
    bool thov=io.MousePos.x>ix&&io.MousePos.x<ix+hw+8&&io.MousePos.y>iy-2&&io.MousePos.y<iy+26;
    TextAt(dl,g_fBig,21,V(ix,iy),thov?COL_GOLD:COL_INK,hdr);
    if(click&&thov) g_calYearView=!g_calYearView;
    { ImVec2 lc=V(b.x-52,iy+11),rc=V(b.x-24,iy+11);
      bool lh=fabsf(io.MousePos.x-lc.x)<13&&fabsf(io.MousePos.y-lc.y)<13, rh=fabsf(io.MousePos.x-rc.x)<13&&fabsf(io.MousePos.y-rc.y)<13;
      dl->AddLine(V(lc.x+3,lc.y-5),V(lc.x-3,lc.y),lh?COL_GOLD:COL_INK2,2);dl->AddLine(V(lc.x-3,lc.y),V(lc.x+3,lc.y+5),lh?COL_GOLD:COL_INK2,2);
      dl->AddLine(V(rc.x-3,rc.y-5),V(rc.x+3,rc.y),rh?COL_GOLD:COL_INK2,2);dl->AddLine(V(rc.x+3,rc.y),V(rc.x-3,rc.y+5),rh?COL_GOLD:COL_INK2,2);
      int step=g_calYearView?12:1;
      if(over){ if(io.MouseWheel>0.1f)CalGo(-step); else if(io.MouseWheel<-0.1f)CalGo(step); if(io.MouseClicked[2]){g_calMonthOff=0;g_calAnim=0;g_calDir=0;} }
      if(click&&lh)CalGo(-step); if(click&&rh)CalGo(step); }
    float bodyY=iy+40, bodyH=ih-40; float slide=(1.0f-EaseOutCubic(g_calAnim))*g_calDir*iw*0.35f;
    int al=(int)(EaseOutCubic(g_calAnim)*255);
    dl->PushClipRect(V(a.x+4,bodyY-4),V(b.x-4,b.y-4),true);
    if(g_calYearView){
        // 3x4 grid of months; click one to jump to it
        float cw=iw/4, chh=bodyH/3;
        for(int m=0;m<12;m++){ int r=m/4,cc=m%4; float cxm=ix+cw*cc+cw/2, cym=bodyY+chh*r+chh/2;
            bool isCur=(dy==tyr&&m==tmo); bool mh=io.MousePos.x>ix+cw*cc&&io.MousePos.x<ix+cw*cc+cw&&io.MousePos.y>bodyY+chh*r&&io.MousePos.y<bodyY+chh*(r+1);
            if(isCur) dl->AddRectFilled(V(cxm-cw*0.4f,cym-16),V(cxm+cw*0.4f,cym+16),AccA(200),12);
            else if(mh) dl->AddRectFilled(V(cxm-cw*0.4f,cym-16),V(cxm+cw*0.4f,cym+16),WithA(COL_INK2,40),12);
            TextAt(dl,g_fMed,16,V(cxm-TextW(g_fMed,16,mn3[m])/2,cym-9),WithA(isCur?IM_COL32(250,254,252,255):COL_INK,al),mn3[m]);
            if(click&&mh){ g_calMonthOff=(dy-tyr)*12+(m-tmo); g_calYearView=0; g_calAnim=0; g_calDir=(m>=tmo?1:-1); } }
        dl->PopClipRect(); return;
    }
    // ---- month grid (weekday header + 6x7 cells, spill days faded, marks, click to mark) ----
    const char* wd[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    float cellW=iw/7, headH=20; float ox=slide;
    for(int i=0;i<7;i++){ float x=ix+cellW*i+cellW/2; TextAt(dl,g_fSml,12,V(x-TextW(g_fSml,12,wd[i])/2,bodyY),WithA(COL_INK2,al),wd[i]); }
    struct tm first={}; first.tm_year=dy-1900; first.tm_mon=dmo; first.tm_mday=1; first.tm_hour=12;
    time_t ft=mktime(&first); struct tm f2; localtime_s(&f2,&ft);
    int startW=f2.tm_wday, dim=DaysInMonth(dy,dmo);
    int pmo=dmo-1,pyr=dy; if(pmo<0){pmo=11;pyr--;} int pdim=DaysInMonth(pyr,pmo);
    float rowsY=bodyY+headH+6, cellH=(bodyH-headH-6)/6;
    int cellYmd[42]; bool cellIn[42];
    int selA=std::min(g_calDragI0,g_calDragI1), selB=std::max(g_calDragI0,g_calDragI1);
    for(int i=0;i<42;i++){ int r=i/7,cc=i%7; float ccx=ix+cellW*cc+cellW/2+ox, ccy=rowsY+cellH*r+cellH/2;
        int dnum=i-startW+1; bool inMonth=(dnum>=1&&dnum<=dim); int showDay; int cyr=dy,cmo=dmo;
        if(dnum<1){ showDay=pdim+dnum; cmo=pmo; cyr=pyr; }
        else if(dnum>dim){ showDay=dnum-dim; cmo=dmo+1; cyr=dy; if(cmo>11){cmo=0;cyr++;} }
        else showDay=dnum;
        bool today=(inMonth && cyr==tyr&&cmo==tmo&&showDay==tday);
        int ymd=cyr*10000+(cmo+1)*100+showDay; bool marked=CalMarked(ymd);
        cellYmd[i]=ymd; cellIn[i]=inMonth;
        bool ch=io.MousePos.x>ix+cellW*cc&&io.MousePos.x<ix+cellW*(cc+1)&&io.MousePos.y>rowsY+cellH*r&&io.MousePos.y<rowsY+cellH*(r+1);
        // start / extend a range drag
        if(io.MouseClicked[0]&&ch&&inMonth&&!g_calNoteDay){
            g_calDragging=true; g_calDragMoved=false; g_calDragI0=i; g_calDragI1=i; }
        else if(g_calDragging&&io.MouseDown[0]&&ch&&inMonth&&i!=g_calDragI1){
            g_calDragI1=i; g_calDragMoved=true;
            selA=std::min(g_calDragI0,g_calDragI1); selB=std::max(g_calDragI0,g_calDragI1); }
        bool inSel = g_calDragging&&g_calDragMoved&&inMonth&&i>=selA&&i<=selB;
        if(inSel){   // the run being blocked out reads as one continuous band
            float pad=(i==selA)?4.0f:0.0f, pad2=(i==selB)?4.0f:0.0f;
            dl->AddRectFilled(V(ix+cellW*cc+pad,ccy-15),V(ix+cellW*(cc+1)-pad2,ccy+15),
                              WithA(COL_GOLD,(int)(52*al/255.0f)),8); }
        if(today) dl->AddCircleFilled(V(ccx,ccy),14,MulA(COL_GOLD,al/255.0f));
        else if(ch&&inMonth) dl->AddCircleFilled(V(ccx,ccy),14,WithA(COL_INK2,(int)(40*al/255.0f)));
        char ds[4]; snprintf(ds,4,"%d",showDay);
        ImU32 col = today?IM_COL32(250,254,252,255):(inMonth?COL_INK:WithA(COL_INK2,120));
        TextAt(dl,g_fSml,14,V(ccx-TextW(g_fSml,14,ds)/2,ccy-9),WithA(col,al),ds);
        if(marked) dl->AddCircleFilled(V(ccx,ccy+11),2.4f,WithA(today?IM_COL32(250,254,252,255):COL_GOLD,al));
        // a day carrying a note gets an underline, so notes and marks stay tellable apart
        if(inMonth&&!CalNote(ymd).empty())
            dl->AddRectFilled(V(ccx-6,ccy-13),V(ccx+6,ccy-11.6f),
                              WithA(today?IM_COL32(250,254,252,255):COL_INK,al),1.0f);
    }
    // ---- release: a DRAG marks the whole run, a plain CLICK opens that day's note ----
    if(g_calDragging && !io.MouseDown[0]){
        if(g_calDragMoved){
            int a2=std::min(g_calDragI0,g_calDragI1), b2=std::max(g_calDragI0,g_calDragI1);
            // if every day in the run is already marked the gesture clears it, so the same drag
            // both blocks out and un-blocks - otherwise re-dragging a range would be a no-op
            bool allMarked=true;
            for(int i=a2;i<=b2;i++) if(cellIn[i]&&!CalMarked(cellYmd[i])) { allMarked=false; break; }
            for(int i=a2;i<=b2;i++){
                if(!cellIn[i]) continue; int d=cellYmd[i]; bool has=CalMarked(d);
                if(allMarked && has){ for(size_t k=0;k<g_calMarks.size();k++) if(g_calMarks[k]==d){ g_calMarks.erase(g_calMarks.begin()+k); break; } }
                else if(!allMarked && !has) g_calMarks.push_back(d); }
            SaveConfig();
        } else if(g_calDragI0>=0 && cellIn[g_calDragI0]){
            g_calNoteDay=cellYmd[g_calDragI0]; g_calNoteAnim=0.0f;
            std::string ex=CalNote(g_calNoteDay);
            strncpy_s(g_calNoteBuf,sizeof(g_calNoteBuf),ex.c_str(),_TRUNCATE);
        }
        g_calDragging=false; g_calDragMoved=false; g_calDragI0=g_calDragI1=-1;
    }
    dl->PopClipRect();

    // ---- note editor: springs open over the calendar for the clicked day ----
    if(g_calNoteDay||g_calNoteAnim>0.004f){
        float want=g_calNoteDay?1.0f:0.0f;
        g_calNoteAnim += (want-g_calNoteAnim)*std::min(1.0f,g_frameDt*15.0f);
        if(fabsf(want-g_calNoteAnim)<0.004f) g_calNoteAnim=want;
        float na=EaseOutCubic(std::clamp(g_calNoteAnim,0.0f,1.0f));
        if(na>0.004f){
            float pw=std::min(aw-28.0f,300.0f), ph=118.0f;
            float sc=0.90f+0.10f*na;                       // springs up out of the card
            ImVec2 pc=V((a.x+b.x)*0.5f,(a.y+b.y)*0.5f);
            ImVec2 p0=V(pc.x-pw*0.5f*sc,pc.y-ph*0.5f*sc), p1=V(pc.x+pw*0.5f*sc,pc.y+ph*0.5f*sc);
            int pal=(int)(na*255);
            dl->AddRectFilled(a,b,PanelCol((int)(na*150)),16);          // dim the grid behind
            dl->AddRectFilled(p0,p1,g_darkUI?IM_COL32(24,24,30,(int)(246*na)):IM_COL32(250,250,253,(int)(248*na)),14);
            dl->AddRect(p0,p1,WithA(COL_GOLD,(int)(120*na)),14,0,1.4f);
            int yy=g_calNoteDay/10000, mm2=(g_calNoteDay/100)%100, dd=g_calNoteDay%100;
            static const char* mn3b[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
            char ttl[48]; snprintf(ttl,48,"%s %d, %d",(mm2>=1&&mm2<=12)?mn3b[mm2-1]:"?",dd,yy);
            TextAt(dl,g_fMed,16,V(p0.x+14,p0.y+10),WithA(COL_INK,pal),ttl);
            // text field
            ImVec2 f0=V(p0.x+14,p0.y+38), f1=V(p1.x-14,p0.y+70);
            dl->AddRectFilled(f0,f1,WithA(COL_INK2,(int)(46*na)),8);
            dl->AddRect(f0,f1,WithA(COL_GOLD,(int)(160*na)),8,0,1.3f);
            if(g_calNoteDay){
                size_t len=strlen(g_calNoteBuf);
                for(int k=0;k<io.InputQueueCharacters.Size;k++){ ImWchar c2=io.InputQueueCharacters[k];
                    if(c2>=32&&c2<127&&len<sizeof(g_calNoteBuf)-1){ g_calNoteBuf[len++]=(char)c2; g_calNoteBuf[len]=0; } }
                if(ImGui::IsKeyPressed(ImGuiKey_Backspace)&&len>0) g_calNoteBuf[--len]=0;
                if(ImGui::IsKeyPressed(ImGuiKey_Enter)){
                    if(g_calNoteBuf[0]) g_calNotes[g_calNoteDay]=g_calNoteBuf;
                    else g_calNotes.erase(g_calNoteDay);
                    SaveConfig(); g_calNoteDay=0; }
                if(ImGui::IsKeyPressed(ImGuiKey_Escape)) g_calNoteDay=0;
            }
            std::string shownTxt=Clip(g_fSml,14,g_calNoteBuf,(f1.x-f0.x)-18);
            if(shownTxt.empty()) TextAt(dl,g_fSml,14,V(f0.x+9,f0.y+8),WithA(COL_INK2,(int)(pal*0.7f)),"Type a note\xE2\x80\xA6");
            else TextAt(dl,g_fSml,14,V(f0.x+9,f0.y+8),WithA(COL_INK,pal),shownTxt.c_str());
            if(g_calNoteDay&&((GetTickCount64()/500)&1)==0){
                float cxx=f0.x+9+TextW(g_fSml,14,shownTxt.c_str());
                dl->AddRectFilled(V(cxx,f0.y+7),V(cxx+1.6f,f1.y-7),WithA(COL_INK,pal)); }
            TextAt(dl,g_fSml,11,V(p0.x+14,p1.y-22),WithA(COL_INK2,(int)(pal*0.85f)),
                   "Enter saves \xE2\x80\xA2 Esc closes \xE2\x80\xA2 drag days to block out");
            // click outside closes without saving
            if(io.MouseClicked[0] && !(io.MousePos.x>p0.x&&io.MousePos.x<p1.x&&io.MousePos.y>p0.y&&io.MousePos.y<p1.y))
                g_calNoteDay=0;
        }
    }
}
// The M3 shape library and the wavy progress arc are defined below the dashboard (they need the
// radius tables), but the media card up here draws with them.
static void M3Shape(ImDrawList* dl, ImVec2 c, float R, ImU32 col, int shape, float spin);
// The visualiser ring from the screen recording: bars all the way round, starting at a fixed gap
// outside the cover, with ROUNDED CAPS. ImGui butt-ends every line, and at this thickness a flat
// end is the difference between "drawn" and "clipped" - the caps are what make it read as the
// reference does. Bars fall to nothing when nothing is playing, which is why a paused card in the
// recording is just a bare circle.
static void MediaBars(ImDrawList* dl, ImVec2 c, float R, ImU32 col, bool playing){
    const int NR=56; float bands[64]; AudioBands(bands,NR,playing);
    const float gap=R*0.13f, th=std::max(2.0f,R*0.055f);
    for(int i=0;i<NR;i++){
        float ang=i*(6.2831853f/NR)-1.5707963f;
        float len=bands[i]*R*0.62f;
        if(len<1.0f) continue;                      // silent bands simply are not drawn
        float r0=R+gap, r1=r0+len;
        ImVec2 p0(c.x+cosf(ang)*r0, c.y+sinf(ang)*r0);
        ImVec2 p1(c.x+cosf(ang)*r1, c.y+sinf(ang)*r1);
        ImU32 bc=WithA(col,(int)(120+bands[i]*135));
        dl->AddLine(p0,p1,bc,th);
        dl->AddCircleFilled(p0,th*0.5f,bc,8);
        dl->AddCircleFilled(p1,th*0.5f,bc,8);
    }
}
static void M3ShapeImage(ImDrawList* dl, ImVec2 c, float R, ID3D11ShaderResourceView* tex,
                         int imgW, int imgH, int shape, float spin);
static void WavyArc(ImDrawList* dl, ImVec2 c, float R, float frac,
                    ImU32 active, ImU32 track, float th, float phase);
static void WUser(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,int uid,bool drawCard);   // fwd (Profile.h)
static void DrawAudioMixer(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,int uid,bool card);   // fwd (AudioMixer.h)
static void DrawDashboard(ImDrawList* dl, ImVec2 org, ImVec2 area){
    if(g_homeLayout==1){ DrawDashboardV2(dl,org,area); return; }
    ImGuiIO& io=ImGui::GetIO();
    bool click=io.MouseClicked[0];
    time_t now=time(nullptr); struct tm lt; localtime_s(&lt,&now);
    bool dk=g_darkUI;
    float anim=EaseOutCubic(std::clamp(g_drawerContent,0.0f,1.0f));
    float T=(float)ImGui::GetTime(); float pulse=0.5f+0.5f*sinf(T*2.2f);
    // staggered entrance: each card index slides up a touch as the drawer settles
    auto rise=[&](int i){ return (1.0f-EaseOutCubic(std::clamp((g_drawerContent-i*0.05f)/0.6f,0.0f,1.0f)))*22.0f; };
    // modern rounded card with a soft shadow + hairline (Caelestia surface feel)
    auto card=[&](ImVec2 a,ImVec2 b,float rnd){
        if(g_panelShadow) for(int i=5;i>0;i--){ float e=i*2.0f; dl->AddRect(V(a.x-e,a.y-e+2),V(b.x+e,b.y+e+2),IM_COL32(0,0,0,8),rnd+e,0,e*0.8f); }
        dl->AddRectFilled(a,b,COL_CARD,rnd);
        if(g_panelOutline) dl->AddRect(a,b,dk?IM_COL32(255,255,255,14):IM_COL32(0,0,0,12),rnd,0,1.0f);
    };
    // Cards that stand for a tab are now buttons. They get a hover lift + accent rim so it is
    // discoverable that they go somewhere, and each carries its own transition style.
    auto navCard=[&](ImVec2 a,ImVec2 b,int tab,int fx,int hid,bool allow)->float{
        bool hov = allow && io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;
        float ha = HoverAnim(hid,hov);
        if(ha>0.01f){
            dl->AddRect(V(a.x-1,a.y-1),V(b.x+1,b.y+1),WithA(COL_GOLD,(int)(ha*150)),18.0f,0,1.6f);
            dl->AddRectFilled(a,b,WithA(COL_GOLD,(int)(ha*14)),18.0f);
        }
        if(hov && click) GoTab(tab,fx,V((a.x+b.x)*0.5f,(a.y+b.y)*0.5f));
        return ha;
    };
    float rnd=18, gap=14;
    static std::string user;
    if(user.empty()){ if(!g_profileName.empty()) user=g_profileName;
        else { wchar_t u[UNLEN+1]; DWORD n=UNLEN+1; if(GetUserNameW(u,&n))user=W2U8(u); } }
    // 4 zones like the reference: [Weather / Clock] [Profile / Calendar] [rings] [Media]
    float c1=area.x*0.185f, c2=area.x*0.35f, c3=area.x*0.11f, c4=area.x-c1-c2-c3-3*gap;
    float x1=org.x, x2=x1+c1+gap, x3=x2+c2+gap, x4=x3+c3+gap;
    ImU32 tint=g_mdTint.load(); bool hasM=g_md.has&&!g_md.title.empty(); if(!tint||!hasM)tint=COL_GOLD;

    // ===== COL 1: weather (top) + big vertical clock (bottom) =====
    { float wH=area.y*0.38f, r=rise(0);
      ImVec2 a=V(x1,org.y+r),b=V(x1+c1,org.y+wH+r); card(a,b,rnd);
      float mx=(a.x+b.x)*0.5f;
      if(g_wx.ok){
          // city, so the card says WHERE this is for
          if(!g_wx.city.empty()){ std::string cy=Clip(g_fSml,12,g_wx.city,c1-16);
              TextAt(dl,g_fSml,12,V(mx-TextW(g_fSml,12,cy.c_str())/2,a.y+9),WithA(COL_INK2,205),cy.c_str()); }
          WxIcon(dl,V(mx,a.y+wH*0.30f),24,g_wx.code);
          // the temperature COUNTS to its value instead of snapping - same animator the rest of the
          // shell uses, so it shares the house easing rather than inventing a second one
          float shown=Cael::anim(9401,(float)g_wx.temp,Cael::DUR_DEFAULT_SPATIAL,Cael::DEFAULT_SPATIAL);
          char t[16]; snprintf(t,16,"%d\xC2\xB0",(int)lroundf(shown));
          TextAt(dl,g_fBig,28,V(mx-TextW(g_fBig,28,t)/2,a.y+wH*0.44f),COL_INK,t);
          { std::string cd=Clip(g_fSml,13,WxText(g_wx.code),c1-14);
            TextAt(dl,g_fSml,13,V(mx-TextW(g_fSml,13,cd.c_str())/2,a.y+wH*0.60f),COL_INK2,cd.c_str()); }
          // ---- 24h temperature sparkline, drawn on as the dashboard settles ----
          if(g_wx.hourly.size()>3){
              float gx0=a.x+12, gx1=b.x-12, gy1=b.y-9, gh=wH*0.21f, gy0=gy1-gh;
              double lo=g_wx.hourly[0], hi=g_wx.hourly[0];
              for(double v:g_wx.hourly){ lo=std::min(lo,v); hi=std::max(hi,v); }
              if(hi-lo<0.5) hi=lo+0.5;
              int n=(int)g_wx.hourly.size();
              float grow=EaseOutCubic(std::clamp((g_drawerContent-0.15f)/0.85f,0.0f,1.0f));
              int shownN=std::max(2,(int)ceilf(n*grow));
              auto px=[&](int i){ return gx0+(gx1-gx0)*(n>1?(float)i/(n-1):0.0f); };
              auto py=[&](int i){ return gy1-(float)((g_wx.hourly[i]-lo)/(hi-lo))*gh; };
              // soft fill under the curve, then the curve itself
              for(int i=0;i+1<shownN;i++)
                  dl->AddQuadFilled(V(px(i),py(i)),V(px(i+1),py(i+1)),
                                    V(px(i+1),gy1),V(px(i),gy1), WithA(COL_GOLD,26));
              for(int i=0;i+1<shownN;i++)
                  dl->AddLine(V(px(i),py(i)),V(px(i+1),py(i+1)),WithA(COL_GOLD,225),1.9f);
              if(shownN>1){ int h=shownN-1;                       // travelling head dot
                  dl->AddCircleFilled(V(px(h),py(h)),2.6f+pulse*0.9f,COL_GOLD); }
              dl->AddLine(V(gx0,gy1),V(gx1,gy1),WithA(COL_INK2,44),1.0f);
          } }
      else TextAt(dl,g_fSml,14,V(mx-TextW(g_fSml,14,"Weather\xE2\x80\xA6")/2,a.y+wH*0.5f),COL_INK2,"Weather\xE2\x80\xA6");
      navCard(a,b,3,TT_ZOOM,9310,true);
      float r2=rise(1); ImVec2 ca=V(x1,org.y+wH+gap+r2),cb=V(x1+c1,org.y+area.y+r2); card(ca,cb,rnd);
      int h12=g_clock24?lt.tm_hour:((lt.tm_hour%12)==0?12:lt.tm_hour%12);
      char hh[8],mm[8]; snprintf(hh,8,"%02d",h12); snprintf(mm,8,"%02d",lt.tm_min);
      float ccx=(ca.x+cb.x)*0.5f, ccy=(ca.y+cb.y)*0.5f, fs=std::min(46.0f,c1*0.42f);
      // digits ROLL when they change: the outgoing number lifts away and fades while the new one
      // rises into its place. Tracked per field so the hour only moves on the hour.
      static int   lastH=-1,lastM=-1;
      static char  prevH[8]={0},prevM[8]={0};
      static float rollH=1.0f, rollM=1.0f;
      if(lastH!=h12){ if(lastH>=0) snprintf(prevH,8,"%02d",lastH==0?12:lastH); lastH=h12; rollH=0.0f; }
      if(lastM!=lt.tm_min){ if(lastM>=0) snprintf(prevM,8,"%02d",lastM); lastM=lt.tm_min; rollM=0.0f; }
      rollH=std::min(1.0f,rollH+g_frameDt*3.2f); rollM=std::min(1.0f,rollM+g_frameDt*3.2f);
      auto rollDigits=[&](const char* cur,const char* prev,float t,float baseY){
          float e=EaseOutCubic(std::clamp(t,0.0f,1.0f));
          if(e<0.999f && prev[0]){      // old value lifts out
              float w0=g_fHuge->CalcTextSizeA(fs,FLT_MAX,0,prev).x;
              dl->AddText(g_fHuge,fs,V(ccx-w0/2,baseY-e*fs*0.55f),
                          WithA(COL_INK,(int)(255*(1.0f-e))),prev); }
          float w1=g_fHuge->CalcTextSizeA(fs,FLT_MAX,0,cur).x;
          dl->AddText(g_fHuge,fs,V(ccx-w1/2,baseY+(1.0f-e)*fs*0.55f),
                      WithA(COL_INK,(int)(255*e)),cur); };
      dl->PushClipRect(ca,cb,true);
      rollDigits(hh,prevH,rollH,ccy-fs*1.5f);
      for(int i=0;i<3;i++) dl->AddCircleFilled(V(ccx-11+i*11,ccy-fs*0.25f),2.6f,COL_INK2);
      rollDigits(mm,prevM,rollM,ccy+fs*0.05f);
      dl->PopClipRect();
      const char* ap=g_clock24?"":(lt.tm_hour<12?"AM":"PM");
      if(*ap) TextAt(dl,g_fMed,18,V(ccx-TextW(g_fMed,18,ap)/2,ccy+fs*1.35f),COL_GOLD,ap); }

    // ===== COL 2: profile+quote (top) + calendar (bottom) =====
    { float pH=area.y*0.30f, r=rise(1);
      ImVec2 a=V(x2,org.y+r),b=V(x2+c2,org.y+pH+r); card(a,b,rnd);
      // Caelestia's user card, now the Profile widget: avatar + presence, name, custom status, "icon : value" rows
      WUser(dl,io,a,V(b.x-a.x,b.y-a.y),0x51F0,false);
      // calendar below (shared interactive calendar: animate, year view, mark days)
      float r2=rise(2); ImVec2 la=V(x2,org.y+pH+gap+r2),lb=V(x2+c2,org.y+area.y+r2); card(la,lb,rnd);
      DrawCalendar(dl,la,lb,io); }

    // ===== COL 3: vertical stack of 3 resource ring gauges (CPU / RAM / disk) =====
    { float r=rise(3); ImVec2 a=V(x3,org.y+r),b=V(x3+c3,org.y+area.y+r); card(a,b,rnd);
      float cx=(a.x+b.x)*0.5f, colH=(b.y-a.y), R=std::min(c3*0.34f,colH/3.0f*0.30f), th=std::max(5.0f,R*0.22f);
      auto ring=[&](int i,float frac,int icon){
          float cy=a.y+colH*((i+0.5f)/3.0f); float a1=-2.618f,a2=a1+5.236f;
          dl->PathArcTo(V(cx,cy),R,a1,a2,48); dl->PathStroke(WithA(COL_INK2,72),0,th);
          float f=std::clamp(frac,0.0f,1.0f)*anim, ae=a1+5.236f*f;
          dl->PathArcTo(V(cx,cy),R,a1,ae,48); dl->PathStroke(COL_GOLD,0,th);
          if(f>0.01f) dl->AddCircleFilled(V(cx+cosf(ae)*R,cy+sinf(ae)*R),th*0.55f+pulse,COL_GOLD);
          ImU32 ic=COL_INK; float s=R*0.42f;
          (void)ic; (void)s;
          // the number IS the readout now - a bare ring told you nothing precise, and the user
          // asked to actually see cpu / ram / storage. It counts to its value on the house animator.
          float shownPct=Cael::anim(9410+i,std::clamp(frac,0.0f,1.0f)*100.0f,
                                    Cael::DUR_DEFAULT_SPATIAL,Cael::DEFAULT_SPATIAL);
          char pv[12]; snprintf(pv,12,"%d%%",(int)lroundf(shownPct));
          float ps=std::max(11.0f,R*0.62f);
          TextAt(dl,g_fMed,ps,V(cx-TextW(g_fMed,ps,pv)/2,cy-ps*0.62f),COL_INK,pv);
          const char* lbl = (icon==0)?"CPU":(icon==1)?"RAM":"DISK";
          TextAt(dl,g_fSml,10,V(cx-TextW(g_fSml,10,lbl)/2,cy+R+3),WithA(COL_INK2,215),lbl);
          // the absolute figure under the label, so "68%" of what is answerable at a glance
          if(icon!=0){ char ab[28];
              double used=(icon==1)?g_st.memUsed:g_st.diskUsed, tot=(icon==1)?g_st.memTotal:g_st.diskTotal;
              if(tot>0){ snprintf(ab,28,"%.0f/%.0fG",used/1073741824.0,tot/1073741824.0);
                  TextAt(dl,g_fSml,9,V(cx-TextW(g_fSml,9,ab)/2,cy+R+14),WithA(COL_INK2,165),ab); } } };
      float memF=g_st.memTotal?(float)g_st.memUsed/g_st.memTotal:0;
      float diskF=g_st.diskTotal?(float)g_st.diskUsed/g_st.diskTotal:0;
      ring(0,(float)g_st.cpuUsage,0); ring(1,memF,1); ring(2,diskF,2);
      navCard(a,b,2,TT_BLUR,9312,true); }   // cpuUsage is 0..1

    // ===== COL 4: tall Media card (circular art + rays + progress + transport + mascot) =====
    { float r=rise(4); ImVec2 a=V(x4,org.y+r),b=V(x4+c4,org.y+area.y+r); card(a,b,rnd);
      bool overBtn=false;
      float cx=(a.x+b.x)*0.5f, cyArt=a.y+(b.y-a.y)*0.26f, R=std::min(c4*0.30f,(b.y-a.y)*0.22f);
      if(g_mdStyle==MDSTYLE_CIRCLE){
          // Measured off the screen recording, frame by frame: a plain ROUND cover with a thin rim,
          // ringed the whole way by rounded-cap bars that start at a fixed gap outside it. Each bar
          // reacts on its own but neighbours track each other, so it reads as a flower rather than
          // noise, and they collapse to nothing when playback is paused. No arc anywhere - progress
          // is the separate bar under the transport.
          if(g_mdRays) MediaBars(dl,V(cx,cyArt),R,tint,g_md.playing);
          if(g_mdArt){ ImVec2 uv0,uv1; CoverUV(g_mdArtW,g_mdArtH,R*2,R*2,uv0,uv1);
              dl->AddImageRounded((ImTextureID)g_mdArt,V(cx-R,cyArt-R),V(cx+R,cyArt+R),uv0,uv1,
                                  IM_COL32(255,255,255,255),R); }
          else { dl->AddCircleFilled(V(cx,cyArt),R,WithA(tint,90),0);
              dl->AddRectFilled(V(cx-R*0.32f,cyArt-R*0.28f),V(cx-R*0.10f,cyArt+R*0.28f),WithA(tint,220),2);
              dl->AddRectFilled(V(cx+R*0.05f,cyArt-R*0.36f),V(cx+R*0.14f,cyArt+R*0.24f),WithA(tint,220),1.5f); }
          dl->AddCircle(V(cx,cyArt),R,dk?IM_COL32(255,255,255,46):IM_COL32(255,255,255,110),0,1.6f);
      } else {
          // The still's look: bars across the BOTTOM half only, because the arc owns the top.
          if(g_mdRays){ const int NR=64; float bands[64]; AudioBands(bands,NR,g_md.playing);
            for(int i=0;i<NR;i++){ float ang=i*(6.2832f/NR)-1.5708f;
                if(sinf(ang)<0.06f) continue;
                float len=4+bands[i]*R*0.6f;
                dl->AddLine(V(cx+cosf(ang)*(R+10),cyArt+sinf(ang)*(R+10)),V(cx+cosf(ang)*(R+10+len),cyArt+sinf(ang)*(R+10+len)),WithA(tint,(int)(80+bands[i]*150)),2.2f); } }
          if(g_mdArt) M3ShapeImage(dl,V(cx,cyArt),R,g_mdArt,g_mdArtW,g_mdArtH,g_mdMediaShape,0.0f);
          else { M3Shape(dl,V(cx,cyArt),R,WithA(tint,90),g_mdMediaShape,0.0f);
              dl->AddRectFilled(V(cx-R*0.32f,cyArt-R*0.28f),V(cx-R*0.10f,cyArt+R*0.28f),WithA(tint,220),2);
              dl->AddRectFilled(V(cx+R*0.05f,cyArt-R*0.36f),V(cx+R*0.14f,cyArt+R*0.24f),WithA(tint,220),1.5f); }
          { double dur=g_md.dur; float pf=(float)(dur>0? std::clamp(MediaPos()/dur,0.0,1.0) : 0.0);
            float wph = g_md.playing? -ShellPhase()*2.4f : 0.0f;
            WavyArc(dl,V(cx,cyArt),R+16,pf,WithA(tint,255),WithA(COL_INK2,110),4.2f,wph); }
      }
      float tw=c4-28, ty=cyArt+R+22;
      TextAt(dl,g_fMed,17,V(cx-TextW(g_fMed,17,hasM?Clip(g_fMed,17,g_md.title,tw).c_str():"No media")/2,ty),COL_INK,hasM?Clip(g_fMed,17,g_md.title,tw).c_str():"No media");
      TextAt(dl,g_fSml,14,V(cx-TextW(g_fSml,14,hasM?Clip(g_fSml,14,g_md.artist,tw).c_str():"No media")/2,ty+24),COL_INK2,hasM?Clip(g_fSml,14,g_md.artist,tw).c_str():"No media");
      if(hasM&&!g_md.album.empty()) TextAt(dl,g_fSml,13,V(cx-TextW(g_fSml,13,Clip(g_fSml,13,g_md.album,tw).c_str())/2,ty+46),WithA(COL_INK2,190),Clip(g_fSml,13,g_md.album,tw).c_str());
      float bty=ty+80; auto tb=[&](float bcx,int kind,BYTE key){
          bool h=fabsf(io.MousePos.x-bcx)<17&&fabsf(io.MousePos.y-bty)<17; ImU32 c=h?COL_GOLD:COL_INK;
          dl->AddCircleFilled(V(bcx,bty),16,WithA(COL_INK2,h?60:28));
          if(kind==0){ dl->AddRectFilled(V(bcx-6,bty-6),V(bcx-4,bty+6),c); dl->AddTriangleFilled(V(bcx+6,bty-6),V(bcx+6,bty+6),V(bcx-3,bty),c); }
          else if(kind==1){ if(g_md.playing){dl->AddRectFilled(V(bcx-5,bty-7),V(bcx-1,bty+7),c);dl->AddRectFilled(V(bcx+1,bty-7),V(bcx+5,bty+7),c);} else dl->AddTriangleFilled(V(bcx-5,bty-7),V(bcx-5,bty+7),V(bcx+7,bty),c); }
          else { dl->AddRectFilled(V(bcx+4,bty-6),V(bcx+6,bty+6),c); dl->AddTriangleFilled(V(bcx-6,bty-6),V(bcx-6,bty+6),V(bcx+3,bty),c); }
          if(h) overBtn=true;
          if(click&&h){ keybd_event(key,0,0,0); keybd_event(key,0,KEYEVENTF_KEYUP,0); } };
      tb(cx-46,0,VK_MEDIA_PREV_TRACK); tb(cx,1,VK_MEDIA_PLAY_PAUSE); tb(cx+46,2,VK_MEDIA_NEXT_TRACK);
      // anywhere on the card EXCEPT the transport opens the Media tab
      navCard(a,b,1,TT_SLIDE,9311,!overBtn);
      if(!DrawMediaImage(dl,V(cx,b.y-42),c4-40,64,hasM&&g_md.playing))
          BongoCat(dl,V(cx,b.y-46),std::min(1.2f,c4/240.0f),hasM&&g_md.playing); }
}

// The music tab. It takes its colour from the cover that is playing, seeks when you drag the
// timeline, and exposes shuffle / repeat when the player supports them.
static float g_mdSeekPreview=-1.0f;     // while dragging, the position under the cursor
// Morphing blob (Caelestia's media BackgroundShapes / CoverVisualiser): a soft convex shape whose
// radius wobbles per-angle over time and swells with the music's energy. Drawn low-alpha behind content.
static void MorphBlob(ImDrawList* dl,ImVec2 c,float baseR,ImU32 col,float phase,float energy){
    const int N=48; ImVec2 pts[N];
    for(int i=0;i<N;i++){ float a=(float)i/N*6.2831853f;
        float r=baseR*(1.0f + 0.14f*sinf(3*a+phase) + 0.10f*sinf(5*a-1.3f*phase)
                            + (0.06f+0.30f*energy)*sinf(2*a+phase*0.7f));
        pts[i]=V(c.x+cosf(a)*r,c.y+sinf(a)*r); }
    dl->AddConvexPolyFilled(pts,N,col);
}

// ---- M3Shapes, as Caelestia's Workspace.qml uses them ------------------------------------------
// upstream (`import M3Shapes`):
//     empty    -> MaterialShape.Circle
//     occupied -> MaterialShape.Square
//     focused  -> focusedShapeList[floor(random * 18)]  - a NEW random shape every time that
//                 workspace becomes focused, which is why the shape keeps changing on switch.
// Material 3 Expressive's real shape set is a spline library; these are parametric stand-ins that
// reproduce each family's silhouette. Every one is star-shaped about the centre and the amplitudes
// are kept modest so AddConvexPolyFilled (what MorphBlob already relies on) stays well behaved.
enum { M3_CIRCLE=0, M3_SQUARE, M3_SLANTED, M3_OVAL, M3_PILL, M3_TRIANGLE, M3_ARROW, M3_DIAMOND,
       M3_PENTAGON, M3_GEM, M3_VERYSUNNY, M3_SUNNY, M3_COOKIE4, M3_COOKIE6, M3_COOKIE7,
       M3_COOKIE9, M3_COOKIE12, M3_CLOVER4, M3_SOFTBURST, M3_GHOSTISH,
       // appended - the rest of Material 3 Expressive's MaterialShapes, as in Caelestia (M3Shapes)
       M3_ARCH, M3_FAN, M3_SEMICIRCLE, M3_CLAMSHELL, M3_CLOVER8, M3_BURST, M3_BOOM, M3_SOFTBOOM,
       M3_FLOWER, M3_PUFFY, M3_PUFFYDIAMOND, M3_PIXELCIRCLE, M3_PIXELTRIANGLE, M3_BUN, M3_HEART, M3_COUNT };
// picker labels, in M3_* order - the settings chooser indexes this directly
static const char* M3_SHAPE_LABEL[M3_COUNT] = {
    "Circle","Square","Slanted","Oval","Pill","Triangle","Arrow","Diamond",
    "Pentagon","Gem","Very sunny","Sunny","Cookie 4","Cookie 6","Cookie 7",
    "Cookie 9","Cookie 12","Clover","Soft burst","Ghostish",
    "Arch","Fan","Semicircle","Clamshell","Clover 8","Burst","Boom","Soft boom",
    "Flower","Puffy","Puffy diamond","Pixel circle","Pixel triangle","Bun","Heart" };
// the names custom widgets (TOML / Python) use for them
static const char* M3_SHAPE_ID[M3_COUNT] = {
    "circle","square","slanted","oval","pill","triangle","arrow","diamond",
    "pentagon","gem","very_sunny","sunny","cookie4","cookie6","cookie7",
    "cookie9","cookie12","clover4","soft_burst","ghostish",
    "arch","fan","semicircle","clamshell","clover8","burst","boom","soft_boom",
    "flower","puffy","puffy_diamond","pixel_circle","pixel_triangle","bun","heart" };
static int M3ShapeFromName(const std::string& n0){
    std::string n; for(char c:n0){ if(c==' '||c=='-') n+='_'; else n+=(char)tolower((unsigned char)c); }
    if(n=="clover") n="clover4"; if(n=="verysunny") n="very_sunny"; if(n=="softburst") n="soft_burst";
    if(n=="softboom") n="soft_boom"; if(n=="puffydiamond") n="puffy_diamond"; if(n=="pixelcircle") n="pixel_circle";
    if(n=="pixeltriangle") n="pixel_triangle";
    for(int i=0;i<M3_COUNT;i++) if(n==M3_SHAPE_ID[i]) return i;
    char* e=nullptr; long v=strtol(n0.c_str(),&e,10); if(e&&*e==0&&v>=0&&v<M3_COUNT) return (int)v;
    return -1;
}
static_assert(M3_PENTAGON==8, "g_m3Shape's default literal must track the M3_* enum");
static_assert(M3_COOKIE12==16, "g_mdMediaShape's default literal must track the M3_* enum");
// the 18 upstream lists for `focused`, in the same order
static const int M3_FOCUSED[18]={ M3_SLANTED,M3_OVAL,M3_PILL,M3_TRIANGLE,M3_ARROW,M3_DIAMOND,
                                  M3_PENTAGON,M3_GEM,M3_VERYSUNNY,M3_SUNNY,M3_COOKIE4,M3_COOKIE6,
                                  M3_COOKIE7,M3_COOKIE9,M3_COOKIE12,M3_CLOVER4,M3_SOFTBURST,M3_GHOSTISH };

// Fill a STAR-SHAPED (not necessarily convex) outline.
//
// AddConvexPolyFilled fans from vertex 0, so on a concave outline it fills straight across the
// concavities - which is what drew a diagonal seam through the morphing gauges and let the accent
// spill outside the silhouette. Every shape in this set is star-shaped about its centre by
// construction, so fanning from the CENTRE instead is exact for all of them.
//
// That fan has no antialiasing of its own, hence the closing stroke: ImGui antialiases polylines, so
// a 1px outline in the same colour puts the smooth edge back.
static void M3FillStar(ImDrawList* dl, ImVec2 c, const ImVec2* p, int n, ImU32 col){
    if(n<3 || (col>>24)==0) return;
    // ImDrawListSharedData is opaque outside imgui_internal.h; the atlas carries the same UV
    ImVec2 uv = ImGui::GetIO().Fonts->TexUvWhitePixel;
    dl->PrimReserve(n*3, n+1);
    unsigned int base = (unsigned int)dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(c, uv, col);
    for(int i=0;i<n;i++) dl->PrimWriteVtx(p[i], uv, col);
    for(int i=0;i<n;i++){
        dl->PrimWriteIdx((ImDrawIdx)base);
        dl->PrimWriteIdx((ImDrawIdx)(base+1+(unsigned)i));
        dl->PrimWriteIdx((ImDrawIdx)(base+1+(unsigned)((i+1)%n)));
    }
    dl->AddPolyline(p, n, col, ImDrawFlags_Closed, 1.0f);
}

// `soft` is how much the lobes OVERLAP: 1.0 = they exactly touch (crisp scallops), higher = they
// swallow each other into shallower, rounder bumps.
static bool M3Scallop(int shape,int& nl,float& soft){
    switch(shape){
    case M3_SUNNY:     nl=8;  soft=1.15f; return true;
    case M3_VERYSUNNY: nl=8;  soft=1.00f; return true;
    case M3_COOKIE4:   nl=4;  soft=1.30f; return true;
    case M3_COOKIE6:   nl=6;  soft=1.30f; return true;
    case M3_COOKIE7:   nl=7;  soft=1.30f; return true;
    case M3_COOKIE9:   nl=9;  soft=1.32f; return true;
    case M3_COOKIE12:  nl=12; soft=1.35f; return true;
    case M3_CLOVER4:   nl=4;  soft=1.02f; return true;   // a clover is FOUR deep round lobes
    case M3_SOFTBURST: nl=10; soft=1.10f; return true;
    case M3_GHOSTISH:  nl=3;  soft=1.20f; return true;
    case M3_CLOVER8:   nl=8;  soft=1.02f; return true;
    case M3_SOFTBOOM:  nl=15; soft=1.05f; return true;
    case M3_FLOWER:    nl=8;  soft=0.88f; return true;
    default: return false;
    }
}
// The radius of a cookie/flower at one angle, as the UNION OF N CIRCLES sitting on a ring.
//
// These were r = base + amp*cos(lobes*theta). A sinusoid's curvature radius at its peaks is about
// r/(1 + n^2*amp/r) - for 12 lobes at amp 0.11 that works out to roughly 5% of the shape, i.e. a
// couple of PIXELS at the size the cover is drawn. So the "cookie" rendered as a gear with sharp
// teeth and V-shaped valleys. Real Material 3 shapes are arcs, not waves: unioning round lobes
// gives round bumps AND round valleys, which is the silhouette in the reference.
//
// d + rho = 1 normalises the peak radius to 1; the inner disc at d*cos(half) closes the valleys so
// the union can never leave a gap between neighbouring lobes.
static float M3ScallopR(int nl,float soft,float th){
    const float half=3.14159265f/(float)std::max(2,nl);
    const float d   =1.0f/(1.0f+soft*sinf(half));
    const float rho =1.0f-d;
    const float r0  =d*cosf(half);
    float dth=th-floorf(th/(2.0f*half)+0.5f)*(2.0f*half);   // fold onto the nearest lobe
    float s=d*sinf(dth), best=r0;
    if(fabsf(s)<=rho){ float v=d*cosf(dth)+sqrtf(std::max(0.0f,rho*rho-s*s)); if(v>best) best=v; }
    return best;
}

static void M3ShapeMorph(ImDrawList* dl, ImVec2 c, float R, ImU32 col, int shapeA, int shapeB, float t, float spin);   // fwd
static void M3Shape(ImDrawList* dl, ImVec2 c, float R, ImU32 col, int shape, float spin=0.0f){
    if(R<0.6f) return;
    if(shape>=M3_ARCH){ M3ShapeMorph(dl,c,R,col,shape,shape,0.0f,spin); return; }
    auto poly=[&](int sides,float rot,float round){
        ImVec2 p[16]; if(sides>16) sides=16;
        for(int i=0;i<sides;i++){ float a=rot+spin+i*6.2831853f/sides;
            p[i]=V(c.x+cosf(a)*R,c.y+sinf(a)*R); }
        (void)round; dl->AddConvexPolyFilled(p,sides,col); };
    switch(shape){
    case M3_CIRCLE:   dl->AddCircleFilled(c,R,col,0); return;
    case M3_SQUARE:   { float k=R*0.80f;                       // M3's square is a soft squircle
                        if(fabsf(spin)<1e-4f){                 // axis-aligned: keep the rounded rect
                            dl->AddRectFilled(V(c.x-k,c.y-k),V(c.x+k,c.y+k),col,R*0.42f); return; }
                        // turning: a rounded rect cannot rotate, so draw it as a quad instead
                        float cs=cosf(spin), sn=sinf(spin);
                        ImVec2 q[4];
                        const float ox[4]={-k, k, k,-k}, oy[4]={-k,-k, k, k};
                        for(int i2=0;i2<4;i2++) q[i2]=V(c.x+ox[i2]*cs-oy[i2]*sn, c.y+ox[i2]*sn+oy[i2]*cs);
                        dl->AddConvexPolyFilled(q,4,col); } return;
    case M3_OVAL:     { const int N=40; ImVec2 p[N];
                        for(int i=0;i<N;i++){ float a=(float)i/N*6.2831853f;
                            p[i]=V(c.x+cosf(a)*R,c.y+sinf(a)*R*0.72f); }
                        dl->AddConvexPolyFilled(p,N,col); } return;
    case M3_PILL:     dl->AddRectFilled(V(c.x-R,c.y-R*0.62f),V(c.x+R,c.y+R*0.62f),col,R*0.62f); return;
    case M3_TRIANGLE: poly(3,-1.5708f,0); return;
    case M3_DIAMOND:  poly(4,-1.5708f,0); return;
    case M3_PENTAGON: poly(5,-1.5708f,0); return;
    case M3_GEM:      poly(6,-1.5708f,0); return;
    case M3_SLANTED:  { float k=R*0.80f, sk=R*0.26f; ImVec2 p[4]={
                          V(c.x-k+sk,c.y-k),V(c.x+k,c.y-k),V(c.x+k-sk,c.y+k),V(c.x-k,c.y+k) };
                        dl->AddConvexPolyFilled(p,4,col); } return;
    case M3_ARROW:    { ImVec2 p[5]={ V(c.x,c.y-R), V(c.x+R,c.y+R*0.25f), V(c.x+R*0.42f,c.y+R*0.12f),
                                      V(c.x,c.y+R), V(c.x-R*0.42f,c.y+R*0.12f) };
                        ImVec2 q[5]={ p[0],p[1],p[2],p[3],V(c.x-R,c.y+R*0.25f) };
                        dl->AddConvexPolyFilled(q,5,col); } return;
    default: break;
    }
    // the scalloped families - the SAME radius function the table and the outline use, so a shape
    // drawn directly and the same shape drawn mid-morph agree instead of being two silhouettes
    int nl=8; float soft=1.2f; M3Scallop(shape,nl,soft);
    const int N=128; ImVec2 p[N];
    for(int i=0;i<N;i++){ float a=(float)i/N*6.2831853f;
        float r=R*M3ScallopR(nl,soft,a);
        p[i]=V(c.x+cosf(a+spin)*r,c.y+sinf(a+spin)*r); }
    M3FillStar(dl,c,p,N,col);      // clover and very-sunny are concave: a convex fill cuts corners
}
// ---- morphing between silhouettes ---------------------------------------------------------------
// The shapes in the reference rice do not merely turn, they MORPH: a cookie flows into a pentagon
// flows into a clover, continuously, while still rotating.
//
// Every shape in this set is star-shaped about its centre (that is what lets M3Shape draw all of
// them as one convex polygon), so each can be reduced to a radius-vs-angle table at unit size. Two
// tables interpolate elementwise, which gives a clean morph between ANY pair without hand-authoring
// per-pair paths - and adding a shape to M3Outline adds it to every morph for free.
// 96 gave a 12-lobed cookie only 8 samples per lobe, so even an exact radius came out faceted.
static const int M3_SAMPLES = 160;

// One shape's outline at unit radius. Curves are sampled; the polygonal ones are exact.
// The scalloped families are a closed form - r(theta) = base + amp*cos(lobes*theta) - so they never
// need to be approximated by a polygon. Sampling them through M3Outline's polygon and then
// RAY-CASTING it back into a radius table did exactly that, and the beat between the two sample
// counts (96 rays against a 72-gon) cut the smooth scallops into sharp triangular teeth. Anything
// that wants the radius asks here first.
static int M3Outline(int shape, ImVec2* out, int cap){
    auto poly=[&](int sides,float rot)->int{
        if(sides>cap) sides=cap;
        for(int i=0;i<sides;i++){ float a=rot+i*6.2831853f/sides; out[i]=V(cosf(a),sinf(a)); }
        return sides; };
    // a rounded rectangle, half-extents (hx,hy), corner radius cr
    auto rrect=[&](float hx,float hy,float cr)->int{
        cr=std::min(cr,std::min(hx,hy)); int n=0; const int Q=14;
        const float cxs[4]={ hx-cr,  -(hx-cr), -(hx-cr),  hx-cr };
        const float cys[4]={ hy-cr,   hy-cr,  -(hy-cr), -(hy-cr) };
        const float a0s[4]={ 0.0f, 1.5707963f, 3.1415927f, 4.712389f };
        for(int k=0;k<4;k++) for(int i=0;i<Q && n<cap;i++){
            float a=a0s[k]+ (float)i/(Q-1)*1.5707963f;
            out[n++]=V(cxs[k]+cosf(a)*cr, cys[k]+sinf(a)*cr); }
        return n; };
    switch(shape){
    case M3_CIRCLE:   { int n=std::min(cap,64); for(int i=0;i<n;i++){ float a=(float)i/n*6.2831853f;
                          out[i]=V(cosf(a),sinf(a)); } return n; }
    case M3_SQUARE:   return rrect(0.80f,0.80f,0.42f);
    case M3_PILL:     return rrect(1.00f,0.62f,0.62f);
    case M3_OVAL:     { int n=std::min(cap,64); for(int i=0;i<n;i++){ float a=(float)i/n*6.2831853f;
                          out[i]=V(cosf(a),sinf(a)*0.72f); } return n; }
    case M3_TRIANGLE: return poly(3,-1.5707963f);
    case M3_DIAMOND:  return poly(4,-1.5707963f);
    case M3_PENTAGON: return poly(5,-1.5707963f);
    case M3_GEM:      return poly(6,-1.5707963f);
    case M3_SLANTED:  { const float k=0.80f, sk=0.26f;
                        out[0]=V(-k+sk,-k); out[1]=V(k,-k); out[2]=V(k-sk,k); out[3]=V(-k,k); return 4; }
    case M3_ARROW:    { out[0]=V(0,-1); out[1]=V(1,0.25f); out[2]=V(0.42f,0.12f);
                        out[3]=V(0,1);  out[4]=V(-1,0.25f); return 5; }
    case M3_ARCH:     { int n=0; out[n++]=V(-0.82f,0.86f); out[n++]=V(0.82f,0.86f);
                        for(int i=0;i<=24&&n<cap;i++){ float a=(float)i/24*3.1415927f; out[n++]=V(cosf(a)*0.82f,-sinf(a)*0.82f); }
                        return n; }
    case M3_FAN:      { int n=0; out[n++]=V(-0.78f,0.78f); out[n++]=V(-0.78f,-0.78f);
                        for(int i=0;i<=24&&n<cap;i++){ float a=-1.5707963f+(float)i/24*1.5707963f; out[n++]=V(-0.78f+cosf(a)*1.56f,0.78f+sinf(a)*1.56f); }
                        return n; }
    case M3_SEMICIRCLE:{ int n=0;
                        for(int i=0;i<=32&&n<cap;i++){ float a=3.1415927f+(float)i/32*3.1415927f; out[n++]=V(cosf(a),0.42f+sinf(a)); }
                        return n; }
    case M3_CLAMSHELL:{ int n=std::min(cap,72); for(int i=0;i<n;i++){ float a=(float)i/n*6.2831853f;
                          float r=0.86f+0.14f*cosf(6.0f*a); out[i]=V(cosf(a)*r,sinf(a)*r*0.82f); } return n; }
    case M3_BURST:    { int n=0; for(int i=0;i<24&&n<cap;i++){ float a=-1.5707963f+(float)i/24*6.2831853f; float r=(i%2)?0.76f:1.0f;
                          out[n++]=V(cosf(a)*r,sinf(a)*r); } return n; }
    case M3_BOOM:     { int n=0; for(int i=0;i<30&&n<cap;i++){ float a=-1.5707963f+(float)i/30*6.2831853f; float r=(i%2)?0.70f:1.0f;
                          out[n++]=V(cosf(a)*r,sinf(a)*r); } return n; }
    case M3_PUFFY:    { int n=std::min(cap,96); for(int i=0;i<n;i++){ float a=(float)i/n*6.2831853f; float r=M3ScallopR(6,1.08f,a);
                          out[i]=V(cosf(a)*r,sinf(a)*r*0.84f); } return n; }
    case M3_PUFFYDIAMOND:{ int n=std::min(cap,96); for(int i=0;i<n;i++){ float a=(float)i/n*6.2831853f;
                          float r=M3ScallopR(4,1.55f,a+0.7853982f); float dm=1.0f/(fabsf(cosf(a))+fabsf(sinf(a)));
                          r=r*0.55f+dm*0.45f; out[i]=V(cosf(a)*r,sinf(a)*r); } return n; }
    case M3_PIXELCIRCLE:{ static const int ys[]={-5,-4,-3,-2,-1,0,1,2,3,4};   // a 10x10 pixel disc, as its staircase outline
                        static const int hw[]={2,4,4,5,5,5,5,4,4,2};
                        int n=0; const float u=0.19f;
                        for(int k=0;k<10&&n+2<=cap;k++){ out[n++]=V(hw[k]*u,ys[k]*u); out[n++]=V(hw[k]*u,(ys[k]+1)*u); }
                        for(int k=9;k>=0&&n+2<=cap;k--){ out[n++]=V(-hw[k]*u,(ys[k]+1)*u); out[n++]=V(-hw[k]*u,ys[k]*u); }
                        return n; }
    case M3_PIXELTRIANGLE:{ int n=0; const float u=0.19f;
                        for(int k=0;k<9&&n+2<=cap;k++){ float y0=(-4.5f+k)*u, hwv=(0.5f+k*0.5f)*u*2.0f; out[n++]=V(hwv,y0); out[n++]=V(hwv,y0+u); }
                        for(int k=8;k>=0&&n+2<=cap;k--){ float y0=(-4.5f+k)*u, hwv=(0.5f+k*0.5f)*u*2.0f; out[n++]=V(-hwv,y0+u); out[n++]=V(-hwv,y0); }
                        return n; }
    case M3_BUN:      { int n=std::min(cap,96); for(int i=0;i<n;i++){ float a=(float)i/n*6.2831853f;
                          float x=cosf(a), y=sinf(a)*0.86f; float waist=1.0f-0.16f*exp(-(y*y)/0.02f);
                          float sq=pow(fabsf(cosf(a)),0.55f)*(cosf(a)<0?-1.0f:1.0f);
                          out[i]=V(sq*0.95f*waist,y); (void)x; } return n; }
    case M3_HEART:    { int n=std::min(cap,96); for(int i=0;i<n;i++){ float t=(float)i/n*6.2831853f;
                          float x=16.0f*powf(sinf(t),3.0f);
                          float y=-(13.0f*cosf(t)-5.0f*cosf(2*t)-2.0f*cosf(3*t)-cosf(4*t));
                          out[i]=V(x/17.0f,(y+2.0f)/17.0f); } return n; }
    default: break;
    }
    // the scalloped families - one source of truth for the radius, shared with M3Table
    { int nl; float soft;
      if(M3Scallop(shape,nl,soft)){
          int n=std::min(cap,120);          // round lobes need more points than a sinusoid did
          for(int i=0;i<n;i++){ float a=(float)i/n*6.2831853f; float r=M3ScallopR(nl,soft,a);
              out[i]=V(cosf(a)*r,sinf(a)*r); }
          return n;
      } }
    int n=std::min(cap,72);                 // anything unlisted: a plain circle
    for(int i=0;i<n;i++){ float a=(float)i/n*6.2831853f; out[i]=V(cosf(a),sinf(a)); }
    return n;
}

// radius-vs-angle for one shape, built once and kept
static const float* M3Table(int shape){
    static float tab[M3_COUNT][M3_SAMPLES];
    static bool  built[M3_COUNT]={false};
    shape=std::clamp(shape,0,M3_COUNT-1);
    if(built[shape]) return tab[shape];
    { int nl; float soft;                           // exact for the scalloped families
      if(M3Scallop(shape,nl,soft)){
          for(int i=0;i<M3_SAMPLES;i++)
              tab[shape][i]=M3ScallopR(nl,soft,(float)i/M3_SAMPLES*6.2831853f);
          built[shape]=true; return tab[shape];
      } }
    ImVec2 pts[128]; int n=M3Outline(shape,pts,128);
    for(int i=0;i<M3_SAMPLES;i++){
        float ang=(float)i/M3_SAMPLES*6.2831853f;
        ImVec2 d=V(cosf(ang),sinf(ang));
        float best=0.0f;
        for(int e=0;e<n;e++){
            ImVec2 p=pts[e], q=pts[(e+1)%n];
            ImVec2 s2=V(q.x-p.x,q.y-p.y);
            float den=d.x*s2.y-d.y*s2.x;
            if(fabsf(den)<1e-7f) continue;
            float u=-(d.x*p.y-d.y*p.x)/den;          // where along the edge
            if(u<-1e-4f||u>1.0f+1e-4f) continue;
            float hx=p.x+u*s2.x, hy=p.y+u*s2.y;
            float t=(fabsf(d.x)>fabsf(d.y))? hx/d.x : hy/d.y;
            if(t>best) best=t;
        }
        tab[shape][i]= best>0.0f? best : 1.0f;
    }
    built[shape]=true;
    return tab[shape];
}

// Draw a silhouette that is `t` of the way from shapeA to shapeB, rotated by `spin`.
static void M3ShapeMorph(ImDrawList* dl, ImVec2 c, float R, ImU32 col,
                         int shapeA, int shapeB, float t, float spin){
    if(R<0.6f) return;
    // NO early-out to M3Shape at the ends of the morph. M3Shape is a DIFFERENT renderer - hand-built
    // polygons and rounded rects - while the middle of a morph samples the radius tables, so the two
    // do not agree on the same silhouette. Handing the ends to one and the middle to the other made
    // the shape SNAP as t crossed 0.001 and again as it crossed 0.999: it held a still picture for
    // most of the cycle, jumped, flowed, then jumped back. That is the "glitching, looks like a
    // video" - not the morph itself. Sampling the table the whole way through is continuous.
    const float* ta=M3Table(shapeA);
    const float* tb=M3Table(shapeB);
    ImVec2 p[M3_SAMPLES];
    for(int i=0;i<M3_SAMPLES;i++){
        float a=(float)i/M3_SAMPLES*6.2831853f;
        float r=R*(ta[i]+(tb[i]-ta[i])*t);
        p[i]=V(c.x+cosf(a+spin)*r, c.y+sinf(a+spin)*r);
    }
    M3FillStar(dl,c,p,M3_SAMPLES,col);
}
// Material 3 Expressive's WAVY progress indicator, bent into an arc that stands OVER the cover.
//
// I had this as a plain ring encircling the art. It is not: in the reference it is an arc across
// the TOP only, the cover sits under it untouched, and the elapsed portion is a travelling SINE
// WAVE - the "little scribble" - while the remainder is a thin flat track. M3 Expressive separates
// the two with a gap and parks a stop-indicator dot at the far end. All four of those details are
// what make it read as drawn-by-hand rather than as a progress bar someone bent into a circle.
//
// `phase` travels while the track plays, so the wave crawls along instead of sitting still.
static void WavyArc(ImDrawList* dl,ImVec2 c,float R,float frac,
                    ImU32 active,ImU32 track,float th,float phase){
    frac=std::clamp(frac,0.0f,1.0f);
    const float SWEEP=3.6652f;                       // 210 degrees...
    const float A0=-1.5707963f-SWEEP*0.5f;           // ...centred on straight up
    const float A1=A0+SWEEP;
    float aEnd=A0+SWEEP*frac;
    float gap=(frac>0.002f && frac<0.998f)? std::min(th*2.2f/std::max(1.0f,R), SWEEP*0.06f) : 0.0f;

    // remaining: thin and flat
    float ts=std::min(A1,aEnd+gap);
    if(A1-ts>0.004f){
        dl->PathArcTo(c,R,ts,A1,std::max(10,(int)((A1-ts)*R*0.35f)));
        dl->PathStroke(track,0,th*0.5f);
    }
    // stop indicator at the far end
    dl->AddCircleFilled(ImVec2(c.x+cosf(A1)*R,c.y+sinf(A1)*R),th*0.42f,track);

    // elapsed: the wave. Amplitude eases in over the first stretch so it leaves the start cleanly
    // instead of kinking straight into full swing.
    if(frac>0.002f){
        const int MAXP=256;
        float arcLen=SWEEP*frac*R;
        int n=std::clamp((int)(arcLen*0.7f),12,MAXP);
        float amp=th*0.80f;
        float waves=std::max(0.75f,arcLen/(th*6.0f));      // wavelength ~6x the stroke
        ImVec2 p[MAXP];
        for(int i=0;i<n;i++){
            float t=(float)i/(float)(n-1);
            float a=A0+SWEEP*frac*t;
            float taper=std::min(1.0f,t*5.0f);
            float r=R+sinf(t*waves*6.2831853f+phase)*amp*taper;
            p[i]=ImVec2(c.x+cosf(a)*r,c.y+sinf(a)*r);
        }
        dl->AddPolyline(p,n,active,0,th);
        dl->AddCircleFilled(p[0],th*0.5f,active);          // round the butt-end at the start
    }
}
// The cover, filled into an M3 silhouette instead of a circle - the reference art is a scalloped
// cookie. ImGui can only clip to rectangles, so this is not a clip: it is the same centre-fan
// M3FillStar builds, with per-vertex UVs so the polygon is TEXTURED with the artwork.
static void M3ShapeImage(ImDrawList* dl,ImVec2 c,float R,ID3D11ShaderResourceView* tex,
                         int imgW,int imgH,int shape,float spin){
    if(!tex||R<1.0f) return;
    const float* tab=M3Table(std::clamp(shape,0,M3_COUNT-1));
    ImVec2 uv0,uv1; CoverUV(imgW>0?imgW:1,imgH>0?imgH:1,R*2,R*2,uv0,uv1);
    auto uvAt=[&](ImVec2 p){
        return ImVec2(uv0.x+(uv1.x-uv0.x)*std::clamp((p.x-(c.x-R))/(2*R),0.0f,1.0f),
                      uv0.y+(uv1.y-uv0.y)*std::clamp((p.y-(c.y-R))/(2*R),0.0f,1.0f)); };
    ImVec2 p[M3_SAMPLES];
    for(int i=0;i<M3_SAMPLES;i++){
        float a=(float)i/M3_SAMPLES*6.2831853f;
        p[i]=ImVec2(c.x+cosf(a+spin)*R*tab[i], c.y+sinf(a+spin)*R*tab[i]);
    }
    const ImU32 W=IM_COL32(255,255,255,255);
    dl->PushTextureID((ImTextureID)tex);
    dl->PrimReserve(M3_SAMPLES*3, M3_SAMPLES+1);
    unsigned int base=(unsigned int)dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(c,uvAt(c),W);
    for(int i=0;i<M3_SAMPLES;i++) dl->PrimWriteVtx(p[i],uvAt(p[i]),W);
    for(int i=0;i<M3_SAMPLES;i++){
        dl->PrimWriteIdx((ImDrawIdx)base);
        dl->PrimWriteIdx((ImDrawIdx)(base+1+(unsigned)i));
        dl->PrimWriteIdx((ImDrawIdx)(base+1+(unsigned)((i+1)%M3_SAMPLES)));
    }
    dl->PopTextureID();
    // The centre fan has no antialiasing of its own, so the silhouette comes out pixel-crunchy.
    // ImGui antialiases polylines: restroking the same outline puts a smooth edge back.
    dl->AddPolyline(p,M3_SAMPLES,IM_COL32(255,255,255,26),ImDrawFlags_Closed,1.6f);
}

// Where an element is in its morph cycle right now. `seed` staggers elements so a row of gauges
// does not change shape in lockstep, which would read as a glitch rather than as motion.
static void M3MorphPick(int seed, const int* list, int n, float periodSec, int& a, int& b, float& t){
    a=b=list[0]; t=0.0f;
    if(n<=1 || !g_idleMotion) return;
    float ph=ShellPhase()/std::max(0.5f,periodSec) + (float)seed*0.41f;
    int i=(int)floorf(ph); float f=ph-(float)i;
    a=list[((i%n)+n)%n];
    b=list[(((i+1)%n)+n)%n];
    // hold the shape for most of the cycle, then flow into the next one
    float k=std::clamp((f-0.55f)/0.45f,0.0f,1.0f);
    t=k*k*(3.0f-2.0f*k);
}

// ---- M3 shape as a level meter ----------------------------------------------------------------
// The one thing in sarods2d's Quickshell rice that Aether had no equivalent of: a Material 3 shape
// used as a RESOURCE GAUGE, the metric filling it from the bottom like liquid.
//
// It is the shape drawn twice - once as the track, once in the accent clipped to the bottom `frac`
// of its bounding box. Clipping rather than building a filled sub-path is what makes this work for
// all 20 shapes at once: the silhouette stays exactly M3Shape's, so a cookie gauge has scalloped
// edges on its fill line for free, and adding a shape to M3Shape adds it here too.
static void M3Gauge(ImDrawList* dl, ImVec2 o, ImVec2 s, const char* title,
                    float frac, const std::string& sub, int shape, int animId){
    frac = std::clamp(frac, 0.0f, 1.0f);
    // 0.47 of the SHORT side, centred in the cell, left no margin at all: stack three of these and
    // the silhouettes touch - and the spiky families (cookie, soft burst) reach past R, so they
    // visibly ran into each other. Inset the box, and reserve a band underneath for the label so
    // the text has somewhere to live that is not on top of the fill.
    const float labelH = sub.empty()? 20.0f : 32.0f;
    float availH = s.y - labelH - 8.0f;
    float R = std::min(s.x*0.5f - 8.0f, availH*0.5f);
    R *= 0.92f;                                   // headroom for the shapes that spike past 1.0
    if(R < 10.0f) return;
    ImVec2 c = V(o.x + s.x*0.5f, o.y + 6.0f + availH*0.5f);
    shape = std::clamp(shape, 0, M3_COUNT-1);

    // The level EASES to the reading instead of stepping to it once a second, so a gauge is always
    // either moving or settling - never a frozen picture of a number.
    float shown = Cael::anim(animId, frac, Cael::DUR_DEFAULT_SPATIAL*2, Cael::DEFAULT_SPATIAL);
    shown = std::clamp(shown, 0.0f, 1.0f);
    float ph = ShellPhase();
    float spin = ph * 0.16f;                        // the silhouette turns slowly, forever
    // ...and flows through a family of shapes while it turns, which is what the rice actually does.
    // The chosen shape leads the cycle, so with idle motion off the gauge is exactly that shape.
    int mA=shape, mB=shape; float mT=0.0f;
    { const int cyc[6]={ shape, M3_COOKIE7, M3_SQUARE, M3_CLOVER4, M3_GEM, M3_SOFTBURST };
      M3MorphPick(animId, cyc, 6, 5.0f, mA, mB, mT); }

    M3ShapeMorph(dl, c, R, COL_TRACK, mA, mB, mT, spin);
    if(shown > 0.002f){
        // clip to the bottom of the shape's box, then repaint the same shape in the accent. The cut
        // line rocks gently, which is what sells it as liquid rather than a progress bar.
        float wob = R*0.018f*sinf(ph*1.15f);
        float top = c.y + R - 2.0f*R*shown + wob;
        dl->PushClipRect(V(c.x-R-2, top), V(c.x+R+2, c.y+R+2), true);
        M3ShapeMorph(dl, c, R, AccA(225), mA, mB, mT, spin);
        dl->PopClipRect();
        // The surface itself: a shallow travelling wave rather than a straight rule. Its width has
        // to follow the container or the line pokes out of the silhouette at high and low levels -
        // the shape's own path is not exposed, so the circumscribed circle at this height, pulled in
        // a fifth, is the approximation that stays inside every one of the 20 shapes.
        { float dy=top-c.y;
          float hw=sqrtf(std::max(0.0f, R*R-dy*dy))*0.80f;
          if(hw>2.0f){
              const int NW=18; ImVec2 wv[NW];
              for(int i=0;i<NW;i++){
                  float t=(float)i/(NW-1);
                  wv[i]=V(c.x-hw + t*hw*2.0f,
                          top + sinf(t*6.2831853f*1.35f + ph*2.1f)*R*0.035f);
              }
              dl->AddPolyline(wv,NW,AccBright(),0,1.7f);
          } }
    }

    // Only the VALUE goes inside the silhouette, because only the value has the on-accent/ink
    // contrast test below it. The title and the detail used to be drawn inside too, in a fixed
    // COL_INK2 - so on a filled shape they were dark grey on the accent and simply could not be
    // read. They now sit in the reserved band UNDER the shape, on the card, where the ring gauges
    // put theirs and where the contrast is known.
    float vs = std::min(R*0.62f, 30.0f);          // value size
    float ls = std::max(10.0f, vs*0.42f);         // label / sub size
    char pc[16]; snprintf(pc, 16, "%d%%", (int)std::round(shown*100));
    bool  onFill = (c.y - vs*0.30f) > (c.y + R - 2.0f*R*shown);
    ImU32 vcol = onFill ? M3OnPrimary() : COL_INK;
    TextAt(dl, g_fMed, vs, V(c.x - TextW(g_fMed,vs,pc)*0.5f, c.y - vs*0.55f), vcol, pc);
    float ty = c.y + R + 4.0f;
    TextAt(dl, g_fSml, ls, V(c.x - TextW(g_fSml,ls,title)*0.5f, ty), WithA(COL_INK2,215), title);
    if(!sub.empty()){
        std::string sb = sub;
        while(!sb.empty() && TextW(g_fSml,ls*0.9f,sb.c_str()) > s.x-8.0f) sb.pop_back();
        TextAt(dl, g_fSml, ls*0.9f, V(c.x - TextW(g_fSml,ls*0.9f,sb.c_str())*0.5f, ty+ls+2.0f),
               WithA(COL_INK2,170), sb.c_str());
    }
}
// "62\xC2\xB0C", or empty when the sensor said it does not know
static std::string TempSub(double t){
    if(t <= 0) return "";
    // octal, not \xC2\xB0: a hex escape is greedy, so "\xB0C" would be read as one 12-bit constant
    char c[24]; snprintf(c,24,"%d\302\260C",(int)std::round(t)); return c;
}



// a loop with an arrow head; mode 1 (repeat-one) adds the "1", matching every music player
static void RepeatGlyph(ImDrawList* dl, ImVec2 c, ImU32 col, int mode){
    dl->PathArcTo(c,7.0f,-2.75f,0.45f,20); dl->PathStroke(col,0,1.7f);
    dl->PathArcTo(c,7.0f,0.40f,3.60f,20);  dl->PathStroke(col,0,1.7f);
    dl->AddTriangleFilled(V(c.x+2.6f,c.y-9.0f),V(c.x+9.0f,c.y-6.4f),V(c.x+3.0f,c.y-3.4f),col);
    dl->AddTriangleFilled(V(c.x-2.6f,c.y+9.0f),V(c.x-9.0f,c.y+6.4f),V(c.x-3.0f,c.y+3.4f),col);
    if(mode==1){
        dl->AddRectFilled(V(c.x-3.2f,c.y-3.6f),V(c.x+3.2f,c.y+3.6f),IM_COL32(0,0,0,0));
        TextAt(dl,g_fSml,11,V(c.x-2.6f,c.y-6.0f),col,"1"); }
}
// ---- lyrics rendering ------------------------------------------------------------------------------------
// One lyric line, "sung": the whole line in a dim ink, then the part already sung painted over it in bright
// ink with a soft glow in the song's colour, filling left to right over the time the line is sung. `p` is
// 0..1 through the line; the lit edge is feathered so the fill reads as light moving, not a hard wipe.
static void LyricSing(ImDrawList* dl,ImFont* f,float fs,const std::vector<std::string>& subs,float x0,float y0,float boxW,
                      float lineH,bool centre,float p,ImU32 dim,ImU32 lit,ImU32 glow,float glowAmt){
    float total=0; std::vector<float> ws; for(auto& t:subs){ float w=TextW(f,fs,t.c_str()); ws.push_back(w); total+=w; }
    float litW=total*std::clamp(p,0.0f,1.0f), acc=0;
    for(size_t k=0;k<subs.size();k++){
        float w=ws[k], x=centre? x0+(boxW-w)*0.5f : x0, y=y0+lineH*k;
        const char* t=subs[k].c_str();
        float here=std::clamp(litW-acc,0.0f,w);
        if(here<w) TextAt(dl,f,fs,V(x,y),dim,t);
        if(here>0.0f){
            if(glowAmt>0.01f){                                    // glow: the lit text smeared in the song's colour
                dl->PushClipRect(V(x-10,y-10),V(x+here+6,y+fs+12),true);
                int ga=(int)(((glow>>IM_COL32_A_SHIFT)&0xFF)*glowAmt);
                for(int r=0;r<2;r++){ float rad= r? 3.5f : 1.8f; int aa= r? ga/5 : ga/3;
                    for(int d=0;d<8;d++){ float an=d*0.785398f; TextAt(dl,f,fs,V(x+cosf(an)*rad,y+sinf(an)*rad),WithA(glow,aa),t); } }
                dl->PopClipRect(); }
            dl->PushClipRect(V(x-2,y-6),V(x+here,y+fs+8),true);
            TextAt(dl,f,fs,V(x,y),lit,t);
            dl->PopClipRect();
            if(here<w){                                           // feathered leading edge
                dl->PushClipRect(V(x+here,y-6),V(x+std::min(w,here+9.0f),y+fs+8),true);
                TextAt(dl,f,fs,V(x,y),WithA(lit,(int)(((lit>>IM_COL32_A_SHIFT)&0xFF)*0.5f)),t);
                dl->PopClipRect(); }
        }
        acc+=w;
    }
}
static void LyricWrap(ImFont* f,float fs,const std::string& in,float w,std::vector<std::string>& out){
    out.clear(); std::string cur; size_t i=0;
    while(i<in.size()){
        size_t sp=in.find(' ',i); std::string word=in.substr(i,(sp==std::string::npos?in.size():sp)-i);
        std::string trial=cur.empty()? word : cur+" "+word;
        if(!cur.empty() && TextW(f,fs,trial.c_str())>w){ out.push_back(cur); cur=word; } else cur=trial;
        if(sp==std::string::npos) break; i=sp+1;
    }
    if(!cur.empty()||out.empty()) out.push_back(cur);
}
// How long a line is actually sung (vs the gap before the next one): fill over the sung part, then hold lit.
static float LyricFillDur(const std::vector<LyricLine>& L,size_t i){
    double start=L[i].t, next=(i+1<L.size())? L[i+1].t : start+6.0;
    double gap=std::max(0.3,next-start);
    double est=0.9+0.07*(double)L[i].text.size();
    return (float)std::clamp(std::min(gap*0.94,est*1.6),0.35,gap);
}
// Current synced line index for the playhead (-1 before the first line), and progress through it.
static int LyricCurrent(const std::vector<LyricLine>& L,double t,float& p){
    p=0; if(L.empty()) return -1;
    int lo=0,hi=(int)L.size()-1,cur=-1;
    while(lo<=hi){ int m=(lo+hi)/2; if(L[m].t<=t){ cur=m; lo=m+1; } else hi=m-1; }
    if(cur>=0) p=(float)std::clamp((t-L[cur].t)/LyricFillDur(L,cur),0.0,1.0);
    return cur;
}
// The scrolling lyrics column (Media tab). Current line centred ~38% down, sung with a glow; neighbours dim
// with distance; edges fade out; wheel scrolls freely and it drifts back 3s later; click a line to seek there.
static void DrawLyricsColumn(ImDrawList* dl,ImVec2 a,ImVec2 b,ImU32 tint,ImGuiIO& io,bool click){
    int st=g_lyricsState.load();
    float W=b.x-a.x, H=b.y-a.y; if(W<60||H<40) return;
    std::vector<LyricLine> L; std::vector<std::string> P;
    { std::lock_guard<std::mutex> lk(g_lyricsMtx); L=g_lyrics; P=g_lyricsPlain; }
    ImFont* f=g_fMed; const float fs=22.0f, lh=fs*1.28f, gap=14.0f;
    if(st<=1 || (st==3) || (L.empty() && P.empty())){
        const char* m = (st<=1)? "Finding lyrics\xE2\x80\xA6" : "No lyrics found for this song";
        float cy=a.y+H*0.42f;
        if(st<=1){ float t=(float)GetTickCount64()*0.004f;           // three breathing dots
            for(int i=0;i<3;i++){ float k=0.5f+0.5f*sinf(t-i*0.9f); dl->AddCircleFilled(V(a.x+W*0.5f-18+i*18,cy-22),3.5f+k*1.5f,WithA(tint,(int)(90+k*150))); } }
        TextAt(dl,g_fSml,15,V(a.x+(W-TextW(g_fSml,15,m))*0.5f,cy),WithA(COL_INK2,190),m);
        return;
    }
    bool synced=!L.empty();
    // ---- layout (wrapped) - cached per song/width ----
    static std::vector<std::vector<std::string>> wr; static std::vector<float> ys; static float contentH=0;
    static int wrGen=-1; static float wrW=-1; static size_t wrN=0;
    size_t n = synced? L.size() : P.size();
    if(wrGen!=g_lyricsGen.load() || fabsf(wrW-W)>0.5f || wrN!=n){
        wrGen=g_lyricsGen.load(); wrW=W; wrN=n; wr.assign(n,{}); ys.assign(n,0); float y=0;
        for(size_t i=0;i<n;i++){
            std::string t = synced? L[i].text : P[i];
            if(t.empty()) t = synced? "\xE2\x80\xA2  \xE2\x80\xA2  \xE2\x80\xA2" : " ";
            LyricWrap(f,fs,t,W-24,wr[i]); ys[i]=y; y+=lh*wr[i].size()+gap; }
        contentH=y; }
    double t=MediaPos()+LyrOffset();
    float p=0; int cur = synced? LyricCurrent(L,t,p) : -1;
    // ---- scroll ----
    static float scroll=0, manual=0; static ULONGLONG manualAt=0; static int lastGen=-2;
    bool over=io.MousePos.x>a.x&&io.MousePos.x<b.x&&io.MousePos.y>a.y&&io.MousePos.y<b.y;
    if(over && io.MouseWheel!=0){ manual-=io.MouseWheel*lh*2.0f; manualAt=GetTickCount64(); }
    float target;
    if(synced){ int ci=std::max(0,cur); target = ys[ci] + lh*wr[ci].size()*0.5f - H*0.38f; }
    else target = (g_md.dur>1)? (contentH-H*0.6f)*(float)std::clamp(t/g_md.dur,0.0,1.0) - H*0.2f : 0.0f;
    if(GetTickCount64()-manualAt>3000) manual += (0.0f-manual)*std::min(1.0f,g_frameDt*4.0f);
    target += manual;
    if(lastGen!=g_lyricsGen.load()){ lastGen=g_lyricsGen.load(); scroll=target; }
    scroll += (target-scroll)*std::min(1.0f,g_frameDt*7.0f);
    // ---- draw ----
    dl->PushClipRect(a,b,true);
    const float fade=46.0f;
    for(size_t i=0;i<n;i++){
        float y=a.y+ys[i]-scroll, hh=lh*wr[i].size();
        if(y+hh<a.y-4 || y>b.y+4) continue;
        float edge=std::min(std::clamp((y+hh*0.5f-a.y)/fade,0.0f,1.0f), std::clamp((b.y-(y+hh*0.5f))/fade,0.0f,1.0f));
        int d = synced? abs((int)i-std::max(cur,0)) : 2;
        float la = (synced && (int)i==cur)? 1.0f : d<=1? 0.50f : std::max(0.22f,0.50f-0.09f*(d-1));
        if(!synced) la=0.72f;
        bool hov = synced && over && io.MousePos.y>y-4 && io.MousePos.y<y+hh+2;
        if(hov){ la=std::min(1.0f,la+0.25f);
            dl->AddRectFilled(V(a.x+2,y-5),V(b.x-2,y+hh+1),WithA(COL_INK,(int)(16*edge)),10);
            if(click){ g_reqSeek=std::max(0.0,(double)L[i].t-LyrOffset()+0.02); manual=0; manualAt=0; } }
        ImU32 dim=WithA(COL_INK,(int)(255*la*edge*(synced&&(int)i==cur? 0.42f : 1.0f)));
        if(synced && (int)i==cur){
            float glowK=0.75f+0.25f*sinf((float)GetTickCount64()*0.004f);
            LyricSing(dl,f,fs,wr[i],a.x+12,y,W-24,lh,false,p,dim,WithA(COL_INK,(int)(255*edge)),WithA(tint,255),glowK*edge);
        } else {
            for(size_t k=0;k<wr[i].size();k++) TextAt(dl,f,fs,V(a.x+12,y+lh*k),dim,wr[i][k].c_str());
        }
    }
    dl->PopClipRect();
}

static bool MsIcon(ImDrawList* dl,const std::string& name,ImVec2 c,float px,ImU32 col);   // fwd
static float MotionAnim(int panel,int id,float target);                                    // fwd
static int  g_mediaView=0;            // 0 player, 1 lyrics (media.lyrics_layout = "tab")
static bool g_mediaInner=false;       // drawing the player half of the sectioned layout
static void DrawMedia(ImDrawList* dl, ImVec2 org, ImVec2 area, ImGuiIO& io){
    LyricsMaybeFetch();
    if(g_mediaLayout==1){ DrawMediaV2(dl,org,area,io); return; }
    // ---- sectioned layout: Player | Lyrics, switched by a segmented control, sliding between them ----
    if(g_mediaLyrics && g_lyricsLayout==1 && !g_mediaInner && g_md.has && !g_md.title.empty()){
        const float SEGH=34.0f, top=SEGH+12.0f;
        float slide=MotionAnim(MP_DASHBOARD,840020,(float)g_mediaView);
        float sv=std::clamp(slide,-0.2f,1.2f);
        ImVec2 end=V(org.x+area.x,org.y+area.y);
        // player (it also paints the album wash behind both sections)
        dl->PushClipRect(org,end,true);
        g_mediaInner=true;
        DrawMedia(dl,V(org.x-sv*area.x,org.y+top),V(area.x,area.y-top),io);
        g_mediaInner=false;
        // lyrics section
        if(sv>0.001f){
            float ox=org.x+(1.0f-sv)*area.x, oy=org.y+top, ah=area.y-top;
            ImU32 tint=g_mdTint.load(); if(!tint) tint=COL_GOLD;
            float colW=std::min(area.x*0.30f,300.0f), artS=std::min(colW-20.0f,ah-110.0f);
            ImVec2 a0=V(ox+10+(colW-20-artS)*0.5f,oy+6), a1=V(a0.x+artS,a0.y+artS);
            for(int i=6;i>0;i--) dl->AddRectFilled(V(a0.x-i*0.5f,a0.y+i),V(a1.x+i*0.5f,a1.y+i*1.2f),IM_COL32(0,0,0,10),22);
            if(g_mdArt) DrawArtFit(dl,a0,a1,22.0f,255,tint); else dl->AddRectFilled(a0,a1,WithA(tint,80),22);
            float ty=a1.y+12, cxl=ox+colW*0.5f;
            std::string t1=Clip(g_fMed,17,g_md.title,colW-20), t2=Clip(g_fSml,14,g_md.artist,colW-20);
            TextAt(dl,g_fMed,17,V(cxl-TextW(g_fMed,17,t1.c_str())*0.5f,ty),COL_INK,t1.c_str());
            TextAt(dl,g_fSml,14,V(cxl-TextW(g_fSml,14,t2.c_str())*0.5f,ty+22),COL_INK2,t2.c_str());
            float by=ty+50, bx0=ox+20, bx1=ox+colW-20;
            if(g_md.dur>0){ float f=(float)std::clamp(MediaPos()/g_md.dur,0.0,1.0);
                dl->AddRectFilled(V(bx0,by),V(bx1,by+4),WithA(COL_INK2,60),2);
                dl->AddRectFilled(V(bx0,by),V(bx0+(bx1-bx0)*f,by+4),tint,2);
                bool bh=io.MousePos.x>bx0&&io.MousePos.x<bx1&&io.MousePos.y>by-8&&io.MousePos.y<by+12;
                if(bh&&io.MouseClicked[0]) g_reqSeek=(double)std::clamp((io.MousePos.x-bx0)/(bx1-bx0),0.0f,1.0f)*g_md.dur; }
            { float ry=by+26;
              if(ToastBtn(dl,V(cxl-40,ry),14,0,1.0f,io,7481)&&io.MouseClicked[0]) g_reqPrev=1;
              if(ToastBtn(dl,V(cxl,ry),16,1,1.0f,io,7482)&&io.MouseClicked[0]) g_reqPlay=1;
              if(ToastBtn(dl,V(cxl+40,ry),14,2,1.0f,io,7483)&&io.MouseClicked[0]) g_reqNext=1; }
            // the column itself, with Caelestia's "Lyrics" header row
            float lx0=ox+colW+10, lx1=org.x+(1.0f-sv)*area.x+area.x-6;
            MsIcon(dl,"lyrics",V(lx0+18,oy+14),20,COL_INK);
            TextAt(dl,g_fMed,17,V(lx0+34,oy+3),COL_INK,"Lyrics");
            int lst=g_lyricsState.load();
            const char* sub = lst==2? (g_lyricsSrc.load()==2? "synced \xC2\xB7 NetEase" : "synced \xC2\xB7 lrclib") : lst==4? "not synced" : lst==1? "searching" : lst==3? "none" : "";
            TextAt(dl,g_fSml,12,V(lx0+40+TextW(g_fMed,17,"Lyrics"),oy+8),WithA(COL_INK2,190),sub);
            DrawLyricsColumn(dl,V(lx0,oy+32),V(lx1,oy+ah),tint,io,io.MouseClicked[0]);
        }
        dl->PopClipRect();
        // segmented control on top
        { const char* L0="Player", *L1="Lyrics";
          float w0=TextW(g_fSml,14,L0)+52, w1=TextW(g_fSml,14,L1)+52, tw=w0+w1;
          float sx=org.x+(area.x-tw)*0.5f, sy=org.y+4;
          dl->AddRectFilled(V(sx,sy),V(sx+tw,sy+SEGH),WithA(COL_INK2,g_darkUI?34:26),SEGH*0.5f);
          float selX=sx+std::clamp(slide,0.0f,1.0f)*w0, selW=w0+(w1-w0)*std::clamp(slide,0.0f,1.0f);
          dl->AddRectFilled(V(selX+3,sy+3),V(selX+selW-3,sy+SEGH-3),AccA(215),(SEGH-6)*0.5f);
          for(int k=0;k<2;k++){
              float x0=k? sx+w0 : sx, ww=k? w1 : w0;
              bool h=io.MousePos.x>x0&&io.MousePos.x<x0+ww&&io.MousePos.y>sy&&io.MousePos.y<sy+SEGH;
              float on=std::clamp(k? slide : 1.0f-slide,0.0f,1.0f);
              ImU32 c2=Mix(COL_INK,M3OnPrimary(),on);
              MsIcon(dl,k? "lyrics" : "music_note",V(x0+22,sy+SEGH*0.5f),17,c2);
              TextAt(dl,g_fSml,14,V(x0+36,sy+SEGH*0.5f-9),c2,k? L1 : L0);
              if(h&&io.MouseClicked[0]) g_mediaView=k; } }
        return;
    }
    const char* tipMed=nullptr;   // hover hint for the source-row controls
    bool has = g_md.has && !g_md.title.empty();
    ImVec2 end=V(org.x+area.x,org.y+area.y);
    // the cover's own colour drives the accent for this tab
    ImU32 tint = g_mdTint.load(); if(!tint || !has) tint=COL_GOLD;
    auto TA=[&](int a){ return WithA(tint,a); };

    dl->PushClipRect(org,end,true);
    // No inner card: in the rice (clear1.png) the Media tab's content sits directly on the drawer
    // surface — the extra inset panel made the tab read as a card inside a card. The album-art wash
    // below still tints the whole area, which is what gives the tab its per-song colour.
    //
    // FULL BLEED: the wash paints into the DRAWER's inner rect, not the inset content box, so it
    // runs to the panel edge the way the rice does instead of sitting inside it as a rounded card.
    // It has to escape the content clip to do that, hence the extra push/pop around it.
    ImVec2 bo=org, be=end; bool bleed=(g_pageBleed1.x>g_pageBleed0.x+8);
    if(bleed){ bo=g_pageBleed0; be=g_pageBleed1; }
    float washRound = bleed? g_panelRound : g_cardRound;
    ImDrawFlags washFlags = bleed? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersAll;
    if(has){
        if(bleed){ dl->PopClipRect(); dl->PushClipRect(bo,be,true); }
        // cover as a soft, heavily knocked-back backdrop + a colour wash, so the tab feels like the song
        if(g_mdArt){
            ImVec2 uv0,uv1; CoverUV(g_mdArtW>0?g_mdArtW:1,g_mdArtH>0?g_mdArtH:1,be.x-bo.x,be.y-bo.y,uv0,uv1);
            dl->AddImageRounded((ImTextureID)g_mdArt,bo,be,uv0,uv1,IM_COL32(255,255,255,g_darkUI?70:52),washRound,washFlags);
            dl->AddRectFilled(bo,be,PanelCol(g_darkUI?212:222),washRound,washFlags);
        }
        dl->AddRectFilled(bo,be,TA(g_darkUI?26:20),washRound,washFlags);
        // morphing cover-visualiser shapes that breathe with the music (Caelestia BackgroundShapes)
        { float bands[32]; AudioBands(bands,24,g_md.playing);
          float energy=0; for(int i=0;i<24;i++)energy+=bands[i]; energy=std::min(energy/14.0f,1.0f);
          float t=(float)GetTickCount64()*0.0012f;
          dl->PushClipRect(bo,be,true);
          float bw2=be.x-bo.x, bh2=be.y-bo.y;
          MorphBlob(dl,V(be.x-bw2*0.20f,bo.y+bh2*0.30f),bh2*0.30f,TA(g_darkUI?32:26),t,energy);
          MorphBlob(dl,V(bo.x+bw2*0.28f,bo.y+bh2*0.80f),bh2*0.26f,TA(g_darkUI?24:20),t*0.8f+2.0f,energy);
          dl->PopClipRect(); }
        // a vertical sheen so the top reads brighter than the bottom - FADING. It was clipped at 55%,
        // which drew the "separation bar in the middle that divides the colours" on the Media page.
        TopWash(dl,bo,be,g_darkUI?12:20,washRound,washFlags,0.7f);
        if(bleed){ dl->PopClipRect(); dl->PushClipRect(org,end,true); }   // back to the content box
    }

    if(!has){
        // split layout like media.png: dotted-ring art placeholder (left) + message (right)
        float midY=org.y+area.y*0.5f, R=std::min(area.y*0.24f,area.x*0.135f);
        ImVec2 ac=V(org.x+area.x*0.30f, midY);
        int ND=40; for(int i=0;i<ND;i++){ float a=i*(6.2832f/ND)-1.5708f; float rr=R+8;
            dl->AddCircleFilled(V(ac.x+cosf(a)*rr,ac.y+sinf(a)*rr),1.8f,WithA(COL_INK2,150)); }   // dotted ring
        dl->AddCircleFilled(ac,R,WithA(COL_INK2,g_darkUI?26:20));
        { ImU32 g=COL_INK2; ImVec2 c=ac;                                                          // image-stack icon
          dl->AddRect(V(c.x-16,c.y-12),V(c.x+6,c.y+10),g,2,0,2.0f); dl->AddCircleFilled(V(c.x-9,c.y-4),3,g);
          dl->AddTriangleFilled(V(c.x-14,c.y+8),V(c.x-5,c.y-2),V(c.x+4,c.y+8),g);
          dl->AddRectFilled(V(c.x+10,c.y-10),V(c.x+12,c.y+10),g,1); dl->AddRectFilled(V(c.x+15,c.y-10),V(c.x+17,c.y+10),g,1); }
        float cx=org.x+area.x*0.60f;
        dl->AddCircleFilled(V(cx-92,midY-30),20,TA(220)); { ImVec2 n=V(cx-92,midY-30); ImU32 w=IM_COL32(255,255,255,235);
          dl->AddCircleFilled(V(n.x-6,n.y+6),4,w); dl->AddCircleFilled(V(n.x+8,n.y+2),4,w); dl->AddRectFilled(V(n.x-3,n.y-9),V(n.x-1,n.y+7),w,1); dl->AddRectFilled(V(n.x+9,n.y-11),V(n.x+11,n.y+3),w,1); dl->AddRectFilled(V(n.x-3,n.y-11),V(n.x+11,n.y-8),w,1); }
        const char* m="Nothing playing"; TextAt(dl,g_fBig,24,V(cx-64,midY-14),COL_INK,m);
        const char* m2="Play something for it to show up here!"; TextAt(dl,g_fSml,14,V(cx-64,midY+18),COL_INK2,m2);
        dl->PopClipRect();
        return;
    }

    // ===== centred layout (Caelestia media tab): art left, title/transport centre, bongocat right =====
    bool rawClick=io.MouseClicked[0];
    float midY=org.y+area.y*0.5f;
    // The player picker is drawn LAST (over the transport) but has to claim its clicks FIRST, or a
    // press inside it would also fire whatever control sits underneath. So its rect is computed up
    // front from the same formulas the source row uses, and `click` is masked for everything else.
    static bool  s_pickOpen=false;
    static float s_pickAnim=0.0f;
    std::vector<MediaSession> sess;
    { std::lock_guard<std::mutex> lk(g_mdSessMtx); sess=g_mdSessions; }
    // with the lyrics column on, everything shifts left to make room for it
    const bool lyrOn = g_mediaLyrics && g_lyricsLayout==0;
    const float cxF = lyrOn? 0.40f : 0.53f;
    float pk_cx=org.x+area.x*cxF;
    float pk_sy=org.y+area.y*0.52f+78.0f;          // == (trY+40)+38, the source row's y
    int   pk_n=(int)sess.size()+1;                 // +1 for "Follow current player"
    const float PK_RH=26.0f, PK_PAD=8.0f, PK_W=240.0f;
    float pk_h=pk_n*PK_RH+PK_PAD*2;
    ImVec2 pk0=V(pk_cx-PK_W*0.5f, pk_sy-10.0f-pk_h), pk1=V(pk_cx+PK_W*0.5f, pk_sy-10.0f);
    bool  pk_inside = s_pickOpen && io.MousePos.x>pk0.x&&io.MousePos.x<pk1.x&&
                                     io.MousePos.y>pk0.y&&io.MousePos.y<pk1.y;
    bool click = rawClick && !pk_inside;
    bool pk_toggled=false;      // this frame's click was the one that opened/closed the list
    // ---- circular art (left of centre) with dotted/burst ring + progress ring ----
    float artR= lyrOn? std::min(area.y*0.22f, area.x*0.095f) : std::min(area.y*0.24f, area.x*0.135f);
    ImVec2 ac=V(org.x+area.x*(lyrOn? 0.14f : 0.27f), midY);
    { const int NR=72; float bands[72]; AudioBands(bands,NR,g_md.playing);
      float r0=artR+8;
      for(int i=0;i<NR;i++){ float a=i*(6.2832f/NR)-1.5708f; float len=4.0f+bands[i]*(artR*0.6f);
          int aa=(int)(90+bands[i]*150);
          dl->AddLine(V(ac.x+cosf(a)*r0,ac.y+sinf(a)*r0),V(ac.x+cosf(a)*(r0+len),ac.y+sinf(a)*(r0+len)),WithA(tint,aa),2.2f); } }
    for(int i=9;i>0;i--) dl->AddCircleFilled(V(ac.x,ac.y+i*0.6f),artR+i*0.5f,IM_COL32(0,0,0,7));
    if(g_mdArt){ ImVec2 uv0,uv1; CoverUV(g_mdArtW>0?g_mdArtW:1,g_mdArtH>0?g_mdArtH:1,artR*2,artR*2,uv0,uv1);
        dl->AddImageRounded((ImTextureID)g_mdArt,V(ac.x-artR,ac.y-artR),V(ac.x+artR,ac.y+artR),uv0,uv1,IM_COL32(255,255,255,255),artR); }
    else { dl->AddCircleFilled(ac,artR,TA(90));
        dl->AddCircleFilled(V(ac.x-12,ac.y+16),9,tint); dl->AddCircleFilled(V(ac.x+20,ac.y+10),9,tint);
        dl->AddRectFilled(V(ac.x-4,ac.y-28),V(ac.x,ac.y+18),tint,2); dl->AddRectFilled(V(ac.x+28,ac.y-34),V(ac.x+32,ac.y+12),tint,2);
        dl->AddRectFilled(V(ac.x-4,ac.y-34),V(ac.x+32,ac.y-26),tint,3); }
    dl->AddCircle(ac,artR,IM_COL32(255,255,255,g_darkUI?40:90),0,1.6f);
    { double dur0=g_md.dur,pos0=MediaPos(); float pf=(float)(dur0>0?std::clamp(pos0/dur0,0.0,1.0):0.0);
      float rr=artR+4; dl->PathArcTo(ac,rr,-1.5708f,-1.5708f+6.2832f,90); dl->PathStroke(WithA(COL_INK2,60),0,3.0f);
      if(pf>0.001f){ dl->PathArcTo(ac,rr,-1.5708f,-1.5708f+6.2832f*pf,90); dl->PathStroke(WithA(tint,255),0,3.0f); } }

    // ---- bongocat (right) - or the lyrics column ----
    if(lyrOn){
        float lx0=org.x+area.x*0.60f, lx1=end.x-4;
        // header: LYRICS  - synced/plain -  timing offset
        float hy=org.y+6;
        TextAt(dl,g_fSml,12,V(lx0+12,hy+2),WithA(tint,230),"LYRICS");
        int lst=g_lyricsState.load();
        const char* sub = lst==2? (g_lyricsSrc.load()==2? "synced \xC2\xB7 NetEase" : "synced \xC2\xB7 lrclib")
                        : lst==4? "not synced" : lst==1? "searching" : lst==3? "none" : "";
        TextAt(dl,g_fSml,12,V(lx0+12+TextW(g_fSml,12,"LYRICS")+10,hy+2),WithA(COL_INK2,170),sub);
        if(lst==2){
            char ob[16]; snprintf(ob,16,"%+.2fs",LyrSongOffset());
            float ow=TextW(g_fSml,12,ob), bx=lx1-8;
            auto sbtn=[&](float x,const char* lab,int id)->bool{
                ImVec2 c=V(x,hy+9); bool h=fabsf(io.MousePos.x-c.x)<11&&fabsf(io.MousePos.y-c.y)<11;
                float ha=HoverAnim(id,h); dl->AddCircleFilled(c,10,WithA(COL_INK2,(int)(30+ha*40)));
                TextAt(dl,g_fMed,15,V(c.x-TextW(g_fMed,15,lab)*0.5f,c.y-9),WithA(COL_INK,220),lab);
                if(h) tipMed="This song's lyric timing: earlier / later";
                return h&&click; };
            if(sbtn(bx-10,"+",7461)) LyrSongOffsetAdd(+0.25f);
            TextAt(dl,g_fSml,12,V(bx-26-ow,hy+2),WithA(COL_INK2,200),ob);
            if(sbtn(bx-40-ow,"-",7462)) LyrSongOffsetAdd(-0.25f);
        }
        DrawLyricsColumn(dl,V(lx0,org.y+30),V(lx1,end.y-2),tint,io,click);
    } else
    { float catX=org.x+area.x*0.84f, catH=area.y*0.34f;
      if(!DrawMediaImage(dl,V(catX,midY),area.x*0.22f,catH,g_md.playing))
          BongoCat(dl,V(catX,midY-4),std::min(1.4f,catH/60.0f),g_md.playing); }

    // ---- centre column: title / album / artist ----
    float cx=org.x+area.x*cxF, cw=area.x*(lyrOn? 0.30f : 0.40f);
    float ty=org.y+area.y*0.20f;
    { std::string t1=Clip(g_fBig,26,g_md.title,cw); TextAt(dl,g_fBig,26,V(cx-TextW(g_fBig,26,t1.c_str())/2,ty),COL_INK,t1.c_str()); }
    ty+=34;
    // ALWAYS three lines (title / album / artist), like the rice. Dropping the album row when the
    // player reports none made the block jump up and down as tracks changed and left the artist
    // sitting where the album had been; a dash holds the slot the way the source row already does.
    { bool ka=!g_md.album.empty();
      std::string a2=ka? Clip(g_fSml,15,g_md.album,cw) : std::string("\xE2\x80\x93");
      TextAt(dl,g_fSml,15,V(cx-TextW(g_fSml,15,a2.c_str())/2,ty),WithA(COL_INK2,ka?210:110),a2.c_str()); ty+=22; }
    { std::string a3=Clip(g_fMed,16,g_md.artist,cw); TextAt(dl,g_fMed,16,V(cx-TextW(g_fMed,16,a3.c_str())/2,ty),COL_INK2,a3.c_str()); }

    // ---- transport row: shuffle · prev · PLAY · next · queue ----
    float trY=org.y+area.y*0.52f, sp=50;
    { int sh=g_mdShuffle.load(); ImVec2 c=V(cx-2*sp,trY); bool hov=fabsf(io.MousePos.x-c.x)<15&&fabsf(io.MousePos.y-c.y)<15;
      if(hov)dl->AddCircleFilled(c,15,WithA(COL_INK2,34)); ImU32 col=(sh==1)?tint:WithA(COL_INK2,200);
      dl->AddLine(V(c.x-7,c.y-4),V(c.x+2,c.y-4),col,1.7f); dl->AddLine(V(c.x+2,c.y-4),V(c.x+7,c.y+4),col,1.7f);
      dl->AddLine(V(c.x-7,c.y+4),V(c.x+2,c.y+4),col,1.7f); dl->AddLine(V(c.x+2,c.y+4),V(c.x+7,c.y-4),col,1.7f);
      if(hov&&click) g_reqShuffle=1; }
    if(ToastBtn(dl,V(cx-sp,trY),16,0,1.0f,io,7401)&&click) g_reqPrev=1;
    // The rice's play control is a PLAIN LIGHT GLYPH on the wash, not a filled accent disc - the
    // disc fought the album-art wash for the eye and made the transport look like a web player.
    // Hover brings a soft ring in so it is still obviously a button.
    { ImVec2 c=V(cx,trY); bool hov=fabsf(io.MousePos.x-c.x)<25&&fabsf(io.MousePos.y-c.y)<25; float ha=HoverAnim(7402,hov);
      if(ha>0.01f) dl->AddCircleFilled(c,22+ha*2.0f,WithA(COL_INK2,(int)(ha*40)));
      dl->AddCircle(c,22+ha*2.0f,WithA(COL_INK,(int)(60+ha*90)),0,1.4f);
      ImU32 ic=WithA(COL_INK,245);
      if(g_md.playing){ dl->AddRectFilled(V(c.x-7,c.y-10),V(c.x-2,c.y+10),ic,2); dl->AddRectFilled(V(c.x+2,c.y-10),V(c.x+7,c.y+10),ic,2); }
      else dl->AddTriangleFilled(V(c.x-6,c.y-10),V(c.x-6,c.y+10),V(c.x+10,c.y),ic);
      if(hov&&click) g_reqPlay=1; }
    if(ToastBtn(dl,V(cx+sp,trY),16,2,1.0f,io,7403)&&click) g_reqNext=1;
    // This button always toggled REPEAT, but it was drawn as a playlist/queue glyph - so it read as
    // a queue button that "did nothing useful". Now it looks like what it does: a loop, with a "1"
    // when the player is repeating a single track.
    { int rp=g_mdRepeat.load(); ImVec2 c=V(cx+2*sp,trY); bool hov=fabsf(io.MousePos.x-c.x)<15&&fabsf(io.MousePos.y-c.y)<15;
      if(hov)dl->AddCircleFilled(c,15,WithA(COL_INK2,34)); ImU32 col=(rp>0)?tint:WithA(COL_INK2,200);
      RepeatGlyph(dl,c,col,rp);
      if(hov&&click) g_reqRepeat=1; }
    // jump back / forward 10 seconds
    for(int dir=-1; dir<=1; dir+=2){
        ImVec2 c=V(cx+dir*3*sp,trY); bool hov=fabsf(io.MousePos.x-c.x)<16&&fabsf(io.MousePos.y-c.y)<16;
        float ha=HoverAnim(7470+dir,hov);
        if(ha>0.01f) dl->AddCircleFilled(c,16,WithA(COL_INK2,(int)(34*ha)));
        ImU32 col=WithA(COL_INK2,(int)(200+ha*55));
        if(dir<0){ dl->PathArcTo(c,9.0f,-1.2f,3.9f,24); dl->PathStroke(col,0,1.6f);
                   dl->AddTriangleFilled(V(c.x-7.5f,c.y-10.5f),V(c.x-1.0f,c.y-8.5f),V(c.x-5.5f,c.y-4.0f),col); }
        else     { dl->PathArcTo(c,9.0f,-0.76f,4.34f,24); dl->PathStroke(col,0,1.6f);
                   dl->AddTriangleFilled(V(c.x+7.5f,c.y-10.5f),V(c.x+1.0f,c.y-8.5f),V(c.x+5.5f,c.y-4.0f),col); }
        TextAt(dl,g_fSml,9,V(c.x-TextW(g_fSml,9,"10")*0.5f,c.y-5.5f),col,"10");
        if(hov){ tipMed = dir<0? "Back 10 seconds" : "Forward 10 seconds";
            if(click && g_md.dur>0) g_reqSeek=std::clamp(MediaPos()+dir*10.0,0.0,std::max(0.0,g_md.dur-1.0)); }
    }

    // ---- progress bar + times (drag to seek) ----
    double dur=g_md.dur, pos=MediaPos();
    float bw=area.x*(lyrOn? 0.27f : 0.34f), bx0=cx-bw/2, bx1=cx+bw/2, by=trY+40;
    bool overBar=io.MousePos.x>bx0-8&&io.MousePos.x<bx1+8&&io.MousePos.y>by-8&&io.MousePos.y<by+14;
    if(dur>0&&overBar&&io.MouseDown[0]) g_mdSeekPreview=(float)std::clamp((io.MousePos.x-bx0)/(bx1-bx0),0.0f,1.0f)*(float)dur;
    if(g_mdSeekPreview>=0&&!io.MouseDown[0]){ g_reqSeek=(double)g_mdSeekPreview; g_mdSeekPreview=-1.0f; }
    double showPos=(g_mdSeekPreview>=0)?g_mdSeekPreview:pos;
    float f=(float)(dur>0?std::clamp(showPos/dur,0.0,1.0):0.0); float barH=overBar?7.0f:5.0f;
    dl->AddRectFilled(V(bx0,by),V(bx1,by+barH),WithA(COL_INK2,70),barH*0.5f);
    dl->AddRectFilled(V(bx0,by),V(bx0+(bx1-bx0)*f,by+barH),tint,barH*0.5f);
    if(dur>0) dl->AddCircleFilled(V(bx0+(bx1-bx0)*f,by+barH*0.5f),overBar?7.0f:5.0f,tint);
    auto ts=[&](double s){ char c[16]; snprintf(c,16,"%d:%02d",(int)s/60,(int)s%60); return std::string(c); };
    TextAt(dl,g_fSml,13,V(bx0,by+14),COL_INK2,ts(showPos).c_str());
    { std::string rr=ts(dur); TextAt(dl,g_fSml,13,V(bx1-TextW(g_fSml,13,rr.c_str()),by+14),COL_INK2,rr.c_str()); }

    // ---- source row: [loop] [source pill] [dropdown ▾] [delete] ----
    { float sy=by+38; std::string src=g_mdSource.empty()?std::string("Player"):g_mdSource;
      float pillW=TextW(g_fSml,14,src.c_str())+44, total=32+8+pillW+8+30+8+26, gx=cx-total/2;
      // loop/repeat glyph — was drawn but completely inert; it is the same repeat toggle as the
      // transport row, so it now reflects the same state and drives the same request
      // lyrics toggle (the repeat control already lives in the transport row)
      { ImVec2 c=V(gx+14,sy+14);
        bool h=io.MousePos.x>gx&&io.MousePos.x<gx+28&&io.MousePos.y>sy&&io.MousePos.y<sy+28;
        if(h||g_mediaLyrics) dl->AddRectFilled(V(gx,sy),V(gx+28,sy+28),g_mediaLyrics? WithA(tint,h?90:60) : WithA(COL_INK2,52),14);
        ImU32 lc=g_mediaLyrics? WithA(COL_INK,240) : WithA(COL_INK2,200);
        for(int k=0;k<3;k++){ float w= k==1? 8.0f : 12.0f; dl->AddRectFilled(V(c.x-6,c.y-6+k*5),V(c.x-6+w,c.y-4.4f+k*5),lc,1); }
        dl->AddCircleFilled(V(c.x+6,c.y+5),2.6f,lc); dl->AddLine(V(c.x+8.4f,c.y+5),V(c.x+8.4f,c.y-3),lc,1.4f);
        if(h&&click){ g_mediaLyrics=!g_mediaLyrics; SaveConfig(); }
        if(h){ tipMed = g_mediaLyrics? "Hide lyrics" : "Show lyrics"; }
        gx+=32+8; }
      // source pill — clicking it opens the player picker (same as the chevron)
      { ImVec2 p0=V(gx,sy),p1=V(gx+pillW,sy+28);
        bool h=io.MousePos.x>p0.x&&io.MousePos.x<p1.x&&io.MousePos.y>p0.y&&io.MousePos.y<p1.y;
        dl->AddRectFilled(p0,p1,WithA(COL_INK2,g_darkUI?(h?62:46):(h?50:34)),14);
        dl->AddTriangleFilled(V(p0.x+13,sy+9),V(p0.x+13,sy+19),V(p0.x+21,sy+14),tint);   // play glyph
        TextAt(dl,g_fSml,14,V(p0.x+28,sy+6),COL_INK,src.c_str());
        if(h&&click){ s_pickOpen=!s_pickOpen; pk_toggled=true; }
        if(h) tipMed="Choose which player to control";
        gx+=pillW+8; }
      // dropdown chevron — had a hover state but no action at all
      { ImVec2 d0=V(gx,sy),d1=V(gx+30,sy+28); bool h=io.MousePos.x>d0.x&&io.MousePos.x<d1.x&&io.MousePos.y>d0.y&&io.MousePos.y<d1.y;
        dl->AddRectFilled(d0,d1,WithA(COL_INK2,(h?60:34)),14); ImVec2 cc=V(gx+15,sy+14);
        float dir=s_pickOpen?-1.0f:1.0f;   // points up while the list is open
        dl->AddLine(V(cc.x-4,cc.y-2*dir),V(cc.x,cc.y+2*dir),COL_INK,1.7f);
        dl->AddLine(V(cc.x,cc.y+2*dir),V(cc.x+4,cc.y-2*dir),COL_INK,1.7f);
        if(h&&click){ s_pickOpen=!s_pickOpen; pk_toggled=true; }
        if(h) tipMed="Choose which player to control";
        gx+=30+8; }
      // delete
      { ImVec2 t0=V(gx,sy),t1=V(gx+26,sy+28); bool h=io.MousePos.x>t0.x&&io.MousePos.x<t1.x&&io.MousePos.y>t0.y&&io.MousePos.y<t1.y;
        ImU32 tc=h?IM_COL32(232,96,86,255):WithA(COL_INK2,200); ImVec2 c=V(gx+13,sy+14);
        dl->AddRect(V(c.x-5,c.y-3),V(c.x+5,c.y+7),tc,1,0,1.6f); dl->AddLine(V(c.x-7,c.y-3),V(c.x+7,c.y-3),tc,1.6f); dl->AddLine(V(c.x-2,c.y-6),V(c.x+2,c.y-6),tc,1.6f);
        if(h&&click){ keybd_event(VK_MEDIA_STOP,0,0,0); keybd_event(VK_MEDIA_STOP,0,KEYEVENTF_KEYUP,0); }
        if(h) tipMed="Stop playback"; } }

    // ---- player picker (opened by the source pill / chevron) ----
    // NOTE: this is a PLAYER list, not a track queue. SMTC exposes no queue at all - a player's
    // upcoming tracks are simply not available to anything outside that app - so the honest thing
    // behind this chevron is "which player am I controlling", which is what it is upstream too.
    s_pickAnim += ((s_pickOpen?1.0f:0.0f)-s_pickAnim)*std::min(1.0f,g_frameDt*16.0f);
    if(fabsf((s_pickOpen?1.0f:0.0f)-s_pickAnim)<0.004f) s_pickAnim=(s_pickOpen?1.0f:0.0f);
    if(s_pickAnim>0.004f){
        float pa=EaseOutCubic(std::clamp(s_pickAnim,0.0f,1.0f)); int pal=(int)(pa*255);
        ImVec2 a0=V(pk0.x,pk1.y-(pk1.y-pk0.y)*pa), a1=pk1;      // grows upward out of the row
        // an opaque backing UNDER the glass: this list sits on top of the transport row and the
        // album-art wash, and the frosted surface alone is far too sheer to read names through
        dl->AddRectFilled(a0,a1, g_darkUI? IM_COL32(20,20,26,(int)(238*pa))
                                         : IM_COL32(246,247,250,(int)(240*pa)), 12);
        GlassPanel(dl,a0,a1,12,0,nullptr,pa);
        dl->PushClipRect(a0,a1,true);
        std::string cur; { std::lock_guard<std::mutex> lk(g_mdPickMtx); cur=g_mdPick; }
        float ry=a1.y-PK_PAD-pk_n*PK_RH;
        for(int i=0;i<pk_n;i++){
            bool follow=(i==0);
            const MediaSession* ms = follow? nullptr : &sess[i-1];
            bool sel = follow? cur.empty() : (cur==ms->id);
            bool rh = io.MousePos.x>a0.x+4&&io.MousePos.x<a1.x-4&&
                      io.MousePos.y>ry&&io.MousePos.y<ry+PK_RH;
            if(rh) dl->AddRectFilled(V(a0.x+4,ry),V(a1.x-4,ry+PK_RH),WithA(COL_INK2,(int)(40*pa)),8);
            if(sel) dl->AddRectFilled(V(a0.x+4,ry),V(a1.x-4,ry+PK_RH),WithA(tint,(int)(46*pa)),8);
            const char* label = follow? "Follow current player" : ms->name.c_str();
            TextAt(dl,g_fSml,14,V(a0.x+16,ry+5),WithA(sel?COL_INK:COL_INK2,pal),label);
            if(!follow && ms->playing)
                dl->AddCircleFilled(V(a1.x-16,ry+PK_RH*0.5f),3.2f,WithA(tint,pal));   // this one is playing
            if(rh&&rawClick){
                { std::lock_guard<std::mutex> lk(g_mdPickMtx); g_mdPick = follow? std::string() : ms->id; }
                s_pickOpen=false; }
            ry+=PK_RH;
        }
        dl->PopClipRect();
        // click anywhere else closes it - but never the click that just opened it
        if(rawClick && !pk_inside && !pk_toggled) s_pickOpen=false;
    }

    if(tipMed){ float tw=TextW(g_fSml,13,tipMed)+18, th=24;
        ImVec2 t0=V(std::clamp(io.MousePos.x-tw*0.5f,org.x+6,end.x-tw-6), io.MousePos.y-th-12);
        dl->AddRectFilled(t0,V(t0.x+tw,t0.y+th),IM_COL32(28,28,34,242),7);
        dl->AddRect(t0,V(t0.x+tw,t0.y+th),IM_COL32(255,255,255,26),7,0,1.0f);
        TextAt(dl,g_fSml,13,V(t0.x+9,t0.y+5),IM_COL32(240,242,246,250),tipMed); }
    dl->PopClipRect();
}
