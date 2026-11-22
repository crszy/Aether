#include "core.hpp"

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpointer.h>
#include <qt_windows.h>
#include <qtimer.h>

#include "../../../core/logcat.hpp"
#include "../message_window.hpp"

namespace qs::service::upower {

namespace {
QS_LOGGING_CATEGORY(logUPower, "quickshell.service.upower", QtWarningMsg);

// NOLINTBEGIN(cert-err58-cpp)
const GUID kGuidAcDcPowerSource =
    {0x5d3e9a59, 0xe9d5, 0x4b00, {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}};
const GUID kGuidBatteryPercentageRemaining =
    {0xa7ad8041, 0xb45a, 0x4cae, {0x87, 0xa3, 0xee, 0xcb, 0xb4, 0x68, 0xa9, 0xe1}};
const GUID kGuidEnergySaverStatus =
    {0x550e8400, 0xe29b, 0x41d4, {0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00}};
// NOLINTEND(cert-err58-cpp)

HPOWERNOTIFY registerSetting(HWND hwnd, const GUID& guid) {
	auto* handle = RegisterPowerSettingNotification(hwnd, &guid, DEVICE_NOTIFY_WINDOW_HANDLE);
	if (handle == nullptr) {
		qCDebug(logUPower) << "RegisterPowerSettingNotification failed for a power setting GUID"
		                   << "(non-fatal, other triggers still refresh UPower).";
	}
	return handle;
}

} // namespace

UPowerQml::UPowerQml(QObject* parent): QObject(parent) {
	this->refresh();

	auto* window = qs::windows::services::ServiceMessageWindow::instance();

	window->addHandler(WM_POWERBROADCAST, [self = QPointer(this)](WPARAM wParam, LPARAM /*lParam*/) {
		if (self && wParam == PBT_POWERSETTINGCHANGE) {
			self->refresh();
		}
	});

	this->acdcNotify = registerSetting(window->hwnd(), kGuidAcDcPowerSource);
	this->batteryPercentNotify = registerSetting(window->hwnd(), kGuidBatteryPercentageRemaining);
	this->energySaverNotify = registerSetting(window->hwnd(), kGuidEnergySaverStatus);

	this->pollTimer.setInterval(30000);
	QObject::connect(&this->pollTimer, &QTimer::timeout, this, &UPowerQml::refresh);
	this->pollTimer.start();
}

UPowerQml::~UPowerQml() {
	if (this->acdcNotify != nullptr) UnregisterPowerSettingNotification(this->acdcNotify);
	if (this->batteryPercentNotify != nullptr) {
		UnregisterPowerSettingNotification(this->batteryPercentNotify);
	}
	if (this->energySaverNotify != nullptr) {
		UnregisterPowerSettingNotification(this->energySaverNotify);
	}
}

void UPowerQml::refresh() {
	this->mDevice.updateFromSystemPowerStatus();

	auto present = this->mDevice.bindableIsPresent().value();
	auto alreadyListed = this->mDevices.valueList().contains(&this->mDevice);

	if (present && !alreadyListed) {
		this->mDevices.insertObject(&this->mDevice);
	} else if (!present && alreadyListed) {
		this->mDevices.removeObject(&this->mDevice);
	}

	SYSTEM_POWER_STATUS status {};
	if (GetSystemPowerStatus(&status)) {
		this->bOnBattery = status.ACLineStatus == 0;
	}
}

} // namespace qs::service::upower
