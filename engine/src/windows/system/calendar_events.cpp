#include "calendar_events.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <utility>

#include <qbytearray.h>
#include <qcoreapplication.h>
#include <qcryptographichash.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qjsvalue.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qmetatype.h>
#include <qnetworkaccessmanager.h>
#include <qnetworkreply.h>
#include <qnetworkrequest.h>
#include <qobject.h>
#include <qpointer.h>
#include <qregularexpression.h>
#include <qrunnable.h>
#include <qsavefile.h>
#include <qset.h>
#include <qthreadpool.h>
#include <qtimer.h>
#include <qurl.h>
#include <qvariant.h>

#include "../../core/logcat.hpp"
#include "calendar_ics.hpp"

namespace qs::windows::sys {

struct CalendarJobResult {
	std::shared_ptr<const ics::Calendar> calendar;
	QByteArray hash;
	QString error;
	QDateTime fileTime;
	bool unchanged = false;
	bool wroteCache = false;
	bool missing = false;
};

namespace {

QS_LOGGING_CATEGORY(logCalendar, "quickshell.windows.calendar", QtWarningMsg);

using L1 = QLatin1StringView;

constexpr qint64 MAX_BYTES = 10LL * 1024 * 1024;
constexpr int TIMEOUT_MS = 30000;
constexpr int MAX_REDIRECTS = 10;
constexpr int POOL_STOP_TIMEOUT_MS = 5000;
constexpr int MAX_TIMER_MINUTES = 7 * 24 * 60;
constexpr qint64 MAX_RETRY_WAIT_MS = 3600000;
constexpr auto RETRY_DELAYS_SECS = std::array<int, 5> {30, 60, 120, 300, 600};
constexpr const char* TIMED_OUT_PROPERTY = "qsCalendarTimedOut";
constexpr const char* TOO_LARGE_PROPERTY = "qsCalendarTooLarge";

enum class TargetKind : quint8 {
	Invalid,
	Network,
	File,
};

struct Target {
	TargetKind kind = TargetKind::Invalid;
	QUrl url;
	QString path;
};

Target classify(const QString& raw) {
	auto text = raw.trimmed();
	if (text.size() >= 2 && text.startsWith(u'"') && text.endsWith(u'"')) {
		text = text.mid(1, text.size() - 2).trimmed();
	}

	auto target = Target();
	if (text.isEmpty()) return target;

	for (auto scheme: {L1("webcal://"), L1("webcals://")}) {
		if (text.startsWith(scheme, Qt::CaseInsensitive)) {
			text = QStringLiteral("https://") + text.mid(scheme.size());
			break;
		}
	}

	if (text.startsWith(L1("https://"), Qt::CaseInsensitive)
	    || text.startsWith(L1("http://"), Qt::CaseInsensitive))
	{
		auto url = QUrl(text);
		if (url.isValid() && !url.host().isEmpty()) {
			target.kind = TargetKind::Network;
			target.url = url;
		}
		return target;
	}

	if (text.startsWith(L1("file:"), Qt::CaseInsensitive)) {
		auto path = QUrl(text).toLocalFile();
		if (!path.isEmpty()) {
			target.kind = TargetKind::File;
			target.path = path;
		}
		return target;
	}

	static const auto windowsPath = QRegularExpression(QStringLiteral("^([A-Za-z]:[\\\\/]|\\\\\\\\)"));
	if (windowsPath.match(text).hasMatch() || QDir::isAbsolutePath(text)) {
		target.kind = TargetKind::File;
		target.path = QDir::fromNativeSeparators(text);
	}
	return target;
}

QString describeTarget(const Target& target) {
	switch (target.kind) {
	case TargetKind::Network: return target.url.scheme() + QStringLiteral("://") + target.url.host();
	case TargetKind::File: return QStringLiteral("a local file");
	case TargetKind::Invalid: break;
	}
	return QStringLiteral("an unsupported address");
}

QString tooLargeMessage() { return QStringLiteral("The calendar is larger than 10 MB"); }

QString describeFailure(QNetworkReply::NetworkError error, int status) {
	if (status == 401 || status == 403) return QStringLiteral("Access denied (HTTP %1)").arg(status);
	if (status == 404 || status == 410) return QStringLiteral("Calendar not found (HTTP %1)").arg(status);
	if (status >= 500) return QStringLiteral("Server error (HTTP %1)").arg(status);
	if (status >= 300) return QStringLiteral("Request refused (HTTP %1)").arg(status);

	switch (error) {
	case QNetworkReply::HostNotFoundError: return QStringLiteral("Server not found");
	case QNetworkReply::ConnectionRefusedError: return QStringLiteral("Connection refused");
	case QNetworkReply::RemoteHostClosedError: return QStringLiteral("Connection closed by the server");
	case QNetworkReply::TimeoutError:
	case QNetworkReply::OperationCanceledError: return QStringLiteral("Timed out");
	case QNetworkReply::SslHandshakeFailedError: return QStringLiteral("Secure connection failed");
	case QNetworkReply::TemporaryNetworkFailureError:
	case QNetworkReply::NetworkSessionFailedError: return QStringLiteral("No network connection");
	case QNetworkReply::TooManyRedirectsError: return QStringLiteral("Too many redirects");
	case QNetworkReply::InsecureRedirectError:
		return QStringLiteral("Redirected to an insecure address");
	case QNetworkReply::ProtocolUnknownError: return QStringLiteral("Unsupported address");
	case QNetworkReply::ProxyConnectionRefusedError:
	case QNetworkReply::ProxyConnectionClosedError:
	case QNetworkReply::ProxyNotFoundError:
	case QNetworkReply::ProxyTimeoutError:
	case QNetworkReply::ProxyAuthenticationRequiredError: return QStringLiteral("Proxy error");
	default: break;
	}
	return QStringLiteral("Network error %1").arg(static_cast<int>(error));
}

bool transientFailure(QNetworkReply::NetworkError error, int status) {
	if (status >= 500 || status == 408 || status == 429) return true;
	if (status != 0) return false;
	switch (error) {
	case QNetworkReply::ProtocolUnknownError:
	case QNetworkReply::InsecureRedirectError:
	case QNetworkReply::TooManyRedirectsError: return false;
	default: return true;
	}
}

bool looksLikeCalendar(const QByteArray& body) {
	return body.left(65536).toUpper().contains("BEGIN:VCALENDAR");
}

CalendarJobResult runJob(
    QByteArray body,
    const QString& readPath,
    const QString& writePath,
    const QByteArray& currentHash,
    const QByteArray& cachedHash,
    const std::shared_ptr<std::atomic_bool>& cancel
) {
	auto result = CalendarJobResult();

	if (!readPath.isEmpty()) {
		auto file = QFile(readPath);
		if (!file.exists()) {
			result.missing = true;
			result.error = QStringLiteral("File not found");
			return result;
		}
		if (file.size() > MAX_BYTES) {
			result.error = tooLargeMessage();
			return result;
		}
		if (!file.open(QIODevice::ReadOnly)) {
			result.error = QStringLiteral("Couldn't open the file");
			return result;
		}
		body = file.read(MAX_BYTES + 1);
		if (body.size() > MAX_BYTES) {
			result.error = tooLargeMessage();
			return result;
		}
		result.fileTime = QFileInfo(file).lastModified();
	}

	if (!looksLikeCalendar(body)) {
		result.error = QStringLiteral("Not an iCalendar feed");
		return result;
	}

	result.hash = QCryptographicHash::hash(body, QCryptographicHash::Sha1);
	if (cancel->load()) return result;

	if (!currentHash.isEmpty() && result.hash == currentHash) {
		result.unchanged = true;
	} else {
		auto calendar = std::make_shared<const ics::Calendar>(ics::parse(body));
		if (!calendar->valid) {
			result.error = QStringLiteral("Not an iCalendar feed");
			return result;
		}
		result.calendar = calendar;
	}

	if (!writePath.isEmpty() && result.hash != cachedHash && !cancel->load()) {
		QDir().mkpath(QFileInfo(writePath).absolutePath());
		auto file = QSaveFile(writePath);
		if (file.open(QIODevice::WriteOnly) && file.write(body) == body.size() && file.commit()) {
			result.wroteCache = true;
		} else {
			qCDebug(logCalendar) << "Couldn't write the calendar cache";
		}
	}

	return result;
}

QVariantMap sourceFields(const QVariant& item) {
	if (item.metaType() == QMetaType::fromType<QJSValue>()) {
		return item.value<QJSValue>().toVariant().toMap();
	}

	if (item.metaType().flags().testFlag(QMetaType::PointerToQObject)) {
		auto* object = item.value<QObject*>();
		auto fields = QVariantMap();
		if (object == nullptr) return fields;
		for (const auto* name: {"id", "url", "enabled"}) {
			auto value = object->property(name);
			if (value.isValid()) fields.insert(QString::fromLatin1(name), value);
		}
		return fields;
	}

	return item.toMap();
}

} // namespace

CalendarEvents::CalendarEvents(QObject* parent): QObject(parent), mPool(new QThreadPool()) {
	this->mPool->setMaxThreadCount(1);

	this->mSyncTimer.setSingleShot(true);
	this->mSyncTimer.setInterval(0);
	QObject::connect(&this->mSyncTimer, &QTimer::timeout, this, &CalendarEvents::sync);

	this->mRetryTimer.setSingleShot(true);
	QObject::connect(&this->mRetryTimer, &QTimer::timeout, this, &CalendarEvents::retryDue);

	QObject::connect(&this->mRefreshTimer, &QTimer::timeout, this, &CalendarEvents::refresh);
}

CalendarEvents::~CalendarEvents() {
	for (auto& source: this->mSources) {
		if (auto* reply = source.reply.data()) QObject::disconnect(reply, nullptr, this, nullptr);
	}

	this->mPool->clear();
	if (!this->mPool->waitForDone(POOL_STOP_TIMEOUT_MS)) {
		qCWarning(logCalendar) << "A calendar job didn't finish in time";
		return;
	}

	delete this->mPool;
}

CalendarSource* CalendarEvents::find(const QString& id) {
	for (auto& source: this->mSources) {
		if (source.id == id) return &source;
	}
	return nullptr;
}

void CalendarEvents::setSources(const QVariantList& sources) {
	if (sources == this->mSourcesValue) return;
	this->mSourcesValue = sources;

	struct Entry {
		QString id;
		QString url;
		bool enabled = true;
	};

	auto entries = QList<Entry>();
	auto ids = QSet<QString>();
	for (const auto& item: sources) {
		auto fields = sourceFields(item);
		auto id = fields.value(QStringLiteral("id")).toString().trimmed();
		if (id.isEmpty() || ids.contains(id)) continue;
		ids.insert(id);
		entries.append(
		    Entry {
		        .id = id,
		        .url = fields.value(QStringLiteral("url")).toString().trimmed(),
		        .enabled = fields.value(QStringLiteral("enabled"), true).toBool(),
		    }
		);
	}

	auto dataChanged = false;
	auto statusDirty = false;

	for (auto& source: this->mSources) {
		if (!ids.contains(source.id)) this->abortFetch(source);
	}
	for (auto i = this->mSources.size() - 1; i >= 0; --i) {
		auto& source = this->mSources[i];
		if (ids.contains(source.id)) continue;
		if (source.calendar != nullptr && source.enabled) dataChanged = true;
		source.cancel->store(true);
		this->removeCache(source.id);
		this->mSources.removeAt(i);
		statusDirty = true;
	}

	auto ordered = QList<CalendarSource>();
	ordered.reserve(entries.size());
	for (const auto& entry: entries) {
		auto source = CalendarSource();
		auto* existing = this->find(entry.id);
		if (existing == nullptr) {
			source.id = entry.id;
			source.url = entry.url;
			source.enabled = entry.enabled;
			statusDirty = true;
			ordered.append(std::move(source));
			continue;
		}

		if (existing->url != entry.url) {
			this->abortFetch(*existing);
			if (existing->calendar != nullptr && existing->enabled) dataChanged = true;
			this->invalidate(*existing);
			existing->url = entry.url;
			statusDirty = true;
		}

		if (existing->enabled != entry.enabled) {
			if (!entry.enabled) {
				this->abortFetch(*existing);
				existing->retryAt = 0;
			}
			existing->enabled = entry.enabled;
			if (existing->calendar != nullptr) dataChanged = true;
			statusDirty = true;
		}

		ordered.append(std::move(*existing));
	}
	this->mSources = std::move(ordered);

	emit this->sourcesChanged();
	if (dataChanged) this->bumpRevision();
	if (statusDirty) emit this->statusChanged();
	this->scheduleRetry();
	this->scheduleSync();
	this->updateLoading();
}

void CalendarEvents::setActive(bool active) {
	if (active == this->mActive) return;
	this->mActive = active;

	if (active) {
		this->scheduleSync();
	} else {
		this->mSyncTimer.stop();
		this->mRetryTimer.stop();
		for (auto& source: this->mSources) this->abortFetch(source);
	}

	this->updateTimers();
	this->updateLoading();
	emit this->activeChanged();
	emit this->statusChanged();
}

void CalendarEvents::setRefreshMinutes(int minutes) {
	minutes = std::max(0, minutes);
	if (minutes == this->mRefreshMinutes) return;
	this->mRefreshMinutes = minutes;
	this->updateTimers();
	emit this->refreshMinutesChanged();
}

void CalendarEvents::setCacheDir(const QString& dir) {
	auto path = dir.trimmed();
	if (path.startsWith(L1("file:"), Qt::CaseInsensitive)) path = QUrl(path).toLocalFile();
	if (path == this->mCacheDir) return;
	this->mCacheDir = path;

	for (auto& source: this->mSources) {
		source.cachedHash.clear();
		if (source.calendar == nullptr) source.cacheTried = false;
	}

	emit this->cacheDirChanged();
	this->scheduleSync();
}

QVariantMap CalendarEvents::status() const {
	auto result = QVariantMap();
	for (const auto& source: this->mSources) {
		result.insert(
		    source.id,
		    QVariantMap {
		        {QStringLiteral("ok"), source.ok},
		        {QStringLiteral("error"), source.error},
		        {QStringLiteral("updated"), source.updated},
		        {QStringLiteral("events"),
		         source.calendar != nullptr ? static_cast<int>(source.calendar->events.size()) : 0},
		        {QStringLiteral("name"), source.calendar != nullptr ? source.calendar->name : QString()},
		        {QStringLiteral("loading"), !source.reply.isNull() || source.pending > 0},
		    }
		);
	}
	return result;
}

void CalendarEvents::refresh() {
	if (!this->mActive) return;
	for (auto& source: this->mSources) {
		if (source.enabled) this->fetch(source);
	}
	this->updateLoading();
	emit this->statusChanged();
}

QVariantList CalendarEvents::eventsBetween(const QDateTime& from, const QDateTime& to) const {
	struct Item {
		const CalendarSource* source = nullptr;
		ics::Occurrence occurrence;
	};

	auto items = QList<Item>();
	for (const auto& source: this->mSources) {
		if (!source.enabled || source.calendar == nullptr) continue;
		for (auto& occurrence: ics::occurrences(*source.calendar, from, to)) {
			items.append(Item {.source = &source, .occurrence = std::move(occurrence)});
		}
	}

	std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
		if (a.occurrence.start != b.occurrence.start) return a.occurrence.start < b.occurrence.start;
		auto byTitle =
		    QString::compare(a.occurrence.event->title, b.occurrence.event->title, Qt::CaseInsensitive);
		if (byTitle != 0) return byTitle < 0;
		return a.occurrence.end < b.occurrence.end;
	});

	auto result = QVariantList();
	result.reserve(items.size());
	for (const auto& item: items) {
		const auto& event = *item.occurrence.event;
		result.append(
		    QVariantMap {
		        {QStringLiteral("sourceId"), item.source->id},
		        {QStringLiteral("uid"), event.uid},
		        {QStringLiteral("title"), event.title},
		        {QStringLiteral("location"), event.location},
		        {QStringLiteral("description"), event.description},
		        {QStringLiteral("start"), item.occurrence.start},
		        {QStringLiteral("end"), item.occurrence.end},
		        {QStringLiteral("allDay"), event.allDay},
		        {QStringLiteral("recurring"), event.recurring},
		    }
		);
	}
	return result;
}

QVariantList CalendarEvents::daysWithEvents(int year, int month) const {
	if (month < 1 || month > 12) return {};
	auto first = QDate(year, month, 1);
	if (!first.isValid()) return {};
	auto last = QDate(year, month, first.daysInMonth());
	auto from = first.startOfDay();
	auto to = first.addMonths(1).startOfDay();

	auto days = QList<QStringList>(first.daysInMonth());
	for (const auto& source: this->mSources) {
		if (!source.enabled || source.calendar == nullptr) continue;
		for (const auto& occurrence: ics::occurrences(*source.calendar, from, to)) {
			auto begin = std::max(occurrence.firstDay, first);
			auto end = std::min(occurrence.lastDay, last);
			for (auto date = begin; date <= end; date = date.addDays(1)) {
				auto& ids = days[first.daysTo(date)];
				if (!ids.contains(source.id)) ids.append(source.id);
			}
		}
	}

	auto result = QVariantList();
	for (qsizetype i = 0; i < days.size(); ++i) {
		if (days.at(i).isEmpty()) continue;
		result.append(
		    QVariantMap {
		        {QStringLiteral("day"), static_cast<int>(i + 1)},
		        {QStringLiteral("sourceIds"), days.at(i)},
		    }
		);
	}
	return result;
}

void CalendarEvents::scheduleSync() {
	if (this->mActive && !this->mSyncTimer.isActive()) this->mSyncTimer.start();
}

void CalendarEvents::sync() {
	if (!this->mActive) return;

	auto now = QDateTime::currentMSecsSinceEpoch();
	auto interval = qint64(this->mRefreshMinutes) * 60000;
	for (auto& source: this->mSources) {
		if (!source.enabled) continue;
		if (source.calendar == nullptr && !source.cacheTried) this->loadCache(source);
		auto due = source.lastAttempt == 0 || (interval > 0 && now - source.lastAttempt >= interval);
		if (due) this->fetch(source);
	}

	this->updateTimers();
	this->updateLoading();
	emit this->statusChanged();
}

void CalendarEvents::loadCache(CalendarSource& source) {
	source.cacheTried = true;
	auto path = this->cachePath(source.id);
	if (path.isEmpty() || classify(source.url).kind != TargetKind::Network) return;
	if (!QFile::exists(path)) return;
	this->startJob(source, JobKind::Cache, {}, path);
}

void CalendarEvents::fetch(CalendarSource& source) {
	if (!this->mActive || !source.enabled || !source.reply.isNull()) return;

	auto target = classify(source.url);
	if (target.kind == TargetKind::File && source.pending > 0) return;

	source.lastAttempt = QDateTime::currentMSecsSinceEpoch();
	source.retryAt = 0;

	if (target.kind == TargetKind::Invalid) {
		this->fail(source, QStringLiteral("Unsupported address"), false);
		return;
	}

	if (target.kind == TargetKind::File) {
		this->startJob(source, JobKind::File, {}, target.path);
		return;
	}

	if (this->mNam == nullptr) this->mNam = new QNetworkAccessManager(this);

	auto request = QNetworkRequest(target.url);
	request.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("illogical-impulse calendar"));
	request.setRawHeader("Accept", "text/calendar, text/plain;q=0.9, */*;q=0.8");
	request.setAttribute(
	    QNetworkRequest::RedirectPolicyAttribute,
	    QNetworkRequest::NoLessSafeRedirectPolicy
	);
	request.setMaximumRedirectsAllowed(MAX_REDIRECTS);
	if (!source.etag.isEmpty()) request.setRawHeader("If-None-Match", source.etag);
	if (!source.lastModified.isEmpty()) request.setRawHeader("If-Modified-Since", source.lastModified);

	qCDebug(logCalendar).noquote() << "Fetching a calendar from" << describeTarget(target);

	auto* reply = this->mNam->get(request);
	source.reply = reply;

	auto* timeout = new QTimer(reply);
	timeout->setSingleShot(true);
	QObject::connect(timeout, &QTimer::timeout, reply, [reply]() {
		reply->setProperty(TIMED_OUT_PROPERTY, true);
		reply->abort();
	});
	timeout->start(TIMEOUT_MS);

	QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [reply]() {
		auto length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
		if (length > MAX_BYTES && reply->isRunning()) {
			reply->setProperty(TOO_LARGE_PROPERTY, true);
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
		if (received > MAX_BYTES && reply->isRunning()) {
			reply->setProperty(TOO_LARGE_PROPERTY, true);
			reply->abort();
		}
	});

	auto id = source.id;
	QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
		this->onReply(reply, id);
	});
}

void CalendarEvents::abortFetch(CalendarSource& source) {
	auto* reply = source.reply.data();
	if (reply == nullptr) return;
	source.reply.clear();
	source.lastAttempt = 0;
	reply->abort();
}

void CalendarEvents::onReply(QNetworkReply* reply, const QString& id) {
	reply->deleteLater();
	auto* source = this->find(id);
	if (source == nullptr || source->reply.data() != reply) return;
	source->reply.clear();

	auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (reply->property(TOO_LARGE_PROPERTY).toBool()) {
		this->fail(*source, tooLargeMessage(), false);
	} else if (reply->property(TIMED_OUT_PROPERTY).toBool()) {
		this->fail(*source, QStringLiteral("Timed out"), true);
	} else if (status == 304) {
		this->succeed(*source);
	} else if (reply->error() != QNetworkReply::NoError || (status != 0 && (status < 200 || status >= 300)))
	{
		this->fail(*source, describeFailure(reply->error(), status), transientFailure(reply->error(), status));
	} else {
		auto body = reply->readAll();
		if (body.size() > MAX_BYTES) {
			this->fail(*source, tooLargeMessage(), false);
		} else {
			source->etag = reply->rawHeader("ETag");
			source->lastModified = reply->rawHeader("Last-Modified");
			this->startJob(*source, JobKind::Network, body, {});
		}
	}

	this->updateLoading();
}

void CalendarEvents::startJob(
    CalendarSource& source,
    JobKind kind,
    const QByteArray& body,
    const QString& readPath
) {
	auto writePath = kind == JobKind::Network ? this->cachePath(source.id) : QString();
	auto currentHash = kind == JobKind::Cache ? QByteArray() : source.bodyHash;
	auto cachedHash = source.cachedHash;
	auto cancel = source.cancel;
	auto id = source.id;
	auto generation = source.generation;
	auto guard = QPointer<CalendarEvents>(this);
	++source.pending;

	auto* task = QRunnable::create(
	    [guard, id, generation, kind, body, readPath, writePath, currentHash, cachedHash, cancel]() {
		    auto result = runJob(body, readPath, writePath, currentHash, cachedHash, cancel);
		    QMetaObject::invokeMethod(
		        QCoreApplication::instance(),
		        [guard, id, generation, kind, result]() {
			        auto* self = guard.data();
			        if (self == nullptr) return;
			        self->finishJob(id, generation, kind, result);
		        },
		        Qt::QueuedConnection
		    );
	    }
	);
	this->mPool->start(task);
	this->updateLoading();
}

void CalendarEvents::finishJob(
    const QString& id,
    quint64 generation,
    JobKind kind,
    const CalendarJobResult& result
) {
	auto* source = this->find(id);
	if (source == nullptr || source->generation != generation) return;
	source->pending = std::max(0, source->pending - 1);
	if (result.wroteCache) source->cachedHash = result.hash;

	if (kind == JobKind::Cache) {
		if (result.calendar != nullptr && !source->fresh && source->calendar == nullptr) {
			source->calendar = result.calendar;
			source->bodyHash = result.hash;
			source->cachedHash = result.hash;
			if (source->error.isEmpty()) source->ok = true;
			if (!source->updated.isValid()) source->updated = result.fileTime;
			if (source->enabled) this->bumpRevision();
		} else if (!result.error.isEmpty()) {
			qCDebug(logCalendar) << "Ignoring an unreadable calendar cache:" << result.error;
		}
		this->updateLoading();
		emit this->statusChanged();
		return;
	}

	if (!result.error.isEmpty()) {
		this->fail(*source, result.error, false);
		this->updateLoading();
		return;
	}

	source->fresh = true;
	auto changed = false;
	if (!result.unchanged && result.calendar != nullptr) {
		source->calendar = result.calendar;
		source->bodyHash = result.hash;
		changed = source->enabled;
	}

	this->succeed(*source);
	if (changed) this->bumpRevision();
	this->updateLoading();
}

void CalendarEvents::succeed(CalendarSource& source) {
	source.ok = true;
	source.error.clear();
	source.updated = QDateTime::currentDateTime();
	source.failures = 0;
	source.retryAt = 0;
	this->scheduleRetry();
	emit this->statusChanged();
}

void CalendarEvents::fail(CalendarSource& source, const QString& error, bool transient) {
	source.ok = false;
	source.error = error;
	source.etag.clear();
	source.lastModified.clear();

	qCInfo(logCalendar).noquote() << "Couldn't load the calendar from"
	                              << describeTarget(classify(source.url)) << "-" << error;

	if (transient && this->mActive && source.enabled) {
		auto index = std::min<qsizetype>(source.failures, RETRY_DELAYS_SECS.size() - 1);
		source.failures += 1;
		source.retryAt = QDateTime::currentMSecsSinceEpoch() + qint64(RETRY_DELAYS_SECS.at(index)) * 1000;
	} else {
		source.retryAt = 0;
	}

	this->scheduleRetry();
	emit this->statusChanged();
}

void CalendarEvents::invalidate(CalendarSource& source) {
	source.cancel->store(true);
	source.cancel = std::make_shared<std::atomic_bool>(false);
	source.generation += 1;
	source.calendar.reset();
	source.bodyHash.clear();
	source.cachedHash.clear();
	source.etag.clear();
	source.lastModified.clear();
	source.pending = 0;
	source.cacheTried = false;
	source.fresh = false;
	source.ok = false;
	source.error.clear();
	source.updated = QDateTime();
	source.lastAttempt = 0;
	source.retryAt = 0;
	source.failures = 0;
	this->removeCache(source.id);
}

void CalendarEvents::removeCache(const QString& id) const {
	auto path = this->cachePath(id);
	if (!path.isEmpty() && QFile::exists(path)) QFile::remove(path);
}

void CalendarEvents::scheduleRetry() {
	if (!this->mActive) {
		this->mRetryTimer.stop();
		return;
	}

	auto next = std::numeric_limits<qint64>::max();
	for (const auto& source: this->mSources) {
		if (source.enabled && source.retryAt > 0) next = std::min(next, source.retryAt);
	}
	if (next == std::numeric_limits<qint64>::max()) {
		this->mRetryTimer.stop();
		return;
	}

	auto wait = std::clamp<qint64>(next - QDateTime::currentMSecsSinceEpoch(), 0, MAX_RETRY_WAIT_MS);
	this->mRetryTimer.start(static_cast<int>(wait));
}

void CalendarEvents::retryDue() {
	if (!this->mActive) return;
	auto now = QDateTime::currentMSecsSinceEpoch();
	for (auto& source: this->mSources) {
		if (source.enabled && source.retryAt > 0 && source.retryAt <= now + 1000) this->fetch(source);
	}
	this->scheduleRetry();
	this->updateLoading();
	emit this->statusChanged();
}

void CalendarEvents::updateTimers() {
	if (!this->mActive || this->mRefreshMinutes <= 0) {
		this->mRefreshTimer.stop();
		return;
	}

	auto interval = std::min(this->mRefreshMinutes, MAX_TIMER_MINUTES) * 60000;
	if (!this->mRefreshTimer.isActive() || this->mRefreshTimer.interval() != interval) {
		this->mRefreshTimer.start(interval);
	}
}

void CalendarEvents::updateLoading() {
	auto loading = std::ranges::any_of(this->mSources, [](const CalendarSource& source) {
		return !source.reply.isNull() || source.pending > 0;
	});
	if (loading == this->mLoading) return;
	this->mLoading = loading;
	emit this->loadingChanged();
}

void CalendarEvents::bumpRevision() {
	this->mRevision += 1;
	emit this->revisionChanged();
}

QString CalendarEvents::cachePath(const QString& id) const {
	if (this->mCacheDir.isEmpty()) return {};
	auto hash = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha1).toHex();
	return QDir(this->mCacheDir).filePath(QString::fromLatin1(hash) + QStringLiteral(".ics"));
}

} // namespace qs::windows::sys
