#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace qs::windows::sys::terminal_colors {

using Argb = std::uint32_t;

struct TerminalInputs {
	std::optional<Argb> primaryKeyColor;
	std::optional<Argb> surfaceContainerLow;
	std::optional<Argb> onSurface;
	bool darkMode = true;
	double harmony = 0.8;
	double harmonizeThreshold = 100;
	double fgBoost = 0.35;
	bool monochrome = false;
};

Argb harmonize(Argb designColor, Argb sourceColor, double threshold, double harmony);

Argb boostChromaTone(Argb color, double chroma, double tone);

std::optional<Argb>
terminalColor(std::string_view name, std::optional<Argb> base, const TerminalInputs& inputs);

double hctHue(Argb color);

} // namespace qs::windows::sys::terminal_colors
