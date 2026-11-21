#include "adapter.hpp"

#include <qdebug.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qproperty.h>
#include <qstring.h>
#include <qtypes.h>

#include "backend_types.hpp"
#include "bluetooth.hpp"
#include "bt_backend.hpp"

namespace qs::bluetooth {

namespace {
Q_LOGGING_CATEGORY(logAdapter, "quickshell.windows.bluetooth.adapter", QtWarningMsg);

quint64 nextPowerSeq() {
	static quint64 seq = 0;
	return ++seq;
}

BluetoothAdapterState::Enum stateForPower(RadioPower power) {
	switch (power) {
	case RadioPower::On: return BluetoothAdapterState::Enabled;
	case RadioPower::Blocked: return BluetoothAdapterState::Blocked;
	default: return BluetoothAdapterState::Disabled;
	}
}
} // namespace

QString BluetoothAdapterState::toString(BluetoothAdapterState::Enum state) {
	switch (state) {
	case BluetoothAdapterState::Disabled: return QStringLiteral("Disabled");
	case BluetoothAdapterState::Enabled: return QStringLiteral("Enabled");
	case BluetoothAdapterState::Enabling: return QStringLiteral("Enabling");
	case BluetoothAdapterState::Disabling: return QStringLiteral("Disabling");
	case BluetoothAdapterState::Blocked: return QStringLiteral("Blocked");
	default: return QStringLiteral("Unknown");
	}
}

BluetoothAdapter::BluetoothAdapter(const AdapterSnapshot& snapshot, QObject* parent)
    : QObject(parent)
    , mId(snapshot.id) {
	this->bName = snapshot.name;
	this->applyPower(snapshot.power);
}

void BluetoothAdapter::applySnapshot(const AdapterSnapshot& snapshot) {
	Qt::beginPropertyUpdateGroup();
	this->bName = snapshot.name;
	if (snapshot.powerSeqDone >= this->mPowerSeq) this->applyPower(snapshot.power);
	Qt::endPropertyUpdateGroup();
}

void BluetoothAdapter::applyPower(RadioPower power) {
	if (power == RadioPower::Unknown && !this->mUnknownPowerWarned) {
		this->mUnknownPowerWarned = true;
		qCWarning(logAdapter) << "Bluetooth radio state unknown; reporting the adapter as disabled";
	}

	this->bEnabled = power == RadioPower::On;
	this->bState = stateForPower(power);
}

void BluetoothAdapter::applyDiscovering(bool discovering) { this->bDiscovering = discovering; }

void BluetoothAdapter::setEnabled(bool enabled) {
	if (enabled == this->bEnabled) return;

	if (enabled && this->bState == BluetoothAdapterState::Blocked) {
		qCCritical(logAdapter) << "Cannot enable adapter because it is blocked by a hardware switch.";
		return;
	}

	Qt::beginPropertyUpdateGroup();
	this->bEnabled = enabled;
	this->bState = enabled ? BluetoothAdapterState::Enabling : BluetoothAdapterState::Disabling;
	Qt::endPropertyUpdateGroup();

	this->mPowerSeq = nextPowerSeq();
	WinBluetooth::instance()->backend()->setPowered(enabled, this->mPowerSeq);
}

void BluetoothAdapter::setDiscoverable(bool discoverable) {
	if (discoverable == this->bDiscoverable) return;
	if (!this->mDiscoverableWarned) {
		this->mDiscoverableWarned = true;
		qCWarning(logAdapter) << "Setting discoverable isn't supported on Windows (it has no public "
		                         "API; the PC is discoverable while its Bluetooth settings are open)";
	}
}

void BluetoothAdapter::setDiscovering(bool discovering) {
	if (discovering) {
		this->startDiscovery();
	} else {
		this->stopDiscovery();
	}
}

void BluetoothAdapter::startDiscovery() {
	qCDebug(logAdapter) << "Starting discovery for adapter" << this;
	WinBluetooth::instance()->backend()->setDiscovering(true);
}

void BluetoothAdapter::stopDiscovery() {
	qCDebug(logAdapter) << "Stopping discovery for adapter" << this;
	WinBluetooth::instance()->backend()->setDiscovering(false);
}

void BluetoothAdapter::setDiscoverableTimeout(quint32 timeout) { this->bDiscoverableTimeout = timeout; }
void BluetoothAdapter::setPairable(bool pairable) { this->bPairable = pairable; }
void BluetoothAdapter::setPairableTimeout(quint32 timeout) { this->bPairableTimeout = timeout; }

} // namespace qs::bluetooth

QDebug operator<<(QDebug debug, const qs::bluetooth::BluetoothAdapter* adapter) {
	auto saver = QDebugStateSaver(debug);

	if (adapter) {
		debug.nospace() << "BluetoothAdapter(" << static_cast<const void*>(adapter)
		                << ", id=" << adapter->adapterId() << ")";
	} else {
		debug << "BluetoothAdapter(nullptr)";
	}

	return debug;
}
