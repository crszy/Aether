#pragma once

#include <qstring.h>
#include <qtypes.h>

namespace qs::bluetooth {

enum class RadioPower : quint8 {
	Unknown,
	Off,
	On,
	Blocked,
};

struct AdapterSnapshot {
	QString id;
	QString name;
	QString address;
	RadioPower power = RadioPower::Unknown;
	quint64 powerSeqDone = 0;
};

struct DeviceSnapshot {
	QString key;
	QString path;
	QString address;
	QString name;
	QString deviceName;
	QString icon;
	bool paired = false;
	bool connected = false;
	bool batteryAvailable = false;
	qreal battery = 0;

	bool operator==(const DeviceSnapshot& other) const = default;
};

enum class ConnectResult : quint8 {
	Sent,
	Unsupported,
	Failed,
};

} // namespace qs::bluetooth
