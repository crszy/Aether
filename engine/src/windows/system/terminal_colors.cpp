#include "terminal_colors.hpp"

#include <limits>
#include <optional>

#include <qcolor.h>
#include <qmetatype.h>
#include <qstring.h>
#include <qvariant.h>

#include "terminal_colors_math.hpp"

namespace qs::windows::sys {

namespace {

using terminal_colors::Argb;

std::optional<Argb> parseColor(const QColor& color) {
	if (!color.isValid()) return std::nullopt;
	return static_cast<Argb>(color.rgb()) | 0xFF000000u;
}

std::optional<Argb> parseColor(const QString& color) {
	return parseColor(QColor::fromString(color));
}

std::optional<Argb> parseColor(const QVariant& color) {
	if (color.metaType() == QMetaType::fromType<QColor>()) return parseColor(color.value<QColor>());
	if (color.metaType() == QMetaType::fromType<QString>()) return parseColor(color.toString());
	return std::nullopt;
}

QString formatColor(Argb color) {
	return QString::asprintf(
	    "#%02X%02X%02X",
	    static_cast<unsigned>((color >> 16) & 0xFF),
	    static_cast<unsigned>((color >> 8) & 0xFF),
	    static_cast<unsigned>(color & 0xFF)
	);
}

} // namespace

QVariantMap TerminalColors::generate(
    const QVariantMap& baseScheme,
    const QString& primaryKeyColor,
    const QString& surfaceContainerLow,
    const QString& onSurface,
    bool darkMode,
    double harmony,
    double harmonizeThreshold,
    double fgBoost,
    bool monochrome
) {
	auto inputs = terminal_colors::TerminalInputs {
	    .primaryKeyColor = parseColor(primaryKeyColor),
	    .surfaceContainerLow = parseColor(surfaceContainerLow),
	    .onSurface = parseColor(onSurface),
	    .darkMode = darkMode,
	    .harmony = harmony,
	    .harmonizeThreshold = harmonizeThreshold,
	    .fgBoost = fgBoost,
	    .monochrome = monochrome,
	};

	auto result = QVariantMap();
	for (auto it = baseScheme.cbegin(); it != baseScheme.cend(); ++it) {
		auto name = it.key().toStdString();
		auto color = terminal_colors::terminalColor(name, parseColor(it.value()), inputs);
		result.insert(it.key(), color ? QVariant(formatColor(*color)) : it.value());
	}

	return result;
}

double TerminalColors::hue(const QString& color) {
	auto argb = parseColor(color);
	if (!argb) return std::numeric_limits<double>::quiet_NaN();
	return terminal_colors::hctHue(*argb);
}

} // namespace qs::windows::sys
