#include "keyboard.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include <qcoreapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qpointer.h>
#include <qstring.h>

#include "../../core/logcat.hpp"
#include "../window_tracker.hpp"

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logKeyboard, "quickshell.windows.keyboard", QtWarningMsg);

constexpr int LAYOUT_POLL_MS = 500;

QString localeNameFromHkl(HKL hkl) {
	auto langId = LOWORD(reinterpret_cast<quintptr>(hkl)); // NOLINT
	auto lcid = MAKELCID(langId, SORT_DEFAULT);

	wchar_t buffer[LOCALE_NAME_MAX_LENGTH] {};
	if (LCIDToLocaleName(lcid, buffer, LOCALE_NAME_MAX_LENGTH, 0) == 0) return QString();
	return QString::fromWCharArray(buffer);
}

QString displayNameForLocale(const QString& localeName) {
	if (localeName.isEmpty()) return QString();

	wchar_t buffer[256] {};
	auto wName = localeName.toStdWString();

	if (GetLocaleInfoEx(wName.c_str(), LOCALE_SLOCALIZEDDISPLAYNAME, buffer, 256) > 0) {
		return QString::fromWCharArray(buffer);
	}
	if (GetLocaleInfoEx(wName.c_str(), LOCALE_SENGLISHLANGUAGENAME, buffer, 256) > 0) {
		return QString::fromWCharArray(buffer);
	}
	return localeName;
}

std::vector<HKL> installedLayouts() {
	auto count = GetKeyboardLayoutList(0, nullptr);
	if (count <= 0) return {};

	std::vector<HKL> layouts(static_cast<size_t>(count));
	auto got = GetKeyboardLayoutList(count, layouts.data());
	layouts.resize(static_cast<size_t>(std::max(got, 0)));
	return layouts;
}

HKL foregroundLayout() {
	auto* foreground = GetForegroundWindow();
	if (foreground == nullptr) return nullptr;

	auto threadId = GetWindowThreadProcessId(foreground, nullptr);
	if (threadId == 0) return nullptr;

	return GetKeyboardLayout(threadId);
}

} // namespace

KeyboardLayoutWatcher* KeyboardLayoutWatcher::instance() {
	static QPointer<KeyboardLayoutWatcher> watcher; // NOLINT
	if (watcher.isNull()) watcher = new KeyboardLayoutWatcher(QCoreApplication::instance());
	return watcher.data();
}

KeyboardLayoutWatcher::KeyboardLayoutWatcher(QObject* parent): QObject(parent) {
	this->pollTimer.setInterval(LAYOUT_POLL_MS);
	QObject::connect(&this->pollTimer, &QTimer::timeout, this, &KeyboardLayoutWatcher::check);

	QObject::connect(
	    WindowTracker::instance(),
	    &WindowTracker::foregroundChanged,
	    this,
	    &KeyboardLayoutWatcher::check
	);

	this->mLayouts = installedLayouts();
	this->mCurrent = foregroundLayout();
	if (this->mLayouts.size() > 1) this->pollTimer.start();
}

void KeyboardLayoutWatcher::check() {
	auto layouts = installedLayouts();

	if (layouts != this->mLayouts) {
		this->mLayouts = std::move(layouts);

		if (this->mLayouts.size() > 1) this->pollTimer.start();
		else this->pollTimer.stop();

		qCDebug(logKeyboard) << "Installed keyboard layouts changed:" << this->mLayouts.size();
		emit this->layoutsChanged();
	}

	auto* current = foregroundLayout();
	if (current == nullptr || current == this->mCurrent) return;

	this->mCurrent = current;
	emit this->currentChanged();
}

Keyboard::Keyboard(QObject* parent): QObject(parent) {
	QTimer::singleShot(0, this, &Keyboard::initDeferred);
}

void Keyboard::initDeferred() {
	auto* watcher = KeyboardLayoutWatcher::instance();
	QObject::connect(watcher, &KeyboardLayoutWatcher::currentChanged, this, &Keyboard::refresh);

	QObject::connect(
	    watcher,
	    &KeyboardLayoutWatcher::layoutsChanged,
	    this,
	    &Keyboard::refreshLayoutList
	);

	this->refreshLayoutList();
	this->refresh();
}

Keyboard::~Keyboard() = default;

void Keyboard::setActive(bool active) {
	if (this->mActive == active) return;
	this->mActive = active;
	emit this->activeChanged();

	if (active) this->refresh();
}

void Keyboard::refreshLayoutList() {
	QStringList codes;
	for (auto* hkl: KeyboardLayoutWatcher::instance()->layouts()) {
		auto code = localeNameFromHkl(hkl);
		if (!code.isEmpty() && !codes.contains(code)) codes.append(code);
	}
	this->bLayoutCodes = codes;
}

void Keyboard::refresh() {
	if (!this->mActive) return;

	auto* hkl = KeyboardLayoutWatcher::instance()->current();
	if (hkl == nullptr) return;

	auto code = localeNameFromHkl(hkl);
	if (code.isEmpty() || code == this->bCurrentLayoutCode.value()) return;

	this->bCurrentLayoutCode = code;
	this->bCurrentLayoutName = displayNameForLocale(code);
}

void Keyboard::activateLayout(const QString& code) {
	for (auto* hkl: installedLayouts()) {
		if (localeNameFromHkl(hkl) != code) continue;

		auto* foreground = GetForegroundWindow();
		if (foreground == nullptr) return;

		PostMessageW(
		    foreground,
		    WM_INPUTLANGCHANGEREQUEST,
		    INPUTLANGCHANGE_FORWARD,
		    reinterpret_cast<LPARAM>(hkl) // NOLINT
		);
		return;
	}

	qCWarning(logKeyboard) << "activateLayout: no installed layout matches" << code;
}

} // namespace qs::windows::sys
