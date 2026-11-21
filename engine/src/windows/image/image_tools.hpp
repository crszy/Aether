#pragma once

#include <memory>

#include <qobject.h>
#include <qsize.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::windows::image {

class ImageToolsBackend;

class ImageTools: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit ImageTools(QObject* parent = nullptr);
	~ImageTools() override;
	Q_DISABLE_COPY_MOVE(ImageTools);

	Q_INVOKABLE static QVariantMap leastBusyRegion(
	    const QString& imagePath,
	    int width,
	    int height,
	    int screenWidth,
	    int screenHeight,
	    int horizontalPadding,
	    int verticalPadding,
	    bool busiest
	);

	Q_INVOKABLE static QVariantMap textColorFromImage(const QString& imagePath);

	Q_INVOKABLE static QString schemeForImage(const QString& imagePath);

	Q_INVOKABLE static QSize imageSize(const QString& imagePath);

	Q_INVOKABLE int requestLeastBusyRegion(
	    const QString& imagePath,
	    int width,
	    int height,
	    int screenWidth,
	    int screenHeight,
	    int horizontalPadding,
	    int verticalPadding,
	    bool busiest
	);

	Q_INVOKABLE int requestSchemeForImage(const QString& imagePath);

	Q_INVOKABLE static QVariantList findContentRegions(
	    const QString& imagePath,
	    int minWidth,
	    int minHeight,
	    int maxWidth,
	    int maxHeight
	);

	Q_INVOKABLE int requestFindContentRegions(
	    const QString& imagePath,
	    int minWidth,
	    int minHeight,
	    int maxWidth,
	    int maxHeight
	);

	void backendDone(int requestId, const QVariantMap& result);
	void backendSchemeDone(int requestId, const QString& scheme);
	void backendFindContentRegionsDone(int requestId, const QVariantList& regions);

signals:
	void leastBusyRegionReady(int requestId, const QVariantMap& result);
	void schemeForImageReady(int requestId, const QString& scheme);
	void findContentRegionsReady(int requestId, const QVariantList& regions);

private:
	std::unique_ptr<ImageToolsBackend> mBackend;
	int mNextRequestId = 1;
};

} // namespace qs::windows::image
