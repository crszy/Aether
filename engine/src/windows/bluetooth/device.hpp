#pragma once

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "backend_types.hpp"

namespace qs::bluetooth {

class BluetoothDeviceState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Disconnected = 0,
		Connected = 1,
		Disconnecting = 2,
		Connecting = 3,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::bluetooth::BluetoothDeviceState::Enum state);
};

class BluetoothAdapter;

class BluetoothDevice: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("");
	Q_MOC_INCLUDE("adapter.hpp");
	// clang-format off
	Q_PROPERTY(QString address READ default NOTIFY addressChanged BINDABLE bindableAddress);
	Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged);
	Q_PROPERTY(QString deviceName READ default NOTIFY deviceNameChanged BINDABLE bindableDeviceName);
	Q_PROPERTY(QString icon READ default NOTIFY iconChanged BINDABLE bindableIcon);
	Q_PROPERTY(qs::bluetooth::BluetoothDeviceState::Enum state READ default NOTIFY stateChanged BINDABLE bindableState);
	Q_PROPERTY(bool connected READ connected WRITE setConnected NOTIFY connectedChanged);
	Q_PROPERTY(bool paired READ default NOTIFY pairedChanged BINDABLE bindablePaired);
	Q_PROPERTY(bool bonded READ default NOTIFY bondedChanged BINDABLE bindableBonded);
	Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged);
	Q_PROPERTY(bool trusted READ trusted WRITE setTrusted NOTIFY trustedChanged);
	Q_PROPERTY(bool blocked READ blocked WRITE setBlocked NOTIFY blockedChanged);
	Q_PROPERTY(bool wakeAllowed READ wakeAllowed WRITE setWakeAllowed NOTIFY wakeAllowedChanged);
	Q_PROPERTY(bool batteryAvailable READ batteryAvailable NOTIFY batteryAvailableChanged);
	Q_PROPERTY(qreal battery READ default NOTIFY batteryChanged BINDABLE bindableBattery);
	Q_PROPERTY(qs::bluetooth::BluetoothAdapter* adapter READ adapter NOTIFY adapterChanged);
	Q_PROPERTY(QString dbusPath READ path CONSTANT);
	// clang-format on

public:
	explicit BluetoothDevice(const DeviceSnapshot& snapshot, QObject* parent = nullptr);

	Q_INVOKABLE void connect();
	Q_INVOKABLE void disconnect();
	Q_INVOKABLE void pair();
	Q_INVOKABLE void cancelPair();
	Q_INVOKABLE void forget();

	[[nodiscard]] QString key() const { return this->mKey; }
	[[nodiscard]] QString path() const { return this->mPath; }

	[[nodiscard]] bool batteryAvailable() const { return this->mBatteryAvailable; }
	[[nodiscard]] BluetoothAdapter* adapter() const;

	[[nodiscard]] bool connected() const { return this->bConnected; }
	void setConnected(bool connected);

	[[nodiscard]] bool trusted() const { return this->bTrusted; }
	void setTrusted(bool trusted);

	[[nodiscard]] bool blocked() const { return this->bBlocked; }
	void setBlocked(bool blocked);

	[[nodiscard]] QString name() const { return this->bName; }
	void setName(const QString& name);

	[[nodiscard]] bool wakeAllowed() const { return this->bWakeAllowed; }
	void setWakeAllowed(bool wakeAllowed);

	[[nodiscard]] bool pairing() const { return this->bPairing; }

	[[nodiscard]] QBindable<QString> bindableAddress() { return &this->bAddress; }
	[[nodiscard]] QBindable<QString> bindableDeviceName() { return &this->bDeviceName; }
	[[nodiscard]] QBindable<QString> bindableName() { return &this->bName; }
	[[nodiscard]] QBindable<bool> bindableConnected() { return &this->bConnected; }
	[[nodiscard]] QBindable<bool> bindablePaired() { return &this->bPaired; }
	[[nodiscard]] QBindable<bool> bindableBonded() { return &this->bBonded; }
	[[nodiscard]] QBindable<bool> bindableTrusted() { return &this->bTrusted; }
	[[nodiscard]] QBindable<bool> bindableBlocked() { return &this->bBlocked; }
	[[nodiscard]] QBindable<bool> bindableWakeAllowed() { return &this->bWakeAllowed; }
	[[nodiscard]] QBindable<QString> bindableIcon() { return &this->bIcon; }
	[[nodiscard]] QBindable<qreal> bindableBattery() { return &this->bBattery; }
	[[nodiscard]] QBindable<BluetoothDeviceState::Enum> bindableState() { return &this->bState; }

	void applySnapshot(const DeviceSnapshot& snapshot);
	void applyPairFinished(bool paired);
	void applyConnectFinished(bool connect, ConnectResult result);

signals:
	void addressChanged();
	void deviceNameChanged();
	void nameChanged();
	void connectedChanged();
	void stateChanged();
	void pairedChanged();
	void bondedChanged();
	void pairingChanged();
	void trustedChanged();
	void blockedChanged();
	void wakeAllowedChanged();
	void iconChanged();
	void batteryAvailableChanged();
	void batteryChanged();
	void adapterChanged();

private:
	void onConnectedChanged();
	void settleState();
	void warnUnsupported(const char* what);

	QString mKey;
	QString mPath;
	bool mBatteryAvailable = false;
	bool mUnsupportedWarned = false;
	QTimer mStateTimeout;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QString, bAddress, &BluetoothDevice::addressChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QString, bDeviceName, &BluetoothDevice::deviceNameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QString, bName, &BluetoothDevice::nameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bConnected, &BluetoothDevice::onConnectedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bPaired, &BluetoothDevice::pairedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bBonded, &BluetoothDevice::bondedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bTrusted, &BluetoothDevice::trustedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bBlocked, &BluetoothDevice::blockedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bWakeAllowed, &BluetoothDevice::wakeAllowedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QString, bIcon, &BluetoothDevice::iconChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, qreal, bBattery, &BluetoothDevice::batteryChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, BluetoothDeviceState::Enum, bState, &BluetoothDevice::stateChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bPairing, &BluetoothDevice::pairingChanged);
	// clang-format on
};

} // namespace qs::bluetooth

QDebug operator<<(QDebug debug, const qs::bluetooth::BluetoothDevice* device);
