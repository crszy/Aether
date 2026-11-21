#include "main.hpp"
#include <cerrno>

#include <qcoreapplication.h>
#include <qdatastream.h>
#include <qdatetime.h>
#include <qdebug.h>
#include <qlogging.h>
#include <qtenvironmentvariables.h>

#ifdef QS_WINDOWS_GUI_EXE
#include <cstdio>
#include <windows.h>
#endif

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include "../core/instanceinfo.hpp"
#include "../core/logging.hpp"
#include "../core/paths.hpp"
#include "build.hpp"
#include "launch_p.hpp"

#if CRASH_HANDLER && !defined(_WIN32)
#include "../crash/main.hpp"
#endif

namespace qs::launch {

namespace {

void checkCrashRelaunch(char** argv) {
#if CRASH_HANDLER && defined(_WIN32)
	Q_UNUSED(argv);

	if (qEnvironmentVariableIsSet("__QUICKSHELL_CRASH_RELAUNCH")) {
		auto launchTimeMs = qEnvironmentVariable("__QUICKSHELL_CRASH_LAUNCH_TIME").toLongLong();
		auto nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();

		qCritical().nospace() << "Quickshell has crashed ("
		                      << qEnvironmentVariable("__QUICKSHELL_CRASH_REASON") << "). Dump saved to "
		                      << qEnvironmentVariable("__QUICKSHELL_CRASH_DUMP_PATH");

		qunsetenv("__QUICKSHELL_CRASH_RELAUNCH");
		qunsetenv("__QUICKSHELL_CRASH_LAUNCH_TIME");
		qunsetenv("__QUICKSHELL_CRASH_REASON");
		qunsetenv("__QUICKSHELL_CRASH_DUMP_PATH");

		if (launchTimeMs != 0 && nowMs - launchTimeMs < 10000) {
			qCritical() << "Quickshell crashed within 10 seconds of launching. Not restarting to avoid "
			               "a crash loop.";
			exit(-1); // NOLINT
		} else {
			qCritical() << "Quickshell has been restarted.";
		}
	}
#elif CRASH_HANDLER
	auto lastInfoFdStr = qEnvironmentVariable("__QUICKSHELL_CRASH_INFO_FD");
	auto dumpPid = qEnvironmentVariable("__QUICKSHELL_CRASH_DUMP_PID").toInt();

	if (!lastInfoFdStr.isEmpty()) {
		auto lastInfoFd = lastInfoFdStr.toInt();

		RelaunchInfo info;

		{
			QFile file;
			if (!file.open(lastInfoFd, QFile::ReadOnly, QFile::AutoCloseHandle)) {
				qFatal() << "Failed to open crash info fd. Cannot restart.";
			}

			file.seek(0);

			auto ds = QDataStream(&file);
			ds >> info;
		}

		qunsetenv("__QUICKSHELL_CRASH_INFO_FD");
		qunsetenv("__QUICKSHELL_CRASH_DUMP_PID");
		qunsetenv("__QUICKSHELL_CRASH_SIGNAL");

		LogManager::init(
		    !info.noColor,
		    info.timestamp,
		    info.sparseLogsOnly,
		    info.defaultLogLevel,
		    info.logRules
		);

		qCritical().nospace() << "Quickshell has crashed under pid " << dumpPid
		                      << " (Coredumps will be available under that pid.)";

		qCritical() << "Further crash information is stored under"
		            << QsPaths::crashDir(info.instance.instanceId).path();

		if (info.instance.launchTime.msecsTo(QDateTime::currentDateTime()) < 10000) {
			qCritical() << "Quickshell crashed within 10 seconds of launching. Not restarting to avoid "
			               "a crash loop.";
			exit(-1); // NOLINT
		} else {
			qCritical() << "Quickshell has been restarted.";

			launch({.configPath = info.instance.configPath}, argv);
		}
	}
#endif
}

} // namespace

#ifndef _WIN32
int DAEMON_PIPE = -1; // NOLINT

void exitDaemon(int code) {
	if (DAEMON_PIPE == -1) return;

	if (write(DAEMON_PIPE, &code, sizeof(int)) == -1) {
		qCritical().nospace() << "Failed to write daemon exit command with error code " << errno << ": "
		                      << qt_error_string();
	}

	close(DAEMON_PIPE);

	auto fd = open("/dev/null", O_RDWR);
	if (fd == -1) {
		qCritical().nospace() << "Failed to open /dev/null for daemon stdio" << errno << ": "
		                      << qt_error_string();
		return;
	}

	if (dup2(fd, STDIN_FILENO) != STDIN_FILENO) { // NOLINT
		qCritical().nospace() << "Failed to set daemon stdin to /dev/null" << errno << ": "
		                      << qt_error_string();
	}

	if (dup2(fd, STDOUT_FILENO) != STDOUT_FILENO) { // NOLINT
		qCritical().nospace() << "Failed to set daemon stdout to /dev/null" << errno << ": "
		                      << qt_error_string();
	}

	if (dup2(fd, STDERR_FILENO) != STDERR_FILENO) { // NOLINT
		qCritical().nospace() << "Failed to set daemon stderr to /dev/null" << errno << ": "
		                      << qt_error_string();
	}

	close(fd);
}
#endif

int main(int argc, char** argv) {
#ifdef QS_WINDOWS_GUI_EXE
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		FILE* stream = nullptr;
		freopen_s(&stream, "CONIN$", "r", stdin);
		freopen_s(&stream, "CONOUT$", "w", stdout);
		freopen_s(&stream, "CONOUT$", "w", stderr);
	}
#endif

	QCoreApplication::setApplicationName("quickshell");

#if CRASH_HANDLER && !defined(_WIN32)
	qsCheckCrash(argc, argv);
#endif

	checkCrashRelaunch(argv);
	auto code = runCommand(argc, argv);

	exitDaemon(code);

#ifdef _WIN32
	fflush(nullptr);
	TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
#endif

	return code;
}

} // namespace qs::launch
