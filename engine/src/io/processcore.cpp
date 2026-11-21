#include "processcore.hpp"

#include <vector>

#include <qcontainerfwd.h>
#include <qcoreapplication.h>
#include <qdir.h>
#include <qfile.h>
#include <qhash.h>
#include <qlogging.h>
#include <qpointer.h>
#include <qprocess.h>
#include <qvariant.h>

#include "../core/common.hpp"

#ifdef _WIN32
#include <qt_windows.h>
#include <tlhelp32.h>
#endif

namespace qs::io::process {

void setupProcessEnvironment(
    QProcess* process,
    bool clear,
    const QHash<QString, QVariant>& envChanges
) {
	const auto& sysenv = qs::Common::INITIAL_ENVIRONMENT;
	auto env = clear ? QProcessEnvironment() : sysenv;

	for (auto& name: envChanges.keys()) {
		auto value = envChanges.value(name);
		if (!value.isValid()) continue;

		if (clear) {
			if (value.isNull()) {
				if (sysenv.contains(name)) env.insert(name, sysenv.value(name));
			} else env.insert(name, value.toString());
		} else {
			if (value.isNull()) env.remove(name);
			else env.insert(name, value.toString());
		}
	}

	process->setProcessEnvironment(env);
}

void disownHandles(QProcess* process) {
#ifdef _WIN32
	process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
		args->inheritHandles = false;
		args->startupInfo->dwFlags &= ~static_cast<DWORD>(STARTF_USESTDHANDLES);
		args->startupInfo->hStdInput = nullptr;
		args->startupInfo->hStdOutput = nullptr;
		args->startupInfo->hStdError = nullptr;
	});
#else
	Q_UNUSED(process);
#endif
}

// An app the user opens (execDetached / Process.startDetached) is not a helper of the shell: it leaves the shell's
// job, or stopping or restarting Aether closed every app opened from its launcher or dock (Discord, 2026-11-15).
// Windows refuses the breakaway when an outer job does not allow it - then it starts as before.
bool startDetachedFree(QProcess* process) {
#ifdef _WIN32
	auto modifier = [](bool breakAway) {
		return [breakAway](QProcess::CreateProcessArguments* args) {
			args->inheritHandles = false;
			args->startupInfo->dwFlags &= ~static_cast<DWORD>(STARTF_USESTDHANDLES);
			args->startupInfo->hStdInput = nullptr;
			args->startupInfo->hStdOutput = nullptr;
			args->startupInfo->hStdError = nullptr;
			if (breakAway) args->flags |= CREATE_BREAKAWAY_FROM_JOB;
		};
	};
	process->setCreateProcessArgumentsModifier(modifier(true));
	if (process->startDetached()) return true;
	process->setCreateProcessArgumentsModifier(modifier(false));
#endif
	return process->startDetached();
}

#ifdef _WIN32
namespace {

HANDLE shellJob() {
	static HANDLE job = [] {
		auto* created = CreateJobObjectW(nullptr, nullptr);
		if (created != nullptr) {
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION info {};
			info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
			SetInformationJobObject(created, JobObjectExtendedLimitInformation, &info, sizeof(info));
		}
		return created;
	}();
	return job;
}

// Stops every process descended from `pid` (by parent links). sh.exe (MSYS) starts its programs with
// CREATE_BREAKAWAY_FROM_JOB, so they leave the shell's job: when DMS stopped "sh -c cava ..." only sh died and cava
// kept running, one more each time (2026-11-15: six of them). Windows keeps a dead parent's id in its children, so
// they are still found.
// when a process was created (100 ns ticks), 0 when it cannot be read
quint64 processCreated(DWORD pid) {
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (h == nullptr) return 0;
	FILETIME created {}, exited {}, kernel {}, user {};
	quint64 value = 0;
	if (GetProcessTimes(h, &created, &exited, &kernel, &user)) {
		value = (static_cast<quint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
	}
	CloseHandle(h);
	return value;
}

// A child is only one if it was created after its parent. Windows reuses process ids: a long-running app whose
// own parent exited long ago (Discord, Aether's shell itself - its launcher exits right away) keeps that old id as
// its "parent", and when the id came back as one of the shell's short helpers (cmd.exe...), stopping that helper
// terminated the app - silently, exit code 0 (2026-11-15).
void stopDescendants(DWORD pid, quint64 created) {
	if (pid == 0 || created == 0) return;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return;
	std::vector<PROCESSENTRY32W> all;
	PROCESSENTRY32W e {};
	e.dwSize = sizeof(e);
	if (Process32FirstW(snap, &e)) {
		do { all.push_back(e); } while (Process32NextW(snap, &e));
	}
	CloseHandle(snap);
	std::vector<std::pair<DWORD, quint64>> parents {{pid, created}};
	for (size_t i = 0; i < parents.size(); i++) {
		auto [parent, parentCreated] = parents[i];
		for (const auto& p: all) {
			if (p.th32ParentProcessID != parent || p.th32ProcessID == pid) continue;
			auto childCreated = processCreated(p.th32ProcessID);
			if (childCreated == 0 || childCreated < parentCreated) continue; // older: only a reused id
			parents.emplace_back(p.th32ProcessID, childCreated);
			if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, p.th32ProcessID)) {
				TerminateProcess(h, 0);
				CloseHandle(h);
			}
		}
	}
}

struct ExtendedStartup {
	STARTUPINFOEXW info {};
	std::vector<char> attributes;
	HANDLE job = nullptr;
	bool initialized = false;
};

} // namespace
#endif

// At shell start: visualiser processes (cava) whose parent no longer exists were left by an earlier shell
void stopOrphanedHelpers() {
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return;
	std::vector<PROCESSENTRY32W> all;
	PROCESSENTRY32W e {};
	e.dwSize = sizeof(e);
	if (Process32FirstW(snap, &e)) {
		do { all.push_back(e); } while (Process32NextW(snap, &e));
	}
	CloseHandle(snap);
	for (const auto& p: all) {
		if (_wcsicmp(p.szExeFile, L"cava.exe") != 0) continue;
		bool parentAlive = false;
		for (const auto& q: all) {
			if (q.th32ProcessID == p.th32ParentProcessID) { parentAlive = true; break; }
		}
		if (parentAlive) continue;
		if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, p.th32ProcessID)) {
			TerminateProcess(h, 0);
			CloseHandle(h);
		}
	}
}

void bindToShellLifetime(QProcess* process) {
#ifdef _WIN32
	// what it started goes when it goes (also what escaped the job through sh.exe)
	QObject::connect(process, &QProcess::started, process, [process]() {
		const auto pid = static_cast<DWORD>(process->processId());
		process->setProperty("qsPid", QVariant::fromValue(static_cast<qulonglong>(pid)));
		process->setProperty("qsCreated", QVariant::fromValue(static_cast<qulonglong>(processCreated(pid))));
	});
	QObject::connect(process, &QProcess::stateChanged, process, [process](QProcess::ProcessState state) {
		if (state != QProcess::NotRunning) return;
		const auto pid = static_cast<DWORD>(process->property("qsPid").toULongLong());
		const auto created = process->property("qsCreated").toULongLong();
		if (pid != 0) stopDescendants(pid, created);
		process->setProperty("qsPid", QVariant());
		process->setProperty("qsCreated", QVariant());
	});
	process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
		auto* job = shellJob();
		if (job == nullptr) return;
		// set at creation (PROC_THREAD_ATTRIBUTE_JOB_LIST), so children it starts straight away are in the job too
		thread_local ExtendedStartup ex;
		if (ex.initialized) {
			DeleteProcThreadAttributeList(reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(ex.attributes.data()));
			ex.initialized = false;
		}
		SIZE_T size = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
		ex.attributes.assign(size, 0);
		auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(ex.attributes.data());
		if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) return;
		ex.initialized = true;
		ex.job = job;
		if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &ex.job, sizeof(HANDLE), nullptr, nullptr)) return;
		ex.info.StartupInfo = *args->startupInfo;
		ex.info.StartupInfo.cb = sizeof(STARTUPINFOEXW);
		ex.info.lpAttributeList = list;
		args->startupInfo = &ex.info.StartupInfo;
		args->flags |= EXTENDED_STARTUPINFO_PRESENT;
	});
#else
	Q_UNUSED(process);
#endif
}

namespace {

struct CommandHandler {
	QPointer<QJSEngine> engine;
	QJSValue fn;
	QStringList programs;
	bool busy = false;
};

CommandHandler& commandHandler() {
	static CommandHandler handler;
	return handler;
}

QString programName(const QString& path) {
	auto name = path.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1).toLower();
	if (name.endsWith(QLatin1String(".exe"))) name.chop(4);
	return name;
}

bool wantsCommand(const CommandHandler& h, const QStringList& command) {
	const auto program = programName(command.first());
	if (h.programs.contains(program)) return true;
	// `env VAR=x prog ...`
	if (program == QLatin1String("env")) {
		for (const auto& arg: command.sliced(1)) {
			if (arg.contains(QLatin1Char('='))) continue;
			return h.programs.contains(programName(arg));
		}
		return false;
	}
	// `sh -c "echo x | wl-copy"`: the script mentions one of them
	if ((program == QLatin1String("sh") || program == QLatin1String("bash")) && command.size() >= 3) {
		const auto& script = command.last();
		for (const auto& p: h.programs) {
			if (script.contains(p)) return true;
		}
	}
	return false;
}

} // namespace

void setCommandHandler(QJSEngine* engine, const QStringList& programs, const QJSValue& handler) {
	auto& h = commandHandler();
	h.engine = engine;
	h.fn = handler;
	h.programs.clear();
	for (const auto& p: programs) h.programs.append(p.toLower());
}

HandledCommand handleCommand(const QStringList& command) {
	auto& h = commandHandler();
	auto result = HandledCommand();
	if (command.isEmpty() || h.busy || h.engine.isNull() || !h.fn.isCallable()) return result;
	if (!wantsCommand(h, command)) return result;

	h.busy = true; // a handler that runs commands itself is not asked about those
	auto ret = h.fn.call({h.engine->toScriptValue(QVariant(command))});
	h.busy = false;

	if (ret.isError()) {
		qWarning() << "Command handler failed for" << command << ret.toString();
		return result;
	}
	if (ret.isString()) {
		result.handled = true;
		result.output = ret.toString();
	} else if (ret.isBool()) {
		result.handled = ret.toBool();
	} else if (ret.isObject() && !ret.isNull()) {
		result.handled = true;
		result.output = ret.property(QStringLiteral("output")).toString();
		if (ret.property(QStringLiteral("output")).isUndefined()) result.output.clear();
		auto code = ret.property(QStringLiteral("code"));
		if (code.isNumber()) result.code = code.toInt();
	}
	return result;
}

QPair<QString, QString> handledCommandProcess(const HandledCommand& result) {
	const auto cmd = QStringLiteral("cmd.exe");
	auto exit = QStringLiteral("exit %1").arg(result.code);
	if (result.output.isEmpty()) return {cmd, QStringLiteral("/d /c ") + exit};

	// the output goes through a temporary file (any text, any size, UTF-8), printed with `type`
	static int counter = 0;
	auto path = QDir::toNativeSeparators(QDir::temp().filePath(
	    QStringLiteral("qs-out-%1-%2.txt").arg(QCoreApplication::applicationPid()).arg(++counter)
	));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {cmd, QStringLiteral("/d /c ") + exit};
	file.write(result.output.toUtf8());
	file.close();
	return {cmd, QStringLiteral("/d /c type \"%1\" & del \"%1\" & %2").arg(path, exit)};
}

} // namespace qs::io::process

#include <qfileinfo.h>
#include <qregularexpression.h>
#include <qstandardpaths.h>

namespace qs::io::process {

QString fixLinuxCommand(QString& cmd, QStringList& args) {
#ifdef _WIN32
	// Linux configs turn "file:///C:/x" into "/C:/x" by cutting "file://"; Windows wants "C:/x"
	static const QRegularExpression inText(QStringLiteral("(^|[\\s'\"=(])/([A-Za-z]):([/\\\\])"));
	auto fix = [](QString& a) {
		if (a.size() > 3 && a[0] == QLatin1Char('/') && a[1].isLetter() && a[2] == QLatin1Char(':')
		    && (a[3] == QLatin1Char('/') || a[3] == QLatin1Char('\\')))
			a = a.sliced(1);
		else if (a.contains(QStringLiteral(":/"))) a.replace(inText, QStringLiteral("\\1\\2:\\3"));
	};
	fix(cmd);
	for (auto& a: args) fix(a);

	if (cmd.contains(QLatin1Char('\\')) || (cmd.size() > 1 && cmd[1] == QLatin1Char(':'))) return {};
	auto name = cmd.section(QLatin1Char('/'), -1);
	auto onPath = QStandardPaths::findExecutable(name);
	// Windows has its own, different programs by these names
	static const QStringList clashes = {QStringLiteral("find"), QStringLiteral("sort"), QStringLiteral("timeout")};
	if (clashes.contains(name)) onPath.clear();
	if (!onPath.isEmpty() && !cmd.startsWith(QLatin1Char('/'))) return {};
	if (!onPath.isEmpty()) {
		cmd = onPath; // "/usr/bin/foo" with foo on PATH
		return {};
	}

	static const QString gitBin = [] {
		auto git = QStandardPaths::findExecutable(QStringLiteral("git"));
		QStringList dirs;
		if (!git.isEmpty()) dirs << QFileInfo(git).dir().filePath(QStringLiteral("../usr/bin"));
		dirs << QStringLiteral("C:/Program Files/Git/usr/bin");
		for (const auto& d: dirs)
			if (QFileInfo::exists(d + QStringLiteral("/sh.exe"))) return QDir(d).absolutePath();
		return QString();
	}();
	auto exe = gitBin + QLatin1Char('/') + name + QStringLiteral(".exe");
	if (gitBin.isEmpty() || !QFileInfo::exists(exe)) return {};
	cmd = exe;
	return gitBin;
#else
	Q_UNUSED(cmd);
	Q_UNUSED(args);
	return {};
#endif
}

void prependPath(QProcess* process, const QString& dir) {
	if (dir.isEmpty()) return;
	auto env = process->processEnvironment();
	if (env.isEmpty()) env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("PATH"), QDir::toNativeSeparators(dir) + QLatin1Char(';') + env.value(QStringLiteral("PATH")));
	process->setProcessEnvironment(env);
}

} // namespace qs::io::process
