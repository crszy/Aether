#include "scan.hpp"
#include <cmath>
#include <functional>
#include <utility>

#include <qcontainerfwd.h>
#include <qcryptographichash.h>
#include <qdatastream.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qjsengine.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qjsonvalue.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qpair.h>
#include <qsavefile.h>
#include <qstring.h>
#include <qtenvironmentvariables.h>
#include <qtextstream.h>
#include <qtimezone.h>

#include "logcat.hpp"
#include "scanenv.hpp"

QS_LOGGING_CATEGORY(logQmlScanner, "quickshell.qmlscanner", QtWarningMsg);

namespace {

constexpr quint32 SCAN_CACHE_MAGIC = 0x71736331;
constexpr quint32 SCAN_CACHE_VERSION = 1;

bool scanCacheDisabled() {
	static const bool disabled = qEnvironmentVariableIsSet("QS_DISABLE_SCAN_CACHE");
	return disabled;
}

} // namespace

bool QmlScanner::readAndHashFile(const QString& path, QByteArray& data) {
	auto file = QFile(path);
	if (!file.open(QFile::ReadOnly)) return false;
	data = file.readAll();
	this->fileHashes.insert(path, QCryptographicHash::hash(data, QCryptographicHash::Md5));
	return true;
}

bool QmlScanner::hasFileContentChanged(const QString& path) const {
	auto it = this->fileHashes.constFind(path);
	if (it == this->fileHashes.constEnd()) return true;

	auto file = QFile(path);
	if (!file.open(QFile::ReadOnly)) return true;

	auto newHash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Md5);
	return newHash != it.value();
}

QString QmlScanner::canonicalDirPath(const QDir& dir, const QString& absolutePath) {
#ifdef _WIN32
	qsizetype pos = -1;
	if (absolutePath.startsWith(u"//")) {
		pos = absolutePath.indexOf(u'/', 2);
	} else if (absolutePath.size() >= 3 && absolutePath.at(1) == u':' && absolutePath.at(2) == u'/') {
		pos = 2;
	}

	if (pos == -1) return dir.canonicalPath();

	do {
		pos = absolutePath.indexOf(u'/', pos + 1);
		auto prefix = pos == -1 ? absolutePath : absolutePath.first(pos);
		if (this->plainDirs.contains(prefix)) continue;

		auto info = QFileInfo(prefix);
		if (info.isSymLink()) return dir.canonicalPath();
		if (!info.exists()) return QString();
		this->plainDirs.insert(prefix);
	} while (pos != -1);

	auto path = absolutePath;
	if (path.at(0) != u'/') path[0] = path.at(0).toUpper();
	return path;
#else
	Q_UNUSED(absolutePath);
	return dir.canonicalPath();
#endif
}

void QmlScanner::scanDir(const QDir& dir) {
	if (this->preprocessingDeferred) return;

	auto absolutePath = QDir::cleanPath(dir.absolutePath());
	if (this->seenDirPaths.contains(absolutePath)) return;
	this->seenDirPaths.insert(absolutePath);

	auto dirKey = this->canonicalDirPath(dir, absolutePath);
	if (dirKey.isEmpty()) dirKey = absolutePath;
	if (this->scannedDirs.contains(dirKey)) return;
	this->scannedDirs.insert(dirKey);

	const auto& path = dir.path();

	qCDebug(logQmlScanner) << "Scanning directory" << path;

	struct Entry {
		QString name;
		bool singleton = false;
		bool internal = false;
	};

	bool seenQmldir = false;
	auto entries = QVector<Entry>();

	for (const auto& info: dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
		auto name = info.fileName();
		if (name == "qmldir") {
			qCDebug(
			    logQmlScanner
			) << "Found qmldir file, qmldir synthesization will be disabled for directory"
			  << path;
			seenQmldir = true;
		} else if (name.at(0).isUpper() && name.endsWith(".qml")) {
			auto& entry = entries.emplaceBack();

			if (this->scanQmlFile(dir.filePath(name), info, entry.singleton, entry.internal)) {
				entry.name = name;
			} else {
				entries.pop_back();
			}
		} else if (name.at(0).isUpper() && name.endsWith(".qml.json")) {
			if (this->scanQmlJson(dir.filePath(name))) {
				entries.push_back({
				    .name = name.first(name.length() - 5),
				    .singleton = true,
				});
			}
		}
	}

	if (!seenQmldir) {
		qCDebug(logQmlScanner) << "Synthesizing qmldir for directory" << path;

		QString qmldir;
		auto stream = QTextStream(&qmldir);

		// can't derive a module name if not in shell path
		if (path.startsWith(this->rootPath.path())) {
			auto end = path.sliced(this->rootPath.path().length());

			// verify we have a valid module name.
			for (auto& c: end) {
				if (c == '/') c = '.';
				else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
				         || c == '_')
				{
				} else {
					qCWarning(logQmlScanner) << "Module path contains invalid characters for a module name: "
					                         << path.sliced(this->rootPath.path().length());
					goto skipadd;
				}
			}

			stream << "module qs" << end << '\n';
		skipadd:;
		} else {
			qCWarning(logQmlScanner) << "Module path" << path << "is outside of the config folder.";
		}

		for (const auto& entry: entries) {
			if (entry.internal) stream << "internal ";
			if (entry.singleton) stream << "singleton ";
			stream << entry.name.sliced(0, entry.name.length() - 4) << " 1.0 " << entry.name << '\n';
		}

		qCDebug(logQmlScanner) << "Synthesized qmldir for" << path << qPrintable("\n" + qmldir);
		this->fileIntercepts.insert(QDir(path).filePath("qmldir"), qmldir);
	}
}

bool QmlScanner::scanQmlFile(
    const QString& path,
    const QFileInfo& info,
    bool& singleton,
    bool& internal
) {
	if (this->preprocessingDeferred) return false;
	if (this->scannedFileSet.contains(path)) return false;
	this->scannedFileSet.insert(path);
	this->scannedFiles.push_back(path);

	auto size = info.size();
	auto modified = info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();

	if (auto cached = this->cachedFiles.constFind(path); cached != this->cachedFiles.constEnd()
	                                                   && cached->size == size
	                                                   && cached->modified == modified)
	{
		qCDebug(logQmlScanner) << "Using cached scan of qml file" << path;
		singleton = cached->singleton;
		internal = cached->internal;
		this->fileHashes.insert(path, cached->hash);
		this->freshFiles.insert(path, *cached);
		this->scanImports(path, cached->imports);
		return true;
	}

	qCDebug(logQmlScanner) << "Scanning qml file" << path;
	this->cacheChanged = true;

	QByteArray fileData;
	if (!this->readAndHashFile(path, fileData)) {
		qCWarning(logQmlScanner) << "Failed to open file" << path;
		return false;
	}

	auto mayPreprocess = fileData.contains("//@ if") || fileData.contains("//@ endif");
	if (mayPreprocess && this->deferPreprocessing) {
		this->preprocessingDeferred = true;
		return false;
	}

	auto stream = QTextStream(&fileData);
	auto imports = QVector<QString>();

	bool inHeader = true;
	auto ifScopes = QVector<bool>();
	bool sourceMasked = false;
	int lineNum = 0;
	QString overrideText;
	bool isOverridden = false;
	bool preprocessed = false;

	auto postError = [&, this](QString error) {
		this->scanErrors.append({.file = path, .message = std::move(error), .line = lineNum});
	};

	while (!stream.atEnd()) {
		if (!inHeader && !mayPreprocess) break;
		++lineNum;
		bool hideMask = false;
		auto rawLine = stream.readLine();
		auto line = rawLine.trimmed();
		if (!sourceMasked && inHeader) {
			if (!singleton && line == "pragma Singleton") {
				singleton = true;
			} else if (line.startsWith("import")) {
				// we don't care about "import qs" as we always load the root folder
				if (auto importCursor = line.indexOf(" qs."); importCursor != -1) {
					importCursor += 4;
					QString path;

					while (importCursor != line.length()) {
						auto c = line.at(importCursor);
						if (c == '.') c = '/';
						else if (c == ' ') break;
						else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
						         || c == '_')
						{
						} else {
							qCWarning(logQmlScanner) << "Import line contains invalid characters: " << line;
							goto next;
						}

						path.append(c);
						importCursor += 1;
					}

					imports.append(this->rootPath.filePath(path));
				} else if (auto startQuot = line.indexOf('"');
				           startQuot != -1 && line.length() >= startQuot + 3)
				{
					auto endQuot = line.indexOf('"', startQuot + 1);
					if (endQuot == -1) continue;

					auto name = line.sliced(startQuot + 1, endQuot - startQuot - 1);
					imports.push_back(name);
				}
			} else if (!internal && line == "//@ pragma Internal") {
				internal = true;
			} else if (line.contains('{')) {
				inHeader = false;
			}
		}

		if (line.startsWith("//@ if ")) {
			preprocessed = true;
			auto code = line.sliced(7);
			auto value = QmlScanner::preprocEngine()->evaluate(code, path, 1234);
			bool mask = true;

			if (value.isError()) {
				postError(QString("Evaluating if: %0").arg(value.toString()));
			} else if (!value.isBool()) {
				postError(QString("If expression \"%0\" is not a boolean").arg(value.toString()));
			} else if (value.toBool()) {
				mask = false;
			}
			if (!sourceMasked && mask) hideMask = true;
			mask = sourceMasked || mask; // can't unmask if a nested if passes
			ifScopes.append(mask);
			if (mask) isOverridden = true;
			sourceMasked = mask;
		} else if (line.startsWith("//@ endif")) {
			preprocessed = true;
			if (ifScopes.isEmpty()) {
				postError("endif without matching if");
			} else {
				ifScopes.pop_back();

				if (ifScopes.isEmpty()) sourceMasked = false;
				else sourceMasked = ifScopes.last();
			}
		}

		if (mayPreprocess) {
			if (!hideMask && sourceMasked) overrideText.append("// MASKED: " % rawLine % '\n');
			else overrideText.append(rawLine % '\n');
		}

	next:;
	}

	if (!ifScopes.isEmpty()) {
		postError("unclosed preprocessor if block");
	}

	if (isOverridden) {
		this->fileIntercepts.insert(path, overrideText);
	}

	if (logQmlScanner().isDebugEnabled() && !imports.isEmpty()) {
		qCDebug(logQmlScanner) << "Found imports" << imports;
	}

	if (!preprocessed) {
		this->freshFiles.insert(
		    path,
		    {.size = size,
		     .modified = modified,
		     .hash = this->fileHashes.value(path),
		     .singleton = singleton,
		     .internal = internal,
		     .imports = imports}
		);
	}

	this->scanImports(path, imports);
	return true;
}

void QmlScanner::scanImports(const QString& path, const QVector<QString>& imports) {
	auto currentdir = QDir(QFileInfo(path).absolutePath());

	// the root can never be a singleton so it dosent matter if we skip it
	this->scanDir(currentdir);

	for (const auto& import: imports) {
		QString ipath;
		if (import.startsWith("root:")) {
			auto path = import.sliced(5);
			if (path.startsWith('/')) path = path.sliced(1);
			ipath = this->rootPath.filePath(path);
		} else {
			ipath = currentdir.filePath(import);
		}

		auto target = this->importTargets.constFind(ipath);
		if (target == this->importTargets.constEnd()) {
			auto pathInfo = QFileInfo(ipath);
			target = this->importTargets.insert(
			    ipath,
			    {.exists = pathInfo.exists(),
			     .isDir = pathInfo.isDir(),
			     .absolutePath = pathInfo.absoluteFilePath()}
			);
		}

		const auto& cpath = target->absolutePath;

		if (!target->exists) {
			qCWarning(logQmlScanner) << "Ignoring unresolvable import" << ipath << "from" << path;
			continue;
		}

		if (!target->isDir) {
			qCDebug(logQmlScanner) << "Ignoring non-directory import" << ipath << "from" << path;
			continue;
		}

		if (import.endsWith(".js")) {
			this->scannedFiles.push_back(cpath);
			QByteArray jsData;
			this->readAndHashFile(cpath, jsData);
		} else this->scanDir(cpath);
	}
}

void QmlScanner::scanQmlRoot(const QString& path) {
	bool singleton = false;
	bool internal = false;
	this->scanQmlFile(path, QFileInfo(path), singleton, internal);

	// Folders only reached by file path (Loader { source: "Modals/X/Page.qml" }) are not imported anywhere,
	// so the walk above never builds their type list and pages there can't use their neighbours.
	// Walk the config folder too (headers only, cached) so every folder of .qml files gets one.
	std::function<void(const QDir&, int)> walk = [&](const QDir& dir, int depth) {
		if (depth > 8) return;
		for (const auto& info: dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
			auto name = info.fileName();
			if (name.startsWith('.') || name == "node_modules" || name == "build") continue;
			auto sub = QDir(info.filePath());
			if (!sub.entryList({"*.qml"}, QDir::Files).isEmpty()) this->scanDir(sub);
			walk(sub, depth + 1);
		}
	};
	walk(this->rootPath, 0);
}

void QmlScanner::loadCache(const QString& path) {
	if (scanCacheDisabled() || path.isEmpty()) return;

	auto file = QFile(path);
	if (!file.open(QFile::ReadOnly)) return;

	auto stream = QDataStream(&file);
	stream.setVersion(QDataStream::Qt_6_0);

	quint32 magic = 0;
	quint32 version = 0;
	qint32 count = 0;
	stream >> magic >> version >> count;
	if (magic != SCAN_CACHE_MAGIC || version != SCAN_CACHE_VERSION || count < 0) return;

	auto files = QHash<QString, CachedFile>();
	files.reserve(count);

	for (qint32 i = 0; i != count && stream.status() == QDataStream::Ok; i++) {
		QString filePath;
		CachedFile cached;
		stream >> filePath >> cached.size >> cached.modified >> cached.hash >> cached.singleton
		    >> cached.internal >> cached.imports;
		files.insert(filePath, cached);
	}

	if (stream.status() != QDataStream::Ok) {
		qCDebug(logQmlScanner) << "Ignoring unreadable scan cache" << path;
		return;
	}

	qCDebug(logQmlScanner) << "Loaded scan cache with" << files.size() << "files from" << path;
	this->cachedFiles = std::move(files);
}

void QmlScanner::saveCache(const QString& path) const {
	if (scanCacheDisabled() || path.isEmpty()) return;
	if (!this->cacheChanged && this->freshFiles.size() == this->cachedFiles.size()) return;

	auto file = QSaveFile(path);
	if (!file.open(QFile::WriteOnly)) {
		qCDebug(logQmlScanner) << "Could not write scan cache" << path << file.errorString();
		return;
	}

	auto stream = QDataStream(&file);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << SCAN_CACHE_MAGIC << SCAN_CACHE_VERSION << static_cast<qint32>(this->freshFiles.size());

	for (auto it = this->freshFiles.constBegin(); it != this->freshFiles.constEnd(); ++it) {
		const auto& cached = it.value();
		stream << it.key() << cached.size << cached.modified << cached.hash << cached.singleton
		       << cached.internal << cached.imports;
	}

	if (!file.commit()) {
		qCDebug(logQmlScanner) << "Could not write scan cache" << path << file.errorString();
	}
}

bool QmlScanner::scanQmlJson(const QString& path) {
	qCDebug(logQmlScanner) << "Scanning qml.json file" << path;

	QByteArray data;
	if (!this->readAndHashFile(path, data)) {
		qCWarning(logQmlScanner) << "Failed to open file" << path;
		return false;
	}

	// Importing this makes CI builds fail for some reason.
	QJsonParseError error; // NOLINT (misc-include-cleaner)
	auto json = QJsonDocument::fromJson(data, &error);

	if (error.error != QJsonParseError::NoError) {
		qCCritical(logQmlScanner).nospace()
		    << "Failed to parse qml.json file at " << path << ": " << error.errorString();
		return false;
	}

	const QString body =
	    "pragma Singleton\nimport QtQuick as Q\n\n" % QmlScanner::jsonToQml(json.object()).second;

	qCDebug(logQmlScanner) << "Synthesized qml file for" << path << qPrintable("\n" + body);

	this->fileIntercepts.insert(path.first(path.length() - 5), body);
	this->scannedFiles.push_back(path);
	return true;
}

QPair<QString, QString> QmlScanner::jsonToQml(const QJsonValue& value, int indent) {
	if (value.isObject()) {
		const auto& object = value.toObject();

		auto valIter = object.constBegin();

		QString accum = "Q.QtObject {\n";
		for (const auto& key: object.keys()) {
			const auto& val = *valIter++;
			auto [type, repr] = QmlScanner::jsonToQml(val, indent + 2);
			accum += QString(' ').repeated(indent + 2) % "readonly property " % type % ' ' % key % ": "
			       % repr % ";\n";
		}

		accum += QString(' ').repeated(indent) % '}';
		return qMakePair(QStringLiteral("Q.QtObject"), accum);
	} else if (value.isArray()) {
		return qMakePair(
		    QStringLiteral("var"),
		    QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
		);
	} else if (value.isString()) {
		const auto& str = value.toString();

		if (str.startsWith('#') && (str.length() == 4 || str.length() == 7 || str.length() == 9)) {
			for (auto c: str.sliced(1)) {
				if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
					goto noncolor;
				}
			}

			return qMakePair(QStringLiteral("Q.color"), '"' % str % '"');
		}

	noncolor:
		return qMakePair(QStringLiteral("string"), '"' % QString(str).replace("\"", "\\\"") % '"');
	} else if (value.isDouble()) {
		auto num = value.toDouble();
		double whole = 0;
		if (std::modf(num, &whole) == 0.0) {
			return qMakePair(QStringLiteral("int"), QString::number(static_cast<int>(whole)));
		} else {
			return qMakePair(QStringLiteral("real"), QString::number(num));
		}
	} else if (value.isBool()) {
		return qMakePair(QStringLiteral("bool"), value.toBool() ? "true" : "false");
	} else {
		return qMakePair(QStringLiteral("var"), "null");
	}
}

QJSEngine* QmlScanner::preprocEngine() {
	static auto* engine = [] {
		auto* engine = new QJSEngine();
		engine->globalObject().setPrototype(engine->newQObject(new qs::scan::env::PreprocEnv()));
		return engine;
	}();

	return engine;
}
