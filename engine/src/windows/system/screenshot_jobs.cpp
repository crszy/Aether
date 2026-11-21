#include "screenshot_jobs.hpp"

#include <atomic>
#include <memory>

#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qimage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qstring.h>
#include <qtimer.h>
#include <quuid.h>

namespace qs::windows::sys {

namespace {
Q_LOGGING_CATEGORY(logScreenshotJobs, "quickshell.windows.screenshot", QtWarningMsg);

constexpr int REMOVE_RETRY_MS = 1000;
constexpr int REMOVE_ATTEMPTS = 30;

QString destinationKey(const QString& path) { return QFileInfo(path).absoluteFilePath(); }

bool removeIfPresent(const QString& path) {
	return !QFileInfo::exists(path) || QFile::remove(path);
}
} // namespace

bool ScreenshotJobs::Job::isCancelled() const {
	return !this->cancelled || this->cancelled->load(std::memory_order_acquire);
}

ScreenshotJobs::ScreenshotJobs() {
	this->removalRetry.setSingleShot(true);
	this->removalRetry.setInterval(REMOVE_RETRY_MS);
	QObject::connect(&this->removalRetry, &QTimer::timeout, &this->removalRetry, [this]() {
		this->retryRemovals();
	});
}

ScreenshotJobs::Job ScreenshotJobs::start(const QString& path) {
	auto key = destinationKey(path);
	this->supersede(key);

	auto job = Job {
	    .id = this->nextId++,
	    .path = key,
	    .partPath = key + QStringLiteral(".tmp-") + QUuid::createUuid().toString(QUuid::Id128),
	    .cancelled = std::make_shared<std::atomic_bool>(false),
	};

	this->jobs.insert(job.id, job);
	this->destinations.insert(key, job.id);
	return job;
}

void ScreenshotJobs::cancel(int id) {
	auto it = this->jobs.constFind(id);
	if (it != this->jobs.constEnd()) it->cancelled->store(true, std::memory_order_release);
}

void ScreenshotJobs::supersede(const QString& path) {
	auto key = destinationKey(path);
	this->removals.remove(key);
	auto id = this->destinations.take(key);
	if (id > 0) this->cancel(id);
}

void ScreenshotJobs::discard(const QString& path) {
	auto key = destinationKey(path);
	this->supersede(key);
	if (removeIfPresent(key)) return;

	this->removals.insert(key, REMOVE_ATTEMPTS);
	if (!this->removalRetry.isActive()) this->removalRetry.start();
}

QString ScreenshotJobs::finish(int id, bool written) {
	auto job = this->jobs.take(id);
	if (job.id != id) return {};

	auto current = this->destinations.value(job.path) == id;
	if (current) this->destinations.remove(job.path);

	if (!written || !current || job.isCancelled()) {
		QFile::remove(job.partPath);
		return {};
	}

	if (!removeIfPresent(job.path) || !QFile::rename(job.partPath, job.path)) {
		qCWarning(logScreenshotJobs) << "Cannot move" << job.partPath << "to" << job.path;
		QFile::remove(job.partPath);
		return {};
	}

	return job.path;
}

bool ScreenshotJobs::write(const Job& job, const QImage& image, int quality) {
	if (job.isCancelled() || image.isNull()) return false;

	auto info = QFileInfo(job.partPath);
	if (!QDir().mkpath(info.absolutePath())) {
		qCWarning(logScreenshotJobs) << "Cannot create" << info.absolutePath();
		return false;
	}

	if (!image.save(job.partPath, "PNG", quality)) {
		qCWarning(logScreenshotJobs) << "Cannot write" << job.partPath;
		QFile::remove(job.partPath);
		return false;
	}

	return true;
}

void ScreenshotJobs::retryRemovals() {
	for (auto it = this->removals.begin(); it != this->removals.end();) {
		if (removeIfPresent(it.key())) {
			it = this->removals.erase(it);
		} else if (--it.value() <= 0) {
			qCWarning(logScreenshotJobs) << "Cannot remove" << it.key();
			it = this->removals.erase(it);
		} else {
			++it;
		}
	}

	if (!this->removals.isEmpty()) this->removalRetry.start();
}

} // namespace qs::windows::sys
