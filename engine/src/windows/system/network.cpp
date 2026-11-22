#include "network.hpp"

#include <qobject.h>
#include <qset.h>
#include <qstring.h>
#include <qt_windows.h>
#include <qelapsedtimer.h>

#include <shellapi.h>

#include "network_connectivity.hpp"

namespace qs::windows::sys {

Network::Network(QObject* parent): QObject(parent) {
	this->bWifiStatus = QStringLiteral("disabled");

	this->mWifi = std::make_unique<NetworkWifiBackend>(this);
	this->mConnectivity = std::make_unique<NetworkConnectivityBackend>(this);

	this->mConnectivity->start();
	this->mWifi->start();
}

Network::~Network() = default;

void Network::setWifiRadioEnabled(bool enabled) { this->mWifi->setRadioEnabled(enabled); }

// A WlanScan makes the adapter leave the connected channel to listen on all the others, which stalls the
// current connection (ping spikes of several seconds). Windows already scans in the background on its own
// schedule and the backend picks those results up (scan_complete), so the shell scans only when something
// explicitly asks, and never more often than kMinScanGapMs however many panels or timers ask.
void Network::scanWifiNetworks() {
	constexpr qint64 kMinScanGapMs = 30000;
	if (this->mLastScan.isValid() && this->mLastScan.elapsed() < kMinScanGapMs) {
		this->mWifi->refreshAvailableNetworks(); // the cached list is fresh enough
		return;
	}
	this->mLastScan.start();
	this->mWifi->scan();
}

NetworkWifiNetwork* Network::findNetwork(const QString& ssid) const {
	for (auto* net: this->mNetworks.valueList()) {
		if (net->bindableSsid().value() == ssid) return net;
	}
	return nullptr;
}

void Network::connectToNetwork(const QString& ssid, const QString& password) {
	auto* net = this->findNetwork(ssid);
	auto hasProfile = net != nullptr && net->bindableHasProfile().value();
	auto profileName = net != nullptr ? net->profileName() : QString();
	auto security = net != nullptr ? net->bindableSecurity().value() : QString();
	auto secure = net != nullptr ? !security.isEmpty() : !password.isEmpty();
	auto wpa3 = security.contains(QStringLiteral("WPA3"));

	this->bWifiConnectingSsid = ssid;
	this->bWifiConnecting = true;
	this->updateWifiStatus();

	this->mWifi->connectToNetwork(ssid, profileName, password, hasProfile, secure, wpa3);
}

void Network::disconnectActive() { this->mWifi->disconnectActive(); }

void Network::forgetNetwork(const QString& ssid) {
	auto* net = this->findNetwork(ssid);
	this->mWifi->forgetNetwork(ssid, net != nullptr ? net->profileName() : QString());
}

void Network::openWifiSettings() {
	ShellExecuteW(nullptr, L"open", L"ms-settings:network-wifi", nullptr, nullptr, SW_SHOWNORMAL);
}

void Network::openLocationSettings() {
	ShellExecuteW(nullptr, L"open", L"ms-settings:privacy-location", nullptr, nullptr, SW_SHOWNORMAL);
}

void Network::setWifiListVisible(bool visible) {
	if (this->mWifiListVisible == visible) return;
	this->mWifiListVisible = visible;

	// "visible" only means: keep the list updated. Read what Windows already has; it updates again on
	// Windows' own background scans. No hardware scan and no scan timer here (see scanWifiNetworks).
	if (visible) this->mWifi->refreshAvailableNetworks();
}

void Network::updateWifiStatus() {
	QString status;
	if (!this->bWifiRadioOn.value()) {
		status = QStringLiteral("disabled");
	} else if (this->bWifiConnecting.value()) {
		status = QStringLiteral("connecting");
	} else if (!this->bActiveSsid.value().isEmpty()) {
		status = this->bHasInternet.value() ? QStringLiteral("connected") : QStringLiteral("limited");
	} else {
		status = QStringLiteral("disconnected");
	}

	if (status != this->bWifiStatus.value()) this->bWifiStatus = status;
}

void Network::backendSetHasInternet(bool hasInternet) {
	this->bHasInternet = hasInternet;
	this->updateWifiStatus();
}

void Network::backendSetEthernetConnected(bool connected) { this->bEthernetConnected = connected; }

void Network::backendSetWifiAdapterPresent(bool present) { this->bWifiAdapterPresent = present; }

void Network::backendSetWifiRadioOn(bool on) {
	this->bWifiRadioOn = on;
	this->updateWifiStatus();
}

void Network::backendSetWifiScanning(bool scanning) { this->bWifiScanning = scanning; }

void Network::backendSetNeedsLocationPermission(bool needs) {
	this->bNeedsLocationPermission = needs;
}

void Network::backendSetCurrentConnection(
    const QString& ssid,
    const QString& bssid,
    int signalQuality,
    const QString& security,
    bool connected,
    bool connecting
) {
	this->bActiveSsid = connected ? ssid : QString();
	this->bActiveBssid = connected ? bssid : QString();
	this->bActiveSignalQuality = connected ? signalQuality : 0;
	this->bActiveSecurity = connected ? security : QString();

	if (connected && !this->mWifiListVisible) {
		auto* net = this->findNetwork(ssid);
		if (net != nullptr && net->bindableActive().value()) net->setStrength(signalQuality);
	}

	if (connected && ssid == this->bWifiConnectingSsid.value()) {
		this->bWifiConnecting = false;
		this->bWifiConnectingSsid = QString();
	} else if (connecting && !this->bWifiConnecting.value()) {
		this->bWifiConnecting = true;
		this->bWifiConnectingSsid = ssid;
	}

	this->updateWifiStatus();
}

void Network::backendUpdateAvailableNetworks(const std::vector<RawWifiNetwork>& networks) {
	QSet<QString> seen;
	for (const auto& r: networks) seen.insert(r.ssid);

	auto& list = this->mNetworks.valueList();
	for (auto i = list.size() - 1; i >= 0; i--) {
		if (!seen.contains(list.at(i)->bindableSsid().value())) {
			auto* obj = list.at(i);
			this->mNetworks.removeAt(i);
			obj->deleteLater();
		}
	}

	for (const auto& r: networks) {
		auto* item = this->findNetwork(r.ssid);
		if (item == nullptr) {
			item = new NetworkWifiNetwork(&this->mNetworks);
			this->mNetworks.insertObject(item);
		}

		item->setSsid(r.ssid);
		item->setBssid(r.bssid);
		item->setStrength(r.strength);
		item->setFrequency(r.frequency);
		item->setActive(r.active);
		item->setSecurity(r.security);
		item->setHasProfile(r.hasProfile);
		item->setProfileName(r.profileName);
	}
}

void Network::backendWifiConnectResult(const QString& ssid, bool success, const QString& reason) {
	this->bWifiConnecting = false;
	this->bWifiConnectingSsid = QString();
	this->updateWifiStatus();
	emit this->wifiConnectResult(ssid, success, reason);
}

} // namespace qs::windows::sys
