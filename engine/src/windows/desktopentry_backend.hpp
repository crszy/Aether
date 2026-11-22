#pragma once

#include <qhash.h>
#include <qlist.h>
#include <qmutex.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qvector.h>

#include "../core/desktopentry.hpp"

namespace qs::windows {

class WindowsDesktopEntryBackend: public DesktopEntryBackend {
public:
	QList<ParsedDesktopEntryData> scan() override;
	QStringList watchPaths() override;
	void execute(const QVector<QString>& command, const QString& workingDirectory) override;
	QString aliasFor(const QString& name) override;

	static void install();

	static QString parsingNameForId(const QString& id);
	static QString idForParsingName(const QString& token);
	static QString linkTargetForId(const QString& id);
	static void launch(const QString& token, const QString& workingDirectory = QString());

	static void noteWindowExe(const QString& appId, const QString& exePath);
	static QString exeForAppId(const QString& appId);

private:
	static QMutex sRegistryMutex;
	static QHash<QString, QString> sRegistry;
	static QHash<QString, QString> sTargets;
	static QHash<QString, QString> sLinkTargets;
	static QHash<QString, QString> sExeNames;
	static QMutex sWindowExeMutex;
	static QHash<QString, QString> sWindowExes;
};

} // namespace qs::windows
