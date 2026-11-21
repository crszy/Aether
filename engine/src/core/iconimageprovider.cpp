#include "iconimageprovider.hpp"
#include <algorithm>

#include <qcolor.h>
#include <qfileinfo.h>
#include <qicon.h>
#include <qlogging.h>
#include <qpainter.h>
#include <qpixmap.h>
#include <qsize.h>
#include <qstring.h>

#ifdef Q_OS_WIN
#include <array>
#include <atomic>
#include <memory>
#include <utility>

#include <qcoreapplication.h>
#include <qimage.h>
#include <qmetaobject.h>
#include <qmutex.h>
#include <qtclasshelpermacros.h>
#include <qurl.h>

#include "../windows/appicon.hpp"
#endif

namespace {

struct IconRequest {
	QString id;
	QString iconName;
	QString fallbackName;
	QString path;
	QSize targetSize;
};

IconRequest parseIconRequest(const QString& id, const QSize& requestedSize) {
	auto request = IconRequest();
	request.id = id;

	auto splitIdx = id.indexOf("?path=");
	if (splitIdx != -1) {
		request.iconName = id.sliced(0, splitIdx);
		request.path = id.sliced(splitIdx + 6);
		request.path = QString("/%1/%2").arg(
		    request.path,
		    request.iconName.sliced(request.iconName.lastIndexOf('/') + 1)
		);
	} else {
		splitIdx = id.indexOf("?fallback=");
		if (splitIdx != -1) {
			request.iconName = id.sliced(0, splitIdx);
			request.fallbackName = id.sliced(splitIdx + 10);
		} else {
			request.iconName = id;
		}
	}

	request.targetSize = requestedSize.isValid() ? requestedSize : QSize(100, 100);
	if (request.targetSize.width() == 0 || request.targetSize.height() == 0) {
		request.targetSize = QSize(2, 2);
	}

#ifdef Q_OS_WIN
	request.iconName = QUrl::fromPercentEncoding(request.iconName.toUtf8());
#endif

	return request;
}

QPixmap themedPixmap(const IconRequest& request) {
	auto icon = QIcon::fromTheme(request.iconName);
	if (icon.isNull() && !request.fallbackName.isEmpty()) {
		icon = QIcon::fromTheme(request.fallbackName);
	}
	if (icon.isNull() && !request.path.isEmpty()) icon = QPixmap(request.path);
	// an image file given as the icon name (notification app logos are cached .png files)
	if (icon.isNull()) {
		auto file = request.iconName;
		if (file.startsWith(QStringLiteral("file:"))) file = QUrl(file).toLocalFile();
		if (QFileInfo(file).isAbsolute() && QFileInfo::exists(file)) icon = QPixmap(file);
	}

	auto pixmap = icon.pixmap(request.targetSize.width(), request.targetSize.height());

	if (pixmap.isNull()) {
		qWarning() << "Could not load icon" << request.id << "at size" << request.targetSize
		           << "from request";
		pixmap = IconImageProvider::missingPixmap(request.targetSize);
	}

	return pixmap;
}

#ifdef Q_OS_WIN
constexpr std::array<int, 8> ICON_BUCKETS = {16, 24, 32, 48, 64, 96, 128, 256};

class IconImageResponse;

struct IconResponseState {
	QMutex mutex;
	IconImageResponse* response = nullptr;
	QImage image;
	std::atomic<bool> cancelled = false;
};

class IconImageResponse: public QQuickImageResponse {
public:
	explicit IconImageResponse(std::shared_ptr<IconResponseState> state): state(std::move(state)) {
		QMutexLocker locker(&this->state->mutex);
		this->state->response = this;
	}

	~IconImageResponse() override {
		QMutexLocker locker(&this->state->mutex);
		this->state->response = nullptr;
		this->state->cancelled = true;
	}

	Q_DISABLE_COPY_MOVE(IconImageResponse);

	[[nodiscard]] QQuickTextureFactory* textureFactory() const override {
		QMutexLocker locker(&this->state->mutex);
		return QQuickTextureFactory::textureFactoryForImage(this->state->image);
	}

	void cancel() override { this->state->cancelled = true; }

	void finish() { emit this->finished(); }

private:
	std::shared_ptr<IconResponseState> state;
};

void deliver(const std::shared_ptr<IconResponseState>& state, QImage image) {
	QMutexLocker locker(&state->mutex);
	state->image = std::move(image);

	auto* response = state->response;
	if (response == nullptr) return;

	QMetaObject::invokeMethod(response, [response] { response->finish(); }, Qt::QueuedConnection);
}

void resolveThemed(std::shared_ptr<IconResponseState> state, IconRequest request) {
	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [state = std::move(state), request = std::move(request)] {
		    if (state->cancelled) {
			    deliver(state, QImage());
			    return;
		    }

		    deliver(state, themedPixmap(request).toImage());
	    },
	    Qt::QueuedConnection
	);
}

QSize bucketFor(const QSize& size) {
	auto edge = std::max(size.width(), size.height());

	for (auto bucket: ICON_BUCKETS) {
		if (edge <= bucket) return {bucket, bucket};
	}

	return {edge, edge};
}

QImage fitIcon(const QImage& image, const QSize& size) {
	if (image.size() == size) return image;
	return image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}
#endif

} // namespace

#ifdef Q_OS_WIN
QQuickImageResponse*
IconImageProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
	auto request = parseIconRequest(id, requestedSize);
	auto state = std::make_shared<IconResponseState>();
	auto* response = new IconImageResponse(state);

	if (!request.path.isEmpty() || !qs::windows::isShellIconKey(request.iconName)) {
		resolveThemed(std::move(state), std::move(request));
		return response;
	}

	auto iconName = request.iconName;
	auto bucket = bucketFor(request.targetSize);

	qs::windows::requestShellIcon(
	    iconName,
	    bucket,
	    [state] { return state->cancelled.load(); },
	    [state, request = std::move(request)](QImage image) {
		    if (state->cancelled) {
			    deliver(state, QImage());
		    } else if (image.isNull()) {
			    resolveThemed(state, request);
		    } else {
			    deliver(state, fitIcon(image, request.targetSize));
		    }
	    }
	);

	return response;
}
#else
QPixmap
IconImageProvider::requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) {
	auto pixmap = themedPixmap(parseIconRequest(id, requestedSize));
	if (size != nullptr) *size = pixmap.size();
	return pixmap;
}
#endif

QPixmap IconImageProvider::missingPixmap(const QSize& size) {
	auto width = size.width() % 2 == 0 ? size.width() : size.width() + 1;
	auto height = size.height() % 2 == 0 ? size.height() : size.height() + 1;
	width = std::max(width, 2);
	height = std::max(height, 2);

	auto pixmap = QPixmap(width, height);
	pixmap.fill(QColorConstants::Black);
	auto painter = QPainter(&pixmap);

	auto halfWidth = width / 2;
	auto halfHeight = height / 2;
	auto purple = QColor(0xd900d8);
	painter.fillRect(halfWidth, 0, halfWidth, halfHeight, purple);
	painter.fillRect(0, halfHeight, halfWidth, halfHeight, purple);
	return pixmap;
}

QString IconImageProvider::requestString(
    const QString& icon,
    const QString& path,
    const QString& fallback
) {
	auto req = "image://icon/" + icon;

	if (!path.isEmpty()) {
		req += "?path=" + path;
	}

	if (!fallback.isEmpty()) {
		req += "?fallback=" + fallback;
	}

	return req;
}
