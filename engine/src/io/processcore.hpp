#pragma once

#include <utility>

#include <qcontainerfwd.h>
#include <qhash.h>
#include <qjsengine.h>
#include <qjsvalue.h>
#include <qlist.h>
#include <qpair.h>
#include <qstringlist.h>
#include <qprocess.h>
#include <qqmlintegration.h>
#include <qvariant.h>

namespace qs::io::process {

class ProcessContext {
	Q_PROPERTY(QList<QString> command MEMBER command WRITE setCommand);
	Q_PROPERTY(QVariantHash environment MEMBER environment WRITE setEnvironment);
	Q_PROPERTY(bool clearEnvironment MEMBER clearEnvironment WRITE setClearEnvironment);
	Q_PROPERTY(QString workingDirectory MEMBER workingDirectory WRITE setWorkingDirectory);
	Q_PROPERTY(bool unbindStdout MEMBER unbindStdout WRITE setUnbindStdout);
	Q_GADGET;
	QML_STRUCTURED_VALUE;
	QML_VALUE_TYPE(processContext);

public:
	ProcessContext() = default;
	// Making this a Q_INVOKABLE does not work with QML_STRUCTURED_VALUe in Qt 6.9.
	explicit ProcessContext(QList<QString> command): command(std::move(command)), commandSet(true) {}

	void setCommand(QList<QString> command) {
		this->command = std::move(command);
		this->commandSet = true;
	}

	void setEnvironment(QHash<QString, QVariant> environment) {
		this->environment = std::move(environment);
		this->environmentSet = true;
	}

	void setClearEnvironment(bool clearEnvironment) {
		this->clearEnvironment = clearEnvironment;
		this->clearEnvironmentSet = true;
	}

	void setWorkingDirectory(QString workingDirectory) {
		this->workingDirectory = std::move(workingDirectory);
		this->workingDirectorySet = true;
	}

	void setUnbindStdout(bool unbindStdout) { this->unbindStdout = unbindStdout; }

	QList<QString> command;
	QHash<QString, QVariant> environment;
	bool clearEnvironment = false;
	QString workingDirectory;

	bool commandSet : 1 = false;
	bool environmentSet : 1 = false;
	bool clearEnvironmentSet : 1 = false;
	bool workingDirectorySet : 1 = false;
	bool unbindStdout : 1 = true;
};

void setupProcessEnvironment(
    QProcess* process,
    bool clear,
    const QHash<QString, QVariant>& envChanges
);

void disownHandles(QProcess* process);
// starts an app outside the shell's job (it outlives the shell), handles disowned
bool startDetachedFree(QProcess* process);

/// Linux commands on Windows: "/C:/x" arguments become "C:/x", and a tool Windows lacks (test, mkdir -p,
/// sh, Linux find...) runs from Git for Windows' usr/bin when Git is installed.
/// Returns the Git tools folder when the command was sent there (empty otherwise): put it on the child's PATH
/// with prependPath so a script it runs (sh -c "mkdir ... && touch ...") finds the other tools too.
QString fixLinuxCommand(QString& cmd, QStringList& args);
void prependPath(QProcess* process, const QString& dir);

// Windows: the process (and everything it starts) ends with the shell - a helper such as cava under `sh -c` was
// left running after every shell restart. Only for Process children, never for execDetached / startDetached.
void bindToShellLifetime(QProcess* process);
// at shell start: cava processes an earlier (killed) shell left behind
void stopOrphanedHelpers();

// Windows: Linux programs the shells run (wl-copy, xdg-open, wpctl, notify-send...) do not exist here. The
// config can answer them itself: Quickshell.setCommandHandler(programs, fn). For a command whose program (or
// an `sh -c` / `bash -c` script mentioning it) is in `programs`, fn(command) is called first and returns
// false (not handled: run it as usual), true (done), a string (done; that is its output) or
// { output, code } - a Process then gets that output and exit code as if the program had run.
struct HandledCommand {
	bool handled = false;
	QString output;
	int code = 0;
};

void setCommandHandler(QJSEngine* engine, const QStringList& programs, const QJSValue& handler);
HandledCommand handleCommand(const QStringList& command);
// the stand-in a Process runs for a handled command (program + native Windows arguments): prints `output`,
// exits with `code`
QPair<QString, QString> handledCommandProcess(const HandledCommand& result);

} // namespace qs::io::process
