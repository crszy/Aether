// Caelestia.exe: the one program you run. Sits in the package root next to qs\, caelestia\, config\, cli\.
//   Caelestia.exe              start the shell (or, if it is already running, open Settings)
//   Caelestia.exe --settings [page]   open Settings, optionally on a page ("windows", "all settings", "panels"...)
//   Caelestia.exe --stop       stop the shell (its bars give the screen space back)
//   Caelestia.exe --restart    stop, then start
//   Caelestia.exe --sync-config    merge the package's config into ~/.config/quickshell/aether (your edits kept)
//   Caelestia.exe --reset-config [aether|ryoku]   move your copy aside and start from a fresh one
//   Caelestia.exe --config <name>  run another Quickshell config, ~/.config/quickshell/<name>/shell.qml (like qs -c);
//                                  remembered as windows.json "shellConfig"; --config aether goes back
// It sets up the environment Quickshell needs (DLL path, QML import paths, Qt plugins) itself, so nothing
// else has to be on PATH, and it never opens a console window.
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <map>
#include "aetherconfig.hpp"

static std::wstring g_pkg;   // package root (folder of this exe)

static std::wstring qsDir() { return g_pkg + L"\\qs"; }
// the Quickshell config that runs: ~/.config/quickshell/<windows.json shellConfig>/shell.qml, else the package's
static std::wstring cfgPath() { return aeShellQml(g_pkg); }

// ---- your own copy of Aether's config ------------------------------------------------------------------------
// Like Caelestia on Linux (a git clone in ~/.config/quickshell/caelestia that you edit), Aether's Quickshell config
// lives in ~/.config/quickshell/aether and is yours: edit any file, Quickshell reloads it live.
// First start: <package>\config is copied there. After an update (the package's config changed) the new files are
// merged in without losing your edits - <dir>\.aether\base.tsv remembers what Aether delivered last time:
//   file you never touched          -> replaced by the new version
//   file you edited, Aether didn't  -> kept
//   both changed it                 -> kept; the new version is put next to it as <file>.aether-new
//   file you deleted                -> stays deleted;   files you added -> never touched
// The result of the last update: <dir>\.aether\last-update.txt. A folder that has no .aether\ is not touched.
// Caelestia.exe --reset-config moves your copy aside (aether.backup-<time>) and makes a fresh one.

struct AeFile { unsigned long long size = 0, hash = 0; bool ok = false; };

static AeFile aeHashFile(const std::wstring& path) {
    AeFile r;
    HANDLE f = CreateFileW(aeLong(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return r;
    unsigned long long h = 1469598103934665603ULL;   // FNV-1a 64
    static char buf[1 << 16]; DWORD got = 0;
    while (ReadFile(f, buf, sizeof buf, &got, nullptr) && got) {
        for (DWORD i = 0; i < got; i++) { h ^= (unsigned char)buf[i]; h *= 1099511628211ULL; }
        r.size += got;
    }
    CloseHandle(f);
    r.hash = h; r.ok = true;
    return r;
}

// every file under root (relative paths), plus a stamp of all names/sizes/times (cheap "did anything change")
static void aeWalk(const std::wstring& root, const std::wstring& rel, std::vector<std::wstring>& out, unsigned long long& stamp) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(aeLong(root + (rel.empty() ? L"" : L"\\" + rel) + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        std::wstring r = rel.empty() ? n : rel + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { aeWalk(root, r, out, stamp); continue; }
        out.push_back(r);
        for (wchar_t c : r) { stamp ^= (unsigned)c; stamp *= 1099511628211ULL; }
        stamp ^= ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow; stamp *= 1099511628211ULL;
        stamp ^= ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime; stamp *= 1099511628211ULL;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static std::string aeUtf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
static std::wstring aeWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
static std::string aeReadAll(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{}; GetFileSizeEx(f, &sz);
    std::string s((size_t)sz.QuadPart, '\0'); DWORD got = 0;
    if (!s.empty()) ReadFile(f, s.data(), (DWORD)s.size(), &got, nullptr);
    CloseHandle(f); s.resize(got);
    return s;
}
static bool aeWriteAll(const std::wstring& path, const std::string& s) {
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0; bool ok = WriteFile(f, s.data(), (DWORD)s.size(), &put, nullptr) && put == s.size();
    CloseHandle(f);
    return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
// make a folder and its parents (works past 260 characters, unlike SHCreateDirectoryExW)
static void aeMkdirs(const std::wstring& dir) {
    const std::wstring l = aeLong(dir);
    if (GetFileAttributesW(l.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash > 2) aeMkdirs(dir.substr(0, slash));
    CreateDirectoryW(l.c_str(), nullptr);
}
static bool aeCopy(const std::wstring& from, const std::wstring& to) {
    size_t slash = to.find_last_of(L"\\/");
    if (slash != std::wstring::npos) aeMkdirs(to.substr(0, slash));
    SetFileAttributesW(aeLong(to).c_str(), FILE_ATTRIBUTE_NORMAL);
    return CopyFileW(aeLong(from).c_str(), aeLong(to).c_str(), FALSE) != 0;
}

// merge a package folder (src) into ~/.config/quickshell/<name> (force: even when the package looks unchanged)
static void syncOne(const std::wstring& src, const std::wstring& name, bool force) {
    const std::wstring dst = aeUserConfigDir(name), meta = dst + L"\\.aether";
    if (!aeExists(src + L"\\shell.qml")) return;
    if (aeExists(dst) && !aeExists(meta)) return;    // a folder you made yourself: not ours to change
    SHCreateDirectoryExW(nullptr, meta.c_str(), nullptr);

    std::vector<std::wstring> files; unsigned long long stamp = 1469598103934665603ULL;
    aeWalk(src, L"", files, stamp);
    wchar_t hex[32]; swprintf(hex, 32, L"%016llx", stamp);
    const std::string stampText = aeUtf8(hex);
    if (!force && aeReadAll(meta + L"\\stamp") == stampText && aeExists(dst + L"\\shell.qml")) return;

    std::map<std::wstring, AeFile> base;             // what Aether delivered last time
    {
        std::string t = aeReadAll(meta + L"\\base.tsv");
        size_t p = 0;
        while (p < t.size()) {
            size_t e = t.find('\n', p); if (e == std::string::npos) e = t.size();
            std::string line = t.substr(p, e - p); p = e + 1;
            size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
            if (b == std::string::npos) continue;
            AeFile f; f.ok = true;
            f.hash = strtoull(line.substr(0, a).c_str(), nullptr, 16);
            f.size = strtoull(line.substr(a + 1, b - a - 1).c_str(), nullptr, 10);
            base[aeWide(line.substr(b + 1))] = f;
        }
    }

    // shell.qml last: a half-finished first copy must not look like a working config
    for (size_t i = 0; i < files.size(); i++)
        if (_wcsicmp(files[i].c_str(), L"shell.qml") == 0) { std::swap(files[i], files.back()); break; }

    std::map<std::wstring, AeFile> next;
    std::vector<std::wstring> conflicts, kept, failed;
    int added = 0, updated = 0, removed = 0;
    auto same = [](const AeFile& a, const AeFile& b) { return a.ok && b.ok && a.size == b.size && a.hash == b.hash; };
    for (const auto& rel : files) {
        AeFile nw = aeHashFile(src + L"\\" + rel);
        if (!nw.ok) continue;
        next[rel] = nw;
        const std::wstring mine = dst + L"\\" + rel;
        auto it = base.find(rel);
        const bool known = it != base.end();
        if (!aeExists(mine)) {
            if (known) continue;                     // you deleted it: it stays deleted
            if (aeCopy(src + L"\\" + rel, mine)) added++; else failed.push_back(rel);
            continue;
        }
        AeFile cur = aeHashFile(mine);
        if (same(cur, nw)) continue;
        if (known && same(cur, it->second)) {        // untouched by you: take the new one
            if (aeCopy(src + L"\\" + rel, mine)) updated++; else failed.push_back(rel);
        } else if (known && same(nw, it->second)) {  // only you changed it
            kept.push_back(rel);
        } else {                                     // both changed (or yours was there before we started)
            if (aeCopy(src + L"\\" + rel, mine + L".aether-new")) conflicts.push_back(rel); else failed.push_back(rel);
        }
    }
    for (const auto& [rel, was] : base) {            // gone from Aether: removed if you never touched it
        if (next.count(rel)) continue;
        const std::wstring mine = dst + L"\\" + rel;
        if (aeExists(mine) && same(aeHashFile(mine), was) && DeleteFileW(aeLong(mine).c_str())) removed++;
    }

    std::string tsv;
    for (const auto& [rel, f] : next) {
        char line[64]; snprintf(line, sizeof line, "%016llx\t%llu\t", f.hash, f.size);
        tsv += line + aeUtf8(rel) + "\n";
    }
    aeWriteAll(meta + L"\\base.tsv", tsv);
    if (failed.empty()) aeWriteAll(meta + L"\\stamp", stampText);   // else try again next start

    SYSTEMTIME st; GetLocalTime(&st);
    char head[256];
    snprintf(head, sizeof head, "Config update %04d-%02d-%02d %02d:%02d\nadded %d, updated %d, removed %d, your edits kept %d, both changed %d, failed %d\n",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, added, updated, removed, (int)kept.size(), (int)conflicts.size(), (int)failed.size());
    std::string rep = head;
    if (!conflicts.empty()) {
        rep += "\nBoth you and the update changed these. Yours is unchanged; the new version is <file>.aether-new next to it.\n"
               "Merge what you want from it into yours, then delete the .aether-new file:\n";
        for (const auto& r : conflicts) rep += "  " + aeUtf8(r) + "\n";
    }
    if (!kept.empty()) { rep += "\nYour edits (the update did not change these):\n"; for (const auto& r : kept) rep += "  " + aeUtf8(r) + "\n"; }
    if (!failed.empty()) { rep += "\nCould not copy (tried again next start):\n"; for (const auto& r : failed) rep += "  " + aeUtf8(r) + "\n"; }
    aeWriteAll(meta + L"\\last-update.txt", rep);
}

// the configs Aether ships: Aether itself (Caelestia + the ported DMS / Noctalia / end-4 / Zenith inside it) and
// Ryoku (its own Quickshell instance, started by Aether's RyokuHost)
static void syncConfig(bool force) {
    syncOne(g_pkg + L"\\config", L"aether", force);
    syncOne(g_pkg + L"\\ryoku\\shell", L"ryoku", force);
}

// windows.json "shellConfig" = name (the shell must not be running: it rewrites that file)
static bool setConfigName(const std::wstring& name) {
    const std::wstring dir = aeEnv(L"USERPROFILE") + L"\\.config\\caelestia", file = dir + L"\\windows.json";
    std::string s = aeReadAll(file), v = "\"" + aeUtf8(name) + "\"";
    size_t k = s.find("\"shellConfig\"");
    if (k != std::string::npos) {
        size_t q1 = s.find('"', s.find(':', k + 13));
        size_t q2 = q1 == std::string::npos ? q1 : s.find('"', q1 + 1);
        if (q2 == std::string::npos) return false;
        s.replace(q1, q2 - q1 + 1, v);
    } else {
        size_t b = s.find('{');
        if (b == std::string::npos) s = "{\n    \"shellConfig\": " + v + "\n}\n";
        else {
            size_t nx = s.find_first_not_of(" \t\r\n", b + 1);
            s.insert(b + 1, "\n    \"shellConfig\": " + v + (nx != std::string::npos && s[nx] == '}' ? "" : ","));
        }
    }
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return aeWriteAll(file, s);
}

// move ~/.config/quickshell/<name> aside and make a fresh copy
static bool resetConfig(const std::wstring& name) {
    const std::wstring dst = aeUserConfigDir(name);
    if (aeExists(dst)) {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t suffix[64]; swprintf(suffix, 64, L".backup-%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        if (!MoveFileExW(dst.c_str(), (dst + suffix).c_str(), 0)) return false;
    }
    syncConfig(true);
    return true;
}

static void setupEnv() {
    const std::wstring q = qsDir();
    wchar_t old[32767]; DWORD n = GetEnvironmentVariableW(L"PATH", old, 32767);
    std::wstring path = q + L";" + g_pkg + L"\\caelestia\\lib;" + (n ? std::wstring(old, n) : L"");
    SetEnvironmentVariableW(L"PATH", path.c_str());
    SetEnvironmentVariableW(L"QML_IMPORT_PATH", (q + L"\\qml_modules;" + q + L"\\shims;" + g_pkg + L"\\caelestia\\qml;" + q + L"\\qml").c_str());
    SetEnvironmentVariableW(L"QT_PLUGIN_PATH", (q + L"\\plugins").c_str());
    SetEnvironmentVariableW(L"CAELESTIA_PACKAGE_DIR", g_pkg.c_str());
    // Hyprland IPC: where the Hyprland window manager (komorebi --hyprland, started by the shell, so it inherits
    // these) serves Hyprland's sockets, and where the shells' Hyprland code (Caelestia, Quickshell) looks for them:
    // $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE. Forward slashes: they become a named pipe's name.
    if (!GetEnvironmentVariableW(L"HYPRLAND_INSTANCE_SIGNATURE", nullptr, 0))
        SetEnvironmentVariableW(L"HYPRLAND_INSTANCE_SIGNATURE", L"aether");
    if (!GetEnvironmentVariableW(L"XDG_RUNTIME_DIR", nullptr, 0)) {
        wchar_t tmp[MAX_PATH]; DWORD n = GetTempPathW(MAX_PATH, tmp);
        std::wstring runtime(tmp, n);
        while (!runtime.empty() && (runtime.back() == L'\\' || runtime.back() == L'/')) runtime.pop_back();
        for (auto& c : runtime) if (c == L'\\') c = L'/';
        SetEnvironmentVariableW(L"XDG_RUNTIME_DIR", runtime.c_str());
    }
    // the folder must exist: clients check it before connecting to the pipes
    {
        wchar_t rt[MAX_PATH], sig[256];
        GetEnvironmentVariableW(L"XDG_RUNTIME_DIR", rt, MAX_PATH);
        GetEnvironmentVariableW(L"HYPRLAND_INSTANCE_SIGNATURE", sig, 256);
        std::wstring dir = std::wstring(rt) + L"/hypr";
        CreateDirectoryW(dir.c_str(), nullptr);
        CreateDirectoryW((dir + L"/" + sig).c_str(), nullptr);
    }
}

// is a qs.exe / qsw.exe from THIS package running?
static bool running() {
    bool found = false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof pe;
    const std::wstring prefix = qsDir() + L"\\";
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"qsw.exe") && _wcsicmp(pe.szExeFile, L"qs.exe")) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t img[MAX_PATH]; DWORD len = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, img, &len) && _wcsnicmp(img, prefix.c_str(), prefix.size()) == 0) found = true;
        CloseHandle(h);
    }
    CloseHandle(snap);
    return found;
}

// run qs.exe <args> hidden and wait (ipc / kill)
// a line in %LOCALAPPDATA%\AetherShell\launcher.log (switching configs runs with no window to show errors in)
static void aeLog(const std::wstring& m) {
    const std::wstring dir = aeEnv(L"LOCALAPPDATA") + L"\\AetherShell";
    CreateDirectoryW(dir.c_str(), nullptr);
    HANDLE f = CreateFileW((dir + L"\\launcher.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t ts[32]; swprintf(ts, 32, L"%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::wstring line = ts + m + L"\r\n";
    int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), nullptr, 0, nullptr, nullptr);
    std::string u(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), u.data(), n, nullptr, nullptr);
    DWORD w = 0; WriteFile(f, u.data(), (DWORD)u.size(), &w, nullptr); CloseHandle(f);
}
static DWORD runQs(const std::wstring& args, DWORD waitMs) {
    std::wstring exe = qsDir() + L"\\qs.exe";
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, g_pkg.c_str(), &si, &pi)) return 127;
    WaitForSingleObject(pi.hProcess, waitMs);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code;
}

// ---- running as administrator or not ------------------------------------------------------------------------
// windows.json "runAsAdmin": true  -> the shell runs as administrator (asks UAC once), so it can tile and move
//                                     administrator windows too; apps it starts still run as normal apps.
// false (default)                  -> the shell always runs as a normal app, even when Caelestia.exe was started
//                                     from an administrator terminal.
static bool selfElevated() {
    HANDLE t = nullptr; TOKEN_ELEVATION e{}; DWORD n = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) return false;
    bool ok = GetTokenInformation(t, TokenElevation, &e, sizeof e, &n) && e.TokenIsElevated;
    CloseHandle(t);
    return ok;
}

static bool wantAdmin() {
    wchar_t home[MAX_PATH]; if (!GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH)) return false;
    HANDLE f = CreateFileW((std::wstring(home) + L"\\.config\\caelestia\\windows.json").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    char buf[65536]; DWORD got = 0; ReadFile(f, buf, sizeof buf - 1, &got, nullptr); CloseHandle(f);
    buf[got] = 0;
    std::string s(buf);
    size_t k = s.find("\"runAsAdmin\"");
    if (k == std::string::npos) return false;
    size_t v = s.find_first_not_of(" \t\r\n:", k + 12);
    return v != std::string::npos && s.compare(v, 4, "true") == 0;
}

// start a process with explorer's (normal) token - used when we are elevated but the shell should not be
static bool startUnelevated(const std::wstring& cmdIn) {
    // explorer's token, or (no explorer - Caelestia is the shell) sihost.exe's: both run as the normal user
    HWND shell = GetShellWindow(); DWORD pid = 0;
    if (shell) GetWindowThreadProcessId(shell, &pid);
    if (!pid) {
        DWORD session = 0; ProcessIdToSessionId(GetCurrentProcessId(), &session);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof pe;
        for (BOOL ok = Process32FirstW(snap, &pe); ok && !pid; ok = Process32NextW(snap, &pe)) {
            DWORD s = 0;
            if (_wcsicmp(pe.szExeFile, L"sihost.exe") == 0 && ProcessIdToSessionId(pe.th32ProcessID, &s) && s == session) pid = pe.th32ProcessID;
        }
        CloseHandle(snap);
    }
    if (!pid) return false;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!p) return false;
    HANDLE t = nullptr, dup = nullptr;
    bool ok = OpenProcessToken(p, TOKEN_DUPLICATE, &t)
           && DuplicateTokenEx(t, TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                               nullptr, SecurityImpersonation, TokenPrimary, &dup);
    if (t) CloseHandle(t); CloseHandle(p);
    if (!ok) return false;
    // our environment (PATH, QML_IMPORT_PATH ... from setupEnv) goes along
    LPWCH env = GetEnvironmentStringsW();
    std::wstring cmd = cmdIn;
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    ok = CreateProcessWithTokenW(dup, 0, nullptr, cmd.data(), CREATE_UNICODE_ENVIRONMENT, env, g_pkg.c_str(), &si, &pi);
    FreeEnvironmentStringsW(env); CloseHandle(dup);
    if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    return ok;
}

// ---- the watcher: Caelestia.exe --watch stays with the running shell. Windows Error Reporting is off on this PC, so a
// shell that dies hard (heap corruption, fail-fast) left no trace and the desktop just went empty (2026-11-21). The
// watcher writes how it ended to launcher.log and starts it again - unless Aether stopped it on purpose (stop() leaves
// a "stopping" mark), or it keeps dying (3 restarts in 10 minutes, then it gives up and says so).
static std::wstring stoppingMark() { return aeEnv(L"LOCALAPPDATA") + L"\\AetherShell\\stopping"; }
static void markStopping() {
    CreateDirectoryW((aeEnv(L"LOCALAPPDATA") + L"\\AetherShell").c_str(), nullptr);
    HANDLE f = CreateFileW(stoppingMark().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
}
static bool stoppedOnPurpose() {   // a mark from the last 60 s
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(stoppingMark().c_str(), GetFileExInfoStandard, &d)) return false;
    FILETIME now; GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a{}, b{}; a.LowPart = d.ftLastWriteTime.dwLowDateTime; a.HighPart = d.ftLastWriteTime.dwHighDateTime;
    b.LowPart = now.dwLowDateTime; b.HighPart = now.dwHighDateTime;
    return b.QuadPart - a.QuadPart < 60ULL * 10000000ULL;
}
static HANDLE openShell(DWORD* pidOut) {   // this package's running qsw.exe / qs.exe (the shell)
    HANDLE found = nullptr;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return nullptr;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof pe;
    const std::wstring prefix = qsDir() + L"\\";
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"qsw.exe")) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t img[MAX_PATH]; DWORD len = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, img, &len) && _wcsnicmp(img, prefix.c_str(), prefix.size()) == 0) { found = h; if (pidOut) *pidOut = pe.th32ProcessID; }
        else CloseHandle(h);
    }
    CloseHandle(snap);
    return found;
}
static void spawnWatcher() {
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" --watch";
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    // out of the caller's job (the shell binds what it starts to its own lifetime): the watcher must outlive it
    if (!CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB, nullptr, g_pkg.c_str(), &si, &pi)
        && !CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, g_pkg.c_str(), &si, &pi)) return;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}
static bool start();
static int watchShell() {
    // one watcher: a newer one (after a switch) waits for the old one, which leaves when its shell is gone
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\AetherShellWatch");
    if (!mutex || WaitForSingleObject(mutex, 20000) == WAIT_TIMEOUT) return 0;
    ULONGLONG restarts[3] = {0, 0, 0}; int next = 0;
    for (;;) {
        DWORD pid = 0; HANDLE h = nullptr;
        for (int i = 0; i < 100 && !(h = openShell(&pid)); i++) Sleep(200);   // up to 20 s for it to show up
        if (!h) break;
        WaitForSingleObject(h, INFINITE);
        DWORD code = 0; GetExitCodeProcess(h, &code); CloseHandle(h);
        if (stoppedOnPurpose()) break;
        Sleep(3000);
        if (stoppedOnPurpose()) break;
        if (running()) continue;   // its crash handler (or a switch) started a new one: watch that
        wchar_t hex[16]; swprintf(hex, 16, L"0x%08X", code);
        const ULONGLONG now = GetTickCount64();
        if (restarts[next] && now - restarts[next] < 600000) {
            aeLog(L"shell (pid " + std::to_wstring(pid) + L") ended, exit " + hex + L" - it keeps dying: not starting it again (run Caelestia.exe)");
            break;
        }
        aeLog(L"shell (pid " + std::to_wstring(pid) + L") ended by itself, exit " + hex + L" - starting it again");
        restarts[next] = now; next = (next + 1) % 3;
        if (!start()) break;
    }
    ReleaseMutex(mutex); CloseHandle(mutex);
    return 0;
}
static bool start() {
    syncConfig(false);
    std::wstring exe = qsDir() + L"\\qsw.exe";
    std::wstring cmd = L"\"" + exe + L"\" -p \"" + cfgPath() + L"\"";
    const bool admin = wantAdmin(), elevated = selfElevated();
    if (admin && !elevated) {
        // ask UAC once; the elevated Caelestia.exe then starts the shell
        wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
        SHELLEXECUTEINFOW sei{}; sei.cbSize = sizeof sei; sei.lpVerb = L"runas"; sei.lpFile = self;
        sei.lpParameters = L"--start-elevated"; sei.nShow = SW_HIDE; sei.fMask = SEE_MASK_NOASYNC;
        if (ShellExecuteExW(&sei)) return true;   // the elevated half starts the watcher
        // UAC was declined: run as a normal app instead
    }
    if (!admin && elevated && startUnelevated(cmd)) { spawnWatcher(); return true; }
    STARTUPINFOW si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, g_pkg.c_str(), &si, &pi)) {
        MessageBoxW(nullptr, (L"Could not start " + exe).c_str(), L"Caelestia", MB_ICONERROR);
        return false;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    spawnWatcher();
    return true;
}

static void stop() {
    markStopping();   // the watcher leaves it stopped
    runQs(L"kill -p \"" + cfgPath() + L"\"", 5000);
    for (int i = 0; i < 30 && running(); i++) Sleep(100);
    if (!running()) return;
    aeLog(L"stop: the shell did not quit when asked (qs kill) - ending it");
    // last resort: end this package's qs processes only
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof pe;
    const std::wstring prefix = qsDir() + L"\\";
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"qsw.exe") && _wcsicmp(pe.szExeFile, L"qs.exe")) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t img[MAX_PATH]; DWORD len = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, img, &len) && _wcsnicmp(img, prefix.c_str(), prefix.size()) == 0) TerminateProcess(h, 0);
        CloseHandle(h);
    }
    CloseHandle(snap);
}

// switch to another Quickshell config (~/.config/quickshell/<name>) and run it, like `qs -c <name>` on Linux
static bool switchConfig(const std::wstring& name) {
    if (name != L"aether" && !aeExists(aeUserConfigDir(name) + L"\\shell.qml")) {
        MessageBoxW(nullptr, (L"There is no Quickshell config \"" + name + L"\".\nA config is a folder with a shell.qml in it:\n" +
                    aeUserConfigDir(name) + L"\\shell.qml").c_str(), L"Aether", MB_ICONWARNING);
        return false;
    }
    // AetherWM (tiling) keeps running whatever shell runs, like Hyprland on Linux: only the shell is swapped
    aeLog(L"switch to " + name + L": stopping " + cfgPath());
    stop();                                  // with the old name: kill -p needs the running config's path
    for (int i = 0; i < 30 && running(); i++) Sleep(100);
    if (running()) aeLog(L"  the old shell is still running");
    if (!setConfigName(name)) { aeLog(L"  could not write shellConfig to windows.json"); return false; }
    const bool ok = start();
    aeLog(ok ? L"  started " + cfgPath() : L"  start failed");
    return ok;
}

static void openSettings(const std::wstring& page = L"") {
    AllowSetForegroundWindow(ASFW_ANY);   // you just ran us, so the window the shell opens may come to the front
    const std::wstring call = page.empty() ? L"call nexus open" : L"call nexus openPage \"" + page + L"\"";
    // another Quickshell config may not have Aether's Settings: one try, then offer the way back to Aether
    const std::wstring name = aeConfigName();
    const bool own = name == L"aether" || !aeExists(aeUserConfigDir(name) + L"\\shell.qml");
    for (int i = 0; i < (own ? 100 : 1); i++) {   // the shell may still be loading
        if (runQs(L"ipc -p \"" + cfgPath() + L"\" " + call, 3000) == 0) return;
        Sleep(200);
    }
    if (!own && MessageBoxW(nullptr, (L"The Quickshell config \"" + name + L"\" is running, and it has no Aether Settings.\n\n"
                                      L"Switch back to Aether?").c_str(), L"Aether", MB_YESNO | MB_ICONQUESTION) == IDYES)
        switchConfig(L"aether");
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_pkg = self; g_pkg = g_pkg.substr(0, g_pkg.find_last_of(L'\\'));
    setupEnv();

    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring a = argc > 1 ? argv[1] : L"";
    std::wstring b = argc > 2 ? argv[2] : L"";
    std::wstring c = argc > 3 ? argv[3] : L"";
    LocalFree(argv);
    if (!a.empty() && a != L"--sync-config" && a != L"--start-elevated" && a != L"--watch") aeLog(L"Caelestia.exe " + a + (b.empty() ? L"" : L" " + b));

    // a config fetched from a link (assets/install-config.ps1): merge folder b into ~/.config/quickshell/<c>
    if (a == L"--sync-from") {
        if (b.empty() || c.empty() || c.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos || c == L"." || c == L".."
            || c == L"aether" || c == L"ryoku" || !aeExists(b + L"\\shell.qml"))
            return 1;
        syncOne(b, c, true);
        return aeExists(aeUserConfigDir(c) + L"\\shell.qml") ? 0 : 1;
    }

    if (a == L"--start-elevated") {   // the UAC-approved half of start()
        std::wstring exe = qsDir() + L"\\qsw.exe";
        std::wstring cmd = L"\"" + exe + L"\" -p \"" + cfgPath() + L"\"";
        STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, g_pkg.c_str(), &si, &pi)) return 1;
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        spawnWatcher();
        return 0;
    }
    if (a == L"--watch") return watchShell();
    if (a == L"--stop") { stop(); return 0; }
    if (a == L"--sync-config") { syncConfig(true); return 0; }   // merge the package's config into yours now
    if (a == L"--config") return b.empty() ? 1 : switchConfig(b) ? 0 : 1;   // run ~/.config/quickshell/<name>
    if (a == L"--reset-config") {                                 // your copy -> <name>.backup-<time>, fresh copy
        const std::wstring name = b.empty() ? L"aether" : b;
        if (name != L"aether" && name != L"ryoku") return 1;      // only the configs Aether ships
        const bool was = running();
        if (was) stop();
        const bool ok = resetConfig(name);
        if (was) start();
        return ok ? 0 : 1;
    }
    if (a == L"--restart") { stop(); return start() ? 0 : 1; }
    if (a == L"--settings") { if (!running() && !start()) return 1; openSettings(b); return 0; }   // optional page name
    // plain run: start, or bring up Settings when it is already running
    if (running()) { openSettings(); return 0; }
    return start() ? 0 : 1;
}
