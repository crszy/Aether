#include "idle_power.hpp"

#include <algorithm>

#include <qt_windows.h>

#include <powrprof.h>
#include <powersetting.h>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qpointer.h>

#include "../../core/logcat.hpp"
#include "../services/message_window.hpp"

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logIdlePower, "quickshell.windows.idlepower", QtWarningMsg);

// Defined manually (matching the style already used for AC/DC and energy saver GUIDs in
// services/upower/core.cpp) instead of pulling in winnt.h's DEFINE_GUID + INITGUID, which would
// need storage defined in exactly one translation unit.
const GUID kGuidVideoSubgroup =
    {0x7516b95f, 0xf776, 0x4464, {0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99}};
const GUID kGuidVideoPowerdownTimeout =
    {0x3c0bc021, 0xc8a8, 0x4e07, {0xa9, 0x73, 0x6b, 0x14, 0xcb, 0xcb, 0x2b, 0x7e}};
const GUID kGuidSleepSubgroup =
    {0x238c9fa8, 0x0aad, 0x41ed, {0x83, 0xf4, 0x97, 0xbe, 0x24, 0x2c, 0x8f, 0x20}};
const GUID kGuidStandbyTimeout =
    {0x29f6c1db, 0x86da, 0x48c5, {0x9f, 0xdb, 0xf2, 0xb6, 0x7b, 0x1f, 0x44, 0xda}};

int secondsToMinutes(DWORD seconds) {
	return static_cast<int>((seconds + 59) / 60);
}

DWORD minutesToSeconds(int minutes) {
	return static_cast<DWORD>(std::max(0, minutes)) * 60;
}

bool readActiveScheme(GUID& out) {
	GUID* active = nullptr;
	if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || active == nullptr) return false;
	out = *active;
	LocalFree(active);
	return true;
}

bool readAc(const GUID& scheme, const GUID& subgroup, const GUID& setting, DWORD& out) {
	return PowerReadACValueIndex(nullptr, &scheme, &subgroup, &setting, &out) == ERROR_SUCCESS;
}

bool readDc(const GUID& scheme, const GUID& subgroup, const GUID& setting, DWORD& out) {
	return PowerReadDCValueIndex(nullptr, &scheme, &subgroup, &setting, &out) == ERROR_SUCCESS;
}

bool writeAc(const GUID& scheme, const GUID& subgroup, const GUID& setting, DWORD value) {
	auto status = PowerWriteACValueIndex(nullptr, &scheme, &subgroup, &setting, value);
	if (status != ERROR_SUCCESS) {
		qCWarning(logIdlePower) << "PowerWriteACValueIndex failed:" << status;
		return false;
	}
	return true;
}

bool writeDc(const GUID& scheme, const GUID& subgroup, const GUID& setting, DWORD value) {
	auto status = PowerWriteDCValueIndex(nullptr, &scheme, &subgroup, &setting, value);
	if (status != ERROR_SUCCESS) {
		qCWarning(logIdlePower) << "PowerWriteDCValueIndex failed:" << status;
		return false;
	}
	return true;
}

// Re-applies the active scheme so the written AC/DC values take effect immediately (this is the
// same trick `powercfg /change` relies on), without switching the user to a different plan.
bool reapplyActiveScheme(const GUID& scheme) {
	auto status = PowerSetActiveScheme(nullptr, &scheme);
	if (status != ERROR_SUCCESS) {
		qCWarning(logIdlePower) << "PowerSetActiveScheme failed:" << status;
		return false;
	}
	return true;
}

} // namespace

IdlePower::IdlePower(QObject* parent): QObject(parent) {
	this->refresh();

	auto* window = qs::windows::services::ServiceMessageWindow::instance();

	window->addHandler(WM_POWERBROADCAST, [self = QPointer(this)](WPARAM wParam, LPARAM /*lParam*/) {
		if (self && wParam == PBT_POWERSETTINGCHANGE) self->refresh();
	});

	this->mVideoNotify =
	    RegisterPowerSettingNotification(window->hwnd(), &kGuidVideoPowerdownTimeout, DEVICE_NOTIFY_WINDOW_HANDLE);
	this->mStandbyNotify =
	    RegisterPowerSettingNotification(window->hwnd(), &kGuidStandbyTimeout, DEVICE_NOTIFY_WINDOW_HANDLE);
}

IdlePower::~IdlePower() {
	if (this->mVideoNotify != nullptr) {
		UnregisterPowerSettingNotification(static_cast<HPOWERNOTIFY>(this->mVideoNotify));
	}
	if (this->mStandbyNotify != nullptr) {
		UnregisterPowerSettingNotification(static_cast<HPOWERNOTIFY>(this->mStandbyNotify));
	}
}

void IdlePower::refresh() {
	this->refreshBatteryPresence();
	this->refreshTimeouts();
}

void IdlePower::refreshBatteryPresence() {
	SYSTEM_POWER_STATUS status {};
	auto present = false;
	if (GetSystemPowerStatus(&status)) {
		present = status.BatteryFlag != 128 && status.BatteryFlag != 255;
	}

	if (present != this->mHasBattery) {
		this->mHasBattery = present;
		emit this->hasBatteryChanged();
	}
}

void IdlePower::refreshTimeouts() {
	GUID scheme {};
	if (!readActiveScheme(scheme)) {
		qCWarning(logIdlePower) << "Could not read the active power scheme";
		return;
	}

	DWORD value = 0;

	if (readAc(scheme, kGuidVideoSubgroup, kGuidVideoPowerdownTimeout, value)) {
		auto minutes = secondsToMinutes(value);
		if (minutes != this->mScreenOffAc) {
			this->mScreenOffAc = minutes;
			emit this->screenOffAcMinutesChanged();
		}
	}

	if (readDc(scheme, kGuidVideoSubgroup, kGuidVideoPowerdownTimeout, value)) {
		auto minutes = secondsToMinutes(value);
		if (minutes != this->mScreenOffDc) {
			this->mScreenOffDc = minutes;
			emit this->screenOffDcMinutesChanged();
		}
	}

	if (readAc(scheme, kGuidSleepSubgroup, kGuidStandbyTimeout, value)) {
		auto minutes = secondsToMinutes(value);
		if (minutes != this->mSleepAc) {
			this->mSleepAc = minutes;
			emit this->sleepAcMinutesChanged();
		}
	}

	if (readDc(scheme, kGuidSleepSubgroup, kGuidStandbyTimeout, value)) {
		auto minutes = secondsToMinutes(value);
		if (minutes != this->mSleepDc) {
			this->mSleepDc = minutes;
			emit this->sleepDcMinutesChanged();
		}
	}
}

void IdlePower::setScreenOffAcMinutes(int minutes) {
	if (minutes == this->mScreenOffAc) return;

	GUID scheme {};
	auto ok = readActiveScheme(scheme)
	       && writeAc(scheme, kGuidVideoSubgroup, kGuidVideoPowerdownTimeout, minutesToSeconds(minutes))
	       && reapplyActiveScheme(scheme);

	if (ok) {
		this->mScreenOffAc = minutes;
		emit this->screenOffAcMinutesChanged();
	}

	if (ok != this->mLastWriteOk) {
		this->mLastWriteOk = ok;
		emit this->lastWriteOkChanged();
	}
}

void IdlePower::setScreenOffDcMinutes(int minutes) {
	if (minutes == this->mScreenOffDc) return;

	GUID scheme {};
	auto ok = readActiveScheme(scheme)
	       && writeDc(scheme, kGuidVideoSubgroup, kGuidVideoPowerdownTimeout, minutesToSeconds(minutes))
	       && reapplyActiveScheme(scheme);

	if (ok) {
		this->mScreenOffDc = minutes;
		emit this->screenOffDcMinutesChanged();
	}

	if (ok != this->mLastWriteOk) {
		this->mLastWriteOk = ok;
		emit this->lastWriteOkChanged();
	}
}

void IdlePower::setSleepAcMinutes(int minutes) {
	if (minutes == this->mSleepAc) return;

	GUID scheme {};
	auto ok = readActiveScheme(scheme)
	       && writeAc(scheme, kGuidSleepSubgroup, kGuidStandbyTimeout, minutesToSeconds(minutes))
	       && reapplyActiveScheme(scheme);

	if (ok) {
		this->mSleepAc = minutes;
		emit this->sleepAcMinutesChanged();
	}

	if (ok != this->mLastWriteOk) {
		this->mLastWriteOk = ok;
		emit this->lastWriteOkChanged();
	}
}

void IdlePower::setSleepDcMinutes(int minutes) {
	if (minutes == this->mSleepDc) return;

	GUID scheme {};
	auto ok = readActiveScheme(scheme)
	       && writeDc(scheme, kGuidSleepSubgroup, kGuidStandbyTimeout, minutesToSeconds(minutes))
	       && reapplyActiveScheme(scheme);

	if (ok) {
		this->mSleepDc = minutes;
		emit this->sleepDcMinutesChanged();
	}

	if (ok != this->mLastWriteOk) {
		this->mLastWriteOk = ok;
		emit this->lastWriteOkChanged();
	}
}

qint64 IdlePower::idleSeconds() {
	LASTINPUTINFO info {};
	info.cbSize = sizeof(info);
	if (!GetLastInputInfo(&info)) return -1;

	// DWORD is unsigned 32-bit, so this subtraction wraps correctly (mod 2^32) across the ~49.7
	// day GetTickCount() rollover without special-casing it.
	DWORD elapsedMs = GetTickCount() - info.dwTime;
	return static_cast<qint64>(elapsedMs / 1000);
}

} // namespace qs::windows::sys
