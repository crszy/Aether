#pragma once

#include <qhash.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qset.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>

class QThreadPool;
class QTimer;

namespace qs::windows::sys {

enum class BrightnessMethod : quint8 {
	Unresolved,
	Wmi,
	Lcd,
	Ddc,
	Software,
};

struct BrightnessRoute {
	BrightnessMethod method = BrightnessMethod::Unresolved;
	QString wmiInstance;
	QString wmiMethodsPath;
	bool external = false;
	bool lcdTried = false;
};

class Brightness: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Brightness(QObject* parent = nullptr);
	~Brightness() override;
	Q_DISABLE_COPY_MOVE(Brightness);

	Q_INVOKABLE void query(const QString& screenName);

	Q_INVOKABLE void probe(const QString& screenName);

	Q_INVOKABLE void refresh(const QString& screenName);

	Q_INVOKABLE void setBrightness(const QString& screenName, bool isDdc, qreal value);

	Q_INVOKABLE QString methodFor(const QString& screenName) const;

signals:
	void queried(const QString& screenName, bool available, bool isDdc, qreal brightness);
	void brightnessSetFinished(const QString& screenName, bool ok);
	void routeChanged(const QString& screenName, const QString& method);
	void systemBrightnessChanged(const QString& screenName, qreal brightness);

private:
	void startProbe(const QString& screenName, bool readDdc);
	void startSet(const QString& screenName, qreal value);
	void finishSet(
	    const QString& screenName,
	    BrightnessMethod method,
	    qreal value,
	    bool ok,
	    const QString& error
	);
	void useSoftware(const QString& screenName, qreal value, const QString& reason);
	void settle(const QString& screenName);
	void startReadBack(const QString& screenName);
	void onSystemBrightness(const QString& instanceName, int percent);
	[[nodiscard]] bool busy(const QString& screenName) const;
	void ensureEventWatch();

	QThreadPool* pool = nullptr;
	QHash<QString, BrightnessRoute> routes;
	QHash<QString, qreal> pending;
	QSet<QString> inFlight;
	QHash<QString, qreal> lastValue;
	QHash<QString, quint32> generation;
	QHash<QString, QTimer*> settleTimers;
};

} // namespace qs::windows::sys
