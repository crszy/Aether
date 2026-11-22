#include "paths.hpp"
#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <tuple>
#include <utility>

#include <qcontainerfwd.h>
#include <qcoreapplication.h>
#include <qdatetime.h>
#include <qdatastream.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qset.h>
#include <qstandardpaths.h>
#include <qtenvironmentvariables.h>
#include <qtimezone.h>
#include <qtversionchecks.h>

#ifdef _WIN32
#include "winfilelock.hpp"
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "instanceinfo.hpp"
#include "logcat.hpp"

namespace {
QS_LOGGING_CATEGORY(logPaths, "quickshell.paths", QtWarningMsg);

bool createRunLink(const QDir& target, const QString& linkPath) {
	QFile::remove(linkPath);

#ifdef _WIN32
	auto file = QFile(linkPath);
	if (!file.open(QFile::WriteOnly | QFile::Truncate)) return false;
	return file.write(target.absolutePath().toUtf8()) != -1;
#else
	return symlinkat(target.filesystemCanonicalPath().c_str(), 0, linkPath.toStdString().c_str())
	    == 0;
#endif
}
} // namespace

QsPaths* QsPaths::instance() {
	static auto* instance = new QsPaths(); // NOLINT
	return instance;
}

void QsPaths::init(
    QString shellId,
    QString pathId,
    QString dataOverride,
    QString stateOverride,
    QString cacheOverride
) {
	auto* instance = QsPaths::instance();
	instance->shellId = std::move(shellId);
	instance->pathId = std::move(pathId);
	instance->shellDataOverride = std::move(dataOverride);
	instance->shellStateOverride = std::move(stateOverride);
	instance->shellCacheOverride = std::move(cacheOverride);
}

QDir QsPaths::crashDir(const QString& id) {
	auto dir = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
	dir = QDir(dir.filePath("crashes"));
	dir = QDir(dir.filePath(id));

	return dir;
}

QString QsPaths::basePath(const QString& id) {
	auto path = QsPaths::instance()->baseRunDir()->filePath("by-id");
	path = QDir(path).filePath(id);
	return path;
}

QString QsPaths::ipcPath(const QString& id) {
#ifdef _WIN32
	return "quickshell-" + id;
#else
	return QDir(QsPaths::basePath(id)).filePath("ipc.sock");
#endif
}

QString QsPaths::resolveRunLink(const QString& path) {
#ifdef _WIN32
	if (QFileInfo(path).isFile()) {
		auto file = QFile(path);
		if (file.open(QFile::ReadOnly)) {
			auto target = QString::fromUtf8(file.readAll()).trimmed();
			if (!target.isEmpty()) return target;
		}
	}
#endif

	return path;
}

QDir* QsPaths::baseRunDir() {
	if (this->baseRunState == DirState::Unknown) {
#ifdef _WIN32
		auto runtimeDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
		this->mBaseRunDir = QDir(QDir(runtimeDir).filePath("run"));
#else
		auto runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
		if (runtimeDir.isEmpty()) {
			runtimeDir = QString("/run/user/%1").arg(getuid());
			qCInfo(logPaths) << "XDG_RUNTIME_DIR was not set, defaulting to" << runtimeDir;
		}

		this->mBaseRunDir = QDir(runtimeDir);
		this->mBaseRunDir = QDir(this->mBaseRunDir.filePath("quickshell"));
#endif
		qCDebug(logPaths) << "Initialized base runtime path:" << this->mBaseRunDir.path();

		if (!this->mBaseRunDir.mkpath(".")) {
			qCCritical(logPaths) << "Could not create base runtime directory at"
			                     << this->mBaseRunDir.path();

			this->baseRunState = DirState::Failed;
		} else {
			this->baseRunState = DirState::Ready;
		}
	}

	if (this->baseRunState == DirState::Failed) return nullptr;
	else return &this->mBaseRunDir;
}

QDir* QsPaths::shellRunDir() {
	if (this->shellRunState == DirState::Unknown) {
		if (auto* baseRunDir = this->baseRunDir()) {
			this->mShellRunDir = QDir(baseRunDir->filePath("by-shell"));
			this->mShellRunDir = QDir(this->mShellRunDir.filePath(this->shellId));

			qCDebug(logPaths) << "Initialized runtime path:" << this->mShellRunDir.path();

			if (!this->mShellRunDir.mkpath(".")) {
				qCCritical(logPaths) << "Could not create runtime directory at"
				                     << this->mShellRunDir.path();
				this->shellRunState = DirState::Failed;
			} else {
				this->shellRunState = DirState::Ready;
			}
		} else {
			qCCritical(logPaths) << "Could not create shell runtime path as it was not possible to "
			                        "create the base runtime path.";

			this->shellRunState = DirState::Failed;
		}
	}

	if (this->shellRunState == DirState::Failed) return nullptr;
	else return &this->mShellRunDir;
}

QDir* QsPaths::instanceRunDir() {
	if (this->instanceRunState == DirState::Unknown) {
		auto* runDir = this->baseRunDir();

		if (!runDir) {
			qCCritical(logPaths) << "Cannot create instance runtime directory as main runtim directory "
			                        "could not be created.";
			this->instanceRunState = DirState::Failed;
		} else {
			auto byIdDir = QDir(runDir->filePath("by-id"));

			this->mInstanceRunDir = byIdDir.filePath(InstanceInfo::CURRENT.instanceId);

			qCDebug(logPaths) << "Initialized instance runtime path:" << this->mInstanceRunDir.path();

			if (!this->mInstanceRunDir.mkpath(".")) {
				qCCritical(logPaths) << "Could not create instance runtime directory at"
				                     << this->mInstanceRunDir.path();
				this->instanceRunState = DirState::Failed;
			} else {
				this->instanceRunState = DirState::Ready;
			}
		}
	}

	if (this->shellRunState == DirState::Failed) return nullptr;
	else return &this->mInstanceRunDir;
}

QDir* QsPaths::shellVfsDir() {
	if (this->shellVfsState == DirState::Unknown) {
		if (auto* baseRunDir = this->baseRunDir()) {
			this->mShellVfsDir = QDir(baseRunDir->filePath("vfs"));
			this->mShellVfsDir = QDir(this->mShellVfsDir.filePath(this->shellId));

			qCDebug(logPaths) << "Initialized runtime vfs path:" << this->mShellVfsDir.path();

			if (!this->mShellVfsDir.mkpath(".")) {
				qCCritical(logPaths) << "Could not create runtime vfs directory at"
				                     << this->mShellVfsDir.path();
				this->shellVfsState = DirState::Failed;
			} else {
				this->shellVfsState = DirState::Ready;
			}
		} else {
			qCCritical(logPaths) << "Could not create shell runtime vfs path as it was not possible to "
			                        "create the base runtime path.";

			this->shellVfsState = DirState::Failed;
		}
	}

	if (this->shellVfsState == DirState::Failed) return nullptr;
	else return &this->mShellVfsDir;
}

void QsPaths::linkRunDir() {
	if (auto* runDir = this->instanceRunDir()) {
		auto pidDir = QDir(this->baseRunDir()->filePath("by-pid"));
		auto* shellDir = this->shellRunDir();

		if (!shellDir) {
			qCCritical(
			    logPaths
			) << "Could not create by-id symlink as the shell runtime path could not be created.";
		} else {
			auto shellPath = shellDir->filePath(runDir->dirName());

			if (!createRunLink(*runDir, shellPath)) {
				qCCritical(logPaths).nospace()
				    << "Could not create id symlink to " << runDir->path() << " at " << shellPath
				    << " with error code " << errno << ": " << qt_error_string();
			} else {
				qCDebug(logPaths) << "Created shellid symlink" << shellPath << "to instance runtime path"
				                  << runDir->path();
			}
		}

		if (!pidDir.mkpath(".")) {
			qCCritical(logPaths) << "Could not create PID symlink directory.";
		} else {
			auto pidPath = pidDir.filePath(QString::number(QCoreApplication::applicationPid()));

			if (!createRunLink(*runDir, pidPath)) {
				qCCritical(logPaths).nospace()
				    << "Could not create PID symlink to " << runDir->path() << " at " << pidPath
				    << " with error code " << errno << ": " << qt_error_string();
			} else {
				qCDebug(logPaths) << "Created PID symlink" << pidPath << "to instance runtime path"
				                  << runDir->path();
			}
		}
	} else {
		qCCritical(logPaths) << "Could not create PID symlink to runtime directory, as the runtime "
		                        "directory could not be created.";
	}
}

void QsPaths::linkPathDir() {
	if (auto* runDir = this->shellRunDir()) {
		auto pathDir = QDir(this->baseRunDir()->filePath("by-path"));

		if (!pathDir.mkpath(".")) {
			qCCritical(logPaths) << "Could not create path symlink directory.";
			return;
		}

		auto linkPath = pathDir.filePath(this->pathId);

		if (!createRunLink(*runDir, linkPath)) {
			qCCritical(logPaths).nospace()
			    << "Could not create path symlink to " << runDir->path() << " at " << linkPath
			    << " with error code " << errno << ": " << qt_error_string();
		} else {
			qCDebug(logPaths) << "Created path symlink" << linkPath << "to shell runtime path"
			                  << runDir->path();
		}
	} else {
		qCCritical(logPaths) << "Could not create path symlink to shell runtime directory, as the "
		                        "shell runtime directory could not be created.";
	}
}

QDir QsPaths::shellDataDir() {
	if (this->shellDataState == DirState::Unknown) {
		QDir dir;
		if (this->shellDataOverride.isEmpty()) {
			dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
			dir = QDir(dir.filePath("by-shell"));
			dir = QDir(dir.filePath(this->shellId));
		} else {
			auto basedir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
			dir = QDir(this->shellDataOverride.replace("$BASE", basedir));
		}

		this->mShellDataDir = dir;

		qCDebug(logPaths) << "Initialized data path:" << dir.path();

		if (!dir.mkpath(".")) {
			qCCritical(logPaths) << "Could not create data directory at" << dir.path();

			this->shellDataState = DirState::Failed;
		} else {
			this->shellDataState = DirState::Ready;
		}
	}

	// Returning no path on fail might result in files being written in unintended locations.
	return this->mShellDataDir;
}

QDir QsPaths::shellStateDir() {
	if (this->shellStateState == DirState::Unknown) {
#if QT_VERSION < QT_VERSION_CHECK(6, 7, 0)
		QDir dir;
		if (qEnvironmentVariableIsSet("XDG_STATE_HOME")) {
			dir = QDir(qEnvironmentVariable("XDG_STATE_HOME"));
		} else {
			auto home = QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
			dir = QDir(home.filePath(".local/state"));
		}

		if (this->shellStateOverride.isEmpty()) {
			dir = QDir(dir.filePath("quickshell/by-shell"));
			dir = QDir(dir.filePath(this->shellId));
		} else {
			dir = QDir(this->shellStateOverride.replace("$BASE", dir.path()));
		}
#else
		QDir dir;
		if (this->shellStateOverride.isEmpty()) {
			dir = QDir(QStandardPaths::writableLocation(QStandardPaths::StateLocation));
			dir = QDir(dir.filePath("by-shell"));
			dir = QDir(dir.filePath(this->shellId));
		} else {
			auto basedir = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
			dir = QDir(this->shellStateOverride.replace("$BASE", basedir));
		}
#endif
		this->mShellStateDir = dir;

		qCDebug(logPaths) << "Initialized state path:" << dir.path();

		if (!dir.mkpath(".")) {
			qCCritical(logPaths) << "Could not create state directory at" << dir.path();

			this->shellStateState = DirState::Failed;
		} else {
			this->shellStateState = DirState::Ready;
		}
	}

	// Returning no path on fail might result in files being written in unintended locations.
	return this->mShellStateDir;
}

QDir QsPaths::shellCacheDir() {
	if (this->shellCacheState == DirState::Unknown) {
		QDir dir;
		if (this->shellCacheOverride.isEmpty()) {
			dir = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
			dir = QDir(dir.filePath("by-shell"));
			dir = QDir(dir.filePath(this->shellId));
		} else {
			auto basedir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
			dir = QDir(this->shellCacheOverride.replace("$BASE", basedir));
		}

		this->mShellCacheDir = dir;

		qCDebug(logPaths) << "Initialized cache path:" << dir.path();

		if (!dir.mkpath(".")) {
			qCCritical(logPaths) << "Could not create cache directory at" << dir.path();

			this->shellCacheState = DirState::Failed;
		} else {
			this->shellCacheState = DirState::Ready;
		}
	}

	// Returning no path on fail might result in files being written in unintended locations.
	return this->mShellCacheDir;
}

void QsPaths::createLock() {
	if (auto* runDir = this->instanceRunDir()) {
		auto path = runDir->filePath("instance.lock");
		auto* file = new QFile(path); // leaked

		if (!file->open(QFile::ReadWrite | QFile::Truncate)) {
			qCCritical(logPaths) << "Could not create instance lock at" << path;
			return;
		}

#ifdef _WIN32
		auto locked = qs::core::winlock::lockExclusive(*file);
#else
		struct flock lock = {
		    .l_type = F_WRLCK,
		    .l_whence = SEEK_SET,
		    .l_start = 0,
		    .l_len = 0,
		    .l_pid = 0,
		};

		auto locked = fcntl(file->handle(), F_SETLK, &lock) == 0; // NOLINT
#endif

		if (!locked) {
			qCCritical(logPaths).nospace() << "Could not lock instance lock at " << path
			                               << " with error code " << errno << ": " << qt_error_string();
		} else {
			auto stream = QDataStream(file);
			stream << InstanceInfo::CURRENT;
			file->flush();
			qCDebug(logPaths) << "Created instance lock at" << path;
		}
	} else {
		qCCritical(
		    logPaths
		) << "Could not create instance lock, as the instance runtime directory could not be created.";
	}
}

bool QsPaths::checkLock(const QString& path, InstanceLockInfo* info, bool allowDead) {
	auto file = QFile(QDir(QsPaths::resolveRunLink(path)).filePath("instance.lock"));
	if (!file.open(QFile::ReadOnly)) return false;

#ifdef _WIN32
	auto isLocked = qs::core::winlock::isLocked(file);

	if (!isLocked && !allowDead) return false;

	if (info) {
		auto stream = QDataStream(&file);
		stream >> info->instance;

		info->pid = isLocked ? info->instance.pid : -1;
	}
#else
	struct flock lock = {
	    .l_type = F_WRLCK,
	    .l_whence = SEEK_SET,
	    .l_start = 0,
	    .l_len = 0,
	    .l_pid = 0,
	};

	fcntl(file.handle(), F_GETLK, &lock); // NOLINT
	auto isLocked = lock.l_type != F_UNLCK;

	if (!isLocked && !allowDead) return false;

	if (info) {
		info->pid = isLocked ? lock.l_pid : -1;

		auto stream = QDataStream(&file);
		stream >> info->instance;
	}
#endif

	return true;
}

std::tuple<QVector<InstanceLockInfo>, QVector<InstanceLockInfo>, QVector<InstanceLockInfo>>
QsPaths::collectInstances(const QString& path, const QString& display) {
	qCDebug(logPaths) << "Collecting instances from" << path;
	auto liveInstances = QVector<InstanceLockInfo>();
	auto mismatchedInstances = QVector<InstanceLockInfo>();
	auto deadInstances = QVector<InstanceLockInfo>();
	auto dir = QDir(QsPaths::resolveRunLink(path));

#ifdef _WIN32
	constexpr auto filters = QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot;
#else
	constexpr auto filters = QDir::Dirs | QDir::NoDotAndDotDot;
#endif

	InstanceLockInfo info;
	for (auto& entry: dir.entryList(filters)) {
		auto path = QsPaths::resolveRunLink(dir.filePath(entry));

		if (QsPaths::checkLock(path, &info, true)) {
			qCDebug(logPaths).nospace() << "Found instance " << info.instance.instanceId << " (pid "
			                            << info.pid << ") at " << path;

			if (info.pid == -1) {
				deadInstances.push_back(info);
				continue;
			}

			if (!display.isEmpty() && info.instance.display != display) {
				qCDebug(logPaths) << "Skipped instance with mismatched display at" << path;
				mismatchedInstances.push_back(info);
				continue;
			}

			liveInstances.push_back(info);
		} else {
			qCDebug(logPaths) << "Skipped potential instance at" << path;
		}
	}

	return {liveInstances, mismatchedInstances, deadInstances};
}

void QsPaths::pruneDeadInstances(const QDir& baseRunDir, qsizetype keepPerShell) {
	struct Dead {
		QString id;
		QDateTime launchTime;
	};

	auto byId = QDir(baseRunDir.filePath("by-id"));
	auto recentLimit = QDateTime::currentDateTimeUtc().addSecs(-300);
	auto deadByShell = QHash<QString, QVector<Dead>>();
	auto keptIds = QSet<QString>();

	InstanceLockInfo info;
	for (const auto& entry: byId.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		auto id = entry.fileName();
		keptIds.insert(id);

		if (entry.lastModified(QTimeZone::UTC) > recentLimit) continue;
		if (!QsPaths::checkLock(entry.filePath(), &info, true) || info.pid != -1) continue;

		deadByShell[info.instance.shellId].push_back({.id = id, .launchTime = info.instance.launchTime});
	}

	auto removed = QSet<QString>();
	for (auto it = deadByShell.begin(); it != deadByShell.end(); ++it) {
		auto& dead = it.value();
		std::ranges::sort(dead, [](const Dead& a, const Dead& b) { return a.launchTime > b.launchTime; });

		for (qsizetype i = keepPerShell; i < dead.size(); i++) {
			const auto& id = dead[i].id;
			if (!QDir(byId.filePath(id)).removeRecursively()) continue;

			removed.insert(id);
			keptIds.remove(id);
			QFile::remove(QDir(baseRunDir.filePath("by-shell")).filePath(it.key() + '/' + id));

			auto crashes = QsPaths::crashDir(id);
			if (crashes.exists() && crashes.isEmpty()) crashes.removeRecursively();
		}
	}

	auto byPid = QDir(baseRunDir.filePath("by-pid"));
	for (const auto& link: byPid.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
		auto target = QsPaths::resolveRunLink(link.filePath());
		if (!keptIds.contains(QFileInfo(target).fileName())) QFile::remove(link.filePath());
	}

	if (!removed.isEmpty()) {
		qCInfo(logPaths) << "Removed" << removed.size() << "old instance directories";
	}
}
