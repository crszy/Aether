#pragma once

#include <qobject.h>
#include <qpointer.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>

#include <winrt/Windows.Media.Ocr.h>

namespace qs::windows::sys {

class Ocr;

class OcrWorker: public QObject {
	Q_OBJECT;

public:
	explicit OcrWorker(Ocr* frontend);
	~OcrWorker() override;
	Q_DISABLE_COPY_MOVE(OcrWorker);

	void cmdRecognize(int requestId, const QString& path);

public slots:
	void start();
	void shutdown();

private:
	[[nodiscard]] winrt::Windows::Media::Ocr::OcrEngine engine();

	QPointer<Ocr> mFrontend;

	bool mApartmentInitialized = false;
	winrt::Windows::Media::Ocr::OcrEngine mEngine {nullptr};
};

} // namespace qs::windows::sys
