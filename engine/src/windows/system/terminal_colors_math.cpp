#include "terminal_colors_math.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

#include "cpp/cam/hct.h"

namespace qs::windows::sys::terminal_colors {

namespace {

using material_color_utilities::Hct;

double sanitizeDegrees(double degrees) {
	double mod = std::fmod(degrees, 360.0);
	if (mod == 0.0) return 0.0;
	if (mod < 0.0) mod += 360.0;
	return mod;
}

double differenceDegrees(double a, double b) {
	return 180.0 - std::fabs(std::fabs(a - b) - 180.0);
}

double rotationDirection(double from, double to) {
	return sanitizeDegrees(to - from) <= 180.0 ? 1.0 : -1.0;
}

Argb opaque(Argb color) { return color | 0xFF000000u; }

} // namespace

Argb harmonize(Argb designColor, Argb sourceColor, double threshold, double harmony) {
	auto from = Hct(opaque(designColor));
	auto to = Hct(opaque(sourceColor));
	auto rotation = std::min(differenceDegrees(from.get_hue(), to.get_hue()) * harmony, threshold);
	auto hue = sanitizeDegrees(
	    from.get_hue() + rotation * rotationDirection(from.get_hue(), to.get_hue())
	);
	return Hct(hue, from.get_chroma(), from.get_tone()).ToInt();
}

Argb boostChromaTone(Argb color, double chroma, double tone) {
	auto hct = Hct(opaque(color));
	return Hct(hct.get_hue(), hct.get_chroma() * chroma, hct.get_tone() * tone).ToInt();
}

std::optional<Argb>
terminalColor(std::string_view name, std::optional<Argb> base, const TerminalInputs& inputs) {
	if (inputs.monochrome) return std::nullopt;

	if (name == "term0") {
		if (!inputs.surfaceContainerLow) return std::nullopt;
		return boostChromaTone(*inputs.surfaceContainerLow, 1.2, 0.95);
	}

	if (name == "term15") {
		if (!inputs.onSurface) return std::nullopt;
		return boostChromaTone(*inputs.onSurface, 3, 1);
	}

	if (!base || !inputs.primaryKeyColor) return std::nullopt;
	auto harmonized =
	    harmonize(*base, *inputs.primaryKeyColor, inputs.harmonizeThreshold, inputs.harmony);
	return boostChromaTone(harmonized, 1, 1 + inputs.fgBoost * (inputs.darkMode ? 1 : -1));
}

double hctHue(Argb color) { return Hct(opaque(color)).get_hue(); }

} // namespace qs::windows::sys::terminal_colors
