#pragma once

#include <qt_windows.h>

#include <qtypes.h>

namespace qs::windows::recorder {

inline qint64 qpc100ns() {
	static const qint64 frequency = [] {
		LARGE_INTEGER value {};
		QueryPerformanceFrequency(&value);
		return static_cast<qint64>(value.QuadPart);
	}();

	LARGE_INTEGER counter {};
	QueryPerformanceCounter(&counter);
	auto ticks = static_cast<qint64>(counter.QuadPart);
	return ticks / frequency * 10'000'000 + ticks % frequency * 10'000'000 / frequency;
}

} // namespace qs::windows::recorder
