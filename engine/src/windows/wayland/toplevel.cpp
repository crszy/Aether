#include "toplevel.hpp"

#include <qlist.h>
#include <qobject.h>
#include <qtmetamacros.h>

#include "../../core/model.hpp"
#include "../../core/qmlglobal.hpp"
#include "../../core/qmlscreen.hpp"
#include "../../core/util.hpp"
#include "../window_tracker.hpp"

using namespace qs::windows;

namespace qs::wayland::toplevel {

Toplevel::Toplevel(TrackedWindow* window, QObject* parent): QObject(parent), mWindow(window) {
	// clang-format off
	QObject::connect(window, &TrackedWindow::appIdChanged, this, &Toplevel::appIdChanged);
	QObject::connect(window, &TrackedWindow::titleChanged, this, &Toplevel::titleChanged);
	QObject::connect(window, &TrackedWindow::activatedChanged, this, &Toplevel::activatedChanged);
	QObject::connect(window, &TrackedWindow::screenChanged, this, &Toplevel::screensChanged);
	QObject::connect(window, &TrackedWindow::maximizedChanged, this, &Toplevel::maximizedChanged);
	QObject::connect(window, &TrackedWindow::minimizedChanged, this, &Toplevel::minimizedChanged);
	QObject::connect(window, &TrackedWindow::fullscreenChanged, this, &Toplevel::fullscreenChanged);
	// clang-format on
}

void Toplevel::activate() { this->mWindow->activate(); }
void Toplevel::close() { this->mWindow->close(); }

QString Toplevel::appId() const { return this->mWindow->appId(); }
QString Toplevel::title() const { return this->mWindow->title(); }

Toplevel* Toplevel::parent() const {
	return ToplevelManager::instance()->forWindow(this->mWindow->owner());
}

bool Toplevel::activated() const { return this->mWindow->activated(); }

QList<QuickshellScreenInfo*> Toplevel::screens() const {
	QList<QuickshellScreenInfo*> screens;

	if (auto* screen = this->mWindow->screen()) {
		if (auto* info = QuickshellTracked::instance()->screenInfo(screen)) screens.push_back(info);
	}

	return screens;
}

bool Toplevel::maximized() const { return this->mWindow->maximized(); }
void Toplevel::setMaximized(bool maximized) { this->mWindow->setMaximized(maximized); }

bool Toplevel::minimized() const { return this->mWindow->minimized(); }
void Toplevel::setMinimized(bool minimized) { this->mWindow->setMinimized(minimized); }

bool Toplevel::fullscreen() const { return this->mWindow->fullscreen(); }
void Toplevel::setFullscreen(bool fullscreen) { this->mWindow->setFullscreen(fullscreen); }

void Toplevel::fullscreenOn(QuickshellScreenInfo* screen) {
	this->mWindow->fullscreenOn(screen != nullptr ? screen->screen : nullptr);
}

void Toplevel::setRectangle(QObject* /*window*/, QRect /*rect*/) {}
void Toplevel::unsetRectangle() {}

ToplevelManager::ToplevelManager() {
	auto* tracker = WindowTracker::instance();

	// clang-format off
	QObject::connect(tracker, &WindowTracker::windowAdded, this, &ToplevelManager::onWindowAdded);
	QObject::connect(tracker, &WindowTracker::windowRemoved, this, &ToplevelManager::onWindowRemoved);
	QObject::connect(tracker, &WindowTracker::activeWindowChanged, this, &ToplevelManager::onActiveWindowChanged);
	// clang-format on

	for (auto* window: tracker->windows()) this->onWindowAdded(window);
	this->onActiveWindowChanged();
}

ToplevelManager* ToplevelManager::instance() {
	static auto* instance = new ToplevelManager(); // NOLINT
	return instance;
}

Toplevel* ToplevelManager::forWindow(TrackedWindow* window) const {
	return window == nullptr ? nullptr : this->byWindow.value(window);
}

Toplevel* ToplevelManager::forAddress(quint64 address) const {
	for (auto* toplevel: this->mToplevels.valueList()) {
		if (toplevel->mWindow->address() == address) return toplevel;
	}

	return nullptr;
}

ObjectModel<Toplevel>* ToplevelManager::toplevels() { return &this->mToplevels; }

void ToplevelManager::onWindowAdded(TrackedWindow* window) {
	if (this->byWindow.contains(window)) return;

	auto* toplevel = new Toplevel(window, this);
	this->byWindow.insert(window, toplevel);
	this->mToplevels.insertObject(toplevel);
}

void ToplevelManager::onWindowRemoved(TrackedWindow* window) {
	auto* toplevel = this->byWindow.take(window);
	if (toplevel == nullptr) return;

	if (toplevel == this->mActiveToplevel) this->setActiveToplevel(nullptr);
	this->mToplevels.removeObject(toplevel);
	emit toplevel->closed();
	toplevel->deleteLater();
}

void ToplevelManager::onActiveWindowChanged() {
	this->setActiveToplevel(this->forWindow(WindowTracker::instance()->activeWindow()));
}

DEFINE_MEMBER_GETSET(ToplevelManager, activeToplevel, setActiveToplevel);

ToplevelManagerQml::ToplevelManagerQml(QObject* parent): QObject(parent) {
	QObject::connect(
	    ToplevelManager::instance(),
	    &ToplevelManager::activeToplevelChanged,
	    this,
	    &ToplevelManagerQml::activeToplevelChanged
	);
}

ObjectModel<Toplevel>* ToplevelManagerQml::toplevels() {
	return ToplevelManager::instance()->toplevels();
}

Toplevel* ToplevelManagerQml::activeToplevel() {
	return ToplevelManager::instance()->activeToplevel();
}

} // namespace qs::wayland::toplevel
