// src/components/VideoLoop.h  —  Aether shell
// A looping video as a texture. Media Foundation decodes the file on its own thread (H.264 via the system decoder,
// NV12 converted to BGRA here), paced to the file's timestamps, rewinding at the end. The render thread
// uploads only the newest frame into one dynamic texture. Decoding pauses by itself when nothing has drawn the video
// for a moment, so a clip that is off screen costs nothing.
//
// Used for the nebula (assets/vfx/nebula.mp4) behind the Aether card and the launcher's nebula background. If the
// file is missing or the PC has no H.264 decoder (Windows "N" editions without the media pack) VideoLoopTex returns
// nullptr and the caller draws its fallback.
#pragma once
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

struct VideoLoop {
    std::wstring path;
    std::mutex mtx;
    std::vector<uint8_t> frame;            // newest decoded frame (BGRA, top-down)
    int w=0, h=0; bool fresh=false;
    std::atomic<bool> failed{false};
    std::atomic<ULONGLONG> lastUse{0};
    ID3D11Texture2D* tex=nullptr; ID3D11ShaderResourceView* srv=nullptr; int tw=0, th=0;
};
static std::vector<VideoLoop*> g_videoLoops;

static void VideoLoopWorker(VideoLoop* v){
    HRESULT hrCo=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    static std::atomic<bool> mfUp{false};
    if(!mfUp.exchange(true)) MFStartup(MF_VERSION,MFSTARTUP_LITE);
    IMFAttributes* attr=nullptr; IMFSourceReader* rd=nullptr;
    bool ok=false;
    do{
        if(FAILED(MFCreateAttributes(&attr,2))) break;
        if(FAILED(MFCreateSourceReaderFromURL(v->path.c_str(),attr,&rd))) break;
        rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS,FALSE);
        rd->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE);
        IMFMediaType* mt=nullptr; if(FAILED(MFCreateMediaType(&mt))) break;
        mt->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video); mt->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_NV12);   // the decoder's own format; converted below (MF's software RGB path left speckles)
        HRESULT hr=rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,mt); mt->Release();
        if(FAILED(hr)) break;
        ok=true;
    }while(false);
    if(!ok){ v->failed=true; AetherLog("video: could not open %s for decoding",W2U8(v->path).c_str()); }
    UINT32 fw=0, fh=0; LONG stride=0;
    if(ok){
        IMFMediaType* cur=nullptr;
        if(SUCCEEDED(rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,&cur))){
            MFGetAttributeSize(cur,MF_MT_FRAME_SIZE,&fw,&fh);
            UINT32 st=0; if(SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE,&st))) stride=(LONG)st; else stride=(LONG)fw;
            cur->Release(); }
        if(!fw||!fh){ v->failed=true; ok=false; }
    }
    std::vector<uint8_t> buf;
    LARGE_INTEGER qf; QueryPerformanceFrequency(&qf);
    auto nowMs=[&]{ LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart*1000.0/qf.QuadPart; };
    double clock0=nowMs();          // wall time at stream time 0
    while(ok && g_running){
        // nobody is looking: stop decoding, and shift the clock so playback resumes where it paused
        if(GetTickCount64()-v->lastUse.load()>1500){ double p0=nowMs(); Sleep(120); clock0+=nowMs()-p0; continue; }
        DWORD flags=0; LONGLONG ts=0; IMFSample* s=nullptr;
        HRESULT hr=rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,&ts,&s);
        if(FAILED(hr)){ if(s) s->Release(); v->failed=true; AetherLog("video: decode failed (0x%08lX)",(unsigned long)hr); break; }
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM){
            if(s) s->Release();
            PROPVARIANT pv; PropVariantInit(&pv); pv.vt=VT_I8; pv.hVal.QuadPart=0;
            rd->SetCurrentPosition(GUID_NULL,pv); PropVariantClear(&pv);
            clock0=nowMs(); continue; }
        if(!s) continue;
        // pace to the timestamp
        double due=clock0+ts/10000.0, n=nowMs();
        if(due>n+1.0) Sleep((DWORD)std::min(200.0,due-n));
        else if(n-due>500.0) clock0=n-ts/10000.0;          // fell far behind (a stall): catch up instead of racing
        IMFMediaBuffer* mb=nullptr;
        if(SUCCEEDED(s->ConvertToContiguousBuffer(&mb)) && mb){
            BYTE* p=nullptr; DWORD len=0;
            if(SUCCEEDED(mb->Lock(&p,nullptr,&len)) && p){
                // NV12: a full-size Y plane, then interleaved U/V at half size. The decoder pads the height to a
                // multiple of 16, so the plane height comes from the buffer length, not the frame size.
                LONG ys=std::max<LONG>(std::abs(stride),(LONG)fw);
                UINT32 hp=(UINT32)((uint64_t)len*2/(3*(uint64_t)ys)); if(hp<fh) hp=fh;
                if((uint64_t)ys*hp*3/2<=len){
                    buf.resize((size_t)fw*fh*4);
                    const BYTE* Y=p, *UV=p+(size_t)ys*hp;
                    for(UINT32 y=0;y<fh;y++){
                        const BYTE* yr=Y+(size_t)y*ys, *uvr=UV+(size_t)(y/2)*ys; uint8_t* d=&buf[(size_t)y*fw*4];
                        for(UINT32 x=0;x<fw;x++){
                            int c=(int)yr[x]-16, u=(int)uvr[(x/2)*2]-128, vv=(int)uvr[(x/2)*2+1]-128; if(c<0) c=0;
                            int r=(298*c+459*vv+128)>>8, g=(298*c-55*u-136*vv+128)>>8, b=(298*c+541*u+128)>>8;   // BT.709, limited range
                            d[x*4]=(uint8_t)std::clamp(b,0,255); d[x*4+1]=(uint8_t)std::clamp(g,0,255); d[x*4+2]=(uint8_t)std::clamp(r,0,255); d[x*4+3]=255; } }
                    std::lock_guard<std::mutex> lk(v->mtx); v->frame.swap(buf); v->w=(int)fw; v->h=(int)fh; v->fresh=true;
                }
                mb->Unlock(); }
            mb->Release(); }
        s->Release();
    }
    if(rd) rd->Release(); if(attr) attr->Release();
    if(SUCCEEDED(hrCo)) CoUninitialize();
}
static bool SavePng(const std::wstring& path,const uint8_t* bgra,int w,int h);   // fwd
// render thread: the current frame of a looping video (nullptr until the first frame, or if it cannot play)
static ID3D11ShaderResourceView* VideoLoopTex(const std::wstring& path,int& w,int& h){
    VideoLoop* v=nullptr;
    for(auto* x:g_videoLoops) if(x->path==path){ v=x; break; }
    if(!v){
        if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES) return nullptr;
        v=new VideoLoop(); v->path=path; v->lastUse=GetTickCount64(); g_videoLoops.push_back(v);
        std::thread(VideoLoopWorker,v).detach();
    }
    v->lastUse=GetTickCount64();
    if(v->failed) return nullptr;
    { std::lock_guard<std::mutex> lk(v->mtx);
      if(v->fresh && !v->frame.empty() && g_dev && g_ctx){
          if(g_videoDumpReq.exchange(false)) SavePng(U82W(ExeDir()+"video_frame.png"),v->frame.data(),v->w,v->h);
          if(!v->tex || v->tw!=v->w || v->th!=v->h){
              if(v->srv){ v->srv->Release(); v->srv=nullptr; } if(v->tex){ v->tex->Release(); v->tex=nullptr; }
              D3D11_TEXTURE2D_DESC d{}; d.Width=v->w; d.Height=v->h; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
              d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DYNAMIC; d.BindFlags=D3D11_BIND_SHADER_RESOURCE; d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
              if(SUCCEEDED(g_dev->CreateTexture2D(&d,nullptr,&v->tex)) && v->tex) g_dev->CreateShaderResourceView(v->tex,nullptr,&v->srv);
              v->tw=v->w; v->th=v->h; }
          D3D11_MAPPED_SUBRESOURCE m;
          if(v->tex && SUCCEEDED(g_ctx->Map(v->tex,0,D3D11_MAP_WRITE_DISCARD,0,&m))){
              for(int y=0;y<v->h;y++) memcpy((uint8_t*)m.pData+(size_t)y*m.RowPitch,&v->frame[(size_t)y*v->w*4],(size_t)v->w*4);
              g_ctx->Unmap(v->tex,0); }
          v->fresh=false; } }
    w=v->tw; h=v->th;
    return v->srv;
}
// the bundled VFX clips
static std::wstring VfxPath(const char* name){ return U82W(ExeDir()+"assets\\vfx\\"+name); }
