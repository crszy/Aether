#pragma once

#include <memory>

#include <qobject.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <qtmetamacros.h>

namespace qs::windows::sys {

class Ocr;
class OcrWorker;

class OcrBackend: public QObject {
	Q_OBJECT;

public:
	explicit OcrBackend(Ocr* frontend);
	~OcrBackend() override;
	Q_DISABLE_COPY_MOVE(OcrBackend);

	void requestRecognize(int requestId, const QString& path);

private:
	std::unique_ptr<QThread> mThread;
	OcrWorker* mWorker = nullptr;
	bool mStarted = false;
};

} // namespace qs::windows::sys
