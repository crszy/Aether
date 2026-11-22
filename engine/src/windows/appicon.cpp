#include "appicon.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

#include <qcache.h>
#include <qcoreapplication.h>
#include <qdeadlinetimer.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qimage.h>
#include <qlist.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qmutex.h>
#include <qobject.h>
#include <qpixmap.h>
#include <qsize.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qthread.h>

#include <qt_windows.h>

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include "desktopentry_backend.hpp"

using Microsoft::WRL::ComPtr;

namespace qs::windows {

namespace {
Q_LOGGING_CATEGORY(logAppIcon, "quickshell.windows.appicon", QtWarningMsg);

constexpr int ICON_THREAD_COUNT = 2;
constexpr int ICON_CACHE_KB = 32 * 1024;
constexpr int ICON_THREAD_STOP_MS = 1000;

struct ComApartment {
	ComApartment(): hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}

	ComApartment(const ComApartment&) = delete;
	ComApartment(ComApartment&&) = delete;
	ComApartment& operator=(const ComApartment&) = delete;
	ComApartment& operator=(ComApartment&&) = delete;

	~ComApartment() {
		if (SUCCEEDED(this->hr)) CoUninitialize();
	}

	HRESULT hr;
};

bool looksLikeWindowsPath(const QString& s) {
	return s.length() > 2 && s[0].isLetter() && s[1] == u':' && (s[2] == u'/' || s[2] == u'\\');
}

bool isPlainImage(const QString& path) {
	static const auto suffixes = QStringList {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp", ".svg"};

	for (const auto& suffix: suffixes) {
		if (path.endsWith(suffix, Qt::CaseInsensitive)) return true;
	}

	return false;
}

QImage hbitmapToImage(HBITMAP bitmap) {
	auto bm = BITMAP {};
	if (GetObjectW(bitmap, sizeof(bm), &bm) == 0) return QImage();

	auto width = bm.bmWidth;
	auto height = bm.bmHeight;
	if (width <= 0 || height <= 0) return QImage();

	auto bmi = BITMAPINFO {};
	bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = -height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	auto image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
	if (image.isNull()) return image;

	auto* hdc = GetDC(nullptr);
	auto ok = GetDIBits(hdc, bitmap, 0, static_cast<UINT>(height), image.bits(), &bmi, DIB_RGB_COLORS);
	ReleaseDC(nullptr, hdc);

	if (ok == 0) return QImage();

	return image.convertToFormat(QImage::Format_ARGB32);
}

QImage renderShellItemIcon(IShellItem* item, const QSize& size) {
	ComPtr<IShellItemImageFactory> factory;
	if (FAILED(item->QueryInterface(IID_PPV_ARGS(&factory)))) return QImage();

	auto width = size.width() > 0 ? size.width() : 48;
	auto height = size.height() > 0 ? size.height() : 48;
	auto requested = SIZE {.cx = width, .cy = height};

	HBITMAP bitmap = nullptr;
	auto hr = factory->GetImage(requested, SIIGBF_ICONONLY | SIIGBF_RESIZETOFIT, &bitmap);
	if (FAILED(hr) || bitmap == nullptr) return QImage();

	auto image = hbitmapToImage(bitmap);
	DeleteObject(bitmap);
	return image;
}

QImage renderParsingName(const QString& parsingName, const QSize& size) {
	auto wname = parsingName.toStdWString();
	ComPtr<IShellItem> item;
	auto hr = SHCreateItemFromParsingName(wname.c_str(), nullptr, IID_PPV_ARGS(&item));

	if (FAILED(hr)) {
		qCDebug(logAppIcon) << "SHCreateItemFromParsingName failed for" << parsingName << ":"
		                    << Qt::hex << hr;
		return QImage();
	}

	return renderShellItemIcon(item.Get(), size);
}

QString parsingNameForKey(const QString& key) {
	static const auto appIconPrefix = QStringLiteral("appicon:");
	if (key.startsWith(appIconPrefix)) {
		auto id = key.sliced(appIconPrefix.length());
		auto token = WindowsDesktopEntryBackend::parsingNameForId(id);
		if (token.isEmpty()) return QString();
		return QStringLiteral("shell:AppsFolder\\") + token;
	}

	if (looksLikeWindowsPath(key) && !isPlainImage(key)) {
		auto parsingName = key;
		parsingName.replace(u'/', u'\\');
		return parsingName;
	}

	auto exe = WindowsDesktopEntryBackend::exeForAppId(key);
	if (!exe.isEmpty()) return QDir::toNativeSeparators(exe);

	return QString();
}

QString cacheKeyFor(const QString& parsingName, const QSize& size) {
	return parsingName + u'@' + QString::number(size.width()) + u'x' + QString::number(size.height());
}

class IconCache {
public:
	QImage find(const QString& key) {
		QMutexLocker locker(&this->mutex);
		auto* hit = this->images.object(key);
		return hit != nullptr ? *hit : QImage();
	}

	void insert(const QString& key, const QImage& image) {
		auto cost = std::max<qsizetype>(1, image.sizeInBytes() / 1024);
		QMutexLocker locker(&this->mutex);
		this->images.insert(key, new QImage(image), cost);
	}

private:
	QMutex mutex;
	QCache<QString, QImage> images {ICON_CACHE_KB};
};

IconCache& iconCache() {
	static auto* cache = new IconCache(); // NOLINT
	return *cache;
}

QImage loadShellIcon(const QString& parsingName, const QSize& size) {
	auto key = cacheKeyFor(parsingName, size);

	auto cached = iconCache().find(key);
	if (!cached.isNull()) return cached;

	auto image = renderParsingName(parsingName, size);
	if (!image.isNull()) iconCache().insert(key, image);
	return image;
}

struct IconJob {
	QString parsingName;
	QSize size;
	std::function<bool()> cancelled;
	std::function<void(QImage)> done;
};

class IconThread: public QThread {
protected:
	void run() override {
		auto com = ComApartment();
		if (FAILED(com.hr)) {
			qCWarning(logAppIcon) << "CoInitializeEx failed on icon thread:" << Qt::hex << com.hr;
		}

		this->exec();
	}
};

class IconLoader {
public:
	static IconLoader& instance() {
		static auto* loader = new IconLoader(); // NOLINT
		return *loader;
	}

	void enqueue(IconJob job) {
		QMutexLocker locker(&this->mutex);

		if (this->stopping) {
			locker.unlock();
			job.done(QImage());
			return;
		}

		this->queue.append(std::move(job));

		for (auto i = 0; i < this->started; i++) {
			auto& worker = this->workers.at(i);
			if (worker.awake) continue;

			worker.awake = true;
			this->post(worker);
			return;
		}

		if (this->started < ICON_THREAD_COUNT) this->startWorker();
	}

private:
	struct Worker {
		IconThread* thread = nullptr;
		QObject* context = nullptr;
		bool awake = false;
	};

	IconLoader() {
		auto* app = QCoreApplication::instance();
		if (app == nullptr) return;

		QObject::connect(app, &QCoreApplication::aboutToQuit, app, [this] { this->stop(); });
	}

	void startWorker() {
		auto& worker = this->workers.at(this->started++);
		worker.thread = new IconThread();
		worker.thread->setObjectName(QStringLiteral("qs-appicon"));
		worker.context = new QObject();
		worker.context->moveToThread(worker.thread);

		if (auto* app = QCoreApplication::instance()) worker.thread->moveToThread(app->thread());

		worker.awake = true;
		worker.thread->start(QThread::LowPriority);
		this->post(worker);
	}

	void post(Worker& worker) {
		QMetaObject::invokeMethod(
		    worker.context,
		    [this, target = &worker] { this->runNext(*target); },
		    Qt::QueuedConnection
		);
	}

	void runNext(Worker& worker) {
		IconJob job;

		{
			QMutexLocker locker(&this->mutex);
			if (this->queue.isEmpty()) {
				worker.awake = false;
				return;
			}

			job = this->queue.takeFirst();
		}

		auto image = QImage();
		if (!job.cancelled || !job.cancelled()) image = loadShellIcon(job.parsingName, job.size);
		job.done(std::move(image));

		QMutexLocker locker(&this->mutex);
		if (this->queue.isEmpty() || this->stopping) {
			worker.awake = false;
		} else {
			this->post(worker);
		}
	}

	void stop() {
		auto pending = QList<IconJob>();
		auto running = QList<Worker>();

		{
			QMutexLocker locker(&this->mutex);
			if (this->stopping) return;

			this->stopping = true;
			pending.swap(this->queue);
			for (auto i = 0; i < this->started; i++) running.append(this->workers.at(i));
		}

		for (auto& job: pending) job.done(QImage());
		for (auto& worker: running) worker.thread->quit();

		auto deadline = QDeadlineTimer(ICON_THREAD_STOP_MS);
		for (auto& worker: running) {
			if (!worker.thread->wait(deadline)) {
				qCWarning(logAppIcon) << "Icon thread did not stop in time";
				continue;
			}

			delete worker.context;
			delete worker.thread;
		}
	}

	QMutex mutex;
	QList<IconJob> queue;
	std::array<Worker, ICON_THREAD_COUNT> workers {};
	int started = 0;
	bool stopping = false;
};

} // namespace

QImage shellImage(const QString& parsingName, const QSize& size) {
	auto com = ComApartment();
	if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE) return QImage();
	return renderParsingName(parsingName, size);
}

QPixmap iconForKey(const QString& key, const QSize& size) {
	auto parsingName = parsingNameForKey(key);
	if (parsingName.isEmpty()) return QPixmap();

	auto cached = iconCache().find(cacheKeyFor(parsingName, size));
	if (!cached.isNull()) return QPixmap::fromImage(cached);

	auto com = ComApartment();
	if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE) {
		qCWarning(logAppIcon) << "CoInitializeEx failed:" << Qt::hex << com.hr;
		return QPixmap();
	}

	return QPixmap::fromImage(loadShellIcon(parsingName, size));
}

bool isShellIconKey(const QString& key) {
	if (key.startsWith(QStringLiteral("appicon:"))) return true;
	if (looksLikeWindowsPath(key)) return !isPlainImage(key);
	return !WindowsDesktopEntryBackend::exeForAppId(key).isEmpty();
}

bool hasShellIcon(const QString& key) {
	auto parsingName = parsingNameForKey(key);
	if (parsingName.isEmpty()) return false;
	return !looksLikeWindowsPath(parsingName) || QFileInfo::exists(parsingName);
}

void requestShellIcon(
    const QString& key,
    const QSize& size,
    std::function<bool()> cancelled,
    std::function<void(QImage)> done
) {
	auto parsingName = parsingNameForKey(key);
	if (parsingName.isEmpty()) {
		done(QImage());
		return;
	}

	auto cached = iconCache().find(cacheKeyFor(parsingName, size));
	if (!cached.isNull()) {
		done(cached);
		return;
	}

	IconLoader::instance().enqueue({
	    .parsingName = parsingName,
	    .size = size,
	    .cancelled = std::move(cancelled),
	    .done = std::move(done),
	});
}

} // namespace qs::windows
