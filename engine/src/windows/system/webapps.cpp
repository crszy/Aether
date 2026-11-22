#include "webapps.hpp"

#include <algorithm>

#include <qbytearray.h>
#include <qcoreapplication.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qimage.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qnetworkreply.h>
#include <qnetworkrequest.h>
#include <qpainter.h>
#include <qpointer.h>
#include <qprocess.h>
#include <qregularexpression.h>
#include <qrunnable.h>
#include <qset.h>
#include <qstandardpaths.h>
#include <qthreadpool.h>
#include <qtimer.h>
#include <qurl.h>

#include <qt_windows.h>

#include "../../core/desktopentry.hpp"
#include "registry_util.hpp"
#include "shell_link.hpp"

namespace qs::windows::sys {

namespace {
Q_LOGGING_CATEGORY(logWebApps, "quickshell.windows.webapps", QtWarningMsg);

const auto USER_AGENT =
    QByteArrayLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                      "(KHTML, like Gecko) Chrome/150.0 Safari/537.36");

QString normalizeUrl(const QString& raw) {
	auto trimmed = raw;
	trimmed.remove(QRegularExpression(QStringLiteral("\\s+")));
	if (trimmed.isEmpty()) return QString();
	if (!trimmed.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
	    && !trimmed.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
	{
		trimmed = QStringLiteral("https://") + trimmed;
	}
	return trimmed;
}

bool parseUrlParts(const QString& url, QString& slug, QString& label, QString& host) {
	auto parsed = QUrl(url);
	host = parsed.host();
	if (host.isEmpty()) return false;
	if (!host.contains(u'.') && host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) != 0) {
		return false;
	}

	auto path = parsed.path();
	if (path.isEmpty()) path = QStringLiteral("/");

	auto bare = host;
	if (bare.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)) bare = bare.mid(4);

	auto slugSource = (bare + path).toLower();
	slugSource.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
	while (slugSource.startsWith(u'-')) slugSource.remove(0, 1);
	while (slugSource.endsWith(u'-')) slugSource.chop(1);
	slug = slugSource;
	if (slug.isEmpty()) slug = QStringLiteral("webapp");

	auto parts = bare.split(u'.', Qt::SkipEmptyParts);
	static const QSet<QString> secondLevelTlds = {
	    QStringLiteral("co"),
	    QStringLiteral("com"),
	    QStringLiteral("org"),
	    QStringLiteral("net"),
	    QStringLiteral("gov"),
	    QStringLiteral("edu"),
	    QStringLiteral("ac"),
	};

	if (bare.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0 || parts.size() < 2) {
		label = parts.isEmpty() ? bare : parts.constFirst();
	} else if (parts.size() >= 3 && parts.constLast().length() == 2
	           && secondLevelTlds.contains(parts.at(parts.size() - 2).toLower()))
	{
		label = parts.at(parts.size() - 3);
	} else {
		label = parts.at(parts.size() - 2);
	}

	if (!label.isEmpty()) label[0] = label.at(0).toUpper();
	return true;
}

QHash<QString, QString> tagAttrs(const QString& tag) {
	auto result = QHash<QString, QString>();
	static const QRegularExpression attrRe(
	    QStringLiteral(R"re(([\w:-]+)\s*=\s*"([^"]*)"|([\w:-]+)\s*=\s*'([^']*)')re")
	);

	auto it = attrRe.globalMatch(tag);
	while (it.hasNext()) {
		auto match = it.next();
		if (!match.captured(1).isEmpty()) {
			result.insert(match.captured(1).toLower(), match.captured(2));
		} else {
			result.insert(match.captured(3).toLower(), match.captured(4));
		}
	}

	return result;
}

int sizeFromAttr(const QString& sizes) {
	static const QRegularExpression sizeRe(QStringLiteral(R"(^(\d+)x\d+)"));
	auto match = sizeRe.match(sizes.split(u' ').value(0));
	return match.hasMatch() ? match.captured(1).toInt() : 0;
}

struct PageInfo {
	QString name;
	QString manifestUrl;
	QList<QPair<int, QString>> icons;
};

PageInfo parsePageHtml(const QString& html, const QUrl& baseUrl) {
	auto info = PageInfo();

	static const QRegularExpression metaRe(
	    QStringLiteral(R"(<meta\b[^>]*>)"),
	    QRegularExpression::CaseInsensitiveOption
	);
	static const QRegularExpression linkRe(
	    QStringLiteral(R"(<link\b[^>]*>)"),
	    QRegularExpression::CaseInsensitiveOption
	);

	auto metaIt = metaRe.globalMatch(html);
	while (metaIt.hasNext()) {
		auto attrs = tagAttrs(metaIt.next().captured(0));
		auto key = attrs.value(QStringLiteral("property"), attrs.value(QStringLiteral("name"))).toLower();
		if ((key == QStringLiteral("og:site_name") || key == QStringLiteral("application-name"))
		    && info.name.isEmpty())
		{
			auto content = attrs.value(QStringLiteral("content")).trimmed();
			if (!content.isEmpty()) info.name = content;
		}
	}

	auto linkIt = linkRe.globalMatch(html);
	while (linkIt.hasNext()) {
		auto attrs = tagAttrs(linkIt.next().captured(0));
		auto rel = attrs.value(QStringLiteral("rel")).toLower();
		auto href = attrs.value(QStringLiteral("href"));
		if (href.isEmpty()) continue;

		auto absolute = baseUrl.resolved(QUrl(href)).toString();
		if (rel.contains(QStringLiteral("manifest"))) {
			if (info.manifestUrl.isEmpty()) info.manifestUrl = absolute;
		} else if (rel.contains(QStringLiteral("icon"))) {
			auto size = sizeFromAttr(attrs.value(QStringLiteral("sizes")));
			if (size == 0 && rel.contains(QStringLiteral("apple-touch"))) size = 180;
			info.icons.append({size, absolute});
		}
	}

	return info;
}

void mergeManifest(PageInfo& info, const QByteArray& json, const QUrl& manifestUrl) {
	auto doc = QJsonDocument::fromJson(json);
	if (!doc.isObject()) return;
	auto obj = doc.object();

	if (info.name.isEmpty()) {
		auto name = obj.value(QStringLiteral("short_name")).toString();
		if (name.isEmpty()) name = obj.value(QStringLiteral("name")).toString();
		if (!name.isEmpty()) info.name = name;
	}

	for (const auto& iconVal: obj.value(QStringLiteral("icons")).toArray()) {
		auto iconObj = iconVal.toObject();
		auto src = iconObj.value(QStringLiteral("src")).toString();
		if (src.isEmpty()) continue;
		auto size = sizeFromAttr(iconObj.value(QStringLiteral("sizes")).toString());
		info.icons.append({size, manifestUrl.resolved(QUrl(src)).toString()});
	}
}

QString dataRoot() {
	return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/webapps");
}

QString iconsDir() { return dataRoot() + QStringLiteral("/icons"); }


QString webAppsStartMenuFolder() {
	auto appData = qEnvironmentVariable("AppData");
	if (appData.isEmpty()) return QString();
	return appData + QStringLiteral("/Microsoft/Windows/Start Menu/Programs/ii Web Apps");
}

QString appPathExe(const QString& exeName) {
	auto subkey =
	    QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\") + exeName;
	auto wsub = subkey.toStdWString();

	for (auto root: {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
		auto path = readRegistryString(root, wsub.c_str(), nullptr);
		if (!path.isEmpty() && QFileInfo::exists(path)) return path;
	}

	return QString();
}

QString findBrowserExe() {
	static const QString candidates[] = {
	    QStringLiteral("chrome.exe"),
	    QStringLiteral("brave.exe"),
	    QStringLiteral("msedge.exe"),
	};

	for (const auto& exe: candidates) {
		auto path = appPathExe(exe);
		if (!path.isEmpty()) return path;
	}

	return QString();
}

bool saveAsSquareIcon(const QByteArray& iconBytes, const QString& icoPath) {
	if (iconBytes.isEmpty()) return false;

	QImage image;
	if (!image.loadFromData(iconBytes) || image.isNull()) return false;

	image = image.convertToFormat(QImage::Format_ARGB32);
	auto scaled = image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);

	auto canvas = QImage(256, 256, QImage::Format_ARGB32);
	canvas.fill(Qt::transparent);

	auto painter = QPainter(&canvas);
	painter.drawImage((256 - scaled.width()) / 2, (256 - scaled.height()) / 2, scaled);
	painter.end();

	auto dir = QFileInfo(icoPath).absoluteDir();
	if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return false;

	return canvas.save(icoPath, "ico");
}

bool createWebAppOnDisk(
    const QString& slug,
    const QString& name,
    const QString& url,
    const QByteArray& iconBytes,
    QString& error
) {
	auto browser = findBrowserExe();
	if (browser.isEmpty()) {
		error = QStringLiteral("no Edge, Chrome or Brave found");
		return false;
	}

	auto icoPath = iconsDir() + u'/' + slug + QStringLiteral(".ico");
	auto hasIcon = saveAsSquareIcon(iconBytes, icoPath);

	auto folder = webAppsStartMenuFolder();
	if (folder.isEmpty() || !QDir().mkpath(folder)) {
		error = QStringLiteral("could not create the Start menu folder");
		return false;
	}

	auto data = ShellLinkData();
	data.targetPath = browser;
	data.arguments = QStringLiteral("--app=%1").arg(url);
	data.description = name;
	data.workingDirectory = QFileInfo(browser).absolutePath();
	data.iconPath = hasIcon ? icoPath : browser;
	data.iconIndex = 0;

	auto lnkPath = folder + u'/' + slug + QStringLiteral(".lnk");
	if (!writeShellLink(lnkPath, data)) {
		error = QStringLiteral("could not create the shortcut");
		return false;
	}

	return true;
}

void rescanDesktopEntriesLater() {
	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [] { DesktopEntryManager::instance()->scanDesktopEntries(); },
	    Qt::QueuedConnection
	);
}

} // namespace

struct WebAppCreateJob {
	QString rawUrl;
	QString url;
	QString host;
	QString slug;
	QString name;
	QList<QPair<int, QString>> iconCandidates;
	int candidateIndex = 0;
	QStringList fallbackHosts;
	int fallbackIndex = 0;
};

WebApps::WebApps(QObject* parent): QObject(parent) { this->refresh(); }

void WebApps::refresh() {
	auto items = QVariantList();
	auto folder = webAppsStartMenuFolder();
	auto dir = QDir(folder);

	if (dir.exists()) {
		static const QRegularExpression appArgRe(QStringLiteral(R"(--app=(\S+))"));

		const auto files = dir.entryInfoList({QStringLiteral("*.lnk")}, QDir::Files);
		for (const auto& fileInfo: files) {
			auto link = readShellLink(fileInfo.absoluteFilePath());
			if (!link.valid) continue;

			auto id = fileInfo.completeBaseName();
			auto url = QString();
			auto match = appArgRe.match(link.arguments);
			if (match.hasMatch()) url = match.captured(1).remove(u'"');

			auto item = QVariantMap();
			item[QStringLiteral("id")] = id;
			item[QStringLiteral("name")] = link.description.isEmpty() ? id : link.description;
			item[QStringLiteral("url")] = url;
			item[QStringLiteral("icon")] = link.iconPath;
			items.append(item);
		}
	}

	std::sort(items.begin(), items.end(), [](const QVariant& a, const QVariant& b) {
		return a.toMap().value(QStringLiteral("name")).toString().toLower()
		     < b.toMap().value(QStringLiteral("name")).toString().toLower();
	});

	this->mItems = items;
	emit this->itemsChanged();
}

void WebApps::create(const QStringList& urls) {
	if (urls.isEmpty()) return;

	auto wasBusy = this->mPending > 0;
	this->mPending += static_cast<int>(urls.size());
	if (!wasBusy) emit this->busyChanged();

	for (const auto& rawUrl: urls) this->startJob(rawUrl);
}

void WebApps::startJob(const QString& rawUrl) {
	auto url = normalizeUrl(rawUrl);
	if (url.isEmpty()) {
		this->jobFinished(rawUrl, false, QStringLiteral("empty url"));
		return;
	}

	QString slug;
	QString label;
	QString host;
	if (!parseUrlParts(url, slug, label, host)) {
		this->jobFinished(rawUrl, false, QStringLiteral("invalid url: %1").arg(rawUrl));
		return;
	}

	auto job = std::make_shared<WebAppCreateJob>();
	job->rawUrl = rawUrl;
	job->url = url;
	job->host = host;
	job->slug = slug;
	job->name = label;

	this->fetchPage(job);
}

void WebApps::fetchPage(const std::shared_ptr<WebAppCreateJob>& job) {
	auto request = QNetworkRequest(QUrl(job->url));
	request.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);
	request.setAttribute(
	    QNetworkRequest::RedirectPolicyAttribute,
	    QNetworkRequest::NoLessSafeRedirectPolicy
	);

	auto* reply = this->mNam.get(request);

	auto* timeout = new QTimer(reply);
	timeout->setSingleShot(true);
	QObject::connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
	timeout->start(10000);

	QPointer<WebApps> self = this;
	QObject::connect(reply, &QNetworkReply::finished, this, [self, job, reply] {
		reply->deleteLater();
		if (!self) return;

		if (reply->error() == QNetworkReply::NoError) {
			auto html = QString::fromUtf8(reply->readAll());
			auto info = parsePageHtml(html, QUrl(job->url));
			if (!info.name.isEmpty()) job->name = info.name;
			job->iconCandidates.append(info.icons);

			if (!info.manifestUrl.isEmpty()) {
				self->fetchManifest(job, info.manifestUrl);
				return;
			}
		} else {
			qCDebug(logWebApps) << "page fetch failed for" << job->url << ":" << reply->errorString();
		}

		self->startIconFetch(job);
	});
}

void WebApps::fetchManifest(const std::shared_ptr<WebAppCreateJob>& job, const QString& manifestUrl) {
	auto request = QNetworkRequest(QUrl(manifestUrl));
	request.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);
	auto* reply = this->mNam.get(request);

	auto* timeout = new QTimer(reply);
	timeout->setSingleShot(true);
	QObject::connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
	timeout->start(8000);

	QPointer<WebApps> self = this;
	QObject::connect(
	    reply,
	    &QNetworkReply::finished,
	    this,
	    [self, job, reply, manifestUrl] {
		    reply->deleteLater();
		    if (!self) return;

		    if (reply->error() == QNetworkReply::NoError) {
			    auto info = PageInfo();
			    mergeManifest(info, reply->readAll(), QUrl(manifestUrl));
			    if (!info.name.isEmpty() && job->name.isEmpty()) job->name = info.name;
			    job->iconCandidates.append(info.icons);
		    }

		    self->startIconFetch(job);
	    }
	);
}

void WebApps::startIconFetch(const std::shared_ptr<WebAppCreateJob>& job) {
	std::sort(
	    job->iconCandidates.begin(),
	    job->iconCandidates.end(),
	    [](const QPair<int, QString>& a, const QPair<int, QString>& b) { return a.first > b.first; }
	);

	auto deduped = QList<QPair<int, QString>>();
	auto seen = QSet<QString>();
	for (const auto& candidate: job->iconCandidates) {
		if (candidate.second.isEmpty() || seen.contains(candidate.second)) continue;
		seen.insert(candidate.second);
		deduped.append(candidate);
	}
	job->iconCandidates = deduped;
	job->candidateIndex = 0;

	auto bare = job->host;
	if (bare.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)) bare = bare.mid(4);

	job->fallbackHosts = {job->host};
	if (bare != job->host) job->fallbackHosts.append(bare);

	auto dotIndex = bare.indexOf(u'.');
	if (dotIndex > 0) {
		auto afterFirstLabel = bare.mid(dotIndex + 1);
		if (afterFirstLabel.contains(u'.') && !job->fallbackHosts.contains(afterFirstLabel)) {
			job->fallbackHosts.append(afterFirstLabel);
		}
	}
	job->fallbackIndex = 0;

	this->tryNextPageIcon(job);
}

void WebApps::tryNextPageIcon(const std::shared_ptr<WebAppCreateJob>& job) {
	if (job->candidateIndex >= job->iconCandidates.size()) {
		this->tryNextFallbackIcon(job);
		return;
	}

	auto url = job->iconCandidates.at(job->candidateIndex).second;
	job->candidateIndex++;

	auto request = QNetworkRequest(QUrl(url));
	request.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Mozilla/5.0"));
	auto* reply = this->mNam.get(request);

	auto* timeout = new QTimer(reply);
	timeout->setSingleShot(true);
	QObject::connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
	timeout->start(12000);

	QPointer<WebApps> self = this;
	QObject::connect(reply, &QNetworkReply::finished, this, [self, job, reply] {
		reply->deleteLater();
		if (!self) return;

		if (reply->error() == QNetworkReply::NoError) {
			auto bytes = reply->readAll();
			QImage image;
			if (image.loadFromData(bytes) && std::max(image.width(), image.height()) >= 96) {
				self->finalizeJob(job, bytes);
				return;
			}
		}

		self->tryNextPageIcon(job);
	});
}

void WebApps::tryNextFallbackIcon(const std::shared_ptr<WebAppCreateJob>& job) {
	if (job->fallbackIndex >= job->fallbackHosts.size()) {
		this->finalizeJob(job, QByteArray());
		return;
	}

	auto host = job->fallbackHosts.at(job->fallbackIndex);
	job->fallbackIndex++;

	auto url = QStringLiteral(
	                "https://t3.gstatic.com/faviconV2?client=SOCIAL&type=FAVICON&fallback_opts="
	                "TYPE,SIZE,URL&url=https://%1&size=256"
	)
	                .arg(host);

	auto* reply = this->mNam.get(QNetworkRequest(QUrl(url)));

	auto* timeout = new QTimer(reply);
	timeout->setSingleShot(true);
	QObject::connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
	timeout->start(8000);

	QPointer<WebApps> self = this;
	QObject::connect(reply, &QNetworkReply::finished, this, [self, job, reply] {
		reply->deleteLater();
		if (!self) return;

		if (reply->error() == QNetworkReply::NoError) {
			auto bytes = reply->readAll();
			QImage image;
			if (image.loadFromData(bytes) && std::max(image.width(), image.height()) >= 32) {
				self->finalizeJob(job, bytes);
				return;
			}
		}

		self->tryNextFallbackIcon(job);
	});
}

void WebApps::finalizeJob(const std::shared_ptr<WebAppCreateJob>& job, const QByteArray& iconBytes) {
	QPointer<WebApps> self = this;

	auto* task = QRunnable::create([self, job, iconBytes] {
		QString error;
		auto ok = createWebAppOnDisk(job->slug, job->name, job->url, iconBytes, error);

		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [self, job, ok, error] {
			    if (self) self->jobFinished(job->rawUrl, ok, error);
		    },
		    Qt::QueuedConnection
		);
	});
	task->setAutoDelete(true);
	QThreadPool::globalInstance()->start(task);
}

void WebApps::jobFinished(const QString& rawUrl, bool ok, const QString& message) {
	if (!ok) this->mFailures.append(message.isEmpty() ? rawUrl : message);

	this->mPending = std::max(0, this->mPending - 1);
	if (this->mPending > 0) return;

	emit this->busyChanged();

	auto newError = this->mFailures.join(QStringLiteral(", "));
	this->mFailures.clear();
	if (newError != this->mError) {
		this->mError = newError;
		emit this->errorChanged();
	}

	this->refresh();
	rescanDesktopEntriesLater();
}

void WebApps::remove(const QString& id) {
	if (id.isEmpty() || id.contains(u'/') || id.contains(u'\\') || id.contains(QStringLiteral(".."))) {
		return;
	}

	auto folder = webAppsStartMenuFolder();
	QFile::remove(folder + u'/' + id + QStringLiteral(".lnk"));
	QFile::remove(iconsDir() + u'/' + id + QStringLiteral(".ico"));

	this->refresh();
	rescanDesktopEntriesLater();
}

void WebApps::open(const QString& id) {
	if (id.isEmpty() || id.contains(u'/') || id.contains(u'\\')) return;

	auto lnkPath = webAppsStartMenuFolder() + u'/' + id + QStringLiteral(".lnk");
	auto link = readShellLink(lnkPath);
	if (!link.valid || link.targetPath.isEmpty()) return;

	QProcess::startDetached(link.targetPath, QProcess::splitCommand(link.arguments), link.workingDirectory);
}

} // namespace qs::windows::sys
