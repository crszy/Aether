#include <qcolor.h>
#include <qcoreapplication.h>
#include <qcoreevent.h>
#include <qdir.h>
#include <qfile.h>
#include <qfiledevice.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qimage.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qscopeguard.h>
#include <qsemaphore.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtemporarydir.h>
#include <qtest.h>
#include <qtestcase.h>
#include <qthreadpool.h>

#include "screenshot_jobs.hpp"

using qs::windows::sys::ScreenshotJobs;

namespace {

constexpr int QUALITY = 85;

QImage filled(QColor color) {
	auto image = QImage(48, 32, QImage::Format_RGB32);
	image.fill(color);
	return image;
}

QColor colorAt(const QString& path) {
	auto image = QImage(path);
	if (image.isNull()) return {};
	return image.pixelColor(0, 0);
}

QStringList filesIn(const QTemporaryDir& dir) {
	return QDir(dir.path()).entryList(QDir::Files | QDir::Hidden, QDir::Name);
}

QString publish(ScreenshotJobs& jobs, const QString& path, QColor color) {
	auto job = jobs.start(path);
	return jobs.finish(job.id, ScreenshotJobs::write(job, filled(color), QUALITY));
}

void setWritable(const QString& dir, bool writable) {
	auto permissions = QFileDevice::ReadOwner | QFileDevice::ExeOwner;
	if (writable) permissions |= QFileDevice::WriteOwner;
	QFile::setPermissions(dir, permissions);
}

} // namespace

class TestScreenshotJobs: public QObject {
	Q_OBJECT;

private slots:
	void staleWriterReleasedLast();
	void cancelledWriterReleasedLast();
	void cancelledBeforeStart();
	void cancelledAfterWrite();
	void separateDestinations();
	void discardPending();
	void discardPublished();
	void discardRetriesWhileInUse();
	void newRequestStopsRemoval();
	void directWriteSupersedes();
	void unknownRequest();

private:
	void releasedLast(bool cancelOld);
};

void TestScreenshotJobs::releasedLast(bool cancelOld) {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto path = dir.filePath("image-DISPLAY1");

	ScreenshotJobs jobs;
	QHash<int, QString> published;
	QSemaphore oldWritten;
	QSemaphore releaseOld;
	auto released = false;
	auto drain = qScopeGuard([&]() {
		if (!released) releaseOld.release();
		QThreadPool::globalInstance()->waitForDone();
		QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
	});

	auto oldJob = jobs.start(path);
	QThreadPool::globalInstance()->start([&, oldJob]() {
		auto ok = ScreenshotJobs::write(oldJob, filled(Qt::red), QUALITY);
		oldWritten.release();
		releaseOld.acquire();
		QMetaObject::invokeMethod(
		    this,
		    [&, oldJob, ok]() { published.insert(oldJob.id, jobs.finish(oldJob.id, ok)); },
		    Qt::QueuedConnection
		);
	});

	oldWritten.acquire();
	QVERIFY(QFileInfo::exists(oldJob.partPath));
	if (cancelOld) jobs.cancel(oldJob.id);

	auto newJob = jobs.start(path);
	QVERIFY(newJob.partPath != oldJob.partPath);
	QThreadPool::globalInstance()->start([&, newJob]() {
		auto ok = ScreenshotJobs::write(newJob, filled(Qt::blue), QUALITY);
		QMetaObject::invokeMethod(
		    this,
		    [&, newJob, ok]() { published.insert(newJob.id, jobs.finish(newJob.id, ok)); },
		    Qt::QueuedConnection
		);
	});

	QTRY_VERIFY(published.contains(newJob.id));
	QCOMPARE(published.value(newJob.id), QFileInfo(path).absoluteFilePath());
	QCOMPARE(colorAt(path), QColor(Qt::blue));

	releaseOld.release();
	released = true;
	QTRY_VERIFY(published.contains(oldJob.id));
	QVERIFY(published.value(oldJob.id).isEmpty());
	QCOMPARE(colorAt(path), QColor(Qt::blue));
	QCOMPARE(filesIn(dir), QStringList {"image-DISPLAY1"});
}

void TestScreenshotJobs::staleWriterReleasedLast() { this->releasedLast(false); }

void TestScreenshotJobs::cancelledWriterReleasedLast() { this->releasedLast(true); }

void TestScreenshotJobs::cancelledBeforeStart() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto path = dir.filePath("image-DISPLAY1");

	ScreenshotJobs jobs;
	auto job = jobs.start(path);
	jobs.cancel(job.id);

	QVERIFY(!ScreenshotJobs::write(job, filled(Qt::red), QUALITY));
	QVERIFY(!QFileInfo::exists(job.partPath));
	QVERIFY(jobs.finish(job.id, false).isEmpty());
	QCOMPARE(filesIn(dir), QStringList());
}

void TestScreenshotJobs::cancelledAfterWrite() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto path = dir.filePath("image-DISPLAY1");

	ScreenshotJobs jobs;
	auto job = jobs.start(path);
	QVERIFY(ScreenshotJobs::write(job, filled(Qt::red), QUALITY));
	QVERIFY(QFileInfo::exists(job.partPath));
	QVERIFY(!QFileInfo::exists(path));

	jobs.cancel(job.id);
	QVERIFY(jobs.finish(job.id, true).isEmpty());
	QCOMPARE(filesIn(dir), QStringList());
}

void TestScreenshotJobs::separateDestinations() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto first = dir.filePath("image-DISPLAY1-a");
	auto second = dir.filePath("image-DISPLAY1-b");

	ScreenshotJobs jobs;
	auto firstJob = jobs.start(first);
	auto secondJob = jobs.start(second);
	QVERIFY(ScreenshotJobs::write(secondJob, filled(Qt::green), QUALITY));
	QVERIFY(ScreenshotJobs::write(firstJob, filled(Qt::red), QUALITY));

	QCOMPARE(jobs.finish(secondJob.id, true), QFileInfo(second).absoluteFilePath());
	QCOMPARE(jobs.finish(firstJob.id, true), QFileInfo(first).absoluteFilePath());
	QCOMPARE(colorAt(first), QColor(Qt::red));
	QCOMPARE(colorAt(second), QColor(Qt::green));
	QCOMPARE(filesIn(dir), (QStringList {"image-DISPLAY1-a", "image-DISPLAY1-b"}));
}

void TestScreenshotJobs::discardPending() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto path = dir.filePath("image-DISPLAY1");

	ScreenshotJobs jobs;
	auto job = jobs.start(path);
	jobs.discard(path);

	QVERIFY(job.isCancelled());
	QVERIFY(!ScreenshotJobs::write(job, filled(Qt::red), QUALITY));
	QVERIFY(jobs.finish(job.id, false).isEmpty());
	QCOMPARE(filesIn(dir), QStringList());
}

void TestScreenshotJobs::discardPublished() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto path = dir.filePath("image-DISPLAY1");

	ScreenshotJobs jobs;
	QVERIFY(!publish(jobs, path, Qt::red).isEmpty());
	QVERIFY(QFileInfo::exists(path));

	jobs.discard(path);
	QVERIFY(!QFileInfo::exists(path));
	QCOMPARE(filesIn(dir), QStringList());
}

void TestScreenshotJobs::discardRetriesWhileInUse() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto path = dir.filePath("image-DISPLAY1");

	ScreenshotJobs jobs;
	QVERIFY(!publish(jobs, path, Qt::red).isEmpty());

	setWritable(dir.path(), false);
	jobs.discard(path);
	auto blocked = QFileInfo::exists(path);
	setWritable(dir.path(), true);
	if (!blocked) QSKIP("Removal is not blocked by a read-only directory here");

	QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(path), 5000);
	QCOMPARE(filesIn(dir), QStringList());
}

void TestScreenshotJobs::newRequestStopsRemoval() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto path = dir.filePath("image-DISPLAY1");

	ScreenshotJobs jobs;
	QVERIFY(!publish(jobs, path, Qt::red).isEmpty());

	setWritable(dir.path(), false);
	jobs.discard(path);
	auto blocked = QFileInfo::exists(path);
	setWritable(dir.path(), true);
	if (!blocked) QSKIP("Removal is not blocked by a read-only directory here");

	QVERIFY(!publish(jobs, path, Qt::blue).isEmpty());
	QTest::qWait(2500);
	QCOMPARE(colorAt(path), QColor(Qt::blue));
	QCOMPARE(filesIn(dir), QStringList {"image-DISPLAY1"});
}

void TestScreenshotJobs::directWriteSupersedes() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	auto path = dir.filePath("image-DISPLAY1");

	ScreenshotJobs jobs;
	auto job = jobs.start(path);
	QVERIFY(ScreenshotJobs::write(job, filled(Qt::red), QUALITY));

	jobs.supersede(path);
	QVERIFY(filled(Qt::green).save(path, "PNG"));

	QVERIFY(jobs.finish(job.id, true).isEmpty());
	QCOMPARE(colorAt(path), QColor(Qt::green));
	QCOMPARE(filesIn(dir), QStringList {"image-DISPLAY1"});
}

void TestScreenshotJobs::unknownRequest() {
	ScreenshotJobs jobs;
	jobs.cancel(42);
	QVERIFY(jobs.finish(42, true).isEmpty());
}

QTEST_GUILESS_MAIN(TestScreenshotJobs)

#include "screenshot_jobs_test.moc"
