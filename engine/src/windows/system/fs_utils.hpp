#pragma once

#include <qvariant.h>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtmetamacros.h>

namespace qs::windows::sys {

class FsUtils: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit FsUtils(QObject* parent = nullptr): QObject(parent) {}

	Q_INVOKABLE static QString classify(const QString& path);

	Q_INVOKABLE static bool isAccessibleDir(const QString& path);

	Q_INVOKABLE static QStringList listDir(const QString& path);

	/// Like `find <roots> -mindepth N -maxdepth M -name <filter>...`: absolute paths (forward slashes) of the
	/// files (or, with dirs, the folders) under roots whose names match one of nameFilters ("*.json"; empty =
	/// all), between minDepth and maxDepth levels below each root (1 = directly inside it). Follows links.
	Q_INVOKABLE static QStringList findFiles(
	    const QStringList& roots,
	    const QStringList& nameFilters,
	    int minDepth = 1,
	    int maxDepth = 16,
	    bool dirs = false
	);

	Q_INVOKABLE static QString findExecutable(const QString& name);

	Q_INVOKABLE static bool makePath(const QString& path);

	Q_INVOKABLE static bool removeFile(const QString& path);

	Q_INVOKABLE static QString canonicalPath(const QString& path);

	/// Entries of a folder for file browsers: {name, path, isDir, isLink, hidden, size, mtimeMs, mime,
	/// thumbnailable}, folders first, by name.
	Q_INVOKABLE static QVariantList listDetailed(const QString& path, bool includeHidden);
	/// The same map for one path (empty when it does not exist).
	Q_INVOKABLE static QVariantMap statPath(const QString& path);
	/// Renames a file or folder inside its folder.
	Q_INVOKABLE static bool renamePath(const QString& path, const QString& name);
	/// Moves files / folders to the Recycle Bin (recoverable), never deletes them for good.
	Q_INVOKABLE static bool recycle(const QStringList& paths);
};

} // namespace qs::windows::sys
