#include <cstdint>
#include <cstdio>
#include <vector>

#include <qlogging.h>
#include <qstring.h>
#include <qtenvironmentvariables.h>
#include <windows.h>

#include "launch_p.hpp"

namespace qs::launch {

void* DAEMON_PIPE = nullptr; // NOLINT

namespace {

constexpr auto DAEMON_PIPE_ENV = "__QUICKSHELL_DAEMON_PIPE";

QString quoteArgument(const QString& arg) {
	if (!arg.isEmpty() && !arg.contains(u' ') && !arg.contains(u'\t') && !arg.contains(u'"')) {
		return arg;
	}

	auto quoted = QString(u'"');
	qsizetype backslashes = 0;

	for (auto c: arg) {
		if (c == u'\\') {
			backslashes++;
		} else if (c == u'"') {
			quoted += QString(backslashes * 2 + 1, u'\\');
			quoted += c;
			backslashes = 0;
		} else {
			quoted += QString(backslashes, u'\\');
			quoted += c;
			backslashes = 0;
		}
	}

	quoted += QString(backslashes * 2, u'\\');
	quoted += u'"';
	return quoted;
}

} // namespace

bool spawnDaemon(int argc, char** argv, int* exitCode) {
	if (qEnvironmentVariableIsSet(DAEMON_PIPE_ENV)) {
		auto ok = false;
		auto handle = qEnvironmentVariable(DAEMON_PIPE_ENV).toULongLong(&ok);
		qunsetenv(DAEMON_PIPE_ENV);

		if (ok && handle != 0) {
			DAEMON_PIPE = reinterpret_cast<void*>(static_cast<uintptr_t>(handle)); // NOLINT
		}

		return false;
	}

	SECURITY_ATTRIBUTES inheritable {};
	inheritable.nLength = sizeof(inheritable);
	inheritable.bInheritHandle = TRUE;

	HANDLE readEnd = nullptr;
	HANDLE writeEnd = nullptr;
	if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0)) {
		auto error = GetLastError();
		qCritical().nospace() << "Failed to create messaging pipe for daemon with error code "
		                      << error << ": " << qt_error_string(static_cast<int>(error));
		*exitCode = -1;
		return true;
	}

	SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

	auto exeBuf = std::vector<wchar_t>(MAX_PATH);
	for (;;) {
		auto len = GetModuleFileNameW(nullptr, exeBuf.data(), static_cast<DWORD>(exeBuf.size()));
		if (len == 0) {
			auto error = GetLastError();
			qCritical().nospace() << "Failed to get executable path for daemon with error code "
			                      << error << ": " << qt_error_string(static_cast<int>(error));
			CloseHandle(readEnd);
			CloseHandle(writeEnd);
			*exitCode = -1;
			return true;
		}

		if (len < exeBuf.size()) {
			exeBuf.resize(len);
			break;
		}

		exeBuf.resize(exeBuf.size() * 2);
	}

	auto exe = QString::fromWCharArray(exeBuf.data(), static_cast<qsizetype>(exeBuf.size()));
	auto commandLine = quoteArgument(exe);

	for (auto i = 1; i < argc; ++i) {
		auto arg = QString::fromLocal8Bit(argv[i]); // NOLINT
		if (arg == "-d" || arg == "--daemonize") continue;
		commandLine += u' ' + quoteArgument(arg);
	}

	auto pipeValue = QString::number(reinterpret_cast<uintptr_t>(writeEnd)); // NOLINT
	SetEnvironmentVariableW(L"__QUICKSHELL_DAEMON_PIPE", pipeValue.toStdWString().c_str());

	auto commandLineW = commandLine.toStdWString();
	auto commandLineBuf = std::vector<wchar_t>(commandLineW.begin(), commandLineW.end());
	commandLineBuf.push_back(L'\0');

	STARTUPINFOW startupInfo {};
	startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION processInfo {};

	auto created = CreateProcessW(
	    nullptr,
	    commandLineBuf.data(),
	    nullptr,
	    nullptr,
	    TRUE,
	    DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT,
	    nullptr,
	    nullptr,
	    &startupInfo,
	    &processInfo
	);

	auto error = GetLastError();
	SetEnvironmentVariableW(L"__QUICKSHELL_DAEMON_PIPE", nullptr);
	CloseHandle(writeEnd);

	if (!created) {
		qCritical().nospace() << "Failed to launch daemon process with error code " << error << ": "
		                      << qt_error_string(static_cast<int>(error));
		CloseHandle(readEnd);
		*exitCode = -1;
		return true;
	}

	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);

	auto ret = 0;
	DWORD readBytes = 0;
	if (!ReadFile(readEnd, &ret, sizeof(int), &readBytes, nullptr) || readBytes != sizeof(int)) {
		qCritical() << "Failed to wait for daemon launch (it may have crashed)";
		ret = -1;
	}

	CloseHandle(readEnd);
	*exitCode = ret;
	return true;
}

void exitDaemon(int code) {
	if (DAEMON_PIPE == nullptr) return;

	DWORD written = 0;
	if (!WriteFile(DAEMON_PIPE, &code, sizeof(int), &written, nullptr)) {
		auto error = GetLastError();
		qCritical().nospace() << "Failed to write daemon exit command with error code " << error
		                      << ": " << qt_error_string(static_cast<int>(error));
	}

	CloseHandle(DAEMON_PIPE);
	DAEMON_PIPE = nullptr;

	FILE* stream = nullptr;
	freopen_s(&stream, "NUL", "r", stdin);
	freopen_s(&stream, "NUL", "w", stdout);
	freopen_s(&stream, "NUL", "w", stderr);
}

} // namespace qs::launch
