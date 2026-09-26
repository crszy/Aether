// src/modules/dashboard/Terminal.h  —  Aether shell
// The reference rice's Terminal tab: a real shell living in the dashboard. Windows' pseudo console (ConPTY) runs
// the shell (terminal.shell - PowerShell 7 when installed), a small VT parser keeps a character grid with colours,
// and keys typed while the terminal is focused go straight to the shell. Click the terminal to focus it, Esc twice
// (or clicking elsewhere) lets go. Wheel = scrollback, Ctrl+Shift+V / right-click = paste, Ctrl+Shift+C = copy screen.
#pragma once

struct TermCell { uint32_t ch=' '; ImU32 fg=0, bg=0; bool bold=false; };
struct Terminal {
    HPCON pc=nullptr; HANDLE inW=nullptr, outR=nullptr; HANDLE proc=nullptr;
    std::atomic<bool> alive{false};
    std::mutex mtx; std::string pending;                       // bytes from the reader thread
    int cols=0, rows=0;
    std::vector<TermCell> grid;
    std::vector<std::vector<TermCell>> scrollback;
    int cx=0, cy=0, saveX=0, saveY=0; bool cursorOn=true;
    ImU32 fg=0, bg=0; bool bold=false;
    int scrollTop=0, scrollBot=-1;
    std::string esc; int st=0;                                 // parser state: 0 text, 1 ESC, 2 CSI, 3 OSC, 4 charset
    uint32_t u8cp=0; int u8need=0;
    float view=0;                                              // lines scrolled back
    ULONGLONG started=0;
};
static Terminal g_term;

static ImU32 TermColor(int n,bool bright){
    static const ImU32 base[8]={ IM_COL32(40,42,46,255),IM_COL32(230,90,90,255),IM_COL32(120,200,120,255),IM_COL32(225,200,110,255),
                                 IM_COL32(110,160,240,255),IM_COL32(200,130,220,255),IM_COL32(100,200,210,255),IM_COL32(210,212,218,255) };
    static const ImU32 hi[8]  ={ IM_COL32(110,112,120,255),IM_COL32(255,120,120,255),IM_COL32(150,230,150,255),IM_COL32(245,225,140,255),
                                 IM_COL32(140,185,255,255),IM_COL32(225,160,240,255),IM_COL32(130,225,235,255),IM_COL32(245,246,250,255) };
    n=std::clamp(n,0,7); return bright? hi[n] : base[n];
}
static ImU32 Term256(int n){
    if(n<8) return TermColor(n,false); if(n<16) return TermColor(n-8,true);
    if(n<232){ n-=16; int r=n/36, g=(n/6)%6, b=n%6; auto c=[](int v){ return v? 55+v*40 : 0; }; return IM_COL32(c(r),c(g),c(b),255); }
    int v=8+(n-232)*10; return IM_COL32(v,v,v,255);
}
static void TermResize(Terminal& t,int cols,int rows){
    cols=std::clamp(cols,20,400); rows=std::clamp(rows,5,200);
    if(cols==t.cols && rows==t.rows) return;
    std::vector<TermCell> ng((size_t)cols*rows);
    for(int y=0;y<std::min(rows,t.rows);y++) for(int x=0;x<std::min(cols,t.cols);x++) ng[(size_t)y*cols+x]=t.grid[(size_t)y*t.cols+x];
    t.grid.swap(ng); t.cols=cols; t.rows=rows;
    t.cx=std::min(t.cx,cols-1); t.cy=std::min(t.cy,rows-1); t.scrollTop=0; t.scrollBot=-1;
    if(t.pc){ COORD c{(SHORT)cols,(SHORT)rows}; ResizePseudoConsole(t.pc,c); }
}
static std::wstring TermShell(){
    std::string s=Tml::Trim(g_termShell);
    if(!s.empty()) return U82W(s);
    wchar_t b[MAX_PATH]; if(SearchPathW(nullptr,L"pwsh.exe",nullptr,MAX_PATH,b,nullptr)) return std::wstring(L"\"")+b+L"\" -NoLogo";
    return L"powershell.exe -NoLogo";
}
static bool TermStart(Terminal& t,int cols,int rows){
    if(t.alive.load()) return true;
    TermResize(t,cols,rows);
    HANDLE inR=nullptr, outW=nullptr;
    if(!CreatePipe(&inR,&t.inW,nullptr,0)) return false;
    if(!CreatePipe(&t.outR,&outW,nullptr,0)){ CloseHandle(inR); return false; }
    COORD sz{(SHORT)t.cols,(SHORT)t.rows};
    if(FAILED(CreatePseudoConsole(sz,inR,outW,0,&t.pc))){ CloseHandle(inR); CloseHandle(outW); return false; }
    CloseHandle(inR); CloseHandle(outW);
    STARTUPINFOEXW si{}; si.StartupInfo.cb=sizeof(si);
    SIZE_T n=0; InitializeProcThreadAttributeList(nullptr,1,0,&n);
    std::vector<BYTE> buf(n); si.lpAttributeList=(LPPROC_THREAD_ATTRIBUTE_LIST)buf.data();
    InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&n);
    UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,t.pc,sizeof(HPCON),nullptr,nullptr);
    std::wstring cmd=TermShell(); std::vector<wchar_t> cl(cmd.begin(),cmd.end()); cl.push_back(0);
    wchar_t home[MAX_PATH]={0}; GetEnvironmentVariableW(L"USERPROFILE",home,MAX_PATH);
    PROCESS_INFORMATION pi{};
    BOOL ok=CreateProcessW(nullptr,cl.data(),nullptr,nullptr,FALSE,EXTENDED_STARTUPINFO_PRESENT,nullptr,home[0]? home : nullptr,&si.StartupInfo,&pi);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    if(!ok){ ClosePseudoConsole(t.pc); t.pc=nullptr; return false; }
    CloseHandle(pi.hThread); t.proc=pi.hProcess; t.alive=true; t.started=GetTickCount64();
    Terminal* tp=&t;
    std::thread([tp]{
        char b[8192]; DWORD rd=0;
        while(ReadFile(tp->outR,b,sizeof(b),&rd,nullptr) && rd>0){ std::lock_guard<std::mutex> lk(tp->mtx); tp->pending.append(b,rd); if(tp->pending.size()>4000000) tp->pending.erase(0,1000000); }
        tp->alive=false;
    }).detach();
    return true;
}
static void TermStop(Terminal& t){
    if(t.pc){ ClosePseudoConsole(t.pc); t.pc=nullptr; }
    if(t.proc){ TerminateProcess(t.proc,0); CloseHandle(t.proc); t.proc=nullptr; }
    if(t.inW){ CloseHandle(t.inW); t.inW=nullptr; }
    if(t.outR){ CloseHandle(t.outR); t.outR=nullptr; }
    t.alive=false;
}
static void TermWrite(Terminal& t,const std::string& s){ if(!t.inW||s.empty()) return; DWORD w=0; WriteFile(t.inW,s.data(),(DWORD)s.size(),&w,nullptr); }

static TermCell& TCell(Terminal& t,int x,int y){ return t.grid[(size_t)std::clamp(y,0,t.rows-1)*t.cols+std::clamp(x,0,t.cols-1)]; }
static void TermScrollUp(Terminal& t,int n){
    int top=t.scrollTop, bot= t.scrollBot<0? t.rows-1 : std::min(t.scrollBot,t.rows-1);
    for(int k=0;k<n;k++){
        if(top==0){ std::vector<TermCell> line(t.grid.begin(),t.grid.begin()+t.cols); t.scrollback.push_back(line); if(t.scrollback.size()>3000) t.scrollback.erase(t.scrollback.begin()); }
        for(int y=top;y<bot;y++) for(int x=0;x<t.cols;x++) TCell(t,x,y)=TCell(t,x,y+1);
        for(int x=0;x<t.cols;x++) TCell(t,x,bot)=TermCell{' ',0,t.bg,false};
    }
}
static void TermNewline(Terminal& t){ int bot= t.scrollBot<0? t.rows-1 : t.scrollBot; if(t.cy>=bot) TermScrollUp(t,1); else t.cy++; }
static void TermPut(Terminal& t,uint32_t cp){
    if(t.cx>=t.cols){ t.cx=0; TermNewline(t); }
    TermCell& c=TCell(t,t.cx,t.cy); c.ch=cp; c.fg=t.fg; c.bg=t.bg; c.bold=t.bold; t.cx++;
}
static void TermCsi(Terminal& t,const std::string& s){
    if(s.empty()) return;
    char fin=s.back(); std::string body=s.substr(0,s.size()-1);
    bool priv=!body.empty() && (body[0]=='?'||body[0]=='>'||body[0]=='=');
    if(priv) body=body.substr(1);
    std::vector<int> p; { size_t i=0; while(i<=body.size()){ size_t e=body.find(';',i); std::string v=body.substr(i,e==std::string::npos? std::string::npos : e-i); p.push_back(v.empty()? -1 : atoi(v.c_str())); if(e==std::string::npos) break; i=e+1; } }
    auto P=[&](size_t i,int d){ return i<p.size() && p[i]>=0? p[i] : d; };
    if(priv){ if((fin=='h'||fin=='l') && P(0,0)==25) t.cursorOn = fin=='h';
              if((fin=='h'||fin=='l') && (P(0,0)==1049||P(0,0)==47)){ for(auto& c:t.grid) c=TermCell{' ',0,0,false}; t.cx=t.cy=0; }
              return; }
    switch(fin){
    case 'A': t.cy=std::max(0,t.cy-P(0,1)); break;
    case 'B': t.cy=std::min(t.rows-1,t.cy+P(0,1)); break;
    case 'C': t.cx=std::min(t.cols-1,t.cx+P(0,1)); break;
    case 'D': t.cx=std::max(0,t.cx-P(0,1)); break;
    case 'E': t.cx=0; t.cy=std::min(t.rows-1,t.cy+P(0,1)); break;
    case 'F': t.cx=0; t.cy=std::max(0,t.cy-P(0,1)); break;
    case 'G': t.cx=std::clamp(P(0,1)-1,0,t.cols-1); break;
    case 'd': t.cy=std::clamp(P(0,1)-1,0,t.rows-1); break;
    case 'H': case 'f': t.cy=std::clamp(P(0,1)-1,0,t.rows-1); t.cx=std::clamp(P(1,1)-1,0,t.cols-1); break;
    case 'J': { int m=P(0,0);
        auto clr=[&](int x0,int y0,int x1,int y1){ for(int y=y0;y<=y1;y++) for(int x=(y==y0? x0:0); x<=(y==y1? x1:t.cols-1); x++) TCell(t,x,y)=TermCell{' ',0,t.bg,false}; };
        if(m==0) clr(t.cx,t.cy,t.cols-1,t.rows-1); else if(m==1) clr(0,0,t.cx,t.cy); else { clr(0,0,t.cols-1,t.rows-1); if(m==3) t.scrollback.clear(); } } break;
    case 'K': { int m=P(0,0); int x0= m==0? t.cx : 0, x1= m==1? t.cx : t.cols-1;
        for(int x=x0;x<=x1;x++) TCell(t,x,t.cy)=TermCell{' ',0,t.bg,false}; } break;
    case 'X': for(int x=t.cx;x<std::min(t.cols,t.cx+P(0,1));x++) TCell(t,x,t.cy)=TermCell{' ',0,t.bg,false}; break;
    case 'P': { int n=P(0,1); for(int x=t.cx;x<t.cols;x++) TCell(t,x,t.cy)= x+n<t.cols? TCell(t,x+n,t.cy) : TermCell{' ',0,t.bg,false}; } break;
    case '@': { int n=P(0,1); for(int x=t.cols-1;x>=t.cx;x--) TCell(t,x,t.cy)= x-n>=t.cx? TCell(t,x-n,t.cy) : TermCell{' ',0,t.bg,false}; } break;
    case 'S': TermScrollUp(t,P(0,1)); break;
    case 'L': { int n=P(0,1), bot=t.scrollBot<0? t.rows-1 : t.scrollBot; for(int k=0;k<n;k++){ for(int y=bot;y>t.cy;y--) for(int x=0;x<t.cols;x++) TCell(t,x,y)=TCell(t,x,y-1); for(int x=0;x<t.cols;x++) TCell(t,x,t.cy)=TermCell{' ',0,t.bg,false}; } } break;
    case 'M': { int n=P(0,1), bot=t.scrollBot<0? t.rows-1 : t.scrollBot; for(int k=0;k<n;k++){ for(int y=t.cy;y<bot;y++) for(int x=0;x<t.cols;x++) TCell(t,x,y)=TCell(t,x,y+1); for(int x=0;x<t.cols;x++) TCell(t,x,bot)=TermCell{' ',0,t.bg,false}; } } break;
    case 'r': t.scrollTop=std::clamp(P(0,1)-1,0,t.rows-1); t.scrollBot=std::clamp(P(1,t.rows)-1,0,t.rows-1); if(t.scrollTop==0&&t.scrollBot==t.rows-1) t.scrollBot=-1; t.cx=0; t.cy=0; break;
    case 's': t.saveX=t.cx; t.saveY=t.cy; break;
    case 'u': t.cx=t.saveX; t.cy=t.saveY; break;
    case 'm': {
        if(p.empty()) p.push_back(0);
        for(size_t i=0;i<p.size();i++){ int v=p[i]<0? 0 : p[i];
            if(v==0){ t.fg=0; t.bg=0; t.bold=false; }
            else if(v==1) t.bold=true; else if(v==22) t.bold=false;
            else if(v>=30&&v<=37) t.fg=TermColor(v-30,false); else if(v>=90&&v<=97) t.fg=TermColor(v-90,true);
            else if(v>=40&&v<=47) t.bg=TermColor(v-40,false); else if(v>=100&&v<=107) t.bg=TermColor(v-100,true);
            else if(v==39) t.fg=0; else if(v==49) t.bg=0;
            else if((v==38||v==48) && i+1<p.size()){
                ImU32 c=0;
                if(p[i+1]==5 && i+2<p.size()){ c=Term256(std::max(0,p[i+2])); i+=2; }
                else if(p[i+1]==2 && i+4<p.size()){ c=IM_COL32(std::max(0,p[i+2]),std::max(0,p[i+3]),std::max(0,p[i+4]),255); i+=4; }
                if(v==38) t.fg=c; else t.bg=c; }
        } } break;
    default: break;
    }
}
static void TermFeed(Terminal& t){
    std::string in; { std::lock_guard<std::mutex> lk(t.mtx); in.swap(t.pending); }
    if(in.empty()) return;
    t.view=0;
    for(unsigned char b:in){
        if(t.st==1){                                           // after ESC
            if(b=='['){ t.st=2; t.esc.clear(); }
            else if(b==']'){ t.st=3; t.esc.clear(); }
            else if(b=='('||b==')'){ t.st=4; }
            else { if(b=='7'){ t.saveX=t.cx; t.saveY=t.cy; } else if(b=='8'){ t.cx=t.saveX; t.cy=t.saveY; } else if(b=='M'){ if(t.cy>0) t.cy--; } t.st=0; }
            continue; }
        if(t.st==2){ t.esc+=(char)b; if(b>=0x40&&b<=0x7E){ TermCsi(t,t.esc); t.st=0; } else if(t.esc.size()>64) t.st=0; continue; }
        if(t.st==3){ if(b==7){ t.st=0; } else if(b==0x1B){ t.st=5; } continue; }   // OSC: title etc. - ignored
        if(t.st==5){ t.st=0; continue; }                      // ST after OSC
        if(t.st==4){ t.st=0; continue; }
        if(b==0x1B){ t.st=1; continue; }
        if(t.u8need>0){ if((b&0xC0)==0x80){ t.u8cp=(t.u8cp<<6)|(b&0x3F); if(--t.u8need==0) TermPut(t,t.u8cp); continue; } t.u8need=0; }
        if(b>=0xC0){ if(b>=0xF0){ t.u8cp=b&0x07; t.u8need=3; } else if(b>=0xE0){ t.u8cp=b&0x0F; t.u8need=2; } else { t.u8cp=b&0x1F; t.u8need=1; } continue; }
        switch(b){
        case '\r': t.cx=0; break;
        case '\n': TermNewline(t); break;
        case '\b': if(t.cx>0) t.cx--; break;
        case '\t': t.cx=std::min(t.cols-1,(t.cx/8+1)*8); break;
        case 7: break;
        default: if(b>=32) TermPut(t,b); break;
        }
    }
}

static void DrawTerminalCard(ImDrawList* dl,ImGuiIO& io,ImVec2 o,ImVec2 s,bool card){
    if(card) Card(dl,o,s);
    Terminal& t=g_term;
    ImFont* f=g_fMono? g_fMono : g_fReg;
    float fs=std::clamp(g_termFont,9.0f,28.0f);
    float cw=f->CalcTextSizeA(fs*g_textScale,FLT_MAX,0,"M").x, lh=fs*1.25f*g_textScale;
    const float pad=12;
    int cols=std::max(20,(int)((s.x-pad*2)/std::max(1.0f,cw))), rows=std::max(5,(int)((s.y-pad*2)/lh));
    if(!t.alive.load()){
        if(t.proc){ TermStop(t); }
        static ULONGLONG lastTry=0;
        if(g_wInteractive && GetTickCount64()-lastTry>1500){ lastTry=GetTickCount64(); TermStart(t,cols,rows); }
    } else TermResize(t,cols,rows);
    TermFeed(t);
    bool inside=io.MousePos.x>=o.x&&io.MousePos.x<o.x+s.x&&io.MousePos.y>=o.y&&io.MousePos.y<o.y+s.y;
    if(g_wInteractive && io.MouseClicked[0]) g_termFocus=inside;
    if(!g_wInteractive) g_termFocus=false;
    // wheel = scrollback
    if(inside && io.MouseWheel!=0) t.view=std::clamp(t.view+io.MouseWheel*3.0f,0.0f,(float)t.scrollback.size());
    // keys
    if(g_termFocus && t.alive.load()){
        std::string out;
        for(int k=0;k<io.InputQueueCharacters.Size;k++){ unsigned c=io.InputQueueCharacters[k]; if(c<32 || (c>=0xD800&&c<=0xDFFF)) continue;
            if(io.KeyCtrl && !io.KeyAlt) continue;
            if(c<0x80) out+=(char)c; else if(c<0x800){ out+=(char)(0xC0|(c>>6)); out+=(char)(0x80|(c&0x3F)); }
            else { out+=(char)(0xE0|(c>>12)); out+=(char)(0x80|((c>>6)&0x3F)); out+=(char)(0x80|(c&0x3F)); } }
        auto key=[&](ImGuiKey k){ return ImGui::IsKeyPressed(k,true); };
        if(key(ImGuiKey_Enter)||key(ImGuiKey_KeypadEnter)) out+="\r";
        if(key(ImGuiKey_Backspace)) out+= io.KeyCtrl? "\x17" : "\x7f";
        if(key(ImGuiKey_Tab)) out+="\t";
        if(key(ImGuiKey_UpArrow)) out+="\x1b[A"; if(key(ImGuiKey_DownArrow)) out+="\x1b[B";
        if(key(ImGuiKey_RightArrow)) out+= io.KeyCtrl? "\x1b[1;5C" : "\x1b[C"; if(key(ImGuiKey_LeftArrow)) out+= io.KeyCtrl? "\x1b[1;5D" : "\x1b[D";
        if(key(ImGuiKey_Home)) out+="\x1b[H"; if(key(ImGuiKey_End)) out+="\x1b[F";
        if(key(ImGuiKey_Delete)) out+="\x1b[3~"; if(key(ImGuiKey_PageUp)) out+="\x1b[5~"; if(key(ImGuiKey_PageDown)) out+="\x1b[6~";
        static ULONGLONG lastEsc=0;
        if(key(ImGuiKey_Escape)){ if(GetTickCount64()-lastEsc<400) g_termFocus=false; else out+="\x1b"; lastEsc=GetTickCount64(); }
        if(io.KeyCtrl && !io.KeyShift){ for(int L=0;L<26;L++) if(ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_A+L),false)) out+=(char)(L+1); }
        bool paste = (io.KeyCtrl&&io.KeyShift&&ImGui::IsKeyPressed(ImGuiKey_V,false)) || (inside&&io.MouseClicked[1]);
        if(paste && OpenClipboard(nullptr)){ HANDLE h=GetClipboardData(CF_UNICODETEXT); if(h){ wchar_t* w=(wchar_t*)GlobalLock(h); if(w){ std::string u=W2U8(w); for(auto& ch:u) if(ch=='\n') ch='\r'; out+=u; } GlobalUnlock(h); } CloseClipboard(); }
        if(io.KeyCtrl&&io.KeyShift&&ImGui::IsKeyPressed(ImGuiKey_C,false)){
            std::string all; for(int y=0;y<t.rows;y++){ std::string line; for(int x=0;x<t.cols;x++){ uint32_t c=TCell(t,x,y).ch; if(c<0x80) line+=(char)c; else line+='?'; }
                while(!line.empty()&&line.back()==' ') line.pop_back(); all+=line+"\r\n"; }
            std::wstring w=U82W(all);
            if(OpenClipboard(nullptr)){ EmptyClipboard(); HGLOBAL g=GlobalAlloc(GMEM_MOVEABLE,(w.size()+1)*sizeof(wchar_t)); if(g){ memcpy(GlobalLock(g),w.c_str(),(w.size()+1)*sizeof(wchar_t)); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT,g); } CloseClipboard(); } }
        if(!out.empty()){ TermWrite(t,out); t.view=0; }
    }
    // draw
    ImU32 defFg=COL_INK, defBg=0;
    dl->PushClipRect(V(o.x+4,o.y+4),V(o.x+s.x-4,o.y+s.y-4),true);
    int back=(int)t.view;
    char u8[5];
    for(int r=0;r<t.rows;r++){
        const TermCell* line=nullptr; std::vector<TermCell>* sb=nullptr;
        int srcRow=r-back;
        if(srcRow<0){ int si=(int)t.scrollback.size()+srcRow; if(si<0) continue; sb=&t.scrollback[si]; }
        float y=o.y+pad+r*lh;
        int ncols = sb? (int)sb->size() : t.cols;
        for(int x=0;x<std::min(ncols,t.cols);x++){
            const TermCell& c = sb? (*sb)[x] : TCell(t,x,srcRow);
            float px=o.x+pad+x*cw;
            if(c.bg) dl->AddRectFilled(V(px,y),V(px+cw+0.5f,y+lh),c.bg);
            if(c.ch>32){
                uint32_t cp=c.ch; int n=0;
                if(cp<0x80) u8[n++]=(char)cp; else if(cp<0x800){ u8[n++]=(char)(0xC0|(cp>>6)); u8[n++]=(char)(0x80|(cp&0x3F)); }
                else if(cp<0x10000){ u8[n++]=(char)(0xE0|(cp>>12)); u8[n++]=(char)(0x80|((cp>>6)&0x3F)); u8[n++]=(char)(0x80|(cp&0x3F)); }
                else { u8[n++]='?'; }
                u8[n]=0;
                ImU32 fc=c.fg? c.fg : defFg; if(c.bold && !c.fg) fc=Mix(COL_INK,COL_GOLD,0.3f);
                dl->AddText(f,fs*g_textScale,V(px,y),fc,u8,u8+n);
            }
        }
        (void)line;
    }
    if(t.cursorOn && back==0 && t.alive.load()){
        float px=o.x+pad+t.cx*cw, y=o.y+pad+t.cy*lh;
        bool blink=fmodf((float)GetTickCount64()/530.0f,2.0f)<1.0f;
        if(g_termFocus){ if(blink) dl->AddRectFilled(V(px,y),V(px+cw,y+lh),WithA(COL_INK,200)); }
        else dl->AddRect(V(px,y),V(px+cw,y+lh),WithA(COL_INK2,160),0,0,1.2f);
    }
    if(!t.alive.load() && t.started){ const char* m="The shell exited - click to start a new one";
        TextAt(dl,g_fSml,14,V(o.x+s.x*0.5f-TextW(g_fSml,14,m)*0.5f,o.y+s.y-34),COL_INK2,m);
        if(inside&&io.MouseClicked[0]&&g_wInteractive){ TermStop(t); t.started=0; for(auto& c:t.grid) c=TermCell{}; t.cx=t.cy=0; } }
    if(t.view>0){ char b[32]; snprintf(b,32,"%d lines up",(int)t.view); TextAt(dl,g_fSml,12,V(o.x+s.x-pad-TextW(g_fSml,12,b),o.y+6),COL_INK2,b); }
    dl->PopClipRect();
    if(g_termFocus) g_drawerForceUntil=std::max(g_drawerForceUntil,GetTickCount64()+600);
}
