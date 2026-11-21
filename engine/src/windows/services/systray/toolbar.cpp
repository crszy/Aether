#include <cwchar>
#include <vector>

#include <qt_windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <qlogging.h>
#include <qloggingcategory.h>

#include "../../../core/logcat.hpp"
#include "../../util.hpp"
#include "hook.hpp"

namespace qs::windows::services::systray {

namespace {

QS_LOGGING_CATEGORY(logTrayToolbar, "quickshell.windows.systray", QtWarningMsg);

constexpr UINT TOOLBAR_TIMEOUT_MS = 300;
constexpr LRESULT MAX_BUTTONS = 512;

struct TrayItemHead {
	HWND hwnd;
	UINT uid;
	UINT callbackMessage;
	DWORD state;
	UINT version;
};

bool hasClass(HWND hwnd, const wchar_t* name) {
	wchar_t cls[64] {};
	GetClassNameW(hwnd, cls, 64);
	return wcscmp(cls, name) == 0;
}

std::vector<HWND> trayToolbars() {
	std::vector<HWND> toolbars;

	auto collect = [&](HWND root) {
		if (root == nullptr) return;

		EnumChildWindows(
		    root,
		    [](HWND child, LPARAM param) -> BOOL {
			    // NOLINTNEXTLINE(performance-no-int-to-ptr)
			    auto* list = reinterpret_cast<std::vector<HWND>*>(param);
			    if (!hasClass(child, TOOLBARCLASSNAMEW)) return TRUE;

			    auto* parent = GetParent(child);
			    if (hasClass(parent, L"SysPager") || hasClass(parent, L"TrayNotifyWnd")
			        || hasClass(parent, L"NotifyIconOverflowWindow"))
			    {
				    list->push_back(child);
			    }

			    return TRUE;
		    },
		    reinterpret_cast<LPARAM>(&toolbars)
		);
	};

	collect(explorerTaskbarWindow());

	HWND overflow = nullptr;
	while ((overflow = FindWindowExW(nullptr, overflow, L"NotifyIconOverflowWindow", nullptr))
	       != nullptr)
	{
		collect(overflow);
	}

	return toolbars;
}

} // namespace

std::vector<ExplorerIconData> readExplorerToolbars() {
	if constexpr (sizeof(void*) != 8) return {};

	auto toolbars = trayToolbars();
	if (toolbars.empty()) return {};

	DWORD pid = 0;
	GetWindowThreadProcessId(toolbars.front(), &pid);
	if (pid == 0) return {};

	auto* process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ, FALSE, pid);
	if (process == nullptr) {
		qCDebug(logTrayToolbar) << "Can't open explorer to read its tray toolbars:" << GetLastError();
		return {};
	}

	auto* remote =
	    VirtualAllocEx(process, nullptr, sizeof(TBBUTTON), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (remote == nullptr) {
		CloseHandle(process);
		return {};
	}

	auto ask = [](HWND toolbar, UINT msg, WPARAM wParam, LPARAM lParam, DWORD_PTR* result) {
		return SendMessageTimeoutW(
		           toolbar,
		           msg,
		           wParam,
		           lParam,
		           SMTO_ABORTIFHUNG | SMTO_BLOCK,
		           TOOLBAR_TIMEOUT_MS,
		           result
		       )
		    != 0;
	};

	std::vector<ExplorerIconData> icons;

	for (auto* toolbar: toolbars) {
		DWORD owner = 0;
		GetWindowThreadProcessId(toolbar, &owner);
		if (owner != pid) continue;

		DWORD_PTR count = 0;
		if (!ask(toolbar, TB_BUTTONCOUNT, 0, 0, &count)) continue;

		for (DWORD_PTR i = 0; i < count && i < static_cast<DWORD_PTR>(MAX_BUTTONS); i++) {
			DWORD_PTR ok = FALSE;
			if (!ask(toolbar, TB_GETBUTTON, i, reinterpret_cast<LPARAM>(remote), &ok) || ok == FALSE) {
				continue;
			}

			TBBUTTON button {};
			if (!ReadProcessMemory(process, remote, &button, sizeof(button), nullptr)) continue;
			if (button.dwData == 0) continue;

			TrayItemHead head {};
			if (!ReadProcessMemory(
			        process,
			        reinterpret_cast<const void*>(button.dwData), // NOLINT(performance-no-int-to-ptr)
			        &head,
			        sizeof(head),
			        nullptr
			    ))
			{
				continue;
			}

			auto versionKnown = head.version == 0 || head.version == NOTIFYICON_VERSION
			                 || head.version == NOTIFYICON_VERSION_4;
			if (head.hwnd == nullptr || head.callbackMessage == 0 || !versionKnown) continue;
			if (IsWindow(head.hwnd) == 0) continue;

			icons.push_back({
			    .hwnd = head.hwnd,
			    .uid = head.uid,
			    .callbackMessage = head.callbackMessage,
			    .version = head.version,
			});
		}
	}

	VirtualFreeEx(process, remote, 0, MEM_RELEASE);
	CloseHandle(process);

	qCDebug(logTrayToolbar) << "Read" << icons.size() << "icons from" << toolbars.size()
	                        << "explorer tray toolbars";

	return icons;
}

} // namespace qs::windows::services::systray
