#include "windowmanager.hpp"
#include <functional>
#include <utility>

#include <qobject.h>

#include "../core/qmlscreen.hpp"
#include "screenprojection.hpp"

namespace qs::wm {

std::function<WindowManager*()> WindowManager::provider;

void WindowManager::setProvider(std::function<WindowManager*()> provider) {
	WindowManager::provider = std::move(provider);
}

WindowManager* WindowManager::instance() {
	// Windows: no ext-workspace backend registers a provider - an empty manager (no windowsets) instead of calling an
	// empty std::function, which threw bad_function_call and killed any config that read WindowManager (Ryoku)
	static auto* instance = WindowManager::provider ? WindowManager::provider() : new WindowManager();
	return instance;
}

ScreenProjection* WindowManager::screenProjection(QuickshellScreenInfo* screen) {
	if (!screen) return nullptr;

	auto* qscreen = screen->screen;
	auto it = this->mScreenProjections.find(qscreen);
	if (it != this->mScreenProjections.end()) {
		return *it;
	}

	auto* projection = new ScreenProjection(qscreen, this);
	this->mScreenProjections.insert(qscreen, projection);

	QObject::connect(qscreen, &QObject::destroyed, this, [this, projection, qscreen]() {
		this->mScreenProjections.remove(qscreen);
		delete projection;
	});

	return projection;
}

} // namespace qs::wm
