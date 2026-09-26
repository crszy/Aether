// A window that is SLOW but never HUNG - the case that actually costs the shell money.
//
// Windows flags a window "hung" only if its thread stops calling GetMessage/PeekMessage for ~5 s,
// and SMTO_ABORTIFHUNG then returns instantly, so a fully-wedged app is cheap. The expensive one
// pumps just often enough to stay unflagged while spending most of its time not pumping: every
// inter-thread SEND that lands in a busy stretch burns its full timeout. A DAW mid-render, an
// installer, a game loading a level.
//
// usage: slowwin.exe <seconds> <windows> [busy_ms]
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static LRESULT CALLBACK WP(HWND h,UINT m,WPARAM w,LPARAM l){ return DefWindowProcW(h,m,w,l); }

int main(int argc,char** argv){
    int secs   = argc>1 ? atoi(argv[1]) : 40;
    int n      = argc>2 ? atoi(argv[2]) : 10;
    int busyMs = argc>3 ? atoi(argv[3]) : 400;   // < 5000 so we are never flagged hung

    WNDCLASSEXW wc={sizeof(wc)};
    wc.lpfnWndProc=WP; wc.hInstance=GetModuleHandleW(NULL); wc.lpszClassName=L"AetherSlowWin";
    wc.hCursor=LoadCursorW(NULL,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    RegisterClassExW(&wc);

    for(int i=0;i<n;i++){
        wchar_t t[64]; swprintf(t,64,L"AETHER SLOW %d",i);
        HWND h=CreateWindowExW(0,wc.lpszClassName,t,WS_OVERLAPPEDWINDOW,
                               60+i*28,60+i*28,420,300,NULL,NULL,wc.hInstance,NULL);
        ShowWindow(h,SW_SHOWNOACTIVATE);
        if(getenv("SLOWWIN_MIN")) ShowWindow(h,SW_MINIMIZE);
    }
    printf("%d slow windows up, busy %d ms between pumps, for %d s\n",n,busyMs,secs);
    fflush(stdout);

    ULONGLONG end=GetTickCount64()+(ULONGLONG)secs*1000;
    while(GetTickCount64()<end){
        MSG msg;                                   // one short pump: keeps us off the hung list
        while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){ TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(busyMs);                             // and now we are "busy": nothing gets answered
    }
    return 0;
}
