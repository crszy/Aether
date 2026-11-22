#include "focus_grab.hpp"
#include <utility>

#include <qt_windows.h>

#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpoint.h>
#include <qpointer.h>
#include <qquickwindow.h>
#include <qtypes.h>

#include "../window/proxywindow.hpp"
#include "input_mask.hpp"
#include "util.hpp"
#include "window_tracker.hpp"

namespace qs::windows {

namespace {

Q_LOGGING_CATEGORY(logFocusGrab, "quickshell.windows.focusgrab", QtWarningMsg);

bool notBefore(DWORD time, DWORD reference) { return static_cast<LONG>(time - reference) >= 0; }

constexpr DWORD FOREGROUND_GRACE_MS = 500;

class FocusGrabTracker: public QObject {
public:
	static FocusGrabTracker* instance() {
		static QPointer<FocusGrabTracker> tracker; // NOLINT
		if (tracker.isNull()) tracker = new FocusGrabTracker(InputMaskTracker::instance());
		return tracker.data();
	}

	void add(FocusGrab* grab) {
		if (this->grabs.contains(grab)) return;
		this->grabs.append(grab);
		if (this->grabs.size() == 1) this->startWatching();
	}

	void remove(FocusGrab* grab) {
		auto removed = this->grabs.removeIf([grab](const QPointer<FocusGrab>& entry) {
			return entry.isNull() || entry == grab;
		});

		if (removed != 0 && this->grabs.isEmpty()) this->stopWatching();
	}

private:
	explicit FocusGrabTracker(InputMaskTracker* masks): QObject(masks) {
		QObject::connect(masks, &InputMaskTracker::buttonPressed, this, &FocusGrabTracker::onButtonPressed);

		QObject::connect(
		    WindowTracker::instance(),
		    &WindowTracker::foregroundChanged,
		    this,
		    &FocusGrabTracker::onForegroundChanged
		);
	}

	void startWatching() { InputMaskTracker::instance()->acquireButtonEvents(); }
	void stopWatching() { InputMaskTracker::instance()->releaseButtonEvents(); }

	void onButtonPressed(QPoint position, quint32 time) {
		auto* hit = WindowFromPoint(POINT {.x = position.x(), .y = position.y()});
		auto* root = hit == nullptr ? nullptr : GetAncestor(hit, GA_ROOT);

		if (root != nullptr && (GetWindowLongPtrW(root, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0) {
			root = nullptr;
		}

		qCDebug(logFocusGrab) << "Button pressed at" << position << "on" << root;

		auto grabs = this->grabs;
		for (const auto& grab: grabs) {
			if (grab.isNull() || !grab->isActive()) continue;
			if (!notBefore(time, grab->activatedAt())) continue;
			if (!grab->containsWindow(root)) grab->clear();
		}
	}

	void onForegroundChanged(HWND hwnd, quint32 time) {
		if (this->grabs.isEmpty() || hwnd == nullptr || isOwnProcessWindow(hwnd)) return;

		qCDebug(logFocusGrab) << "Foreground moved to another process:" << hwnd;

		auto grabs = this->grabs;
		for (const auto& grab: grabs) {
			if (grab.isNull() || !grab->isActive()) continue;
			if (!notBefore(time, grab->activatedAt() + FOREGROUND_GRACE_MS)) {
				qCDebug(logFocusGrab) << "Ignoring foreground change during the grab's grace period";
				continue;
			}
			grab->clear();
		}
	}

	QList<QPointer<FocusGrab>> grabs;
};

} // namespace

FocusGrab::~FocusGrab() {
	if (this->grabActive) FocusGrabTracker::instance()->remove(this);
}

void FocusGrab::componentComplete() {
	this->complete = true;
	this->tryActivate();
}

void FocusGrab::setActive(bool active) {
	if (active == this->targetActive) return;
	this->targetActive = active;

	if (active) this->tryActivate();
	else this->deactivate();
}

void FocusGrab::setWindows(QObjectList windows) {
	if (windows == this->windowObjects) return;

	for (auto* object: this->windowObjects) {
		if (object == nullptr || windows.contains(object)) continue;
		QObject::disconnect(object, nullptr, this, nullptr);
		if (auto* proxy = ProxyWindowBase::forObject(object)) QObject::disconnect(proxy, nullptr, this, nullptr);
	}

	for (auto it = windows.begin(); it != windows.end();) {
		auto* proxy = ProxyWindowBase::forObject(*it);

		if (proxy == nullptr) {
			it = windows.erase(it);
			continue;
		}

		if (!this->windowObjects.contains(*it)) {
			QObject::connect(*it, &QObject::destroyed, this, &FocusGrab::onObjectDestroyed);
			// clang-format off
			QObject::connect(proxy, &ProxyWindowBase::windowConnected, this, &FocusGrab::onWindowsChanged);
			QObject::connect(proxy, &ProxyWindowBase::backerVisibilityChanged, this, &FocusGrab::onWindowsChanged);
			// clang-format on
		}

		++it;
	}

	this->windowObjects = std::move(windows);
	emit this->windowsChanged();
	this->onWindowsChanged();
}

void FocusGrab::onObjectDestroyed(QObject* object) {
	this->windowObjects.removeAll(object);
	emit this->windowsChanged();
	this->onWindowsChanged();
}

QList<QQuickWindow*> FocusGrab::visibleWindows() const {
	QList<QQuickWindow*> windows;

	for (auto* object: this->windowObjects) {
		auto* proxy = ProxyWindowBase::forObject(object);
		if (proxy == nullptr || !proxy->isVisibleDirect()) continue;

		auto* window = proxy->backingWindow();
		if (window != nullptr && hwndOf(window) != nullptr && !windows.contains(window)) {
			windows.append(window);
		}
	}

	return windows;
}

bool FocusGrab::containsWindow(HWND hwnd) const {
	if (hwnd == nullptr) return false;

	QList<HWND> listed;
	for (auto* window: this->visibleWindows()) listed.append(hwndOf(window));

	for (auto* current = hwnd; current != nullptr; current = GetWindow(current, GW_OWNER)) {
		if (listed.contains(current)) return true;
	}

	return false;
}

void FocusGrab::onWindowsChanged() {
	if (!this->grabActive) {
		this->tryActivate();
		return;
	}

	auto windows = this->visibleWindows();

	if (windows.isEmpty()) {
		this->clear();
		return;
	}

	auto added = false;
	QList<HWND> current;

	for (auto* window: windows) {
		auto* hwnd = hwndOf(window);
		if (!this->knownWindows.contains(hwnd)) added = true;
		current.append(hwnd);
	}

	this->knownWindows = current;
	if (added) this->refocus();
}

void FocusGrab::tryActivate() {
	if (!this->complete || !this->targetActive || this->grabActive) return;

	auto windows = this->visibleWindows();
	if (windows.isEmpty()) return;

	this->grabActive = true;
	this->mActivatedAt = GetTickCount();
	this->knownWindows.clear();
	for (auto* window: windows) this->knownWindows.append(hwndOf(window));

	FocusGrabTracker::instance()->add(this);
	emit this->activeChanged();

	this->refocus();
}

void FocusGrab::deactivate() {
	if (!this->grabActive) return;

	this->grabActive = false;
	this->knownWindows.clear();
	FocusGrabTracker::instance()->remove(this);
	emit this->activeChanged();
}

void FocusGrab::clear() {
	if (!this->grabActive) return;

	this->grabActive = false;
	this->knownWindows.clear();
	FocusGrabTracker::instance()->remove(this);

	emit this->cleared();
	this->targetActive = false;
	emit this->activeChanged();
}

void FocusGrab::refocus() {
	auto* foreground = GetForegroundWindow();
	if (foreground != nullptr && this->containsWindow(GetAncestor(foreground, GA_ROOT))) return;

	for (auto* window: this->visibleWindows()) {
		if (window->flags().testFlag(Qt::WindowDoesNotAcceptFocus)) continue;

		auto* hwnd = hwndOf(window);
		if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0) continue;

		window->requestActivate();
		if (GetForegroundWindow() != hwnd && !forceForegroundWindow(hwnd)) {
			qCDebug(logFocusGrab) << "Could not give keyboard focus to" << window;
		}

		return;
	}
}

} // namespace qs::windows
