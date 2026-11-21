#include "bluetooth.hpp"

#include <memory>

#include <qcoreapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qstring.h>

#include "adapter.hpp"
#include "backend_types.hpp"
#include "bt_backend.hpp"
#include "device.hpp"

namespace qs::bluetooth {

namespace {
Q_LOGGING_CATEGORY(logBluetooth, "quickshell.windows.bluetooth", QtWarningMsg);
}

WinBluetooth* WinBluetooth::instance() {
	static auto* instance = new WinBluetooth();
	return instance;
}

WinBluetooth::WinBluetooth() {
	this->mBackend = std::make_unique<BtBackend>(this);

	if (auto* app = QCoreApplication::instance()) {
		QObject::connect(app, &QCoreApplication::aboutToQuit, this, [this] { this->mBackend->stop(); });
	}
}

WinBluetooth::~WinBluetooth() = default;

void WinBluetooth::backendScanned(bool hasAdapter) {
	this->mScanned = true;
	qCDebug(logBluetooth) << "Initial adapter lookup done, adapter present:" << hasAdapter;
	emit this->scanned(hasAdapter);
}

void WinBluetooth::backendAdapterChanged(const AdapterSnapshot& snapshot) {
	if (this->mAdapter && this->mAdapter->adapterId() == snapshot.id) {
		this->mAdapter->applySnapshot(snapshot);
		return;
	}

	if (this->mAdapter) this->backendAdapterRemoved();

	auto* adapter = new BluetoothAdapter(snapshot, this);
	qCDebug(logBluetooth) << "Tracked new adapter" << adapter;

	for (auto* device: this->mDevices.valueList()) {
		adapter->devices()->insertObject(device);
	}

	this->mAdapter = adapter;
	this->mAdapters.insertObject(adapter);
	this->bDefaultAdapter = adapter;

	for (auto* device: this->mDevices.valueList()) {
		emit device->adapterChanged();
	}
}

void WinBluetooth::backendAdapterRemoved() {
	auto* adapter = this->mAdapter;
	if (!adapter) return;

	qCDebug(logBluetooth) << "Adapter removed:" << adapter;
	this->mAdapter = nullptr;
	this->bDefaultAdapter = nullptr;
	this->mAdapters.removeObject(adapter);

	for (auto* device: this->mDevices.valueList()) {
		emit device->adapterChanged();
	}

	adapter->deleteLater();
}

void WinBluetooth::backendDiscoveringChanged(bool discovering) {
	if (this->mAdapter) this->mAdapter->applyDiscovering(discovering);
}

void WinBluetooth::backendDeviceChanged(const DeviceSnapshot& snapshot) {
	if (auto* device = this->mDeviceMap.value(snapshot.key)) {
		device->applySnapshot(snapshot);
		return;
	}

	auto* device = new BluetoothDevice(snapshot, this);
	qCDebug(logBluetooth) << "Tracked new device" << device << snapshot.name;

	this->mDeviceMap.insert(snapshot.key, device);
	if (this->mAdapter) this->mAdapter->devices()->insertObject(device);
	this->mDevices.insertObject(device);
}

void WinBluetooth::backendDeviceRemoved(const QString& key) {
	auto* device = this->mDeviceMap.take(key);
	if (!device) return;

	qCDebug(logBluetooth) << "Device removed:" << device;
	if (this->mAdapter) this->mAdapter->devices()->removeObject(device);
	this->mDevices.removeObject(device);
	device->deleteLater();
}

void WinBluetooth::backendPairFinished(const QString& key, bool paired) {
	if (auto* device = this->mDeviceMap.value(key)) device->applyPairFinished(paired);
}

void WinBluetooth::backendConnectFinished(const QString& key, bool connect, ConnectResult result) {
	if (auto* device = this->mDeviceMap.value(key)) device->applyConnectFinished(connect, result);
}

BluetoothQml::BluetoothQml() {
	QObject::connect(
	    WinBluetooth::instance(),
	    &WinBluetooth::defaultAdapterChanged,
	    this,
	    &BluetoothQml::defaultAdapterChanged
	);
}

} // namespace qs::bluetooth
