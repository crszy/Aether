// Aether - the file manager.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =============================================================================================
// FILE MANAGER — a GNOME Files (Nautilus) look-alike. We replaced explorer.exe, so the desktop
// needs a file browser. Styled as Adwaita (light/dark) with a Places sidebar, breadcrumb pills
// and an icon grid with image thumbnails. Its palette is fixed to Adwaita, independent of the
// shell accent, so it reads EXACTLY like a Linux file manager.
// =============================================================================================
static HWND g_fmHwnd=nullptr; static IDXGISwapChain1* g_fmSc=nullptr; static ID3D11RenderTargetView* g_fmRtv=nullptr;
static IDCompositionTarget* g_fmTgt=nullptr; static IDCompositionVisual* g_fmVis=nullptr; static ImGuiContext* g_ctxFm=nullptr;
static bool  g_fmShow=false; static float g_fmAnim=0.0f; static RECT g_fmRect={0,0,0,0};
struct FEntry { std::wstring name; bool dir; unsigned long long size; FILETIME mtime; };
static std::wstring g_fmPath;
static std::vector<FEntry> g_fmEntries;
static std::vector<std::wstring> g_fmBack, g_fmFwd;
static int  g_fmSel=-1;
static char g_fmFilter[128]={0};
static bool g_fmEditPath=false; static char g_fmPathBuf[520]={0};
static bool g_fmRenaming=false; static char g_fmRenBuf[260]={0};
static ImVec2 g_fmMenuAt=V(0,0); static bool g_fmMenu=false; static int g_fmMenuTarget=-1;
static float g_fmScroll=0;

// Adwaita palette (kept separate from COL_* so the FM always looks like GNOME)
struct AdwPal { ImU32 win, side, head, content, border, sel, hover, ink, ink2, accent, accentInk, folder, folderTab; };
static AdwPal Adw(){
    if(g_darkUI) return AdwPal{
        IM_COL32(0x24,0x24,0x24,255), IM_COL32(0x30,0x30,0x30,255), IM_COL32(0x2d,0x2d,0x2d,255),
        IM_COL32(0x1e,0x1e,0x1e,255), IM_COL32(0x11,0x11,0x11,255), IM_COL32(0x3d,0x84,0xe4,80),
        IM_COL32(0xff,0xff,0xff,16),  IM_COL32(0xff,0xff,0xff,235), IM_COL32(0xbd,0xbd,0xbd,255),
        IM_COL32(0x3d,0x84,0xe4,255), IM_COL32(255,255,255,255),
        IM_COL32(0x62,0x8a,0xc4,255), IM_COL32(0x4d,0x71,0xa8,255) };
    return AdwPal{
        IM_COL32(0xfa,0xfa,0xfa,255), IM_COL32(0xf2,0xf2,0xf2,255), IM_COL32(0xff,0xff,0xff,255),
        IM_COL32(0xff,0xff,0xff,255), IM_COL32(0xdd,0xdd,0xdd,255), IM_COL32(0x35,0x84,0xe4,46),
        IM_COL32(0x00,0x00,0x00,12),  IM_COL32(0x2e,0x34,0x36,255), IM_COL32(0x5e,0x5c,0x64,255),
        IM_COL32(0x35,0x84,0xe4,255), IM_COL32(255,255,255,255),
        IM_COL32(0x83,0xa8,0xd8,255), IM_COL32(0x6a,0x90,0xc0,255) };
}

static bool FmIsImage(const std::wstring& n){
    size_t d=n.find_last_of(L'.'); if(d==std::wstring::npos) return false;
    std::wstring e=n.substr(d); for(auto&c:e)c=towlower(c);
    return e==L".png"||e==L".jpg"||e==L".jpeg"||e==L".gif"||e==L".bmp"||e==L".webp"||e==L".ico";
}
static void FmListDir(){
    g_fmEntries.clear(); g_fmSel=-1; g_fmScroll=0;
    std::wstring q=g_fmPath; if(!q.empty()&&q.back()!=L'\\') q+=L'\\'; q+=L'*';
    WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW(q.c_str(),&fd);
    if(h!=INVALID_HANDLE_VALUE){
        do{
            if(!wcscmp(fd.cFileName,L".")||!wcscmp(fd.cFileName,L"..")) continue;
            if(fd.dwFileAttributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM)) continue;
            if(fd.cFileName[0]==L'.') continue;    // GNOME hides dot-prefixed names
            FEntry e; e.name=fd.cFileName; e.dir=(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
            e.size=((unsigned long long)fd.nFileSizeHigh<<32)|fd.nFileSizeLow; e.mtime=fd.ftLastWriteTime;
            g_fmEntries.push_back(std::move(e));
        } while(FindNextFileW(h,&fd));
        FindClose(h);
    }
    std::sort(g_fmEntries.begin(),g_fmEntries.end(),[](const FEntry&a,const FEntry&b){
        if(a.dir!=b.dir) return a.dir>b.dir;    // folders first
        return _wcsicmp(a.name.c_str(),b.name.c_str())<0; });
}
static void FmGo(const std::wstring& path,bool hist=true){
    if(hist && !g_fmPath.empty()){ g_fmBack.push_back(g_fmPath); g_fmFwd.clear(); }
    g_fmPath=path; g_fmFilter[0]=0; g_fmEditPath=false; FmListDir();
}
static void FmBack(){ if(g_fmBack.empty())return; g_fmFwd.push_back(g_fmPath); g_fmPath=g_fmBack.back(); g_fmBack.pop_back(); FmListDir(); }
static void FmFwd(){ if(g_fmFwd.empty())return; g_fmBack.push_back(g_fmPath); g_fmPath=g_fmFwd.back(); g_fmFwd.pop_back(); FmListDir(); }
static void FmUp(){ std::wstring p=g_fmPath; while(!p.empty()&&p.back()==L'\\')p.pop_back();
    size_t s=p.find_last_of(L'\\'); if(s!=std::wstring::npos && s>=2) FmGo(p.substr(0,s+1)); else if(s==2) FmGo(p.substr(0,3)); }
static void FmOpen(const FEntry& e){
    std::wstring full=g_fmPath; if(!full.empty()&&full.back()!=L'\\')full+=L'\\'; full+=e.name;
    if(e.dir) FmGo(full);
    else AetherShellExec(nullptr,L"open",full.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
}
static std::wstring FmKnown(REFKNOWNFOLDERID id){ PWSTR p=nullptr; std::wstring o;
    if(SUCCEEDED(SHGetKnownFolderPath(id,0,nullptr,&p))&&p){ o=p; CoTaskMemFree(p); } return o; }
static void FmOpenTerminal(){
    // Windows Terminal if present, else PowerShell — "open terminal here"
    { wchar_t wt[MAX_PATH]={0}; ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Microsoft\\WindowsApps\\wt.exe",wt,MAX_PATH);
      if(GetFileAttributesW(wt)!=INVALID_FILE_ATTRIBUTES) AetherShellExec(nullptr,L"open",wt,(L"-d \""+g_fmPath+L"\"").c_str(),nullptr,SW_SHOWNORMAL);
      else AetherShellExec(nullptr,L"open",L"powershell.exe",nullptr,g_fmPath.c_str(),SW_SHOWNORMAL); }
}
static void FmNewFolder(){
    std::wstring base=g_fmPath; if(!base.empty()&&base.back()!=L'\\')base+=L'\\';
    std::wstring name=L"New Folder"; std::wstring full=base+name; int n=2;
    while(GetFileAttributesW(full.c_str())!=INVALID_FILE_ATTRIBUTES){ wchar_t b[64]; swprintf(b,64,L"New Folder %d",n++); name=b; full=base+name; }
    if(CreateDirectoryW(full.c_str(),nullptr)){ FmListDir();
        for(size_t i=0;i<g_fmEntries.size();i++) if(g_fmEntries[i].name==name){ g_fmSel=(int)i;
            g_fmRenaming=true; strncpy(g_fmRenBuf,W2U8(name).c_str(),sizeof(g_fmRenBuf)-1); break; } }
}
static void FmDelete(int idx){
    if(idx<0||idx>=(int)g_fmEntries.size()) return;
    std::wstring full=g_fmPath; if(!full.empty()&&full.back()!=L'\\')full+=L'\\'; full+=g_fmEntries[idx].name;
    full.push_back(L'\0');
    SHFILEOPSTRUCTW op={}; op.wFunc=FO_DELETE; op.pFrom=full.c_str();
    op.fFlags=FOF_ALLOWUNDO|FOF_NOCONFIRMATION|FOF_SILENT;   // send to Recycle Bin
    SHFileOperationW(&op); FmListDir();
}
static void FmRenameCommit(int idx){
    if(idx<0||idx>=(int)g_fmEntries.size()){ g_fmRenaming=false; return; }
    std::wstring nn=U82W(g_fmRenBuf);
    if(!nn.empty() && nn!=g_fmEntries[idx].name){
        std::wstring base=g_fmPath; if(!base.empty()&&base.back()!=L'\\')base+=L'\\';
        MoveFileW((base+g_fmEntries[idx].name).c_str(),(base+nn).c_str());
        FmListDir();
    }
    g_fmRenaming=false;
}
static void FmCopyPath(int idx){
    std::wstring full=g_fmPath; if(idx>=0&&idx<(int)g_fmEntries.size()){ if(!full.empty()&&full.back()!=L'\\')full+=L'\\'; full+=g_fmEntries[idx].name; }
    if(OpenClipboard(g_fmHwnd)){ EmptyClipboard();
        size_t n=(full.size()+1)*sizeof(wchar_t); HGLOBAL g=GlobalAlloc(GMEM_MOVEABLE,n);
        if(g){ memcpy(GlobalLock(g),full.c_str(),n); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT,g); }
        CloseClipboard(); }
}
static void FmStart(){ if(g_fmPath.empty()) FmGo(FmKnown(FOLDERID_Profile),false); g_fmShow=true; }

// GNOME-style folder glyph
// The Adwaita folder: a rounded blue folder with a raised back flap, a lighter top edge and a
// subtle inner highlight — the GNOME 44 look.
static void FmFolderIcon(ImDrawList* dl,ImVec2 c,float s,const AdwPal& A){
    float w=s*1.02f, h=s*0.76f;
    float rnd=s*0.12f;
    ImVec2 a=V(c.x-w/2,c.y-h/2+s*0.08f), b=V(c.x+w/2,c.y+h/2+s*0.08f);
    // back flap (the tab), slightly taller and behind
    ImVec2 t0=V(a.x, a.y-s*0.16f), t1=V(a.x+w*0.46f, a.y+s*0.12f);
    dl->AddRectFilled(t0,t1,A.folderTab,rnd*0.7f);
    dl->AddRectFilled(a,V(b.x,b.y),A.folderTab,rnd);            // back body
    // front panel a hair lower with a lighter fill + a bright top rim
    ImVec2 f0=V(a.x,a.y+s*0.05f), f1=b;
    dl->AddRectFilled(f0,f1,A.folder,rnd);
    dl->AddLine(V(f0.x+rnd,f0.y+1.0f),V(f1.x-rnd,f0.y+1.0f),IM_COL32(255,255,255,70),std::max(1.0f,s*0.03f));
    dl->AddRect(a,b,IM_COL32(0,0,0,g_darkUI?60:34),rnd,0,1.0f);
}
// ---- monochrome symbolic icons for the sidebar (they take the row's text colour, like GNOME) ----
static void SymHome(ImDrawList* dl,ImVec2 c,float s,ImU32 col){
    dl->AddTriangle(V(c.x,c.y-s*0.9f),V(c.x-s,c.y-s*0.05f),V(c.x+s,c.y-s*0.05f),col,1.6f);
    dl->AddRect(V(c.x-s*0.62f,c.y-s*0.1f),V(c.x+s*0.62f,c.y+s*0.85f),col,1.0f,0,1.6f);
    dl->AddRectFilled(V(c.x-s*0.18f,c.y+s*0.25f),V(c.x+s*0.18f,c.y+s*0.85f),col,0.5f);   // door
}
static void SymFolderS(ImDrawList* dl,ImVec2 c,float s,ImU32 col){
    dl->AddRect(V(c.x-s,c.y-s*0.5f),V(c.x+s,c.y+s*0.65f),col,s*0.2f,0,1.6f);
    dl->AddLine(V(c.x-s,c.y-s*0.5f),V(c.x-s*0.1f,c.y-s*0.5f),col,1.6f);
    dl->AddLine(V(c.x-s*0.1f,c.y-s*0.5f),V(c.x+s*0.05f,c.y-s*0.8f),col,1.6f);
    dl->AddLine(V(c.x+s*0.05f,c.y-s*0.8f),V(c.x-s,c.y-s*0.8f),col,1.6f);
    dl->AddLine(V(c.x-s,c.y-s*0.8f),V(c.x-s,c.y-s*0.5f),col,1.6f);
}
static void SymDownload(ImDrawList* dl,ImVec2 c,float s,ImU32 col){
    dl->AddLine(V(c.x,c.y-s*0.8f),V(c.x,c.y+s*0.35f),col,1.8f);
    dl->AddLine(V(c.x-s*0.5f,c.y-s*0.1f),V(c.x,c.y+s*0.4f),col,1.8f);
    dl->AddLine(V(c.x+s*0.5f,c.y-s*0.1f),V(c.x,c.y+s*0.4f),col,1.8f);
    dl->AddLine(V(c.x-s*0.75f,c.y+s*0.8f),V(c.x+s*0.75f,c.y+s*0.8f),col,1.8f);
}
static void SymMusic(ImDrawList* dl,ImVec2 c,float s,ImU32 col){
    dl->AddCircleFilled(V(c.x-s*0.5f,c.y+s*0.55f),s*0.32f,col);
    dl->AddCircleFilled(V(c.x+s*0.6f,c.y+s*0.3f),s*0.32f,col);
    dl->AddLine(V(c.x-s*0.5f+s*0.3f,c.y+s*0.55f),V(c.x-s*0.5f+s*0.3f,c.y-s*0.7f),col,1.6f);
    dl->AddLine(V(c.x+s*0.6f+s*0.3f,c.y+s*0.3f),V(c.x+s*0.6f+s*0.3f,c.y-s*0.95f),col,1.6f);
    dl->AddLine(V(c.x-s*0.5f+s*0.3f,c.y-s*0.7f),V(c.x+s*0.6f+s*0.3f,c.y-0.95f*s),col,1.6f);
}
static void SymImage(ImDrawList* dl,ImVec2 c,float s,ImU32 col){
    dl->AddRect(V(c.x-s,c.y-s*0.8f),V(c.x+s,c.y+s*0.8f),col,s*0.16f,0,1.6f);
    dl->AddCircleFilled(V(c.x-s*0.4f,c.y-s*0.3f),s*0.18f,col);
    dl->AddTriangleFilled(V(c.x-s*0.7f,c.y+s*0.6f),V(c.x+s*0.1f,c.y-s*0.1f),V(c.x+s*0.9f,c.y+s*0.6f),col);
}
static void SymVideo(ImDrawList* dl,ImVec2 c,float s,ImU32 col){
    dl->AddRect(V(c.x-s,c.y-s*0.7f),V(c.x+s*0.35f,c.y+s*0.7f),col,s*0.14f,0,1.6f);
    dl->AddTriangleFilled(V(c.x+s*0.45f,c.y-s*0.45f),V(c.x+s*0.45f,c.y+s*0.45f),V(c.x+s,c.y),col);
}
static void SymTrash(ImDrawList* dl,ImVec2 c,float s,ImU32 col){
    dl->AddLine(V(c.x-s*0.8f,c.y-s*0.55f),V(c.x+s*0.8f,c.y-s*0.55f),col,1.8f);
    dl->AddLine(V(c.x-s*0.35f,c.y-s*0.55f),V(c.x-s*0.25f,c.y-s*0.8f),col,1.6f);
    dl->AddLine(V(c.x-s*0.25f,c.y-s*0.8f),V(c.x+s*0.25f,c.y-s*0.8f),col,1.6f);
    dl->AddLine(V(c.x+s*0.25f,c.y-s*0.8f),V(c.x+s*0.35f,c.y-s*0.55f),col,1.6f);
    dl->AddRect(V(c.x-s*0.6f,c.y-s*0.5f),V(c.x+s*0.6f,c.y+s*0.85f),col,s*0.12f,0,1.6f);
    for(int i=-1;i<2;i++) dl->AddLine(V(c.x+i*s*0.3f,c.y-s*0.3f),V(c.x+i*s*0.3f,c.y+s*0.65f),col,1.3f);
}
static void SymDisk(ImDrawList* dl,ImVec2 c,float s,ImU32 col){
    dl->AddRect(V(c.x-s,c.y-s*0.55f),V(c.x+s,c.y+s*0.55f),col,s*0.2f,0,1.6f);
    dl->AddCircleFilled(V(c.x+s*0.55f,c.y),s*0.13f,col);
}
static void SymClock(ImDrawList* dl,ImVec2 c,float s,ImU32 col){   // Recent
    dl->AddCircle(c,s*0.85f,col,0,1.6f);
    dl->AddLine(c,V(c.x,c.y-s*0.5f),col,1.5f); dl->AddLine(c,V(c.x+s*0.4f,c.y),col,1.5f);
}
static void SymStar(ImDrawList* dl,ImVec2 c,float s,ImU32 col){
    ImVec2 pts[10]; for(int i=0;i<10;i++){ float ang=-1.5708f+i*0.6283f; float r=(i%2)?s*0.42f:s; pts[i]=V(c.x+cosf(ang)*r,c.y+sinf(ang)*r); }
    dl->AddPolyline(pts,10,col,ImDrawFlags_Closed,1.5f);
}
// Full-colour Adwaita icon (folders, mimetypes) by relative path under the icon root; keeps palette.
static bool AdwColor(ImDrawList* dl,ImVec2 c,float boxpx,const char* rel){
    std::string path=g_iconRoot+"\\Adwaita\\"+rel;
    FILE* f=fopen(path.c_str(),"rb"); if(!f) return false; fclose(f);
    SvgTex t=GetSvgIcon(path,(int)(boxpx*2+0.5f),0);
    if(!t.srv) return false;
    float hw=boxpx*0.5f, hh=boxpx*0.5f*(t.h/(float)(t.w?t.w:1));
    dl->AddImage((ImTextureID)t.srv,V(c.x-hw,c.y-hh),V(c.x+hw,c.y+hh));
    return true;
}
static void FmFileIcon(ImDrawList* dl,ImVec2 c,float s,const AdwPal& A){
    float w=s*0.72f, h=s*0.9f; ImVec2 a=V(c.x-w/2,c.y-h/2), b=V(c.x+w/2,c.y+h/2);
    dl->AddRectFilled(a,b, g_darkUI?IM_COL32(0x50,0x50,0x50,255):IM_COL32(0xff,0xff,0xff,255), s*0.05f);
    dl->AddRect(a,b, g_darkUI?IM_COL32(0,0,0,120):IM_COL32(0xc0,0xc0,0xc0,255), s*0.05f,0,1.2f);
    // dog-ear
    dl->AddTriangleFilled(V(b.x-w*0.28f,a.y),V(b.x,a.y),V(b.x,a.y+h*0.24f), g_darkUI?IM_COL32(0x38,0x38,0x38,255):IM_COL32(0xe4,0xe4,0xe4,255));
    for(int i=0;i<3;i++) dl->AddLine(V(a.x+w*0.2f,a.y+h*(0.5f+i*0.14f)),V(b.x-w*0.2f,a.y+h*(0.5f+i*0.14f)),A.ink2,1.0f);
}

static void DrawFileManager(){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    float W=io.DisplaySize.x, H=io.DisplaySize.y;
    float a=std::clamp(g_fmAnim,0.0f,1.0f);
    if(a<0.004f){ g_fmRect=RECT{0,0,0,0}; return; }
    float e=EaseOutCubic(a); int al=(int)(e*255);
    bool click=io.MouseClicked[0] && a>0.5f, dbl=false;
    { static double lastClk=0; static ImVec2 lastPos=V(-99,-99);
      if(click){ double now=ImGui::GetTime();
          if(now-lastClk<0.32 && fabsf(io.MousePos.x-lastPos.x)<6 && fabsf(io.MousePos.y-lastPos.y)<6) dbl=true;
          lastClk=now; lastPos=io.MousePos; } }
    AdwPal A=Adw();
    auto AA=[&](ImU32 c){ return MulA(c,e); };

    // dim backdrop
    dl->AddRectFilled(V(0,0),V(W,H),IM_COL32(0,0,0,(int)(90*e)));

    // window: a floating rounded Adwaita window
    float pw=std::min(W-120,1040.0f), ph=std::min(H-120,660.0f);
    float px=(W-pw)/2, py=(H-ph)/2 + (1.0f-e)*30.0f;
    ImVec2 p0=V(px,py), p1=V(px+pw,py+ph);
    for(int i=10;i>0;i--) dl->AddRectFilled(V(p0.x-i*0.6f,p0.y+i*0.8f),V(p1.x+i*0.6f,p1.y+i*1.0f),IM_COL32(0,0,0,(int)(7*e)),14+i);
    dl->AddRectFilled(p0,p1,AA(A.content),13);
    dl->PushClipRect(p0,p1,true);

    float HB=48;           // header bar height
    float SB=210;          // sidebar width
    ImVec2 sb0=V(px,py+HB), sb1=V(px+SB,py+ph);
    dl->AddRectFilled(V(px,py),V(px+pw,py+HB),AA(A.head),0);         // header bar
    dl->AddRectFilled(sb0,sb1,AA(A.side),0);                        // sidebar
    dl->AddLine(V(px,py+HB),V(px+pw,py+HB),AA(A.border),1);
    dl->AddLine(V(px+SB,py+HB),V(px+SB,py+ph),AA(A.border),1);

    auto iconBtn=[&](ImVec2 c,float r,bool enabled,int id)->bool{
        bool hov=enabled&&fabsf(io.MousePos.x-c.x)<r&&fabsf(io.MousePos.y-c.y)<r;
        if(hov) dl->AddCircleFilled(c,r,AA(A.hover));
        return hov&&click; };

    // ---- header: back / forward / up ----
    float hx=px+24, hcy=py+HB/2;
    { bool en=!g_fmBack.empty(); ImU32 col=AA(en?A.ink:A.ink2);
      if(iconBtn(V(hx,hcy),16,en,1)) FmBack();
      dl->AddLine(V(hx+4,hcy-5),V(hx-4,hcy),col,2.0f); dl->AddLine(V(hx-4,hcy),V(hx+4,hcy+5),col,2.0f); }
    hx+=38;
    { bool en=!g_fmFwd.empty(); ImU32 col=AA(en?A.ink:A.ink2);
      if(iconBtn(V(hx,hcy),16,en,2)) FmFwd();
      dl->AddLine(V(hx-4,hcy-5),V(hx+4,hcy),col,2.0f); dl->AddLine(V(hx+4,hcy),V(hx-4,hcy+5),col,2.0f); }
    hx+=42;

    // ---- breadcrumb pills (or an editable path) ----
    float bcX=hx, bcW=px+pw-360-hx;
    if(g_fmEditPath){
        ImVec2 a0=V(bcX,py+10),a1=V(bcX+bcW,py+HB-10);
        dl->AddRectFilled(a0,a1,AA(A.content),8); dl->AddRect(a0,a1,AA(A.accent),8,0,1.6f);
        for(ImWchar ch:io.InputQueueCharacters){ if(ch>=32&&ch<127){ size_t l=strlen(g_fmPathBuf); if(l<sizeof(g_fmPathBuf)-1){ g_fmPathBuf[l]=(char)ch; g_fmPathBuf[l+1]=0; } } }
        if(ImGui::IsKeyPressed(ImGuiKey_Backspace)){ size_t l=strlen(g_fmPathBuf); if(l)g_fmPathBuf[l-1]=0; }
        if(ImGui::IsKeyPressed(ImGuiKey_Enter)){ std::wstring np=U82W(g_fmPathBuf);
            if(GetFileAttributesW(np.c_str())!=INVALID_FILE_ATTRIBUTES) FmGo(np); g_fmEditPath=false; }
        if(ImGui::IsKeyPressed(ImGuiKey_Escape)) g_fmEditPath=false;
        TextAt(dl,g_fSml,15,V(a0.x+10,a0.y+5),AA(A.ink),g_fmPathBuf);
    } else {
        // split the path into segments
        std::wstring p=g_fmPath; std::vector<std::pair<std::wstring,std::wstring>> segs; // label, fullpath
        { std::wstring acc; size_t i=0;
          while(i<p.size()){ size_t j=p.find(L'\\',i); std::wstring part=(j==std::wstring::npos)?p.substr(i):p.substr(i,j-i);
              if(j==std::wstring::npos) i=p.size(); else i=j+1;
              if(part.empty()) { acc+=L"\\"; continue; }
              acc+=part; if(part.back()!=L'\\') acc+=L"\\";
              segs.push_back({part,acc}); } }
        float bx=bcX; ImVec2 mp=io.MousePos;
        // home chip first
        std::wstring home=FmKnown(FOLDERID_Profile);
        for(size_t k=0;k<segs.size();k++){
            std::string lbl=W2U8(segs[k].first);
            // show the drive as "C:" not "C:"
            bool last=(k+1==segs.size());
            float tw=TextW(g_fSml,15,lbl.c_str()), cw=tw+22;
            if(bx+cw>bcX+bcW) break;
            ImVec2 c0=V(bx,py+11),c1=V(bx+cw,py+HB-11);
            bool hov=mp.x>c0.x&&mp.x<c1.x&&mp.y>c0.y&&mp.y<c1.y;
            if(last) dl->AddRectFilled(c0,c1,AA(A.accent),7);
            else if(hov) dl->AddRectFilled(c0,c1,AA(A.hover),7);
            TextAt(dl,g_fSml,15,V(bx+11,py+15),last?AA(A.accentInk):AA(A.ink),lbl.c_str());
            if(click&&hov&&!last) FmGo(segs[k].second);
            bx+=cw+3;
            if(!last){ TextAt(dl,g_fSml,15,V(bx,py+15),AA(A.ink2),"\xE2\x80\xBA"); bx+=13; }
        }
        // click empty breadcrumb space to edit the path
        if(click && io.MousePos.x>bx && io.MousePos.x<bcX+bcW && io.MousePos.y>py && io.MousePos.y<py+HB){
            g_fmEditPath=true; strncpy(g_fmPathBuf,W2U8(g_fmPath).c_str(),sizeof(g_fmPathBuf)-1); }
    }

    // ---- header right: search box + close ----
    float srX=px+pw-320, srW=250;
    ImVec2 s0=V(srX,py+10),s1=V(srX+srW,py+HB-10);
    dl->AddRectFilled(s0,s1,AA(g_darkUI?IM_COL32(0x1e,0x1e,0x1e,255):IM_COL32(0xf0,0xf0,0xf0,255)),8);
    dl->AddCircle(V(s0.x+16,(s0.y+s1.y)/2),5,AA(A.ink2),0,1.6f);
    dl->AddLine(V(s0.x+20,(s0.y+s1.y)/2+4),V(s0.x+24,(s0.y+s1.y)/2+8),AA(A.ink2),1.6f);
    { bool sh=io.MousePos.x>s0.x&&io.MousePos.x<s1.x&&io.MousePos.y>s0.y&&io.MousePos.y<s1.y;
      static bool sfocus=false; if(click) sfocus=sh;
      if(sfocus){ for(ImWchar ch:io.InputQueueCharacters){ if(ch>=32&&ch<127){ size_t l=strlen(g_fmFilter); if(l<sizeof(g_fmFilter)-1){ g_fmFilter[l]=(char)ch; g_fmFilter[l+1]=0; } } }
          if(ImGui::IsKeyPressed(ImGuiKey_Backspace)){ size_t l=strlen(g_fmFilter); if(l)g_fmFilter[l-1]=0; } }
      const char* ph2 = g_fmFilter[0]? g_fmFilter : "Search";
      TextAt(dl,g_fSml,15,V(s0.x+30,s0.y+5),g_fmFilter[0]?AA(A.ink):AA(A.ink2),ph2); }
    // close
    ImVec2 xc=V(px+pw-24,py+HB/2);
    { bool hov=fabsf(io.MousePos.x-xc.x)<14&&fabsf(io.MousePos.y-xc.y)<14;
      if(hov) dl->AddCircleFilled(xc,14,AA(A.hover));
      dl->AddLine(V(xc.x-5,xc.y-5),V(xc.x+5,xc.y+5),AA(A.ink),1.8f);
      dl->AddLine(V(xc.x+5,xc.y-5),V(xc.x-5,xc.y+5),AA(A.ink),1.8f);
      if(click&&hov) g_fmShow=false; }

    // ---- sidebar: GNOME-style with symbolic per-place icons ----
    float sy=py+HB+10;
    // sym: 0 recent,1 star,2 home,3 folder,4 download,5 music,6 image,7 video,8 trash,9 disk
    auto placeRow=[&](const char* label,const std::wstring& path,int sym){
        ImVec2 r0=V(px+8,sy),r1=V(px+SB-8,sy+32);
        bool cur = !path.empty() && _wcsicmp(path.c_str(),g_fmPath.c_str())==0;
        bool hov=io.MousePos.x>r0.x&&io.MousePos.x<r1.x&&io.MousePos.y>r0.y&&io.MousePos.y<r1.y;
        if(cur) dl->AddRectFilled(r0,r1,AA(A.sel),8); else if(hov) dl->AddRectFilled(r0,r1,AA(A.hover),8);
        ImU32 tc = cur? AA(A.accent) : AA(A.ink);
        ImVec2 ic=V(px+28,sy+16);
        // real Adwaita symbolic art from the ISO, hand-drawn fallback if the SVG is missing
        static const char* NM[]={"document-open-recent","starred","user-home","folder",
            "folder-download","folder-music","folder-pictures","folder-videos","user-trash","drive-harddisk"};
        int si = (sym>=0&&sym<10)? sym : 9;
        if(!AdwSym(dl,ic,9,tc,NM[si])){
          switch(sym){ case 0:SymClock(dl,ic,8,tc);break; case 1:SymStar(dl,ic,8,tc);break;
            case 2:SymHome(dl,ic,8,tc);break; case 3:SymFolderS(dl,ic,8,tc);break;
            case 4:SymDownload(dl,ic,8,tc);break; case 5:SymMusic(dl,ic,7,tc);break;
            case 6:SymImage(dl,ic,8,tc);break; case 7:SymVideo(dl,ic,8,tc);break;
            case 8:SymTrash(dl,ic,8,tc);break; default:SymDisk(dl,ic,8,tc);break; } }
        TextAt(dl,g_fSml,15,V(px+48,sy+7),tc,label);
        if(click&&hov&&!path.empty()) FmGo(path);
        sy+=34;
        return hov&&click;
    };
    // Recent / Starred (open the corresponding shell folders)
    if(placeRow("Recent",FmKnown(FOLDERID_Recent),0)){}
    placeRow("Starred",FmKnown(FOLDERID_Links),1);
    sy+=4;
    placeRow("Home",FmKnown(FOLDERID_Profile),2);
    placeRow("Desktop",FmKnown(FOLDERID_Desktop),3);
    placeRow("Documents",FmKnown(FOLDERID_Documents),3);
    placeRow("Downloads",FmKnown(FOLDERID_Downloads),4);
    placeRow("Music",FmKnown(FOLDERID_Music),5);
    placeRow("Pictures",FmKnown(FOLDERID_Pictures),6);
    placeRow("Videos",FmKnown(FOLDERID_Videos),7);
    sy+=6;
    // Trash -> opens the Recycle Bin
    { ImVec2 r0=V(px+8,sy),r1=V(px+SB-8,sy+32);
      bool hov=io.MousePos.x>r0.x&&io.MousePos.x<r1.x&&io.MousePos.y>r0.y&&io.MousePos.y<r1.y;
      if(hov) dl->AddRectFilled(r0,r1,AA(A.hover),8);
      if(!AdwSym(dl,V(px+28,sy+16),9,AA(A.ink),"user-trash")) SymTrash(dl,V(px+28,sy+16),8,AA(A.ink));
      TextAt(dl,g_fSml,15,V(px+48,sy+7),AA(A.ink),"Trash");
      if(click&&hov) AetherShellExec(nullptr,L"open",L"shell:RecycleBinFolder",nullptr,nullptr,SW_SHOWNORMAL);
      sy+=34; }
    sy+=8; dl->AddLine(V(px+16,sy),V(px+SB-16,sy),AA(A.border),1); sy+=12;
    TextAt(dl,g_fSml,12,V(px+18,sy),AA(A.ink2),"Other Locations"); sy+=24;
    { DWORD drives=GetLogicalDrives();
      for(int i=0;i<26;i++) if(drives&(1<<i)){ wchar_t root[4]={(wchar_t)(L'A'+i),L':',L'\\',0};
          wchar_t vol[64]={0}; DWORD sn=0,ml=0,fl=0;
          GetVolumeInformationW(root,vol,64,&sn,&ml,&fl,nullptr,0);
          char lbl[96]; if(vol[0]) snprintf(lbl,96,"%s (%c:)",W2U8(vol).c_str(),'A'+i);
                        else snprintf(lbl,96,"%c:",'A'+i);
          placeRow(lbl,root,9); } }

    // ---- content: icon grid ----
    ImVec2 c0=V(px+SB+1,py+HB+1), c1=V(px+pw,py+ph);
    dl->PushClipRect(c0,c1,true);
    dl->AddRectFilled(c0,c1,AA(A.content),0);
    float pad=22, cellW=104, cellH=112, gapx=10, gapy=8;
    int cols=std::max(1,(int)((c1.x-c0.x-2*pad+gapx)/(cellW+gapx)));
    float gx0=c0.x+pad, gy0=c0.y+16 - g_fmScroll;
    std::string flt=g_fmFilter; for(auto&ch:flt)ch=(char)tolower(ch);
    int shown=0; float lastY=gy0;
    for(size_t i=0;i<g_fmEntries.size();i++){
        FEntry& en=g_fmEntries[i];
        if(!flt.empty()){ std::string nm=W2U8(en.name); for(auto&ch:nm)ch=(char)tolower(ch); if(nm.find(flt)==std::string::npos) continue; }
        int col=shown%cols, row=shown/cols;
        float cx=gx0+col*(cellW+gapx), cy=gy0+row*(cellH+gapy);
        lastY=cy+cellH;
        shown++;
        if(cy+cellH<c0.y||cy>c1.y) continue;     // cull
        ImVec2 a0=V(cx,cy),a1=V(cx+cellW,cy+cellH);
        bool hov=io.MousePos.x>a0.x&&io.MousePos.x<a1.x&&io.MousePos.y>a0.y&&io.MousePos.y<a1.y
                 && io.MousePos.y>c0.y && io.MousePos.y<c1.y;
        bool sel=((int)i==g_fmSel);
        if(sel) dl->AddRectFilled(a0,a1,AA(A.sel),10);
        else if(hov) dl->AddRectFilled(a0,a1,AA(A.hover),10);
        ImVec2 ic=V(cx+cellW/2,cy+42);
        if(en.dir){ if(!AdwColor(dl,ic,54,"scalable\\places\\folder.svg")) FmFolderIcon(dl,ic,52,A); }
        else if(FmIsImage(en.name)){
            std::wstring full=g_fmPath; if(!full.empty()&&full.back()!=L'\\')full+=L'\\'; full+=en.name;
            ImgAnim* ia=(g_imgCache.size()<160)? GetImg(W2U8(full)) : (g_imgCache.count(W2U8(full))?&g_imgCache[W2U8(full)]:nullptr);
            if(ia&&!ia->frames.empty()){ float bw=56,bh=48, sw=(float)ia->w,sh=(float)ia->h,sc=std::min(bw/sw,bh/sh);
                float iw=sw*sc,ih=sh*sc;
                dl->AddImageRounded((ImTextureID)ia->frames[std::min(ia->frame,(int)ia->frames.size()-1)],
                    V(ic.x-iw/2,ic.y-ih/2),V(ic.x+iw/2,ic.y+ih/2),ImVec2(0,0),ImVec2(1,1),AA(IM_COL32(255,255,255,255)),4); }
            else FmFileIcon(dl,ic,48,A);
        }
        else FmFileIcon(dl,ic,48,A);
        // label (two lines, ellipsized) or a rename box
        if(sel&&g_fmRenaming){
            ImVec2 e0=V(cx+6,cy+80),e1=V(cx+cellW-6,cy+100);
            dl->AddRectFilled(e0,e1,AA(A.content),4); dl->AddRect(e0,e1,AA(A.accent),4,0,1.4f);
            for(ImWchar ch:io.InputQueueCharacters){ if(ch>=32&&ch<127){ size_t l=strlen(g_fmRenBuf); if(l<sizeof(g_fmRenBuf)-1){g_fmRenBuf[l]=(char)ch;g_fmRenBuf[l+1]=0;} } }
            if(ImGui::IsKeyPressed(ImGuiKey_Backspace)){ size_t l=strlen(g_fmRenBuf); if(l)g_fmRenBuf[l-1]=0; }
            if(ImGui::IsKeyPressed(ImGuiKey_Enter)) FmRenameCommit((int)i);
            if(ImGui::IsKeyPressed(ImGuiKey_Escape)) g_fmRenaming=false;
            TextAt(dl,g_fSml,12,V(e0.x+4,e0.y+3),AA(A.ink),Clip(g_fSml,12,g_fmRenBuf,cellW-20).c_str());
        } else {
            std::string nm=W2U8(en.name);
            std::string one=Clip(g_fSml,13,nm,cellW-12);
            TextAt(dl,g_fSml,13,V(cx+cellW/2-TextW(g_fSml,13,one.c_str())/2,cy+82),AA(sel?A.ink:A.ink),one.c_str());
        }
        if(hov&&click){ g_fmSel=(int)i; if(!g_fmRenaming) g_fmRenaming=false; }
        if(hov&&dbl&&!g_fmRenaming){ FmOpen(en); dl->PopClipRect(); dl->PopClipRect(); g_fmRect=RECT{(LONG)px,(LONG)py,(LONG)(px+pw),(LONG)(py+ph)}; return; }
        if(hov&&io.MouseClicked[1]){ g_fmSel=(int)i; g_fmMenu=true; g_fmMenuAt=io.MousePos; g_fmMenuTarget=(int)i; }
    }
    // empty-area right click -> context menu (New folder / terminal)
    { bool inContent=io.MousePos.x>c0.x&&io.MousePos.y>c0.y;
      if(io.MouseClicked[1]&&inContent&&!g_fmMenu){ g_fmMenu=true; g_fmMenuAt=io.MousePos; g_fmMenuTarget=-1; g_fmSel=-1; }
    }
    // scroll
    float contentH = ((shown+cols-1)/cols)*(cellH+gapy)+40;
    float viewH = c1.y-c0.y;
    if(io.MousePos.x>c0.x&&io.MousePos.y>c0.y&&io.MouseWheel!=0) g_fmScroll=std::clamp(g_fmScroll-io.MouseWheel*60,0.0f,std::max(0.0f,contentH-viewH));
    if(g_fmEntries.empty())
        TextAt(dl,g_fMed,17,V((c0.x+c1.x)/2-70,(c0.y+c1.y)/2-10),AA(A.ink2),"This folder is empty");
    dl->PopClipRect();

    // status bar footer
    { char st[64]; snprintf(st,64,"%d item%s",(int)g_fmEntries.size(),g_fmEntries.size()==1?"":"s");
      TextAt(dl,g_fSml,12,V(px+SB+22,py+ph-22),AA(A.ink2),st); }

    // ---- context menu ----
    if(g_fmMenu){
        bool onFile=g_fmMenuTarget>=0;
        std::vector<const char*> items;
        if(onFile){ items={"Open","Rename","Copy path","Move to Trash"}; }
        else { items={"New folder","Open terminal here","Copy path","Refresh"}; }
        float mw=190, rowH=32, mh=rowH*items.size()+10;
        float mx0=g_fmMenuAt.x, my0=g_fmMenuAt.y; if(mx0+mw>px+pw)mx0=px+pw-mw-6; if(my0+mh>py+ph)my0=py+ph-mh-6;
        ImVec2 m0=V(mx0,my0),m1=V(mx0+mw,my0+mh);
        dl->AddRectFilled(V(m0.x+2,m0.y+3),V(m1.x+2,m1.y+3),IM_COL32(0,0,0,50),9);
        dl->AddRectFilled(m0,m1,AA(A.head),9); dl->AddRect(m0,m1,AA(A.border),9,0,1);
        for(size_t k=0;k<items.size();k++){ float ry=m0.y+5+k*rowH;
            bool rh=io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>ry&&io.MousePos.y<ry+rowH;
            if(rh) dl->AddRectFilled(V(m0.x+4,ry),V(m1.x-4,ry+rowH),AA(A.hover),6);
            bool danger=!strcmp(items[k],"Move to Trash");
            TextAt(dl,g_fSml,15,V(m0.x+16,ry+7),danger?AA(IM_COL32(0xe0,0x1b,0x24,255)):AA(A.ink),items[k]);
            if(click&&rh){
                std::string it=items[k];
                if(it=="Open"&&onFile) FmOpen(g_fmEntries[g_fmMenuTarget]);
                else if(it=="Rename"&&onFile){ g_fmSel=g_fmMenuTarget; g_fmRenaming=true; strncpy(g_fmRenBuf,W2U8(g_fmEntries[g_fmMenuTarget].name).c_str(),sizeof(g_fmRenBuf)-1); }
                else if(it=="Copy path") FmCopyPath(g_fmMenuTarget);
                else if(it=="Move to Trash") FmDelete(g_fmMenuTarget);
                else if(it=="New folder") FmNewFolder();
                else if(it=="Open terminal here") FmOpenTerminal();
                else if(it=="Refresh") FmListDir();
                g_fmMenu=false;
            }
        }
        if(click && !(io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>m0.y&&io.MousePos.y<m1.y)) g_fmMenu=false;
    }

    // keyboard: Backspace = up (when not editing), Delete = trash, F2 = rename, Ctrl+L = edit path
    if(!g_fmEditPath && !g_fmRenaming){
        if(ImGui::IsKeyPressed(ImGuiKey_Backspace)) FmUp();
        if(ImGui::IsKeyPressed(ImGuiKey_Delete)&&g_fmSel>=0) FmDelete(g_fmSel);
        if(ImGui::IsKeyPressed(ImGuiKey_F2)&&g_fmSel>=0){ g_fmRenaming=true; strncpy(g_fmRenBuf,W2U8(g_fmEntries[g_fmSel].name).c_str(),sizeof(g_fmRenBuf)-1); }
        if(ImGui::IsKeyPressed(ImGuiKey_Escape)){ if(g_fmMenu)g_fmMenu=false; else g_fmShow=false; }
    }
    dl->PopClipRect();
    g_fmRect = g_fmShow? RECT{0,0,(LONG)W,(LONG)H} : RECT{0,0,0,0};
}
