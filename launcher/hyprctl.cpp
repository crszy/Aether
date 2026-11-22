// hyprctl.exe: Hyprland's hyprctl for Caelestia on Windows. Talks to the running shell (services/HyprConf.qml) over qs ipc.
//   hyprctl dispatch workspace 2        hyprctl reload          hyprctl keyword general:gaps_in 10
//   hyprctl clients | workspaces | activewindow | activeworkspace | monitors | binds | notes | version
//   hyprctl getoption general:layout    hyprctl batch "dispatch workspace 1 ; keyword general:gaps_out 0"
// -j is accepted (output is always JSON). Lives in <package>\qs\, next to qs.exe; console program.
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include "aetherconfig.hpp"

static std::wstring quote(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    size_t bs = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { bs++; continue; }
        if (c == L'"') r.append(bs * 2 + 1, L'\\'); else r.append(bs, L'\\');
        bs = 0; r += c;
    }
    r.append(bs * 2, L'\\'); r += L'"';
    return r;
}

static std::wstring trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t"), b = s.find_last_not_of(L" \t");
    return a == std::wstring::npos ? L"" : s.substr(a, b - a + 1);
}

static std::wstring g_qs, g_shell;

static int call(const std::vector<std::wstring>& args) {
    std::wstring cmd = quote(g_qs) + L" ipc -p " + quote(g_shell) + L" call hyprctl";
    for (auto& a : args) cmd += L" " + quote(a);
    STARTUPINFOW si{}; si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(g_qs.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        fwprintf(stderr, L"hyprctl: could not start %ls\n", g_qs.c_str());
        return 127;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

// one request: "dispatch workspace 2", "keyword general:gaps_in 5", "clients" ...
static int request(std::vector<std::wstring> w) {
    if (w.empty()) return 0;
    const std::wstring c = w[0];
    if (c == L"dispatch") {
        std::wstring rest;
        for (size_t i = 1; i < w.size(); i++) rest += (i > 1 ? L" " : L"") + w[i];
        return call({L"dispatch", rest});
    }
    if (c == L"keyword") {
        if (w.size() < 3) { fwprintf(stderr, L"hyprctl keyword <option> <value>\n"); return 2; }
        std::wstring v;
        for (size_t i = 2; i < w.size(); i++) v += (i > 2 ? L" " : L"") + w[i];
        return call({L"keyword", w[1], v});
    }
    if (c == L"getoption") {
        if (w.size() < 2) { fwprintf(stderr, L"hyprctl getoption <option>\n"); return 2; }
        return call({L"getoption", w[1]});
    }
    static const wchar_t* plain[] = {L"reload", L"clients", L"workspaces", L"activewindow", L"activeworkspace",
                                     L"monitors", L"binds", L"notes", L"version"};
    for (auto p : plain) if (c == p) return call({c});
    fwprintf(stderr, L"hyprctl: unknown request '%ls'\n", c.c_str());
    return 2;
}

// The Hyprland window manager (komorebi --hyprland, the Rust one) serves Hyprland's own request socket as a named
// pipe called "$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock" (defaults: %TEMP%, "aether").
// When it runs, hyprctl talks to it directly - exactly like Hyprland's hyprctl. Returns false when it is not there.
static std::wstring envOr(const wchar_t* name, const std::wstring& fallback) {
    wchar_t buf[4096]; DWORD n = GetEnvironmentVariableW(name, buf, 4096);
    return (n > 0 && n < 4096) ? std::wstring(buf, n) : fallback;
}

static bool realRequest(const std::string& request) {
    wchar_t tmp[MAX_PATH]; DWORD n = GetTempPathW(MAX_PATH, tmp);
    std::wstring temp(tmp, n);
    while (!temp.empty() && (temp.back() == L'\\' || temp.back() == L'/')) temp.pop_back();
    std::wstring runtime = envOr(L"XDG_RUNTIME_DIR", temp), sig = envOr(L"HYPRLAND_INSTANCE_SIGNATURE", L"aether");
    for (auto& c : runtime) if (c == L'\\') c = L'/';
    std::wstring pipe = L"\\\\.\\pipe\\" + runtime + L"/hypr/" + sig + L"/.socket.sock";
    if (!WaitNamedPipeW(pipe.c_str(), 300)) return false;
    HANDLE h = CreateFileW(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(h, request.data(), (DWORD)request.size(), &written, nullptr);
    std::string reply;
    char buf[65536]; DWORD got = 0;
    while (ReadFile(h, buf, sizeof buf, &got, nullptr) && got > 0) reply.append(buf, got);
    CloseHandle(h);
    fwrite(reply.data(), 1, reply.size(), stdout);
    if (reply.empty() || reply.back() != '\n') fputc('\n', stdout);
    return true;
}

static std::string utf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

int wmain(int argc, wchar_t** argv) {
    // the real Hyprland socket first (the Rust window manager); otherwise the shell's HyprConf service below
    {
        bool json = false, batch = false;
        std::wstring req;
        for (int i = 1; i < argc; i++) {
            std::wstring a = argv[i];
            if (a == L"-j" || a == L"--json") { json = true; continue; }
            if (req.empty() && a == L"--batch") { batch = true; continue; }
            if (req.empty() && a == L"batch") { batch = true; continue; }
            req += (req.empty() ? L"" : L" ") + a;
        }
        if (!req.empty() && req != L"-h" && req != L"--help" && req != L"help") {
            std::string r = (batch ? "[[BATCH]]" : "") + std::string(json ? "j/" : "") + utf8(req);
            if (realRequest(r)) return 0;
        }
    }
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring dir(self); dir = dir.substr(0, dir.find_last_of(L'\\'));      // <package>\qs
    std::wstring pkg = dir.substr(0, dir.find_last_of(L'\\'));
    g_qs = dir + L"\\qs.exe";
    g_shell = aeShellQml(pkg);   // ~/.config/quickshell/<config>/shell.qml, else the package's

    std::vector<std::wstring> w;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"-j" || a == L"--json") continue;
        w.push_back(a);
    }
    if (w.empty() || w[0] == L"-h" || w[0] == L"--help" || w[0] == L"help") {
        wprintf(L"usage: hyprctl [-j] <request>\n"
                L"  dispatch <dispatcher> [args]   keyword <option> <value>   getoption <option>   reload\n"
                L"  clients  workspaces  activewindow  activeworkspace  monitors  binds  notes  version\n"
                L"  batch \"<request> ; <request> ...\"\n");
        return 0;
    }
    if (w[0] == L"batch") {
        std::wstring all;
        for (size_t i = 1; i < w.size(); i++) all += (i > 1 ? L" " : L"") + w[i];
        int rc = 0;
        size_t start = 0;
        while (start <= all.size()) {
            size_t semi = all.find(L';', start);
            std::wstring one = trim(all.substr(start, semi == std::wstring::npos ? std::wstring::npos : semi - start));
            if (!one.empty()) {
                std::vector<std::wstring> parts;
                size_t p = 0;
                while (p < one.size()) {
                    size_t q = one.find(L' ', p);
                    std::wstring t = one.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p);
                    if (!t.empty()) parts.push_back(t);
                    if (q == std::wstring::npos) break;
                    p = q + 1;
                }
                rc |= request(parts);
            }
            if (semi == std::wstring::npos) break;
            start = semi + 1;
        }
        return rc;
    }
    return request(w);
}
