#pragma once

#include <qbytearray.h>
#include <qcontainerfwd.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qset.h>
#include <qjsengine.h>
#include <qloggingcategory.h>
#include <qvector.h>

#include "logcat.hpp"

QS_DECLARE_LOGGING_CATEGORY(logQmlScanner);

// expects canonical paths
class QmlScanner {
public:
	QmlScanner() = default;
	QmlScanner(const QDir& rootPath): rootPath(rootPath) {}

	void scanDir(const QDir& dir);
	void scanQmlRoot(const QString& path);
	void loadCache(const QString& path);
	void saveCache(const QString& path) const;

	QSet<QString> scannedDirs;
	QVector<QString> scannedFiles;
	QHash<QString, QByteArray> fileHashes;
	QHash<QString, QString> fileIntercepts;

	struct ScanError {
		QString file;
		QString message;
		int line;
	};

	QVector<ScanError> scanErrors;

	bool deferPreprocessing = false;
	bool preprocessingDeferred = false;

	bool readAndHashFile(const QString& path, QByteArray& data);
	[[nodiscard]] bool hasFileContentChanged(const QString& path) const;

private:
	struct CachedFile {
		qint64 size = -1;
		qint64 modified = 0;
		QByteArray hash;
		bool singleton = false;
		bool internal = false;
		QVector<QString> imports;
	};

	struct ImportTarget {
		bool exists = false;
		bool isDir = false;
		QString absolutePath;
	};

	QDir rootPath;
	QSet<QString> seenDirPaths;
	QSet<QString> scannedFileSet;
	QHash<QString, ImportTarget> importTargets;
	QHash<QString, CachedFile> cachedFiles;
	QHash<QString, CachedFile> freshFiles;
	QSet<QString> plainDirs;
	bool cacheChanged = false;

	QString canonicalDirPath(const QDir& dir, const QString& absolutePath);

	bool scanQmlFile(const QString& path, const QFileInfo& info, bool& singleton, bool& internal);
	void scanImports(const QString& path, const QVector<QString>& imports);
	bool scanQmlJson(const QString& path);
	[[nodiscard]] static QPair<QString, QString> jsonToQml(const QJsonValue& value, int indent = 0);

	static QJSEngine* preprocEngine();
};
