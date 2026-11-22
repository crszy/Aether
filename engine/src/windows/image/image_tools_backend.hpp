#pragma once

#include <memory>

#include <qobject.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <qtmetamacros.h>

namespace qs::windows::image {

class ImageTools;
class ImageToolsWorker;

class ImageToolsBackend: public QObject {
	Q_OBJECT;

public:
	explicit ImageToolsBackend(ImageTools* frontend);
	~ImageToolsBackend() override;
	Q_DISABLE_COPY_MOVE(ImageToolsBackend);

	void requestLeastBusyRegion(
	    int requestId,
	    const QString& imagePath,
	    int width,
	    int height,
	    int screenWidth,
	    int screenHeight,
	    int horizontalPadding,
	    int verticalPadding,
	    bool busiest
	);

	void requestScheme(int requestId, const QString& imagePath);

	void requestFindContentRegions(
	    int requestId,
	    const QString& imagePath,
	    int minWidth,
	    int minHeight,
	    int maxWidth,
	    int maxHeight
	);

private:
	void ensureStarted();

	std::unique_ptr<QThread> mThread;
	ImageToolsWorker* mWorker = nullptr;
	bool mStarted = false;
};

} // namespace qs::windows::image
