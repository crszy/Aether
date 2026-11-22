#include "unelevated.hpp"

#include <string>
#include <vector>

#include <qdir.h>
#include <qlogging.h>
#include <qloggingcategory.h>

#include <windows.h>
#include <tlhelp32.h>
#include <userenv.h>

namespace qs::windows::unelevated {

namespace {

Q_LOGGING_CATEGORY(logUnelevated, "quickshell.windows.unelevated", QtInfoMsg);

struct Handle {
	HANDLE h = nullptr;
	Handle() = default;
	explicit Handle(HANDLE handle): h(handle) {}
	~Handle() {
		if (this->h != nullptr && this->h != INVALID_HANDLE_VALUE) CloseHandle(this->h);
	}
	Handle(const Handle&) = delete;
	Handle& operator=(const Handle&) = delete;
};

bool tokenIsElevated(HANDLE token);

// a process of this user in this session that runs as a normal app: the desktop shell (explorer.exe) when there
// is one, else Windows' per-user shell host (sihost.exe, always there - also when Caelestia replaces explorer),
// else the text-input host (ctfmon.exe)
DWORD normalProcessId() {
	DWORD session = 0;
	ProcessIdToSessionId(GetCurrentProcessId(), &session);

	if (auto* shell = GetShellWindow(); shell != nullptr) {
		DWORD pid = 0;
		GetWindowThreadProcessId(shell, &pid);
		if (pid != 0) return pid;
	}

	for (const auto* name: {L"explorer.exe", L"sihost.exe", L"ctfmon.exe"}) {
		auto snap = Handle(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
		if (snap.h == INVALID_HANDLE_VALUE) return 0;
		PROCESSENTRY32W entry {};
		entry.dwSize = sizeof(entry);
		for (auto ok = Process32FirstW(snap.h, &entry); ok; ok = Process32NextW(snap.h, &entry)) {
			if (_wcsicmp(entry.szExeFile, name) != 0) continue;
			DWORD s = 0;
			if (!ProcessIdToSessionId(entry.th32ProcessID, &s) || s != session) continue;
			auto process = Handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
			HANDLE token = nullptr;
			if (process.h == nullptr || !OpenProcessToken(process.h, TOKEN_QUERY, &token)) continue;
			auto owned = Handle(token);
			if (!tokenIsElevated(owned.h)) return entry.th32ProcessID;
		}
	}
	return 0;
}

// that process's token, duplicated as a primary token we can start processes with
HANDLE shellToken() {
	auto pid = normalProcessId();
	if (pid == 0) return nullptr;

	auto process = Handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
	if (process.h == nullptr) return nullptr;

	HANDLE token = nullptr;
	if (!OpenProcessToken(process.h, TOKEN_DUPLICATE, &token)) return nullptr;
	auto owned = Handle(token);

	HANDLE primary = nullptr;
	const DWORD access = TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID;
	if (!DuplicateTokenEx(owned.h, access, nullptr, SecurityImpersonation, TokenPrimary, &primary)) return nullptr;
	return primary;
}

// explorer must itself be a normal process, or there is nothing to gain
bool tokenIsElevated(HANDLE token) {
	TOKEN_ELEVATION elevation {};
	DWORD size = 0;
	if (!GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) return true;
	return elevation.TokenIsElevated != 0;
}

std::vector<wchar_t> environmentBlock(const QProcessEnvironment& env) {
	std::vector<wchar_t> block;
	for (const auto& key: env.keys()) {
		auto entry = (key + '=' + env.value(key)).toStdWString();
		block.insert(block.end(), entry.begin(), entry.end());
		block.push_back(L'\0');
	}
	block.push_back(L'\0');
	return block;
}

bool tokenIsElevatedSelf();

bool spawnImpl(const QString& commandLine, const QString& workingDirectory, DWORD flags, const QProcessEnvironment* env) {
	if (!tokenIsElevatedSelf()) return false;

	auto token = Handle(shellToken());
	if (token.h == nullptr) {
		qCWarning(logUnelevated) << "No normal-user process to borrow a token from; starting" << commandLine
		                         << "as administrator";
		return false;
	}
	if (tokenIsElevated(token.h)) return false;   // explorer itself runs elevated: nothing to drop

	auto cmd = commandLine.toStdWString();
	auto cwd = QDir::toNativeSeparators(workingDirectory.isEmpty() ? QDir::homePath() : workingDirectory).toStdWString();

	void* block = nullptr;
	std::vector<wchar_t> custom;
	if (env != nullptr) {
		custom = environmentBlock(*env);
		block = custom.data();
	} else if (!CreateEnvironmentBlock(&block, token.h, FALSE)) {
		block = nullptr;
	}

	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION info {};
	auto ok = CreateProcessWithTokenW(
	    token.h,
	    0,
	    nullptr,
	    cmd.data(),
	    flags | CREATE_UNICODE_ENVIRONMENT,
	    block,
	    cwd.c_str(),
	    &startup,
	    &info
	);
	auto error = GetLastError();
	if (env == nullptr && block != nullptr) DestroyEnvironmentBlock(block);

	if (!ok) {
		qCWarning(logUnelevated) << "Could not start" << commandLine << "as a normal app (error" << error
		                         << "); starting it as administrator";
		return false;
	}

	CloseHandle(info.hThread);
	CloseHandle(info.hProcess);
	qCDebug(logUnelevated) << "Started" << commandLine << "as a normal app";
	return true;
}

bool tokenIsElevatedSelf() { return shellIsElevated(); }

} // namespace

bool spawn(
    const QString& program,
    const QStringList& args,
    const QString& workingDirectory,
    DWORD flags,
    const std::optional<QProcessEnvironment>& environment
) {
	auto line = quoteArgument(QDir::toNativeSeparators(program));
	for (const auto& a: args) line += ' ' + quoteArgument(a);
	return spawnImpl(line, workingDirectory, flags, environment ? &*environment : nullptr);
}

bool shellIsElevated() {
	static const bool elevated = [] {
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
		auto owned = Handle(token);
		return tokenIsElevated(owned.h);
	}();
	return elevated;
}

QString quoteArgument(const QString& argument) {
	if (!argument.isEmpty() && !argument.contains(' ') && !argument.contains('\t') && !argument.contains('"')) {
		return argument;
	}

	QString out = "\"";
	qsizetype slashes = 0;
	for (auto c: argument) {
		if (c == '\\') {
			slashes++;
			continue;
		}
		if (c == '"') out += QString(slashes * 2 + 1, '\\');
		else out += QString(slashes, '\\');
		slashes = 0;
		out += c;
	}
	out += QString(slashes * 2, '\\');
	out += '"';
	return out;
}

bool spawnCommandLine(const QString& commandLine, const QString& workingDirectory, DWORD flags) {
	return spawnImpl(commandLine, workingDirectory, flags, nullptr);
}

} // namespace qs::windows::unelevated
