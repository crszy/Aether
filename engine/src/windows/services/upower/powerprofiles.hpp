#pragma once

#include <qlist.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qt_windows.h>

namespace qs::service::upower {

class PowerProfile: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		PowerSaver = 0,
		Balanced = 1,
		Performance = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::service::upower::PowerProfile::Enum profile);
};

class PerformanceDegradationReason: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		None = 0,
		LapDetected = 1,
		HighTemperature = 2,
	};
	Q_ENUM(Enum);

	// clang-format off
	Q_INVOKABLE static QString toString(qs::service::upower::PerformanceDegradationReason::Enum reason);
	// clang-format on
};

class PowerProfileHold {
	Q_GADGET;
	QML_VALUE_TYPE(powerProfileHold);
	Q_PROPERTY(qs::service::upower::PowerProfile::Enum profile MEMBER profile CONSTANT);
	Q_PROPERTY(QString applicationId MEMBER applicationId CONSTANT);
	Q_PROPERTY(QString reason MEMBER reason CONSTANT);

public:
	explicit PowerProfileHold() = default;

	PowerProfile::Enum profile = PowerProfile::Balanced;
	QString applicationId;
	QString reason;

	[[nodiscard]] bool operator==(const PowerProfileHold& other) const;
};

class PowerProfilesQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(PowerProfiles);
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(qs::service::upower::PowerProfile::Enum profile READ default WRITE setProfile NOTIFY profileChanged BINDABLE bindableProfile);
	Q_PROPERTY(bool hasPerformanceProfile READ default NOTIFY hasPerformanceProfileChanged BINDABLE bindableHasPerformanceProfile);
	Q_PROPERTY(qs::service::upower::PerformanceDegradationReason::Enum degradationReason READ default NOTIFY degradationReasonChanged BINDABLE bindableDegradationReason);
	Q_PROPERTY(QList<qs::service::upower::PowerProfileHold> holds READ default NOTIFY holdsChanged BINDABLE bindableHolds);
	// clang-format on

public:
	explicit PowerProfilesQml(QObject* parent = nullptr);
	~PowerProfilesQml() override;
	Q_DISABLE_COPY_MOVE(PowerProfilesQml);

	void setProfile(PowerProfile::Enum profile);

	[[nodiscard]] QBindable<PowerProfile::Enum> bindableProfile() const { return &this->bProfile; }

	[[nodiscard]] QBindable<bool> bindableHasPerformanceProfile() const {
		return &this->bHasPerformanceProfile;
	}

	[[nodiscard]] QBindable<PerformanceDegradationReason::Enum> bindableDegradationReason() const {
		return &this->bDegradationReason;
	}

	[[nodiscard]] QBindable<QList<PowerProfileHold>> bindableHolds() const { return &this->bHolds; }

	void refresh();

signals:
	void profileChanged();
	void hasPerformanceProfileChanged();
	void degradationReasonChanged();
	void holdsChanged();

private:
	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(PowerProfilesQml, PowerProfile::Enum, bProfile, &PowerProfilesQml::profileChanged);
	Q_OBJECT_BINDABLE_PROPERTY(PowerProfilesQml, bool, bHasPerformanceProfile, &PowerProfilesQml::hasPerformanceProfileChanged);
	Q_OBJECT_BINDABLE_PROPERTY(PowerProfilesQml, PerformanceDegradationReason::Enum, bDegradationReason, &PowerProfilesQml::degradationReasonChanged);
	Q_OBJECT_BINDABLE_PROPERTY(PowerProfilesQml, QList<PowerProfileHold>, bHolds, &PowerProfilesQml::holdsChanged);
	// clang-format on

	HPOWERNOTIFY notificationHandle = nullptr;
	void* effectiveModeHandle = nullptr;
};

} // namespace qs::service::upower
