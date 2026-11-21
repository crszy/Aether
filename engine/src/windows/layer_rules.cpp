#include "layer_rules.hpp"
#include <algorithm>

#include <qbytearray.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qjsonvalue.h>
#include <qregularexpression.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qvariant.h>

#include "panel_animation.hpp"

namespace qs::windows {

LayerRules LayerRules::parse(const QByteArray& data, const QString& path, QStringList* warnings) {
	auto result = LayerRules();
	if (data.isEmpty()) return result;

	auto warn = [warnings](const QString& message) {
		if (warnings != nullptr) warnings->append(message);
	};

	QJsonParseError parseError {};
	auto document = QJsonDocument::fromJson(data, &parseError);
	if (document.isNull()) {
		warn(QString("Failed to parse %1: %2 at offset %3")
		         .arg(path, parseError.errorString())
		         .arg(parseError.offset));
		return result;
	}

	auto root = document.object();
	result.mValid = true;
	result.mEnabled = root.value("enable").toBool(true);
	result.mLayered = root.value("layered").toBool(false);

	auto list = root.value("rules").toArray();

	for (auto i = 0; i < list.size(); i++) {
		auto object = list.at(i).toObject();
		auto where = QString("%1 rule %2").arg(path).arg(i);

		auto rule = Rule();
		rule.pattern = object.value("namespace").toString();

		if (rule.pattern.isEmpty()) {
			warn(where + ": no namespace");
			continue;
		}

		rule.regex = QRegularExpression(QRegularExpression::anchoredPattern(rule.pattern));
		if (!rule.regex.isValid()) {
			warn(QString("%1: invalid regex %2: %3").arg(where, rule.pattern, rule.regex.errorString()));
			continue;
		}

		if (object.contains("blur")) rule.blur = object.value("blur").toBool();

		if (object.contains("ignoreAlpha")) {
			auto value = object.value("ignoreAlpha");

			if (value.isNull()) {
				rule.ignoreAlpha.emplace();
			} else if (value.isDouble()) {
				rule.ignoreAlpha.emplace(std::clamp(value.toDouble(), 0.0, 1.0));
			} else {
				warn(where + ": ignoreAlpha must be a number or null");
			}
		}

		if (object.contains("animation")) {
			auto value = object.value("animation");
			auto style = value.isString() ? PanelAnimationStyle::parse(value.toString()) : std::nullopt;

			if (value.isNull()) {
				rule.animation.emplace();
			} else if (style.has_value()) {
				rule.animation = *style;
			} else {
				warn(QString("%1: animation must be \"slide\", \"slide top|bottom|left|right\", "
				             "\"popin N%\", \"fade\", \"noAnim\" or null, not %2")
				         .arg(where, value.toVariant().toString()));
			}
		}

		if (object.contains("expressive")) {
			auto value = object.value("expressive");
			auto effect = value.isString() ? parsePanelEffect(value.toString()) : std::nullopt;

			if (value.isNull()) {
				rule.effect = PanelEffect::Auto;
			} else if (effect.has_value()) {
				rule.effect = *effect;
			} else {
				warn(QString("%1: expressive must be \"auto\", \"standard\", \"spring\", \"cascade\", "
				             "\"reveal\", \"blur\", \"dissolve\" or null, not %2")
				         .arg(where, value.toVariant().toString()));
			}
		}

		result.rules.append(rule);
	}

	return result;
}

LayerRule LayerRules::ruleFor(const QString& ns) const {
	auto result = LayerRule();

	for (const auto& rule: this->rules) {
		if (!rule.regex.match(ns).hasMatch()) continue;
		if (rule.blur.has_value()) result.blur = *rule.blur;
		if (rule.ignoreAlpha.has_value()) result.ignoreAlpha = *rule.ignoreAlpha;
		if (rule.animation.has_value()) result.animation = *rule.animation;
		if (rule.effect.has_value()) result.effect = *rule.effect;
	}

	return result;
}

QVariantList LayerRules::info() const {
	QVariantList list;

	for (const auto& rule: this->rules) {
		QVariantMap map;
		map["namespace"] = rule.pattern;
		if (rule.blur.has_value()) map["blur"] = *rule.blur;

		if (rule.ignoreAlpha.has_value()) {
			map["ignoreAlpha"] = rule.ignoreAlpha->has_value() ? QVariant(**rule.ignoreAlpha)
			                                                   : QVariant::fromValue(nullptr);
		}

		if (rule.animation.has_value()) map["animation"] = rule.animation->toString();
		if (rule.effect.has_value()) map["expressive"] = panelEffectName(*rule.effect);

		list.append(map);
	}

	return list;
}

} // namespace qs::windows
