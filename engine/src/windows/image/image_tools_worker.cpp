#include "image_tools_worker.hpp"

#include <qcoreapplication.h>
#include <qdatetime.h>
#include <qfileinfo.h>
#include <qmetaobject.h>
#include <qstring.h>

#include "../../core/backgroundpool.hpp"
#include "image_tools.hpp"

namespace qs::windows::image {

namespace {

QString makeCacheKey(
    const QString& imagePath,
    qint64 mtimeMs,
    qint64 fileSize,
    int width,
    int height,
    int screenWidth,
    int screenHeight,
    int horizontalPadding,
    int verticalPadding,
    bool busiest
) {
	return imagePath + u'|' + QString::number(mtimeMs) + u'|' + QString::number(fileSize) + u'|'
	     + QString::number(width) + u'x' + QString::number(height) + u'|'
	     + QString::number(screenWidth) + u'x' + QString::number(screenHeight) + u'|'
	     + QString::number(horizontalPadding) + u',' + QString::number(verticalPadding) + u'|'
	     + (busiest ? QStringLiteral("busiest") : QStringLiteral("quiet"));
}

} // namespace

ImageToolsWorker::ImageToolsWorker(ImageTools* frontend): mFrontend(frontend) {}

ImageToolsWorker::~ImageToolsWorker() = default;

void ImageToolsWorker::cmdLeastBusyRegion(
    int requestId,
    QString imagePath,
    int width,
    int height,
    int screenWidth,
    int screenHeight,
    int horizontalPadding,
    int verticalPadding,
    bool busiest
) {
	auto scope = BackgroundWorkScope();

	QFileInfo info(imagePath);
	auto mtimeMs = info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1;
	auto fileSize = info.exists() ? info.size() : -1;

	auto key = makeCacheKey(
	    imagePath,
	    mtimeMs,
	    fileSize,
	    width,
	    height,
	    screenWidth,
	    screenHeight,
	    horizontalPadding,
	    verticalPadding,
	    busiest
	);

	QVariantMap result;
	auto cached = this->mCache.constFind(key);
	if (cached != this->mCache.constEnd()) {
		result = cached.value();
	} else {
		result = ImageTools::leastBusyRegion(
		    imagePath,
		    width,
		    height,
		    screenWidth,
		    screenHeight,
		    horizontalPadding,
		    verticalPadding,
		    busiest
		);

		this->mCache.insert(key, result);
		this->mCacheOrder.append(key);
		if (this->mCacheOrder.size() > ImageToolsWorker::MaxCacheEntries) {
			auto oldest = this->mCacheOrder.takeFirst();
			this->mCache.remove(oldest);
		}
	}

	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [frontend = this->mFrontend, requestId, result] {
		    if (frontend) frontend->backendDone(requestId, result);
	    },
	    Qt::QueuedConnection
	);
}

void ImageToolsWorker::cmdScheme(int requestId, const QString& imagePath) {
	auto scheme = ImageTools::schemeForImage(imagePath);

	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [frontend = this->mFrontend, requestId, scheme] {
		    if (frontend) frontend->backendSchemeDone(requestId, scheme);
	    },
	    Qt::QueuedConnection
	);
}

void ImageToolsWorker::cmdFindContentRegions(
    int requestId,
    const QString& imagePath,
    int minWidth,
    int minHeight,
    int maxWidth,
    int maxHeight
) {
	auto scope = BackgroundWorkScope();

	auto regions = ImageTools::findContentRegions(imagePath, minWidth, minHeight, maxWidth, maxHeight);

	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [frontend = this->mFrontend, requestId, regions] {
		    if (frontend) frontend->backendFindContentRegionsDone(requestId, regions);
	    },
	    Qt::QueuedConnection
	);
}

} // namespace qs::windows::image
