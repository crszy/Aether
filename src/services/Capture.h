// src/services/Capture.h  —  Aether shell
// Live screen capture through Windows.Graphics.Capture (WGC), one session per monitor.
//
// WHY WGC AND NOT PrintWindow
//
// The switcher's previews use PrintWindow(PW_RENDERFULLCONTENT): it works, but it costs tens of
// milliseconds per window and it BLOCKS on the target app answering WM_PRINT, so a whole screen's
// worth of windows refreshes at a couple of frames a second at best. That is fine for a snapshot on
// a card. It is not "see everything, live", which is what the workspace overview is for.
//
// WGC hands the compositor's own frames straight to a D3D11 texture, at the display's rate, for the
// cost of one copy. And the overview does not need one session per window: it captures each MONITOR
// once and slices that frame per window rect, so N windows cost exactly one capture.
//
// THE FEEDBACK LOOP. A monitor capture sees everything on that monitor - including the overview
// itself, which would then show a picture of itself showing a picture of itself. Any window that
// must not appear in the capture is marked with SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE),
// which makes it invisible to WGC (and to every other capture API) while staying visible on screen.
//
// WHAT CANNOT BE CAPTURED. komorebi hides a workspace by MINIMIZING its windows, so the workspaces
// you are not looking at are not on any screen and no capture API can reach them. The overview keeps
// the last frame it saw of each one instead - see Overview.h.
#pragma once
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <Windows.Graphics.DirectX.Direct3D11.interop.h>

namespace Cap {

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;

// One monitor's live feed.
struct Session {
    HMONITOR              mon = nullptr;
    wgc::GraphicsCaptureItem      item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession   session{nullptr};
    int  w = 0, h = 0;
    bool started = false;
};

// Is WGC available at all? Windows 10 1803+; a machine or a policy can still say no, and on a
// remote session it is simply absent. Asked once.
inline bool Supported(){
    static int cached = -1;
    if(cached >= 0) return cached != 0;
    cached = 0;
    try { cached = wgc::GraphicsCaptureSession::IsSupported() ? 1 : 0; } catch(...) { cached = 0; }
    return cached != 0;
}

// The WinRT face of our D3D11 device. WGC allocates its frames on this device, so the textures it
// produces can be copied into ours with no cross-device round trip.
inline winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice RtDevice(ID3D11Device* dev){
    static winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice rt{nullptr};
    if(rt) return rt;
    if(!dev) return rt;
    IDXGIDevice* dxgi=nullptr;
    if(FAILED(dev->QueryInterface(__uuidof(IDXGIDevice),(void**)&dxgi)) || !dxgi) return rt;
    winrt::com_ptr<::IInspectable> insp;
    HRESULT hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi, insp.put());
    dxgi->Release();
    if(FAILED(hr) || !insp) return rt;
    try { rt = insp.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>(); } catch(...) {}
    return rt;
}

// Never let this window appear in anything we capture.
inline void ExcludeFromCapture(HWND h){
    if(h) SetWindowDisplayAffinity(h, WDA_EXCLUDEFROMCAPTURE);
}

inline void Stop(Session& s){
    try { if(s.session) s.session.Close(); } catch(...) {}
    try { if(s.pool)    s.pool.Close();    } catch(...) {}
    s.session = nullptr; s.pool = nullptr; s.item = nullptr;
    s.started = false; s.w = s.h = 0;
}

inline bool Start(Session& s, HMONITOR mon, ID3D11Device* dev){
    Stop(s);
    if(!Supported() || !mon || !dev) return false;
    auto rt = RtDevice(dev);
    if(!rt) return false;
    try {
        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        wgc::GraphicsCaptureItem item{nullptr};
        if(FAILED(interop->CreateForMonitor(mon, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item))) || !item)
            return false;
        auto sz = item.Size();
        if(sz.Width < 8 || sz.Height < 8) return false;
        // CreateFreeThreaded, because frames are pulled from the render thread and nothing here
        // wants a dispatcher to pump. Two buffers is enough for "give me the newest".
        auto pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
                        rt, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, sz);
        auto ses  = pool.CreateCaptureSession(item);
        // The yellow "you are being recorded" border. Turning it off needs an access grant a
        // packaged app can ask for and a plain desktop app cannot, and older builds do not have the
        // property at all - so try, and carry on without it.
        try { ses.IsCursorCaptureEnabled(false); } catch(...) {}
        try { ses.IsBorderRequired(false); } catch(...) {}
        ses.StartCapture();
        s.mon=mon; s.item=item; s.pool=pool; s.session=ses;
        s.w=sz.Width; s.h=sz.Height; s.started=true;
        return true;
    } catch(...) { Stop(s); return false; }
}

// Copy the newest frame into `dst`, creating/resizing it as needed. Returns false when no new frame
// has arrived since the last call - the caller keeps showing what it already has.
//
// MUST be called on the thread that owns the immediate context (the render thread): everything else
// here is free-threaded, but CopyResource is not.
inline bool Latest(Session& s, ID3D11Device* dev, ID3D11DeviceContext* ctx,
                   ID3D11Texture2D** dst, ID3D11ShaderResourceView** dstSrv, int& w, int& h){
    if(!s.started || !dev || !ctx) return false;
    wgc::Direct3D11CaptureFrame frame{nullptr};
    try { frame = s.pool.TryGetNextFrame(); } catch(...) { return false; }
    if(!frame) return false;
    // Drain: TryGetNextFrame hands out the OLDEST buffered frame, so taking one per call would
    // always be a frame or two behind. Keep going until the pool is empty and use the last.
    for(;;){
        wgc::Direct3D11CaptureFrame next{nullptr};
        try { next = s.pool.TryGetNextFrame(); } catch(...) { break; }
        if(!next) break;
        frame.Close();
        frame = next;
    }
    bool ok=false;
    try {
        auto access = frame.Surface().as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> src;
        if(SUCCEEDED(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), src.put_void())) && src){
            D3D11_TEXTURE2D_DESC sd{}; src->GetDesc(&sd);
            if((int)sd.Width!=w || (int)sd.Height!=h || !*dst){
                if(*dstSrv){ (*dstSrv)->Release(); *dstSrv=nullptr; }
                if(*dst){ (*dst)->Release(); *dst=nullptr; }
                D3D11_TEXTURE2D_DESC td{};
                td.Width=sd.Width; td.Height=sd.Height; td.MipLevels=1; td.ArraySize=1;
                td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1;
                td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                if(SUCCEEDED(dev->CreateTexture2D(&td,nullptr,dst)) && *dst){
                    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
                    vd.Format=td.Format; vd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels=1;
                    dev->CreateShaderResourceView(*dst,&vd,dstSrv);
                    w=(int)sd.Width; h=(int)sd.Height;
                }
            }
            if(*dst && *dstSrv){ ctx->CopyResource(*dst,src.get()); ok=true; }
        }
    } catch(...) { ok=false; }
    frame.Close();
    return ok;
}

// Copy the newest frame into a caller-owned texture, REUSING it when the size already matches.
//
// The overview refreshes its memory of the current workspace twice a second for as long as the
// shell is running. Allocating a fresh full-screen texture every time meant four 8 MB creates and
// releases per second, for ever, to hold a picture almost nobody was looking at. Same two copies,
// no allocation churn.
inline bool SnapshotInto(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* live,
                         int w, int h, ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv,
                         int& tw, int& th){
    if(!dev||!ctx||!live||w<8||h<8) return false;
    if(!*tex || !*srv || tw!=w || th!=h){
        if(*srv){ (*srv)->Release(); *srv=nullptr; }
        if(*tex){ (*tex)->Release(); *tex=nullptr; }
        D3D11_TEXTURE2D_DESC td{};
        td.Width=w; td.Height=h; td.MipLevels=1; td.ArraySize=1;
        td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1;
        td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(dev->CreateTexture2D(&td,nullptr,tex)) || !*tex) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
        vd.Format=td.Format; vd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels=1;
        if(FAILED(dev->CreateShaderResourceView(*tex,&vd,srv)) || !*srv){
            (*tex)->Release(); *tex=nullptr; return false; }
        tw=w; th=h;
    }
    ctx->CopyResource(*tex,live);
    return true;
}

// A standalone copy of the newest frame, for keeping "what this workspace looked like". The caller
// owns the returned view.
inline ID3D11ShaderResourceView* Snapshot(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                          ID3D11Texture2D* live, int w, int h){
    if(!dev||!ctx||!live||w<8||h<8) return nullptr;
    D3D11_TEXTURE2D_DESC td{};
    td.Width=w; td.Height=h; td.MipLevels=1; td.ArraySize=1;
    td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1;
    td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* copy=nullptr;
    if(FAILED(dev->CreateTexture2D(&td,nullptr,&copy)) || !copy) return nullptr;
    ctx->CopyResource(copy,live);
    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
    vd.Format=td.Format; vd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels=1;
    ID3D11ShaderResourceView* srv=nullptr;
    dev->CreateShaderResourceView(copy,&vd,&srv);
    copy->Release();                       // the view keeps the texture alive
    return srv;
}

} // namespace Cap
