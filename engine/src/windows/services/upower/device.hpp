#pragma once

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::service::upower {

class UPowerDeviceState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Unknown = 0,
		Charging = 1,
		Discharging = 2,
		Empty = 3,
		FullyCharged = 4,
		PendingCharge = 5,
		PendingDischarge = 6,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::service::upower::UPowerDeviceState::Enum status);
};

class UPowerDeviceType: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Unknown = 0,
		LinePower = 1,
		Battery = 2,
		Ups = 3,
		Monitor = 4,
		Mouse = 5,
		Keyboard = 6,
		Pda = 7,
		Phone = 8,
		MediaPlayer = 9,
		Tablet = 10,
		Computer = 11,
		GamingInput = 12,
		Pen = 13,
		Touchpad = 14,
		Modem = 15,
		Network = 16,
		Headset = 17,
		Speakers = 18,
		Headphones = 19,
		Video = 20,
		OtherAudio = 21,
		RemoteControl = 22,
		Printer = 23,
		Scanner = 24,
		Camera = 25,
		Wearable = 26,
		Toy = 27,
		BluetoothGeneric = 28,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::service::upower::UPowerDeviceType::Enum type);
};

class UPowerDevice: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(qs::service::upower::UPowerDeviceType::Enum type READ default NOTIFY typeChanged BINDABLE bindableType);
	Q_PROPERTY(bool powerSupply READ default NOTIFY powerSupplyChanged BINDABLE bindablePowerSupply);
	Q_PROPERTY(qreal energy READ default NOTIFY energyChanged BINDABLE bindableEnergy);
	Q_PROPERTY(qreal energyCapacity READ default NOTIFY energyCapacityChanged BINDABLE bindableEnergyCapacity);
	Q_PROPERTY(qreal changeRate READ default NOTIFY changeRateChanged BINDABLE bindableChangeRate);
	Q_PROPERTY(qreal timeToEmpty READ default NOTIFY timeToEmptyChanged BINDABLE bindableTimeToEmpty);
	Q_PROPERTY(qreal timeToFull READ default NOTIFY timeToFullChanged BINDABLE bindableTimeToFull);
	Q_PROPERTY(qreal percentage READ default NOTIFY percentageChanged BINDABLE bindablePercentage);
	Q_PROPERTY(bool isPresent READ default NOTIFY isPresentChanged BINDABLE bindableIsPresent);
	Q_PROPERTY(qs::service::upower::UPowerDeviceState::Enum state READ default NOTIFY stateChanged BINDABLE bindableState);
	Q_PROPERTY(qreal healthPercentage READ default NOTIFY healthPercentageChanged BINDABLE bindableHealthPercentage);
	Q_PROPERTY(bool healthSupported READ default NOTIFY healthSupportedChanged BINDABLE bindableHealthSupported);
	Q_PROPERTY(QString iconName READ default NOTIFY iconNameChanged BINDABLE bindableIconName);
	Q_PROPERTY(bool isLaptopBattery READ default NOTIFY isLaptopBatteryChanged BINDABLE bindableIsLaptopBattery);
	Q_PROPERTY(QString nativePath READ default NOTIFY nativePathChanged BINDABLE bindableNativePath);
	Q_PROPERTY(QString model READ default NOTIFY modelChanged BINDABLE bindableModel);
	Q_PROPERTY(bool ready READ default NOTIFY readyChanged BINDABLE bindableReady);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("UPowerDevices can only be acquired from UPower");

public:
	explicit UPowerDevice(QObject* parent = nullptr);

	void updateFromSystemPowerStatus();

	[[nodiscard]] QBindable<UPowerDeviceType::Enum> bindableType() const { return &this->bType; }
	[[nodiscard]] QBindable<bool> bindablePowerSupply() const { return &this->bPowerSupply; }
	[[nodiscard]] QBindable<qreal> bindableEnergy() const { return &this->bEnergy; }
	[[nodiscard]] QBindable<qreal> bindableEnergyCapacity() const { return &this->bEnergyCapacity; }
	[[nodiscard]] QBindable<qreal> bindableChangeRate() const { return &this->bChangeRate; }
	[[nodiscard]] QBindable<qreal> bindableTimeToEmpty() const { return &this->bTimeToEmpty; }
	[[nodiscard]] QBindable<qreal> bindableTimeToFull() const { return &this->bTimeToFull; }
	[[nodiscard]] QBindable<qreal> bindablePercentage() const { return &this->bPercentage; }
	[[nodiscard]] QBindable<bool> bindableIsPresent() const { return &this->bIsPresent; }
	[[nodiscard]] QBindable<UPowerDeviceState::Enum> bindableState() const { return &this->bState; }
	[[nodiscard]] QBindable<qreal> bindableHealthPercentage() const {
		return &this->bHealthPercentage;
	}
	[[nodiscard]] QBindable<bool> bindableHealthSupported() const { return &this->bHealthSupported; }
	[[nodiscard]] QBindable<QString> bindableIconName() const { return &this->bIconName; }
	[[nodiscard]] QBindable<bool> bindableIsLaptopBattery() const {
		return &this->bIsLaptopBattery;
	}
	[[nodiscard]] QBindable<QString> bindableNativePath() const { return &this->bNativePath; }
	[[nodiscard]] QBindable<QString> bindableModel() const { return &this->bModel; }
	[[nodiscard]] QBindable<bool> bindableReady() const { return &this->bReady; }

signals:
	void typeChanged();
	void powerSupplyChanged();
	void energyChanged();
	void energyCapacityChanged();
	void changeRateChanged();
	void timeToEmptyChanged();
	void timeToFullChanged();
	void percentageChanged();
	void isPresentChanged();
	void stateChanged();
	void healthPercentageChanged();
	void healthSupportedChanged();
	void iconNameChanged();
	void isLaptopBatteryChanged();
	void nativePathChanged();
	void modelChanged();
	void readyChanged();

private:
	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, UPowerDeviceType::Enum, bType, &UPowerDevice::typeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, bool, bPowerSupply, &UPowerDevice::powerSupplyChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, qreal, bEnergy, &UPowerDevice::energyChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, qreal, bEnergyCapacity, &UPowerDevice::energyCapacityChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, qreal, bChangeRate, &UPowerDevice::changeRateChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, qreal, bTimeToEmpty, &UPowerDevice::timeToEmptyChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, qreal, bTimeToFull, &UPowerDevice::timeToFullChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, qreal, bPercentage, &UPowerDevice::percentageChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, bool, bIsPresent, &UPowerDevice::isPresentChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, UPowerDeviceState::Enum, bState, &UPowerDevice::stateChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, qreal, bHealthPercentage, &UPowerDevice::healthPercentageChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, bool, bHealthSupported, &UPowerDevice::healthSupportedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, QString, bIconName, &UPowerDevice::iconNameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, bool, bIsLaptopBattery, &UPowerDevice::isLaptopBatteryChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, QString, bNativePath, &UPowerDevice::nativePathChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, QString, bModel, &UPowerDevice::modelChanged);
	Q_OBJECT_BINDABLE_PROPERTY(UPowerDevice, bool, bReady, &UPowerDevice::readyChanged);
	// clang-format on
};

} // namespace qs::service::upower
