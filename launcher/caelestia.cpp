// caelestia.exe: the `caelestia` command Caelestia's QML runs, for Windows.
// Runs <package>\cli\venv\Scripts\pythonw.exe -m caelestia <args> with PYTHONPATH=<package>\cli\src,
// passing stdin/stdout/stderr straight through (the shell reads `scheme list` etc. from stdout) and returning
// the exit code. Built for the Windows subsystem and starting pythonw, so no console window ever flashes.
// Lives in <package>\qs\, next to qs.exe.
#include <windows.h>
#include <string>
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

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring dir(self); dir = dir.substr(0, dir.find_last_of(L'\\'));          // <package>\qs
    std::wstring pkg = dir.substr(0, dir.find_last_of(L'\\'));                      // <package>
    std::wstring py = pkg + L"\\cli\\venv\\Scripts\\pythonw.exe";
    SetEnvironmentVariableW(L"PYTHONPATH", (pkg + L"\\cli\\src").c_str());
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");

    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    // whoever ran us may hand its right to come to the front on to the shell (Settings opening from a hotkey)
    AllowSetForegroundWindow(ASFW_ANY);

    std::wstring exe = py;
    std::wstring cmd;
    if (argc >= 4 && std::wstring(argv[1]) == L"shell" && argv[2][0] != L'-') {
        // `caelestia shell <target> <function> [args]` -> this shell's IPC, like the Linux CLI does through qs
        exe = dir + L"\\qs.exe";
        cmd = quote(exe) + L" ipc -p " + quote(aeShellQml(pkg)) + L" call";
        for (int i = 2; i < argc; i++) cmd += L" " + quote(argv[i]);
    } else {
        cmd = quote(py) + L" -m caelestia";
        for (int i = 1; i < argc; i++) cmd += L" " + quote(argv[i]);
    }
    LocalFree(argv);

    STARTUPINFOW si{}; si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return 127;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
