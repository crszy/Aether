#pragma once

#include <atomic>
#include <memory>

#include <qbytearray.h>
#include <qdatetime.h>
#include <qlist.h>
#include <qnetworkreply.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

class QNetworkAccessManager;
class QThreadPool;

namespace qs::windows::sys {

namespace ics {
struct Calendar;
}

struct CalendarJobResult;

struct CalendarSource {
	QString id;
	QString url;
	bool enabled = true;
	std::shared_ptr<const ics::Calendar> calendar;
	std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
	QByteArray bodyHash;
	QByteArray cachedHash;
	QByteArray etag;
	QByteArray lastModified;
	QPointer<QNetworkReply> reply;
	quint64 generation = 0;
	int pending = 0;
	bool cacheTried = false;
	bool fresh = false;
	bool ok = false;
	QString error;
	QDateTime updated;
	qint64 lastAttempt = 0;
	qint64 retryAt = 0;
	int failures = 0;
};

class CalendarEvents: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	Q_PROPERTY(QVariantList sources READ sources WRITE setSources NOTIFY sourcesChanged);
	Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged);
	Q_PROPERTY(
	    int refreshMinutes READ refreshMinutes WRITE setRefreshMinutes NOTIFY refreshMinutesChanged
	);
	Q_PROPERTY(QString cacheDir READ cacheDir WRITE setCacheDir NOTIFY cacheDirChanged);
	Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged);
	Q_PROPERTY(int revision READ revision NOTIFY revisionChanged);
	Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged);

public:
	explicit CalendarEvents(QObject* parent = nullptr);
	~CalendarEvents() override;
	Q_DISABLE_COPY_MOVE(CalendarEvents);

	[[nodiscard]] QVariantList sources() const { return this->mSourcesValue; }
	void setSources(const QVariantList& sources);

	[[nodiscard]] bool active() const { return this->mActive; }
	void setActive(bool active);

	[[nodiscard]] int refreshMinutes() const { return this->mRefreshMinutes; }
	void setRefreshMinutes(int minutes);

	[[nodiscard]] QString cacheDir() const { return this->mCacheDir; }
	void setCacheDir(const QString& dir);

	[[nodiscard]] bool loading() const { return this->mLoading; }
	[[nodiscard]] int revision() const { return this->mRevision; }
	[[nodiscard]] QVariantMap status() const;

	Q_INVOKABLE void refresh();
	Q_INVOKABLE [[nodiscard]] QVariantList eventsBetween(const QDateTime& from, const QDateTime& to) const;
	Q_INVOKABLE [[nodiscard]] QVariantList daysWithEvents(int year, int month) const;

signals:
	void sourcesChanged();
	void activeChanged();
	void refreshMinutesChanged();
	void cacheDirChanged();
	void loadingChanged();
	void revisionChanged();
	void statusChanged();

private:
	enum class JobKind : quint8 {
		Cache,
		Network,
		File,
	};

	CalendarSource* find(const QString& id);
	void scheduleSync();
	void sync();
	void loadCache(CalendarSource& source);
	void fetch(CalendarSource& source);
	void abortFetch(CalendarSource& source);
	void onReply(QNetworkReply* reply, const QString& id);
	void startJob(CalendarSource& source, JobKind kind, const QByteArray& body, const QString& readPath);
	void finishJob(
	    const QString& id,
	    quint64 generation,
	    JobKind kind,
	    const CalendarJobResult& result
	);
	void succeed(CalendarSource& source);
	void fail(CalendarSource& source, const QString& error, bool transient);
	void invalidate(CalendarSource& source);
	void removeCache(const QString& id) const;
	void scheduleRetry();
	void retryDue();
	void updateTimers();
	void updateLoading();
	void bumpRevision();
	[[nodiscard]] QString cachePath(const QString& id) const;

	QVariantList mSourcesValue;
	QList<CalendarSource> mSources;
	QNetworkAccessManager* mNam = nullptr;
	QThreadPool* mPool = nullptr;
	QTimer mRefreshTimer;
	QTimer mRetryTimer;
	QTimer mSyncTimer;
	QString mCacheDir;
	int mRefreshMinutes = 30;
	int mRevision = 0;
	bool mActive = false;
	bool mLoading = false;
};

} // namespace qs::windows::sys
