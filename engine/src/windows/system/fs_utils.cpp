#include "fs_utils.hpp"
#include <string>

#include <qt_windows.h>
#include <shellapi.h>

#include <qcoreapplication.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qdatetime.h>
#include <qmimedatabase.h>
#include <qstandardpaths.h>

namespace qs::windows::sys {

QString FsUtils::classify(const QString& path) {
	auto wpath = path.toStdWString();
	auto attrs = GetFileAttributesW(wpath.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES) return QStringLiteral("invalid");
	return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? QStringLiteral("dir") : QStringLiteral("file");
}

bool FsUtils::isAccessibleDir(const QString& path) {
	if (path.isEmpty()) return false;

	auto wpath = path.toStdWString();
	auto attrs = GetFileAttributesW(wpath.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) return false;

	auto pattern = wpath;
	if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') pattern += L'\\';
	pattern += L'*';

	WIN32_FIND_DATAW findData;
	auto handle = FindFirstFileExW(
	    pattern.c_str(),
	    FindExInfoBasic,
	    &findData,
	    FindExSearchNameMatch,
	    nullptr,
	    0
	);

	if (handle == INVALID_HANDLE_VALUE) return false;
	FindClose(handle);
	return true;
}

namespace {
QVariantMap entryMap(const QFileInfo& info) {
	static const QMimeDatabase mimes;
	const auto isDir = info.isDir();
	const auto mime = isDir ? QStringLiteral("inode/directory")
	                        : mimes.mimeTypeForFile(info, QMimeDatabase::MatchExtension).name();
	return {
	    {QStringLiteral("name"), info.fileName()},
	    {QStringLiteral("path"), QDir::fromNativeSeparators(info.absoluteFilePath())},
	    {QStringLiteral("isDir"), isDir},
	    {QStringLiteral("isLink"), info.isSymLink()},
	    {QStringLiteral("hidden"), info.isHidden()},
	    {QStringLiteral("size"), isDir ? 0 : info.size()},
	    {QStringLiteral("mtimeMs"), info.lastModified().toMSecsSinceEpoch()},
	    {QStringLiteral("mime"), mime},
	    {QStringLiteral("thumbnailable"), mime.startsWith(QStringLiteral("image/"))},
	};
}
} // namespace

QVariantList FsUtils::listDetailed(const QString& path, bool includeHidden) {
	QVariantList out;
	auto filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
	if (includeHidden) filters |= QDir::Hidden;
	const auto entries = QDir(path).entryInfoList(filters, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
	out.reserve(entries.size());
	for (const auto& info: entries) out.push_back(entryMap(info));
	return out;
}

QVariantMap FsUtils::statPath(const QString& path) {
	const QFileInfo info(path);
	if (!info.exists()) return {};
	return entryMap(info);
}

bool FsUtils::renamePath(const QString& path, const QString& name) {
	if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) return false;
	const QFileInfo info(path);
	const auto target = QDir::toNativeSeparators(info.dir().filePath(name)).toStdWString();
	const auto source = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
	return MoveFileExW(source.c_str(), target.c_str(), 0) != 0;
}

bool FsUtils::recycle(const QStringList& paths) {
	if (paths.isEmpty()) return false;
	std::wstring list;
	for (const auto& p: paths) {
		list += QDir::toNativeSeparators(QFileInfo(p).absoluteFilePath()).toStdWString();
		list.push_back(L'\0');
	}
	list.push_back(L'\0');
	SHFILEOPSTRUCTW op {};
	op.wFunc = FO_DELETE;
	op.pFrom = list.c_str();
	op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
	return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

QStringList FsUtils::listDir(const QString& path) {
	return QDir(path).entryList(QDir::Files, QDir::Name);
}

namespace {
void findIn(
    const QString& dir,
    const QStringList& filters,
    int depth,
    int minDepth,
    int maxDepth,
    bool dirs,
    QStringList& out
) {
	if (depth > maxDepth || out.size() > 100000) return;
	auto entries = QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
	for (const auto& info: entries) {
		auto isDir = info.isDir();
		if (isDir == dirs && depth >= minDepth
		    && (filters.isEmpty() || QDir::match(filters, info.fileName())))
		{
			out.push_back(QDir::fromNativeSeparators(info.absoluteFilePath()));
		}
		if (isDir) findIn(info.absoluteFilePath(), filters, depth + 1, minDepth, maxDepth, dirs, out);
	}
}
} // namespace

QStringList FsUtils::findFiles(
    const QStringList& roots,
    const QStringList& nameFilters,
    int minDepth,
    int maxDepth,
    bool dirs
) {
	QStringList out;
	for (const auto& root: roots) {
		if (root.isEmpty() || !QFileInfo(root).isDir()) continue;
		findIn(root, nameFilters, 1, minDepth, maxDepth, dirs, out);
	}
	return out;
}

QString FsUtils::findExecutable(const QString& name) {
	if (name.isEmpty()) return QString();

	auto local = QFileInfo(QDir(QCoreApplication::applicationDirPath()), name);
	if (local.isFile()) return local.absoluteFilePath();

	return QStandardPaths::findExecutable(name);
}

bool FsUtils::makePath(const QString& path) {
	if (path.isEmpty()) return false;
	return QDir().mkpath(path);
}

bool FsUtils::removeFile(const QString& path) {
	auto info = QFileInfo(path);
	if (!info.exists()) return true;
	if (!info.isFile()) return false;
	if (!info.isWritable()) QFile::setPermissions(path, info.permissions() | QFileDevice::WriteOwner | QFileDevice::WriteUser);
	return QFile::remove(path);
}

QString FsUtils::canonicalPath(const QString& path) {
	if (path.isEmpty()) return QString();
	auto native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
	auto wide = native.toStdWString();
	auto size = GetLongPathNameW(wide.c_str(), nullptr, 0);
	if (size > 0) {
		auto buffer = std::wstring(size, L'\0');
		auto written = GetLongPathNameW(wide.c_str(), buffer.data(), size);
		if (written > 0 && written < size) native = QString::fromWCharArray(buffer.c_str(), static_cast<qsizetype>(written));
	}
	auto canonical = QFileInfo(native).canonicalFilePath();
	return canonical.isEmpty() ? QDir::fromNativeSeparators(native) : canonical;
}

} // namespace qs::windows::sys
