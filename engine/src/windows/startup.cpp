#include "startup.hpp"
#include <functional>
#include <utility>

#include <qcoreapplication.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpointer.h>
#include <qquickwindow.h>
#include <qtimer.h>

namespace qs::windows::startup {

namespace {

Q_LOGGING_CATEGORY(logStartupGate, "quickshell.windows.startup", QtWarningMsg);

constexpr int SETTLE_DELAY_MS = 400;
constexpr int FALLBACK_MS = 4000;

struct Pending {
	QPointer<QObject> context;
	std::function<void()> callback;
};

struct State {
	bool settled = false;
	bool armed = false;
	bool frameSeen = false;
	QList<Pending> pending;
};

State& state() {
	static auto* instance = new State();
	return *instance;
}

QPointer<qs::windows::StartupGate>& gateInstance() {
	static QPointer<qs::windows::StartupGate> gate;
	return gate;
}

void runNext() {
	auto& s = state();

	while (!s.pending.isEmpty()) {
		auto item = s.pending.takeFirst();
		if (item.context.isNull()) continue;

		item.callback();
		if (!s.pending.isEmpty()) QTimer::singleShot(0, QCoreApplication::instance(), &runNext);
		return;
	}
}

void settle() {
	auto& s = state();
	if (s.settled) return;

	s.settled = true;
	qCDebug(logStartupGate) << "Startup settled, running" << s.pending.size() << "deferred tasks";
	runNext();

	if (auto* gate = gateInstance().data()) emit gate->settledChanged();
}

void arm() {
	auto& s = state();
	if (s.armed) return;

	s.armed = true;
	QTimer::singleShot(FALLBACK_MS, QCoreApplication::instance(), &settle);
}

} // namespace

void afterFirstFrame(QObject* context, std::function<void()> callback) {
	auto& s = state();

	if (s.settled) {
		QTimer::singleShot(0, context, std::move(callback));
		return;
	}

	s.pending.append({.context = context, .callback = std::move(callback)});
	arm();
}

void watchWindow(QQuickWindow* window) {
	auto& s = state();
	if (s.settled || s.frameSeen || window == nullptr) return;

	arm();

	QObject::connect(
	    window,
	    &QQuickWindow::frameSwapped,
	    QCoreApplication::instance(),
	    []() {
		    auto& s = state();
		    if (s.frameSeen) return;

		    s.frameSeen = true;
		    qCDebug(logStartupGate) << "First panel frame presented";
		    QTimer::singleShot(SETTLE_DELAY_MS, QCoreApplication::instance(), &settle);
	    },
	    static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection)
	);
}

bool settled() { return state().settled; }

} // namespace qs::windows::startup

namespace qs::windows {

StartupGate::StartupGate(QObject* parent): QObject(parent) {
	startup::gateInstance() = this;
}

} // namespace qs::windows
