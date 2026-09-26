// Aether - clipboard history.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ================================================================= clipboard history
// The cliphist equivalent. A clipboard format listener is the only way to see copies made by OTHER
// processes - polling GetClipboardSequenceNumber would work but burns a wake-up per frame forever,
// and the listener is a single message when something actually changes.
struct ClipItem { std::wstring text; std::string preview; ULONGLONG at=0; bool pinned=false; };
static std::vector<ClipItem> g_clips;
static std::mutex            g_clipMtx;
static std::vector<int>      g_clipFilt;      // indices into g_clips, filtered by the search box

// One line, whitespace collapsed, cut on a UTF-8 boundary. A row is ~40 glyphs wide; carrying a
// megabyte of copied source into the draw list to then clip it would be silly.
static std::string ClipPreview(const std::wstring& w){
    std::wstring cut = w.size()>400 ? w.substr(0,400) : w;
    std::string sv=W2U8(cut), o; o.reserve(sv.size());
    bool sp=false;
    for(char ch : sv){
        unsigned char u=(unsigned char)ch;
        if(u=='\r'||u=='\n'||u=='\t'||u==' '){ if(!o.empty()&&!sp){ o.push_back(' '); sp=true; } continue; }
        o.push_back(ch); sp=false;
    }
    while(!o.empty()&&o.back()==' ') o.pop_back();
    if(o.size()>200){                       // never split a multi-byte codepoint
        o.resize(200);
        while(!o.empty() && ((unsigned char)o.back()&0xC0)==0x80) o.pop_back();
        if(!o.empty()) o.pop_back();
        o += "\xE2\x80\xA6";
    }
    return o;
}

static void ClipCapture(){
    if(!g_clipEnable) return;
    if(!IsClipboardFormatAvailable(CF_UNICODETEXT)) return;
    // The owner of the clipboard may still be holding it; a failed open is normal, not an error.
    if(!OpenClipboard(g_hwnd)) return;
    std::wstring w;
    if(HANDLE h=GetClipboardData(CF_UNICODETEXT))
        if(const wchar_t* p=(const wchar_t*)GlobalLock(h)){ w=p; GlobalUnlock(h); }
    CloseClipboard();
    if(w.empty()) return;
    if(w.size()>256*1024) w.resize(256*1024);      // a whole file pasted in is not "history"
    { // trailing-whitespace-only copies are noise
      bool any=false; for(wchar_t c : w) if(!iswspace(c)){ any=true; break; }
      if(!any) return; }

    std::lock_guard<std::mutex> lk(g_clipMtx);
    for(size_t i=0;i<g_clips.size();i++)
        if(g_clips[i].text==w){                     // seen before: float it back to the top
            ClipItem it=std::move(g_clips[i]); it.at=GetTickCount64();
            g_clips.erase(g_clips.begin()+i);
            g_clips.insert(g_clips.begin(),std::move(it));
            return; }
    ClipItem it; it.text=w; it.preview=ClipPreview(w); it.at=GetTickCount64();
    g_clips.insert(g_clips.begin(),std::move(it));
    int cap=std::clamp(g_clipMax,10,1000);
    while((int)g_clips.size()>cap){                 // evict oldest UNPINNED
        int victim=-1;
        for(int i=(int)g_clips.size()-1;i>=0;i--) if(!g_clips[i].pinned){ victim=i; break; }
        if(victim<0) break;                         // everything is pinned: let it grow
        g_clips.erase(g_clips.begin()+victim);
    }
}

static void ClipPut(const std::wstring& w){
    if(!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    if(HGLOBAL g=GlobalAlloc(GMEM_MOVEABLE,(w.size()+1)*sizeof(wchar_t))){
        if(void* p=GlobalLock(g)){
            memcpy(p,w.c_str(),(w.size()+1)*sizeof(wchar_t)); GlobalUnlock(g);
            SetClipboardData(CF_UNICODETEXT,g); }
        else GlobalFree(g); }
    CloseClipboard();
}

// Ctrl+V into whatever had focus before the launcher opened. The launcher is WS_EX_NOACTIVATE so
// focus never actually left that window, but the keystroke still has to land AFTER our overlay has
// hidden itself or it goes nowhere - hence the detached beat.
static void ClipSendPaste(){
    HWND target=g_launPrevFg;
    std::thread([target]{
        Sleep(60);
        // Hand focus back first, then paste into it. Without this the keystroke goes to whatever
        // Windows happens to promote after our overlay hides, which is usually nothing.
        if(target && IsWindow(target)) ActivateWindow(target);
        Sleep(140);
        INPUT in[4]={}; for(auto&i:in) i.type=INPUT_KEYBOARD;
        in[0].ki.wVk=VK_CONTROL;
        in[1].ki.wVk='V';
        in[2].ki.wVk='V';          in[2].ki.dwFlags=KEYEVENTF_KEYUP;
        in[3].ki.wVk=VK_CONTROL;   in[3].ki.dwFlags=KEYEVENTF_KEYUP;
        SendInput(4,in,sizeof(INPUT));
    }).detach();
}

static void ClipRebuild(const char* needleRaw){
    std::string needle=needleRaw?needleRaw:"";
    for(auto&ch:needle) ch=(char)tolower((unsigned char)ch);
    std::lock_guard<std::mutex> lk(g_clipMtx);
    g_clipFilt.clear();
    // pinned first, then most recent - the same order the list is drawn in
    std::vector<int> pin, rest;
    for(size_t i=0;i<g_clips.size();i++){
        if(!needle.empty()){
            std::string hay=g_clips[i].preview;
            for(auto&ch:hay) ch=(char)tolower((unsigned char)ch);
            if(hay.find(needle)==std::string::npos) continue; }
        (g_clips[i].pinned?pin:rest).push_back((int)i);
    }
    g_clipFilt=pin; g_clipFilt.insert(g_clipFilt.end(),rest.begin(),rest.end());
}

static std::string ClipAgo(ULONGLONG at){
    ULONGLONG now=GetTickCount64(); if(at>now) return "now";
    unsigned long long s=(now-at)/1000ULL;
    char b[32];
    if(s<10) return "now";
    if(s<60){ snprintf(b,32,"%llus ago",s); return b; }
    if(s<3600){ snprintf(b,32,"%llum ago",s/60); return b; }
    snprintf(b,32,"%lluh ago",s/3600); return b;
}
