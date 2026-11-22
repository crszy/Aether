#include "rootwrapper.hpp"
#include <cstdlib>
#include <future>
#include <optional>
#include <thread>
#include <utility>

#include <qdir.h>
#include <qelapsedtimer.h>
#include <qfileinfo.h>
#include <qfilesystemwatcher.h>
#include <qlogging.h>
#include <qobject.h>
#include <qqmlcomponent.h>
#include <qqmlengine.h>
#include <qquickitem.h>
#include <qtenvironmentvariables.h>
#include <qtmetamacros.h>
#include <qurl.h>

#include "../ui/reload_popup.hpp"
#include "../window/floatingwindow.hpp"
#include "generation.hpp"
#include "instanceinfo.hpp"
#include "logcat.hpp"
#include "paths.hpp"
#include "qmlcache.hpp"
#include "qmlglobal.hpp"
#include "scan.hpp"
#include "toolsupport.hpp"

namespace {
QS_LOGGING_CATEGORY(logStartup, "quickshell.startup", QtWarningMsg);

QString scanCachePath() { return QsPaths::instance()->shellCacheDir().filePath("qmlscan.bin"); }

QmlScanner scanConfig(const QString& rootPath, const QString& cachePath, bool deferPreprocessing) {
	auto scanner = QmlScanner(QFileInfo(rootPath).dir());
	scanner.deferPreprocessing = deferPreprocessing;
	scanner.loadCache(cachePath);
	scanner.scanQmlRoot(rootPath);
	if (!scanner.preprocessingDeferred) scanner.saveCache(cachePath);
	return scanner;
}

struct ScanPrefetch {
	QString rootPath;
	QString cachePath;
	std::future<QmlScanner> result;
};

ScanPrefetch* gScanPrefetch = nullptr; // NOLINT

std::optional<QmlScanner> takePrefetchedScan(const QString& rootPath) {
	auto* prefetch = gScanPrefetch;
	if (prefetch == nullptr) return std::nullopt;
	gScanPrefetch = nullptr;

	auto timer = QElapsedTimer();
	timer.start();
	auto scanner = prefetch->result.get();
	auto matches = prefetch->rootPath == rootPath && prefetch->cachePath == scanCachePath();
	delete prefetch;

	qCDebug(logStartup) << "Waited" << timer.elapsed() << "ms for the prefetched scan";

	if (!matches || scanner.preprocessingDeferred) return std::nullopt;
	return scanner;
}

} // namespace

void RootWrapper::prefetch(const QString& rootPath) {
	if (gScanPrefetch != nullptr || qEnvironmentVariableIsSet("QS_DISABLE_SCAN_PREFETCH")) return;

	auto* prefetch = new ScanPrefetch();
	prefetch->rootPath = rootPath;
	prefetch->cachePath = scanCachePath();
	auto task = std::packaged_task<QmlScanner()>(
	    [rootPath = prefetch->rootPath, cachePath = prefetch->cachePath]() {
		    auto timer = QElapsedTimer();
		    timer.start();
		    auto scanner = scanConfig(rootPath, cachePath, true);
		    qCDebug(logStartup) << "Scanned" << scanner.scannedFiles.size() << "files on the prefetch thread in"
		                        << timer.restart() << "ms";

		    if (!scanner.preprocessingDeferred) {
			    qs::qmlcache::preload(QFileInfo(rootPath).dir());
		    }

		    return scanner;
	    }
	);

	prefetch->result = task.get_future();
	gScanPrefetch = prefetch;
	std::thread(std::move(task)).detach();
}

RootWrapper::RootWrapper(QString rootPath, QString shellId)
    : QObject(nullptr)
    , rootPath(std::move(rootPath))
    , shellId(std::move(shellId))
    , originalWorkingDirectory(QDir::current().absolutePath()) {
	QObject::connect(
	    QuickshellSettings::instance(),
	    &QuickshellSettings::watchFilesChanged,
	    this,
	    &RootWrapper::onWatchFilesChanged
	);

	QObject::connect(
	    &this->configDirWatcher,
	    &QFileSystemWatcher::directoryChanged,
	    this,
	    &RootWrapper::updateTooling
	);

	this->reloadGraph(true);

	if (this->generation == nullptr) {
		exit(-1); // NOLINT
	}
}

RootWrapper::~RootWrapper() {
	// event loop may no longer be running so deleteLater is not an option
	if (this->generation != nullptr) {
		this->generation->shutdown();
	}
}

void RootWrapper::reloadGraph(bool hard) {
	auto rootFile = QFileInfo(this->rootPath);
	auto rootPath = rootFile.dir();
	auto timer = QElapsedTimer();
	timer.start();

	auto prefetched = this->generation == nullptr ? takePrefetchedScan(this->rootPath) : std::nullopt;
	auto scanner = prefetched ? std::move(*prefetched)
	                          : scanConfig(this->rootPath, scanCachePath(), false);
	if (!prefetched) {
		qCDebug(logStartup) << "Scanned" << scanner.scannedFiles.size() << "files in" << timer.elapsed() << "ms";
	}
	timer.restart();

	qs::core::QmlToolingSupport::updateTooling(rootPath, scanner);
	qCDebug(logStartup) << "Updated tooling in" << timer.restart() << "ms";
	this->configDirWatcher.addPath(rootPath.path());

	// todo: move into EngineGeneration
	if (this->generation != nullptr) {
		qInfo() << "Reloading configuration...";
		QuickshellSettings::reset();
	}

	QDir::setCurrent(this->originalWorkingDirectory);

	if (!scanner.scanErrors.isEmpty()) {
		qCritical() << "Failed to load configuration";
		QString errorString = "Failed to load configuration";
		for (auto& error: scanner.scanErrors) {
			const auto& file = error.file;
			QString rel;
			if (file.startsWith(rootPath.path() % '/')) {
				rel = '@' % file.sliced(rootPath.path().length() + 1);
			} else {
				rel = file;
			}

			auto msg = "  error in " % rel % '[' % QString::number(error.line) % ":0]: " % error.message;
			errorString += '\n' % msg;
			qCritical().noquote() << msg;
		}

		if (this->generation != nullptr && this->generation->qsgInstance != nullptr) {
			emit this->generation->qsgInstance->reloadFailed(errorString);
		}

		return;
	}

	auto* generation = new EngineGeneration(rootPath, std::move(scanner));
	generation->wrapper = this;
	qCDebug(logStartup) << "Created the engine in" << timer.restart() << "ms";

	QUrl url;
	url.setScheme("qs");
	url.setPath("@/qs/" % rootFile.fileName());
	auto component = QQmlComponent(generation->engine, url);
	qCDebug(logStartup) << "Loaded the root component in" << timer.restart() << "ms";

	if (!component.isReady()) {
		qCritical() << "Failed to load configuration";
		QString errorString = "Failed to load configuration";

		auto errors = component.errors();
		for (auto& error: errors) {
			const auto& url = error.url();
			auto rel = url.scheme() == "qs" && url.path().startsWith("@/qs/") ? "@" % url.path().sliced(5)
			                                                                  : url.toString();
			auto msg = "  caused by " % rel % '[' % QString::number(error.line()) % ':'
			         % QString::number(error.column()) % "]: " % error.description();
			errorString += '\n' % msg;
			qCritical().noquote() << msg;
		}

		auto newFiles = generation->scanner.scannedFiles;
		generation->destroy();

		if (this->generation != nullptr) {
			if (this->generation->setExtraWatchedFiles(newFiles)) {
				qInfo() << "Watching additional files picked up in reload for changes...";
			}

			auto showPopup = true;
			if (this->generation->qsgInstance != nullptr) {
				this->generation->qsgInstance->clearReloadPopupInhibit();
				emit this->generation->qsgInstance->reloadFailed(errorString);
				showPopup = !this->generation->qsgInstance->isReloadPopupInhibited();
			}

			if (showPopup)
				qs::ui::ReloadPopup::spawnPopup(InstanceInfo::CURRENT.instanceId, true, errorString);
		}

		if (this->generation != nullptr && this->generation->qsgInstance != nullptr) {
			emit this->generation->qsgInstance->reloadFailed(errorString);
		}

		return;
	}

	auto* newRoot = component.beginCreate(generation->engine->rootContext());
	qCDebug(logStartup) << "Created the root object in" << timer.restart() << "ms";

	if (auto* item = qobject_cast<QQuickItem*>(newRoot)) {
		auto* window = new FloatingWindowInterface();
		item->setParent(window);
		item->setParentItem(window->contentItem());
		window->setWidth(static_cast<int>(item->width()));
		window->setHeight(static_cast<int>(item->height()));
		newRoot = window;
	}

	generation->root = newRoot;

	component.completeCreate();
	qCDebug(logStartup) << "Completed the root object in" << timer.restart() << "ms";

	if (this->generation) {
		QObject::disconnect(this->generation, nullptr, this, nullptr);
	}

	auto isReload = this->generation != nullptr;
	generation->onReload(hard ? nullptr : this->generation);

	if (hard && this->generation) {
		this->generation->destroy();
	}

	this->generation = generation;

	qInfo() << "Configuration Loaded";

	QObject::connect(this->generation, &QObject::destroyed, this, &RootWrapper::generationDestroyed);
	QObject::connect(
	    this->generation,
	    &EngineGeneration::filesChanged,
	    this,
	    &RootWrapper::onWatchedFilesChanged
	);

	this->onWatchFilesChanged();

	if (isReload) {
		auto showPopup = true;

		if (this->generation->qsgInstance != nullptr) {
			this->generation->qsgInstance->clearReloadPopupInhibit();
			emit this->generation->qsgInstance->reloadCompleted();
			showPopup = !this->generation->qsgInstance->isReloadPopupInhibited();
		}

		if (showPopup) qs::ui::ReloadPopup::spawnPopup(InstanceInfo::CURRENT.instanceId, false, "");
	}
}

void RootWrapper::generationDestroyed() { this->generation = nullptr; }

void RootWrapper::onWatchFilesChanged() {
	auto watchFiles = QuickshellSettings::instance()->watchFiles();
	if (this->generation != nullptr) {
		this->generation->setWatchingFiles(watchFiles);
	}
}

void RootWrapper::onWatchedFilesChanged() { this->reloadGraph(false); }

void RootWrapper::updateTooling() {
	if (!this->generation) return;
	auto configDir = QFileInfo(this->rootPath).dir();
	qs::core::QmlToolingSupport::updateTooling(configDir, this->generation->scanner);
}
