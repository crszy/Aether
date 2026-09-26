// src/modules/bar/BarCustom.h  —  Aether shell
// Your own items on the bar.
//
// The bar is already an ordered, reorderable list of items with flexible spacers and grouping pills
// - the same model waybar and polybar use - but every item in it was one the shell knew how to draw.
// These six are the ones it does not: what they say comes from a config file, or from the output of
// a command, and clicking one runs whatever you tell it to.
//
// They are ORDINARY bar items. They appear in the Settings list, they can be dragged anywhere in the
// order, they land inside a grouping pill if you put them next to one, and they hide and show like
// everything else. Nothing about the layout knows they are special.
//
// Defined in config\baritems.toml, one [itemN] section per slot:
//
//     [item1]
//     icon     = "memory"                 # a Material Symbols name, optional
//     text     = "hello"                  # literal text - or leave it out and use `command`
//     command  = "powershell -c ..."      # its first line of output becomes the text
//     interval = 5                        # seconds between runs (minimum 1)
//     click    = "notepad"                # run this when the item is clicked
//     colour   = "#89b4fa"                # optional; defaults to the bar's text colour
//     tooltip  = "..."                    # optional hover text
//
// A command runs on its own thread, never on the render thread, and only one run of a given item is
// ever in flight - a command that takes longer than its interval simply refreshes less often instead
// of piling up.
#pragma once

#define BAR_CUSTOM_N 6

struct BarCI {
    std::string icon, text, cmd, click, tooltip;
    int         interval = 5;
    bool        hasCol = false;
    ImU32       col = 0;
    bool        defined = false;                       // the config file mentions this slot
    std::shared_ptr<std::string>       out;            // last stdout (guarded by g_barCIMtx)
    std::shared_ptr<std::atomic<bool>> busy;
    ULONGLONG   lastRun = 0;
};
static BarCI      g_barCI[BAR_CUSTOM_N];
static std::mutex g_barCIMtx;

static void BarCustomWriteExample(){
    std::string path=CfgDir()+"baritems.toml";
    if(GetFileAttributesA(path.c_str())!=INVALID_FILE_ATTRIBUTES) return;
    FILE* f=fopen(path.c_str(),"wb"); if(!f) return;
    fputs("# ==============================================================================\n"
          "# Aether - your own bar items\n"
          "#\n"
          "# Six slots. Fill one in, then turn \"Custom 1\" (or 2, 3...) on in\n"
          "# Settings > Taskbar > Bar items and drag it where you want it.\n"
          "#\n"
          "#   icon      a Material Symbols name (memory, thermostat, cloud, folder, ...)\n"
          "#   text      literal text; leave it out if you are using `command`\n"
          "#   command   run this, and use its first line of output as the text\n"
          "#   interval  seconds between runs of `command` (minimum 1)\n"
          "#   click     run this when the item is clicked\n"
          "#   colour    #rrggbb; defaults to the bar's own text colour\n"
          "#   tooltip   hover text\n"
          "#\n"
          "# Everything is optional. An item with nothing in it simply does not appear.\n"
          "# ==============================================================================\n"
          "\n"
          "# A worked example: the machine's uptime, refreshed every half minute.\n"
          "# [item1]\n"
          "# icon     = \"schedule\"\n"
          "# command  = \"powershell -nop -c \\\"(New-TimeSpan -Start (gcim Win32_OperatingSystem).LastBootUpTime).ToString('d\\\\.hh\\\\:mm')\\\"\"\n"
          "# interval = 30\n"
          "# tooltip  = \"Uptime\"\n"
          "\n"
          "# [item2]\n"
          "# icon   = \"folder\"\n"
          "# text   = \"Projects\"\n"
          "# click  = \"explorer Z:\\\\\"\n"
          , f);
    fclose(f);
}

static void BarCustomLoad(){
    BarCustomWriteExample();
    Tml::File f; bool ok=f.load(CfgDir()+"baritems.toml");
    std::lock_guard<std::mutex> lk(g_barCIMtx);
    for(int i=0;i<BAR_CUSTOM_N;i++){
        BarCI& c=g_barCI[i];
        // The shared out/busy survive a reload: a command mid-flight writes through them, and
        // swapping them out from under it would drop the result (or worse, write into a dead one).
        if(!c.out)  c.out  = std::make_shared<std::string>();
        if(!c.busy) c.busy = std::make_shared<std::atomic<bool>>(false);
        char key[32]; snprintf(key,sizeof(key),"item%d",i+1);
        std::string p=key;
        c.icon=ok? f.str((p+".icon").c_str(),"") : "";
        c.text=ok? f.str((p+".text").c_str(),"") : "";
        c.cmd =ok? f.str((p+".command").c_str(),"") : "";
        c.click=ok? f.str((p+".click").c_str(),"") : "";
        c.tooltip=ok? f.str((p+".tooltip").c_str(),"") : "";
        c.interval=ok? std::max(1,(int)f.num((p+".interval").c_str(),5)) : 5;
        c.hasCol=false;
        if(ok && f.has((p+".colour").c_str())){
            std::string cs=f.str((p+".colour").c_str(),"");
            unsigned r=0,g=0,b=0;
            if(cs.size()>=7 && cs[0]=='#' && sscanf(cs.c_str()+1,"%02x%02x%02x",&r,&g,&b)==3){
                c.col=IM_COL32(r,g,b,255); c.hasCol=true; }
        }
        c.defined = !c.icon.empty() || !c.text.empty() || !c.cmd.empty();
        c.lastRun = 0;                                  // re-read config = run it again now
    }
}

// One run of one item, off the render thread. Mirrors the dashboard widgets' source runner.
static void BarCustomRun(int i){
    BarCI& c=g_barCI[i];
    if(c.cmd.empty() || !c.busy || c.busy->exchange(true)) return;
    auto out=c.out; auto busy=c.busy; std::wstring cmd=L"cmd.exe /c "+U82W(c.cmd);
    std::thread([cmd,out,busy]{
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE}; HANDLE rd=nullptr,wr=nullptr;
        if(CreatePipe(&rd,&wr,&sa,1<<16)){
            SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0);
            STARTUPINFOW si{}; si.cb=sizeof(si); si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
            si.hStdOutput=wr; si.hStdError=wr; PROCESS_INFORMATION pi{};
            std::vector<wchar_t> b(cmd.begin(),cmd.end()); b.push_back(0);
            if(CreateProcessW(nullptr,b.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
                CloseHandle(wr); wr=nullptr; std::string acc; char buf[2048]; DWORD n=0;
                while(ReadFile(rd,buf,sizeof(buf),&n,nullptr) && n>0 && acc.size()<8192) acc.append(buf,n);
                WaitForSingleObject(pi.hProcess,8000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                // one line is all a bar item can show
                size_t nl=acc.find_first_of("\r\n"); if(nl!=std::string::npos) acc.resize(nl);
                while(!acc.empty() && (acc.back()==' '||acc.back()=='\t')) acc.pop_back();
                std::lock_guard<std::mutex> lk(g_barCIMtx); *out=acc;
            }
            if(wr) CloseHandle(wr); CloseHandle(rd);
        }
        busy->store(false);
    }).detach();
}
// Called once a frame from the bar. Cheap: a compare per slot.
static void BarCustomTick(){
    ULONGLONG now=GetTickCount64();
    for(int i=0;i<BAR_CUSTOM_N;i++){
        BarCI& c=g_barCI[i];
        if(!c.defined || c.cmd.empty()) continue;
        if(c.lastRun && now-c.lastRun < (ULONGLONG)c.interval*1000) continue;
        c.lastRun=now; BarCustomRun(i);
    }
}
static bool BarCustomOn(int i){ return i>=0 && i<BAR_CUSTOM_N && g_barCI[i].defined; }
// What the item currently says: the command's output if it has one, else the literal text.
static std::string BarCustomText(int i){
    if(i<0||i>=BAR_CUSTOM_N) return "";
    BarCI& c=g_barCI[i];
    if(!c.cmd.empty()){ std::lock_guard<std::mutex> lk(g_barCIMtx); if(c.out && !c.out->empty()) return *c.out; }
    return c.text;
}
static const char* BarCustomTip(int i){
    if(i<0||i>=BAR_CUSTOM_N || g_barCI[i].tooltip.empty()) return nullptr;
    return g_barCI[i].tooltip.c_str();
}
// Width on a horizontal bar: icon, gap, text - whichever of them exist.
static float BarCustomExtH(int i,float fs){
    if(!BarCustomOn(i)) return 0.0f;
    std::string t=BarCustomText(i);
    float w=10.0f;
    if(!g_barCI[i].icon.empty()) w+=fs*1.25f;
    if(!t.empty()) w+=TextW(g_fSml,fs,t.c_str())+(g_barCI[i].icon.empty()?0.0f:5.0f);
    return std::max(w,fs);
}
static void BarCustomDrawH(ImDrawList* dl,int i,float x,float cy,float fs,ImU32 ink,float hoverA,float half){
    if(!BarCustomOn(i)) return;
    BarCI& c=g_barCI[i];
    float w=BarCustomExtH(i,fs);
    if(hoverA>0.01f) dl->AddRectFilled(V(x-2,cy-half),V(x+w+2,cy+half),WithA(COL_INK2,(int)(hoverA*40)),half*0.55f);
    ImU32 col = c.hasCol? WithA(c.col,(int)(((ink>>IM_COL32_A_SHIFT)&0xFF))) : ink;
    float cx=x+5;
    if(!c.icon.empty()){ MsIcon(dl,c.icon.c_str(),V(cx+fs*0.6f,cy),fs*1.15f,col); cx+=fs*1.25f; }
    std::string t=BarCustomText(i);
    if(!t.empty()){ if(!c.icon.empty()) cx+=5.0f; TextAt(dl,g_fSml,fs,V(cx,cy-fs*0.62f),col,t.c_str()); }
}
// A side strip is too narrow for a label, so a vertical item is its icon - and its text only when
// that text is short enough to be read in a column (a percentage, a temperature).
static void BarCustomDrawV(ImDrawList* dl,int i,float cx,float cyy,float fs,ImU32 ink,float hoverA,float half){
    if(!BarCustomOn(i)) return;
    BarCI& c=g_barCI[i];
    if(hoverA>0.01f) dl->AddCircleFilled(V(cx,cyy),half,WithA(COL_INK2,(int)(hoverA*40)));
    ImU32 col = c.hasCol? WithA(c.col,(int)(((ink>>IM_COL32_A_SHIFT)&0xFF))) : ink;
    std::string t=BarCustomText(i);
    if(!c.icon.empty()){
        MsIcon(dl,c.icon.c_str(),V(cx,cyy-(t.size()&&t.size()<=4?fs*0.42f:0.0f)),fs*1.2f,col);
        if(!t.empty() && t.size()<=4) TextAt(dl,g_fSml,fs*0.80f,V(cx-TextW(g_fSml,fs*0.80f,t.c_str())*0.5f,cyy+fs*0.10f),col,t.c_str());
    } else if(!t.empty()){
        std::string s=t.size()<=4? t : t.substr(0,4);
        TextAt(dl,g_fSml,fs*0.85f,V(cx-TextW(g_fSml,fs*0.85f,s.c_str())*0.5f,cyy-fs*0.45f),col,s.c_str());
    }
}
static void BarCustomClick(int i){
    if(!BarCustomOn(i) || g_barCI[i].click.empty()) return;
    // Through cmd, so anything you could type works - a program, a path, a shell line. SW_HIDE both
    // keeps the console from flashing and marks it a background launch, so a failure logs instead of
    // throwing a popup at someone who clicked a bar item.
    std::wstring args=L"/c "+U82W(g_barCI[i].click);
    AetherShellExec(nullptr,nullptr,L"cmd.exe",args.c_str(),nullptr,SW_HIDE);
}
