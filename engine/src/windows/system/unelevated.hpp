#pragma once

// Starting programs as a normal (non-administrator) app while the shell itself runs as administrator.
//
// The shell may run elevated so it can move, tile and focus administrator windows (Windows' UIPI stops a normal
// process from touching them). Programs the user launches from it - binds, the launcher, exec, execDetached - must
// not inherit that: they are started with the desktop shell's (explorer.exe's) token instead, which is the user's
// normal token. When the shell is not elevated nothing changes.

#include <optional>

#include <qprocess.h>
#include <qstring.h>
#include <qstringlist.h>

#include <windows.h>

namespace qs::windows::unelevated {

// true when this process has an elevated (administrator) token
bool shellIsElevated();

// Starts `program args` with explorer's token. Returns true when it started; false when the shell is not elevated
// or this failed (the caller then starts it the normal way). `flags` are CreateProcess flags
// (CREATE_NO_WINDOW, CREATE_NEW_CONSOLE ...); `environment` replaces the user's environment when given.
bool spawn(
    const QString& program,
    const QStringList& args,
    const QString& workingDirectory,
    DWORD flags,
    const std::optional<QProcessEnvironment>& environment = std::nullopt
);

// Same for a ready-made command line ("C:\\x\\app.exe" --flag).
bool spawnCommandLine(const QString& commandLine, const QString& workingDirectory, DWORD flags);

// Windows' quoting rules for one argument of a command line
QString quoteArgument(const QString& argument);

} // namespace qs::windows::unelevated
