#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>

namespace qs::windows::image {

class Thumbnailer: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Thumbnailer(QObject* parent = nullptr): QObject(parent) {}

	Q_INVOKABLE void generate(const QString& sourcePath, const QString& outputPath, int maxSize);

signals:
	void finished(const QString& sourcePath, const QString& outputPath, bool ok);
};

} // namespace qs::windows::image
