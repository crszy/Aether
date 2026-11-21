#pragma once

#include <memory>

#include <qhash.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"
#include "adapter.hpp"
#include "backend_types.hpp"
#include "device.hpp"

namespace qs::bluetooth {

class BtBackend;

class WinBluetooth: public QObject {
	Q_OBJECT;

public:
	static WinBluetooth* instance();

	[[nodiscard]] ObjectModel<BluetoothAdapter>* adapters() { return &this->mAdapters; }
	[[nodiscard]] ObjectModel<BluetoothDevice>* devices() { return &this->mDevices; }
	[[nodiscard]] BluetoothAdapter* adapter() const { return this->mAdapter; }
	[[nodiscard]] BtBackend* backend() const { return this->mBackend.get(); }
	[[nodiscard]] bool isScanned() const { return this->mScanned; }

	void backendScanned(bool hasAdapter);
	void backendAdapterChanged(const AdapterSnapshot& snapshot);
	void backendAdapterRemoved();
	void backendDiscoveringChanged(bool discovering);
	void backendDeviceChanged(const DeviceSnapshot& snapshot);
	void backendDeviceRemoved(const QString& key);
	void backendPairFinished(const QString& key, bool paired);
	void backendConnectFinished(const QString& key, bool connect, ConnectResult result);

signals:
	void defaultAdapterChanged();
	void scanned(bool hasAdapter);

private:
	explicit WinBluetooth();
	~WinBluetooth() override;

	ObjectModel<BluetoothAdapter> mAdapters {this};
	ObjectModel<BluetoothDevice> mDevices {this};
	QHash<QString, BluetoothDevice*> mDeviceMap;
	BluetoothAdapter* mAdapter = nullptr;
	bool mScanned = false;
	std::unique_ptr<BtBackend> mBackend;

public:
	Q_OBJECT_BINDABLE_PROPERTY(
	    WinBluetooth,
	    BluetoothAdapter*,
	    bDefaultAdapter,
	    &WinBluetooth::defaultAdapterChanged
	);
};

class BluetoothQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(Bluetooth);
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(qs::bluetooth::BluetoothAdapter* defaultAdapter READ default NOTIFY defaultAdapterChanged BINDABLE bindableDefaultAdapter);
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::bluetooth::BluetoothAdapter>*);
	Q_PROPERTY(UntypedObjectModel* adapters READ adapters CONSTANT);
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::bluetooth::BluetoothDevice>*);
	Q_PROPERTY(UntypedObjectModel* devices READ devices CONSTANT);
	// clang-format on

signals:
	void defaultAdapterChanged();

public:
	explicit BluetoothQml();

	[[nodiscard]] static ObjectModel<BluetoothAdapter>* adapters() {
		return WinBluetooth::instance()->adapters();
	}

	[[nodiscard]] static ObjectModel<BluetoothDevice>* devices() {
		return WinBluetooth::instance()->devices();
	}

	[[nodiscard]] static QBindable<BluetoothAdapter*> bindableDefaultAdapter() {
		return &WinBluetooth::instance()->bDefaultAdapter;
	}
};

} // namespace qs::bluetooth
