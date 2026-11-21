#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <qhash.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qset.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.h>

#include "backend_types.hpp"

class QTimer;

namespace qs::bluetooth {

class WinBluetooth;
class BtWorkerGate;

class BtWorker: public QObject {
	Q_OBJECT;

public:
	explicit BtWorker(WinBluetooth* frontend);
	~BtWorker() override;
	Q_DISABLE_COPY_MOVE(BtWorker);

	void cmdSetPowered(bool on, quint64 seq);
	void cmdSetDiscovering(bool on);
	void cmdPair(const QString& key);
	void cmdCancelPair(const QString& key);
	void cmdForget(const QString& key);
	void cmdConnect(const QString& key, bool connect);

public slots:
	void start();
	void shutdown();

private:
	enum Source : quint8 {
		PairedClassic = 0,
		PairedLe = 1,
		UnpairedClassic = 2,
		UnpairedLe = 3,
		SourceCount = 4,
	};

	struct WatcherSlot {
		winrt::Windows::Devices::Enumeration::DeviceWatcher watcher {nullptr};
		winrt::event_token added {};
		winrt::event_token updated {};
		winrt::event_token removed {};
		winrt::event_token stopped {};
		quint64 generation = 0;
	};

	struct Transport {
		QString id;
		winrt::Windows::Devices::Enumeration::DeviceInformation info {nullptr};
		bool inPaired = false;
		bool inUnpaired = false;
		bool objectRequested = false;
		winrt::Windows::Devices::Bluetooth::BluetoothDevice classicDevice {nullptr};
		winrt::Windows::Devices::Bluetooth::BluetoothLEDevice leDevice {nullptr};
		winrt::event_token connectionToken {};
		winrt::event_token nameToken {};
	};

	struct Entry {
		QString key;
		std::unique_ptr<Transport> classic;
		std::unique_ptr<Transport> le;
		std::optional<int> battery;
		bool justPaired = false;
		quint64 removalSerial = 0;
		winrt::Windows::Foundation::IAsyncOperation<
		    winrt::Windows::Devices::Enumeration::DevicePairingResult>
		    pairOp {nullptr};
		winrt::Windows::Devices::Enumeration::DeviceInformationCustomPairing customPairing {nullptr};
		winrt::event_token pairingToken {};
		DeviceSnapshot last;
		bool published = false;
	};

	static bool isLe(Source source) { return source == PairedLe || source == UnpairedLe; }
	static bool isPairedSource(Source source) {
		return source == PairedClassic || source == PairedLe;
	}

	void scheduleAdapterRefresh();
	void refreshAdapter();
	void setUpAdapter(const winrt::Windows::Devices::Bluetooth::BluetoothAdapter& adapter);
	void tearDownAdapter();
	void publishAdapter();
	[[nodiscard]] RadioPower currentPower() const;
	void onRadioStateChanged();
	void requestPower();
	void onPowerCompleted(
	    bool on,
	    quint64 seq,
	    winrt::Windows::Foundation::AsyncStatus status,
	    winrt::Windows::Devices::Radios::RadioAccessStatus access
	);

	void startWatcher(Source source);
	void stopWatcher(Source source);
	void updateDiscoveryWatchers();
	void onAdded(Source source, const winrt::Windows::Devices::Enumeration::DeviceInformation& info);
	void onUpdated(Source source, const winrt::Windows::Devices::Enumeration::DeviceInformationUpdate& update);
	void onRemoved(Source source, const winrt::Windows::Devices::Enumeration::DeviceInformationUpdate& update);
	void onWatcherStopped(Source source, quint64 generation);

	Entry* findEntry(const QString& key);
	Transport* transportForId(Entry& entry, const QString& id, bool* le);
	void ensureDeviceObject(Entry& entry, bool le);
	void onClassicDeviceReady(
	    const QString& key,
	    const QString& id,
	    const winrt::Windows::Devices::Bluetooth::BluetoothDevice& device
	);
	void onLeDeviceReady(
	    const QString& key,
	    const QString& id,
	    const winrt::Windows::Devices::Bluetooth::BluetoothLEDevice& device
	);
	void watchDeviceObject(Transport& transport, const QString& key);
	static void releaseDeviceObject(Transport& transport);
	void dropTransport(Entry& entry, bool le);
	void scheduleRemoval(const QString& key);
	void removeEntry(const QString& key);
	void onConnectionChanged(const QString& key);
	void publish(Entry& entry);
	[[nodiscard]] DeviceSnapshot buildSnapshot(const Entry& entry) const;
	[[nodiscard]] static bool transportConnected(const Transport* transport);
	[[nodiscard]] static std::optional<winrt::guid> containerIdOf(const Entry& entry);
	void refreshBattery(Entry& entry);
	void pollBatteries();

	void onPairCompleted(
	    const QString& key,
	    winrt::Windows::Foundation::AsyncStatus status,
	    winrt::Windows::Devices::Enumeration::DevicePairingResultStatus result
	);
	void finishPairing(Entry& entry);

	std::shared_ptr<BtWorkerGate> mGate;
	WinBluetooth* mFrontend;
	bool mApartmentInitialized = false;
	QTimer* mBatteryTimer = nullptr;

	winrt::Windows::Devices::Enumeration::DeviceWatcher mAdapterWatcher {nullptr};
	winrt::event_token mAdapterAddedToken {};
	winrt::event_token mAdapterRemovedToken {};
	winrt::event_token mAdapterUpdatedToken {};
	bool mAdapterRefreshQueued = false;
	bool mAdapterScanned = false;

	winrt::Windows::Devices::Bluetooth::BluetoothAdapter mAdapter {nullptr};
	winrt::Windows::Devices::Radios::Radio mRadio {nullptr};
	winrt::event_token mRadioStateToken {};
	AdapterSnapshot mAdapterSnapshot;
	RadioPower mLastPower = RadioPower::Unknown;

	bool mPowerInFlight = false;
	bool mPowerWanted = false;
	quint64 mPowerSeqWanted = 0;
	quint64 mPowerSeqDone = 0;
	bool mRadioAccessRequested = false;

	bool mDiscoveryWanted = false;
	bool mDiscovering = false;
	std::array<WatcherSlot, SourceCount> mWatchers;

	std::unordered_map<QString, std::unique_ptr<Entry>> mEntries;
	QHash<QString, QString> mIdToKey;
	QSet<QString> mUnsupportedConnectLogged;
};

class BtWorkerGate {
public:
	explicit BtWorkerGate(BtWorker* worker): mWorker(worker) {}

	template <typename F>
	void post(F&& fn) {
		std::lock_guard lock(this->mMutex);
		if (this->mWorker == nullptr) return;
		auto* worker = this->mWorker;
		QMetaObject::invokeMethod(
		    worker,
		    [worker, fn = std::forward<F>(fn)]() mutable { fn(worker); },
		    Qt::QueuedConnection
		);
	}

	void close() {
		std::lock_guard lock(this->mMutex);
		this->mWorker = nullptr;
	}

private:
	std::mutex mMutex;
	BtWorker* mWorker;
};

} // namespace qs::bluetooth
