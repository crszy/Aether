#include "session.hpp"

#include <qt_windows.h>

#include <mmsystem.h>
#include <powrprof.h>
#include <winbase.h>
#include <winreg.h>

#include <qhash.h>
#include <qlogging.h>
#include <qloggingcategory.h>

#include "../../core/logcat.hpp"

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logSession, "quickshell.windows.session", QtWarningMsg);

bool enablePrivilege(LPCWSTR name) {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
		return false;
	}

	LUID luid {};
	if (!LookupPrivilegeValueW(nullptr, name, &luid)) {
		CloseHandle(token);
		return false;
	}

	TOKEN_PRIVILEGES tp {};
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	auto ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr) != FALSE;
	CloseHandle(token);
	return ok && GetLastError() == ERROR_SUCCESS;
}

} // namespace

void Session::lock() {
	if (!LockWorkStation()) {
		qCWarning(logSession) << "LockWorkStation failed:" << GetLastError();
	}
}

void Session::logout() {
	if (!ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_FLAG_PLANNED | SHTDN_REASON_MAJOR_OTHER)) {
		qCWarning(logSession) << "ExitWindowsEx(EWX_LOGOFF) failed:" << GetLastError();
	}
}

void Session::shutdown() {
	enablePrivilege(SE_SHUTDOWN_NAME);

	auto result = InitiateShutdownW(
	    nullptr,
	    nullptr,
	    0,
	    SHUTDOWN_POWEROFF,
	    SHTDN_REASON_FLAG_PLANNED | SHTDN_REASON_MAJOR_OTHER
	);

	if (result != ERROR_SUCCESS) {
		qCWarning(logSession) << "InitiateShutdownW(SHUTDOWN_POWEROFF) failed:" << result;
	}
}

void Session::reboot() {
	enablePrivilege(SE_SHUTDOWN_NAME);

	auto result = InitiateShutdownW(
	    nullptr,
	    nullptr,
	    0,
	    SHUTDOWN_RESTART,
	    SHTDN_REASON_FLAG_PLANNED | SHTDN_REASON_MAJOR_OTHER
	);

	if (result != ERROR_SUCCESS) {
		qCWarning(logSession) << "InitiateShutdownW(SHUTDOWN_RESTART) failed:" << result;
	}
}

void Session::suspend() {
	if (!SetSuspendState(FALSE, FALSE, FALSE)) {
		qCWarning(logSession) << "SetSuspendState(suspend) failed:" << GetLastError();
	}
}

void Session::hibernate() {
	if (!SetSuspendState(TRUE, FALSE, FALSE)) {
		qCWarning(logSession) << "SetSuspendState(hibernate) failed:" << GetLastError();
	}
}

void Session::rebootToFirmware() {
	STARTUPINFOW si {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi {};

	wchar_t cmdLine[] = L"shutdown.exe /r /fw /t 0"; // NOLINT

	if (!CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		qCWarning(logSession) << "CreateProcessW(shutdown /r /fw) failed:" << GetLastError();
		return;
	}

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
}

bool Session::canHibernate() {
	SYSTEM_POWER_CAPABILITIES caps {};
	if (!GetPwrCapabilities(&caps)) return false;
	return caps.HiberFilePresent != FALSE;
}

void Session::playSystemSound(const QString& name) {
	static const QHash<QString, const wchar_t*> EVENTS = {
	    {"dialog-warning", L"SystemExclamation"},
	    {"dialog-error", L"SystemHand"},
	    {"dialog-information", L"SystemAsterisk"},
	    {"suspend-error", L"CriticalBatteryAlarm"},
	    {"battery-low", L"LowBatteryAlarm"},
	    {"battery-caution", L"CriticalBatteryAlarm"},
	    {"complete", L"Notification.Default"},
	    {"message", L"Notification.Default"},
	    {"message-new-instant", L"Notification.IM"},
	    {"power-plug", L"DeviceConnect"},
	    {"power-unplug", L"DeviceDisconnect"},
	    {"device-added", L"DeviceConnect"},
	    {"device-removed", L"DeviceDisconnect"},
	    {"alarm-clock-elapsed", L"Notification.Looping.Alarm"},
	    {"bell", L"SystemDefault"},
	};

	const auto* alias = EVENTS.value(name, L"Notification.Default");
	PlaySoundW(alias, nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
}

QVariantMap Session::osInfo() {
	auto readString = [](const wchar_t* name) {
		wchar_t buffer[256] {};
		DWORD size = sizeof(buffer);
		auto status = RegGetValueW(
		    HKEY_LOCAL_MACHINE,
		    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
		    name,
		    RRF_RT_REG_SZ,
		    nullptr,
		    buffer,
		    &size
		);
		return status == ERROR_SUCCESS ? QString::fromWCharArray(buffer) : QString();
	};

	DWORD ubr = 0;
	DWORD ubrSize = sizeof(ubr);
	auto hasUbr = RegGetValueW(
	                  HKEY_LOCAL_MACHINE,
	                  L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
	                  L"UBR",
	                  RRF_RT_REG_DWORD,
	                  nullptr,
	                  &ubr,
	                  &ubrSize
	              )
	           == ERROR_SUCCESS;

	auto build = readString(L"CurrentBuildNumber");
	auto name = readString(L"ProductName");

	if (build.toInt() >= 22000) name.replace("Windows 10", "Windows 11");
	if (name.isEmpty()) name = "Windows";

	return {
	    {"name", name},
	    {"version", readString(L"DisplayVersion")},
	    {"build", hasUbr ? build + "." + QString::number(ubr) : build},
	    {"edition", readString(L"EditionID")},
	};
}

} // namespace qs::windows::sys
