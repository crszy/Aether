// Aether - window previews (DWM thumbnails).
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =========================================================================================
// Window previews — the DWM thumbnail technique Cairo uses in DwmThumbnail.xaml.cs:
// register the source window against a destination HWND, ask the DWM for the source size,
// then letterbox it into the destination rect preserving aspect ratio.
// The host must be an ordinary redirected window: a NOREDIRECTIONBITMAP/DComp surface has no
// redirection bitmap for the DWM to composite the thumbnail into, so this one is plain GDI.
// =========================================================================================
static HWND        g_thumbHwnd=nullptr;
static HTHUMBNAIL  g_thumb=nullptr;
static HWND        g_thumbSrc=nullptr;
static std::string g_thumbTitle;
static const int   THUMBW=284, THUMBH=190, THUMBPAD=8, THUMBBAR=26;

static LRESULT CALLBACK ThumbProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_PAINT){
        PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps);
        RECT rc; GetClientRect(h,&rc);
        HBRUSH bg=CreateSolidBrush(RGB(18,18,22)); FillRect(dc,&rc,bg); DeleteObject(bg);   // EXACT bar material
        // the thumbnail's letterbox well
        RECT well={THUMBPAD,THUMBPAD+THUMBBAR,rc.right-THUMBPAD,rc.bottom-THUMBPAD};
        HBRUSH wb=CreateSolidBrush(RGB(10,10,13)); FillRect(dc,&well,wb); DeleteObject(wb);
        // hairline ONLY on the desktop-facing edges (top/right/bottom) — NO left border, so it merges
        // seamlessly into the bar instead of reading as a bordered card.
        HPEN pen=CreatePen(PS_SOLID,1,RGB(44,44,52)); HGDIOBJ op=SelectObject(dc,pen);
        MoveToEx(dc,0,0,nullptr); LineTo(dc,rc.right-1,0);                 // top
        LineTo(dc,rc.right-1,rc.bottom-1); LineTo(dc,0,rc.bottom-1);       // right + bottom
        SelectObject(dc,op); DeleteObject(pen);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(226,240,237));
        HFONT f=CreateFontW(15,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        HGDIOBJ of=SelectObject(dc,f);
        std::wstring t=U82W(g_thumbTitle);
        RECT tr={THUMBPAD+2,4,rc.right-THUMBPAD,THUMBPAD+THUMBBAR};
        DrawTextW(dc,t.c_str(),-1,&tr,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        SelectObject(dc,of); DeleteObject(f);
        EndPaint(h,&ps); return 0;
    }
    if(m==WM_NCHITTEST) return HTTRANSPARENT;         // the preview never eats a click
    return DefWindowProcW(h,m,w,l);
}
static void InitThumbHost(HINSTANCE hi){
    WNDCLASSEXW wc={sizeof(wc)}; wc.lpfnWndProc=ThumbProc; wc.hInstance=hi;
    wc.lpszClassName=L"CaelestiaThumb"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    RegisterClassExW(&wc);
    g_thumbHwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,
        L"CaelestiaThumb",L"",WS_POPUP,0,0,THUMBW,THUMBH,nullptr,nullptr,hi,nullptr);
    if(g_thumbHwnd){
        // left corners SQUARE (merge flush with the bar), right corners rounded (face the desktop)
        HRGN r=CreateRoundRectRgn(0,0,THUMBW+1,THUMBH+1,14,14);
        HRGN lf=CreateRectRgn(0,0,16,THUMBH+1); CombineRgn(r,r,lf,RGN_OR); DeleteObject(lf);
        SetWindowRgn(g_thumbHwnd,r,FALSE);
        MARGINS mg={-1,-1,-1,-1}; DwmExtendFrameIntoClientArea(g_thumbHwnd,&mg);
    }
}
