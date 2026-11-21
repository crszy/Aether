#pragma once

#include <memory>

#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qthread.h>
#include <qtypes.h>

namespace qs::bluetooth {

class WinBluetooth;
class BtWorker;

class BtBackend {
public:
	explicit BtBackend(WinBluetooth* frontend);
	~BtBackend();
	Q_DISABLE_COPY_MOVE(BtBackend);

	void setPowered(bool on, quint64 seq);
	void setDiscovering(bool on);
	void pair(const QString& key);
	void cancelPair(const QString& key);
	void forget(const QString& key);
	void connectDevice(const QString& key, bool connect);

	void stop();

private:
	std::unique_ptr<QThread> mThread;
	BtWorker* mWorker = nullptr;
};

} // namespace qs::bluetooth
