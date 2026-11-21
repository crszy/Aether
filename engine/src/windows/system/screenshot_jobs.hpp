#pragma once

#include <atomic>
#include <memory>

#include <qhash.h>
#include <qimage.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>

namespace qs::windows::sys {

class ScreenshotJobs {
public:
	struct Job {
		int id = -1;
		QString path;
		QString partPath;
		std::shared_ptr<std::atomic_bool> cancelled;

		[[nodiscard]] bool isCancelled() const;
	};

	ScreenshotJobs();
	~ScreenshotJobs() = default;
	Q_DISABLE_COPY_MOVE(ScreenshotJobs);

	Job start(const QString& path);
	void cancel(int id);
	void supersede(const QString& path);
	void discard(const QString& path);
	QString finish(int id, bool written);

	static bool write(const Job& job, const QImage& image, int quality);

private:
	void retryRemovals();

	QHash<int, Job> jobs;
	QHash<QString, int> destinations;
	QHash<QString, int> removals;
	QTimer removalRetry;
	int nextId = 1;
};

} // namespace qs::windows::sys
