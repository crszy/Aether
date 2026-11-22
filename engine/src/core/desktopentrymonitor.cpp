#include "desktopentrymonitor.hpp"

#include <qcoreapplication.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qfilesystemwatcher.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qpointer.h>
#include <qrunnable.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qthreadpool.h>
#include <qtmetamacros.h>

#include "backgroundpool.hpp"
#include "desktopentry.hpp"

namespace {
void addPathAndParents(QStringList& paths, const QString& path) {
	paths.append(path);

	auto p = QFileInfo(path).absolutePath();
	while (!p.isEmpty()) {
		paths.append(p);
		const auto parent = QFileInfo(p).dir().absolutePath();
		if (parent == p) break;
		p = parent;
	}
}

constexpr int DEBOUNCE_MS = 100;
constexpr int BACKEND_DEBOUNCE_MS = 1000;
} // namespace

DesktopEntryMonitor::DesktopEntryMonitor(QObject* parent): QObject(parent) {
	this->debounceTimer.setSingleShot(true);
	this->debounceTimer.setInterval(
	    DesktopEntryManager::backend() != nullptr ? BACKEND_DEBOUNCE_MS : DEBOUNCE_MS
	);

	QObject::connect(
	    &this->watcher,
	    &QFileSystemWatcher::directoryChanged,
	    this,
	    &DesktopEntryMonitor::onDirectoryChanged
	);
	QObject::connect(
	    &this->debounceTimer,
	    &QTimer::timeout,
	    this,
	    &DesktopEntryMonitor::processChanges
	);

	this->startMonitoring();
}

void DesktopEntryMonitor::startMonitoring() {
	auto guard = QPointer(this);

	auto watchParents = DesktopEntryManager::backend() == nullptr;

	BackgroundThreadPool::instance()->start(QRunnable::create([guard, watchParents] {
		QStringList paths;
		for (const auto& path: DesktopEntryManager::desktopPaths()) {
			if (!QDir(path).exists()) continue;
			if (watchParents) addPathAndParents(paths, path);
			DesktopEntryMonitor::scanAndWatch(paths, path);
		}
		paths.removeDuplicates();

		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [guard, paths] {
			    if (guard != nullptr && !paths.isEmpty()) guard->watcher.addPaths(paths);
		    },
		    Qt::QueuedConnection
		);
	}));
}

void DesktopEntryMonitor::scanAndWatch(QStringList& paths, const QString& dirPath) {
	auto dir = QDir(dirPath);
	if (!dir.exists()) return;

	paths.append(dirPath);

	auto subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
	for (const auto& subdir: subdirs) paths.append(subdir.absoluteFilePath());
}

void DesktopEntryMonitor::onDirectoryChanged(const QString& /*path*/) {
	this->debounceTimer.start();
}

void DesktopEntryMonitor::processChanges() { emit this->desktopEntriesChanged(); }