// src/services/Komorebi.h  —  Aether shell
// Workspace source: komorebi (github.com/LGUG2Z/komorebi), the tiling window manager for Windows.
// Aether is a GPL-3.0 derivative of Caelestia Shell.
//
// Caelestia's bar reads workspaces from Hyprland (services/Hypr.qml): a ring of workspaces per
// monitor, each with the toplevels living on it, plus which one is focused. Windows virtual desktops
// are a poor stand-in for that - they have no names, no per-monitor rings, and no event stream - and
// komorebi is the thing on Windows that actually has all three. So when komorebi is running, it is
// the workspace source, and virtual desktops are the fallback.
//
// Transport: `komorebic subscribe-pipe <name>` makes komorebi connect to a named pipe WE host and
// push the complete state as JSON on every event - the same push model Hypr.qml gets from the
// Hyprland event socket. `komorebic state` is used once per (re)connect so the bar is correct
// before the first event arrives.
#pragma once
#include <cstdarg>

// ---------------------------------------------------------------------------------------------
// A very small JSON reader. komorebi's state document is deeply nested (Ring<Monitor> ->
// Ring<Workspace> -> Ring<Container> -> Ring<Window>), so the flat jkey/jstr helpers this shell
// uses for its own config cannot walk it, and scanning for substrings across a 100KB document is
// how you end up reading a window title as a workspace name.
// ---------------------------------------------------------------------------------------------
namespace kjson {

struct Val;
using Obj = std::vector<std::pair<std::string, Val>>;   // insertion-ordered; these are small
using Arr = std::vector<Val>;

struct Val {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::shared_ptr<Arr> a;
    std::shared_ptr<Obj> o;

    const Val* find(const char* key) const {
        if (t != OBJ || !o) return nullptr;
        for (auto& kv : *o) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    // Ring<T> serialises as { "elements": [...], "focused": n }. Everywhere komorebi hands us a
    // ring we want the elements, and a couple of fields are a bare array instead depending on the
    // version, so accept both shapes at the point of use rather than at every call site.
    const Arr* elems() const {
        if (t == ARR && a) return a.get();
        if (const Val* e = find("elements")) if (e->t == ARR && e->a) return e->a.get();
        return nullptr;
    }
    int focused() const { const Val* f = find("focused"); return f && f->t == NUM ? (int)f->n : 0; }
    std::string str(const char* key, const char* dflt = "") const {
        const Val* v = find(key); return (v && v->t == STR) ? v->s : std::string(dflt);
    }
    double num(const char* key, double dflt = 0) const {
        const Val* v = find(key); return (v && v->t == NUM) ? v->n : dflt;
    }
};

struct Parser {
    const char* p; const char* e;
    Parser(const std::string& src) : p(src.c_str()), e(src.c_str() + src.size()) {}
    void ws() { while (p < e && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r')) p++; }
    bool lit(const char* s) { size_t n = strlen(s);
        if ((size_t)(e-p) < n || strncmp(p, s, n)) return false; p += n; return true; }
    bool str(std::string& out) {
        if (p >= e || *p != '"') return false; p++;
        out.clear();
        while (p < e && *p != '"') {
            if (*p == '\\') {
                p++; if (p >= e) return false;
                switch (*p) {
                case 'n': out += '\n'; break;   case 't': out += '\t'; break;
                case 'r': out += '\r'; break;   case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (e - p < 5) return false;
                    unsigned cp = (unsigned)strtoul(std::string(p+1, p+5).c_str(), nullptr, 16);
                    p += 4;
                    // UTF-16 surrogate pair -> one code point
                    if (cp >= 0xD800 && cp <= 0xDBFF && e - p >= 7 && p[1] == '\\' && p[2] == 'u') {
                        unsigned lo = (unsigned)strtoul(std::string(p+3, p+7).c_str(), nullptr, 16);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp-0xD800)<<10) + (lo-0xDC00); p += 6; }
                    }
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) { out += (char)(0xC0|(cp>>6)); out += (char)(0x80|(cp&0x3F)); }
                    else if (cp < 0x10000) { out += (char)(0xE0|(cp>>12)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
                    else { out += (char)(0xF0|(cp>>18)); out += (char)(0x80|((cp>>12)&0x3F));
                           out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
                } break;
                default: out += *p; break;
                }
                p++;
            } else out += *p++;
        }
        if (p >= e) return false;
        p++; return true;
    }
    bool val(Val& v, int depth = 0) {
        if (depth > 64) return false;                 // the schema is nowhere near this deep
        ws(); if (p >= e) return false;
        if (*p == '"') { v.t = Val::STR; return str(v.s); }
        if (*p == '{') {
            p++; v.t = Val::OBJ; v.o = std::make_shared<Obj>();
            ws(); if (p < e && *p == '}') { p++; return true; }
            for (;;) {
                ws(); std::string k; if (!str(k)) return false;
                ws(); if (p >= e || *p != ':') return false; p++;
                Val child; if (!val(child, depth+1)) return false;
                v.o->emplace_back(std::move(k), std::move(child));
                ws(); if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == '}') { p++; return true; }
                return false;
            }
        }
        if (*p == '[') {
            p++; v.t = Val::ARR; v.a = std::make_shared<Arr>();
            ws(); if (p < e && *p == ']') { p++; return true; }
            for (;;) {
                Val child; if (!val(child, depth+1)) return false;
                v.a->push_back(std::move(child));
                ws(); if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == ']') { p++; return true; }
                return false;
            }
        }
        if (lit("true"))  { v.t = Val::BOOL; v.b = true;  return true; }
        if (lit("false")) { v.t = Val::BOOL; v.b = false; return true; }
        if (lit("null"))  { v.t = Val::NUL;  return true; }
        { char* end = nullptr; double d = strtod(p, &end);
          if (end == p) return false; p = end; v.t = Val::NUM; v.n = d; return true; }
    }
};

inline bool parse(const std::string& src, Val& out) { Parser pr(src); return pr.val(out); }

} // namespace kjson

// ---------------------------------------------------------------------------------------------
// The snapshot the bar reads
// ---------------------------------------------------------------------------------------------
struct KomoWs {
    std::string name;                       // "I", "II", ... (komorebi.json) or "" when unnamed
    std::vector<std::wstring> exes;         // one per window, in stack order
    std::vector<HWND> hwnds;                // same order - komorebi's state carries the handles, and
                                            // they are what lets the shell hide a workspace itself
};
struct KomoMon {
    // komorebi's own stable handle for the display (its `id`). A monitor's POSITION in komorebi's array is not
    // stable - it reorders as monitors are focused, added or removed - so anything that has to mean "the same
    // screen as last time" keys off this, never off the index.
    long long id = 0;
    RECT rect{0,0,0,0};                     // so we can line komorebi's monitors up with ours
    RECT work{0,0,0,0};                     // komorebi's work area (Windows' work area as komorebi last read it)
    int  focused = 0;
    std::vector<KomoWs> ws;
};

static std::mutex             g_komoMtx;
static std::vector<KomoMon>   g_komo;               // guarded by g_komoMtx
static int                    g_komoFocusedMon = 0; // guarded by g_komoMtx
static std::atomic<bool>      g_komoLive{false};    // komorebi is running AND we have its state
static std::atomic<bool>      g_komoRun{false};     // reader thread should keep going

// ---- the window index: "is this komorebi's, and where?" answered without g_komoMtx -------------
//
// The WinEvent hooks (the tiling glide, the workspace slide) fire for EVERY window move on the
// system - a drag, a resize, an app animating its own window, a video player. Both used to answer
// that question by taking g_komoMtx and walking monitors x workspaces x windows. Two things went
// wrong with that:
//   * g_komoMtx is the lock the komorebi reader holds while it swaps a workspace, and swapping does
//     window work on foreign processes (a hung app makes SetWindowPos wait). Anything asking the
//     question at that moment waited with it - and the hooks were delivered to the RENDER thread, so
//     the whole shell froze. That is the multi-second "render loop has not drawn a frame" in
//     errors.log;
//   * even uncontended it is a linear scan on a path that can fire thousands of times a second.
// So the answer is precomputed once per state ingest and read under its own short-lived lock, and
// the hooks now live on their own thread (see WorkspaceSlide2.h).
struct KomoLoc { int mon, ws; };
static std::mutex                       g_komoIdxMtx;
static std::unordered_map<HWND,KomoLoc> g_komoIdx;              // guarded by g_komoIdxMtx
static int                              g_komoIdxFocus[16]={};  // focused workspace per monitor
static RECT                             g_komoIdxRect[16]={};   // monitor rect, as komorebi sees it
static int                              g_komoIdxMons = 0;

static void KomoIndexBuild(const std::vector<KomoMon>& mons){
    std::unordered_map<HWND,KomoLoc> idx; idx.reserve(64);
    int  foc[16]={}; RECT rc[16]={};
    int  n = std::min((int)mons.size(), 16);
    for(int m=0;m<n;m++){
        foc[m]=mons[m].focused; rc[m]=mons[m].rect;
        for(int w=0;w<(int)mons[m].ws.size();w++)
            for(HWND h:mons[m].ws[w].hwnds) if(h){ idx[h]=KomoLoc{m,w}; Ws2NoteRect(h); TaNoteRect(h); }
    }
    // Which windows are NEW since the last state? This is the only place the shell can know that a
    // window has just joined the layout - komorebi's event stream says "a workspace changed", never
    // "this window is new". Skipped on the very first build, or every window already open when the
    // shell starts would play an entrance at once.
    std::vector<HWND> fresh;
    {
        std::lock_guard<std::mutex> lk(g_komoIdxMtx);
        static bool seeded=false;
        if(seeded) for(auto& kv:idx) if(!g_komoIdx.count(kv.first)) fresh.push_back(kv.first);
        seeded=true;
        g_komoIdx.swap(idx); g_komoIdxMons=n;
        for(int m=0;m<n;m++){ g_komoIdxFocus[m]=foc[m]; g_komoIdxRect[m]=rc[m]; }
    }
    for(HWND h:fresh) TaOpenAnim(h);
}
static bool KomoIndexFind(HWND h,int& mon,int& ws,int& focused){
    std::lock_guard<std::mutex> lk(g_komoIdxMtx);
    auto it=g_komoIdx.find(h); if(it==g_komoIdx.end()) return false;
    mon=it->second.mon; ws=it->second.ws;
    focused = (mon>=0 && mon<g_komoIdxMons) ? g_komoIdxFocus[mon] : 0;
    return true;
}
static bool KomoIndexManaged(HWND h){
    std::lock_guard<std::mutex> lk(g_komoIdxMtx);
    return g_komoIdx.find(h)!=g_komoIdx.end();
}
static bool KomoIndexMonRect(int mon,RECT& r){
    std::lock_guard<std::mutex> lk(g_komoIdxMtx);
    if(mon<0 || mon>=g_komoIdxMons) return false;
    r=g_komoIdxRect[mon]; return true;
}
static int KomoIndexMonCount(){ std::lock_guard<std::mutex> lk(g_komoIdxMtx); return g_komoIdxMons; }
// ---- hiding a workspace instantly ---------------------------------------------------------------
// komorebi's "Cloak" hiding is the one that vanishes a window immediately, but on Windows 11 26100 it
// panics komorebi dead (com/mod.rs, REGDB_E_CLASSNOTREG), so the config has to use "Minimize". And
// Minimize is visibly slow: SW_MINIMIZE runs the shell minimize animation, and with no taskbar for
// the window to fly into, Electron apps in particular linger on screen for seconds.
//
// So the shell does the hiding itself. komorebi's state carries every window's HWND, so on a switch
// Aether cloaks the outgoing workspace's windows directly - DwmSetWindowAttribute(DWMWA_CLOAK) takes
// effect on the next compose, with no animation and nothing left behind - and uncloaks the arriving
// ones. komorebi still does its own minimize/restore underneath; this just gets there first.
//
// Everything cloaked is remembered so it can be put back: a window left cloaked is a window the user
// cannot see or recover, so KomoUncloakAll runs on stop and on shell exit.
// WHY NOT DWM CLOAK: DwmSetWindowAttribute(DWMWA_CLOAK) is documented as being for the window's
// OWNER to use, and on another process's window it silently does nothing - no error, no effect. That
// was verified here: FastSwap ran with the right handles and DWMWA_CLOAKED still read back false
// every time. komorebi gets away with it because it calls the undocumented user32 SetCloak, which is
// not exported by name and so is not available to us.
//
// Moving the window off-screen is the thing that works on someone else's window, needs no
// undocumented entry point, and takes effect on the same frame. The real position is remembered so
// it can be put back, and putting it back is what KomoUncloakAll does on stop and on shell exit -
// a window parked at -32000 with no record of where it came from is a lost window.
static std::mutex                       g_cloakMtx;
static std::unordered_map<HWND,RECT>    g_hidden;   // hwnd -> where it was, guarded by g_cloakMtx
static const int HIDE_X = -32000, HIDE_Y = -32000;
// g_fastHide lives in WorkspaceSlide.h: SaveConfig/LoadConfig sit ~3800 lines above this include,
// and a setting has to be declared before the code that persists it.

static void KomoCloak(HWND h, bool on) {
    if (!h || !IsWindow(h)) return;
    if (on) {
        RECT r{};
        if (!GetWindowRect(h, &r)) return;
        if (r.left <= HIDE_X + 1000) return;            // already parked
        { std::lock_guard<std::mutex> lk(g_cloakMtx); g_hidden[h] = r; }
        SetWindowPos(h, nullptr, HIDE_X, HIDE_Y, 0, 0,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE | SWP_NOSENDCHANGING);
    } else {
        // Putting the window back where WE found it is wrong: komorebi may have restored it from a
        // minimize in the meantime (to the off-screen spot we ourselves set), and the correct place
        // depends on the layout, which is komorebi's business. Measured: restoring by hand left the
        // window stranded at -32000, and `komorebic retile` put it right. So drop the record and let
        // komorebi lay the workspace out - that is the only thing that knows the answer.
        std::lock_guard<std::mutex> lk(g_cloakMtx);
        g_hidden.erase(h);
    }
}
static void KomoUncloakAll() {
    std::unordered_map<HWND,RECT> v;
    { std::lock_guard<std::mutex> lk(g_cloakMtx); v.swap(g_hidden); }
    // On the way out the rect IS the right answer - komorebi is not going to retile for us after
    // the shell is gone, so put every hidden window back exactly where it was.
    for (auto& kv : v) {
        if (!IsWindow(kv.first)) continue;
        SetWindowPos(kv.first, nullptr, kv.second.left, kv.second.top, 0, 0,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE | SWP_NOSENDCHANGING);
    }
}
static void KomoLog(const char* fmt, ...);   // fwd: defined with the pipe reader below
static bool KomoRun(const std::wstring& args, std::string* out);   // fwd: defined just below
// Hide `oldWs` and reveal `newWs` on one monitor, immediately.
static void KomoFastSwap(const KomoMon& km, int oldWs, int newWs) {
    if (!g_fastHide.load()) return;
    if (newWs >= 0 && newWs < (int)km.ws.size())
        for (HWND h : km.ws[newWs].hwnds) KomoCloak(h, false);   // arriving: reveal first
    if (oldWs >= 0 && oldWs < (int)km.ws.size()) {
        const std::vector<HWND>& out = km.ws[oldWs].hwnds;
        // WHY THIS NO LONGER HIDES ANYTHING.
        //
        // Parking the leaving windows off-screen worked - and it wedged Chromium. Discord came back
        // as a blank grey rectangle and stayed that way: minimize/restore, a real resize and a forced
        // RedrawWindow all failed to bring it back, because Chromium suspends its compositor when a
        // window is moved off-screen and does not always rebuild the surface afterwards. Only
        // restarting the app recovered it.
        //
        // And it bought nothing. Timed on this machine: our off-screen hide landed at 115-138ms,
        // komorebi's own minimize at ~124ms. Same speed, and one of them destroys Electron apps.
        //
        // So komorebi does the hiding, as it always did. The handover below is kept because it costs
        // nothing and is what the animator would need if the outgoing half ever becomes reachable.
        WsSlideAdoptOutgoing(out);
    }

}

static std::atomic<ULONGLONG> g_komoStamp{0};       // tick of the last accepted snapshot
// Bumped each time komorebi goes from down to up. Anything that must happen ONCE PER KOMOREBI RUN -
// re-applying the scrolling layout, adopting the windows that were already open - keys off this. A
// timestamp cannot do that job: g_komoStamp changes on every snapshot, so it can never tell "same
// komorebi, new snapshot" apart from "komorebi restarted".
static std::atomic<unsigned> g_komoSession{0};

// Run a komorebic command, optionally capturing stdout. Returns false if the process could not be
// started at all (komorebic missing), which is how "komorebi is not installed" is detected.
static bool KomoRun(const std::wstring& args, std::string* out) {
    HANDLE rd = nullptr, wr = nullptr;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    if (out && !CreatePipe(&rd, &wr, &sa, 1 << 20)) return false;
    if (out) SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | (out ? STARTF_USESTDHANDLES : 0);
    si.wShowWindow = SW_HIDE;
    if (out) { si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = nullptr; }

    std::wstring cmd = L"komorebic.exe " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, out ? TRUE : FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (out) CloseHandle(wr);
    if (!ok) { if (out) CloseHandle(rd); return false; }

    if (out) {
        out->clear(); char tmp[8192]; DWORD got = 0;
        while (ReadFile(rd, tmp, sizeof(tmp), &got, nullptr) && got) out->append(tmp, got);
        CloseHandle(rd);
    }
    WaitForSingleObject(pi.hProcess, out ? 8000 : 4000);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code == 0;
}

// Turn one parsed komorebi State document into our snapshot.
static bool KomoIngestVal(const kjson::Val& root, bool fromEvent) {
    if (root.t != kjson::Val::OBJ) return false;
    const kjson::Val* mons = root.find("monitors");
    if (!mons) return false;
    const kjson::Arr* melems = mons->elems();
    if (!melems || melems->empty()) return false;

    std::vector<KomoMon> out;
    for (const kjson::Val& m : *melems) {
        KomoMon km;
        km.id = (long long)m.num("id");
        if (const kjson::Val* sz = m.find("size")) {
            km.rect.left   = (LONG)sz->num("left");
            km.rect.top    = (LONG)sz->num("top");
            // komorebi's Rect is left/top/right/bottom where right/bottom are WIDTH and HEIGHT
            km.rect.right  = km.rect.left + (LONG)sz->num("right");
            km.rect.bottom = km.rect.top  + (LONG)sz->num("bottom");
        }
        km.work = km.rect;
        if (const kjson::Val* wa = m.find("work_area_size")) {
            km.work.left   = (LONG)wa->num("left");
            km.work.top    = (LONG)wa->num("top");
            km.work.right  = km.work.left + (LONG)wa->num("right");
            km.work.bottom = km.work.top  + (LONG)wa->num("bottom");
        }
        const kjson::Val* wss = m.find("workspaces");
        if (!wss) continue;
        km.focused = wss->focused();
        if (const kjson::Arr* welems = wss->elems()) {
            for (const kjson::Val& w : *welems) {
                KomoWs kw;
                kw.name = w.str("name");
                auto addWindow = [&](const kjson::Val& win) {
                    std::string exe = win.str("exe");
                    if (exe.empty()) return;
                    kw.exes.push_back(U82W(exe));
                    kw.hwnds.push_back((HWND)(intptr_t)win.num("hwnd"));
                };
                if (const kjson::Val* cs = w.find("containers"))
                    if (const kjson::Arr* celems = cs->elems())
                        for (const kjson::Val& c : *celems)
                            if (const kjson::Val* wins = c.find("windows"))
                                if (const kjson::Arr* welems2 = wins->elems())
                                    for (const kjson::Val& win : *welems2) addWindow(win);
                // a monocle'd or maximised window is not in `containers`, and floating windows are
                // their own list - all three still live on the workspace
                if (const kjson::Val* mc = w.find("monocle_container"))
                    if (const kjson::Val* wins = mc->find("windows"))
                        if (const kjson::Arr* welems2 = wins->elems())
                            for (const kjson::Val& win : *welems2) addWindow(win);
                if (const kjson::Val* mw = w.find("maximized_window"))
                    if (mw->t == kjson::Val::OBJ) addWindow(*mw);
                if (const kjson::Val* fw = w.find("floating_windows"))
                    if (const kjson::Arr* felems = fw->elems())
                        for (const kjson::Val& win : *felems) addWindow(win);
                km.ws.push_back(std::move(kw));
            }
        }
        if (!km.ws.empty()) out.push_back(std::move(km));
    }
    if (out.empty()) return false;

    // Remember what each monitor was focused on so a real event can be told from a heartbeat, and
    // so the slide knows which way to travel.
    //
    // Keyed by komorebi's monitor id, NOT by its position in the array. Measured on this two-screen machine:
    // switch both monitors in the same instant and komorebi's next document has them in the other order, so the
    // index-keyed table reported "monitor 1 went 0 -> 1" AND "monitor 1 went 1 -> 0" while monitor 0's real
    // switch vanished - the slide ran twice on one screen and never on the other. A laptop with a second display
    // does this all day, because docking and undocking reorder them too.
    static std::unordered_map<long long,int> prevFocus;
    struct Fire { int mon, oldWs, newWs; RECT rect; };
    std::vector<Fire> fires;
    for (int i = 0; i < (int)out.size() && i < 16; i++) {
        long long key = out[i].id ? out[i].id : (long long)(-1 - i);   // no id (older komorebi): fall back to the slot
        auto it = prevFocus.find(key);
        if (it != prevFocus.end() && it->second != out[i].focused)
            fires.push_back({ i, it->second, out[i].focused, out[i].rect });
        prevFocus[key] = out[i].focused;
    }

    KomoIndexBuild(out);            // built from `out` so it never nests inside g_komoMtx
    { std::lock_guard<std::mutex> lk(g_komoMtx);
      g_komo = std::move(out);
      g_komoFocusedMon = std::clamp(mons->focused(), 0, (int)g_komo.size() - 1); }

    // Only a pushed event animates. The state heartbeat sees the same change seconds later and
    // would replay a switch that already finished.
    if (fromEvent) {
        // Hide/reveal FIRST and synchronously: the whole point is that the outgoing workspace is
        // gone on the very next frame rather than melting away over a second or more.
        { std::lock_guard<std::mutex> lk(g_komoMtx);
          for (const Fire& f : fires)
              if (f.mon < (int)g_komo.size()) KomoFastSwap(g_komo[f.mon], f.oldWs, f.newWs); }
        for (const Fire& f : fires) WsSlideFire(f.mon, f.oldWs, f.newWs, f.rect);
    }
    g_komoStamp.store(GetTickCount64());
    if(!g_komoLive.exchange(true)) g_komoSession.fetch_add(1);   // down -> up: a new komorebi run
    return true;
}

// A notification is { "event": ..., "state": ... }; a bare State document is accepted too, so the
// event stream and `komorebic state` share one path.
static bool KomoIngestDoc(const std::string& json, bool fromEvent) {
    kjson::Val v;
    if (!kjson::parse(json, v) || v.t != kjson::Val::OBJ) return false;
    if (const kjson::Val* st = v.find("state")) return KomoIngestVal(*st, fromEvent);
    return KomoIngestVal(v, fromEvent);
}

// Pull the whole state once (used on start and on every reconnect).
static bool KomoPollState() {
    std::string out;
    if (!KomoRun(L"state", &out)) return false;
    size_t b = out.find('{');
    if (b == std::string::npos) return false;
    return KomoIngestDoc(out.substr(b), false);
}

// g_komoLog is declared up in main.cpp - the config loader needs it long before this header.
static void KomoLog(const char* fmt, ...) {
    if (!g_komoLog) return;
    static std::mutex m; std::lock_guard<std::mutex> lk(m);
    FILE* f = nullptr;
    // relative would follow the launch cwd, which is not the install folder when the shell is
    // started by anything other than WinLogon
    static const std::string logPath = ExeDir() + "komorebi.log";
    if (fopen_s(&f, logPath.c_str(), "a") || !f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

// Split a byte stream into balanced top-level JSON objects. komorebi does not newline-delimit its
// notifications, and window titles are full of braces, so string state has to be tracked.
static void KomoDrain(std::string& acc) {
    for (;;) {
        size_t start = acc.find('{');
        if (start == std::string::npos) { acc.clear(); return; }
        int depth = 0; bool inStr = false, esc = false; size_t end = std::string::npos;
        for (size_t i = start; i < acc.size(); i++) {
            char c = acc[i];
            if (esc) { esc = false; continue; }
            if (inStr) { if (c == '\\') esc = true; else if (c == '"') inStr = false; continue; }
            if (c == '"') { inStr = true; continue; }
            if (c == '{') depth++;
            else if (c == '}') { if (--depth == 0) { end = i; break; } }
        }
        if (end == std::string::npos) {
            if (start > 0) acc.erase(0, start);
            if (acc.size() > (4u << 20)) acc.clear();     // never grow without bound
            return;
        }
        std::string doc = acc.substr(start, end - start + 1);
        acc.erase(0, end + 1);
        if (KomoIngestDoc(doc, true)) KomoLog("event ingested (%zu bytes)", doc.size());
    }
}

// The reader. The pipe gives instant updates, but liveness is decided by `komorebic state`, not by
// the pipe: the first version treated a dropped pipe as "komorebi is gone", so a single reconnect
// silently put the bar back on virtual desktops - where a one-desktop machine makes every workspace
// click a no-op. That is exactly the "workspaces don't switch" symptom. The pipe is now read
// non-blocking so the same thread can keep a slow state poll going as the source of truth.
static void KomoPipeThread() {
    const wchar_t* PIPE = L"\\\\.\\pipe\\aether-komorebi";
    std::string acc;
    int  pollFails = 0;
    ULONGLONG lastPoll = 0, lastSub = 0;
    HANDLE h = INVALID_HANDLE_VALUE;
    bool connected = false;

    while (g_komoRun.load()) {
        ULONGLONG now = GetTickCount64();

        // ---- authoritative heartbeat -------------------------------------------------------
        if (now - lastPoll > (connected ? 5000ULL : 2000ULL)) {
            lastPoll = now;
            if (KomoPollState()) { pollFails = 0; }
            else if (++pollFails >= 2) {
                if (g_komoLive.exchange(false)) KomoLog("komorebi went away (state failed x%d)", pollFails);
            }
        }

        // ---- (re)establish the event pipe ---------------------------------------------------
        if (h == INVALID_HANDLE_VALUE) {
            h = CreateNamedPipeW(PIPE, PIPE_ACCESS_INBOUND,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
                                 1, 0, 1 << 20, 0, nullptr);
            connected = false; acc.clear();
            if (h == INVALID_HANDLE_VALUE) { KomoLog("CreateNamedPipe failed %lu", GetLastError()); Sleep(2000); continue; }
            lastSub = 0;
        }
        if (!connected) {
            // ask komorebi to connect, at most once every few seconds
            if (now - lastSub > 4000ULL) {
                lastSub = now;
                bool ok = KomoRun(L"subscribe-pipe aether-komorebi", nullptr);
                KomoLog("subscribe-pipe -> %s", ok ? "ok" : "FAILED");
            }
            if (ConnectNamedPipe(h, nullptr)) connected = true;
            else {
                DWORD e = GetLastError();
                if (e == ERROR_PIPE_CONNECTED) connected = true;
                else if (e != ERROR_PIPE_LISTENING && e != ERROR_NO_DATA) {
                    KomoLog("ConnectNamedPipe error %lu, recycling", e);
                    CloseHandle(h); h = INVALID_HANDLE_VALUE; Sleep(1000); continue;
                }
            }
            if (connected) KomoLog("pipe connected");
        }

        // ---- drain whatever komorebi has pushed ---------------------------------------------
        if (connected) {
            char buf[16384]; DWORD got = 0; bool broke = false;
            for (;;) {
                if (!ReadFile(h, buf, sizeof(buf), &got, nullptr)) {
                    DWORD e = GetLastError();
                    if (e == ERROR_NO_DATA) break;                 // nothing pending, fine
                    KomoLog("ReadFile error %lu, recycling pipe", e);
                    broke = true; break;
                }
                if (!got) break;
                acc.append(buf, got);
                if (got < sizeof(buf)) break;
            }
            if (!acc.empty()) KomoDrain(acc);
            if (broke) { DisconnectNamedPipe(h); CloseHandle(h); h = INVALID_HANDLE_VALUE; connected = false; }
        }
        Sleep(60);
    }
    if (h != INVALID_HANDLE_VALUE) { DisconnectNamedPipe(h); CloseHandle(h); }
}

// KomorebiStart/Stop only ever started and stopped OUR reader. Nothing in the shell could launch
// komorebi itself, so "use komorebi workspaces" silently did nothing whenever komorebi was not
// already running - which is the state a fresh machine is in, and the state this one was in.
static void KomorebiStart() {
    if (g_komoRun.exchange(true)) return;
    std::thread(KomoPipeThread).detach();
}
static void KomorebiStop() {
    if (!g_komoRun.exchange(false)) return;
    KomoUncloakAll();          // never leave a window hidden with no way to get it back
    KomoRun(L"unsubscribe-pipe aether-komorebi", nullptr);
}

// ---- is komorebic even on this machine? ---------------------------------------------------------
// Cached: this shells out, and the settings page asks every frame it is open. Re-checked on demand
// (KomoForgetInstalled) after anything that might have installed it.
static std::atomic<int> g_komoInstalled{-1};        // -1 unknown, 0 no, 1 yes
static bool KomoInstalled() {
    int v = g_komoInstalled.load();
    if (v >= 0) return v == 1;
    std::string out;
    bool ok = KomoRun(L"--version", &out);
    g_komoInstalled.store(ok ? 1 : 0);
    return ok;
}
static void KomoForgetInstalled(){ g_komoInstalled.store(-1); }

// ---- start / stop the window manager itself ------------------------------------------------------
// `komorebic start` returns as soon as it has spawned komorebi.exe, but the socket is not up yet, so
// the poll thread is what actually decides we are live - do not report success from here.
static void KomorebiLaunch(bool focusFollowsMouse, bool keybinds) {
    std::wstring args = L"start";
    // whkd is what runs the user's keybindings; without it komorebi has no shortcuts at all, so a
    // "start" that leaves it out gives you a tiling WM you cannot drive.
    if (keybinds)          args += L" --whkd";
    if (focusFollowsMouse) args += L" --masir";   // komorebi's focus-follows-mouse helper
    std::thread([args]{ KomoRun(args, nullptr); }).detach();
}
static void KomorebiQuit() {
    // restores every window komorebi hid; without this a stop leaves cloaked windows invisible
    std::thread([]{ KomoRun(L"stop", nullptr); }).detach();
}

// ---- niri mode -----------------------------------------------------------------------------------
// komorebi 0.1.41 ships a `scrolling` layout, which IS niri's model: one row of columns, a viewport
// onto it, focus scrolls the row. So this does not emulate anything - it switches komorebi into the
// layout it already has, on every workspace of every monitor.
//
// The column count is a focused-workspace command with no targeted form, so it cannot be pushed to
// all workspaces in one go; KomoApplyScrollCols is re-sent whenever a workspace takes focus instead.
static void KomoSetLayoutAll(const wchar_t* layout) {
    std::wstring lay = layout;
    std::thread([lay]{
        std::vector<KomoMon> mons;
        { std::lock_guard<std::mutex> lk(g_komoMtx); mons = g_komo; }
        for (size_t m = 0; m < mons.size(); m++)
            for (size_t w = 0; w < mons[m].ws.size(); w++) {
                wchar_t a[96];
                _snwprintf_s(a, 96, L"workspace-layout %d %d %s", (int)m, (int)w, lay.c_str());
                KomoRun(a, nullptr);
            }
    }).detach();
}
static void KomoApplyScrollCols(int cols) {
    wchar_t a[64];
    _snwprintf_s(a, 64, L"scrolling-layout-columns %d", std::max(1, std::min(cols, 8)));
    std::thread([s2 = std::wstring(a)]{ KomoRun(s2, nullptr); }).detach();
}
// niri's core navigation: step along the row rather than between workspaces
static void KomoFocusDir(const wchar_t* dir) {
    std::thread([d = std::wstring(dir)]{ KomoRun(L"focus " + d, nullptr); }).detach();
}

// Focus a workspace. komorebi's own index space, so no translation is needed beyond the monitor.
static void KomorebiFocus(int monIdx, int wsIdx) {
    wchar_t a[64];
    _snwprintf_s(a, 64, L"focus-monitor-workspace %d %d", std::max(0, monIdx), std::max(0, wsIdx));
    // a bar click: pause komorebi's animations BEFORE it moves anything, then switch
    std::thread([s = std::wstring(a)]{
        if (g_wsSlide.load()) { KomoAnimPauseNow(); }
        KomoRun(s, nullptr);
        if (g_wsSlide.load()) KomoAnimResumeSoon(g_wsSlideMs.load() + 600);   // safety net if no slide fires
    }).detach();
}
