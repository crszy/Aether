#pragma once

#include <qhash.h>
#include <qobject.h>
#include <qstring.h>
#include <qt_windows.h>

namespace qs::windows::sys {

class GammaController: public QObject {
	Q_OBJECT;

public:
	static GammaController* instance();

	void setTemperature(int kelvin);
	void setBrightness(const QString& screenName, qreal brightness);
	void setDim(const QString& screenName, qreal factor);
	qreal probeDimFloor(const QString& screenName);
	[[nodiscard]] bool dims(const QString& screenName) const;

	void restoreAll();

private:
	explicit GammaController(QObject* parent);

	struct Ramp {
		WORD values[3][256]; // NOLINT
	};

	void reapply(const QString& screenName);
	bool apply(const QString& screenName, qreal brightness);
	qreal floorFor(const QString& screenName);

	int mKelvin = 0;
	QHash<QString, qreal> backlightFactor;
	QHash<QString, qreal> dimFactor;
	QHash<QString, qreal> floors;
	QHash<QString, Ramp> originals;
};

} // namespace qs::windows::sys
