#include "app_actions.hpp"

#include <vector>

#include <qdir.h>
#include <qdiriterator.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmutex.h>
#include <qobject.h>
#include <qstring.h>
#include <qstringlist.h>
#include <quuid.h>
#include <qvariant.h>

#include <qt_windows.h>

#include <appmodel.h>
#include <objbase.h>
#include <propkey.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include "../../core/desktopentry.hpp"
#include "../../core/logcat.hpp"
#include "../desktopentry_backend.hpp"
#include "../util.hpp"

using Microsoft::WRL::ComPtr;

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logAppActions, "quickshell.windows.appactions", QtWarningMsg);

const auto INSTALLED_APPS_URI = QStringLiteral("ms-settings:appsfeatures");
const auto APP_SETTINGS_URI = QStringLiteral("ms-settings:appsfeatures-app?");
constexpr qsizetype PUBLISHER_ID_LENGTH = 13;

QString takeComString(PWSTR raw) {
	if (raw == nullptr) return QString();
	auto str = QString::fromWCharArray(raw);
	CoTaskMemFree(raw);
	return str;
}

const QString& windowsDirectory() {
	static const auto dir = []() {
		wchar_t buffer[MAX_PATH] = {};
		auto length = GetSystemWindowsDirectoryW(buffer, MAX_PATH);
		if (length == 0 || length >= MAX_PATH) return QString();
		return QString::fromWCharArray(buffer, static_cast<qsizetype>(length));
	}();

	return dir;
}

bool isUnderWindows(const QString& path) {
	const auto& dir = windowsDirectory();
	if (dir.isEmpty() || path.size() <= dir.size()) return false;
	return path.startsWith(dir, Qt::CaseInsensitive) && path[dir.size()] == u'\\';
}

bool isFilesystemPath(const QString& path) {
	if (path.size() < 3) return false;
	if (path[0].isLetter() && path[1] == u':' && path[2] == u'\\') return true;
	return path.startsWith(QStringLiteral("\\\\")) && path[2] != u'?' && path[2] != u'.';
}

bool canElevate(const QString& path) {
	static const auto suffixes = QStringList {"exe", "bat", "cmd", "com", "msc"};
	return !path.isEmpty() && suffixes.contains(QFileInfo(path).suffix(), Qt::CaseInsensitive);
}

QString packageFamilyOf(const QString& token) {
	auto bang = token.indexOf(u'!');
	if (bang <= 0) return QString();

	auto family = token.left(bang);
	auto underscore = family.lastIndexOf(u'_');
	if (underscore <= 0) return QString();

	auto publisher = QStringView(family).sliced(underscore + 1);
	if (publisher.size() != PUBLISHER_ID_LENGTH) return QString();

	for (auto c: publisher) {
		if (c.unicode() > 0x7f || !c.isLetterOrNumber()) return QString();
	}

	return family;
}

bool isSystemPackage(const QString& family) {
	auto wfamily = family.toStdWString();
	UINT32 count = 0;
	UINT32 length = 0;

	auto rc = GetPackagesByPackageFamily(wfamily.c_str(), &count, nullptr, &length, nullptr);
	if (rc != ERROR_INSUFFICIENT_BUFFER || count == 0) return false;

	auto names = std::vector<PWSTR>(count);
	auto buffer = std::vector<wchar_t>(length);
	rc = GetPackagesByPackageFamily(wfamily.c_str(), &count, names.data(), &length, buffer.data());
	if (rc != ERROR_SUCCESS || count == 0) return false;

	UINT32 pathLength = 0;
	if (GetPackagePathByFullName(names.front(), &pathLength, nullptr) != ERROR_INSUFFICIENT_BUFFER) {
		return false;
	}

	auto path = std::vector<wchar_t>(pathLength);
	if (GetPackagePathByFullName(names.front(), &pathLength, path.data()) != ERROR_SUCCESS) {
		return false;
	}

	return isUnderWindows(QString::fromWCharArray(path.data()));
}

QString knownFolderTarget(const QString& token) {
	if (!token.startsWith(u'{')) return QString();

	auto close = token.indexOf(u'}');
	if (close < 0 || close + 1 >= token.size() || token[close + 1] != u'\\') return QString();

	auto folderId = QUuid::fromString(QStringView(token).first(close + 1));
	if (folderId.isNull()) return QString();

	PWSTR raw = nullptr;
	auto hr = SHGetKnownFolderPath(static_cast<GUID>(folderId), KF_FLAG_DONT_VERIFY, nullptr, &raw);
	auto folder = takeComString(raw);
	if (FAILED(hr) || folder.isEmpty()) return QString();

	return folder + token.sliced(close + 1);
}

QString win32Target(const QString& id, const QString& token) {
	auto target = WindowsDesktopEntryBackend::linkTargetForId(id);
	if (target.isEmpty()) target = knownFolderTarget(token);
	if (target.isEmpty()) target = token;

	target = QDir::toNativeSeparators(target);
	return isFilesystemPath(target) ? target : QString();
}

struct LinkIdentity {
	QString aumid;
	QString target;
};

LinkIdentity readLink(const QString& path) {
	auto wpath = path.toStdWString();
	ComPtr<IShellItem2> item;
	if (FAILED(SHCreateItemFromParsingName(wpath.c_str(), nullptr, IID_PPV_ARGS(&item)))) return {};

	auto link = LinkIdentity();
	PWSTR raw = nullptr;
	if (SUCCEEDED(item->GetString(PKEY_AppUserModel_ID, &raw))) link.aumid = takeComString(raw);

	raw = nullptr;
	if (SUCCEEDED(item->GetString(PKEY_Link_TargetParsingPath, &raw))) {
		link.target = QDir::toNativeSeparators(takeComString(raw));
	}

	return link;
}

QStringList startMenuRoots() {
	auto roots = QStringList();

	for (const auto& folderId: {FOLDERID_StartMenu, FOLDERID_CommonStartMenu}) {
		PWSTR raw = nullptr;
		auto hr = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &raw);
		auto root = takeComString(raw);
		if (SUCCEEDED(hr) && !root.isEmpty()) roots.append(root);
	}

	return roots;
}

QString findShortcut(const AppActionInfo& app) {
	auto named = QStringList();
	auto others = QStringList();

	for (const auto& root: startMenuRoots()) {
		auto iter = QDirIterator(
		    root,
		    {QStringLiteral("*.lnk")},
		    QDir::Files | QDir::Hidden,
		    QDirIterator::Subdirectories
		);

		while (iter.hasNext()) {
			auto path = QDir::toNativeSeparators(iter.next());
			auto baseName = iter.fileInfo().completeBaseName();
			auto isNamed = baseName.compare(app.name, Qt::CaseInsensitive) == 0;
			(isNamed ? named : others).append(path);
		}
	}

	auto fallback = QString();

	for (const auto* list: {&named, &others}) {
		for (const auto& path: *list) {
			auto link = readLink(path);

			if (!link.aumid.isEmpty()) {
				if (link.aumid.compare(app.token, Qt::CaseInsensitive) == 0) return path;
				continue;
			}

			if (link.target.isEmpty() || link.target.compare(app.target, Qt::CaseInsensitive) != 0) {
				continue;
			}

			if (list == &named) return path;
			if (fallback.isEmpty()) fallback = path;
		}
	}

	return fallback;
}

struct ShortcutCache {
	QMutex mutex;
	QHash<QString, QString> paths;
};

ShortcutCache& shortcutCache() {
	static auto* cache = new ShortcutCache(); // NOLINT
	return *cache;
}

void clearShortcutCache() {
	auto& cache = shortcutCache();
	QMutexLocker locker(&cache.mutex);
	cache.paths.clear();
}

QString startMenuShortcut(const AppActionInfo& app) {
	auto& cache = shortcutCache();
	auto cached = QString();
	auto known = false;

	{
		QMutexLocker locker(&cache.mutex);
		auto it = cache.paths.constFind(app.token);
		known = it != cache.paths.constEnd();
		if (known) cached = *it;
	}

	if (known && (cached.isEmpty() || QFileInfo::exists(cached))) return cached;

	auto path = findShortcut(app);

	QMutexLocker locker(&cache.mutex);
	cache.paths.insert(app.token, path);
	return path;
}

DWORD shellExecute(const QString& file, const wchar_t* verb, ULONG mask, const QString& directory) {
	auto wfile = file.toStdWString();
	auto wdir = directory.toStdWString();

	auto info = SHELLEXECUTEINFOW {};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI | mask;
	info.lpVerb = verb;
	info.lpFile = wfile.c_str();
	info.lpDirectory = wdir.empty() ? nullptr : wdir.c_str();
	info.nShow = SW_SHOWNORMAL;

	return ShellExecuteExW(&info) ? ERROR_SUCCESS : GetLastError();
}

void runElevated(const QString& id, const AppActionInfo& app) {
	auto shortcut = startMenuShortcut(app);
	auto error = static_cast<DWORD>(ERROR_FILE_NOT_FOUND);

	if (!shortcut.isEmpty()) {
		error = shellExecute(shortcut, L"runas", SEE_MASK_INVOKEIDLIST, QString());
		if (error != ERROR_SUCCESS && error != ERROR_CANCELLED) {
			qCDebug(logAppActions) << "Elevating the shortcut of" << id << "failed:" << error;
		}
	}

	if (error != ERROR_SUCCESS && error != ERROR_CANCELLED) {
		auto directory = QDir::toNativeSeparators(QFileInfo(app.target).absolutePath());
		error = shellExecute(app.target, L"runas", SEE_MASK_INVOKEIDLIST, directory);
	}

	if (error == ERROR_CANCELLED) {
		qCDebug(logAppActions) << "Elevation of" << id << "was cancelled";
	} else if (error != ERROR_SUCCESS) {
		qCWarning(logAppActions) << "Could not run" << id << "as administrator:" << error;
	}
}

bool selectInExplorer(const QString& id, const QString& path) {
	auto wpath = path.toStdWString();
	PIDLIST_ABSOLUTE pidl = nullptr;

	auto hr = SHParseDisplayName(wpath.c_str(), nullptr, &pidl, 0, nullptr);
	if (FAILED(hr)) {
		qCDebug(logAppActions) << "SHParseDisplayName failed for" << id << ":" << Qt::hex << hr;
		return false;
	}

	AllowSetForegroundWindow(ASFW_ANY);
	hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
	ILFree(pidl);

	if (FAILED(hr)) {
		qCDebug(logAppActions) << "SHOpenFolderAndSelectItems failed for" << id << ":" << Qt::hex
		                       << hr;
		return false;
	}

	return true;
}

void openLocation(const QString& id, const AppActionInfo& app) {
	auto shortcut = startMenuShortcut(app);
	if (!shortcut.isEmpty() && selectInExplorer(id, shortcut)) return;
	if (selectInExplorer(id, app.target)) return;

	qCWarning(logAppActions) << "Could not open the file location of" << id;
}

void openSettings(const QString& id, const QString& uri) {
	runLauncher([id, uri]() {
		auto error = shellExecute(uri, L"open", 0, QString());
		if (error != ERROR_SUCCESS) {
			qCWarning(logAppActions) << "Could not open Settings for" << id << ":" << error;
		}
	});
}

} // namespace

AppActions::AppActions(QObject* parent): QObject(parent) {
	QObject::connect(
	    DesktopEntryManager::instance(),
	    &DesktopEntryManager::applicationsChanged,
	    this,
	    [this]() {
		    this->mCache.clear();
		    clearShortcutCache();
	    }
	);
}

AppActionInfo AppActions::details(const QString& entryId) {
	auto id = entryId.toLower();
	auto token = WindowsDesktopEntryBackend::parsingNameForId(id);
	if (token.isEmpty()) return AppActionInfo();

	auto cached = this->mCache.constFind(id);
	if (cached != this->mCache.constEnd() && cached->token == token) return *cached;

	auto app = AppActionInfo();
	app.token = token;
	app.packageFamilyName = packageFamilyOf(token);
	app.packaged = !app.packageFamilyName.isEmpty();

	if (app.packaged) {
		app.canUninstall = !isSystemPackage(app.packageFamilyName);
	} else {
		app.target = win32Target(id, token);

		auto* entry = DesktopEntryManager::instance()->byId(id);
		if (entry != nullptr) app.name = entry->bindableName().value();

		app.canRunAsAdmin = canElevate(app.target);
		app.canOpenLocation = !app.target.isEmpty();
		app.canUninstall = !app.target.isEmpty() && !isUnderWindows(app.target);
	}

	this->mCache.insert(id, app);
	return app;
}

QVariantMap AppActions::info(const QString& entryId) {
	auto app = this->details(entryId);

	return {
	    {QStringLiteral("packaged"), app.packaged},
	    {QStringLiteral("canRunAsAdmin"), app.canRunAsAdmin},
	    {QStringLiteral("canOpenLocation"), app.canOpenLocation},
	    {QStringLiteral("canUninstall"), app.canUninstall},
	    {QStringLiteral("packageFamilyName"), app.packageFamilyName},
	};
}

bool AppActions::runAsAdmin(const QString& entryId) {
	auto app = this->details(entryId);
	if (!app.canRunAsAdmin) return false;

	runLauncher([id = entryId, app]() { runElevated(id, app); });
	return true;
}

bool AppActions::openFileLocation(const QString& entryId) {
	auto app = this->details(entryId);
	if (!app.canOpenLocation) return false;

	runLauncher([id = entryId, app]() { openLocation(id, app); });
	return true;
}

bool AppActions::openAppSettings(const QString& entryId) {
	auto app = this->details(entryId);
	if (!app.packaged) return false;

	openSettings(entryId, APP_SETTINGS_URI + app.packageFamilyName);
	return true;
}

bool AppActions::uninstall(const QString& entryId) {
	auto app = this->details(entryId);
	if (!app.canUninstall) return false;

	openSettings(
	    entryId,
	    app.packaged ? APP_SETTINGS_URI + app.packageFamilyName : INSTALLED_APPS_URI
	);
	return true;
}

} // namespace qs::windows::sys
