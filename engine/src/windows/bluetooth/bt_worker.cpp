#include "bt_worker.hpp"

#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <qcoreapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qnamespace.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtypes.h>
#include <quuid.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>

#include "backend_types.hpp"
#include "bluetooth.hpp"
#include "device_class.hpp"
#include "ks_audio.hpp"

using WinRtAdapter = winrt::Windows::Devices::Bluetooth::BluetoothAdapter;
using WinRtDevice = winrt::Windows::Devices::Bluetooth::BluetoothDevice;
using WinRtLeDevice = winrt::Windows::Devices::Bluetooth::BluetoothLEDevice;
using winrt::Windows::Devices::Bluetooth::BluetoothConnectionStatus;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Devices::Radios;
using namespace winrt::Windows::Foundation;
using winrt::Windows::Foundation::Collections::IMapView;

namespace qs::bluetooth {

namespace {
Q_LOGGING_CATEGORY(logWorker, "quickshell.windows.bluetooth.worker", QtWarningMsg);

constexpr auto PROP_ADDRESS = L"System.Devices.Aep.DeviceAddress";
constexpr auto PROP_CONNECTED = L"System.Devices.Aep.IsConnected";
constexpr auto PROP_PAIRED = L"System.Devices.Aep.IsPaired";
constexpr auto PROP_CAN_PAIR = L"System.Devices.Aep.CanPair";
constexpr auto PROP_CONTAINER = L"System.Devices.Aep.ContainerId";
constexpr auto PROP_COD_MAJOR = L"System.Devices.Aep.Bluetooth.Cod.Major";
constexpr auto PROP_COD_MINOR = L"System.Devices.Aep.Bluetooth.Cod.Minor";
constexpr auto PROP_LE_APPEARANCE = L"System.Devices.Aep.Bluetooth.Le.Appearance";

constexpr auto PROP_BATTERY_LIFE = L"System.Devices.BatteryLife";
constexpr auto PROP_BT_BATTERY = L"{104EA319-6EE2-4701-BD47-8DDBF425BBE5} 2";

constexpr int ADAPTER_REFRESH_DELAY_MS = 250;
constexpr int REMOVAL_GRACE_MS = 3000;
constexpr int BATTERY_POLL_MS = 60'000;
constexpr int BATTERY_AFTER_CONNECT_MS = 5000;
constexpr int WATCHER_RETRY_MS = 5000;

QString toQString(const winrt::hstring& value) {
	return QString::fromWCharArray(value.c_str(), static_cast<qsizetype>(value.size()));
}

winrt::hstring toHString(const QString& value) {
	return {reinterpret_cast<const wchar_t*>(value.utf16()), static_cast<uint32_t>(value.size())};
}

QString hresultText(winrt::hresult code) {
	return QStringLiteral("0x%1").arg(static_cast<quint32>(code.value), 8, 16, QLatin1Char('0'));
}

std::vector<winrt::hstring> aepProperties() {
	return {
	    PROP_ADDRESS,
	    PROP_CONNECTED,
	    PROP_PAIRED,
	    PROP_CAN_PAIR,
	    PROP_CONTAINER,
	    PROP_COD_MAJOR,
	    PROP_COD_MINOR,
	    PROP_LE_APPEARANCE,
	};
}

IPropertyValue lookup(const IMapView<winrt::hstring, IInspectable>& props, const wchar_t* name) {
	try {
		if (props && props.HasKey(name)) return props.Lookup(name).try_as<IPropertyValue>();
	} catch (const winrt::hresult_error&) {
	}
	return nullptr;
}

IMapView<winrt::hstring, IInspectable> propsOf(const DeviceInformation& info) {
	if (!info) return nullptr;
	try {
		return info.Properties();
	} catch (const winrt::hresult_error&) {
		return nullptr;
	}
}

std::optional<quint32>
propUInt(const IMapView<winrt::hstring, IInspectable>& props, const wchar_t* name) {
	auto value = lookup(props, name);
	if (!value) return std::nullopt;

	try {
		switch (value.Type()) {
		case PropertyType::UInt8: return value.GetUInt8();
		case PropertyType::UInt16: return value.GetUInt16();
		case PropertyType::UInt32: return value.GetUInt32();
		case PropertyType::Int16: {
			auto v = value.GetInt16();
			if (v >= 0) return static_cast<quint32>(v);
			return std::nullopt;
		}
		case PropertyType::Int32: {
			auto v = value.GetInt32();
			if (v >= 0) return static_cast<quint32>(v);
			return std::nullopt;
		}
		default: return std::nullopt;
		}
	} catch (const winrt::hresult_error&) {
		return std::nullopt;
	}
}

std::optional<bool> propBool(const IMapView<winrt::hstring, IInspectable>& props, const wchar_t* name) {
	auto value = lookup(props, name);
	if (!value || value.Type() != PropertyType::Boolean) return std::nullopt;
	return value.GetBoolean();
}

QString propString(const IMapView<winrt::hstring, IInspectable>& props, const wchar_t* name) {
	auto value = lookup(props, name);
	if (!value || value.Type() != PropertyType::String) return {};
	return toQString(value.GetString());
}

std::optional<winrt::guid>
propGuid(const IMapView<winrt::hstring, IInspectable>& props, const wchar_t* name) {
	auto value = lookup(props, name);
	if (!value || value.Type() != PropertyType::Guid) return std::nullopt;
	return value.GetGuid();
}

QString normalizeAddress(const QString& raw) {
	QString hex;
	for (auto c: raw.trimmed()) {
		if (c == QLatin1Char(':') || c == QLatin1Char('-')) continue;
		if (!c.isDigit() && !(c.toLower() >= QLatin1Char('a') && c.toLower() <= QLatin1Char('f'))) {
			return {};
		}
		hex.append(c.toLower());
	}

	if (hex.size() != 12) return {};

	QString address;
	for (qsizetype i = 0; i < 12; i += 2) {
		if (i != 0) address.append(QLatin1Char(':'));
		address.append(hex.mid(i, 2));
	}
	return address;
}

QString addressOf(const DeviceInformation& info) {
	auto address = normalizeAddress(propString(propsOf(info), PROP_ADDRESS));
	if (!address.isEmpty()) return address;

	auto id = toQString(info.Id());
	return normalizeAddress(id.mid(id.lastIndexOf(QLatin1Char('-')) + 1));
}

QString formatAddress(quint64 raw) {
	QString address;
	for (int shift = 40; shift >= 0; shift -= 8) {
		if (!address.isEmpty()) address.append(QLatin1Char(':'));
		address.append(QStringLiteral("%1").arg((raw >> shift) & 0xff, 2, 16, QLatin1Char('0')));
	}
	return address.toUpper();
}

QString guidText(const winrt::guid& guid) {
	return QUuid(
	           guid.Data1,
	           guid.Data2,
	           guid.Data3,
	           guid.Data4[0],
	           guid.Data4[1],
	           guid.Data4[2],
	           guid.Data4[3],
	           guid.Data4[4],
	           guid.Data4[5],
	           guid.Data4[6],
	           guid.Data4[7]
	)
	    .toString(QUuid::WithBraces);
}

const char* accessName(RadioAccessStatus status) {
	switch (status) {
	case RadioAccessStatus::Allowed: return "Allowed";
	case RadioAccessStatus::DeniedByUser: return "DeniedByUser";
	case RadioAccessStatus::DeniedBySystem: return "DeniedBySystem";
	default: return "Unspecified";
	}
}

const char* pairingResultName(DevicePairingResultStatus status) {
	switch (status) {
	case DevicePairingResultStatus::Paired: return "Paired";
	case DevicePairingResultStatus::NotReadyToPair: return "NotReadyToPair";
	case DevicePairingResultStatus::NotPaired: return "NotPaired";
	case DevicePairingResultStatus::AlreadyPaired: return "AlreadyPaired";
	case DevicePairingResultStatus::ConnectionRejected: return "ConnectionRejected";
	case DevicePairingResultStatus::TooManyConnections: return "TooManyConnections";
	case DevicePairingResultStatus::HardwareFailure: return "HardwareFailure";
	case DevicePairingResultStatus::AuthenticationTimeout: return "AuthenticationTimeout";
	case DevicePairingResultStatus::AuthenticationNotAllowed: return "AuthenticationNotAllowed";
	case DevicePairingResultStatus::AuthenticationFailure: return "AuthenticationFailure";
	case DevicePairingResultStatus::NoSupportedProfiles: return "NoSupportedProfiles";
	case DevicePairingResultStatus::ProtectionLevelCouldNotBeMet: return "ProtectionLevelCouldNotBeMet";
	case DevicePairingResultStatus::AccessDenied: return "AccessDenied";
	case DevicePairingResultStatus::InvalidCeremonyData: return "InvalidCeremonyData";
	case DevicePairingResultStatus::PairingCanceled: return "PairingCanceled";
	case DevicePairingResultStatus::OperationAlreadyInProgress: return "OperationAlreadyInProgress";
	case DevicePairingResultStatus::RequiredHandlerNotRegistered: return "RequiredHandlerNotRegistered";
	case DevicePairingResultStatus::RejectedByHandler: return "RejectedByHandler";
	case DevicePairingResultStatus::RemoteDeviceHasAssociation: return "RemoteDeviceHasAssociation";
	default: return "Failed";
	}
}

const char* unpairingResultName(DeviceUnpairingResultStatus status) {
	switch (status) {
	case DeviceUnpairingResultStatus::Unpaired: return "Unpaired";
	case DeviceUnpairingResultStatus::AlreadyUnpaired: return "AlreadyUnpaired";
	case DeviceUnpairingResultStatus::OperationAlreadyInProgress: return "OperationAlreadyInProgress";
	case DeviceUnpairingResultStatus::AccessDenied: return "AccessDenied";
	default: return "Failed";
	}
}

const char* sourceName(int source) {
	switch (source) {
	case 0: return "paired classic";
	case 1: return "paired LE";
	case 2: return "unpaired classic";
	case 3: return "unpaired LE";
	default: return "?";
	}
}

void answerPairingRequest(const QString& key, const DevicePairingRequestedEventArgs& args) {
	try {
		switch (args.PairingKind()) {
		case DevicePairingKinds::ConfirmOnly:
			qCInfo(logWorker) << "Pairing" << key << "- accepting a confirm-only request";
			args.Accept();
			break;
		case DevicePairingKinds::ConfirmPinMatch:
			qCInfo(logWorker).nospace() << "Pairing " << key << " - accepting numeric comparison, PIN "
			                            << toQString(args.Pin());
			args.Accept();
			break;
		case DevicePairingKinds::DisplayPin:
			qCWarning(logWorker) << "Pairing" << key
			                     << "rejected: the device wants a PIN shown here and typed on it, and the "
			                        "shell has no pairing UI. Pair it from Windows Settings > Bluetooth & "
			                        "devices.";
			break;
		case DevicePairingKinds::ProvidePin:
			qCWarning(logWorker) << "Pairing" << key
			                     << "rejected: the device needs its PIN entered on this computer, and the "
			                        "shell has no pairing UI. Pair it from Windows Settings > Bluetooth & "
			                        "devices.";
			break;
		default:
			qCWarning(logWorker) << "Pairing" << key << "rejected: unsupported pairing kind"
			                     << static_cast<quint32>(args.PairingKind());
			break;
		}
	} catch (const winrt::hresult_error& e) {
		qCWarning(logWorker) << "Answering the pairing request for" << key
		                     << "failed:" << hresultText(e.code());
	}
}

} // namespace

BtWorker::BtWorker(WinBluetooth* frontend)
    : mGate(std::make_shared<BtWorkerGate>(this))
    , mFrontend(frontend) {}

BtWorker::~BtWorker() = default;

void BtWorker::start() {
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
		this->mApartmentInitialized = true;
	} catch (const winrt::hresult_error& e) {
		qCWarning(logWorker) << "init_apartment(multi_threaded) failed:" << hresultText(e.code());
		QMetaObject::invokeMethod(
		    this->mFrontend,
		    [frontend = this->mFrontend] { frontend->backendScanned(false); },
		    Qt::QueuedConnection
		);
		return;
	}

	this->mBatteryTimer = new QTimer(this);
	this->mBatteryTimer->setInterval(BATTERY_POLL_MS);
	QObject::connect(this->mBatteryTimer, &QTimer::timeout, this, &BtWorker::pollBatteries);
	this->mBatteryTimer->start();

	try {
		this->mAdapterWatcher = DeviceInformation::CreateWatcher(WinRtAdapter::GetDeviceSelector());

		auto gate = this->mGate;
		auto refresh = [gate](auto&&...) {
			gate->post([](BtWorker* self) { self->scheduleAdapterRefresh(); });
		};

		this->mAdapterAddedToken = this->mAdapterWatcher.Added(refresh);
		this->mAdapterRemovedToken = this->mAdapterWatcher.Removed(refresh);
		this->mAdapterUpdatedToken = this->mAdapterWatcher.Updated(refresh);
		this->mAdapterWatcher.Start();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logWorker) << "Can't watch for Bluetooth adapters:" << hresultText(e.code());
		this->mAdapterWatcher = nullptr;
	}

	this->refreshAdapter();
}

void BtWorker::shutdown() {
	this->mGate->close();
	if (this->mBatteryTimer) this->mBatteryTimer->stop();

	if (this->mAdapterWatcher) {
		try {
			this->mAdapterWatcher.Added(this->mAdapterAddedToken);
			this->mAdapterWatcher.Removed(this->mAdapterRemovedToken);
			this->mAdapterWatcher.Updated(this->mAdapterUpdatedToken);
			auto status = this->mAdapterWatcher.Status();
			if (status == DeviceWatcherStatus::Started || status == DeviceWatcherStatus::EnumerationCompleted) {
				this->mAdapterWatcher.Stop();
			}
		} catch (const winrt::hresult_error&) {
		}
		this->mAdapterWatcher = nullptr;
	}

	for (auto& slot: this->mWatchers) {
		if (!slot.watcher) continue;
		slot.generation++;
		try {
			slot.watcher.Added(slot.added);
			slot.watcher.Updated(slot.updated);
			slot.watcher.Removed(slot.removed);
			slot.watcher.Stopped(slot.stopped);
			auto status = slot.watcher.Status();
			if (status == DeviceWatcherStatus::Started || status == DeviceWatcherStatus::EnumerationCompleted) {
				slot.watcher.Stop();
			}
		} catch (const winrt::hresult_error&) {
		}
		slot.watcher = nullptr;
	}

	for (auto& [key, entry]: this->mEntries) {
		if (entry->pairOp) {
			try {
				entry->pairOp.Cancel();
			} catch (const winrt::hresult_error&) {
			}
		}
		this->finishPairing(*entry);
		if (entry->classic) releaseDeviceObject(*entry->classic);
		if (entry->le) releaseDeviceObject(*entry->le);
	}
	this->mEntries.clear();
	this->mIdToKey.clear();

	if (this->mRadio) {
		try {
			this->mRadio.StateChanged(this->mRadioStateToken);
		} catch (const winrt::hresult_error&) {
		}
		this->mRadio = nullptr;
	}
	this->mAdapter = nullptr;

	QCoreApplication::removePostedEvents(this);

	if (this->mApartmentInitialized) {
		winrt::uninit_apartment();
		this->mApartmentInitialized = false;
	}
}

void BtWorker::scheduleAdapterRefresh() {
	if (this->mAdapterRefreshQueued) return;
	this->mAdapterRefreshQueued = true;
	QTimer::singleShot(ADAPTER_REFRESH_DELAY_MS, this, &BtWorker::refreshAdapter);
}

void BtWorker::refreshAdapter() {
	this->mAdapterRefreshQueued = false;

	WinRtAdapter adapter {nullptr};
	try {
		adapter = WinRtAdapter::GetDefaultAsync().get();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logWorker) << "BluetoothAdapter::GetDefaultAsync failed:" << hresultText(e.code());
	}

	if (!adapter) {
		if (this->mAdapter) {
			qCInfo(logWorker) << "Bluetooth adapter went away";
			this->tearDownAdapter();
		} else if (!this->mAdapterScanned) {
			qCInfo(logWorker) << "No Bluetooth adapter; Quickshell.Bluetooth stays empty until one appears";
		}
	} else if (this->mAdapter && this->mAdapter.DeviceId() == adapter.DeviceId()) {
		this->publishAdapter();
	} else {
		if (this->mAdapter) this->tearDownAdapter();
		this->setUpAdapter(adapter);
	}

	if (!this->mAdapterScanned) {
		this->mAdapterScanned = true;
		auto hasAdapter = static_cast<bool>(this->mAdapter);
		QMetaObject::invokeMethod(
		    this->mFrontend,
		    [frontend = this->mFrontend, hasAdapter] { frontend->backendScanned(hasAdapter); },
		    Qt::QueuedConnection
		);
	}
}

void BtWorker::setUpAdapter(const WinRtAdapter& adapter) {
	this->mAdapter = adapter;

	AdapterSnapshot snapshot;
	snapshot.id = toQString(adapter.DeviceId());

	try {
		snapshot.address = formatAddress(adapter.BluetoothAddress());
	} catch (const winrt::hresult_error&) {
	}

	try {
		this->mRadio = adapter.GetRadioAsync().get();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logWorker) << "BluetoothAdapter::GetRadioAsync failed:" << hresultText(e.code());
	}

	if (this->mRadio) {
		auto gate = this->mGate;
		this->mRadioStateToken = this->mRadio.StateChanged([gate](auto&&...) {
			gate->post([](BtWorker* self) { self->onRadioStateChanged(); });
		});
	} else {
		qCWarning(logWorker) << "The Bluetooth adapter has no accessible radio: its power state is "
		                        "unknown and can't be changed";
	}

	try {
		auto info = DeviceInformation::CreateFromIdAsync(adapter.DeviceId()).get();
		snapshot.name = toQString(info.Name());
	} catch (const winrt::hresult_error&) {
	}
	if (snapshot.name.isEmpty() && this->mRadio) snapshot.name = toQString(this->mRadio.Name());
	if (snapshot.name.isEmpty()) snapshot.name = QStringLiteral("Bluetooth");

	this->mAdapterSnapshot = snapshot;
	this->mLastPower = this->currentPower();
	qCInfo(logWorker) << "Using Bluetooth adapter" << snapshot.name << snapshot.address << snapshot.id;

	this->publishAdapter();
	this->startWatcher(PairedClassic);
	this->startWatcher(PairedLe);
	this->updateDiscoveryWatchers();
}

void BtWorker::tearDownAdapter() {
	this->mDiscoveryWanted = false;
	for (auto source: {PairedClassic, PairedLe, UnpairedClassic, UnpairedLe}) {
		this->stopWatcher(source);
	}

	std::vector<QString> keys;
	keys.reserve(this->mEntries.size());
	for (const auto& [key, entry]: this->mEntries) keys.push_back(key);
	for (const auto& key: keys) this->removeEntry(key);

	if (this->mRadio) {
		try {
			this->mRadio.StateChanged(this->mRadioStateToken);
		} catch (const winrt::hresult_error&) {
		}
		this->mRadio = nullptr;
	}

	this->mAdapter = nullptr;
	this->mPowerInFlight = false;
	this->mPowerSeqDone = this->mPowerSeqWanted;
	this->mDiscovering = false;

	QMetaObject::invokeMethod(
	    this->mFrontend,
	    [frontend = this->mFrontend] { frontend->backendAdapterRemoved(); },
	    Qt::QueuedConnection
	);
}

void BtWorker::publishAdapter() {
	if (!this->mAdapter) return;

	this->mAdapterSnapshot.power = this->currentPower();
	this->mAdapterSnapshot.powerSeqDone = this->mPowerSeqDone;

	QMetaObject::invokeMethod(
	    this->mFrontend,
	    [frontend = this->mFrontend, snapshot = this->mAdapterSnapshot] {
		    frontend->backendAdapterChanged(snapshot);
	    },
	    Qt::QueuedConnection
	);
}

RadioPower BtWorker::currentPower() const {
	if (!this->mRadio) return RadioPower::Unknown;

	try {
		switch (this->mRadio.State()) {
		case RadioState::On: return RadioPower::On;
		case RadioState::Off: return RadioPower::Off;
		case RadioState::Disabled: return RadioPower::Blocked;
		default: return RadioPower::Unknown;
		}
	} catch (const winrt::hresult_error&) {
		return RadioPower::Unknown;
	}
}

void BtWorker::onRadioStateChanged() {
	auto power = this->currentPower();

	if (this->mLastPower == RadioPower::On && power != RadioPower::On) this->mDiscoveryWanted = false;
	this->mLastPower = power;

	this->publishAdapter();
	this->updateDiscoveryWatchers();
}

void BtWorker::cmdSetPowered(bool on, quint64 seq) {
	this->mPowerWanted = on;
	this->mPowerSeqWanted = seq;

	if (!this->mRadio) {
		qCWarning(logWorker) << "Can't turn Bluetooth" << (on ? "on" : "off") << "- no radio";
		this->mPowerSeqDone = seq;
		this->publishAdapter();
		return;
	}

	if (!this->mPowerInFlight) this->requestPower();
}

void BtWorker::requestPower() {
	auto on = this->mPowerWanted;
	auto seq = this->mPowerSeqWanted;

	auto power = this->currentPower();
	if (power != RadioPower::Unknown && (power == RadioPower::On) == on) {
		this->mPowerSeqDone = seq;
		this->publishAdapter();
		return;
	}

	this->mPowerInFlight = true;
	auto gate = this->mGate;

	auto setState = [gate, on, seq](BtWorker* self) {
		if (!self->mRadio) {
			self->onPowerCompleted(on, seq, AsyncStatus::Error, RadioAccessStatus::Unspecified);
			return;
		}

		try {
			self->mRadio.SetStateAsync(on ? RadioState::On : RadioState::Off)
			    .Completed([gate, on, seq](const IAsyncOperation<RadioAccessStatus>& op, AsyncStatus status) {
				    auto access = RadioAccessStatus::Unspecified;
				    if (status == AsyncStatus::Completed) {
					    try {
						    access = op.GetResults();
					    } catch (const winrt::hresult_error&) {
					    }
				    }

				    gate->post([on, seq, status, access](BtWorker* self) {
					    self->onPowerCompleted(on, seq, status, access);
				    });
			    });
		} catch (const winrt::hresult_error& e) {
			qCWarning(logWorker) << "Radio::SetStateAsync failed:" << hresultText(e.code());
			self->onPowerCompleted(on, seq, AsyncStatus::Error, RadioAccessStatus::Unspecified);
		}
	};

	if (!this->mRadioAccessRequested) {
		this->mRadioAccessRequested = true;
		try {
			Radio::RequestAccessAsync().Completed(
			    [gate, setState](const IAsyncOperation<RadioAccessStatus>& op, AsyncStatus status) {
				    auto access = RadioAccessStatus::Unspecified;
				    if (status == AsyncStatus::Completed) {
					    try {
						    access = op.GetResults();
					    } catch (const winrt::hresult_error&) {
					    }
				    }

				    gate->post([setState, access](BtWorker* self) {
					    if (access != RadioAccessStatus::Allowed) {
						    qCWarning(logWorker) << "Radio::RequestAccessAsync:" << accessName(access);
					    } else {
						    qCInfo(logWorker) << "Radio::RequestAccessAsync: Allowed";
					    }
					    setState(self);
				    });
			    }
			);
			return;
		} catch (const winrt::hresult_error& e) {
			qCWarning(logWorker) << "Radio::RequestAccessAsync failed:" << hresultText(e.code());
		}
	}

	setState(this);
}

void BtWorker::onPowerCompleted(bool on, quint64 seq, AsyncStatus status, RadioAccessStatus access) {
	this->mPowerInFlight = false;

	if (status != AsyncStatus::Completed) {
		qCWarning(logWorker) << "Turning Bluetooth" << (on ? "on" : "off") << "didn't complete (status"
		                     << static_cast<int>(status) << ")";
	} else if (access != RadioAccessStatus::Allowed) {
		qCWarning(logWorker) << "Windows refused to turn Bluetooth" << (on ? "on:" : "off:")
		                     << accessName(access);
	}

	this->mPowerSeqDone = seq;
	if (this->mPowerSeqWanted > seq && this->mRadio) this->requestPower();

	this->onRadioStateChanged();
}

void BtWorker::cmdSetDiscovering(bool on) {
	if (on && !this->mAdapter) qCDebug(logWorker) << "Discovery requested without an adapter";
	this->mDiscoveryWanted = on;
	this->updateDiscoveryWatchers();
}

void BtWorker::updateDiscoveryWatchers() {
	auto power = this->currentPower();
	auto run = this->mAdapter && this->mDiscoveryWanted
	        && (power == RadioPower::On || power == RadioPower::Unknown);

	if (run) {
		this->startWatcher(UnpairedClassic);
		this->startWatcher(UnpairedLe);
	} else {
		this->stopWatcher(UnpairedClassic);
		this->stopWatcher(UnpairedLe);
	}

	auto discovering = this->mWatchers[UnpairedClassic].watcher || this->mWatchers[UnpairedLe].watcher;
	if (discovering != this->mDiscovering) {
		this->mDiscovering = discovering;
		QMetaObject::invokeMethod(
		    this->mFrontend,
		    [frontend = this->mFrontend, discovering] {
			    frontend->backendDiscoveringChanged(discovering);
		    },
		    Qt::QueuedConnection
		);
	}
}

void BtWorker::startWatcher(Source source) {
	auto& slot = this->mWatchers[source];
	if (slot.watcher) return;

	auto generation = ++slot.generation;

	try {
		winrt::hstring selector;
		switch (source) {
		case PairedClassic: selector = WinRtDevice::GetDeviceSelectorFromPairingState(true); break;
		case PairedLe: selector = WinRtLeDevice::GetDeviceSelectorFromPairingState(true); break;
		case UnpairedClassic: selector = WinRtDevice::GetDeviceSelectorFromPairingState(false); break;
		case UnpairedLe: selector = WinRtLeDevice::GetDeviceSelectorFromPairingState(false); break;
		default: return;
		}

		slot.watcher = DeviceInformation::CreateWatcher(
		    selector,
		    aepProperties(),
		    DeviceInformationKind::AssociationEndpoint
		);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logWorker) << "Can't create the" << sourceName(source)
		                     << "device watcher:" << hresultText(e.code());
		slot.watcher = nullptr;
		return;
	}

	auto gate = this->mGate;

	slot.added = slot.watcher.Added([gate, source, generation](const DeviceWatcher&, const DeviceInformation& info) {
		gate->post([source, generation, info](BtWorker* self) {
			if (self->mWatchers[source].generation == generation) self->onAdded(source, info);
		});
	});

	slot.updated = slot.watcher.Updated(
	    [gate, source, generation](const DeviceWatcher&, const DeviceInformationUpdate& update) {
		    gate->post([source, generation, update](BtWorker* self) {
			    if (self->mWatchers[source].generation == generation) self->onUpdated(source, update);
		    });
	    }
	);

	slot.removed = slot.watcher.Removed(
	    [gate, source, generation](const DeviceWatcher&, const DeviceInformationUpdate& update) {
		    gate->post([source, generation, update](BtWorker* self) {
			    if (self->mWatchers[source].generation == generation) self->onRemoved(source, update);
		    });
	    }
	);

	slot.stopped = slot.watcher.Stopped([gate, source, generation](auto&&...) {
		gate->post([source, generation](BtWorker* self) { self->onWatcherStopped(source, generation); });
	});

	try {
		slot.watcher.Start();
		qCDebug(logWorker) << "Started the" << sourceName(source) << "device watcher";
	} catch (const winrt::hresult_error& e) {
		qCWarning(logWorker) << "Can't start the" << sourceName(source)
		                     << "device watcher:" << hresultText(e.code());
		this->stopWatcher(source);
	}
}

void BtWorker::stopWatcher(Source source) {
	auto& slot = this->mWatchers[source];
	if (!slot.watcher) return;

	slot.generation++;
	auto watcher = slot.watcher;
	slot.watcher = nullptr;

	try {
		watcher.Added(slot.added);
		watcher.Updated(slot.updated);
		watcher.Removed(slot.removed);
		watcher.Stopped(slot.stopped);
		auto status = watcher.Status();
		if (status == DeviceWatcherStatus::Started || status == DeviceWatcherStatus::EnumerationCompleted) {
			watcher.Stop();
		}
	} catch (const winrt::hresult_error&) {
	}

	auto le = isLe(source);
	auto paired = isPairedSource(source);

	std::vector<QString> keys;
	keys.reserve(this->mEntries.size());
	for (const auto& [key, entry]: this->mEntries) keys.push_back(key);

	for (const auto& key: keys) {
		auto* entry = this->findEntry(key);
		auto* transport = le ? entry->le.get() : entry->classic.get();
		if (!transport) continue;

		if (paired) {
			transport->inPaired = false;
			releaseDeviceObject(*transport);
		} else {
			transport->inUnpaired = false;
		}

		if (!transport->inPaired && !transport->inUnpaired) this->dropTransport(*entry, le);

		if (!entry->classic && !entry->le) this->scheduleRemoval(key);
		else this->publish(*entry);
	}
}

void BtWorker::onWatcherStopped(Source source, quint64 generation) {
	auto& slot = this->mWatchers[source];
	if (slot.generation != generation || !slot.watcher) return;

	DeviceWatcherStatus status = DeviceWatcherStatus::Stopped;
	try {
		status = slot.watcher.Status();
	} catch (const winrt::hresult_error&) {
	}

	this->stopWatcher(source);

	if (isPairedSource(source)) {
		qCWarning(logWorker) << "The" << sourceName(source) << "device watcher stopped on its own (status"
		                     << static_cast<int>(status) << "), restarting it in" << WATCHER_RETRY_MS << "ms";
		QTimer::singleShot(WATCHER_RETRY_MS, this, [this, source] {
			if (this->mAdapter) this->startWatcher(source);
		});
	} else {
		qCWarning(logWorker) << "The" << sourceName(source) << "device watcher stopped on its own (status"
		                     << static_cast<int>(status) << "), ending discovery";
		this->mDiscoveryWanted = false;
		this->updateDiscoveryWatchers();
	}
}

void BtWorker::onAdded(Source source, const DeviceInformation& info) {
	QString id;
	QString key;
	try {
		id = toQString(info.Id());
		key = addressOf(info);
	} catch (const winrt::hresult_error&) {
		return;
	}

	if (key.isEmpty()) {
		qCDebug(logWorker) << "Ignoring device without an address:" << id;
		return;
	}

	auto& owner = this->mEntries[key];
	if (!owner) {
		owner = std::make_unique<Entry>();
		owner->key = key;
	}

	auto* entry = owner.get();
	entry->removalSerial++;

	auto le = isLe(source);
	auto& transport = le ? entry->le : entry->classic;
	if (transport && transport->id != id) this->dropTransport(*entry, le);
	if (!transport) {
		transport = std::make_unique<Transport>();
		transport->id = id;
	}

	transport->info = info;
	if (isPairedSource(source)) {
		transport->inPaired = true;
		entry->justPaired = false;
	} else {
		transport->inUnpaired = true;
	}

	this->mIdToKey.insert(id, key);
	if (transport->inPaired) this->ensureDeviceObject(*entry, le);
	this->publish(*entry);
}

void BtWorker::onUpdated(Source /*source*/, const DeviceInformationUpdate& update) {
	auto id = toQString(update.Id());
	auto* entry = this->findEntry(this->mIdToKey.value(id));
	if (!entry) return;

	auto le = false;
	auto* transport = this->transportForId(*entry, id, &le);
	if (!transport || !transport->info) return;

	try {
		transport->info.Update(update);
	} catch (const winrt::hresult_error&) {
		return;
	}

	this->publish(*entry);
}

void BtWorker::onRemoved(Source source, const DeviceInformationUpdate& update) {
	auto id = toQString(update.Id());
	auto key = this->mIdToKey.value(id);
	auto* entry = this->findEntry(key);
	if (!entry) return;

	auto le = false;
	auto* transport = this->transportForId(*entry, id, &le);
	if (!transport) return;

	if (isPairedSource(source)) {
		transport->inPaired = false;
		releaseDeviceObject(*transport);
	} else {
		transport->inUnpaired = false;
	}

	if (!transport->inPaired && !transport->inUnpaired) this->dropTransport(*entry, le);

	if (!entry->classic && !entry->le) this->scheduleRemoval(key);
	else this->publish(*entry);
}

BtWorker::Entry* BtWorker::findEntry(const QString& key) {
	if (key.isEmpty()) return nullptr;
	auto it = this->mEntries.find(key);
	return it == this->mEntries.end() ? nullptr : it->second.get();
}

BtWorker::Transport* BtWorker::transportForId(Entry& entry, const QString& id, bool* le) {
	if (entry.classic && entry.classic->id == id) {
		*le = false;
		return entry.classic.get();
	}
	if (entry.le && entry.le->id == id) {
		*le = true;
		return entry.le.get();
	}
	return nullptr;
}

void BtWorker::ensureDeviceObject(Entry& entry, bool le) {
	auto* transport = le ? entry.le.get() : entry.classic.get();
	if (!transport || !transport->inPaired || transport->objectRequested) return;
	transport->objectRequested = true;

	auto gate = this->mGate;
	auto key = entry.key;
	auto id = transport->id;

	try {
		if (le) {
			WinRtLeDevice::FromIdAsync(toHString(id))
			    .Completed([gate, key, id](const IAsyncOperation<WinRtLeDevice>& op, AsyncStatus status) {
				    WinRtLeDevice device {nullptr};
				    if (status == AsyncStatus::Completed) {
					    try {
						    device = op.GetResults();
					    } catch (const winrt::hresult_error&) {
					    }
				    }
				    gate->post([key, id, device](BtWorker* self) { self->onLeDeviceReady(key, id, device); });
			    });
		} else {
			WinRtDevice::FromIdAsync(toHString(id))
			    .Completed([gate, key, id](const IAsyncOperation<WinRtDevice>& op, AsyncStatus status) {
				    WinRtDevice device {nullptr};
				    if (status == AsyncStatus::Completed) {
					    try {
						    device = op.GetResults();
					    } catch (const winrt::hresult_error&) {
					    }
				    }
				    gate->post([key, id, device](BtWorker* self) {
					    self->onClassicDeviceReady(key, id, device);
				    });
			    });
		}
	} catch (const winrt::hresult_error& e) {
		qCDebug(logWorker) << "FromIdAsync failed for" << id << hresultText(e.code());
		transport->objectRequested = false;
	}
}

void BtWorker::onClassicDeviceReady(const QString& key, const QString& id, const WinRtDevice& device) {
	auto* entry = this->findEntry(key);
	auto* transport = entry ? entry->classic.get() : nullptr;
	auto wanted = transport && transport->id == id && transport->inPaired && !transport->classicDevice;

	if (!wanted || !device) {
		if (device) {
			try {
				device.Close();
			} catch (const winrt::hresult_error&) {
			}
		} else if (transport && transport->id == id) {
			qCDebug(logWorker) << "No BluetoothDevice for" << id;
			transport->objectRequested = false;
		}
		return;
	}

	transport->classicDevice = device;
	this->watchDeviceObject(*transport, key);
	this->publish(*entry);
}

void BtWorker::onLeDeviceReady(const QString& key, const QString& id, const WinRtLeDevice& device) {
	auto* entry = this->findEntry(key);
	auto* transport = entry ? entry->le.get() : nullptr;
	auto wanted = transport && transport->id == id && transport->inPaired && !transport->leDevice;

	if (!wanted || !device) {
		if (device) {
			try {
				device.Close();
			} catch (const winrt::hresult_error&) {
			}
		} else if (transport && transport->id == id) {
			qCDebug(logWorker) << "No BluetoothLEDevice for" << id;
			transport->objectRequested = false;
		}
		return;
	}

	transport->leDevice = device;
	this->watchDeviceObject(*transport, key);
	this->publish(*entry);
}

void BtWorker::watchDeviceObject(Transport& transport, const QString& key) {
	auto gate = this->mGate;
	auto changed = [gate, key](auto&&...) {
		gate->post([key](BtWorker* self) { self->onConnectionChanged(key); });
	};

	try {
		if (transport.classicDevice) {
			transport.connectionToken = transport.classicDevice.ConnectionStatusChanged(changed);
			transport.nameToken = transport.classicDevice.NameChanged(changed);
		} else if (transport.leDevice) {
			transport.connectionToken = transport.leDevice.ConnectionStatusChanged(changed);
			transport.nameToken = transport.leDevice.NameChanged(changed);
		}
	} catch (const winrt::hresult_error& e) {
		qCDebug(logWorker) << "Can't watch" << transport.id << hresultText(e.code());
	}
}

void BtWorker::releaseDeviceObject(Transport& transport) {
	try {
		if (transport.classicDevice) {
			transport.classicDevice.ConnectionStatusChanged(transport.connectionToken);
			transport.classicDevice.NameChanged(transport.nameToken);
			transport.classicDevice.Close();
		}
		if (transport.leDevice) {
			transport.leDevice.ConnectionStatusChanged(transport.connectionToken);
			transport.leDevice.NameChanged(transport.nameToken);
			transport.leDevice.Close();
		}
	} catch (const winrt::hresult_error&) {
	}

	transport.classicDevice = nullptr;
	transport.leDevice = nullptr;
	transport.connectionToken = {};
	transport.nameToken = {};
	transport.objectRequested = false;
}

void BtWorker::dropTransport(Entry& entry, bool le) {
	auto& transport = le ? entry.le : entry.classic;
	if (!transport) return;

	releaseDeviceObject(*transport);
	this->mIdToKey.remove(transport->id);
	transport.reset();
}

void BtWorker::scheduleRemoval(const QString& key) {
	auto* entry = this->findEntry(key);
	if (!entry) return;

	auto serial = ++entry->removalSerial;
	QTimer::singleShot(REMOVAL_GRACE_MS, this, [this, key, serial] {
		auto* entry = this->findEntry(key);
		if (!entry || entry->removalSerial != serial || entry->classic || entry->le) return;
		if (entry->pairOp) return;
		this->removeEntry(key);
	});
}

void BtWorker::removeEntry(const QString& key) {
	auto it = this->mEntries.find(key);
	if (it == this->mEntries.end()) return;

	auto& entry = *it->second;
	if (entry.pairOp) {
		try {
			entry.pairOp.Cancel();
		} catch (const winrt::hresult_error&) {
		}
	}
	this->finishPairing(entry);
	this->dropTransport(entry, false);
	this->dropTransport(entry, true);

	auto published = entry.published;
	this->mEntries.erase(it);

	if (published) {
		QMetaObject::invokeMethod(
		    this->mFrontend,
		    [frontend = this->mFrontend, key] { frontend->backendDeviceRemoved(key); },
		    Qt::QueuedConnection
		);
	}
}

void BtWorker::onConnectionChanged(const QString& key) {
	if (auto* entry = this->findEntry(key)) this->publish(*entry);
}

void BtWorker::publish(Entry& entry) {
	if (!entry.classic && !entry.le) return;

	auto snapshot = this->buildSnapshot(entry);

	if (snapshot.connected && (!entry.published || !entry.last.connected)) {
		auto key = entry.key;
		QTimer::singleShot(entry.published ? BATTERY_AFTER_CONNECT_MS : 0, this, [this, key] {
			auto* entry = this->findEntry(key);
			if (!entry || !entry->last.connected) return;
			this->refreshBattery(*entry);
			this->publish(*entry);
		});
	} else if (!snapshot.connected && entry.battery) {
		entry.battery.reset();
		snapshot = this->buildSnapshot(entry);
	}

	if (entry.published && snapshot == entry.last) return;

	entry.last = snapshot;
	entry.published = true;

	QMetaObject::invokeMethod(
	    this->mFrontend,
	    [frontend = this->mFrontend, snapshot] { frontend->backendDeviceChanged(snapshot); },
	    Qt::QueuedConnection
	);
}

DeviceSnapshot BtWorker::buildSnapshot(const Entry& entry) const {
	const auto* classic = entry.classic.get();
	const auto* le = entry.le.get();
	const auto* main = classic ? classic : le;

	DeviceSnapshot snapshot;
	snapshot.key = entry.key;
	snapshot.address = entry.key.toUpper();
	snapshot.path = main ? main->id : QString();

	auto infoName = [](const Transport* transport) -> QString {
		if (!transport || !transport->info) return {};
		try {
			return toQString(transport->info.Name()).trimmed();
		} catch (const winrt::hresult_error&) {
			return {};
		}
	};

	auto name = infoName(classic);
	if (name.isEmpty()) name = infoName(le);
	snapshot.name = name.isEmpty() ? QString(snapshot.address).replace(QLatin1Char(':'), QLatin1Char('-'))
	                               : name;

	QString deviceName;
	try {
		if (classic && classic->classicDevice) deviceName = toQString(classic->classicDevice.Name());
		else if (le && le->leDevice) deviceName = toQString(le->leDevice.Name());
	} catch (const winrt::hresult_error&) {
	}
	deviceName = deviceName.trimmed();
	snapshot.deviceName = deviceName.isEmpty() ? snapshot.name : deviceName;

	if (classic) {
		std::optional<quint32> cod;
		try {
			if (classic->classicDevice) cod = classic->classicDevice.ClassOfDevice().RawValue();
		} catch (const winrt::hresult_error&) {
		}
		if (!cod || *cod == 0) {
			auto props = propsOf(classic->info);
			auto major = propUInt(props, PROP_COD_MAJOR);
			auto minor = propUInt(props, PROP_COD_MINOR);
			if (major) cod = ((*major & 0x1f) << 8) | ((minor.value_or(0) & 0x3f) << 2);
		}
		if (cod) snapshot.icon = iconForClassOfDevice(*cod);
	}

	if (snapshot.icon.isEmpty() && le) {
		std::optional<quint32> appearance;
		try {
			if (le->leDevice) appearance = le->leDevice.Appearance().RawValue();
		} catch (const winrt::hresult_error&) {
		}
		if (!appearance || *appearance == 0) appearance = propUInt(propsOf(le->info), PROP_LE_APPEARANCE);
		if (appearance) snapshot.icon = iconForAppearance(static_cast<quint16>(*appearance));
	}

	snapshot.paired = (classic && classic->inPaired) || (le && le->inPaired) || entry.justPaired;
	snapshot.connected = transportConnected(classic) || transportConnected(le);

	if (entry.battery && snapshot.connected) {
		snapshot.batteryAvailable = true;
		snapshot.battery = *entry.battery / 100.0;
	}

	return snapshot;
}

bool BtWorker::transportConnected(const Transport* transport) {
	if (!transport) return false;

	try {
		if (transport->classicDevice) {
			return transport->classicDevice.ConnectionStatus() == BluetoothConnectionStatus::Connected;
		}
		if (transport->leDevice) {
			return transport->leDevice.ConnectionStatus() == BluetoothConnectionStatus::Connected;
		}
	} catch (const winrt::hresult_error&) {
	}

	return propBool(propsOf(transport->info), PROP_CONNECTED).value_or(false);
}

std::optional<winrt::guid> BtWorker::containerIdOf(const Entry& entry) {
	for (const auto* transport: {entry.classic.get(), entry.le.get()}) {
		if (!transport) continue;
		if (auto container = propGuid(propsOf(transport->info), PROP_CONTAINER)) return container;
	}
	return std::nullopt;
}

void BtWorker::refreshBattery(Entry& entry) {
	auto container = containerIdOf(entry);
	if (!container) return;
	auto containerText = guidText(*container);

	std::optional<quint32> level;

	try {
		auto info = DeviceInformation::CreateFromIdAsync(
		                toHString(containerText),
		                std::vector<winrt::hstring> {PROP_BATTERY_LIFE},
		                DeviceInformationKind::DeviceContainer
		)
		                .get();
		level = propUInt(propsOf(info), PROP_BATTERY_LIFE);
	} catch (const winrt::hresult_error& e) {
		qCDebug(logWorker) << "No container battery for" << entry.key << hresultText(e.code());
	}

	if (!level) {
		try {
			auto selector = QStringLiteral("System.Devices.ContainerId:=\"%1\"").arg(containerText);
			auto nodes = DeviceInformation::FindAllAsync(
			                 toHString(selector),
			                 std::vector<winrt::hstring> {PROP_BT_BATTERY},
			                 DeviceInformationKind::Device
			)
			                 .get();

			for (const auto& node: nodes) {
				level = propUInt(propsOf(node), PROP_BT_BATTERY);
				if (level) break;
			}
		} catch (const winrt::hresult_error& e) {
			qCDebug(logWorker) << "Battery lookup failed for" << entry.key << hresultText(e.code());
		}
	}

	if (level && *level <= 100) entry.battery = static_cast<int>(*level);
	else entry.battery.reset();
}

void BtWorker::pollBatteries() {
	std::vector<QString> keys;
	for (const auto& [key, entry]: this->mEntries) {
		if (entry->last.connected && entry->last.paired) keys.push_back(key);
	}

	for (const auto& key: keys) {
		if (auto* entry = this->findEntry(key)) {
			this->refreshBattery(*entry);
			this->publish(*entry);
		}
	}
}

void BtWorker::cmdPair(const QString& key) {
	auto finished = [this, key](bool paired) {
		QMetaObject::invokeMethod(
		    this->mFrontend,
		    [frontend = this->mFrontend, key, paired] { frontend->backendPairFinished(key, paired); },
		    Qt::QueuedConnection
		);
	};

	auto* entry = this->findEntry(key);
	if (!entry) {
		qCWarning(logWorker) << "Can't pair" << key << "- unknown device";
		finished(false);
		return;
	}
	if (entry->pairOp) return;

	auto* transport = entry->classic ? entry->classic.get() : entry->le.get();
	if (!transport || !transport->info) {
		finished(false);
		return;
	}

	try {
		auto pairing = transport->info.Pairing();
		if (pairing.IsPaired()) {
			entry->justPaired = true;
			this->publish(*entry);
			finished(true);
			return;
		}
		if (!pairing.CanPair()) qCDebug(logWorker) << "Windows says" << key << "can't pair; trying anyway";

		auto gate = this->mGate;
		entry->customPairing = pairing.Custom();
		entry->pairingToken = entry->customPairing.PairingRequested(
		    [key](const DeviceInformationCustomPairing&, const DevicePairingRequestedEventArgs& args) {
			    answerPairingRequest(key, args);
		    }
		);

		auto kinds = DevicePairingKinds::ConfirmOnly | DevicePairingKinds::ConfirmPinMatch
		           | DevicePairingKinds::DisplayPin | DevicePairingKinds::ProvidePin;
		entry->pairOp = entry->customPairing.PairAsync(kinds, DevicePairingProtectionLevel::Default);
		entry->pairOp.Completed(
		    [gate, key](const IAsyncOperation<DevicePairingResult>& op, AsyncStatus status) {
			    auto result = DevicePairingResultStatus::Failed;
			    if (status == AsyncStatus::Completed) {
				    try {
					    result = op.GetResults().Status();
				    } catch (const winrt::hresult_error&) {
				    }
			    } else if (status == AsyncStatus::Canceled) {
				    result = DevicePairingResultStatus::PairingCanceled;
			    }

			    gate->post([key, status, result](BtWorker* self) { self->onPairCompleted(key, status, result); });
		    }
		);

		qCDebug(logWorker) << "Pairing" << key << "through" << transport->id;
	} catch (const winrt::hresult_error& e) {
		qCWarning(logWorker) << "Can't pair" << key << "-" << hresultText(e.code());
		this->finishPairing(*entry);
		finished(false);
	}
}

void BtWorker::onPairCompleted(const QString& key, AsyncStatus /*status*/, DevicePairingResultStatus result) {
	auto* entry = this->findEntry(key);
	if (!entry) return;

	this->finishPairing(*entry);

	auto paired = result == DevicePairingResultStatus::Paired
	           || result == DevicePairingResultStatus::AlreadyPaired;

	if (paired) {
		qCInfo(logWorker) << "Paired" << key;
		entry->justPaired = true;
		this->publish(*entry);
	} else {
		qCWarning(logWorker) << "Pairing" << key << "failed:" << pairingResultName(result);
	}

	QMetaObject::invokeMethod(
	    this->mFrontend,
	    [frontend = this->mFrontend, key, paired] { frontend->backendPairFinished(key, paired); },
	    Qt::QueuedConnection
	);

	if (!entry->classic && !entry->le) this->scheduleRemoval(key);
}

void BtWorker::finishPairing(Entry& entry) {
	if (entry.customPairing) {
		try {
			entry.customPairing.PairingRequested(entry.pairingToken);
		} catch (const winrt::hresult_error&) {
		}
	}

	entry.customPairing = nullptr;
	entry.pairingToken = {};
	entry.pairOp = nullptr;
}

void BtWorker::cmdCancelPair(const QString& key) {
	auto* entry = this->findEntry(key);
	if (entry && entry->pairOp) {
		try {
			entry->pairOp.Cancel();
			return;
		} catch (const winrt::hresult_error& e) {
			qCWarning(logWorker) << "Can't cancel pairing" << key << hresultText(e.code());
		}
	}

	QMetaObject::invokeMethod(
	    this->mFrontend,
	    [frontend = this->mFrontend, key] { frontend->backendPairFinished(key, false); },
	    Qt::QueuedConnection
	);
}

void BtWorker::cmdForget(const QString& key) {
	auto* entry = this->findEntry(key);
	if (!entry) return;

	entry->justPaired = false;
	auto gate = this->mGate;

	for (auto* transport: {entry->classic.get(), entry->le.get()}) {
		if (!transport || !transport->info) continue;

		try {
			transport->info.Pairing().UnpairAsync().Completed(
			    [gate, key](const IAsyncOperation<DeviceUnpairingResult>& op, AsyncStatus status) {
				    auto result = DeviceUnpairingResultStatus::Failed;
				    if (status == AsyncStatus::Completed) {
					    try {
						    result = op.GetResults().Status();
					    } catch (const winrt::hresult_error&) {
					    }
				    }

				    gate->post([key, result](BtWorker* self) {
					    if (result == DeviceUnpairingResultStatus::Unpaired
					        || result == DeviceUnpairingResultStatus::AlreadyUnpaired)
					    {
						    qCInfo(logWorker) << "Forgot" << key << unpairingResultName(result);
					    } else {
						    qCWarning(logWorker) << "Forgetting" << key << "failed:" << unpairingResultName(result);
					    }
					    if (auto* entry = self->findEntry(key)) self->publish(*entry);
				    });
			    }
			);
		} catch (const winrt::hresult_error& e) {
			qCWarning(logWorker) << "Can't forget" << key << hresultText(e.code());
		}
	}

	this->publish(*entry);
}

void BtWorker::cmdConnect(const QString& key, bool connect) {
	auto result = ConnectResult::Failed;

	if (auto* entry = this->findEntry(key)) {
		auto container = containerIdOf(*entry);

		ks::OneShotResult oneShot;
		if (container) {
			GUID guid;
			static_assert(sizeof(guid) == sizeof(*container));
			std::memcpy(&guid, &*container, sizeof(guid));
			oneShot = ks::sendOneShot(guid, connect);
		}

		if (oneShot.filters == 0) {
			result = ConnectResult::Unsupported;
			if (!this->mUnsupportedConnectLogged.contains(key)) {
				this->mUnsupportedConnectLogged.insert(key);
				qCWarning(logWorker).nospace()
				    << (connect ? "Connecting " : "Disconnecting ") << entry->last.name << " (" << key
				    << ") isn't supported on Windows: it has no audio endpoints, and Windows only offers "
				       "a (dis)connect request for Bluetooth audio devices. It reconnects on its own when "
				       "used.";
			}
		} else if (oneShot.sent > 0) {
			result = ConnectResult::Sent;
			qCDebug(logWorker) << (connect ? "Reconnect" : "Disconnect") << "sent to" << oneShot.sent << "of"
			                   << oneShot.filters << "audio filters of" << key;
		} else {
			qCWarning(logWorker) << (connect ? "Connecting" : "Disconnecting") << key
			                     << "failed: no audio filter accepted the request,"
			                     << hresultText(oneShot.lastError);
		}
	}

	QMetaObject::invokeMethod(
	    this->mFrontend,
	    [frontend = this->mFrontend, key, connect, result] {
		    frontend->backendConnectFinished(key, connect, result);
	    },
	    Qt::QueuedConnection
	);
}

} // namespace qs::bluetooth
