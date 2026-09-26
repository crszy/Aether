// Aether - Lua plugins.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ===================================================================== LUA PLUGINS (layer 4)
// User plugins are Lua, not native DLLs: a bad plugin must not be able to kill the shell — which
// matters more than usual now that the shell IS the desktop. Each plugin gets its OWN lua_State
// (isolation), a trimmed standard library (no io/package/os.execute/debug), an instruction-count
// hook so a runaway loop errors out instead of freezing the frame, and every callback runs under
// lua_pcall. A plugin that throws is disabled with its message kept, and re-enabled when its file
// changes on disk (hot reload). Native DLL plugins are deliberately NOT offered.
struct LuaPlugin {
    std::string file, name, err, log;
    lua_State* L=nullptr;
    int   surface=PSURF_DESKTOP;
    float x=40,y=40,w=240,h=110;     // desktop: position+size. bar: h is the slot height
    bool  enabled=true, ok=false;
    FILETIME mtime{};
};
static std::vector<LuaPlugin> g_plugins;
static ULONGLONG g_plNextScan=0;

// per-call drawing context, so the bound functions know where they may paint
static ImDrawList* g_plDL=nullptr;
static float g_plX=0,g_plY=0,g_plW=0,g_plH=0;
static LuaPlugin* g_plCur=nullptr;

static std::string PluginDir(){ return ExeDir()+"plugins\\"; }
static bool PluginsActive(int surface){
    if(!g_pluginsOn) return false;
    for(auto& p:g_plugins) if(p.enabled&&p.ok&&p.surface==surface) return true;
    return false;
}
// ---- bound API: everything a plugin can touch ----
static ImU32 lcolor(lua_State* L,int i){ return (ImU32)(uint32_t)luaL_optnumber(L,i,0xFFFFFFFF); }
static int l_color(lua_State* L){
    int r=(int)luaL_checkinteger(L,1),g=(int)luaL_checkinteger(L,2),b=(int)luaL_checkinteger(L,3);
    int a=(int)luaL_optinteger(L,4,255);
    lua_pushnumber(L,(lua_Number)IM_COL32(r,g,b,a)); return 1; }
static int l_accent(lua_State* L){ lua_pushnumber(L,(lua_Number)COL_GOLD); return 1; }
static int l_ink   (lua_State* L){ lua_pushnumber(L,(lua_Number)COL_INK);  return 1; }
static int l_ink2  (lua_State* L){ lua_pushnumber(L,(lua_Number)COL_INK2); return 1; }
static int l_card  (lua_State* L){ lua_pushnumber(L,(lua_Number)COL_CARD); return 1; }
static int l_rectf(lua_State* L){
    if(!g_plDL) return 0;
    float x=(float)luaL_checknumber(L,1),y=(float)luaL_checknumber(L,2);
    float w=(float)luaL_checknumber(L,3),h=(float)luaL_checknumber(L,4);
    g_plDL->AddRectFilled(V(x,y),V(x+w,y+h),lcolor(L,5),(float)luaL_optnumber(L,6,0)); return 0; }
static int l_rect(lua_State* L){
    if(!g_plDL) return 0;
    float x=(float)luaL_checknumber(L,1),y=(float)luaL_checknumber(L,2);
    float w=(float)luaL_checknumber(L,3),h=(float)luaL_checknumber(L,4);
    g_plDL->AddRect(V(x,y),V(x+w,y+h),lcolor(L,5),(float)luaL_optnumber(L,6,0),0,
                    (float)luaL_optnumber(L,7,1.5)); return 0; }
static int l_line(lua_State* L){
    if(!g_plDL) return 0;
    g_plDL->AddLine(V((float)luaL_checknumber(L,1),(float)luaL_checknumber(L,2)),
                    V((float)luaL_checknumber(L,3),(float)luaL_checknumber(L,4)),
                    lcolor(L,5),(float)luaL_optnumber(L,6,1.5)); return 0; }
static int l_circle(lua_State* L){
    if(!g_plDL) return 0;
    g_plDL->AddCircle(V((float)luaL_checknumber(L,1),(float)luaL_checknumber(L,2)),
                      (float)luaL_checknumber(L,3),lcolor(L,4),0,(float)luaL_optnumber(L,5,1.5)); return 0; }
static int l_circlef(lua_State* L){
    if(!g_plDL) return 0;
    g_plDL->AddCircleFilled(V((float)luaL_checknumber(L,1),(float)luaL_checknumber(L,2)),
                            (float)luaL_checknumber(L,3),lcolor(L,4)); return 0; }
static ImFont* PluginFont(float size){ return size>=30? g_fBig : size>=18? g_fMed : g_fSml; }
static int l_text(lua_State* L){
    if(!g_plDL) return 0;
    float x=(float)luaL_checknumber(L,1),y=(float)luaL_checknumber(L,2);
    float sz=(float)luaL_checknumber(L,3); ImU32 c=lcolor(L,4);
    const char* s=luaL_checkstring(L,5);
    TextAt(g_plDL,PluginFont(sz),sz,V(x,y),c,s); return 0; }
static int l_textw(lua_State* L){
    float sz=(float)luaL_checknumber(L,1); const char* s=luaL_checkstring(L,2);
    lua_pushnumber(L,TextW(PluginFont(sz),sz,s)); return 1; }
static int l_stats(lua_State* L){
    lua_newtable(L);
    auto set=[&](const char* k,double v){ lua_pushstring(L,k); lua_pushnumber(L,v); lua_settable(L,-3); };
    set("cpu",g_st.cpuUsage); set("gpu",g_st.gpuUsage);
    set("cpu_temp",g_st.cpuTemp); set("gpu_temp",g_st.gpuTemp);
    set("mem_used",(double)g_st.memUsed); set("mem_total",(double)g_st.memTotal);
    set("disk_used",(double)g_st.diskUsed); set("disk_total",(double)g_st.diskTotal);
    set("net_down",g_st.netDown); set("net_up",g_st.netUp);
    set("battery",g_st.battPct);
    lua_pushstring(L,"charging"); lua_pushboolean(L,g_st.charging); lua_settable(L,-3);
    lua_pushstring(L,"online");   lua_pushboolean(L,g_st.online);   lua_settable(L,-3);
    return 1; }
static int l_media(lua_State* L){
    lua_newtable(L);
    auto sets=[&](const char* k,const std::string& v){ lua_pushstring(L,k); lua_pushstring(L,v.c_str()); lua_settable(L,-3); };
    sets("title",g_md.title); sets("artist",g_md.artist); sets("album",g_md.album);
    lua_pushstring(L,"playing"); lua_pushboolean(L,g_md.playing&&g_md.has); lua_settable(L,-3);
    lua_pushstring(L,"pos"); lua_pushnumber(L,g_md.pos); lua_settable(L,-3);
    lua_pushstring(L,"dur"); lua_pushnumber(L,g_md.dur); lua_settable(L,-3);
    return 1; }
static int l_weather(lua_State* L){
    lua_newtable(L);
    lua_pushstring(L,"ok");   lua_pushboolean(L,g_wx.ok);           lua_settable(L,-3);
    lua_pushstring(L,"city"); lua_pushstring(L,g_wx.city.c_str());  lua_settable(L,-3);
    lua_pushstring(L,"text"); lua_pushstring(L,WxText(g_wx.code));  lua_settable(L,-3);
    auto set=[&](const char* k,double v){ lua_pushstring(L,k); lua_pushnumber(L,v); lua_settable(L,-3); };
    set("temp",g_wx.temp); set("feels",g_wx.feels); set("wind",g_wx.wind);
    set("humidity",g_wx.hum); set("code",g_wx.code);
    return 1; }
static int l_mouse(lua_State* L){
    ImGuiIO& io=ImGui::GetIO();
    lua_newtable(L);
    lua_pushstring(L,"x"); lua_pushnumber(L,io.MousePos.x); lua_settable(L,-3);
    lua_pushstring(L,"y"); lua_pushnumber(L,io.MousePos.y); lua_settable(L,-3);
    lua_pushstring(L,"down");    lua_pushboolean(L,io.MouseDown[0]);    lua_settable(L,-3);
    lua_pushstring(L,"clicked"); lua_pushboolean(L,io.MouseClicked[0]); lua_settable(L,-3);
    return 1; }
static int l_now(lua_State* L){ lua_pushnumber(L,(lua_Number)GetTickCount64()); return 1; }
static int l_open(lua_State* L){       // launch a file/url — the user's own scripts, on their machine
    const char* s=luaL_checkstring(L,1);
    std::wstring w=U82W(s);
    AetherShellExec(nullptr,L"open",w.c_str(),nullptr,nullptr,SW_SHOWNORMAL); return 0; }
static int l_log(lua_State* L){
    const char* s=luaL_checkstring(L,1);
    if(g_plCur){ g_plCur->log=s; }
    return 0; }

static const luaL_Reg CW_API[] = {
    {"color",l_color},{"accent",l_accent},{"ink",l_ink},{"ink2",l_ink2},{"card",l_card},
    {"rect_fill",l_rectf},{"rect",l_rect},{"line",l_line},
    {"circle",l_circle},{"circle_fill",l_circlef},
    {"text",l_text},{"text_w",l_textw},
    {"stats",l_stats},{"media",l_media},{"weather",l_weather},{"mouse",l_mouse},
    {"now",l_now},{"open",l_open},{"log",l_log},
    {nullptr,nullptr}
};
// a runaway loop must not freeze the shell: bail out after a generous instruction budget
static void PluginHook(lua_State* L,lua_Debug*){ luaL_error(L,"plugin ran too long (instruction limit)"); }

static void PluginClose(LuaPlugin& p){ if(p.L){ lua_close(p.L); p.L=nullptr; } p.ok=false; }

static bool PluginLoad(LuaPlugin& p){
    PluginClose(p);
    lua_State* L=luaL_newstate(); if(!L){ p.err="out of memory"; return false; }
    luaL_openlibs(L);
    // sandbox: drop everything that can touch the machine outside our API
    const char* kill[]={"io","package","require","dofile","loadfile","load","debug","print"};
    for(const char* k:kill){ lua_pushnil(L); lua_setglobal(L,k); }
    lua_getglobal(L,"os");
    if(lua_istable(L,-1)){ const char* okill[]={"execute","remove","rename","exit","tmpname","setlocale","getenv"};
        for(const char* k:okill){ lua_pushnil(L); lua_setfield(L,-2,k); } }
    lua_pop(L,1);
    luaL_newlib(L,CW_API); lua_setglobal(L,"cw");
    lua_sethook(L,PluginHook,LUA_MASKCOUNT,4000000);
    p.L=L;
    p.name=p.file;                 // so a plugin that fails to load still has a name in the UI
    std::string path=PluginDir()+p.file;
    if(luaL_dofile(L,path.c_str())!=LUA_OK){
        p.err = lua_tostring(L,-1)? lua_tostring(L,-1) : "load failed"; lua_pop(L,1);
        PluginClose(p); return false; }
    // read the plugin descriptor
    p.surface=PSURF_DESKTOP; p.w=240; p.h=110; p.x=40; p.y=40;
    lua_getglobal(L,"plugin");
    if(lua_istable(L,-1)){
        lua_getfield(L,-1,"name");    if(lua_isstring(L,-1)) p.name=lua_tostring(L,-1);      lua_pop(L,1);
        lua_getfield(L,-1,"surface"); if(lua_isstring(L,-1)) p.surface=!strcmp(lua_tostring(L,-1),"bar")?PSURF_BAR:PSURF_DESKTOP; lua_pop(L,1);
        auto num=[&](const char* k,float& out){ lua_getfield(L,-1,k); if(lua_isnumber(L,-1)) out=(float)lua_tonumber(L,-1); lua_pop(L,1); };
        num("x",p.x); num("y",p.y); num("w",p.w); num("h",p.h);
    }
    lua_pop(L,1);
    lua_getglobal(L,"draw");
    bool hasDraw=lua_isfunction(L,-1); lua_pop(L,1);
    if(!hasDraw){ p.err="no draw(ctx) function"; PluginClose(p); return false; }
    p.err.clear(); p.ok=true; return true;
}

static bool PluginDisabledInConfig(const std::string& file){
    for(auto& d:g_plDisabled) if(_stricmp(d.c_str(),file.c_str())==0) return true;
    return false;
}
// scan plugins\ for *.lua; load new files, hot-reload changed ones, drop deleted ones
static void PluginsScan(){
    std::string dir=PluginDir();
    CreateDirectoryW(U82W(dir).c_str(),nullptr);
    std::vector<std::string> seen;
    WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW(U82W(dir+"*.lua").c_str(),&fd);
    if(h!=INVALID_HANDLE_VALUE){
        do{
            if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string name=W2U8(fd.cFileName); seen.push_back(name);
            LuaPlugin* found=nullptr;
            for(auto& p:g_plugins) if(p.file==name){ found=&p; break; }
            if(!found){ LuaPlugin np; np.file=name; np.mtime=fd.ftLastWriteTime;
                np.enabled=!PluginDisabledInConfig(name);
                g_plugins.push_back(np); found=&g_plugins.back();
                if(found->enabled) PluginLoad(*found); }
            else if(CompareFileTime(&found->mtime,&fd.ftLastWriteTime)!=0){   // hot reload
                found->mtime=fd.ftLastWriteTime;
                if(found->enabled) PluginLoad(*found);
            }
        } while(FindNextFileW(h,&fd));
        FindClose(h);
    }
    for(size_t i=0;i<g_plugins.size();){
        bool still=false; for(auto& s:seen) if(s==g_plugins[i].file){ still=true; break; }
        if(!still){ PluginClose(g_plugins[i]); g_plugins.erase(g_plugins.begin()+i); } else i++;
    }
}
static void PluginsShutdown(){ for(auto& p:g_plugins) PluginClose(p); g_plugins.clear(); }

// --pluginstatus: dump what each plugin did to plugins\status.log (headless verification —
// checking the Plugins page instead would mean screenshotting the whole settings app)
static bool g_plStatusLog=false;
static void PluginsWriteStatus(){
    if(!g_plStatusLog) return;
    std::ofstream f(PluginDir()+"status.log");
    if(!f) return;
    f<<"plugins enabled: "<<(g_pluginsOn?"yes":"no")<<"\n";
    for(auto& p:g_plugins)
        f<<p.file<<" | name="<<p.name<<" | surface="<<(p.surface==PSURF_BAR?"bar":"desktop")
         <<" | enabled="<<(p.enabled?"yes":"no")<<" | running="<<(p.ok?"yes":"no")
         <<" | error="<<(p.err.empty()?"-":p.err)
         <<" | log="<<(p.log.empty()?"-":p.log)<<"\n";
}

// ship a working example + the API reference, so the folder is never an empty mystery
static void WriteExamplePlugin(bool force){
    std::string dir=PluginDir();
    CreateDirectoryW(U82W(dir).c_str(),nullptr);
    std::string ex=dir+"example-clock.lua";
    bool exists = GetFileAttributesA(ex.c_str())!=INVALID_FILE_ATTRIBUTES;
    // a freshly shipped example starts OFF: finding a widget you never asked for on your desktop
    // is worse than having to flip one switch on Settings > Plugins
    bool markOff=false;
    if((!exists || g_freshConfig) && !force){ bool known=false;
        for(auto& d:g_plDisabled) if(d=="example-clock.lua") known=true;
        if(!known){ g_plDisabled.push_back("example-clock.lua"); markOff=true; } }
    if(!exists || force){
        std::ofstream f(ex);
        f<<"-- Aether example plugin: a desktop clock + CPU bar.\n"
           "-- Edit and save; the shell reloads it within a second.\n"
           "plugin = { name = \"Clock & CPU\", surface = \"desktop\", x = 60, y = 60, w = 260, h = 132 }\n"
           "\n"
           "function draw(ctx)\n"
           "  local a = cw.accent()\n"
           "  cw.rect_fill(ctx.x, ctx.y, ctx.w, ctx.h, cw.color(0,0,0,110), 16)\n"
           "  cw.rect(ctx.x, ctx.y, ctx.w, ctx.h, cw.color(255,255,255,40), 16, 1.0)\n"
           "\n"
           "  local t = os.date(\"%H:%M\")\n"
           "  cw.text(ctx.x + 18, ctx.y + 14, 34, cw.color(255,255,255,235), t)\n"
           "  cw.text(ctx.x + 18, ctx.y + 58, 14, cw.color(255,255,255,150), os.date(\"%A, %d %B\"))\n"
           "\n"
           "  local s = cw.stats()\n"
           "  local w = ctx.w - 36\n"
           "  cw.rect_fill(ctx.x + 18, ctx.y + 92, w, 8, cw.color(255,255,255,45), 4)\n"
           "  cw.rect_fill(ctx.x + 18, ctx.y + 92, w * s.cpu, 8, a, 4)\n"
           "  cw.text(ctx.x + 18, ctx.y + 104, 13, cw.color(255,255,255,170),\n"
           "          string.format(\"CPU %.0f%%   RAM %.0f%%\", s.cpu*100,\n"
           "                        (s.mem_total > 0) and (s.mem_used/s.mem_total*100) or 0))\n"
           "end\n";
    }
    std::string rd=dir+"README.txt";
    if(GetFileAttributesA(rd.c_str())==INVALID_FILE_ATTRIBUTES || force){
        std::ofstream f(rd);
        f<<"Aether plugins\n"
           "====================\n"
           "Drop a .lua file in this folder. It is loaded at startup and re-loaded whenever you save it.\n"
           "\n"
           "A plugin declares itself and provides draw(ctx):\n"
           "\n"
           "  plugin = { name=\"My widget\", surface=\"desktop\", x=60, y=60, w=260, h=132 }\n"
           "  function draw(ctx) ... end\n"
           "\n"
           "  surface = \"desktop\"  draws on the wallpaper at x,y with size w,h\n"
           "  surface = \"bar\"      gets a slot h pixels tall in the taskbar (x,y,w are given to you)\n"
           "\n"
           "ctx = { x, y, w, h, dt, hovered, clicked } - x,y is your slot's top-left on screen.\n"
           "Drawing outside your slot is clipped away.\n"
           "\n"
           "API (global table cw):\n"
           "  cw.color(r,g,b[,a])            -> colour value\n"
           "  cw.accent() cw.ink() cw.ink2() cw.card()   -> the current theme's colours\n"
           "  cw.rect_fill(x,y,w,h,col[,round])\n"
           "  cw.rect(x,y,w,h,col[,round][,thickness])\n"
           "  cw.line(x1,y1,x2,y2,col[,thickness])\n"
           "  cw.circle(x,y,r,col[,thickness])   cw.circle_fill(x,y,r,col)\n"
           "  cw.text(x,y,size,col,str)          cw.text_w(size,str) -> width\n"
           "  cw.stats()   -> cpu, gpu, cpu_temp, gpu_temp, mem_used, mem_total, disk_used,\n"
           "                  disk_total, net_down, net_up, battery, charging, online\n"
           "  cw.media()   -> title, artist, album, playing, pos, dur\n"
           "  cw.weather() -> ok, city, text, temp, feels, wind, humidity, code\n"
           "  cw.mouse()   -> x, y, down, clicked\n"
           "  cw.now()     -> milliseconds since boot (for animation)\n"
           "  cw.open(path_or_url)               cw.log(text)  (shown on the Plugins page)\n"
           "\n"
           "Sandbox: io, package, require, load/loadfile/dofile, debug and the dangerous os.*\n"
           "functions are removed. A plugin that loops forever is stopped with an error rather\n"
           "than freezing the shell, and an erroring plugin is switched off until you save it again.\n";
    }
    if(markOff) SaveConfig();   // remember that the shipped example starts switched off
}

// run one plugin's draw(ctx). Anything it throws disables it until the file changes.
static void PluginCall(LuaPlugin& p,float x,float y,float w,float h){
    if(!p.L||!p.ok) return;
    lua_State* L=p.L;
    lua_getglobal(L,"draw");
    if(!lua_isfunction(L,-1)){ lua_pop(L,1); return; }
    ImGuiIO& io=ImGui::GetIO();
    bool hov = io.MousePos.x>x&&io.MousePos.x<x+w&&io.MousePos.y>y&&io.MousePos.y<y+h;
    lua_newtable(L);
    auto setn=[&](const char* k,double v){ lua_pushstring(L,k); lua_pushnumber(L,v); lua_settable(L,-3); };
    setn("x",x); setn("y",y); setn("w",w); setn("h",h); setn("dt",g_frameDt);
    lua_pushstring(L,"hovered"); lua_pushboolean(L,hov); lua_settable(L,-3);
    lua_pushstring(L,"clicked"); lua_pushboolean(L,hov&&io.MouseClicked[0]); lua_settable(L,-3);
    g_plCur=&p;
    if(lua_pcall(L,1,0,0)!=LUA_OK){
        p.err = lua_tostring(L,-1)? lua_tostring(L,-1) : "error"; lua_pop(L,1);
        p.ok=false;                       // stays off until the file is edited (hot reload)
    }
    g_plCur=nullptr;
}
// draw every plugin bound to a surface. Each one is clipped to its own slot, so a plugin cannot
// paint over the rest of the shell.
static void PluginsDraw(ImDrawList* dl,int surface,float x,float y,float w,float h){
    if(!g_pluginsOn||g_plugins.empty()) return;
    g_plDL=dl; g_plX=x; g_plY=y; g_plW=w; g_plH=h;
    if(surface==PSURF_DESKTOP){
        for(auto& p:g_plugins){
            if(!p.enabled||!p.ok||p.surface!=PSURF_DESKTOP) continue;
            float px=x+p.x, py=y+p.y, pw=std::min(p.w,w), ph=std::min(p.h,h);
            dl->PushClipRect(V(px,py),V(px+pw,py+ph),true);
            PluginCall(p,px,py,pw,ph);
            dl->PopClipRect();
        }
    } else {                                   // bar: stack the slots down from y, inside the strip
        float cy=y;
        for(auto& p:g_plugins){
            if(!p.enabled||!p.ok||p.surface!=PSURF_BAR) continue;
            float ph=std::clamp(p.h,16.0f,160.0f);
            if(cy+ph>y+h) break;
            dl->PushClipRect(V(x,cy),V(x+w,cy+ph),true);
            PluginCall(p,x,cy,w,ph);
            dl->PopClipRect();
            cy+=ph+6;
        }
    }
    g_plDL=nullptr;
}
// height the bar must reserve for its plugin slots
static float PluginsBarHeight(){
    float t=0; if(!g_pluginsOn) return 0;
    for(auto& p:g_plugins) if(p.enabled&&p.ok&&p.surface==PSURF_BAR) t+=std::clamp(p.h,16.0f,160.0f)+6;
    return t;
}

// Frosted-wallpaper acrylic for the bar. The bar lives in the solid desktop margin OUTSIDE the
// wallpaper bubble, so plain screen-capture acrylic reads as flat colour. Instead we sample the
// full-screen hard-blurred wallpaper (g_deskFrost) cropped to the bar's screen slice, then lay a
// translucent scrim + a top highlight + edge strokes over it — genuine see-through frosted glass.
// A vertical wash that stays inside a ROUNDED body.
// ImGui clip rects are rectangular, so PushClipRect(a,b) does NOT clip to a rounded shape - it
// clips to the bounding box. Painting a gradient across that box spilled it into the corners
// outside the rounded caps, which is what showed as a pale square block behind each end of the
// bar. Slice the wash and inset each slice by the cap's arc, and it stays on the bar.
static void RoundedVWash(ImDrawList* dl, ImVec2 a, ImVec2 b, float rnd, ImU32 rgb,
                         float t0, float t1, int alpha0, int alpha1){
    float H=b.y-a.y; if(H<=1.0f || b.x<=a.x) return;
    // Clamp the radius the same way PathRect does. The bar is ~56px wide with a 39.6px corner
    // radius, so an unclamped inset asks for 39.6px off BOTH sides of a 56px strip - the slices
    // near the cap come out negative-width and get dropped, which leaves a hard horizontal seam
    // where the wash suddenly starts.
    const float r = std::min(rnd, std::min((b.x-a.x)*0.5f, H*0.5f));
    float yA=a.y+H*t0, yB=a.y+H*t1; if(yB-yA<=0.5f) return;
    auto inset=[&](float y)->float{                 // how far in the rounded cap has pulled the edge
        if(r<=0.0f) return 0.0f;
        float d = (y < a.y+r) ? (a.y+r-y) : (y > b.y-r ? y-(b.y-r) : 0.0f);
        if(d<=0.0f) return 0.0f;
        d=std::min(d,r);
        return r - sqrtf(std::max(0.0f, r*r - d*d));
    };
    // Slice finely only where the edge is actually curving. A uniform slice count over the whole
    // bar is the wrong shape: at 64 slices down a ~1000px bar each step is ~15px, but the cap is
    // only ~28px, so the arc got two steps and the wash began with a hard horizontal seam. The
    // straight middle needs almost no subdivision; the caps need about a pixel each.
    auto emit=[&](float y0,float y1,int steps){
        if(y1-y0<=0.05f || steps<1) return;
        for(int i=0;i<steps;i++){
            float g0=y0+(y1-y0)*(float)i/steps, g1=y0+(y1-y0)*(float)(i+1)/steps;
            float f=(((g0+g1)*0.5f)-yA)/(yB-yA);
            int al=(int)(alpha0+(alpha1-alpha0)*std::clamp(f,0.0f,1.0f));
            if(al<=0) continue;
            float ix=std::max(inset(g0),inset(g1));
            if(b.x-ix <= a.x+ix) continue;
            dl->AddRectFilled(V(a.x+ix,g0), V(b.x-ix,g1), WithA(rgb,al));
        }
    };
    const int NCAP=std::max(8,(int)ceilf(r));       // ~1px per step through the curve
    float capT=std::clamp(a.y+r, yA, yB);
    float capB=std::clamp(b.y-r, yA, yB);
    emit(yA,   capT, NCAP);      // top cap
    emit(capT, capB, 24);        // straight middle
    emit(capB, yB,   NCAP);      // bottom cap
}

static void BarGlass(ImDrawList* dl,ImVec2 a,ImVec2 b,float mx,float my,float mw,float mh,float rnd,ImDrawFlags fl,float alpha){
    if(g_deskGlow)
        for(int i=6;i>0;i--){ float e=i*2.4f;
            dl->AddRect(V(a.x-e,a.y-e+3),V(b.x+e,b.y+e+3),IM_COL32(0,0,0,(int)(11*alpha)),rnd+e,fl,e*0.9f); }
    // Caelestia's bar is a FULLY SOLID near-black strip, not a frosted panel, so it is painted at
    // full opacity and nothing bleeds through - not even a bright fullscreen game.
    //
    // A frosted image used to be drawn underneath this fill. It was completely invisible, being
    // covered by an opaque rect of the same size - except along the rounded edge, where the image's
    // antialias fringe reached one pixel further out than the fill's and left a brighter halo
    // tracing the bar. That hairline was the last of the grey outline. Drawing it was pure cost for
    // an artefact, so it is gone; (mx,my,mw,mh) are kept in the signature for the callers.
    (void)mx; (void)my; (void)mw; (void)mh;
    // bar.custom_colours: this near-black is the bar's REAL surface - not COL_PANELL, not the frame
    // material - so an override that does not reach here recolours the pills and leaves the strip
    // itself exactly as it was, which is what the first attempt at this did.
    if(g_frameTint)   dl->AddRectFilled(a,b,WithA(g_frameTint,(int)(255*alpha)),rnd,fl);
    else if(g_darkUI) dl->AddRectFilled(a,b,IM_COL32(18,18,22,(int)(255*alpha)),rnd,fl);
    else              dl->AddRectFilled(a,b,PanelCol((int)(255*alpha)),rnd,fl);
    // Depth pass: a full-length vertical gradient (light at the head, settling into the base colour)
    // plus a whisper of the live accent, so the strip reads as a lit surface instead of flat paint.
    // Clipped to the rounded body so the corners stay clean.
    { int top = g_darkUI? 22 : 30;
      RoundedVWash(dl,a,b,rnd, IM_COL32(255,255,255,255), 0.0f, 1.0f, (int)(top*alpha), 0);
      RoundedVWash(dl,a,b,rnd, COL_GOLD,                  0.55f,1.0f, 0, (int)((g_darkUI?16:12)*alpha));
    }
    // rim: one crisp light hairline inside, one soft dark line outside. The bar draws its own glass
    // rather than going through GlassPanel, so it needs the same switch - this pair is the grey
    // line that traced the bar edge and made the strip read as boxy.
    if(g_panelOutline){
        dl->AddRect(V(a.x+0.5f,a.y+0.5f),V(b.x-0.5f,b.y-0.5f), g_darkUI?IM_COL32(255,255,255,(int)(26*alpha)):IM_COL32(255,255,255,(int)(150*alpha)),rnd,fl,1.0f);
        dl->AddRect(a,b, g_darkUI?IM_COL32(0,0,0,(int)(70*alpha)):IM_COL32(0,0,0,(int)(30*alpha)),rnd,fl,1.0f);
    }
}

// Horizontal taskbar (top / bottom edge): the same items laid out left-to-right, like a normal
// CachyOS/Windows taskbar. Left = launcher/files/theme/mic/workspaces, middle = running apps,
// right = clock/wifi/bt/battery/power/calendar. The desktop insets on this edge (BubbleInsetsFor),
// so the bar sits OUTSIDE the desktop exactly like the vertical dock.
// ---------------------------------------------------------------------------------------------
// BAR LAYOUT ENGINE — measure, then place. Every item reports one EXTENT along the bar's main axis
// (its whole occupied run, leading/trailing breathing room baked in, so a uniform inter-item gap of
// zero reproduces the hand-tuned rice spacing exactly). Spacers report zero and split whatever is
// left over. Alignment only gets a say when no spacer is visible - a spacer has already answered
// the question "where does the slack go?", and applying both produces layouts nobody asked for.
// gapAfter is the ColumnLayout spacing that follows this entry. Bar.qml puts Tokens.spacing.medium
// between every entry; StatusIcons.qml packs the icons INSIDE its own pill at spacing.medium/2, and
// those are separate slots here, so the gap has to be per-slot rather than one global number.
struct BarSlot{ int id; float pos, ext, gapAfter; };

static void BarPlace(std::vector<BarSlot>& v, float start, float avail, int align){
    int nsp=0; float fixed=0, gaps=0;
    for(size_t q=0;q<v.size();q++){ if(BarIsSpacer(v[q].id)) nsp++; else fixed+=v[q].ext;
                                    if(q+1<v.size()) gaps+=v[q].gapAfter; }
    float slack = avail-fixed-gaps; if(slack<0) slack=0;
    int ngap = (int)v.size()-1; if(ngap<0) ngap=0;
    float lead=0, extra=0, spEach=0;
    if(nsp>0)      spEach = slack/nsp;                       // spacers own the slack
    else switch(align){
        case BALIGN_CENTER: lead=slack*0.5f; break;
        case BALIGN_END:    lead=slack;      break;
        case BALIGN_SPREAD: if(ngap>0) extra=slack/ngap; else lead=slack*0.5f; break;
        default: break;                                      // BALIGN_START: slack trails
    }
    float c=start+lead;
    for(size_t q=0;q<v.size();q++){ BarSlot& s=v[q];
        if(BarIsSpacer(s.id)) s.ext=spEach;
        s.pos=c; c+=s.ext+extra+(q+1<v.size()? s.gapAfter : 0.0f); }
}
// Where this item wants its group capsule to start/end. The three historic pills were hand-tuned to
// different insets (the workspace pill hugs tighter at the top than the system cluster does), so the
// numbers live per item rather than as one global padding that would move all of them.
static inline float BarPillTop(int id,float pos){
    if(id==BIT_WORKSPACES) return pos;                          // its extent already carries the pad
    return pos-(float)Tok::padding::medium;                     // StatusIcons pads 12 at each end
}
static inline float BarPillBot(int id,float pos,float ext){
    if(id==BIT_WORKSPACES) return pos+ext;
    return pos+ext+(float)Tok::padding::medium;
}

#include "src/modules/bar/BarCustom.h"   // your own bar items, from configaritems.toml
#include "src/modules/bar/BarV2.h"   // the Caelestia horizontal bar
static void DrawBarHoriz(int mi,float MX,float MY,float MW,float MH,BarState& BS,float slide){
    ImGuiIO& io=ImGui::GetIO(); ImDrawList* dl=ImGui::GetBackgroundDrawList();
    const Panel& P=g_pn[PN_BAR];
    PRect pr=PanelRect(P,MW,MH); pr.x+=MX; pr.y+=MY;
    float barW=pr.w, bh=pr.h;                           // width (length) x height (thickness)
    PanelSlide(P,pr,slide);
    float bx=pr.x, by=pr.y;
    const bool onTop=(P.edge==EDGE_TOP);
    ImVec2 pmin=V(bx,by),pmax=V(bx+barW,by+bh);
    float rnd=g_bubble?g_bubbleRound:g_panelRound;
    BarGlass(dl,pmin,pmax,MX,MY,MW,MH,rnd,PanelCorners(P),1.0f);   // frosted-wallpaper acrylic
    BS.rect=PanelHitRect(PRect{bx,by,barW,bh});
    if(g_barStyle==1){ DrawBarHorizV2(mi,bx,by,barW,bh,onTop,slide); return; }
    const int HID=mi*20000;
    bool click=io.MouseClicked[0], rclick=io.MouseClicked[1];
    float cy=by+bh*0.5f, half=bh*0.5f-4;
    float inner = onTop? by+bh : by;                    // strip edge facing the desktop
    const char* tip=nullptr; float tipX=0; std::string tipStore;
    auto IE=[&](int i){ return EaseOutCubic(Stagger(slide,i,0.03f,0.6f)); };
    auto IY=[&](int i){ return (1.0f-IE(i))*(onTop?-14.0f:14.0f); };   // items drop/rise in
    auto IA=[&](int i,ImU32 c){ return MulA(c,IE(i)); };
    auto hitC=[&](float x,float r){ return io.MousePos.x>x-r&&io.MousePos.x<x+r&&io.MousePos.y>cy-r&&io.MousePos.y<cy+r; };
    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);

    // ===== REGISTRY-DRIVEN LAYOUT (same engine as the vertical bar, main axis = x) =============
    // Was two hardcoded cursors: a left group growing right (launcher/files/theme/mic/workspaces/
    // title) and a right cluster growing LEFT (power/battery/bt/wifi/clock/calendar), with the task
    // buttons squeezed into whatever was left between them. Nothing could be hidden or moved. Now
    // it is the same ordered list the vertical bar uses, so one Settings page drives both edges.
    if(g_barItems.empty()) BarItemsDefault();

    HWND fg=GetForegroundWindow(); (void)fg;
    // focused window title (skips our own shell windows, so an empty desktop reads blank)
    std::string wiTitle;
    { HWND fgw=GetForegroundWindow();
      wchar_t wt[256]={0}; if(fgw) GetWindowTextW(fgw,wt,255);
      DWORD wpid=0; if(fgw) GetWindowThreadProcessId(fgw,&wpid);
      if(fgw && wpid!=GetCurrentProcessId() && wt[0]) wiTitle=W2U8(CleanTitle(wt)); }
    float wiCap=std::min(260.0f, barW*0.28f);
    std::string wiClip = wiTitle.empty()? std::string() : Clip(g_fMed,15,wiTitle,wiCap-18);
    float wiW = wiClip.empty()? 0.0f : TextW(g_fMed,15,wiClip.c_str());

    static std::vector<DockApp*> mine; mine.clear();
    { HMONITOR barMon=MonitorFromPoint(POINT{MonRect(mi).left+2,MonRect(mi).top+2},MONITOR_DEFAULTTONEAREST);
      // a.mon is null for a pin whose app is closed: it belongs on every bar, not none of them
      for(auto& a:g_dockApps)
          if(!g_barSameMonitor || a.mon==barMon || (a.pinned && !a.running)) mine.push_back(&a); }
    float isz=bh-14, ai=isz+10;
    int nApps=(int)mine.size();

    int notifN; { std::lock_guard<std::mutex> lk(g_notifMtx); notifN=(int)g_notifs.size(); }
    // The tray is a TRAY: only the icons the user promoted sit in the bar, the rest live behind the
    // chevron. barTray is therefore the PROMOTED set (everything, when collapse is switched off).
    static std::vector<SysTrayIcon*> barTray; barTray.clear();
    int trayTotal=0, trayHidden=0;
    { std::lock_guard<std::mutex> lk(g_systrayMtx);
      for(auto&s:g_systray){ if(s.hidden||!s.tex) continue; trayTotal++;
          if(TrayIsShown(s)) barTray.push_back(&s); else trayHidden++; } }
    const float trayStepH=28.0f, trayChevW=26.0f;
    int trayShown=std::min((int)barTray.size(),10);   // a wide bar can show more than the vertical strip
    bool trayChev=g_trayCollapse;                     // the chevron is the tray's handle; always there

    char battTxt[10]; snprintf(battTxt,10,"%d%%",g_st.battPct);
    int  h12=g_clock24?lt.tm_hour:(((lt.tm_hour%12)==0)?12:lt.tm_hour%12);
    char clkTxt[16]; snprintf(clkTxt,16,"%d:%02d",h12,lt.tm_min);
    const char* clkAp=g_clock24?"":(lt.tm_hour<12?" AM":" PM");
    float clkW=TextW(g_fMed,16,clkTxt)+TextW(g_fSml,12,clkAp)+14;

    auto liveOK=[&](int id)->bool{
        switch(id){
        case BIT_WORKSPACES: return WsRingFor(mi).count>1;
        case BIT_APPS:       return nApps>0;
        case BIT_WINDOWINFO: return !wiClip.empty();
        case BIT_MIC:        return g_micPresent;
        case BIT_BATT:       return g_st.hasBattery;
        case BIT_TRAY:       return trayTotal>0;
        case BIT_NOTIF:      return notifN>0||g_dnd;
        default: return BIT_IS_CUSTOM(id)? BarCustomOn(id-BIT_CUSTOM1) : true; }
    };
    auto extOf=[&](int id)->float{
        if(BIT_IS_CUSTOM(id)) return BarCustomExtH(id-BIT_CUSTOM1,13.0f);
        switch(id){
        case BIT_LOGO:       return 34.0f;
        case BIT_FILES:      return 30.0f;
        case BIT_WORKSPACES: return WsRingFor(mi).count*16.0f+20.0f;
        case BIT_WINDOWINFO: return 14.0f+wiW+14.0f;
        case BIT_APPS:       return 8.0f+nApps*ai;
        case BIT_CALENDAR:   return 30.0f;
        case BIT_CLOCK:      return clkW;
        case BIT_WIFI:       return 28.0f;
        case BIT_BT:         return 28.0f;
        case BIT_THEME:      return 30.0f;
        case BIT_MIC:        return 30.0f;
        case BIT_BATT:       return TextW(g_fSml,13,battTxt)+8.0f;
        case BIT_POWER:      return 30.0f;
        case BIT_TRAY:       return 8.0f+(trayChev?trayChevW:0.0f)+trayShown*trayStepH;
        case BIT_NOTIF:      return 30.0f;
        case BIT_ETH:        return 28.0f;
        case BIT_AUDIO:      return 30.0f;
        default:             return 0.0f; }                 // spacers
    };

    std::vector<BarSlot> slots;
    auto rebuild=[&](){
        slots.clear();
        for(auto& c:g_barItems){
            if(!c.on || !BAR_ITEMS[c.id].horiz || !liveOK(c.id)) continue;
            slots.push_back({c.id,0.0f,extOf(c.id)}); }
        float t=0; for(auto& sl:slots) t+=sl.ext; return t;
    };
    const float PAD=18.0f, availW=barW-PAD*2;
    float need=rebuild();
    while(need>availW && nApps>0){ nApps--; need=rebuild(); }   // task buttons are the elastic item

    BarPlace(slots,bx+PAD,availW,g_barAlign);

    float posOf[BIT_COUNT]; bool vis[BIT_COUNT];
    for(int k=0;k<BIT_COUNT;k++){ posOf[k]=0; vis[k]=false; }
    for(auto& sl:slots){ posOf[sl.id]=sl.pos; vis[sl.id]=true; }
    int n = vis[BIT_APPS]? nApps : 0;
    bool anyThumb=false, appTookRclick=false;

    // (no group dividers: the rice-matching pass dropped every hairline from the vertical strip as
    //  clutter, and with items now free to be reordered a fixed divider cannot know what it divides)
    int hq=0;
    for(auto& sl:slots){
        int i=hq++; float oy=IY(i);
        switch(sl.id){
        case BIT_LOGO: {
            float c=sl.pos+17; bool hov=hitC(c,half); float ha=HoverAnim(HID+3100,hov);
            float pop=0.85f+0.15f*IE(i); ImVec2 lc=V(c,cy+oy);
            int lw=0,lh=0;
            if(BarLogoTex(lw,lh)){
                // A real mark carries its own colour, so the accent tile behind it just fights the
                // artwork - go bare like the vertical strip and keep only the hover state layer.
                float box=(bh-16.0f)*pop;
                if(ha>0.01f){ float sq=box*0.62f;
                    dl->AddRectFilled(V(lc.x-sq,lc.y-sq),V(lc.x+sq,lc.y+sq),IA(i,AccA((int)(ha*52))),sq*0.56f); }
                DrawLogoMark(dl,lc,box*(1.0f+ha*0.06f),IE(i));
            } else {
                dl->AddRectFilled(V(c-(13+ha*1.5f)*pop,cy+oy-(13+ha*1.5f)*pop),V(c+(13+ha*1.5f)*pop,cy+oy+(13+ha*1.5f)*pop),IA(i,AccA((int)(215+40*ha))),9);
                ImU32 gl=g_darkUI?IM_COL32(12,16,13,255):IM_COL32(250,253,252,255);
                dl->AddCircle(lc,6.5f*pop,IA(i,gl),0,2.0f); dl->AddCircleFilled(lc,2.0f*pop,IA(i,gl));
            }
            if(click&&hov)g_launShow=true; if(hov){tip="Launcher";tipX=c;} } break;
        case BIT_FILES: {
            float c=sl.pos+15; bool hov=hitC(c,half); float ha=HoverAnim(HID+3110,hov);
            if(ha>0.01f)dl->AddCircleFilled(V(c,cy+oy),half,WithA(COL_INK2,(int)(ha*40*IE(i))));
            ImU32 fc=IA(i,COL_INK); dl->AddRectFilled(V(c-8,cy+oy-4),V(c-1,cy+oy+1),fc,1.5f); dl->AddRectFilled(V(c-8,cy+oy-2),V(c+8,cy+oy+7),fc,2.0f);
            if(click&&hov)FmStart(); if(hov){tip="Files";tipX=c;} } break;
        case BIT_CUSTOM1: case BIT_CUSTOM2: case BIT_CUSTOM3:
        case BIT_CUSTOM4: case BIT_CUSTOM5: case BIT_CUSTOM6: {
            int ci=sl.id-BIT_CUSTOM1; float w=BarCustomExtH(ci,13.0f);
            bool hov = io.MousePos.x>=sl.pos && io.MousePos.x<sl.pos+w && io.MousePos.y>=by && io.MousePos.y<by+bh;
            float ha=HoverAnim(HID+3200+ci,hov);
            BarCustomDrawH(dl,ci,sl.pos,cy+oy,13.0f,IA(i,COL_INK),ha*IE(i),half);
            if(click&&hov) BarCustomClick(ci);
            if(hov){ const char* t=BarCustomTip(ci); if(t){ tip=t; tipX=sl.pos+w*0.5f; } } } break;
        case BIT_THEME: {
            float c=sl.pos+15; bool hov=hitC(c,half); float ha=HoverAnim(HID+3101,hov);
            if(ha>0.01f)dl->AddCircleFilled(V(c,cy+oy),half,WithA(COL_INK2,(int)(ha*40*IE(i))));
            if(g_darkUI)MoonIcon(dl,V(c,cy+oy),IA(i,COL_INK),COL_PANELL);else SunIcon(dl,V(c,cy+oy),IA(i,COL_INK));
            if(click&&hov){g_themeMode=g_darkUI?1:2;ApplyThemeMode();SaveConfig();g_deskDirty=true;}
            if(hov){tip=g_darkUI?"Switch to light":"Switch to dark";tipX=c;} } break;
        case BIT_MIC: {
            float c=sl.pos+15; bool hov=hitC(c,half); float ha=HoverAnim(HID+3102,hov);
            if(ha>0.01f)dl->AddCircleFilled(V(c,cy+oy),half,WithA(COL_INK2,(int)(ha*40*IE(i))));
            MicIcon(dl,V(c,cy+oy),IA(i,g_micMuted?COL_INK2:COL_INK),g_micMuted);
            if(click&&hov){g_micMuted=!g_micMuted;SetMicMute(g_micMuted);}
            if(hov){tip=g_micMuted?"Microphone muted":"Microphone on";tipX=c;} } break;
        case BIT_WORKSPACES: {
            // Was driven by the legacy g_wsCount/g_wsCur globals, which only ever describe the
            // FOCUSED monitor - so a second monitor's bar drew (and switched) the wrong ring. The
            // vertical strip already used the per-monitor ring; this one now matches it.
            const WsRing& WR=WsRingFor(mi);
            int wsn=std::min(WR.count,64);
            for(int k=0;k<wsn;k++){ float c=sl.pos+18+k*16.0f; bool act=(k==WR.cur);
                bool occ=(k<WR.count)&&WR.occ[k];
                bool wh=hitC(c,8); float wa=HoverAnim(HID+3300+k,wh);
                if(act)dl->AddRectFilled(V(c-6,cy-3),V(c+6,cy+3),IA(i,COL_GOLD),3);
                else dl->AddCircleFilled(V(c,cy),(occ?3.2f:2.6f)+wa*1.2f,
                        IA(i,WithA(COL_INK2,(int)((occ?205:150)+wa*80))));
                // SwitchWorkspace() fakes Win+Ctrl+Arrow, which is the VIRTUAL DESKTOP gesture and
                // does nothing at all when komorebi owns the workspaces - so on this bar the chips
                // were simply dead under komorebi. GotoWorkspace picks the right backend, and it is
                // also the path that snapshots the outgoing windows for the slide animation.
                if(click&&wh)GotoWorkspace(mi,k); } } break;
        case BIT_WINDOWINFO: {
            dl->AddCircleFilled(V(sl.pos+4,cy),3.0f,IA(i,COL_GOLD));
            TextAt(dl,g_fMed,15,V(sl.pos+14,cy-9),IA(i,COL_INK),wiClip.c_str()); } break;
        case BIT_CALENDAR: {
            float c=sl.pos+15; bool hov=hitC(c,half);
            if(hov){dl->AddCircleFilled(V(c,cy),half,WithA(COL_INK2,40));tip="Calendar";tipX=c;}
            CalIcon(dl,V(c,cy),IA(i,COL_INK2));
            if(click&&hov){g_tab=0;g_drawerForceUntil=GetTickCount64()+4000;} } break;
        case BIT_CLOCK: {
            float t0=sl.pos+7;
            TextAt(dl,g_fMed,16,V(t0,cy-9),IA(i,COL_INK),clkTxt);
            if(*clkAp)TextAt(dl,g_fSml,12,V(t0+TextW(g_fMed,16,clkTxt),cy-6),IA(i,COL_GOLD),clkAp); } break;
        case BIT_WIFI: {
            float c=sl.pos+14; bool hov=hitC(c,half);
            if(hov){dl->AddCircleFilled(V(c,cy),half,WithA(COL_INK2,40));tip=g_st.online?"Network":"Offline";tipX=c;}
            WifiIcon(dl,V(c,cy-2),IA(i,g_st.online?COL_GOLD:COL_INK2));
            if(click&&hov){g_sideView=1;RefreshWifi();g_sideForceUntil=GetTickCount64()+3500;} } break;
        case BIT_BT: {
            float c=sl.pos+14; bool hov=hitC(c,half);
            if(hov){dl->AddCircleFilled(V(c,cy),half,WithA(COL_INK2,40));tip="Bluetooth";tipX=c;}
            BtIcon(dl,V(c,cy),IA(i,COL_INK2));
            if(click&&hov){g_sideView=2;RefreshBt();g_sideForceUntil=GetTickCount64()+3500;} } break;
        case BIT_BATT: {
            TextAt(dl,g_fSml,13,V(sl.pos+4,cy-7),IA(i,g_st.charging?COL_GOLD:COL_INK2),battTxt); } break;
        case BIT_POWER: {
            float c=sl.pos+15; bool hov=hitC(c,half);
            if(hov){dl->AddCircleFilled(V(c,cy),half,IM_COL32(224,130,120,50));tip="Power";tipX=c;}
            PowerIcon(dl,V(c,cy),IA(i,hov?IM_COL32(240,120,110,255):IM_COL32(214,110,100,255)));
            if(click&&hov)g_sessShow=true; } break;
        case BIT_NOTIF: {
            // Same reasoning as the vertical strip: a bell with an unread DOT, not a number - at bar
            // size a digit is unreadable and just muddies the glyph.
            float c=sl.pos+15; bool hov=hitC(c,half); float ha=HoverAnim(HID+3106,hov);
            if(ha>0.01f)dl->AddCircleFilled(V(c,cy+oy),half,WithA(COL_INK2,(int)(ha*40*IE(i))));
            ImU32 col=IA(i, g_dnd? WithA(COL_INK2,110) : COL_INK2);
            ImVec2 bc=V(c,cy+oy);
            dl->PathArcTo(V(bc.x,bc.y+1.5f),6.5f,3.1416f,6.2832f,14);
            dl->PathLineTo(V(bc.x+8.0f,bc.y+4.0f)); dl->PathLineTo(V(bc.x-8.0f,bc.y+4.0f));
            dl->PathStroke(col,ImDrawFlags_Closed,1.7f);
            dl->AddLine(V(bc.x-2.2f,bc.y+6.4f),V(bc.x+2.2f,bc.y+6.4f),col,1.7f);
            if(g_dnd) dl->AddLine(V(bc.x-8,bc.y+8),V(bc.x+8,bc.y-8),IA(i,COL_ERR),1.8f);
            else if(notifN>0){ ImVec2 bp=V(bc.x+6.5f,bc.y-6.0f);
                dl->AddCircleFilled(bp,5.0f, g_darkUI?IM_COL32(18,18,22,255):PanelCol(255));
                dl->AddCircleFilled(bp,3.4f, IA(i,COL_GOLD)); }
            if(hov){ tip = g_dnd? "Do not disturb (right-click to allow)" : "Notifications (right-click for DND)"; tipX=c; }
            if(click&&hov){ g_notifForceUntil = (GetTickCount64()<g_notifForceUntil)? 0
                                                                                    : GetTickCount64()+8000; }
            if(rclick&&hov){ g_dnd=!g_dnd; SaveConfig(); appTookRclick=true; } } break;
        case BIT_ETH: {
            // RJ45 plug: body + latch tab + contact pins. The horizontal bar has no in-bar flyout
            // (SysFlyout is built around a vertical strip), so this opens the Network panel in the
            // sidebar, which is where the horizontal wifi/bluetooth buttons already go.
            float c=sl.pos+14; bool hov=hitC(c,half); float ha=HoverAnim(HID+3113,hov);
            if(ha>0.01f)dl->AddCircleFilled(V(c,cy+oy),half,WithA(COL_INK2,(int)(ha*40*IE(i))));
            ImU32 col=IA(i,hov?COL_INK:COL_INK2); ImVec2 ec=V(c,cy+oy);
            dl->AddRect(V(ec.x-7,ec.y-7),V(ec.x+7,ec.y+4),col,2.0f,0,1.7f);
            dl->AddRectFilled(V(ec.x-2.5f,ec.y+4),V(ec.x+2.5f,ec.y+8),col,1.0f);
            for(int q=-2;q<=2;q++) dl->AddRectFilled(V(ec.x+q*3.0f-0.6f,ec.y-5),V(ec.x+q*3.0f+0.6f,ec.y-1),col,0.5f);
            if(hov){ tip="Ethernet"; tipX=c; }
            if(click&&hov){ g_sideView=1; RefreshEth(); RefreshWifi(); g_sideForceUntil=GetTickCount64()+3500; } } break;
        case BIT_AUDIO: {
            float c=sl.pos+15; bool hov=hitC(c,half); float ha=HoverAnim(HID+3103,hov);
            if(ha>0.01f)dl->AddCircleFilled(V(c,cy+oy),half,WithA(COL_INK2,(int)(ha*40*IE(i))));
            SpeakerIcon(dl,V(c,cy+oy),IA(i,hov?COL_INK:COL_INK2));
            if(hov){ static char vt2[24]; snprintf(vt2,24,"Volume %d%%",(int)(std::clamp(g_volCache,0.0f,1.0f)*100));
                     tip = g_volCache>=0.0f? vt2 : "Volume mixer"; tipX=c; }
            if(click&&hov){ g_sideView=4; g_sideForceUntil=GetTickCount64()+4000; } } break;
        case BIT_TRAY: {
            // Real Windows tray icons. The vertical strip can dwell-scrape an app's menu and morph it
            // out of the bar; that panel is SysFlyout, which only knows how to grow sideways out of a
            // vertical strip. Here a right-click forwards to the app's OWN menu instead, which every
            // tray app supports and which lands in the right place on a bottom bar.
            // The chevron leads, then the promoted icons - so the handle keeps its place in the bar
            // no matter how many icons the user has pinned or how many apps come and go.
            if(trayChev){ float c=sl.pos+8+trayChevW*0.5f;
                // keep the anchor live so the panel tracks the chevron across a re-layout
                if(g_trayFlyMon==mi){ g_trayFlyDir=onTop?1:0; g_trayFlyAt=V(c, onTop? by+bh : by); }
                bool ch=hitC(c,trayChevW*0.5f); float ha=HoverAnim(HID+3600,ch||g_trayOpen);
                if(ha>0.01f) dl->AddRectFilled(V(c-12,cy+oy-12),V(c+12,cy+oy+12),
                                               IA(i,g_trayOpen?AccA((int)(60+ha*40)):WithA(COL_INK2,(int)(ha*52))),8);
                ChevronGlyph(dl,V(c,cy+oy),5.5f,IA(i,WithA(g_trayOpen?COL_INK:COL_INK2,235)),g_trayAnim,onTop?1:0);
                if(trayHidden>0 && !g_trayOpen)   // a dot says "there is something in here"
                    dl->AddCircleFilled(V(c+9,cy+oy-8),2.4f,IA(i,AccA(230)));
                if(ch && !g_trayOpen){ tip="System tray"; tipX=c; }
                if(click&&ch){ if(g_trayOpen) g_trayOpen=false;
                    else { g_trayOpen=true; g_trayFlyMon=mi; g_trayFlyDir=onTop?1:0;
                           g_trayFlyAt=V(c, onTop? by+bh : by); } } }
            float trayX0=sl.pos+8+(trayChev?trayChevW:0.0f);
            for(int k=0;k<trayShown;k++){ auto* t=barTray[k];
                float c=trayX0+k*trayStepH+trayStepH*0.5f;
                bool th=io.MousePos.x>c-trayStepH*0.5f&&io.MousePos.x<c+trayStepH*0.5f&&
                        io.MousePos.y>by&&io.MousePos.y<by+bh;
                float ha=HoverAnim(HID+3500+k,th);
                if(ha>0.01f){ float sq=13.0f*(0.9f+0.1f*ha);
                    dl->AddRectFilled(V(c-sq,cy+oy-sq),V(c+sq,cy+oy+sq),AccA((int)(ha*46*IE(i))),sq*0.56f); }
                float ts=20+ha*2;
                dl->AddImage((ImTextureID)t->tex,V(c-ts/2,cy+oy-ts/2),V(c+ts/2,cy+oy+ts/2),
                             ImVec2(0,0),ImVec2(1,1),IA(i,IM_COL32(255,255,255,255)));
                if(th){ if(!t->tip.empty()){ tipStore=W2U8(t->tip); size_t nl=tipStore.find(0x0A);
                            if(nl!=std::string::npos)tipStore=tipStore.substr(0,nl);
                            tip=tipStore.c_str(); tipX=c; }
                    int sx=(int)(g_vs.left+c*g_uiScale), sy=(int)(g_vs.top+inner*g_uiScale);
                    // middle-click demotes it back behind the chevron - the mirror of the flyout's
                    // pin badge, and the only spare button (right-click belongs to the app's menu).
                    if(io.MouseClicked[2]) TrayToggleShown(*t);
                    else if(click) TrayForward(*t,0x201,0x202,sx,sy);
                    if(rclick){ TrayForward(*t,0x204,0x205,sx,sy); appTookRclick=true; } } } } break;
        case BIT_APPS: {
            float appLeft=sl.pos+4;
            for(int k=0;k<n;k++){ auto&a=*mine[k]; int ii=i;
                float ix=appLeft+k*ai; ImVec2 ip=V(ix,cy-isz/2);
                bool hov=io.MousePos.x>ip.x-4&&io.MousePos.x<ip.x+isz+4&&io.MousePos.y>by&&io.MousePos.y<by+bh;
                float ha=HoverAnim(HID+3000+k,hov); float ex=ha*2.5f;
                if(ha>0.01f)dl->AddRectFilled(V(ip.x-4-ex,ip.y-4-ex),V(ip.x+isz+4+ex,ip.y+isz+4+ex),WithA(COL_INK2,(int)(ha*46*IE(ii))),9);
                if(a.icon)dl->AddImage((ImTextureID)a.icon,ip,V(ip.x+isz,ip.y+isz),ImVec2(0,0),ImVec2(1,1),IA(ii,IM_COL32(255,255,255,255)));
                bool active=DockGroupHasFg(a); bool multi=a.wins.size()>1;
                // running indicator: a line on the desktop-facing edge (top bar -> bottom of icon; bottom bar -> top)
                auto mark=[&](float t0,float t1,float w,ImU32 col){ float e0=inner+(onTop?-1:1)*2, e1=inner+(onTop?-1:1)*w;
                    dl->AddRectFilled(V(ip.x+isz*t0,std::min(e0,e1)),V(ip.x+isz*t1,std::max(e0,e1)),col,2); };
                if(active)mark(multi?0.15f:0.25f,multi?0.85f:0.75f,4.5f,IA(ii,COL_GOLD));
                else if(a.minimized)mark(0.44f,0.56f,4.0f,IA(ii,WithA(COL_INK2,110)));
                else mark(multi?0.28f:0.38f,multi?0.72f:0.62f,4.0f,IA(ii,WithA(COL_INK2,150)));
                // ...and never while the right-click menu is up. The preview is a REAL window
                // (CaelestiaThumb) drawn over the menu, so without this it covers the menu and eats
                // every click on it - which is exactly why "Pin to taskbar" did nothing. The
                // vertical bar has always had this guard; the horizontal one never did.
                if(hov && !g_appMenu){ if(g_barPreviews) ShowThumb(a.hwnd,a.title.empty()?"(untitled)":a.title,
                            (int)(g_vs.left+(ix+isz*0.5f)*g_uiScale),
                            onTop?(int)(g_vs.top+(by+bh+8)*g_uiScale):(int)(g_vs.top+(by-8)*g_uiScale)-THUMBH);
                         anyThumb=g_barPreviews; }
                if(click&&hov){ DockGroupClick(a); HideThumb(); anyThumb=false; }
                if(rclick&&hov){ HideThumb(); anyThumb=false; appTookRclick=true;
                    // bottom bar -> the menu grows UP from the strip's top edge; top bar -> down
                    OpenAppMenu(a,mi,V(ix,onTop? by+bh+6 : by-6), 1, onTop? 0 : -1); }
            } } break;
        default: break;                                     // spacers draw nothing
        }
    }
    if(anyThumb)g_thumbWanted=true;

    // ---- right-click menu (opens above a bottom bar / below a top bar) ----
    { bool onStrip=io.MousePos.x>bx&&io.MousePos.x<bx+barW&&io.MousePos.y>by&&io.MousePos.y<by+bh;
      if(rclick&&onStrip&&!appTookRclick){ g_barMenu=true; g_barMenuMon=mi; g_barMenuOpenedAt=GetTickCount64();
          float mw=200,mh=6*34+12;
          float my0 = onTop? by+bh+8 : by-mh-8;
          g_barMenuAt=V(std::min(std::max(io.MousePos.x-mw*0.5f,bx),bx+barW-mw),my0); }
      bool mine2=(g_barMenuMon==mi);
      float mt=(g_barMenu&&mine2)?1.0f:0.0f;
      if(mine2){ g_barMenuAnim+=(mt-g_barMenuAnim)*std::min(1.0f,g_frameDt*18.0f); if(fabsf(mt-g_barMenuAnim)<0.002f)g_barMenuAnim=mt; }
      if(mine2&&g_barMenuAnim>0.004f){
          float a=g_barMenu?EaseOutBack(std::clamp(g_barMenuAnim,0.0f,1.0f)):EaseOutCubic(g_barMenuAnim);
          float af=std::clamp(g_barMenuAnim,0.0f,1.0f); int al=(int)(af*255);
          const char* items[6]={"Snip a screenshot","Auto-hide taskbar","Keep awake","Settings","Restart shell","Exit shell"};
          float mw=200,rowH=34,mh=rowH*6+12;
          float mx0=g_barMenuAt.x, my0=g_barMenuAt.y+(1.0f-a)*(onTop?-8.0f:8.0f);
          ImVec2 m0=V(mx0,my0),m1=V(mx0+mw,my0+mh); GlassPanel(dl,m0,m1,12,0,nullptr,af);
          for(int i=0;i<6;i++){ float ra=EaseOutCubic(Stagger(af,i,0.05f,0.5f)); float ry=m0.y+6+i*rowH+(1.0f-ra)*8.0f;
              bool rh=g_barMenu&&io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>ry&&io.MousePos.y<ry+rowH;
              if(rh)dl->AddRectFilled(V(m0.x+5,ry+1),V(m1.x-5,ry+rowH-1),AccA((int)(af*26)),8);
              TextAt(dl,g_fSml,15,V(m0.x+34,ry+9),WithA(COL_INK,(int)(al*ra)),items[i]);
              bool checked=(i==1&&g_barAutoHide)||(i==2&&g_keepAwake);
              if(checked){ ImVec2 k=V(m0.x+18,ry+rowH*0.5f); dl->AddLine(V(k.x-5,k.y),V(k.x-1,k.y+4),AccA((int)(al*ra)),2.0f); dl->AddLine(V(k.x-1,k.y+4),V(k.x+6,k.y-4),AccA((int)(al*ra)),2.0f); }
              if(click&&rh){ if(i==0){g_barMenu=false;g_snipPending=8;} else if(i==1){g_barAutoHide=!g_barAutoHide;SaveConfig();}
                  else if(i==2){g_keepAwake=!g_keepAwake;ApplyKeepAwake();} else if(i==3)g_setShow=true;
                  else if(i==4){ RestartShell(); }
                  else PostMessageW(g_hwnd,WM_CLOSE,0,0); g_barMenu=false; } }
          if(g_barMenu){ BS.rect.left=(LONG)std::min((float)BS.rect.left,m0.x); BS.rect.top=(LONG)std::min((float)BS.rect.top,m0.y);
              BS.rect.right=(LONG)std::max((float)BS.rect.right,m1.x); BS.rect.bottom=(LONG)std::max((float)BS.rect.bottom,m1.y);
              bool inMenu=io.MousePos.x>m0.x&&io.MousePos.x<m1.x&&io.MousePos.y>m0.y&&io.MousePos.y<m1.y;
              if((click||rclick)&&!inMenu&&!onStrip)g_barMenu=false;
              static MenuDismiss dis;
              if(MenuOutsideClick(dis,inMenu,g_barMenuOpenedAt)) g_barMenu=false;
              if(ImGui::IsKeyPressed(ImGuiKey_Escape)) g_barMenu=false; } }
    }

    TrayFlyout(dl,io,mi,BS.rect);      // the chevron's panel: on top of the bar, under the tooltip

    // tooltip above (bottom bar) / below (top bar) the item
    if(tip){ float tw=TextW(g_fSml,15,tip)+22,th=30; float ty= onTop? by+bh+8 : by-8-th;
        ImVec2 a0=V(tipX-tw/2,ty),b0=V(tipX+tw/2,ty+th);
        dl->AddRectFilled(a0,b0,IM_COL32(30,28,36,247),8); dl->AddRect(a0,b0,IM_COL32(255,255,255,22),8,0,1.0f);
        TextAt(dl,g_fSml,15,V(a0.x+11,ty+7),IM_COL32(232,230,238,255),tip); }
}

// The taskbar: a THIN strip living in the desktop bubble's left margin (not on top of it), with
// the same corner radius and material as the bubble surround (defect D3). Its contents cascade in.
// NOTE: no recorder and no system tray here — both moved into the bottom-right quick settings.
