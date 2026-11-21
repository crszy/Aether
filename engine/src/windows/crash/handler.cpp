#include "handler.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cwchar>
#include <exception>
#include <iterator>

#include <qt_windows.h>

#include <dbghelp.h>
#include <io.h>

#include <qdatetime.h>
#include <qdir.h>
#include <qfile.h>
#include <qlogging.h>
#include <qstring.h>
#include <qtenvironmentvariables.h>
#include <qtextstream.h>

#include "../../core/debuginfo.hpp"
#include "../../core/instanceinfo.hpp"
#include "../../core/logcat.hpp"
#include "../../core/paths.hpp"
#include "../taskbar.hpp"
#include "../tiling.hpp"
#include "../window_decorations.hpp"

namespace qs::crash {

namespace {

QS_LOGGING_CATEGORY(logCrashHandler, "quickshell.crashhandler", QtWarningMsg);

constexpr int PATH_CHARS = 1024;
bool gReady = false;
wchar_t gExePath[PATH_CHARS] = L"";
wchar_t gConfigPath[PATH_CHARS] = L"";
wchar_t gCrashDir[PATH_CHARS] = L"";
wchar_t gDumpPath[PATH_CHARS] = L"";
wchar_t gLaunchTimeStr[32] = L"0";
qint64 gLaunchTimeMs = 0;
QtMessageHandler gPreviousMessageHandler = nullptr;

std::atomic<bool> gHandling {false};

qint64 currentEpochMs() {
	FILETIME ft {};
	GetSystemTimeAsFileTime(&ft);
	auto ticks = (static_cast<quint64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	return static_cast<qint64>(ticks / 10000) - 11644473600000LL;
}

void writeSupportingFiles(const wchar_t* reason, DWORD exceptionCode) {
	auto dir = QDir(QString::fromWCharArray(gCrashDir));

	if (CrashInfo::INSTANCE.logFd != -1) {
		auto logHandle = reinterpret_cast<HANDLE>(_get_osfhandle(CrashInfo::INSTANCE.logFd)); // NOLINT
		if (logHandle != INVALID_HANDLE_VALUE) {
			auto destPath = dir.filePath("log.qslog.log");
			auto* dest = CreateFileW(
			    reinterpret_cast<LPCWSTR>(destPath.utf16()),
			    GENERIC_WRITE,
			    0,
			    nullptr,
			    CREATE_ALWAYS,
			    FILE_ATTRIBUTE_NORMAL,
			    nullptr
			);

			if (dest != INVALID_HANDLE_VALUE) {
				SetFilePointer(logHandle, 0, nullptr, FILE_BEGIN); // NOLINT
				auto buf = std::array<char, 64 * 1024>();
				DWORD read = 0;
				while (ReadFile(logHandle, buf.data(), buf.size(), &read, nullptr) && read > 0) {
					DWORD written = 0;
					WriteFile(dest, buf.data(), read, &written, nullptr);
				}
				CloseHandle(dest);
			}
		}
	}

	auto reportFile = QFile(dir.filePath("report.txt"));
	if (reportFile.open(QFile::WriteOnly)) {
		auto stream = QTextStream(&reportFile);
		stream << qs::debuginfo::combinedInfo();
		stream << "\n===== Instance Information =====\n";
		stream << "Reason: " << QString::fromWCharArray(reason) << " (exception code 0x" << Qt::hex
		       << exceptionCode << Qt::dec << ")\n";
		stream << "Config Path: " << QString::fromWCharArray(gConfigPath) << '\n';
		stream << "Dump: " << QString::fromWCharArray(gDumpPath) << '\n';
	}
}

void relaunch(const wchar_t* reason) {
	SetEnvironmentVariableW(L"QS_DEBUG_CRASH_TEST", nullptr);
	SetEnvironmentVariableW(L"QS_DEBUG_CRASH_DELAY_MS", nullptr);

	SetEnvironmentVariableW(L"__QUICKSHELL_CRASH_RELAUNCH", L"1");
	SetEnvironmentVariableW(L"__QUICKSHELL_CRASH_LAUNCH_TIME", gLaunchTimeStr);
	SetEnvironmentVariableW(L"__QUICKSHELL_CRASH_REASON", reason);
	SetEnvironmentVariableW(L"__QUICKSHELL_CRASH_DUMP_PATH", gDumpPath);

	STARTUPINFOW si {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi {};

	auto cmdLine = std::array<wchar_t, PATH_CHARS * 4>();
	swprintf(cmdLine.data(), cmdLine.size(), L"\"%ls\" -p \"%ls\"", gExePath, gConfigPath); // NOLINT

	if (CreateProcessW(
	        gExePath,
	        cmdLine.data(),
	        nullptr,
	        nullptr,
	        FALSE,
	        0,
	        nullptr,
	        nullptr,
	        &si,
	        &pi
	    ))
	{
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
}

void handleCrash(const wchar_t* reason, EXCEPTION_POINTERS* ep) {
	auto exceptionCode = ep != nullptr ? ep->ExceptionRecord->ExceptionCode : 0;
	auto isStackOverflow = exceptionCode == EXCEPTION_STACK_OVERFLOW;

	if (gHandling.exchange(true)) {
		Sleep(30000); // NOLINT
		TerminateProcess(GetCurrentProcess(), 1);
	}

	if (gReady) {
		auto* dumpFile = CreateFileW(
		    gDumpPath,
		    GENERIC_WRITE,
		    0,
		    nullptr,
		    CREATE_ALWAYS,
		    FILE_ATTRIBUTE_NORMAL,
		    nullptr
		);

		if (dumpFile != INVALID_HANDLE_VALUE) {
			MINIDUMP_EXCEPTION_INFORMATION exInfo {};
			MINIDUMP_EXCEPTION_INFORMATION* exInfoPtr = nullptr;

			if (ep != nullptr) {
				exInfo.ThreadId = GetCurrentThreadId();
				exInfo.ExceptionPointers = ep;
				exInfo.ClientPointers = FALSE;
				exInfoPtr = &exInfo;
			}

			MiniDumpWriteDump(
			    GetCurrentProcess(),
			    GetCurrentProcessId(),
			    dumpFile,
			    static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithThreadInfo),
			    exInfoPtr,
			    nullptr,
			    nullptr
			);

			CloseHandle(dumpFile);
		}

		qs::windows::TaskbarManager::restoreForCrash();
		qs::windows::DecorationManager::restoreForCrash();
		qs::windows::TilingManager::restoreForCrash();

		auto elapsed = currentEpochMs() - gLaunchTimeMs;
		if (gLaunchTimeMs == 0 || elapsed >= 10000) {
			relaunch(reason);
		}

		if (!isStackOverflow) {
			writeSupportingFiles(reason, static_cast<DWORD>(exceptionCode));
		}
	}

	TerminateProcess(GetCurrentProcess(), 1);
}

LONG WINAPI topLevelFilter(EXCEPTION_POINTERS* ep) {
	handleCrash(L"exception", ep);
	return EXCEPTION_EXECUTE_HANDLER;
}

void invalidParameterHandler(
    const wchar_t* /*expr*/,
    const wchar_t* /*func*/,
    const wchar_t* /*file*/,
    unsigned int /*line*/,
    uintptr_t /*reserved*/
) {
	handleCrash(L"invalid-parameter", nullptr);
}

void terminateHandler() { handleCrash(L"terminate", nullptr); }

void abortHandler(int /*sig*/) { handleCrash(L"abort", nullptr); }

// NOLINTNEXTLINE(misc-no-recursion)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winfinite-recursion"
#endif
__declspec(noinline) int debugForceStackOverflow(int depth) {
	volatile char padding[4096]; // NOLINT
	padding[0] = static_cast<char>(depth);
	return debugForceStackOverflow(depth + 1) + padding[0];
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

void maybeTriggerDebugCrash() {
	auto mode = qEnvironmentVariable("QS_DEBUG_CRASH_TEST");
	if (mode.isEmpty()) return;

	auto delayMs = qEnvironmentVariable("QS_DEBUG_CRASH_DELAY_MS").toInt();
	if (delayMs > 0) {
		qCWarning(logCrashHandler) << "QS_DEBUG_CRASH_TEST set, waiting" << delayMs
		                           << "ms before crashing via:" << mode;
		Sleep(static_cast<DWORD>(delayMs));
	}

	qCWarning(logCrashHandler) << "QS_DEBUG_CRASH_TEST set, deliberately crashing via:" << mode;

	if (mode == "access-violation") {
		auto* p = static_cast<volatile int*>(nullptr);
		*p = 1; // NOLINT
	} else if (mode == "stack-overflow") {
		volatile auto sink = debugForceStackOverflow(0);
		(void) sink;
	} else if (mode == "abort") {
		abort();
	} else if (mode == "terminate") {
		std::terminate();
	} else if (mode == "invalid-parameter") {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnonnull"
#endif
		printf(nullptr); // NOLINT
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
	} else {
		qCWarning(logCrashHandler) << "Unknown QS_DEBUG_CRASH_TEST mode, ignoring:" << mode;
	}
}

} // namespace

void CrashHandler::init() {
	qCDebug(logCrashHandler) << "Starting crash handler...";

	gPreviousMessageHandler = qInstallMessageHandler(
	    [](QtMsgType type, const QMessageLogContext& context, const QString& message) {
		    if (gPreviousMessageHandler != nullptr) gPreviousMessageHandler(type, context, message);
		    if (type == QtFatalMsg) handleCrash(L"qFatal", nullptr);
	    }
	);

	auto guarantee = ULONG {64 * 1024};
	SetThreadStackGuarantee(&guarantee);

	SetUnhandledExceptionFilter(&topLevelFilter);
	_set_invalid_parameter_handler(&invalidParameterHandler);
	std::set_terminate(&terminateHandler);
	signal(SIGABRT, &abortHandler); // NOLINT (misc-include-cleaner)

	qCInfo(logCrashHandler) << "Crash handler initialized.";
}

namespace {
void copyToBuffer(const QString& str, wchar_t* buf, int capacity) {
	auto truncated = str.left(capacity - 1);
	truncated.toWCharArray(buf);
	buf[truncated.size()] = L'\0';
}
} // namespace

void CrashHandler::setRelaunchInfo(const RelaunchInfo& info) {
	GetModuleFileNameW(nullptr, gExePath, PATH_CHARS);

	copyToBuffer(info.instance.configPath, gConfigPath, PATH_CHARS);

	gLaunchTimeMs = info.instance.launchTime.toMSecsSinceEpoch();
	swprintf(gLaunchTimeStr, std::size(gLaunchTimeStr), L"%lld", static_cast<long long>(gLaunchTimeMs)); // NOLINT

	auto dir = QsPaths::crashDir(info.instance.instanceId);
	if (dir.mkpath(".")) {
		copyToBuffer(dir.path(), gCrashDir, PATH_CHARS);

		auto dumpPath =
		    dir.filePath(info.instance.instanceId + "-" + QString::number(gLaunchTimeMs) + ".dmp");
		copyToBuffer(dumpPath, gDumpPath, PATH_CHARS);

		gReady = true;
		qCDebug(logCrashHandler) << "Crash dump path ready:" << dumpPath;
	} else {
		qCCritical(logCrashHandler
		) << "Failed to create crash directory, crash dumps will not be available.";
	}

	maybeTriggerDebugCrash();
}

} // namespace qs::crash
