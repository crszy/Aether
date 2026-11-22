#include "toast_worker.hpp"

#include <windows.h>
#include <winerror.h>

#include <qbytearray.h>
#include <qcryptographichash.h>
#include <qdir.h>
#include <qimage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qstandardpaths.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtimer.h>

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/base.h>

#include <qfileinfo.h>
#include <qurl.h>

#include "../../../core/logcat.hpp"
#include "../../appicon.hpp"
#include "toast_mirror.hpp"

using namespace winrt::Windows::UI::Notifications;
using namespace winrt::Windows::UI::Notifications::Management;
using winrt::Windows::ApplicationModel::AppInfo;

namespace qs::windows::services::notifications {

namespace {
QS_LOGGING_CATEGORY(logToastMirror, "quickshell.windows.notifications.mirror", QtWarningMsg);

constexpr int POLL_INTERVAL_MS = 5000; // each poll is a cross-process call (~50-150 ms CPU); 2 s cost ~7% of a core idle
constexpr int ACCESS_CHECK_INTERVAL_MS = 10000;
constexpr int EVENT_COALESCE_MS = 150;
constexpr float LOGO_SIZE = 96;
constexpr uint32_t MAX_LOGO_BYTES = 4 * 1024 * 1024;

QString toQString(const winrt::hstring& value) {
	return QString::fromWCharArray(value.c_str(), static_cast<qsizetype>(value.size()));
}

uint32_t codeOf(const winrt::hresult_error& e) { return static_cast<uint32_t>(e.code().value); }

SystemNotificationAccess::Enum mapAccess(UserNotificationListenerAccessStatus status) {
	switch (status) {
	case UserNotificationListenerAccessStatus::Allowed: return SystemNotificationAccess::Allowed;
	case UserNotificationListenerAccessStatus::Denied: return SystemNotificationAccess::Denied;
	default: return SystemNotificationAccess::Unspecified;
	}
}

QString logoCacheDir() {
	auto dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
	         + QStringLiteral("/notification-icons");
	QDir().mkpath(dir);
	return dir;
}

// Win32 apps (Discord, Zen, Spotify ...) give the listener no logo. Windows still knows their icon:
//  1. Software\Classes\AppUserModelId\<aumid>\IconUri (HKCU, then HKLM) - what the Action Center itself shows
//  2. the Start menu entry with that app id (shell:AppsFolder\<aumid>)
QImage fallbackLogo(const QString& aumid) {
	auto size = QSize(static_cast<int>(LOGO_SIZE), static_cast<int>(LOGO_SIZE));
	auto key = (QStringLiteral("Software\\Classes\\AppUserModelId\\") + aumid).toStdWString();

	for (auto* root: {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
		wchar_t buffer[2048] {};
		DWORD bytes = sizeof(buffer);
		if (RegGetValueW(root, key.c_str(), L"IconUri", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buffer, &bytes)
		    != ERROR_SUCCESS)
		{
			continue;
		}

		auto uri = QString::fromWCharArray(buffer);
		if (uri.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) uri = QUrl(uri).toLocalFile();
		if (uri.isEmpty() || !QFileInfo::exists(uri)) continue;

		auto image = QImage(uri);
		if (image.isNull()) image = qs::windows::shellImage(QDir::toNativeSeparators(uri), size);
		if (!image.isNull()) return image;
	}

	return qs::windows::shellImage(QStringLiteral("shell:AppsFolder\\") + aumid, size);
}
} // namespace

ToastMirrorWorker::ToastMirrorWorker(ToastMirror* frontend): mFrontend(frontend) {}

ToastMirrorWorker::~ToastMirrorWorker() = default;

void ToastMirrorWorker::start() {
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
		this->mApartment = true;
	} catch (const winrt::hresult_error& e) {
		qCWarning(logToastMirror) << "init_apartment(multi_threaded) failed:" << Qt::hex << codeOf(e);
	}

	this->mPollTimer = new QTimer(this);
	this->mPollTimer->setInterval(POLL_INTERVAL_MS);
	QObject::connect(this->mPollTimer, &QTimer::timeout, this, [this] { this->resync(true); });

	this->mAccessTimer = new QTimer(this);
	this->mAccessTimer->setInterval(ACCESS_CHECK_INTERVAL_MS);
	QObject::connect(this->mAccessTimer, &QTimer::timeout, this, &ToastMirrorWorker::checkAccess);

	this->mEventTimer = new QTimer(this);
	this->mEventTimer->setSingleShot(true);
	this->mEventTimer->setInterval(EVENT_COALESCE_MS);
	QObject::connect(this->mEventTimer, &QTimer::timeout, this, [this] { this->resync(true); });
}

void ToastMirrorWorker::shutdown() {
	this->mEnabled = false;
	this->stop();
	this->mListener = nullptr;

	if (this->mApartment) {
		winrt::uninit_apartment();
		this->mApartment = false;
	}
}

void ToastMirrorWorker::setEnabled(bool enabled) {
	if (enabled == this->mEnabled) return;
	this->mEnabled = enabled;

	if (enabled) this->begin(true);
	else {
		this->stop();
		this->report();
	}
}

void ToastMirrorWorker::requestAccess() { this->begin(true); }

void ToastMirrorWorker::begin(bool request) {
	if (!this->mApartment) {
		this->mAccess = SystemNotificationAccess::Unavailable;
		this->report();
		return;
	}

	try {
		if (!this->mListener) this->mListener = UserNotificationListener::Current();

		auto status = this->mListener.GetAccessStatus();

		if (request && status == UserNotificationListenerAccessStatus::Unspecified) {
			status = this->mListener.RequestAccessAsync().get();
		}

		this->applyAccess(status);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logToastMirror) << "UserNotificationListener unavailable:" << Qt::hex << codeOf(e)
		                          << QString::fromWCharArray(e.message().c_str());
		this->stop();
		this->mAccess = SystemNotificationAccess::Unavailable;
		this->report();
	}
}

void ToastMirrorWorker::applyAccess(UserNotificationListenerAccessStatus status) {
	this->mAccess = mapAccess(status);

	if (this->mAccess != SystemNotificationAccess::Allowed || !this->mEnabled) {
		this->stop();
		if (this->mEnabled) this->mAccessTimer->start();
		this->report();
		return;
	}

	this->mAccessTimer->stop();

	if (!this->mActive) {
		if (!this->mEvents) {
			try {
				this->mChangedToken = this->mListener.NotificationChanged([this](auto&&, auto&&) {
					QMetaObject::invokeMethod(
					    this,
					    [this] {
						    if (this->mActive) this->mEventTimer->start();
					    },
					    Qt::QueuedConnection
					);
				});
				this->mEvents = true;
			} catch (const winrt::hresult_error& e) {
				qCInfo(logToastMirror) << "NotificationChanged unavailable (" << Qt::hex << codeOf(e)
				                       << "), polling every" << POLL_INTERVAL_MS << "ms";
			}
		}

		this->mActive = true;
		this->mKnown.clear();
		this->resync(false);

		if (this->mActive && !this->mEvents) this->mPollTimer->start();
	}

	this->report();
}

void ToastMirrorWorker::checkAccess() {
	if (!this->mListener || !this->mEnabled) return;

	try {
		auto status = this->mListener.GetAccessStatus();
		if (mapAccess(status) != this->mAccess || status == UserNotificationListenerAccessStatus::Allowed) {
			this->applyAccess(status);
		}
	} catch (const winrt::hresult_error& e) {
		qCWarning(logToastMirror) << "GetAccessStatus failed:" << Qt::hex << codeOf(e);
	}
}

void ToastMirrorWorker::stop() {
	if (this->mPollTimer) this->mPollTimer->stop();
	if (this->mEventTimer) this->mEventTimer->stop();
	if (this->mAccessTimer) this->mAccessTimer->stop();

	if (this->mEvents) {
		try {
			this->mListener.NotificationChanged(this->mChangedToken);
		} catch (const winrt::hresult_error&) {}
		this->mEvents = false;
	}

	for (auto id: std::as_const(this->mKnown)) {
		QMetaObject::invokeMethod(
		    this->mFrontend,
		    [frontend = this->mFrontend, id] { emit frontend->toastRemoved(id); },
		    Qt::QueuedConnection
		);
	}

	this->mKnown.clear();
	this->mActive = false;
}

void ToastMirrorWorker::resync(bool announce) {
	if (!this->mActive) return;

	winrt::Windows::Foundation::Collections::IVectorView<UserNotification> toasts {nullptr};
	try {
		toasts = this->mListener.GetNotificationsAsync(NotificationKinds::Toast).get();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logToastMirror) << "GetNotificationsAsync failed:" << Qt::hex << codeOf(e);
		this->stop();
		this->mAccess = e.code() == E_ACCESSDENIED ? SystemNotificationAccess::Denied
		                                           : SystemNotificationAccess::Unspecified;
		if (this->mEnabled) this->mAccessTimer->start();
		this->report();
		return;
	}

	auto current = QSet<quint32>();

	for (const auto& toast: toasts) {
		quint32 id = 0;
		try {
			id = toast.Id();
		} catch (const winrt::hresult_error&) {
			continue;
		}

		current.insert(id);
		if (!announce || this->mKnown.contains(id)) continue;

		auto snapshot = this->snapshotOf(toast);
		qCDebug(logToastMirror) << "New toast" << id << "from" << snapshot.aumid;

		QMetaObject::invokeMethod(
		    this->mFrontend,
		    [frontend = this->mFrontend, snapshot] { emit frontend->toastAdded(snapshot); },
		    Qt::QueuedConnection
		);
	}

	for (auto id: std::as_const(this->mKnown)) {
		if (current.contains(id)) continue;
		qCDebug(logToastMirror) << "Toast" << id << "left the notification center";

		QMetaObject::invokeMethod(
		    this->mFrontend,
		    [frontend = this->mFrontend, id] { emit frontend->toastRemoved(id); },
		    Qt::QueuedConnection
		);
	}

	this->mKnown = current;
}

void ToastMirrorWorker::removeToast(quint32 id) {
	this->mKnown.remove(id);
	if (!this->mListener || !this->mActive) return;

	try {
		this->mListener.RemoveNotification(id);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logToastMirror) << "RemoveNotification" << id << "failed:" << Qt::hex << codeOf(e);
	}
}

ToastSnapshot ToastMirrorWorker::snapshotOf(const UserNotification& toast) {
	auto snapshot = ToastSnapshot();
	snapshot.id = toast.Id();

	try {
		if (auto info = toast.AppInfo()) {
			snapshot.aumid = toQString(info.AppUserModelId());
			if (auto display = info.DisplayInfo()) snapshot.appName = toQString(display.DisplayName());
			snapshot.logoPath = this->logoFor(info, snapshot.aumid);
		}
	} catch (const winrt::hresult_error& e) {
		qCDebug(logToastMirror) << "AppInfo of toast" << snapshot.id << "failed:" << Qt::hex << codeOf(e);
	}

	if (snapshot.appName.isEmpty()) snapshot.appName = snapshot.aumid;
	if (snapshot.appName.isEmpty()) snapshot.appName = QStringLiteral("Windows");

	try {
		auto notification = toast.Notification();
		auto visual = notification ? notification.Visual() : nullptr;

		if (visual) {
			auto binding = visual.GetBinding(KnownNotificationBindings::ToastGeneric());
			if (!binding && visual.Bindings().Size() != 0) binding = visual.Bindings().GetAt(0);

			if (binding) {
				auto lines = QStringList();
				for (const auto& text: binding.GetTextElements()) lines << toQString(text.Text());
				if (!lines.isEmpty()) snapshot.summary = lines.takeFirst();
				snapshot.body = lines.join('\n');
			}
		}
	} catch (const winrt::hresult_error& e) {
		qCDebug(logToastMirror) << "Reading toast" << snapshot.id << "failed:" << Qt::hex << codeOf(e);
	}

	return snapshot;
}

QString ToastMirrorWorker::logoFor(const AppInfo& info, const QString& aumid) {
	if (auto it = this->mLogos.constFind(aumid); it != this->mLogos.constEnd()) return *it;

	QString path;

	try {
		auto logo = info.DisplayInfo().GetLogo(winrt::Windows::Foundation::Size(LOGO_SIZE, LOGO_SIZE));
		auto stream = logo ? logo.OpenReadAsync().get() : nullptr;

		if (stream && stream.Size() > 0 && stream.Size() <= MAX_LOGO_BYTES) {
			auto size = static_cast<uint32_t>(stream.Size());
			auto reader = winrt::Windows::Storage::Streams::DataReader(stream);
			reader.LoadAsync(size).get();

			auto bytes = QByteArray(static_cast<qsizetype>(size), Qt::Uninitialized);
			reader.ReadBytes(winrt::array_view<uint8_t>(
			    reinterpret_cast<uint8_t*>(bytes.data()),               // NOLINT
			    reinterpret_cast<uint8_t*>(bytes.data()) + bytes.size() // NOLINT
			));

			auto image = QImage();
			if (image.loadFromData(bytes)) {
				auto hash = QCryptographicHash::hash(aumid.toUtf8(), QCryptographicHash::Sha1).toHex();
				auto file = logoCacheDir() + '/' + QString::fromLatin1(hash) + QStringLiteral(".png");
				if (image.save(file, "PNG")) path = file;
				else qCWarning(logToastMirror) << "Could not save the logo of" << aumid << "to" << file;
			}
		}
	} catch (const winrt::hresult_error& e) {
		qCDebug(logToastMirror) << "GetLogo for" << aumid << "failed:" << Qt::hex << codeOf(e);
	}

	if (path.isEmpty() && !aumid.isEmpty()) {
		auto image = fallbackLogo(aumid);
		if (!image.isNull()) {
			if (image.width() > LOGO_SIZE || image.height() > LOGO_SIZE) {
				image = image.scaled(
				    static_cast<int>(LOGO_SIZE),
				    static_cast<int>(LOGO_SIZE),
				    Qt::KeepAspectRatio,
				    Qt::SmoothTransformation
				);
			}
			auto hash = QCryptographicHash::hash(aumid.toUtf8(), QCryptographicHash::Sha1).toHex();
			auto file = logoCacheDir() + '/' + QString::fromLatin1(hash) + QStringLiteral(".png");
			if (image.save(file, "PNG")) path = file;
		}
	}

	this->mLogos.insert(aumid, path);
	return path;
}

void ToastMirrorWorker::report() {
	QMetaObject::invokeMethod(
	    this->mFrontend,
	    [frontend = this->mFrontend, access = this->mAccess, active = this->mActive] {
		    frontend->workerState(access, active);
	    },
	    Qt::QueuedConnection
	);
}

} // namespace qs::windows::services::notifications
