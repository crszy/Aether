#include "network_wifi.hpp"

#include <unordered_map>

#include <qcoreapplication.h>
#include <qhash.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qpointer.h>
#include <qrunnable.h>
#include <qstringbuilder.h>
#include <qstringview.h>
#include <qthreadpool.h>

#include <shellapi.h>

#include "../../core/backgroundpool.hpp"
#include "../../core/logcat.hpp"
#include "../startup.hpp"
#include "network.hpp"

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logWifi, "quickshell.windows.network.wifi", QtWarningMsg);

QString ssidToString(const DOT11_SSID& ssid) {
	auto len = static_cast<int>(ssid.uSSIDLength);
	if (len <= 0) return {};
	if (len > static_cast<int>(sizeof(ssid.ucSSID))) len = static_cast<int>(sizeof(ssid.ucSSID));
	return QString::fromUtf8(reinterpret_cast<const char*>(ssid.ucSSID), len); // NOLINT
}

QString macToString(const DOT11_MAC_ADDRESS& mac) {
	return QStringLiteral("%1:%2:%3:%4:%5:%6")
	    .arg(static_cast<uint>(mac[0]), 2, 16, QChar('0'))
	    .arg(static_cast<uint>(mac[1]), 2, 16, QChar('0'))
	    .arg(static_cast<uint>(mac[2]), 2, 16, QChar('0'))
	    .arg(static_cast<uint>(mac[3]), 2, 16, QChar('0'))
	    .arg(static_cast<uint>(mac[4]), 2, 16, QChar('0'))
	    .arg(static_cast<uint>(mac[5]), 2, 16, QChar('0'))
	    .toUpper();
}

QString securityLabel(DOT11_AUTH_ALGORITHM algo) {
	switch (algo) {
	case DOT11_AUTH_ALGO_80211_OPEN: return {};
	case DOT11_AUTH_ALGO_80211_SHARED_KEY: return QStringLiteral("WEP");
	case DOT11_AUTH_ALGO_WPA:
	case DOT11_AUTH_ALGO_WPA_PSK: return QStringLiteral("WPA");
	case DOT11_AUTH_ALGO_RSNA: return QStringLiteral("WPA2-Enterprise");
	case DOT11_AUTH_ALGO_RSNA_PSK: return QStringLiteral("WPA2");
	case DOT11_AUTH_ALGO_WPA3: return QStringLiteral("WPA3-Enterprise");
	case DOT11_AUTH_ALGO_WPA3_SAE: return QStringLiteral("WPA3");
	case DOT11_AUTH_ALGO_OWE: return QStringLiteral("OWE");
	default: return QStringLiteral("Secured");
	}
}

QString xmlEscape(const QString& s) {
	auto out = s;
	out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
	out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
	out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
	out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
	out.replace(QLatin1Char('\''), QStringLiteral("&apos;"));
	return out;
}

QString buildProfileXml(const QString& ssid, const QString& password, bool secure, bool wpa3) {
	auto escapedSsid = xmlEscape(ssid);
	auto hexSsid = QString::fromLatin1(ssid.toUtf8().toHex()).toUpper();

	QString authEncryption;
	QString sharedKey;
	if (!secure) {
		authEncryption = QStringLiteral("<authEncryption>"
		                                 "<authentication>open</authentication>"
		                                 "<encryption>none</encryption>"
		                                 "<useOneX>false</useOneX>"
		                                 "</authEncryption>");
	} else {
		auto authentication = wpa3 ? QStringLiteral("WPA3SAE") : QStringLiteral("WPA2PSK");
		authEncryption = QStringLiteral("<authEncryption>"
		                                 "<authentication>%1</authentication>"
		                                 "<encryption>AES</encryption>"
		                                 "<useOneX>false</useOneX>"
		                                 "</authEncryption>")
		                     .arg(authentication);
		sharedKey = QStringLiteral("<sharedKey>"
		                            "<keyType>passPhrase</keyType>"
		                            "<protected>false</protected>"
		                            "<keyMaterial>%1</keyMaterial>"
		                            "</sharedKey>")
		                .arg(xmlEscape(password));
	}

	return QStringLiteral(
	           "<?xml version=\"1.0\"?>"
	           "<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
	           "<name>%1</name>"
	           "<SSIDConfig><SSID><hex>%2</hex><name>%1</name></SSID></SSIDConfig>"
	           "<connectionType>ESS</connectionType>"
	           "<connectionMode>auto</connectionMode>"
	           "<MSM><security>%3%4</security></MSM>"
	           "</WLANProfile>"
	)
	    .arg(escapedSsid, hexSsid, authEncryption, sharedKey);
}

} // namespace

NetworkWifiBackend::NetworkWifiBackend(Network* owner): mOwner(owner) {}

NetworkWifiBackend::~NetworkWifiBackend() { this->closeHandle(); }

void NetworkWifiBackend::closeHandle() {
	if (this->mHandle != nullptr) {
		WlanCloseHandle(this->mHandle, nullptr);
		this->mHandle = nullptr;
	}
	this->mHasAdapter = false;
}

void NetworkWifiBackend::start() {
	startup::afterFirstFrame(this, [this]() { this->open(); });
}

void NetworkWifiBackend::open() {
	auto guard = QPointer<NetworkWifiBackend>(this);
	auto* task = QRunnable::create([guard]() {
		HANDLE handle = nullptr;
		DWORD negotiatedVersion = 0;
		auto result = WlanOpenHandle(2, nullptr, &negotiatedVersion, &handle);

		GUID guid {};
		auto hasAdapter = false;
		if (result == ERROR_SUCCESS && handle != nullptr) {
			PWLAN_INTERFACE_INFO_LIST ifaceList = nullptr;
			if (WlanEnumInterfaces(handle, nullptr, &ifaceList) == ERROR_SUCCESS
			    && ifaceList != nullptr)
			{
				if (ifaceList->dwNumberOfItems > 0) {
					guid = ifaceList->InterfaceInfo[0].InterfaceGuid;
					hasAdapter = true;
				}
				WlanFreeMemory(ifaceList);
			}
		}

		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [guard, result, handle, guid, hasAdapter]() {
			    if (guard == nullptr) {
				    if (handle != nullptr) WlanCloseHandle(handle, nullptr);
				    return;
			    }
			    guard->finishOpen(result, handle, guid, hasAdapter);
		    },
		    Qt::QueuedConnection
		);
	});

	BackgroundThreadPool::instance()->start(task);
}

void NetworkWifiBackend::finishOpen(DWORD openResult, HANDLE handle, GUID guid, bool hasAdapter) {
	if (openResult != ERROR_SUCCESS || handle == nullptr) {
		qCDebug(logWifi) << "WlanOpenHandle failed (no WLAN service/adapter?):" << openResult;
		this->mOwner->backendSetWifiAdapterPresent(false);
		return;
	}

	this->mHandle = handle;
	this->mInterfaceGuid = guid;
	this->mHasAdapter = hasAdapter;

	WlanRegisterNotification(
	    this->mHandle,
	    WLAN_NOTIFICATION_SOURCE_ACM | WLAN_NOTIFICATION_SOURCE_MSM,
	    TRUE,
	    &NetworkWifiBackend::notificationCallback,
	    this,
	    nullptr,
	    nullptr
	);

	this->mOwner->backendSetWifiAdapterPresent(this->mHasAdapter);
	if (this->mHasAdapter) {
		this->refreshRadioState();
		this->refreshCurrentConnection();
		this->refreshAvailableNetworks();
	}
}

void NetworkWifiBackend::refreshRadioState() {
	if (!this->mHasAdapter) return;

	DWORD size = 0;
	PVOID data = nullptr;
	auto result = WlanQueryInterface(
	    this->mHandle,
	    &this->mInterfaceGuid,
	    wlan_intf_opcode_radio_state,
	    nullptr,
	    &size,
	    &data,
	    nullptr
	);
	if (result != ERROR_SUCCESS || data == nullptr) {
		qCDebug(logWifi) << "WlanQueryInterface(radio_state) failed:" << result;
		return;
	}

	auto* state = static_cast<PWLAN_RADIO_STATE>(data);
	auto on = false;
	for (DWORD i = 0; i < state->dwNumberOfPhys; i++) {
		const auto& phy = state->PhyRadioState[i]; // NOLINT
		if (phy.dot11SoftwareRadioState == dot11_radio_state_on
		    && phy.dot11HardwareRadioState == dot11_radio_state_on)
		{
			on = true;
			break;
		}
	}
	WlanFreeMemory(data);

	this->mOwner->backendSetWifiRadioOn(on);
}

void NetworkWifiBackend::setRadioEnabled(bool enabled) {
	if (!this->mHasAdapter) {
		ShellExecuteW(nullptr, L"open", L"ms-settings:network-wifi", nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}

	WLAN_PHY_RADIO_STATE state {};
	state.dwPhyIndex = 0;
	state.dot11SoftwareRadioState = enabled ? dot11_radio_state_on : dot11_radio_state_off;

	auto result = WlanSetInterface(
	    this->mHandle,
	    &this->mInterfaceGuid,
	    wlan_intf_opcode_radio_state,
	    sizeof(state),
	    &state,
	    nullptr
	);
	if (result != ERROR_SUCCESS) {
		qCWarning(logWifi) << "WlanSetInterface(radio_state) failed (needs admin?):" << result
		                   << "-- opening Wi-Fi settings instead";
		ShellExecuteW(nullptr, L"open", L"ms-settings:network-wifi", nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}

	this->refreshRadioState();
}

void NetworkWifiBackend::scan() {
	if (!this->mHasAdapter) return;

	this->mOwner->backendSetWifiScanning(true);
	this->mScanRequested = true;
	auto result = WlanScan(this->mHandle, &this->mInterfaceGuid, nullptr, nullptr, nullptr);
	if (result != ERROR_SUCCESS) {
		qCDebug(logWifi) << "WlanScan failed:" << result;
		this->mScanRequested = false;
		this->mOwner->backendSetWifiScanning(false);
	}
}

void NetworkWifiBackend::refreshAvailableNetworks() {
	if (!this->mHasAdapter) return;

	PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
	auto result =
	    WlanGetAvailableNetworkList(this->mHandle, &this->mInterfaceGuid, 0, nullptr, &list);
	if (result == ERROR_ACCESS_DENIED) {
		this->mOwner->backendSetNeedsLocationPermission(true);
		return;
	}
	if (result != ERROR_SUCCESS || list == nullptr) {
		qCDebug(logWifi) << "WlanGetAvailableNetworkList failed:" << result;
		return;
	}
	this->mOwner->backendSetNeedsLocationPermission(false);

	QHash<QString, RawWifiNetwork> byName;
	for (DWORD i = 0; i < list->dwNumberOfItems; i++) {
		const auto& net = list->Network[i]; // NOLINT
		auto ssid = ssidToString(net.dot11Ssid);
		if (ssid.isEmpty()) continue;

		RawWifiNetwork raw;
		raw.ssid = ssid;
		raw.strength = static_cast<int>(net.wlanSignalQuality);
		raw.active = (net.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
		raw.hasProfile = (net.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0;
		raw.security = net.bSecurityEnabled ? securityLabel(net.dot11DefaultAuthAlgorithm) : QString();
		if (raw.hasProfile) raw.profileName = QString::fromWCharArray(net.strProfileName);

		auto existing = byName.constFind(ssid);
		if (existing == byName.constEnd()) {
			byName.insert(ssid, raw);
		} else if (raw.active && !existing->active) {
			byName.insert(ssid, raw);
		} else if (!raw.active && !existing->active && raw.strength > existing->strength) {
			byName.insert(ssid, raw);
		}
	}
	WlanFreeMemory(list);

	PWLAN_BSS_LIST bssList = nullptr;
	result = WlanGetNetworkBssList(
	    this->mHandle,
	    &this->mInterfaceGuid,
	    nullptr,
	    dot11_BSS_type_any,
	    FALSE,
	    nullptr,
	    &bssList
	);
	if (result == ERROR_ACCESS_DENIED) {
		this->mOwner->backendSetNeedsLocationPermission(true);
	} else if (result == ERROR_SUCCESS && bssList != nullptr) {
		std::unordered_map<std::wstring, LONG> bestRssi;
		for (DWORD i = 0; i < bssList->dwNumberOfItems; i++) {
			const auto& bss = bssList->wlanBssEntries[i]; // NOLINT
			auto ssid = ssidToString(bss.dot11Ssid);
			auto it = byName.find(ssid);
			if (it == byName.end()) continue;

			auto key = ssid.toStdWString();
			auto best = bestRssi.find(key);
			if (best != bestRssi.end() && best->second >= bss.lRssi) continue;
			bestRssi[key] = bss.lRssi;

			it->bssid = macToString(bss.dot11Bssid);
			it->frequency = static_cast<int>(bss.ulChCenterFrequency / 1000);
		}
		WlanFreeMemory(bssList);
	}

	std::vector<RawWifiNetwork> raw;
	raw.reserve(static_cast<size_t>(byName.size()));
	for (auto it = byName.constBegin(); it != byName.constEnd(); ++it) raw.push_back(it.value());

	this->mOwner->backendUpdateAvailableNetworks(raw);
}

void NetworkWifiBackend::refreshCurrentConnection() {
	if (!this->mHasAdapter) {
		this->mOwner->backendSetCurrentConnection({}, {}, 0, {}, false, false);
		return;
	}

	DWORD size = 0;
	PVOID data = nullptr;
	auto result = WlanQueryInterface(
	    this->mHandle,
	    &this->mInterfaceGuid,
	    wlan_intf_opcode_current_connection,
	    nullptr,
	    &size,
	    &data,
	    nullptr
	);
	if (result == ERROR_ACCESS_DENIED) {
		this->mOwner->backendSetNeedsLocationPermission(true);
		return;
	}
	if (result != ERROR_SUCCESS || data == nullptr) {
		this->mOwner->backendSetCurrentConnection({}, {}, 0, {}, false, false);
		return;
	}

	auto* conn = static_cast<PWLAN_CONNECTION_ATTRIBUTES>(data);
	auto connected = conn->isState == wlan_interface_state_connected;
	auto connecting = conn->isState == wlan_interface_state_associating
	                || conn->isState == wlan_interface_state_discovering
	                || conn->isState == wlan_interface_state_authenticating;
	auto ssid = ssidToString(conn->wlanAssociationAttributes.dot11Ssid);
	auto bssid = macToString(conn->wlanAssociationAttributes.dot11Bssid);
	auto quality = static_cast<int>(conn->wlanAssociationAttributes.wlanSignalQuality);
	auto security = conn->wlanSecurityAttributes.bSecurityEnabled
	                  ? securityLabel(conn->wlanSecurityAttributes.dot11AuthAlgorithm)
	                  : QString();
	WlanFreeMemory(data);

	this->mOwner->backendSetCurrentConnection(ssid, bssid, quality, security, connected, connecting);
}

void NetworkWifiBackend::connectToNetwork(
    const QString& ssid,
    const QString& profileName,
    const QString& password,
    bool hasProfile,
    bool secure,
    bool wpa3
) {
	if (!this->mHasAdapter) {
		this->mOwner->backendWifiConnectResult(ssid, false, QStringLiteral("other"));
		return;
	}

	auto targetProfile = ssid;

	if (!hasProfile) {
		auto xml = buildProfileXml(ssid, password, secure, wpa3);
		auto wxml = xml.toStdWString();
		DWORD reason = 0;
		auto setResult = WlanSetProfile(
		    this->mHandle,
		    &this->mInterfaceGuid,
		    0,
		    wxml.c_str(),
		    nullptr,
		    TRUE,
		    nullptr,
		    &reason
		);
		if (setResult != ERROR_SUCCESS) {
			qCWarning(logWifi) << "WlanSetProfile failed for" << ssid << ":" << setResult
			                   << "reason" << reason;
			this->mOwner->backendWifiConnectResult(ssid, false, QStringLiteral("other"));
			return;
		}
	} else if (!profileName.isEmpty()) {
		targetProfile = profileName;
	}

	auto profile = targetProfile.toStdWString();
	WLAN_CONNECTION_PARAMETERS params {};
	params.wlanConnectionMode = wlan_connection_mode_profile;
	params.strProfile = profile.c_str();
	params.pDot11Ssid = nullptr;
	params.pDesiredBssidList = nullptr;
	params.dot11BssType = dot11_BSS_type_infrastructure;
	params.dwFlags = 0;

	auto result = WlanConnect(this->mHandle, &this->mInterfaceGuid, &params, nullptr);
	if (result != ERROR_SUCCESS) {
		qCWarning(logWifi) << "WlanConnect failed for" << ssid << "(profile" << targetProfile
		                   << "):" << result;
		this->mOwner->backendWifiConnectResult(ssid, false, QStringLiteral("other"));
		return;
	}
}

void NetworkWifiBackend::disconnectActive() {
	if (!this->mHasAdapter) return;
	WlanDisconnect(this->mHandle, &this->mInterfaceGuid, nullptr);
}

void NetworkWifiBackend::forgetNetwork(const QString& ssid, const QString& profileName) {
	if (!this->mHasAdapter) return;
	auto name = (profileName.isEmpty() ? ssid : profileName).toStdWString();
	WlanDeleteProfile(this->mHandle, &this->mInterfaceGuid, name.c_str(), nullptr);
}

void WINAPI NetworkWifiBackend::notificationCallback(PWLAN_NOTIFICATION_DATA data, PVOID context) {
	if (data == nullptr || context == nullptr) return;
	auto* self = static_cast<NetworkWifiBackend*>(context);

	if (data->NotificationSource == WLAN_NOTIFICATION_SOURCE_ACM) {
		auto code = data->NotificationCode;

		if ((code == wlan_notification_acm_connection_complete
		     || code == wlan_notification_acm_connection_attempt_fail)
		    && data->pData != nullptr
		    && data->dwDataSize >= offsetof(WLAN_CONNECTION_NOTIFICATION_DATA, strProfileXml))
		{
			const auto* connData = static_cast<const WLAN_CONNECTION_NOTIFICATION_DATA*>(data->pData);
			auto ssid = ssidToString(connData->dot11Ssid);
			auto success = code == wlan_notification_acm_connection_complete
			            && connData->wlanReasonCode == WLAN_REASON_CODE_SUCCESS;
			auto reasonCode = connData->wlanReasonCode;

			QMetaObject::invokeMethod(
			    self,
			    [self, ssid, success, reasonCode]() {
				    self->mOwner->backendWifiConnectResult(
				        ssid,
				        success,
				        reasonCode != WLAN_REASON_CODE_SUCCESS ? QStringLiteral("auth")
				                                                : QStringLiteral("other")
				    );
				    self->refreshCurrentConnection();
				    self->refreshAvailableNetworks();
			    },
			    Qt::QueuedConnection
			);
			return;
		}

		QMetaObject::invokeMethod(
		    self,
		    [self, code]() { self->handleAcmNotification(code); },
		    Qt::QueuedConnection
		);
	} else if (data->NotificationSource == WLAN_NOTIFICATION_SOURCE_MSM) {
		auto code = data->NotificationCode;
		QMetaObject::invokeMethod(
		    self,
		    [self, code]() { self->handleMsmNotification(code); },
		    Qt::QueuedConnection
		);
	}
}

void NetworkWifiBackend::handleAcmNotification(DWORD code) {
	switch (code) {
	case wlan_notification_acm_scan_complete:
		this->mOwner->backendSetWifiScanning(false);
		if (this->mScanRequested || this->mOwner->wifiListVisible()) {
			this->mScanRequested = false;
			this->refreshAvailableNetworks();
		}
		break;
	case wlan_notification_acm_scan_fail:
		this->mScanRequested = false;
		this->mOwner->backendSetWifiScanning(false);
		break;
	case wlan_notification_acm_disconnecting:
	case wlan_notification_acm_disconnected:
		this->refreshCurrentConnection();
		this->refreshAvailableNetworks();
		break;
	case wlan_notification_acm_profile_change:
	case wlan_notification_acm_profile_name_change: this->refreshAvailableNetworks(); break;
	case wlan_notification_acm_interface_removal:
		this->mHasAdapter = false;
		this->mOwner->backendSetWifiAdapterPresent(false);
		this->mOwner->backendSetCurrentConnection({}, {}, 0, {}, false, false);
		this->mOwner->backendUpdateAvailableNetworks({});
		break;
	case wlan_notification_acm_interface_arrival: {
		PWLAN_INTERFACE_INFO_LIST ifaceList = nullptr;
		if (WlanEnumInterfaces(this->mHandle, nullptr, &ifaceList) == ERROR_SUCCESS
		    && ifaceList != nullptr)
		{
			if (ifaceList->dwNumberOfItems > 0) {
				this->mInterfaceGuid = ifaceList->InterfaceInfo[0].InterfaceGuid;
				this->mHasAdapter = true;
			}
			WlanFreeMemory(ifaceList);
		}
		this->mOwner->backendSetWifiAdapterPresent(this->mHasAdapter);
		if (this->mHasAdapter) {
			this->refreshRadioState();
			this->refreshCurrentConnection();
			this->refreshAvailableNetworks();
		}
		break;
	}
	default: break;
	}
}

void NetworkWifiBackend::handleMsmNotification(DWORD code) {
	switch (code) {
	case wlan_notification_msm_radio_state_change: this->refreshRadioState(); break;
	case wlan_notification_msm_connected:
	case wlan_notification_msm_disconnected:
		this->refreshCurrentConnection();
		this->refreshAvailableNetworks();
		break;
	case wlan_notification_msm_signal_quality_change: this->refreshCurrentConnection(); break;
	default: break;
	}
}

} // namespace qs::windows::sys
