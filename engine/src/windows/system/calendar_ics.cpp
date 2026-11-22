#include "calendar_ics.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <utility>

#include <qbytearray.h>
#include <qbytearrayview.h>
#include <qchar.h>
#include <qdatetime.h>
#include <qhash.h>
#include <qlist.h>
#include <qregularexpression.h>
#include <qset.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qstringview.h>
#include <qtimezone.h>
#include <qtypes.h>

namespace qs::windows::sys::ics {

namespace {

using L1 = QLatin1StringView;

constexpr int MAX_ITERATIONS = 10000;
constexpr int MAX_INTERVAL = 1000;
constexpr int MAX_YEAR = 9999;
constexpr int MAX_SPAN_DAYS = 36600;
constexpr qsizetype DESCRIPTION_LIMIT = 500;
constexpr qint64 DAY_MS = 86400000;
constexpr qint64 SPAN_SLACK_MS = 2 * DAY_MS;
constexpr qint64 UNBOUNDED = std::numeric_limits<qint64>::max();

struct Property {
	QString name;
	QHash<QString, QString> params;
	QString value;
};

struct TimeValue {
	QDate date;
	QTime time;
	bool dateOnly = false;
	bool utc = false;
	QString tzid;

	[[nodiscard]] bool isValid() const { return this->date.isValid(); }
};

struct ZoneBlock {
	QString location;
	QString standardStart;
	QString daylightStart;
	int standardOffset = 0;
	int daylightOffset = 0;
	bool hasStandard = false;
	bool hasDaylight = false;
};

struct RawEvent {
	QList<Property> properties;

	[[nodiscard]] const Property* find(L1 name) const {
		for (const auto& property: this->properties) {
			if (property.name == name) return &property;
		}
		return nullptr;
	}

	[[nodiscard]] QString text(L1 name) const {
		const auto* property = this->find(name);
		return property != nullptr ? property->value : QString();
	}
};

struct RuleExtras {
	TimeValue until;
	QList<int> byHour;
	QList<int> byMinute;
	QList<int> bySecond;
};

struct Built {
	Event event;
	TimeValue recurrenceId;
	int sequence = 0;
	bool cancelled = false;
	bool isOverride = false;
};

bool wanted(QByteArrayView name) {
	static constexpr auto NAMES = std::array<QByteArrayView, 20> {
	    QByteArrayView("BEGIN"),       QByteArrayView("END"),
	    QByteArrayView("X-WR-CALNAME"), QByteArrayView("NAME"),
	    QByteArrayView("TZID"),        QByteArrayView("X-LIC-LOCATION"),
	    QByteArrayView("TZOFFSETTO"),  QByteArrayView("DTSTART"),
	    QByteArrayView("DTEND"),       QByteArrayView("DURATION"),
	    QByteArrayView("UID"),         QByteArrayView("SUMMARY"),
	    QByteArrayView("LOCATION"),    QByteArrayView("DESCRIPTION"),
	    QByteArrayView("STATUS"),      QByteArrayView("RRULE"),
	    QByteArrayView("EXDATE"),      QByteArrayView("RDATE"),
	    QByteArrayView("RECURRENCE-ID"), QByteArrayView("SEQUENCE"),
	};
	return std::find(NAMES.begin(), NAMES.end(), name) != NAMES.end();
}

QList<QByteArray> unfoldLines(QByteArray data) {
	if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
	if (!data.contains('\n')) data.replace('\r', '\n');

	auto lines = QList<QByteArray>();
	auto size = data.size();
	qsizetype pos = 0;
	while (pos < size) {
		auto eol = data.indexOf('\n', pos);
		if (eol < 0) eol = size;
		auto end = eol;
		if (end > pos && data.at(end - 1) == '\r') --end;
		if (end > pos) {
			auto first = data.at(pos);
			if ((first == ' ' || first == '\t') && !lines.isEmpty()) {
				lines.last().append(data.constData() + pos + 1, end - pos - 1);
			} else {
				lines.append(data.mid(pos, end - pos));
			}
		}
		pos = eol + 1;
	}

	return lines;
}

QByteArray propertyName(const QByteArray& line) {
	qsizetype end = 0;
	while (end < line.size() && line.at(end) != ';' && line.at(end) != ':') ++end;
	return line.left(end).trimmed().toUpper();
}

bool parseProperty(const QString& line, Property& property) {
	auto size = line.size();
	qsizetype index = 0;
	while (index < size && line.at(index) != u';' && line.at(index) != u':') ++index;
	if (index >= size) return false;
	property.name = line.left(index).trimmed().toUpper();

	while (index < size && line.at(index) == u';') {
		++index;
		auto keyStart = index;
		while (index < size && line.at(index) != u'=' && line.at(index) != u';' && line.at(index) != u':')
		{
			++index;
		}
		auto key = line.mid(keyStart, index - keyStart).trimmed().toUpper();
		auto value = QString();
		if (index < size && line.at(index) == u'=') {
			++index;
			while (index < size && line.at(index) != u';' && line.at(index) != u':') {
				if (line.at(index) == u'"') {
					auto close = line.indexOf(u'"', index + 1);
					if (close < 0) close = size;
					value += line.mid(index + 1, close - index - 1);
					index = std::min(close + 1, size);
				} else {
					value += line.at(index);
					++index;
				}
			}
		}
		if (!key.isEmpty() && !property.params.contains(key)) property.params.insert(key, value);
	}

	if (index >= size || line.at(index) != u':') return false;
	property.value = line.mid(index + 1);
	return true;
}

QString unescape(const QString& value) {
	if (!value.contains(u'\\')) return value;

	auto result = QString();
	result.reserve(value.size());
	for (qsizetype i = 0; i < value.size(); ++i) {
		auto c = value.at(i);
		if (c != u'\\' || i + 1 >= value.size()) {
			result.append(c);
			continue;
		}
		auto next = value.at(++i);
		if (next == u'n' || next == u'N') result.append(u'\n');
		else result.append(next);
	}
	return result;
}

QString singleLine(const QString& text) { return text.simplified(); }

QString decodeEntities(const QString& text) {
	static const auto numeric = QRegularExpression(QStringLiteral("&#([xX]?)([0-9a-fA-F]{1,8});"));

	auto result = QString();
	qsizetype last = 0;
	auto matches = numeric.globalMatch(text);
	while (matches.hasNext()) {
		auto match = matches.next();
		result += QStringView(text).mid(last, match.capturedStart() - last);
		auto ok = false;
		auto code = match.captured(2).toUInt(&ok, match.captured(1).isEmpty() ? 10 : 16);
		if (ok && code > 0 && code <= 0x10FFFF) {
			auto character = static_cast<char32_t>(code);
			result += QString::fromUcs4(&character, 1);
		} else {
			result += match.captured(0);
		}
		last = match.capturedEnd();
	}
	result += QStringView(text).mid(last);

	result.replace(L1("&nbsp;"), L1(" "));
	result.replace(L1("&lt;"), L1("<"));
	result.replace(L1("&gt;"), L1(">"));
	result.replace(L1("&quot;"), L1("\""));
	result.replace(L1("&apos;"), L1("'"));
	result.replace(L1("&amp;"), L1("&"));
	return result;
}

QString plainText(QString text) {
	text.replace(L1("\r\n"), L1("\n"));
	text.replace(u'\r', u'\n');

	static const auto htmlHint = QRegularExpression(
	    QStringLiteral(
	        "<\\s*/?\\s*(br|p|div|span|a|b|i|u|em|strong|ul|ol|li|html|body|font|table|tr|td|h[1-6])\\b[^>]*>"
	    ),
	    QRegularExpression::CaseInsensitiveOption
	);
	if (text.contains(u'<') && text.contains(htmlHint)) {
		static const auto breaks = QRegularExpression(
		    QStringLiteral("<\\s*br\\s*/?\\s*>|<\\s*/\\s*(p|div|li|tr|h[1-6])\\s*>"),
		    QRegularExpression::CaseInsensitiveOption
		);
		static const auto items =
		    QRegularExpression(QStringLiteral("<\\s*li\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
		static const auto tags = QRegularExpression(QStringLiteral("<[^>]*>"));
		text.replace(breaks, QStringLiteral("\n"));
		text.replace(items, QStringLiteral("- "));
		text.remove(tags);
		text = decodeEntities(text);
	}

	static const auto trailing = QRegularExpression(QStringLiteral("[ \\t]+\\n"));
	static const auto blank = QRegularExpression(QStringLiteral("\\n{3,}"));
	text.replace(trailing, QStringLiteral("\n"));
	text.replace(blank, QStringLiteral("\n\n"));
	text = text.trimmed();

	if (text.size() > DESCRIPTION_LIMIT) {
		text.truncate(DESCRIPTION_LIMIT);
		if (!text.isEmpty() && text.back().isHighSurrogate()) text.chop(1);
		text = text.trimmed();
		text.append(QChar(0x2026));
	}
	return text;
}

int digitsAt(QStringView text, qsizetype pos, qsizetype length, bool& ok) {
	if (pos + length > text.size()) {
		ok = false;
		return 0;
	}
	auto valueOk = false;
	auto value = text.mid(pos, length).toInt(&valueOk);
	if (!valueOk) ok = false;
	return value;
}

TimeValue parseTimeText(const QString& raw, const QHash<QString, QString>& params) {
	auto result = TimeValue();
	auto text = raw.trimmed();
	text.remove(u'-');
	text.remove(u':');
	if (text.size() < 8) return result;

	auto ok = true;
	auto date = QDate(digitsAt(text, 0, 4, ok), digitsAt(text, 4, 2, ok), digitsAt(text, 6, 2, ok));
	if (!ok || !date.isValid()) return result;

	auto separator = text.indexOf(u'T', 8, Qt::CaseInsensitive);
	auto forcedDate = params.value(QStringLiteral("VALUE")).compare(L1("DATE"), Qt::CaseInsensitive) == 0;
	if (forcedDate || separator < 0) {
		result.date = date;
		result.dateOnly = true;
		return result;
	}

	auto clock = text.mid(separator + 1);
	if (clock.endsWith(u'Z', Qt::CaseInsensitive)) {
		result.utc = true;
		clock.chop(1);
	}

	auto hour = digitsAt(clock, 0, 2, ok);
	auto minute = clock.size() >= 4 ? digitsAt(clock, 2, 2, ok) : 0;
	auto second = clock.size() >= 6 ? digitsAt(clock, 4, 2, ok) : 0;
	if (!ok) return result;
	second = std::min(second, 59);
	if (hour == 24 && minute == 0 && second == 0) {
		date = date.addDays(1);
		hour = 0;
	}

	auto time = QTime(hour, minute, second);
	if (!time.isValid()) return result;

	result.date = date;
	result.time = time;
	if (!result.utc) result.tzid = params.value(QStringLiteral("TZID")).trimmed();
	return result;
}

bool parseDuration(const QString& raw, int& days, qint64& secs) {
	auto text = raw.trimmed().toUpper();
	auto negative = false;
	if (text.startsWith(u'+')) {
		text.remove(0, 1);
	} else if (text.startsWith(u'-')) {
		negative = true;
		text.remove(0, 1);
	}
	if (!text.startsWith(u'P')) return false;

	qint64 totalDays = 0;
	qint64 totalSecs = 0;
	qint64 number = 0;
	auto inTime = false;
	auto digits = false;
	for (qsizetype i = 1; i < text.size(); ++i) {
		auto c = text.at(i);
		if (c.isDigit()) {
			number = std::min<qint64>(number * 10 + c.digitValue(), 100000000);
			digits = true;
			continue;
		}
		if (c == u'T') {
			inTime = true;
			continue;
		}
		if (!digits) return false;
		if (c == u'W' && !inTime) totalDays += number * 7;
		else if (c == u'D' && !inTime) totalDays += number;
		else if (c == u'H' && inTime) totalSecs += number * 3600;
		else if (c == u'M' && inTime) totalSecs += number * 60;
		else if (c == u'S' && inTime) totalSecs += number;
		else return false;
		number = 0;
		digits = false;
	}

	totalDays = std::min<qint64>(totalDays, MAX_SPAN_DAYS);
	days = static_cast<int>(negative ? -totalDays : totalDays);
	secs = negative ? -totalSecs : totalSecs;
	return true;
}

std::optional<int> parseOffset(const QString& raw) {
	auto text = raw.trimmed();
	if (text.size() < 5) return std::nullopt;
	auto sign = text.at(0) == u'-' ? -1 : (text.at(0) == u'+' ? 1 : 0);
	if (sign == 0) return std::nullopt;

	auto ok = true;
	auto hours = digitsAt(text, 1, 2, ok);
	auto minutes = digitsAt(text, 3, 2, ok);
	auto seconds = text.size() >= 7 ? digitsAt(text, 5, 2, ok) : 0;
	if (!ok) return std::nullopt;
	return sign * (hours * 3600 + minutes * 60 + seconds);
}

int weekdayFromCode(QStringView code) {
	static constexpr auto CODES = std::array<QStringView, 7> {
	    QStringView(u"MO"),
	    QStringView(u"TU"),
	    QStringView(u"WE"),
	    QStringView(u"TH"),
	    QStringView(u"FR"),
	    QStringView(u"SA"),
	    QStringView(u"SU"),
	};
	for (auto i = 0; i < 7; ++i) {
		if (code.compare(CODES.at(i), Qt::CaseInsensitive) == 0) return i + 1;
	}
	return 0;
}

QList<int> intList(const QString& value, int min, int max) {
	auto result = QList<int>();
	for (const auto& part: value.split(u',', Qt::SkipEmptyParts)) {
		auto ok = false;
		auto number = part.trimmed().toInt(&ok);
		if (ok && number != 0 && number >= min && number <= max) result.append(number);
	}
	return result;
}

QList<int> timeList(const QString& value, int max) {
	auto result = QList<int>();
	for (const auto& part: value.split(u',', Qt::SkipEmptyParts)) {
		auto ok = false;
		auto number = part.trimmed().toInt(&ok);
		if (ok && number >= 0 && number <= max) result.append(number);
	}
	return result;
}

RecurrenceRule parseRule(const QString& text, RuleExtras& extras) {
	auto rule = RecurrenceRule();

	for (const auto& part: text.split(u';', Qt::SkipEmptyParts)) {
		auto separator = part.indexOf(u'=');
		if (separator < 0) continue;
		auto key = part.left(separator).trimmed().toUpper();
		auto value = part.mid(separator + 1).trimmed();

		if (key == L1("FREQ")) {
			auto frequency = value.toUpper();
			if (frequency == L1("DAILY")) rule.frequency = Frequency::Daily;
			else if (frequency == L1("WEEKLY")) rule.frequency = Frequency::Weekly;
			else if (frequency == L1("MONTHLY")) rule.frequency = Frequency::Monthly;
			else if (frequency == L1("YEARLY")) rule.frequency = Frequency::Yearly;
			else rule.frequency = Frequency::None;
		} else if (key == L1("INTERVAL")) {
			auto ok = false;
			auto interval = value.toInt(&ok);
			rule.interval = ok && interval > 0 ? std::min(interval, MAX_INTERVAL) : 1;
		} else if (key == L1("COUNT")) {
			auto ok = false;
			auto count = value.toInt(&ok);
			if (ok && count > 0) rule.count = count;
			else rule.supported = false;
		} else if (key == L1("UNTIL")) {
			extras.until = parseTimeText(value, {});
		} else if (key == L1("BYDAY")) {
			for (const auto& token: value.split(u',', Qt::SkipEmptyParts)) {
				auto trimmed = token.trimmed();
				if (trimmed.size() < 2) continue;
				auto weekday = weekdayFromCode(QStringView(trimmed).right(2));
				if (weekday == 0) continue;
				auto prefix = trimmed.left(trimmed.size() - 2);
				auto ordinal = 0;
				if (!prefix.isEmpty()) {
					auto ok = false;
					ordinal = prefix.toInt(&ok);
					if (!ok || ordinal < -53 || ordinal > 53) continue;
				}
				rule.byDay.append(WeekdaySpec {.ordinal = ordinal, .weekday = weekday});
			}
		} else if (key == L1("BYMONTHDAY")) {
			rule.byMonthDay = intList(value, -31, 31);
		} else if (key == L1("BYMONTH")) {
			rule.byMonth = intList(value, 1, 12);
		} else if (key == L1("BYSETPOS")) {
			rule.bySetPos = intList(value, -366, 366);
		} else if (key == L1("WKST")) {
			auto weekday = weekdayFromCode(value);
			if (weekday != 0) rule.weekStart = weekday;
		} else if (key == L1("BYHOUR")) {
			extras.byHour = timeList(value, 23);
		} else if (key == L1("BYMINUTE")) {
			extras.byMinute = timeList(value, 59);
		} else if (key == L1("BYSECOND")) {
			extras.bySecond = timeList(value, 60);
		} else if (key == L1("BYYEARDAY") || key == L1("BYWEEKNO")) {
			rule.supported = false;
		} else if (key == L1("RSCALE")) {
			if (value.compare(L1("GREGORIAN"), Qt::CaseInsensitive) != 0) rule.supported = false;
		} else if (key == L1("SKIP")) {
			if (value.compare(L1("BACKWARD"), Qt::CaseInsensitive) == 0) rule.skip = -1;
			else if (value.compare(L1("FORWARD"), Qt::CaseInsensitive) == 0) rule.skip = 1;
		}
	}

	if (rule.frequency == Frequency::None) rule.supported = false;
	return rule;
}

QTimeZone tryZone(const QString& id) {
	if (id.isEmpty() || id.size() > 128) return {};
	auto bytes = id.toUtf8();
	auto zone = QTimeZone(bytes);
	if (zone.isValid()) return zone;

	auto iana = QTimeZone::windowsIdToDefaultIanaId(bytes);
	if (!iana.isEmpty()) {
		zone = QTimeZone(iana);
		if (zone.isValid()) return zone;
	}
	return {};
}

QTimeZone localZone() {
	auto zone = QTimeZone::systemTimeZone();
	return zone.isValid() ? zone : QTimeZone(QTimeZone::LocalTime);
}

QTimeZone matchOffsets(const ZoneBlock& block, const QTimeZone& local) {
	if (!block.hasStandard && !block.hasDaylight) return {};
	auto standard = block.hasStandard ? block.standardOffset : block.daylightOffset;
	auto daylight = block.hasDaylight ? block.daylightOffset : standard;
	if (daylight == standard) return QTimeZone::fromSecondsAheadOfUtc(standard);

	auto year = QDate::currentDate().year();
	auto january = QDateTime(QDate(year, 1, 15), QTime(12, 0), QTimeZone(QTimeZone::UTC));
	auto july = QDateTime(QDate(year, 7, 15), QTime(12, 0), QTimeZone(QTimeZone::UTC));
	auto matches = [&](const QTimeZone& zone) {
		auto winter = zone.offsetFromUtc(january);
		auto summer = zone.offsetFromUtc(july);
		return (winter == standard && summer == daylight) || (winter == daylight && summer == standard);
	};

	if (matches(local)) return local;
	for (const auto& id: QTimeZone::availableTimeZoneIds(standard)) {
		auto zone = QTimeZone(id);
		if (zone.isValid() && matches(zone)) return zone;
	}
	return QTimeZone::fromSecondsAheadOfUtc(standard);
}

std::optional<int> offsetFromName(const QString& id) {
	static const auto pattern = QRegularExpression(
	    QStringLiteral("^\\(?\\s*(?:UTC|GMT)\\s*(?:([+-])\\s*(\\d{1,2})(?:[:.]?(\\d{2}))?)?\\s*(?:\\)|$)"),
	    QRegularExpression::CaseInsensitiveOption
	);
	auto match = pattern.match(id);
	if (!match.hasMatch()) return std::nullopt;
	if (match.captured(1).isEmpty()) return 0;
	auto sign = match.captured(1) == L1("-") ? -1 : 1;
	auto hours = match.captured(2).toInt();
	auto minutes = match.captured(3).toInt();
	if (hours > 14 || minutes > 59) return std::nullopt;
	return sign * (hours * 3600 + minutes * 60);
}

class ZoneResolver {
public:
	explicit ZoneResolver(const QHash<QString, ZoneBlock>& blocks)
	    : blocks(blocks)
	    , systemZone(localZone()) {}

	[[nodiscard]] const QTimeZone& local() const { return this->systemZone; }

	QTimeZone zone(const QString& tzid) {
		auto cached = this->cache.constFind(tzid);
		if (cached != this->cache.constEnd()) return cached.value();
		auto zone = this->lookup(tzid);
		this->cache.insert(tzid, zone);
		return zone;
	}

private:
	[[nodiscard]] QTimeZone lookup(const QString& tzid) const {
		auto id = tzid.trimmed();
		if (id.isEmpty()) return this->systemZone;

		auto microsoft = L1("tzone://Microsoft/");
		if (id.startsWith(microsoft, Qt::CaseInsensitive)) {
			id = id.mid(microsoft.size());
			if (id.compare(L1("Utc"), Qt::CaseInsensitive) == 0) return QTimeZone(QTimeZone::UTC);
		}

		auto candidates = QStringList {id};
		static const auto numbered = QRegularExpression(QStringLiteral("^(.*\\S)\\s+\\d+$"));
		auto numberedMatch = numbered.match(id);
		if (numberedMatch.hasMatch()) candidates.append(numberedMatch.captured(1));
		auto parts = id.split(u'/', Qt::SkipEmptyParts);
		for (auto keep = std::min<qsizetype>(3, parts.size()); keep >= 1; --keep) {
			candidates.append(parts.mid(parts.size() - keep).join(u'/'));
		}
		candidates.removeDuplicates();

		for (const auto& candidate: candidates) {
			auto zone = tryZone(candidate);
			if (zone.isValid()) return zone;
		}

		auto block = this->blocks.constFind(tzid);
		if (block != this->blocks.constEnd()) {
			if (!block->location.isEmpty()) {
				auto zone = tryZone(block->location);
				if (zone.isValid()) return zone;
			}
			auto zone = matchOffsets(*block, this->systemZone);
			if (zone.isValid()) return zone;
		}

		auto offset = offsetFromName(id);
		if (offset) {
			auto zone = QTimeZone::fromSecondsAheadOfUtc(*offset);
			if (zone.isValid()) return zone;
		}

		return this->systemZone;
	}

	const QHash<QString, ZoneBlock>& blocks;
	QTimeZone systemZone;
	QHash<QString, QTimeZone> cache;
};

QTimeZone zoneOf(const TimeValue& value, ZoneResolver& zones) {
	if (value.utc) return QTimeZone(QTimeZone::UTC);
	if (!value.tzid.isEmpty()) return zones.zone(value.tzid);
	return zones.local();
}

QDateTime resolveTime(const TimeValue& value, const QTimeZone& fallback, ZoneResolver& zones) {
	auto time = value.dateOnly ? QTime(0, 0) : value.time;
	if (value.utc) return QDateTime(value.date, time, QTimeZone(QTimeZone::UTC));
	if (!value.tzid.isEmpty()) return QDateTime(value.date, time, zones.zone(value.tzid));
	return QDateTime(value.date, time, fallback);
}

void addExclusion(Event& event, const TimeValue& value, ZoneResolver& zones) {
	if (!value.isValid()) return;
	if (event.allDay || value.dateOnly) {
		event.excludedDays.insert(value.date.toJulianDay());
		return;
	}
	event.excludedInstants.insert(resolveTime(value, event.zone, zones).toMSecsSinceEpoch());
}

QString recurrenceKey(const TimeValue& value) {
	return QString::number(value.date.toJulianDay()) + u'|'
	     + (value.dateOnly ? QString() : value.time.toString(QStringLiteral("HHmmss")))
	     + (value.utc ? QStringLiteral("Z") : QString()) + u'|' + value.tzid;
}

bool fixedPart(const QList<int>& values, int expected) {
	return values.isEmpty() || (values.size() == 1 && values.first() == expected);
}

void readDuration(const RawEvent& raw, Event& event, ZoneResolver& zones) {
	const auto* endProperty = raw.find(L1("DTEND"));
	const auto* durationProperty = raw.find(L1("DURATION"));

	if (endProperty != nullptr) {
		auto endValue = parseTimeText(endProperty->value, endProperty->params);
		if (event.allDay) {
			auto days = endValue.isValid() ? event.startDate.daysTo(endValue.date) : 1;
			event.durationDays = static_cast<int>(std::clamp<qint64>(days, 1, MAX_SPAN_DAYS));
		} else if (endValue.isValid()) {
			auto end = resolveTime(endValue, event.zone, zones);
			event.durationSecs = std::max<qint64>(0, event.start.secsTo(end));
		}
		return;
	}

	if (durationProperty != nullptr) {
		auto days = 0;
		qint64 secs = 0;
		if (parseDuration(durationProperty->value, days, secs)) {
			if (event.allDay) {
				auto total = qint64(days) + (secs > 0 ? (secs + 86399) / 86400 : 0);
				event.durationDays = static_cast<int>(std::clamp<qint64>(total, 1, MAX_SPAN_DAYS));
			} else if (days >= 0 && secs >= 0) {
				event.durationDays = days;
				event.durationSecs = secs;
			}
			return;
		}
	}

	if (event.allDay) event.durationDays = 1;
}

std::optional<Built> buildEvent(const RawEvent& raw, ZoneResolver& zones) {
	const auto* startProperty = raw.find(L1("DTSTART"));
	if (startProperty == nullptr) return std::nullopt;
	auto startValue = parseTimeText(startProperty->value, startProperty->params);
	if (!startValue.isValid()) return std::nullopt;

	auto built = Built();
	auto& event = built.event;
	event.uid = raw.text(L1("UID")).trimmed();
	event.title = singleLine(unescape(raw.text(L1("SUMMARY"))));
	event.location = singleLine(unescape(raw.text(L1("LOCATION"))));
	event.description = plainText(unescape(raw.text(L1("DESCRIPTION"))));
	built.cancelled =
	    raw.text(L1("STATUS")).trimmed().compare(L1("CANCELLED"), Qt::CaseInsensitive) == 0;
	built.sequence = raw.text(L1("SEQUENCE")).trimmed().toInt();

	event.allDay = startValue.dateOnly;
	event.startDate = startValue.date;
	if (event.allDay) {
		event.zone = zones.local();
	} else {
		event.zone = zoneOf(startValue, zones);
		event.wallTime = startValue.time;
		event.start = QDateTime(startValue.date, startValue.time, event.zone);
	}

	readDuration(raw, event, zones);

	const auto* ruleProperty = raw.find(L1("RRULE"));
	if (ruleProperty != nullptr) {
		auto extras = RuleExtras();
		event.rule = parseRule(ruleProperty->value, extras);
		event.hasRule = true;
		if (extras.until.isValid()) {
			event.rule.hasUntil = true;
			event.rule.untilDate = extras.until.date;
			event.rule.untilDateOnly = extras.until.dateOnly;
			if (!extras.until.dateOnly) event.rule.until = resolveTime(extras.until, event.zone, zones);
		}
		if (event.allDay) {
			if (!extras.byHour.isEmpty() || !extras.byMinute.isEmpty() || !extras.bySecond.isEmpty()) {
				event.rule.supported = false;
			}
		} else if (!fixedPart(extras.byHour, event.wallTime.hour())
		           || !fixedPart(extras.byMinute, event.wallTime.minute())
		           || !fixedPart(extras.bySecond, event.wallTime.second()))
		{
			event.rule.supported = false;
		}
	}

	for (const auto& property: raw.properties) {
		if (property.name == L1("EXDATE")) {
			for (const auto& part: property.value.split(u',', Qt::SkipEmptyParts)) {
				addExclusion(event, parseTimeText(part, property.params), zones);
			}
		} else if (property.name == L1("RDATE")) {
			for (const auto& part: property.value.split(u',', Qt::SkipEmptyParts)) {
				auto value = parseTimeText(part.section(u'/', 0, 0), property.params);
				if (!value.isValid()) continue;
				if (event.allDay) event.extraDates.append(value.date);
				else if (value.dateOnly) event.extraTimes.append(QDateTime(value.date, event.wallTime, event.zone));
				else event.extraTimes.append(resolveTime(value, event.zone, zones));
			}
		}
	}

	const auto* recurrenceProperty = raw.find(L1("RECURRENCE-ID"));
	if (recurrenceProperty != nullptr) {
		built.recurrenceId = parseTimeText(recurrenceProperty->value, recurrenceProperty->params);
		built.isOverride = built.recurrenceId.isValid();
	}

	if (built.isOverride) {
		event.hasRule = false;
		event.extraDates.clear();
		event.extraTimes.clear();
		event.recurring = true;
	} else {
		event.recurring = event.hasRule || !event.extraDates.isEmpty() || !event.extraTimes.isEmpty();
	}

	return built;
}

QDate weekStartOf(QDate date, int weekStart) {
	return date.addDays(-((date.dayOfWeek() - weekStart + 7) % 7));
}

QDate dayInMonth(int year, int month, int day, int skip) {
	auto first = QDate(year, month, 1);
	if (!first.isValid()) return {};
	auto length = first.daysInMonth();
	auto resolved = day < 0 ? day + length + 1 : day;
	if (resolved >= 1 && resolved <= length) return QDate(year, month, resolved);
	if (resolved > length && resolved <= 31) {
		if (skip < 0) return QDate(year, month, length);
		if (skip > 0) return QDate(year, month, length).addDays(1);
	}
	return {};
}

void addWeekdays(QList<QDate>& out, QDate first, QDate last, const WeekdaySpec& spec) {
	auto match = first.addDays((spec.weekday - first.dayOfWeek() + 7) % 7);
	if (match > last) return;
	auto total = match.daysTo(last) / 7 + 1;

	if (spec.ordinal == 0) {
		for (qint64 index = 0; index < total; ++index) out.append(match.addDays(index * 7));
	} else if (spec.ordinal > 0) {
		if (spec.ordinal <= total) out.append(match.addDays(qint64(spec.ordinal - 1) * 7));
	} else if (-spec.ordinal <= total) {
		out.append(match.addDays((total + spec.ordinal) * 7));
	}
}

bool matchesWeekday(QDate date, const QList<WeekdaySpec>& specs, QDate first, QDate last) {
	for (const auto& spec: specs) {
		if (date.dayOfWeek() != spec.weekday) continue;
		if (spec.ordinal == 0) return true;
		if (spec.ordinal > 0 && first.daysTo(date) / 7 + 1 == spec.ordinal) return true;
		if (spec.ordinal < 0 && date.daysTo(last) / 7 + 1 == -spec.ordinal) return true;
	}
	return false;
}

bool matchesMonthDay(QDate date, const QList<int>& days) {
	return std::ranges::any_of(days, [&date](int day) {
		return day > 0 ? date.day() == day : date.day() == date.daysInMonth() + 1 + day;
	});
}

void addMonthDates(const RecurrenceRule& rule, QDate start, int year, int month, QList<QDate>& out) {
	auto first = QDate(year, month, 1);
	if (!first.isValid()) return;
	auto last = QDate(year, month, first.daysInMonth());

	if (!rule.byMonthDay.isEmpty()) {
		for (auto day: rule.byMonthDay) {
			auto date = dayInMonth(year, month, day, rule.skip);
			if (!date.isValid()) continue;
			if (rule.byDay.isEmpty() || matchesWeekday(date, rule.byDay, first, last)) out.append(date);
		}
	} else if (!rule.byDay.isEmpty()) {
		for (const auto& spec: rule.byDay) addWeekdays(out, first, last, spec);
	} else {
		auto date = dayInMonth(year, month, start.day(), rule.skip);
		if (date.isValid()) out.append(date);
	}
}

QDate periodStart(const RecurrenceRule& rule, QDate start, qint64 period) {
	auto step = period * rule.interval;
	switch (rule.frequency) {
	case Frequency::Daily: return start.addDays(step);
	case Frequency::Weekly: return weekStartOf(start, rule.weekStart).addDays(step * 7);
	case Frequency::Monthly: {
		auto index = qint64(start.year()) * 12 + start.month() - 1 + step;
		if (index / 12 > MAX_YEAR) return {};
		return QDate(static_cast<int>(index / 12), static_cast<int>(index % 12) + 1, 1);
	}
	case Frequency::Yearly: {
		auto year = qint64(start.year()) + step;
		if (year > MAX_YEAR) return {};
		return QDate(static_cast<int>(year), 1, 1);
	}
	case Frequency::None: break;
	}
	return {};
}

qint64 firstPeriod(const RecurrenceRule& rule, QDate start, QDate lower) {
	if (lower <= start) return 0;

	qint64 span = 0;
	switch (rule.frequency) {
	case Frequency::Daily: span = start.daysTo(lower); break;
	case Frequency::Weekly: span = weekStartOf(start, rule.weekStart).daysTo(lower) / 7; break;
	case Frequency::Monthly:
		span = (qint64(lower.year()) - start.year()) * 12 + lower.month() - start.month();
		break;
	case Frequency::Yearly: span = qint64(lower.year()) - start.year(); break;
	case Frequency::None: break;
	}
	return std::max<qint64>(0, span / rule.interval - 1);
}

QList<QDate> periodDates(const RecurrenceRule& rule, QDate start, QDate begin) {
	auto dates = QList<QDate>();

	switch (rule.frequency) {
	case Frequency::Daily: {
		if (!rule.byMonth.isEmpty() && !rule.byMonth.contains(begin.month())) break;
		if (!rule.byMonthDay.isEmpty() && !matchesMonthDay(begin, rule.byMonthDay)) break;
		if (!rule.byDay.isEmpty()
		    && std::ranges::none_of(rule.byDay, [&begin](const WeekdaySpec& spec) {
			       return spec.weekday == begin.dayOfWeek();
		       }))
		{
			break;
		}
		dates.append(begin);
		break;
	}
	case Frequency::Weekly: {
		if (rule.byDay.isEmpty()) dates.append(begin.addDays((start.dayOfWeek() - rule.weekStart + 7) % 7));
		for (const auto& spec: rule.byDay) {
			dates.append(begin.addDays((spec.weekday - rule.weekStart + 7) % 7));
		}
		dates.removeIf([&rule](QDate date) {
			return (!rule.byMonth.isEmpty() && !rule.byMonth.contains(date.month()))
			    || (!rule.byMonthDay.isEmpty() && !matchesMonthDay(date, rule.byMonthDay));
		});
		break;
	}
	case Frequency::Monthly:
		if (rule.byMonth.isEmpty() || rule.byMonth.contains(begin.month())) {
			addMonthDates(rule, start, begin.year(), begin.month(), dates);
		}
		break;
	case Frequency::Yearly: {
		auto year = begin.year();
		if (!rule.byMonth.isEmpty()) {
			for (auto month: rule.byMonth) addMonthDates(rule, start, year, month, dates);
		} else if (!rule.byMonthDay.isEmpty()) {
			for (auto month = 1; month <= 12; ++month) addMonthDates(rule, start, year, month, dates);
		} else if (!rule.byDay.isEmpty()) {
			auto first = QDate(year, 1, 1);
			auto last = QDate(year, 12, 31);
			for (const auto& spec: rule.byDay) addWeekdays(dates, first, last, spec);
		} else {
			auto date = dayInMonth(year, start.month(), start.day(), rule.skip);
			if (date.isValid()) dates.append(date);
		}
		break;
	}
	case Frequency::None: break;
	}

	std::sort(dates.begin(), dates.end());
	dates.erase(std::unique(dates.begin(), dates.end()), dates.end());

	if (!rule.bySetPos.isEmpty() && !dates.isEmpty()) {
		auto picked = QList<QDate>();
		auto size = dates.size();
		for (auto position: rule.bySetPos) {
			auto index = position > 0 ? qsizetype(position - 1) : size + position;
			if (index >= 0 && index < size) picked.append(dates.at(index));
		}
		std::sort(picked.begin(), picked.end());
		picked.erase(std::unique(picked.begin(), picked.end()), picked.end());
		dates = picked;
	}

	return dates;
}

template <typename Visit>
bool walkRule(const RecurrenceRule& rule, QDate start, QDate lower, QDate upper, const Visit& visit) {
	auto period = rule.count > 0 ? qint64(0) : firstPeriod(rule, start, lower);
	auto produced = 1;

	for (auto iteration = 0; iteration < MAX_ITERATIONS; ++iteration, ++period) {
		auto begin = periodStart(rule, start, period);
		if (!begin.isValid() || begin > upper || begin.year() > MAX_YEAR) return true;

		for (const auto& date: periodDates(rule, start, begin)) {
			if (date <= start) continue;
			if (date > upper) return true;
			if (rule.count > 0) {
				if (produced >= rule.count) return true;
				++produced;
			}
			if (!visit(date)) return true;
		}
	}

	return false;
}

qint64 dayMs(QDate date) { return date.startOfDay(QTimeZone(QTimeZone::UTC)).toMSecsSinceEpoch(); }

void computeSpan(Event& event) {
	auto first = event.allDay ? dayMs(event.startDate) : event.start.toMSecsSinceEpoch();
	auto last = first;
	for (const auto& date: event.extraDates) {
		first = std::min(first, dayMs(date));
		last = std::max(last, dayMs(date));
	}
	for (const auto& time: event.extraTimes) {
		first = std::min(first, time.toMSecsSinceEpoch());
		last = std::max(last, time.toMSecsSinceEpoch());
	}

	auto lengthMs = qint64(event.durationDays) * DAY_MS + event.durationSecs * 1000;
	event.spanStart = first - SPAN_SLACK_MS;

	if (event.hasRule && event.rule.supported) {
		const auto& rule = event.rule;
		if (rule.count == 0) {
			if (!rule.hasUntil) {
				event.spanEnd = UNBOUNDED;
				return;
			}
			last = std::max(last, dayMs(rule.untilDate.addDays(1)));
		} else {
			auto upper = rule.hasUntil ? rule.untilDate.addDays(1) : QDate(MAX_YEAR, 12, 31);
			auto lastDate = event.startDate;
			auto complete = walkRule(rule, event.startDate, event.startDate, upper, [&lastDate](QDate date) {
				lastDate = date;
				return true;
			});
			if (!complete) {
				auto horizon = periodStart(rule, event.startDate, MAX_ITERATIONS);
				if (!horizon.isValid()) {
					event.spanEnd = UNBOUNDED;
					return;
				}
				lastDate = horizon;
			}
			last = std::max(last, dayMs(lastDate.addDays(1)));
		}
	}

	event.spanEnd = last + lengthMs + SPAN_SLACK_MS;
}

struct Window {
	QDateTime from;
	QDateTime to;
	qint64 fromMs = 0;
	qint64 toMs = 0;
	QTimeZone local;
	QDate fromDate;
	QDate toDate;
	bool toAfterMidnight = false;
};

void expandAllDay(const Event& event, const Window& window, QList<Occurrence>& out) {
	auto hasExtras = !event.extraDates.isEmpty();
	auto seen = QSet<qint64>();

	auto consider = [&](QDate date) {
		auto day = date.toJulianDay();
		if (event.excludedDays.contains(day)) return;
		if (hasExtras) {
			if (seen.contains(day)) return;
			seen.insert(day);
		}

		auto endDate = date.addDays(event.durationDays);
		if (endDate <= window.fromDate) return;
		if (date > window.toDate || (date == window.toDate && !window.toAfterMidnight)) return;

		out.append(
		    Occurrence {
		        .event = &event,
		        .start = QDateTime(date, QTime(0, 0), window.local).toLocalTime(),
		        .end = QDateTime(endDate, QTime(0, 0), window.local).toLocalTime(),
		        .firstDay = date,
		        .lastDay = endDate.addDays(-1),
		    }
		);
	};

	consider(event.startDate);
	if (event.hasRule && event.rule.supported) {
		auto lower = window.fromDate.addDays(-qint64(event.durationDays) - 1);
		walkRule(event.rule, event.startDate, lower, window.toDate, [&](QDate date) {
			if (event.rule.hasUntil && date > event.rule.untilDate) return false;
			consider(date);
			return true;
		});
	}
	for (const auto& date: event.extraDates) consider(date);
}

void expandTimed(const Event& event, const Window& window, QList<Occurrence>& out) {
	auto hasExtras = !event.extraTimes.isEmpty();
	auto seen = QSet<qint64>();

	auto consider = [&](const QDateTime& zoned) {
		auto startMs = zoned.toMSecsSinceEpoch();
		if (event.excludedInstants.contains(startMs)) return;
		if (!event.excludedDays.isEmpty() && event.excludedDays.contains(zoned.date().toJulianDay())) return;
		if (hasExtras) {
			if (seen.contains(startMs)) return;
			seen.insert(startMs);
		}

		auto zonedEnd = event.durationDays != 0 ? zoned.addDays(event.durationDays).addSecs(event.durationSecs)
		                                        : zoned.addSecs(event.durationSecs);
		auto endMs = std::max(startMs, zonedEnd.toMSecsSinceEpoch());
		if (startMs >= window.toMs) return;
		if (endMs <= window.fromMs && !(endMs == startMs && startMs >= window.fromMs)) return;

		auto start = zoned.toLocalTime();
		auto end = endMs == startMs ? start : zonedEnd.toLocalTime();
		auto lastDay = end.date();
		if (endMs == startMs) lastDay = start.date();
		else if (end.time() == QTime(0, 0)) lastDay = lastDay.addDays(-1);
		out.append(
		    Occurrence {.event = &event, .start = start, .end = end, .firstDay = start.date(), .lastDay = lastDay}
		);
	};

	consider(event.start);
	if (event.hasRule && event.rule.supported) {
		auto slackDays = qint64(event.durationDays) + event.durationSecs / 86400 + 2;
		auto lower = window.from.toTimeZone(event.zone).date().addDays(-slackDays);
		auto upper = window.to.toTimeZone(event.zone).date().addDays(1);
		walkRule(event.rule, event.startDate, lower, upper, [&](QDate date) {
			auto zoned = QDateTime(date, event.wallTime, event.zone);
			if (event.rule.hasUntil) {
				auto beyond = event.rule.untilDateOnly ? date > event.rule.untilDate : zoned > event.rule.until;
				if (beyond) return false;
			}
			consider(zoned);
			return true;
		});
	}
	for (const auto& time: event.extraTimes) consider(time.toTimeZone(event.zone));
}

} // namespace

Calendar parse(const QByteArray& data) {
	auto calendar = Calendar();
	auto blocks = QHash<QString, ZoneBlock>();
	auto rawEvents = QList<RawEvent>();
	auto stack = QStringList();
	auto current = RawEvent();
	auto block = ZoneBlock();
	auto blockId = QString();
	auto subStart = QString();
	auto subOffset = std::optional<int>();

	for (const auto& line: unfoldLines(data)) {
		auto name = propertyName(line);
		if (!wanted(name)) continue;

		auto property = Property();
		auto text = QByteArrayView(line).isValidUtf8() ? QString::fromUtf8(line) : QString::fromLatin1(line);
		if (!parseProperty(text, property)) continue;

		if (property.name == L1("BEGIN")) {
			auto component = property.value.trimmed().toUpper();
			if (component == L1("VCALENDAR")) {
				calendar.valid = true;
			} else if (component == L1("VEVENT")) {
				current = RawEvent();
			} else if (component == L1("VTIMEZONE")) {
				block = ZoneBlock();
				blockId.clear();
			} else if (component == L1("STANDARD") || component == L1("DAYLIGHT")) {
				subStart.clear();
				subOffset.reset();
			}
			stack.append(component);
			continue;
		}

		if (property.name == L1("END")) {
			auto component = property.value.trimmed().toUpper();
			auto index = stack.lastIndexOf(component);
			if (index < 0) continue;
			auto parent = index > 0 ? stack.at(index - 1) : QString();

			if (component == L1("VEVENT")) {
				rawEvents.append(std::move(current));
				current = RawEvent();
			} else if (component == L1("VTIMEZONE")) {
				if (!blockId.isEmpty()) blocks.insert(blockId, block);
			} else if ((component == L1("STANDARD") || component == L1("DAYLIGHT"))
			           && parent == L1("VTIMEZONE") && subOffset)
			{
				auto daylight = component == L1("DAYLIGHT");
				auto& start = daylight ? block.daylightStart : block.standardStart;
				auto& has = daylight ? block.hasDaylight : block.hasStandard;
				if (!has || subStart >= start) {
					start = subStart;
					(daylight ? block.daylightOffset : block.standardOffset) = *subOffset;
					has = true;
				}
			}
			stack.resize(index);
			continue;
		}

		if (stack.isEmpty()) continue;
		const auto& top = stack.last();
		if (top == L1("VEVENT")) {
			current.properties.append(std::move(property));
		} else if (top == L1("VCALENDAR")) {
			if ((property.name == L1("X-WR-CALNAME") || property.name == L1("NAME")) && calendar.name.isEmpty())
			{
				calendar.name = singleLine(unescape(property.value));
			}
		} else if (top == L1("VTIMEZONE")) {
			if (property.name == L1("TZID")) blockId = property.value.trimmed();
			else if (property.name == L1("X-LIC-LOCATION")) block.location = property.value.trimmed();
		} else if (top == L1("STANDARD") || top == L1("DAYLIGHT")) {
			if (property.name == L1("TZOFFSETTO")) subOffset = parseOffset(property.value);
			else if (property.name == L1("DTSTART")) subStart = property.value.trimmed();
		}
	}
	if (stack.contains(L1("VEVENT"))) rawEvents.append(std::move(current));

	auto zones = ZoneResolver(blocks);
	auto built = QList<Built>();
	built.reserve(rawEvents.size());
	for (const auto& raw: std::as_const(rawEvents)) {
		auto item = buildEvent(raw, zones);
		if (item) built.append(std::move(*item));
	}
	rawEvents.clear();

	auto keep = QList<bool>(built.size(), true);
	auto masters = QHash<QString, QList<qsizetype>>();
	auto overrides = QHash<QString, qsizetype>();
	for (qsizetype i = 0; i < built.size(); ++i) {
		const auto& item = built.at(i);
		if (item.event.uid.isEmpty()) continue;

		if (item.isOverride) {
			auto key = item.event.uid + u'\n' + recurrenceKey(item.recurrenceId);
			auto existing = overrides.constFind(key);
			if (existing == overrides.constEnd()) {
				overrides.insert(key, i);
			} else if (built.at(existing.value()).sequence <= item.sequence) {
				keep[existing.value()] = false;
				overrides.insert(key, i);
			} else {
				keep[i] = false;
			}
			continue;
		}

		auto& list = masters[item.event.uid];
		if (!list.isEmpty()) {
			auto existingSequence = built.at(list.first()).sequence;
			if (item.sequence < existingSequence) {
				keep[i] = false;
				continue;
			}
			if (item.sequence > existingSequence) {
				for (auto index: std::as_const(list)) keep[index] = false;
				list.clear();
			}
		}
		list.append(i);
	}

	for (qsizetype i = 0; i < built.size(); ++i) {
		if (!keep.at(i) || !built.at(i).isOverride) continue;
		auto found = masters.constFind(built.at(i).event.uid);
		if (found == masters.constEnd()) continue;
		for (auto index: found.value()) {
			if (keep.at(index)) addExclusion(built[index].event, built.at(i).recurrenceId, zones);
		}
	}

	for (qsizetype i = 0; i < built.size(); ++i) {
		if (!keep.at(i) || built.at(i).cancelled) continue;
		auto& event = built[i].event;
		computeSpan(event);
		calendar.events.append(std::move(event));
	}

	return calendar;
}

QList<Occurrence> occurrences(const Calendar& calendar, const QDateTime& from, const QDateTime& to) {
	auto out = QList<Occurrence>();
	if (!from.isValid() || !to.isValid() || to <= from) return out;

	auto window = Window();
	window.from = from;
	window.to = to;
	window.fromMs = from.toMSecsSinceEpoch();
	window.toMs = to.toMSecsSinceEpoch();
	window.local = localZone();
	window.fromDate = from.toTimeZone(window.local).date();
	auto localTo = to.toTimeZone(window.local);
	window.toDate = localTo.date();
	window.toAfterMidnight = localTo > QDateTime(window.toDate, QTime(0, 0), window.local);

	for (const auto& event: calendar.events) {
		if (event.spanEnd < window.fromMs || event.spanStart > window.toMs) continue;
		if (event.allDay) expandAllDay(event, window, out);
		else expandTimed(event, window, out);
	}

	return out;
}

} // namespace qs::windows::sys::ics
