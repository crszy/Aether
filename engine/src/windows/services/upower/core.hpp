#pragma once

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qqmllist.h>
#include <qt_windows.h>
#include <qtimer.h>
#include <qtmetamacros.h>

#include "../../../core/doc.hpp"
#include "../../../core/model.hpp"
#include "device.hpp"

namespace qs::service::upower {

class UPowerQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(UPower);
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(qs::service::upower::UPowerDevice* displayDevice READ displayDevice CONSTANT);
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::service::upower::UPowerDevice>*);
	Q_PROPERTY(UntypedObjectModel* devices READ devices CONSTANT);
	Q_PROPERTY(bool onBattery READ default NOTIFY onBatteryChanged BINDABLE bindableOnBattery);
	// clang-format on

public:
	explicit UPowerQml(QObject* parent = nullptr);
	~UPowerQml() override;
	Q_DISABLE_COPY_MOVE(UPowerQml);

	[[nodiscard]] UPowerDevice* displayDevice() { return &this->mDevice; }
	[[nodiscard]] ObjectModel<UPowerDevice>* devices() { return &this->mDevices; }

	[[nodiscard]] QBindable<bool> bindableOnBattery() const { return &this->bOnBattery; }

	void refresh();

signals:
	void onBatteryChanged();

private:
	UPowerDevice mDevice {this};
	ObjectModel<UPowerDevice> mDevices {this};
	QTimer pollTimer;

	HPOWERNOTIFY acdcNotify = nullptr;
	HPOWERNOTIFY batteryPercentNotify = nullptr;
	HPOWERNOTIFY energySaverNotify = nullptr;

	Q_OBJECT_BINDABLE_PROPERTY(UPowerQml, bool, bOnBattery, &UPowerQml::onBatteryChanged);
};

} // namespace qs::service::upower
