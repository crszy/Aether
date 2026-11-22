#include "device.hpp"

#include <qt_windows.h>

#include <qobject.h>
#include <qstring.h>
#include <qtypes.h>

namespace qs::service::upower {

QString UPowerDeviceState::toString(UPowerDeviceState::Enum status) {
	switch (status) {
	case UPowerDeviceState::Unknown: return "Unknown";
	case UPowerDeviceState::Charging: return "Charging";
	case UPowerDeviceState::Discharging: return "Discharging";
	case UPowerDeviceState::Empty: return "Empty";
	case UPowerDeviceState::FullyCharged: return "Fully Charged";
	case UPowerDeviceState::PendingCharge: return "Pending Charge";
	case UPowerDeviceState::PendingDischarge: return "Pending Discharge";
	default: return "Invalid Status";
	}
}

QString UPowerDeviceType::toString(UPowerDeviceType::Enum type) {
	switch (type) {
	case UPowerDeviceType::Unknown: return "Unknown";
	case UPowerDeviceType::LinePower: return "Line Power";
	case UPowerDeviceType::Battery: return "Battery";
	case UPowerDeviceType::Ups: return "Ups";
	case UPowerDeviceType::Monitor: return "Monitor";
	case UPowerDeviceType::Mouse: return "Mouse";
	case UPowerDeviceType::Keyboard: return "Keyboard";
	case UPowerDeviceType::Pda: return "Pda";
	case UPowerDeviceType::Phone: return "Phone";
	case UPowerDeviceType::MediaPlayer: return "Media Player";
	case UPowerDeviceType::Tablet: return "Tablet";
	case UPowerDeviceType::Computer: return "Computer";
	case UPowerDeviceType::GamingInput: return "Gaming Input";
	case UPowerDeviceType::Pen: return "Pen";
	case UPowerDeviceType::Touchpad: return "Touchpad";
	case UPowerDeviceType::Modem: return "Modem";
	case UPowerDeviceType::Network: return "Network";
	case UPowerDeviceType::Headset: return "Headset";
	case UPowerDeviceType::Speakers: return "Speakers";
	case UPowerDeviceType::Headphones: return "Headphones";
	case UPowerDeviceType::Video: return "Video";
	case UPowerDeviceType::OtherAudio: return "Other Audio";
	case UPowerDeviceType::RemoteControl: return "Remote Control";
	case UPowerDeviceType::Printer: return "Printer";
	case UPowerDeviceType::Scanner: return "Scanner";
	case UPowerDeviceType::Camera: return "Camera";
	case UPowerDeviceType::Wearable: return "Wearable";
	case UPowerDeviceType::Toy: return "Toy";
	case UPowerDeviceType::BluetoothGeneric: return "Bluetooth Generic";
	default: return "Invalid Type";
	}
}

UPowerDevice::UPowerDevice(QObject* parent): QObject(parent) {
	this->bIsLaptopBattery.setBinding([this]() {
		return this->bType == UPowerDeviceType::Battery && this->bPowerSupply.value();
	});

	this->bHealthSupported = false;
	this->bHealthPercentage = 100.0;
	this->bReady = true;
}

void UPowerDevice::updateFromSystemPowerStatus() {
	SYSTEM_POWER_STATUS status {};
	if (!GetSystemPowerStatus(&status)) {
		this->bIsPresent = false;
		this->bType = UPowerDeviceType::Unknown;
		return;
	}

	auto present = status.BatteryFlag != 128 && status.BatteryFlag != 255;

	this->bIsPresent = present;
	this->bPowerSupply = present;
	this->bType = present ? UPowerDeviceType::Battery : UPowerDeviceType::Unknown;
	this->bNativePath = present ? QStringLiteral("WindowsBattery") : QString();
	this->bModel = present ? QStringLiteral("System Battery") : QString();

	auto percent =
	    status.BatteryLifePercent == 255 ? 0.0 : qreal(status.BatteryLifePercent) / 100.0;
	this->bPercentage = present ? percent : 0.0;

	auto charging = (status.BatteryFlag & 8) != 0;
	auto onAc = status.ACLineStatus == 1;

	UPowerDeviceState::Enum state = UPowerDeviceState::Unknown;
	if (present) {
		if (charging) {
			state = UPowerDeviceState::Charging;
		} else if (onAc) {
			state = percent >= 0.99 ? UPowerDeviceState::FullyCharged : UPowerDeviceState::Charging;
		} else {
			state = UPowerDeviceState::Discharging;
		}
	}
	this->bState = state;

	auto lifeTimeUnknown = status.BatteryLifeTime == static_cast<DWORD>(-1);
	this->bTimeToEmpty =
	    (present && !onAc && !lifeTimeUnknown) ? qreal(status.BatteryLifeTime) : 0.0;
	this->bTimeToFull = 0.0;

	this->bIconName = present
	    ? (charging ? QStringLiteral("battery-good-charging-symbolic")
	                : QStringLiteral("battery-good-symbolic"))
	    : QString();
}

} // namespace qs::service::upower
