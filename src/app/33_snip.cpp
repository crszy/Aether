// Aether - the snipping tool.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =============================================================================================
// SNIPPING TOOL — region screenshot. We are the shell now, so this is ours to provide.
// grim/slurp/flameshot-style: freeze the whole virtual screen, dim it, drag a box, and the crop
// is saved to Pictures\Screenshots + copied to the clipboard, with a toast to confirm.
// =============================================================================================
static HWND g_snipHwnd=nullptr; static IDXGISwapChain1* g_snipSc=nullptr; static ID3D11RenderTargetView* g_snipRtv=nullptr;
static IDCompositionTarget* g_snipTgt=nullptr; static IDCompositionVisual* g_snipVis=nullptr; static ImGuiContext* g_ctxSnip=nullptr;
static bool  g_snipActive=false;
static ID3D11ShaderResourceView* g_snipTex=nullptr;   // the frozen screen, for display
static std::vector<uint8_t> g_snipPixels;             // BGRA, top-down, the frozen virtual screen
static int   g_snipW=0,g_snipH=0;                     // its physical pixel size
static ImVec2 g_snipA=V(0,0), g_snipB=V(0,0);         // selection, LOGICAL px in the snip window
static bool  g_snipDrag=false, g_snipHave=false;
// screenshot toast

static void ShowShotToast(bool ok,const std::string& file){
    g_shotOk=ok; g_shotFile=file;
    { size_t s2=file.find_last_of("\\/"); g_shotDir = (s2==std::string::npos)? file : file.substr(0,s2); }
    g_shotToastUntil=GetTickCount64()+5000;
    if(g_medWake) PostMessageW(g_medWake,WM_NULL,0,0);
}

// grab the whole virtual screen with GDI into a top-down 32bpp buffer
static bool CaptureVirtualScreen(){
    int W=g_vs.right-g_vs.left, H=g_vs.bottom-g_vs.top;
    if(W<=0||H<=0) return false;
    HDC screen=GetDC(nullptr); if(!screen) return false;
    HDC mem=CreateCompatibleDC(screen);
    BITMAPINFO bi={}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=W; bi.bmiHeader.biHeight=-H;    // top-down
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP dib=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    bool ok=false;
    if(dib&&bits){
        HGDIOBJ old=SelectObject(mem,dib);
        if(BitBlt(mem,0,0,W,H,screen,g_vs.left,g_vs.top,SRCCOPY|CAPTUREBLT)){
            g_snipPixels.assign((uint8_t*)bits,(uint8_t*)bits+(size_t)W*H*4);
            for(size_t i=3;i<g_snipPixels.size();i+=4) g_snipPixels[i]=255;   // force opaque
            g_snipW=W; g_snipH=H; ok=true;
        }
        SelectObject(mem,old);
    }
    if(dib) DeleteObject(dib);
    DeleteDC(mem); ReleaseDC(nullptr,screen);
    if(ok){ if(g_snipTex){ g_snipTex->Release(); g_snipTex=nullptr; }
            g_snipTex=MakeTextureBGRA(g_snipPixels.data(),g_snipW,g_snipH); }
    return ok && g_snipTex;
}
static const char* g_snipWhy="never started";   // last StartSnip outcome, for -s uidump
static void StartSnip(){
    if(ShellLocked()){ g_snipWhy="refused: locked"; return; }
    if(g_snipActive){ g_snipWhy="refused: already active"; return; }
    if(!CaptureVirtualScreen()){ g_snipWhy="failed: CaptureVirtualScreen"; return; }
    g_snipWhy="started";
    g_snipHave=false; g_snipDrag=false;
    int W=g_vs.right-g_vs.left, H=g_vs.bottom-g_vs.top;
    SetWindowPos(g_snipHwnd,HWND_TOPMOST,g_vs.left,g_vs.top,W,H,SWP_NOACTIVATE);
    ShowWindow(g_snipHwnd,SW_SHOW); SetForegroundWindow(g_snipHwnd); SetActiveWindow(g_snipHwnd);
    g_snipActive=true;
}
static void EndSnip(){
    g_snipActive=false; ShowWindow(g_snipHwnd,SW_HIDE);
    if(g_snipTex){ g_snipTex->Release(); g_snipTex=nullptr; }
    g_snipPixels.clear(); g_snipPixels.shrink_to_fit();
}

// write a BGRA (top-down) crop to a PNG via WIC
static bool SavePng(const std::wstring& path,const uint8_t* bgra,int w,int h){
    IWICImagingFactory* fac=nullptr;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&fac)))) return false;
    bool ok=false; IWICStream* st=nullptr;
    if(SUCCEEDED(fac->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(path.c_str(),GENERIC_WRITE))){
        IWICBitmapEncoder* enc=nullptr;
        if(SUCCEEDED(fac->CreateEncoder(GUID_ContainerFormatPng,nullptr,&enc)) &&
           SUCCEEDED(enc->Initialize(st,WICBitmapEncoderNoCache))){
            IWICBitmapFrameEncode* fr=nullptr; IPropertyBag2* pb=nullptr;
            if(SUCCEEDED(enc->CreateNewFrame(&fr,&pb)) && SUCCEEDED(fr->Initialize(pb))){
                fr->SetSize(w,h);
                WICPixelFormatGUID fmt=GUID_WICPixelFormat32bppBGRA; fr->SetPixelFormat(&fmt);
                if(SUCCEEDED(fr->WritePixels(h,w*4,(UINT)((size_t)w*h*4),(BYTE*)bgra)) &&
                   SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit())) ok=true;
            }
            if(fr)fr->Release(); if(pb)pb->Release();
        }
        if(enc)enc->Release();
    }
    if(st)st->Release(); fac->Release();
    return ok;
}
// copy a BGRA (top-down) crop to the clipboard as a DIB
static void ClipboardDib(const uint8_t* bgra,int w,int h){
    size_t rowsz=(size_t)w*4, sz=sizeof(BITMAPINFOHEADER)+rowsz*h;
    HGLOBAL g=GlobalAlloc(GMEM_MOVEABLE,sz); if(!g) return;
    uint8_t* d=(uint8_t*)GlobalLock(g);
    BITMAPINFOHEADER bih={}; bih.biSize=sizeof(bih); bih.biWidth=w; bih.biHeight=h;   // bottom-up
    bih.biPlanes=1; bih.biBitCount=32; bih.biCompression=BI_RGB; bih.biSizeImage=(DWORD)(rowsz*h);
    memcpy(d,&bih,sizeof(bih));
    uint8_t* dst=d+sizeof(bih);
    for(int y=0;y<h;y++) memcpy(dst+(size_t)(h-1-y)*rowsz, bgra+(size_t)y*rowsz, rowsz);
    GlobalUnlock(g);
    if(OpenClipboard(g_snipHwnd)){ EmptyClipboard(); SetClipboardData(CF_DIB,g); CloseClipboard(); }
    else GlobalFree(g);
}
// crop the physical rect out of the frozen screen and save + copy
static void FinishSnip(int px,int py,int pw,int ph){
    px=std::max(0,px); py=std::max(0,py);
    if(px+pw>g_snipW) pw=g_snipW-px; if(py+ph>g_snipH) ph=g_snipH-py;
    if(pw<2||ph<2){ EndSnip(); return; }
    std::vector<uint8_t> crop((size_t)pw*ph*4);
    for(int y=0;y<ph;y++)
        memcpy(crop.data()+(size_t)y*pw*4, g_snipPixels.data()+(((size_t)(py+y)*g_snipW+px)*4), (size_t)pw*4);
    // Pictures\Screenshots\Screenshot yyyy-MM-dd HHmmss.png
    wchar_t pic[MAX_PATH]={0}; std::wstring dir;
    if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_MYPICTURES,nullptr,0,pic))){ dir=std::wstring(pic)+L"\\Screenshots";
        CreateDirectoryW(dir.c_str(),nullptr); } else dir=L".";
    SYSTEMTIME t; GetLocalTime(&t); wchar_t fn[128];
    swprintf(fn,128,L"\\Screenshot %04d-%02d-%02d %02d%02d%02d.png",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    std::wstring full=dir+fn;
    bool ok=SavePng(full,crop.data(),pw,ph);
    ClipboardDib(crop.data(),pw,ph);
    ShowShotToast(ok, W2U8(full));
    EndSnip();
}

static void DrawSnip(){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float LW=io.DisplaySize.x, LH=io.DisplaySize.y;
    if(!g_snipActive){ return; }
    if(g_snipTex) dl->AddImage((ImTextureID)g_snipTex,V(0,0),V(LW,LH),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,255));
    // dim everything, then punch the selection back to full brightness
    dl->AddRectFilled(V(0,0),V(LW,LH),IM_COL32(10,12,18,150));

    bool down=io.MouseDown[0], click=io.MouseClicked[0], rel=io.MouseReleased[0];
    if(click){ g_snipA=io.MousePos; g_snipB=io.MousePos; g_snipDrag=true; g_snipHave=true; }
    if(g_snipDrag && down) g_snipB=io.MousePos;
    ImVec2 a=V(std::min(g_snipA.x,g_snipB.x),std::min(g_snipA.y,g_snipB.y));
    ImVec2 b=V(std::max(g_snipA.x,g_snipB.x),std::max(g_snipA.y,g_snipB.y));

    if(g_snipHave && b.x-a.x>1 && b.y-a.y>1 && g_snipTex){
        // the crisp selection region, drawn from the texture UVs
        ImVec2 uv0=V(a.x/LW,a.y/LH), uv1=V(b.x/LW,b.y/LH);
        dl->AddImage((ImTextureID)g_snipTex,a,b,uv0,uv1,IM_COL32(255,255,255,255));
        dl->AddRect(a,b,COL_GOLD,0,0,1.6f);
        // grab handles at the corners
        for(ImVec2 h : { a, V(b.x,a.y), V(a.x,b.y), b })
            { dl->AddCircleFilled(h,4.0f,COL_GOLD); dl->AddCircleFilled(h,2.0f,IM_COL32(255,255,255,255)); }
        // size label above the selection
        int pw=(int)((b.x-a.x)*g_uiScale), ph=(int)((b.y-a.y)*g_uiScale);
        char sz[32]; snprintf(sz,32,"%d \xC3\x97 %d",pw,ph);
        float lw=TextW(g_fSml,14,sz)+16, ly=a.y-30; if(ly<4) ly=b.y+8;
        dl->AddRectFilled(V(a.x,ly),V(a.x+lw,ly+24),IM_COL32(20,22,30,235),6);
        TextAt(dl,g_fSml,14,V(a.x+8,ly+4),IM_COL32(240,242,248,255),sz);
    } else {
        // crosshair guides before a drag begins
        dl->AddLine(V(0,io.MousePos.y),V(LW,io.MousePos.y),WithA(COL_GOLD,90),1);
        dl->AddLine(V(io.MousePos.x,0),V(io.MousePos.x,LH),WithA(COL_GOLD,90),1);
        const char* hint="Drag to snip a region \xE2\x80\xA2 Esc to cancel";
        float hw=TextW(g_fMed,17,hint)+40;
        dl->AddRectFilled(V(LW/2-hw/2,24),V(LW/2+hw/2,64),IM_COL32(20,22,30,220),12);
        TextAt(dl,g_fMed,17,V(LW/2-TextW(g_fMed,17,hint)/2,34),IM_COL32(235,238,246,255),hint);
    }

    if(ImGui::IsKeyPressed(ImGuiKey_Escape)){ g_snipDrag=false; EndSnip(); return; }
    if(rel && g_snipDrag){
        g_snipDrag=false;
        if(b.x-a.x>2 && b.y-a.y>2){
            int px=(int)(a.x*g_uiScale), py=(int)(a.y*g_uiScale);
            int pw=(int)((b.x-a.x)*g_uiScale), ph=(int)((b.y-a.y)*g_uiScale);
            FinishSnip(px,py,pw,ph);
        } else EndSnip();
    }
}
