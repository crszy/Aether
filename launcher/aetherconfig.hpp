// Which Quickshell config Aether runs - shared by Caelestia.exe, caelestia.exe (CLI shim) and hyprctl.exe.
// Like Linux: a config is a folder ~/.config/quickshell/<name>/ with a shell.qml, and it is yours to edit
// (Quickshell reloads it live). The name comes from windows.json "shellConfig" (default "aether").
// "aether" is copied from <package>\config on first start (Caelestia.exe syncConfig); until then, or when the
// chosen folder has no shell.qml, the package's own config runs.
#pragma once
#include <windows.h>
#include <string>

inline std::wstring aeEnv(const wchar_t* name) {
    wchar_t buf[MAX_PATH]; DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    return n && n < MAX_PATH ? std::wstring(buf, n) : std::wstring();
}

// "C:\x\..." -> "\\?\C:\x\..." so paths over 260 characters work (shell repos nest deep)
inline std::wstring aeLong(std::wstring p) {
    for (auto& c : p) if (c == L'/') c = L'\\';
    if (p.size() > 2 && p[1] == L':' && p[2] == L'\\') return L"\\\\?\\" + p;
    return p;
}
inline bool aeExists(const std::wstring& p) { return GetFileAttributesW(aeLong(p).c_str()) != INVALID_FILE_ATTRIBUTES; }

inline std::string aeReadSmall(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string s(65536, '\0'); DWORD got = 0;
    ReadFile(f, s.data(), (DWORD)s.size(), &got, nullptr); CloseHandle(f);
    s.resize(got);
    return s;
}

// ~/.config/quickshell (the folder of all Quickshell configs)
inline std::wstring aeQuickshellDir() { return aeEnv(L"USERPROFILE") + L"\\.config\\quickshell"; }

// windows.json "shellConfig"; only a plain folder name is accepted
inline std::wstring aeConfigName() {
    std::string s = aeReadSmall(aeEnv(L"USERPROFILE") + L"\\.config\\caelestia\\windows.json");
    size_t k = s.find("\"shellConfig\"");
    std::wstring name = L"aether";
    if (k != std::string::npos) {
        size_t q1 = s.find_first_not_of(" \t\r\n:", k + 13);
        size_t q2 = q1 != std::string::npos && s[q1] == '"' ? s.find('"', q1 + 1) : std::string::npos;
        if (q2 != std::string::npos && q2 > q1 + 1) {
            std::string v = s.substr(q1 + 1, q2 - q1 - 1);
            bool ok = v != "." && v != "..";
            for (char c : v) if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|') ok = false;
            if (ok) {
                int n = MultiByteToWideChar(CP_UTF8, 0, v.data(), (int)v.size(), nullptr, 0);
                name.assign(n, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, v.data(), (int)v.size(), name.data(), n);
            }
        }
    }
    return name;
}

inline std::wstring aeUserConfigDir(const std::wstring& name) { return aeQuickshellDir() + L"\\" + name; }

// the shell.qml to run / talk to (ipc, kill): the chosen user config, else the package's
inline std::wstring aeShellQml(const std::wstring& pkg) {
    std::wstring user = aeUserConfigDir(aeConfigName()) + L"\\shell.qml";
    return aeExists(user) ? user : pkg + L"\\config\\shell.qml";
}
