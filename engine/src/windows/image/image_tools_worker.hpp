#pragma once

#include <qhash.h>
#include <qlist.h>
#include <qobject.h>
#include <qpointer.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::windows::image {

class ImageTools;

class ImageToolsWorker: public QObject {
	Q_OBJECT;

public:
	explicit ImageToolsWorker(ImageTools* frontend);
	~ImageToolsWorker() override;
	Q_DISABLE_COPY_MOVE(ImageToolsWorker);

	void cmdLeastBusyRegion(
	    int requestId,
	    QString imagePath,
	    int width,
	    int height,
	    int screenWidth,
	    int screenHeight,
	    int horizontalPadding,
	    int verticalPadding,
	    bool busiest
	);

	void cmdScheme(int requestId, const QString& imagePath);

	void cmdFindContentRegions(
	    int requestId,
	    const QString& imagePath,
	    int minWidth,
	    int minHeight,
	    int maxWidth,
	    int maxHeight
	);

private:
	static constexpr qsizetype MaxCacheEntries = 64;

	QPointer<ImageTools> mFrontend;
	QHash<QString, QVariantMap> mCache;
	QList<QString> mCacheOrder;
};

} // namespace qs::windows::image
