#pragma once

#include <qbytearray.h>
#include <qdatetime.h>
#include <qlist.h>
#include <qset.h>
#include <qstring.h>
#include <qtimezone.h>
#include <qtypes.h>

namespace qs::windows::sys::ics {

enum class Frequency : quint8 {
	None,
	Daily,
	Weekly,
	Monthly,
	Yearly,
};

struct WeekdaySpec {
	int ordinal = 0;
	int weekday = 0;
};

struct RecurrenceRule {
	Frequency frequency = Frequency::None;
	int interval = 1;
	int count = 0;
	bool hasUntil = false;
	bool untilDateOnly = false;
	QDate untilDate;
	QDateTime until;
	QList<WeekdaySpec> byDay;
	QList<int> byMonthDay;
	QList<int> byMonth;
	QList<int> bySetPos;
	int weekStart = 1;
	int skip = 0;
	bool supported = true;
};

struct Event {
	QString uid;
	QString title;
	QString location;
	QString description;
	bool allDay = false;
	QDate startDate;
	QTime wallTime;
	QDateTime start;
	QTimeZone zone;
	qint64 durationSecs = 0;
	int durationDays = 0;
	bool recurring = false;
	bool hasRule = false;
	RecurrenceRule rule;
	QList<QDate> extraDates;
	QList<QDateTime> extraTimes;
	QSet<qint64> excludedInstants;
	QSet<qint64> excludedDays;
	qint64 spanStart = 0;
	qint64 spanEnd = 0;
};

struct Calendar {
	QString name;
	QList<Event> events;
	bool valid = false;
};

struct Occurrence {
	const Event* event = nullptr;
	QDateTime start;
	QDateTime end;
	QDate firstDay;
	QDate lastDay;
};

Calendar parse(const QByteArray& data);

QList<Occurrence> occurrences(const Calendar& calendar, const QDateTime& from, const QDateTime& to);

} // namespace qs::windows::sys::ics
