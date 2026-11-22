#include "time_zones.hpp"
#include <algorithm>

#include <qdatetime.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtimezone.h>
#include <qvariant.h>

namespace qs::windows::sys {

QVariant TimeZones::offsetMinutes(const QString& id) const {
	auto zone = QTimeZone(id.toUtf8());
	if (!zone.isValid()) return {};
	return zone.offsetFromUtc(QDateTime::currentDateTimeUtc()) / 60;
}

QStringList TimeZones::ids() const {
	auto result = QStringList();
	for (const auto& id: QTimeZone::availableTimeZoneIds()) {
		if (id.contains('/') && !id.startsWith("Etc/")) result.append(QString::fromUtf8(id));
	}
	std::ranges::sort(result);
	return result;
}

} // namespace qs::windows::sys
