#include "notify_send.hpp"

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qvariant.h>

#include "../../../core/logcat.hpp"

namespace qs::windows::services::notifications {

// NOLINTNEXTLINE(misc-use-internal-linkage)
QS_DECLARE_LOGGING_CATEGORY(logNotifications);

namespace {

struct Option {
	char shortName;
	const char* longName;
	bool takesValue;
};

constexpr Option OPTIONS[] = {
    {'u', "urgency",     true },
    {'t', "expire-time", true },
    {'a', "app-name",    true },
    {'i', "icon",        true },
    {'c', "category",    true },
    {'h', "hint",        true },
    {'A', "action",      true },
    {'r', "replace-id",  true },
    {'e', "transient",   false},
    {'p', "print-id",    false},
    {'w', "wait",        false},
};

const Option* findShort(QChar c) {
	for (const auto& option: OPTIONS) {
		if (c == QLatin1Char(option.shortName)) return &option;
	}
	return nullptr;
}

const Option* findLong(const QString& name) {
	for (const auto& option: OPTIONS) {
		if (name == QLatin1StringView(option.longName)) return &option;
	}
	return nullptr;
}

void addHint(QVariantMap& hints, const QString& spec) {
	auto first = spec.indexOf(':');
	auto second = first == -1 ? -1 : spec.indexOf(':', first + 1);

	if (first == -1 || second == -1) {
		qCWarning(logNotifications) << "notifySend: invalid hint" << spec << "(expected TYPE:NAME:VALUE)";
		return;
	}

	auto type = spec.first(first).toLower();
	auto name = spec.sliced(first + 1, second - first - 1);
	auto value = spec.sliced(second + 1);

	bool ok = true;
	QVariant parsed;

	if (type == "int" || type == "byte") {
		parsed = value.toInt(&ok);
	} else if (type == "double") {
		parsed = value.toDouble(&ok);
	} else if (type == "boolean") {
		auto lower = value.toLower();
		ok = lower == "true" || lower == "false" || lower == "1" || lower == "0";
		parsed = lower == "true" || lower == "1";
	} else if (type == "string" || type == "variant") {
		parsed = value;
	} else {
		ok = false;
	}

	if (!ok || name.isEmpty()) {
		qCWarning(logNotifications) << "notifySend: invalid hint" << spec;
		return;
	}

	hints.insert(name, parsed);
}

void apply(NotifySendRequest& request, const Option& option, const QString& value, int& actionIndex) {
	switch (option.shortName) {
	case 'u': {
		auto level = value.toLower();
		if (level == "low") request.hints.insert("urgency", 0);
		else if (level == "normal") request.hints.insert("urgency", 1);
		else if (level == "critical") request.hints.insert("urgency", 2);
		else qCWarning(logNotifications) << "notifySend: unknown urgency" << value;
		break;
	}
	case 't': {
		bool ok = false;
		auto timeout = value.toInt(&ok);
		if (ok) request.expireTimeout = timeout;
		else qCWarning(logNotifications) << "notifySend: invalid expire time" << value;
		break;
	}
	case 'a': request.appName = value; break;
	case 'i': request.appIcon = value; break;
	case 'c': request.hints.insert("category", value); break;
	case 'h': addHint(request.hints, value); break;
	case 'A': {
		auto eq = value.indexOf('=');
		if (eq > 0) {
			request.actions << value.first(eq) << value.sliced(eq + 1);
		} else {
			request.actions << QString::number(actionIndex) << value;
		}
		actionIndex++;
		break;
	}
	case 'r': {
		bool ok = false;
		auto id = value.toUInt(&ok);
		if (ok) request.replacesId = id;
		else qCWarning(logNotifications) << "notifySend: invalid replace id" << value;
		break;
	}
	case 'e': request.hints.insert("transient", true); break;
	default: break;
	}
}

} // namespace

NotifySendRequest parseNotifySendArgs(const QStringList& args) {
	auto request = NotifySendRequest();
	auto actionIndex = 0;

	for (qsizetype i = 0; i < args.length(); i++) {
		const auto& arg = args.at(i);
		const Option* option = nullptr;
		QString value;
		bool hasValue = false;

		if (arg.startsWith("--") && arg.length() > 2) {
			auto eq = arg.indexOf('=');
			auto name = eq == -1 ? arg.sliced(2) : arg.sliced(2, eq - 2);
			option = findLong(name);
			if (eq != -1) {
				value = arg.sliced(eq + 1);
				hasValue = true;
			}
		} else if (arg.startsWith('-') && arg.length() > 1) {
			option = findShort(arg.at(1));
			if (arg.length() > 2) {
				value = arg.sliced(arg.at(2) == '=' ? 3 : 2);
				hasValue = true;
			}
		}

		if (option == nullptr) {
			qCWarning(logNotifications) << "notifySend: ignoring unknown argument" << arg;
			continue;
		}

		if (option->takesValue && !hasValue) {
			if (i + 1 >= args.length()) {
				qCWarning(logNotifications) << "notifySend: missing value for" << arg;
				break;
			}
			value = args.at(++i);
		}

		apply(request, *option, value, actionIndex);
	}

	return request;
}

} // namespace qs::windows::services::notifications
