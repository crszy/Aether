#pragma once

#include <memory>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::windows::sys {

class OcrBackend;

class Ocr: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Ocr(QObject* parent = nullptr);
	~Ocr() override;
	Q_DISABLE_COPY_MOVE(Ocr);

	Q_INVOKABLE int recognizeText(const QString& path);

	void backendDone(
	    int requestId,
	    const QString& text,
	    bool ok,
	    const QString& error,
	    const QVariantList& lines
	);

signals:
	void recognized(
	    int requestId,
	    const QString& text,
	    bool ok,
	    const QString& error,
	    const QVariantList& lines
	);

private:
	std::unique_ptr<OcrBackend> mBackend;
	int mNextRequestId = 1;
};

} // namespace qs::windows::sys
