#include "../util.hpp"
#include "input_settings.hpp"

#include <algorithm>
#include <thread>
#include <utility>
#include <vector>

#include <qt_windows.h>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qpointer.h>
#include <qstring.h>
#include <qstringlist.h>

#include "../../core/logcat.hpp"
#include "../services/message_window.hpp"

#ifndef SPI_GETTOUCHPADPARAMETERS
#define SPI_GETTOUCHPADPARAMETERS 0x00AE
#endif
#ifndef SPI_SETTOUCHPADPARAMETERS
#define SPI_SETTOUCHPADPARAMETERS 0x00AF
#endif

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logInputSettings, "quickshell.windows.inputsettings", QtWarningMsg);

const wchar_t* const KEYBOARD_KEY = L"Control Panel\\Keyboard";
const wchar_t* const PRECISION_TOUCHPAD_KEY =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\PrecisionTouchPad";
const wchar_t* const CURSORS_KEY = L"Control Panel\\Cursors";
const wchar_t* const CURSOR_SCHEMES_KEY = L"Control Panel\\Cursors\\Schemes";
const wchar_t* const SYSTEM_CURSOR_SCHEMES_KEY =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Control Panel\\Cursors\\Schemes";
const wchar_t* const ACCESSIBILITY_KEY = L"Software\\Microsoft\\Accessibility";

const QStringList CURSOR_ROLES_17 = {
    "Arrow",
    "Help",
    "AppStarting",
    "Wait",
    "Crosshair",
    "IBeam",
    "NWPen",
    "No",
    "SizeNS",
    "SizeWE",
    "SizeNWSE",
    "SizeNESW",
    "SizeAll",
    "UpArrow",
    "Hand",
    "Pin",
    "Person",
};

const QStringList CURSOR_ROLES_15 = {
    "Arrow",
    "Help",
    "AppStarting",
    "Wait",
    "Crosshair",
    "IBeam",
    "NWPen",
    "No",
    "SizeNS",
    "SizeWE",
    "SizeNWSE",
    "SizeNESW",
    "SizeAll",
    "UpArrow",
    "Hand",
};

bool readDword(HKEY root, const wchar_t* subkey, const wchar_t* name, DWORD& out) {
	HKEY key = nullptr;
	if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
	DWORD value = 0;
	DWORD size = sizeof(value);
	DWORD type = 0;
	auto ok = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
	RegCloseKey(key);
	if (ok != ERROR_SUCCESS || type != REG_DWORD) return false;
	out = value;
	return true;
}

bool writeDword(HKEY root, const wchar_t* subkey, const wchar_t* name, DWORD value) {
	HKEY key = nullptr;
	if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
	    != ERROR_SUCCESS)
	{
		return false;
	}

	auto ok =
	    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));

	RegCloseKey(key);
	return ok == ERROR_SUCCESS;
}

bool readString(HKEY root, const wchar_t* subkey, const wchar_t* name, QString& out) {
	HKEY key = nullptr;
	if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;

	DWORD size = 0;
	DWORD type = 0;
	auto ok = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
	if (ok != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
		RegCloseKey(key);
		return false;
	}

	std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, 0);
	ok = RegQueryValueExW(
	    key,
	    name,
	    nullptr,
	    &type,
	    reinterpret_cast<BYTE*>(buffer.data()),
	    &size
	);
	RegCloseKey(key);
	if (ok != ERROR_SUCCESS) return false;

	out = QString::fromWCharArray(buffer.data());
	return true;
}

QString expandEnvironment(const QString& value) {
	auto wide = value.toStdWString();
	auto needed = ExpandEnvironmentStringsW(wide.c_str(), nullptr, 0);
	if (needed == 0) return value;
	std::vector<wchar_t> buffer(needed + 1, 0);
	if (ExpandEnvironmentStringsW(wide.c_str(), buffer.data(), needed) == 0) return value;
	return QString::fromWCharArray(buffer.data());
}

QString cursorSchemeLabel(const QString& name) {
	if (name == QStringLiteral("Windows Aero")) return QStringLiteral("Windows Default");
	if (name == QStringLiteral("Windows Aero L")) return QStringLiteral("Windows Default (large)");
	if (name.startsWith(QStringLiteral("Windows Aero XL"))) {
		return QStringLiteral("Windows Default (extra large)");
	}
	return name;
}

QStringList splitScheme(const QString& scheme) {
	auto tokens = scheme.split(QLatin1Char(','));
	for (auto& token: tokens) token = expandEnvironment(token.trimmed()).toLower();
	return tokens;
}

bool writeString(HKEY root, const wchar_t* subkey, const wchar_t* name, const QString& value, DWORD type = REG_SZ) {
	HKEY key = nullptr;
	if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
	    != ERROR_SUCCESS)
	{
		return false;
	}

	auto wvalue = value.toStdWString();
	auto size = static_cast<DWORD>((wvalue.size() + 1) * sizeof(wchar_t));
	auto ok = RegSetValueExW(
	    key,
	    name,
	    0,
	    type,
	    reinterpret_cast<const BYTE*>(wvalue.c_str()),
	    size
	);

	RegCloseKey(key);
	return ok == ERROR_SUCCESS;
}

void broadcastSettingChange(const wchar_t* setting) {
	std::thread([setting] {
		DWORD_PTR result = 0;
		SendMessageTimeoutW(
		    HWND_BROADCAST,
		    WM_SETTINGCHANGE,
		    0,
		    reinterpret_cast<LPARAM>(setting),
		    SMTO_ABORTIFHUNG,
		    2000,
		    &result
		);
	}).detach();
}

bool getTouchpadParameters(TOUCHPAD_PARAMETERS_V1& out) {
	out = {};
	out.versionNumber = TOUCHPAD_PARAMETERS_VERSION_1;
	return SystemParametersInfoW(SPI_GETTOUCHPADPARAMETERS, sizeof(out), &out, 0) != 0;
}

bool setTouchpadParameters(TOUCHPAD_PARAMETERS_V1& params) {
	return systemParametersNoWait(
	           SPI_SETTOUCHPADPARAMETERS,
	           sizeof(params),
	           &params,
	           SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	       )
	    != 0;
}

void toggleNumLockOn() {
	if ((GetKeyState(VK_NUMLOCK) & 1) != 0) return;

	INPUT down {};
	down.type = INPUT_KEYBOARD;
	down.ki.wVk = VK_NUMLOCK;
	down.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;

	INPUT up = down;
	up.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;

	INPUT batch[2] = {down, up};
	SendInput(2, batch, sizeof(INPUT));
}

} // namespace

InputSettings::InputSettings(QObject* parent): QObject(parent) {
	this->refresh();

	auto* window = qs::windows::services::ServiceMessageWindow::instance();
	window->addHandler(WM_SETTINGCHANGE, [self = QPointer(this)](WPARAM wparam, LPARAM lparam) {
		if (!self) return;

		QString name;
		if (lparam != 0) {
			name = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(lparam)); // NOLINT
		}

		self->onSettingChanged(wparam, name);
	});
}

void InputSettings::onSettingChanged(quintptr wparam, const QString& lparam) {
	switch (wparam) {
	case SPI_SETKEYBOARDDELAY:
	case SPI_SETKEYBOARDSPEED:
		this->refreshKeyboard();
		return;
	case SPI_SETMOUSESPEED:
	case SPI_SETMOUSE:
	case SPI_SETMOUSEBUTTONSWAP:
	case SPI_SETMOUSEVANISH:
	case SPI_SETWHEELSCROLLLINES:
	case SPI_SETACTIVEWINDOWTRACKING:
	case SPI_SETACTIVEWNDTRKZORDER:
	case SPI_SETACTIVEWNDTRKTIMEOUT:
		this->refreshMouse();
		return;
	case SPI_SETTOUCHPADPARAMETERS:
		this->refreshTouchpad();
		return;
	case SPI_SETCLIENTAREAANIMATION:
	case SPI_SETANIMATION:
		this->refreshAnimations();
		return;
	default:
		break;
	}

	if (lparam.compare(QStringLiteral("PrecisionTouchPad"), Qt::CaseInsensitive) == 0) {
		this->refreshTouchpad();
	} else if (lparam.compare(QStringLiteral("Control Panel\\Keyboard"), Qt::CaseInsensitive) == 0
	           || lparam.compare(QStringLiteral("Keyboard"), Qt::CaseInsensitive) == 0)
	{
		this->refreshKeyboard();
	} else if (lparam.compare(QStringLiteral("CursorSize"), Qt::CaseInsensitive) == 0) {
		this->refreshCursor();
	}
}

void InputSettings::refresh() {
	this->refreshKeyboard();
	this->refreshMouse();
	this->refreshTouchpad();
	this->refreshCursor();
	this->refreshAnimations();
}

void InputSettings::refreshKeyboard() {
	DWORD delay = 0;
	if (SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delay, 0) != 0) {
		auto value = static_cast<int>(delay);
		if (value != this->mKeyboardRepeatDelay) {
			this->mKeyboardRepeatDelay = value;
			emit this->keyboardRepeatDelayChanged();
		}
	}

	DWORD speed = 0;
	if (SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &speed, 0) != 0) {
		auto value = static_cast<int>(speed);
		if (value != this->mKeyboardRepeatRate) {
			this->mKeyboardRepeatRate = value;
			emit this->keyboardRepeatRateChanged();
		}
	}

	QString indicators;
	auto on = false;
	if (readString(HKEY_CURRENT_USER, KEYBOARD_KEY, L"InitialKeyboardIndicators", indicators)) {
		on = (indicators.toInt() & 2) != 0;
	}
	if (on != this->mNumLockOnAtSignIn) {
		this->mNumLockOnAtSignIn = on;
		emit this->numLockOnAtSignInChanged();
	}
}

void InputSettings::setKeyboardRepeatDelay(int value) {
	auto clamped = std::clamp(value, 0, 3);
	if (clamped == this->mKeyboardRepeatDelay) return;

	if (systemParametersNoWait(
	        SPI_SETKEYBOARDDELAY,
	        static_cast<UINT>(clamped),
	        nullptr,
	        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	    )
	    == 0)
	{
		qCWarning(logInputSettings) << "SPI_SETKEYBOARDDELAY failed:" << GetLastError();
		return;
	}

	this->mKeyboardRepeatDelay = clamped;
	emit this->keyboardRepeatDelayChanged();
}

void InputSettings::setKeyboardRepeatRate(int value) {
	auto clamped = std::clamp(value, 0, 31);
	if (clamped == this->mKeyboardRepeatRate) return;

	if (systemParametersNoWait(
	        SPI_SETKEYBOARDSPEED,
	        static_cast<UINT>(clamped),
	        nullptr,
	        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	    )
	    == 0)
	{
		qCWarning(logInputSettings) << "SPI_SETKEYBOARDSPEED failed:" << GetLastError();
		return;
	}

	this->mKeyboardRepeatRate = clamped;
	emit this->keyboardRepeatRateChanged();
}

void InputSettings::setNumLockOnAtSignIn(bool value) {
	if (value == this->mNumLockOnAtSignIn) return;

	if (!writeString(
	        HKEY_CURRENT_USER,
	        KEYBOARD_KEY,
	        L"InitialKeyboardIndicators",
	        value ? QStringLiteral("2") : QStringLiteral("0")
	    ))
	{
		qCWarning(logInputSettings) << "failed writing InitialKeyboardIndicators";
		return;
	}

	this->mNumLockOnAtSignIn = value;
	emit this->numLockOnAtSignInChanged();

	if (value) toggleNumLockOn();
}

void InputSettings::refreshMouse() {
	DWORD speed = 0;
	if (SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &speed, 0) != 0) {
		auto value = static_cast<int>(speed);
		if (value != this->mPointerSpeed) {
			this->mPointerSpeed = value;
			emit this->pointerSpeedChanged();
		}
	}

	int mouseParams[3] = {0, 0, 0}; // NOLINT
	if (SystemParametersInfoW(SPI_GETMOUSE, 0, mouseParams, 0) != 0) {
		auto enhanced = mouseParams[2] != 0;
		if (enhanced != this->mEnhancePointerPrecision) {
			this->mEnhancePointerPrecision = enhanced;
			emit this->enhancePointerPrecisionChanged();
		}
	}

	{
		auto value = GetSystemMetrics(SM_SWAPBUTTON) != 0;
		if (value != this->mLeftHanded) {
			this->mLeftHanded = value;
			emit this->leftHandedChanged();
		}
	}

	BOOL vanish = FALSE;
	if (SystemParametersInfoW(SPI_GETMOUSEVANISH, 0, &vanish, 0) != 0) {
		auto value = vanish != FALSE;
		if (value != this->mHidePointerWhileTyping) {
			this->mHidePointerWhileTyping = value;
			emit this->hidePointerWhileTypingChanged();
		}
	}

	DWORD wheelLines = 0;
	if (SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheelLines, 0) != 0) {
		auto value = static_cast<int>(wheelLines);
		if (value != this->mWheelScrollLines) {
			this->mWheelScrollLines = value;
			emit this->wheelScrollLinesChanged();
		}
	}

	BOOL tracking = FALSE;
	if (SystemParametersInfoW(SPI_GETACTIVEWINDOWTRACKING, 0, &tracking, 0) != 0) {
		auto value = tracking != FALSE;
		if (value != this->mFocusFollowsMouse) {
			this->mFocusFollowsMouse = value;
			emit this->focusFollowsMouseChanged();
		}
	}

	BOOL raise = FALSE;
	if (SystemParametersInfoW(SPI_GETACTIVEWNDTRKZORDER, 0, &raise, 0) != 0) {
		auto value = raise != FALSE;
		if (value != this->mRaiseOnFocus) {
			this->mRaiseOnFocus = value;
			emit this->raiseOnFocusChanged();
		}
	}

	DWORD trackTimeout = 0;
	if (SystemParametersInfoW(SPI_GETACTIVEWNDTRKTIMEOUT, 0, &trackTimeout, 0) != 0) {
		auto value = static_cast<int>(trackTimeout);
		if (value != this->mFocusFollowsMouseDelayMs) {
			this->mFocusFollowsMouseDelayMs = value;
			emit this->focusFollowsMouseDelayMsChanged();
		}
	}
}

void InputSettings::setPointerSpeed(int value) {
	auto clamped = std::clamp(value, 1, 20);
	if (clamped == this->mPointerSpeed) return;

	if (systemParametersNoWait(
	        SPI_SETMOUSESPEED,
	        0,
	        reinterpret_cast<PVOID>(static_cast<UINT_PTR>(clamped)), // NOLINT
	        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	    )
	    == 0)
	{
		qCWarning(logInputSettings) << "SPI_SETMOUSESPEED failed:" << GetLastError();
		return;
	}

	this->mPointerSpeed = clamped;
	emit this->pointerSpeedChanged();
}

void InputSettings::setEnhancePointerPrecision(bool value) {
	if (value == this->mEnhancePointerPrecision) return;

	int mouseParams[3] = {0, 0, 0}; // NOLINT
	if (value) {
		mouseParams[0] = 6;
		mouseParams[1] = 10;
		mouseParams[2] = 1;
	}

	if (systemParametersNoWait(
	        SPI_SETMOUSE,
	        0,
	        mouseParams,
	        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	    )
	    == 0)
	{
		qCWarning(logInputSettings) << "SPI_SETMOUSE failed:" << GetLastError();
		return;
	}

	this->mEnhancePointerPrecision = value;
	emit this->enhancePointerPrecisionChanged();
}

void InputSettings::setLeftHanded(bool value) {
	if (value == this->mLeftHanded) return;

	if (systemParametersNoWait(
	        SPI_SETMOUSEBUTTONSWAP,
	        static_cast<UINT>(value ? TRUE : FALSE),
	        nullptr,
	        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	    )
	    == 0)
	{
		qCWarning(logInputSettings) << "SPI_SETMOUSEBUTTONSWAP failed:" << GetLastError();
		return;
	}

	this->mLeftHanded = value;
	emit this->leftHandedChanged();
}

void InputSettings::setHidePointerWhileTyping(bool value) {
	if (value == this->mHidePointerWhileTyping) return;

	if (systemParametersNoWait(
	        SPI_SETMOUSEVANISH,
	        0,
	        reinterpret_cast<PVOID>(static_cast<UINT_PTR>(value ? TRUE : FALSE)), // NOLINT
	        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	    )
	    == 0)
	{
		qCWarning(logInputSettings) << "SPI_SETMOUSEVANISH failed:" << GetLastError();
		return;
	}

	this->mHidePointerWhileTyping = value;
	emit this->hidePointerWhileTypingChanged();
}

void InputSettings::setWheelScrollLines(int value) {
	auto clamped = std::clamp(value, 1, 100);
	if (clamped == this->mWheelScrollLines) return;

	if (systemParametersNoWait(
	        SPI_SETWHEELSCROLLLINES,
	        static_cast<UINT>(clamped),
	        nullptr,
	        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	    )
	    == 0)
	{
		qCWarning(logInputSettings) << "SPI_SETWHEELSCROLLLINES failed:" << GetLastError();
		return;
	}

	this->mWheelScrollLines = clamped;
	emit this->wheelScrollLinesChanged();
}

void InputSettings::setFocusFollowsMouse(bool value) {
	if (value == this->mFocusFollowsMouse) return;

	if (systemParametersNoWait(
	        SPI_SETACTIVEWINDOWTRACKING,
	        0,
	        reinterpret_cast<PVOID>(static_cast<UINT_PTR>(value ? TRUE : FALSE)), // NOLINT
	        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	    )
	    == 0)
	{
		qCWarning(logInputSettings) << "SPI_SETACTIVEWINDOWTRACKING failed:" << GetLastError();
		return;
	}

	this->mFocusFollowsMouse = value;
	emit this->focusFollowsMouseChanged();
}

void InputSettings::setRaiseOnFocus(bool value) {
	if (value == this->mRaiseOnFocus) return;

	if (systemParametersNoWait(
	        SPI_SETACTIVEWNDTRKZORDER,
	        0,
	        reinterpret_cast<PVOID>(static_cast<UINT_PTR>(value ? TRUE : FALSE)), // NOLINT
	        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	    )
	    == 0)
	{
		qCWarning(logInputSettings) << "SPI_SETACTIVEWNDTRKZORDER failed:" << GetLastError();
		return;
	}

	this->mRaiseOnFocus = value;
	emit this->raiseOnFocusChanged();
}

void InputSettings::setFocusFollowsMouseDelayMs(int value) {
	auto clamped = std::clamp(value, 0, 5000);
	if (clamped == this->mFocusFollowsMouseDelayMs) return;

	if (systemParametersNoWait(
	        SPI_SETACTIVEWNDTRKTIMEOUT,
	        0,
	        reinterpret_cast<PVOID>(static_cast<UINT_PTR>(clamped)), // NOLINT
	        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	    )
	    == 0)
	{
		qCWarning(logInputSettings) << "SPI_SETACTIVEWNDTRKTIMEOUT failed:" << GetLastError();
		return;
	}

	this->mFocusFollowsMouseDelayMs = clamped;
	emit this->focusFollowsMouseDelayMsChanged();
}

void InputSettings::refreshTouchpad() {
	TOUCHPAD_PARAMETERS_V1 params {};
	auto available = getTouchpadParameters(params) && params.touchpadPresent != FALSE;
	if (available != this->mTouchpadAvailable) {
		this->mTouchpadAvailable = available;
		emit this->touchpadAvailableChanged();
	}

	if (available) {
		auto tap = params.tapEnabled != FALSE;
		if (tap != this->mTouchpadTapToClick) {
			this->mTouchpadTapToClick = tap;
			emit this->touchpadTapToClickChanged();
		}

		auto naturalScroll = params.scrollDirectionReversed != FALSE;
		if (naturalScroll != this->mTouchpadNaturalScroll) {
			this->mTouchpadNaturalScroll = naturalScroll;
			emit this->touchpadNaturalScrollChanged();
		}

		auto sensitivity = static_cast<int>(params.sensitivityLevel);
		if (sensitivity != this->mTouchpadSensitivity) {
			this->mTouchpadSensitivity = sensitivity;
			emit this->touchpadSensitivityChanged();
		}
	}

	DWORD touchpadOn = 1;
	auto enabled = !readDword(HKEY_CURRENT_USER, PRECISION_TOUCHPAD_KEY, L"TouchpadOn", touchpadOn)
	            || touchpadOn != 0;
	if (enabled != this->mTouchpadEnabled) {
		this->mTouchpadEnabled = enabled;
		emit this->touchpadEnabledChanged();
	}
}

void InputSettings::setTouchpadEnabled(bool value) {
	if (value == this->mTouchpadEnabled) return;

	if (!writeDword(HKEY_CURRENT_USER, PRECISION_TOUCHPAD_KEY, L"TouchpadOn", value ? 1 : 0)) {
		qCWarning(logInputSettings) << "failed writing PrecisionTouchPad\\TouchpadOn";
		return;
	}

	this->mTouchpadEnabled = value;
	emit this->touchpadEnabledChanged();

	broadcastSettingChange(L"PrecisionTouchPad");
}

void InputSettings::setTouchpadTapToClick(bool value) {
	if (value == this->mTouchpadTapToClick) return;

	TOUCHPAD_PARAMETERS_V1 params {};
	if (!getTouchpadParameters(params)) {
		qCWarning(logInputSettings) << "SPI_GETTOUCHPADPARAMETERS failed:" << GetLastError();
		return;
	}

	params.tapEnabled = value ? TRUE : FALSE;
	if (!setTouchpadParameters(params)) {
		qCWarning(logInputSettings) << "SPI_SETTOUCHPADPARAMETERS failed:" << GetLastError();
		return;
	}

	this->mTouchpadTapToClick = value;
	emit this->touchpadTapToClickChanged();
}

void InputSettings::setTouchpadNaturalScroll(bool value) {
	if (value == this->mTouchpadNaturalScroll) return;

	TOUCHPAD_PARAMETERS_V1 params {};
	if (!getTouchpadParameters(params)) {
		qCWarning(logInputSettings) << "SPI_GETTOUCHPADPARAMETERS failed:" << GetLastError();
		return;
	}

	params.scrollDirectionReversed = value ? TRUE : FALSE;
	if (!setTouchpadParameters(params)) {
		qCWarning(logInputSettings) << "SPI_SETTOUCHPADPARAMETERS failed:" << GetLastError();
		return;
	}

	this->mTouchpadNaturalScroll = value;
	emit this->touchpadNaturalScrollChanged();
}

void InputSettings::setTouchpadSensitivity(int value) {
	auto clamped = std::clamp(value, 0, 4);
	if (clamped == this->mTouchpadSensitivity) return;

	TOUCHPAD_PARAMETERS_V1 params {};
	if (!getTouchpadParameters(params)) {
		qCWarning(logInputSettings) << "SPI_GETTOUCHPADPARAMETERS failed:" << GetLastError();
		return;
	}

	params.sensitivityLevel = static_cast<TOUCHPAD_SENSITIVITY_LEVEL>(clamped);
	if (!setTouchpadParameters(params)) {
		qCWarning(logInputSettings) << "SPI_SETTOUCHPADPARAMETERS failed:" << GetLastError();
		return;
	}

	this->mTouchpadSensitivity = clamped;
	emit this->touchpadSensitivityChanged();
}

void InputSettings::refreshCursor() {
	QStringList names;
	QStringList schemes;

	for (auto [root, path]: {std::pair {HKEY_CURRENT_USER, CURSOR_SCHEMES_KEY},
	                         std::pair {HKEY_LOCAL_MACHINE, SYSTEM_CURSOR_SCHEMES_KEY}})
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;

		for (DWORD index = 0;; index++) {
			wchar_t nameBuf[256] {};
			DWORD nameSize = 256;
			auto result = RegEnumValueW(
			    key,
			    index,
			    nameBuf,
			    &nameSize,
			    nullptr,
			    nullptr,
			    nullptr,
			    nullptr
			);
			if (result == ERROR_NO_MORE_ITEMS) break;
			if (result != ERROR_SUCCESS) break;

			auto name = QString::fromWCharArray(nameBuf);
			if (name.isEmpty() || names.contains(name)) continue;
			QString scheme;
			readString(root, path, nameBuf, scheme);
			names.append(name);
			schemes.append(scheme);
		}

		RegCloseKey(key);
	}

	QStringList labels;
	for (const auto& name: names) labels.append(cursorSchemeLabel(name));

	if (names != this->mCursorSchemeNames) {
		this->mCursorSchemeNames = names;
		emit this->cursorSchemeNamesChanged();
	}

	if (labels != this->mCursorSchemeLabels) {
		this->mCursorSchemeLabels = labels;
		emit this->cursorSchemeLabelsChanged();
	}

	QString savedName;
	readString(HKEY_CURRENT_USER, CURSORS_KEY, L"", savedName);

	QString currentName;
	if (names.contains(savedName)) currentName = savedName;
	else if (labels.contains(savedName)) currentName = names.at(labels.indexOf(savedName));
	else {
		QStringList current;
		for (const auto& role: CURSOR_ROLES_17) {
			QString path;
			readString(HKEY_CURRENT_USER, CURSORS_KEY, role.toStdWString().c_str(), path);
			current.append(expandEnvironment(path).toLower());
		}

		for (int i = 0; i < names.size(); i++) {
			auto tokens = splitScheme(schemes.at(i));
			if (tokens.size() != CURSOR_ROLES_15.size() && tokens.size() != CURSOR_ROLES_17.size()) continue;
			auto matches = true;
			for (int role = 0; role < tokens.size() && matches; role++) {
				matches = tokens.at(role) == current.at(role);
			}
			if (matches) {
				currentName = names.at(i);
				break;
			}
		}
	}

	if (currentName != this->mCursorSchemeName) {
		this->mCursorSchemeName = currentName;
		emit this->cursorSchemeNameChanged();
	}

	DWORD baseSize = 0;
	int sliderFromBaseSize = 1;
	if (readDword(HKEY_CURRENT_USER, CURSORS_KEY, L"CursorBaseSize", baseSize) && baseSize >= 32) {
		sliderFromBaseSize = std::clamp(static_cast<int>((baseSize - 32) / 16) + 1, 1, 15);
	}

	DWORD accessibilitySize = 0;
	auto value = readDword(HKEY_CURRENT_USER, ACCESSIBILITY_KEY, L"CursorSize", accessibilitySize)
	    ? std::clamp(static_cast<int>(accessibilitySize), 1, 15)
	    : sliderFromBaseSize;

	if (value != this->mCursorSize) {
		this->mCursorSize = value;
		emit this->cursorSizeChanged();
	}
}

bool InputSettings::setCursorScheme(const QString& name) {
	QString scheme;
	DWORD source = 1;
	auto wideName = name.toStdWString();
	if (!readString(HKEY_CURRENT_USER, CURSOR_SCHEMES_KEY, wideName.c_str(), scheme)) {
		source = 2;
		if (!readString(HKEY_LOCAL_MACHINE, SYSTEM_CURSOR_SCHEMES_KEY, wideName.c_str(), scheme)) {
			qCWarning(logInputSettings) << "no cursor scheme named" << name;
			return false;
		}
	}

	auto tokens = scheme.split(QLatin1Char(','));
	for (auto& token: tokens) token = token.trimmed();

	const QStringList* roles = nullptr;
	if (tokens.size() == CURSOR_ROLES_17.size()) roles = &CURSOR_ROLES_17;
	else if (tokens.size() == CURSOR_ROLES_15.size()) roles = &CURSOR_ROLES_15;

	if (roles == nullptr) {
		qCWarning(logInputSettings) << "cursor scheme" << name << "has an unexpected field count:"
		                            << tokens.size();
		return false;
	}

	for (int i = 0; i < roles->size(); i++) {
		auto path = tokens.at(i);
		auto wname = roles->at(i).toStdWString();
		if (!writeString(HKEY_CURRENT_USER, CURSORS_KEY, wname.c_str(), path, REG_EXPAND_SZ)) {
			qCWarning(logInputSettings) << "failed writing cursor role" << roles->at(i);
		}
	}

	writeString(HKEY_CURRENT_USER, CURSORS_KEY, L"", source == 2 ? cursorSchemeLabel(name) : name);
	writeDword(HKEY_CURRENT_USER, CURSORS_KEY, L"Scheme Source", source);

	systemParametersNoWait(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);

	if (name != this->mCursorSchemeName) {
		this->mCursorSchemeName = name;
		emit this->cursorSchemeNameChanged();
	}

	return true;
}

void InputSettings::setCursorSize(int value) {
	auto clamped = std::clamp(value, 1, 15);
	if (clamped == this->mCursorSize) return;

	auto baseSize = static_cast<DWORD>(32 + (clamped - 1) * 16);
	auto ok1 = writeDword(HKEY_CURRENT_USER, CURSORS_KEY, L"CursorBaseSize", baseSize);
	auto ok2 = writeDword(
	    HKEY_CURRENT_USER,
	    ACCESSIBILITY_KEY,
	    L"CursorSize",
	    static_cast<DWORD>(clamped)
	);

	if (!ok1 || !ok2) {
		qCWarning(logInputSettings) << "failed writing cursor size registry values";
	}

	systemParametersNoWait(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
	broadcastSettingChange(L"CursorSize");

	this->mCursorSize = clamped;
	emit this->cursorSizeChanged();
}

void InputSettings::refreshAnimations() {
	BOOL clientArea = FALSE;
	if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &clientArea, 0) != 0) {
		auto value = clientArea != FALSE;
		if (value != this->mClientAreaAnimations) {
			this->mClientAreaAnimations = value;
			emit this->clientAreaAnimationsChanged();
		}
	}

	ANIMATIONINFO info {};
	info.cbSize = sizeof(info);
	if (SystemParametersInfoW(SPI_GETANIMATION, sizeof(info), &info, 0) != 0) {
		auto value = info.iMinAnimate != 0;
		if (value != this->mMinimizeRestoreAnimations) {
			this->mMinimizeRestoreAnimations = value;
			emit this->minimizeRestoreAnimationsChanged();
		}
	}
}

void InputSettings::setClientAreaAnimations(bool value) {
	if (value == this->mClientAreaAnimations) return;

	if (systemParametersNoWait(
	        SPI_SETCLIENTAREAANIMATION,
	        0,
	        reinterpret_cast<PVOID>(static_cast<UINT_PTR>(value ? TRUE : FALSE)), // NOLINT
	        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	    )
	    == 0)
	{
		qCWarning(logInputSettings) << "SPI_SETCLIENTAREAANIMATION failed:" << GetLastError();
		return;
	}

	this->mClientAreaAnimations = value;
	emit this->clientAreaAnimationsChanged();
}

void InputSettings::setMinimizeRestoreAnimations(bool value) {
	if (value == this->mMinimizeRestoreAnimations) return;

	ANIMATIONINFO info {};
	info.cbSize = sizeof(info);
	if (SystemParametersInfoW(SPI_GETANIMATION, sizeof(info), &info, 0) == 0) {
		qCWarning(logInputSettings) << "SPI_GETANIMATION failed:" << GetLastError();
		return;
	}

	info.iMinAnimate = value ? TRUE : FALSE;
	if (systemParametersNoWait(
	        SPI_SETANIMATION,
	        sizeof(info),
	        &info,
	        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE
	    )
	    == 0)
	{
		qCWarning(logInputSettings) << "SPI_SETANIMATION failed:" << GetLastError();
		return;
	}

	this->mMinimizeRestoreAnimations = value;
	emit this->minimizeRestoreAnimationsChanged();
}

} // namespace qs::windows::sys
