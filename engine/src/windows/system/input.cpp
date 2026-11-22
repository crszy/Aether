#include "input.hpp"

#include <qt_windows.h>

#include <qguiapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qscreen.h>
#include <qstring.h>
#include <qvariant.h>

#include "../../core/logcat.hpp"
#include "../util.hpp"

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logInput, "quickshell.windows.input", QtWarningMsg);

struct ScanCode {
	WORD scan;
	bool extended;
};

bool scanCodeForEvdevKeycode(int keycode, ScanCode& out) {
	switch (keycode) {
	case 96: out = {0x1C, true}; return true;
	case 97: out = {0x1D, true}; return true;
	case 98: out = {0x35, true}; return true;
	case 99: out = {0x37, true}; return true;
	case 100: out = {0x38, true}; return true;
	case 102: out = {0x47, true}; return true;
	case 103: out = {0x48, true}; return true;
	case 104: out = {0x49, true}; return true;
	case 105: out = {0x4B, true}; return true;
	case 106: out = {0x4D, true}; return true;
	case 107: out = {0x4F, true}; return true;
	case 108: out = {0x50, true}; return true;
	case 109: out = {0x51, true}; return true;
	case 110: out = {0x52, true}; return true;
	case 111: out = {0x53, true}; return true;
	case 125: out = {0x5B, true}; return true;
	case 126: out = {0x5C, true}; return true;
	case 127: out = {0x5D, true}; return true;
	default:
		if (keycode >= 1 && keycode <= 88) {
			out = {static_cast<WORD>(keycode), false};
			return true;
		}
		return false;
	}
}

void sendOne(WORD scan, bool extended, bool down) {
	INPUT input {};
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = 0;
	input.ki.wScan = scan;
	input.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0)
	                  | (down ? 0 : KEYEVENTF_KEYUP);

	if (SendInput(1, &input, sizeof(INPUT)) != 1) {
		qCWarning(logInput) << "SendInput failed:" << GetLastError();
	}
}

} // namespace

void Input::sendKey(int keycode, bool down) {
	ScanCode sc {};
	if (!scanCodeForEvdevKeycode(keycode, sc)) {
		qCDebug(logInput) << "sendKey: no scancode mapping for evdev keycode" << keycode;
		return;
	}
	sendOne(sc.scan, sc.extended, down);
}

void Input::sendText(const QString& text) {
	for (auto codeUnit: text) {
		INPUT down {};
		down.type = INPUT_KEYBOARD;
		down.ki.wVk = 0;
		down.ki.wScan = codeUnit.unicode();
		down.ki.dwFlags = KEYEVENTF_UNICODE;

		INPUT up = down;
		up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

		INPUT batch[2] = {down, up};
		SendInput(2, batch, sizeof(INPUT));
	}
}

QVariantMap Input::cursorPosition() {
	POINT point {};
	if (!GetCursorPos(&point)) {
		qCWarning(logInput) << "GetCursorPos failed:" << GetLastError();
		return {};
	}

	auto* monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
	auto rects = monitorRects(monitor);
	if (!rects.valid) return {};

	for (auto* screen: QGuiApplication::screens()) {
		if (monitorForScreen(screen) != monitor) continue;

		auto dpr = screen->devicePixelRatio();
		return {
		    {"screen", screen->name()},
		    {"x", (point.x - rects.monitor.left()) / dpr},
		    {"y", (point.y - rects.monitor.top()) / dpr},
		};
	}

	return {};
}

} // namespace qs::windows::sys
