#pragma once

#include <qpixmap.h>
#include <qquickimageprovider.h>
#include <qsize.h>
#include <qstring.h>

#ifdef Q_OS_WIN
using IconImageProviderBase = QQuickAsyncImageProvider;
#else
using IconImageProviderBase = QQuickImageProvider;
#endif

class IconImageProvider: public IconImageProviderBase {
public:
#ifdef Q_OS_WIN
	QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;
#else
	explicit IconImageProvider(): QQuickImageProvider(QQuickImageProvider::Pixmap) {}

	QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override;
#endif

	static QPixmap missingPixmap(const QSize& size);

	static QString requestString(
	    const QString& icon,
	    const QString& path = QString(),
	    const QString& fallback = QString()
	);
};
