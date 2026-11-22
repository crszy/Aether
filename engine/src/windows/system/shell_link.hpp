#pragma once

#include <qstring.h>

namespace qs::windows::sys {

struct ShellLinkData {
	QString targetPath;
	QString arguments;
	QString description;
	QString workingDirectory;
	QString iconPath;
	int iconIndex = 0;
	bool valid = false;
};

bool writeShellLink(const QString& lnkPath, const ShellLinkData& data);
ShellLinkData readShellLink(const QString& lnkPath);

} // namespace qs::windows::sys
