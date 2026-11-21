#include "powerprofiles.hpp"

#include <qcontainerfwd.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qstring.h>
#include <qt_windows.h>

#include <powersetting.h>

#include "../../../core/logcat.hpp"

namespace qs::service::upower {

namespace {
QS_LOGGING_CATEGORY(logPowerProfiles, "quickshell.service.powerprofiles", QtWarningMsg);

using PowerGetEffectiveOverlaySchemeFn = DWORD(WINAPI*)(GUID*);
using PowerSetActiveOverlaySchemeFn = DWORD(WINAPI*)(GUID);

struct OverlaySchemeApi {
	PowerGetEffectiveOverlaySchemeFn get = nullptr;
	PowerSetActiveOverlaySchemeFn set = nullptr;

	static const OverlaySchemeApi& instance() {
		static const auto api = []() {
			OverlaySchemeApi result;
			if (auto* module = LoadLibraryW(L"powrprof.dll")) {
				result.get = reinterpret_cast<PowerGetEffectiveOverlaySchemeFn>( // NOLINT
				    GetProcAddress(module, "PowerGetEffectiveOverlayScheme")
				);
				result.set = reinterpret_cast<PowerSetActiveOverlaySchemeFn>( // NOLINT
				    GetProcAddress(module, "PowerSetActiveOverlayScheme")
				);
			}
			return result;
		}();
		return api;
	}
};

const GUID GUID_OVERLAY_BEST_EFFICIENCY =
    {0x961cc777, 0x2547, 0x4f9d, {0x81, 0x74, 0x7d, 0x86, 0x18, 0x1b, 0x8a, 0x7a}};
const GUID GUID_OVERLAY_BEST_PERFORMANCE =
    {0xded574b5, 0x45a0, 0x4f42, {0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38}};
const GUID GUID_OVERLAY_BALANCED = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};

PowerProfile::Enum profileFromOverlayGuid(const GUID& guid) {
	if (IsEqualGUID(guid, GUID_OVERLAY_BEST_EFFICIENCY)) return PowerProfile::PowerSaver;
	if (IsEqualGUID(guid, GUID_OVERLAY_BEST_PERFORMANCE)) return PowerProfile::Performance;
	return PowerProfile::Balanced;
}

GUID overlayGuidFromProfile(PowerProfile::Enum profile) {
	switch (profile) {
	case PowerProfile::PowerSaver: return GUID_OVERLAY_BEST_EFFICIENCY;
	case PowerProfile::Performance: return GUID_OVERLAY_BEST_PERFORMANCE;
	default: return GUID_OVERLAY_BALANCED;
	}
}

void CALLBACK effectiveModeChanged(EFFECTIVE_POWER_MODE /*mode*/, void* context) {
	auto* self = static_cast<PowerProfilesQml*>(context);
	QMetaObject::invokeMethod(self, &PowerProfilesQml::refresh, Qt::QueuedConnection);
}

} // namespace

QString PowerProfile::toString(PowerProfile::Enum profile) {
	switch (profile) {
	case PowerProfile::PowerSaver: return QStringLiteral("PowerSaver");
	case PowerProfile::Balanced: return QStringLiteral("Balanced");
	case PowerProfile::Performance: return QStringLiteral("Performance");
	default: return QStringLiteral("Invalid");
	}
}

QString PerformanceDegradationReason::toString(PerformanceDegradationReason::Enum reason) {
	switch (reason) {
	case PerformanceDegradationReason::LapDetected: return QStringLiteral("LapDetected");
	case PerformanceDegradationReason::HighTemperature: return QStringLiteral("HighTemperature");
	default: return QStringLiteral("None");
	}
}

bool PowerProfileHold::operator==(const PowerProfileHold& other) const {
	return other.profile == this->profile && other.applicationId == this->applicationId
	    && other.reason == this->reason;
}

PowerProfilesQml::PowerProfilesQml(QObject* parent): QObject(parent) {
	this->bHasPerformanceProfile = true;
	this->bDegradationReason = PerformanceDegradationReason::None;
	this->bHolds = QList<PowerProfileHold> {};

	this->refresh();

	auto status = PowerRegisterForEffectivePowerModeNotifications(
	    EFFECTIVE_POWER_MODE_V1,
	    &effectiveModeChanged,
	    this,
	    &this->effectiveModeHandle
	);

	if (status != ERROR_SUCCESS) {
		qCWarning(logPowerProfiles) << "PowerRegisterForEffectivePowerModeNotifications failed:"
		                            << status;
		this->effectiveModeHandle = nullptr;
	}
}

PowerProfilesQml::~PowerProfilesQml() {
	if (this->effectiveModeHandle != nullptr) {
		PowerUnregisterFromEffectivePowerModeNotifications(this->effectiveModeHandle);
	}
}

void PowerProfilesQml::refresh() {
	const auto& api = OverlaySchemeApi::instance();
	if (api.get == nullptr) {
		qCWarning(logPowerProfiles) << "PowerGetEffectiveOverlayScheme is not available";
		return;
	}

	GUID activeGuid {};
	auto status = api.get(&activeGuid);

	if (status != ERROR_SUCCESS) {
		qCWarning(logPowerProfiles) << "PowerGetEffectiveOverlayScheme failed:" << status;
		return;
	}

	this->bProfile = profileFromOverlayGuid(activeGuid);
}

void PowerProfilesQml::setProfile(PowerProfile::Enum profile) {
	if (profile < PowerProfile::PowerSaver || profile > PowerProfile::Performance) {
		qCWarning(logPowerProfiles) << "Tried to request invalid power profile" << profile;
		return;
	}

	const auto& api = OverlaySchemeApi::instance();
	if (api.set == nullptr) {
		qCWarning(logPowerProfiles) << "PowerSetActiveOverlayScheme is not available";
		return;
	}

	auto guid = overlayGuidFromProfile(profile);
	auto status = api.set(guid);

	if (status != ERROR_SUCCESS) {
		qCWarning(logPowerProfiles) << "PowerSetActiveOverlayScheme failed:" << status;
		return;
	}

	this->bProfile = profile;
}

} // namespace qs::service::upower
