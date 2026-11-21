#include "connection.hpp"

#include <algorithm>
#include <thread>
#include <utility>

#include <qbytearray.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qbytearrayview.h>
#include <qcontainerfwd.h>
#include <qelapsedtimer.h>
#include <qguiapplication.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpointer.h>
#include <qproperty.h>
#include <qscreen.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/model.hpp"
#include "../../core/qmlscreen.hpp"
#include "../hotkeys.hpp"
#include "../input_mask.hpp"
#include "../util.hpp"
#include "../virtual_desktops.hpp"
#include "../window_tracker.hpp"
#include "dispatcher.hpp"
#include "hyprland_toplevel.hpp"
#include "monitor.hpp"
#include "workspace.hpp"

using namespace qs::windows;

namespace qs::hyprland::ipc {

namespace {
Q_LOGGING_CATEGORY(logHyprlandIpc, "quickshell.hyprland.ipc", QtWarningMsg);
Q_LOGGING_CATEGORY(logHyprlandIpcEvents, "quickshell.hyprland.ipc.events", QtWarningMsg);

constexpr int GEOMETRY_EVENT_MS = 500;
constexpr int TITLE_EVENT_MS = 200;
constexpr int IPC_OBJECT_REFRESH_MS = 100;

QByteArray workspaceName(HyprlandWorkspace* workspace) {
	return workspace == nullptr ? QByteArray() : workspace->bindableName().value().toUtf8();
}

QByteArray workspaceId(HyprlandWorkspace* workspace) {
	return workspace == nullptr ? QByteArray("-1")
	                            : QByteArray::number(workspace->bindableId().value());
}

} // namespace

QVector<QString> HyprlandIpcEvent::parse(qint32 argumentCount) const {
	auto args = QVector<QString>();

	for (auto arg: this->parseView(argumentCount)) {
		args.push_back(QString::fromUtf8(arg));
	}

	return args;
}

QVector<QByteArrayView> HyprlandIpcEvent::parseView(qint32 argumentCount) const {
	return HyprlandIpc::parseEventArgs(this->data, static_cast<quint16>(argumentCount));
}

QString HyprlandIpcEvent::nameStr() const { return QString::fromUtf8(this->name); }
QString HyprlandIpcEvent::dataStr() const { return QString::fromUtf8(this->data); }

QVector<QByteArrayView> HyprlandIpc::parseEventArgs(QByteArrayView event, quint16 count) {
	auto args = QVector<QByteArrayView>();

	for (auto i = 0; i < count - 1; i++) {
		auto splitIdx = event.indexOf(',');
		if (splitIdx == -1) break;
		args.push_back(event.sliced(0, splitIdx));
		event = event.sliced(splitIdx + 1);
	}

	if (!event.isEmpty()) {
		args.push_back(event);
	}

	while (args.length() < count) {
		args.push_back(QByteArrayView());
	}

	return args;
}

HyprlandIpc* HyprlandIpc::instance() {
	static HyprlandIpc* instance = nullptr; // NOLINT

	if (instance == nullptr) {
		auto timer = QElapsedTimer();
		timer.start();
		instance = new HyprlandIpc();

		qCInfo(logHyprlandIpc) << "Hyprland compatibility module ready:"
		                       << instance->mMonitors.valueList().length() << "monitors,"
		                       << instance->mWorkspaces.valueList().length() << "workspaces,"
		                       << instance->mToplevels.valueList().length() << "toplevels in"
		                       << timer.elapsed() << "ms";
	}

	return instance;
}

HyprlandIpc::HyprlandIpc()
    : mTracker(WindowTracker::instance())
    , mDesktops(VirtualDesktops::instance())
    , dispatcher(new Dispatcher(this)) {
	this->geometryTimer.setSingleShot(true);
	QObject::connect(&this->geometryTimer, &QTimer::timeout, this, &HyprlandIpc::emitGeometryEvent);

	this->titleTimer.setSingleShot(true);
	this->titleTimer.setInterval(TITLE_EVENT_MS);
	QObject::connect(&this->titleTimer, &QTimer::timeout, this, &HyprlandIpc::emitTitleEvents);

	this->ipcObjectTimer.setSingleShot(true);
	this->ipcObjectTimer.setInterval(IPC_OBJECT_REFRESH_MS);
	QObject::connect(&this->ipcObjectTimer, &QTimer::timeout, this, &HyprlandIpc::flushIpcObjects);

	QObject::connect(this->mTracker, &WindowTracker::moveSizeEnded, this, [this]() {
		this->geometryAfterFlush = true;
	});

	// clang-format off
	QObject::connect(this->mTracker, &WindowTracker::windowAdded, this, &HyprlandIpc::onWindowAdded);
	QObject::connect(this->mTracker, &WindowTracker::windowRemoved, this, &HyprlandIpc::onWindowRemoved);
	QObject::connect(this->mTracker, &WindowTracker::activeWindowChanged, this, &HyprlandIpc::onActiveWindowChanged);
	QObject::connect(this->mTracker, &WindowTracker::flushed, this, &HyprlandIpc::onTrackerFlushed);
	QObject::connect(this->mDesktops, &VirtualDesktops::desktopsChanged, this, &HyprlandIpc::onDesktopsChanged);
	QObject::connect(this->mDesktops, &VirtualDesktops::currentChanged, this, &HyprlandIpc::onCurrentDesktopChanged);
	// clang-format on

	auto* masks = qs::windows::InputMaskTracker::instance();
	masks->acquireButtonEvents();
	masks->acquireMonitorEvents();
	QObject::connect(
	    masks,
	    &qs::windows::InputMaskTracker::cursorMonitorChanged,
	    this,
	    &HyprlandIpc::onCursorMonitorChanged
	);
	QObject::connect(
	    masks,
	    &qs::windows::InputMaskTracker::buttonPressed,
	    this,
	    &HyprlandIpc::onButtonPressed
	);

	QObject::connect(this, &HyprlandIpc::dispatchGlobal, this, [](const QString& name) {
		qs::windows::hotkeys::HotkeyManager::instance()->triggerGlobal(name);
	});

	if (auto* app = qobject_cast<QGuiApplication*>(QGuiApplication::instance())) {
		QObject::connect(app, &QGuiApplication::screenAdded, this, &HyprlandIpc::onScreensChanged);
		QObject::connect(app, &QGuiApplication::screenRemoved, this, &HyprlandIpc::onScreensChanged);
	}

	this->syncMonitors(true);
	this->syncWorkspaces(true);

	for (auto* window: this->mTracker->windows()) this->onWindowAdded(window);
	this->onActiveWindowChanged();

	emit this->connected();

	// the Hyprland window manager, when it runs (see wmMode)
	this->wmReconnectTimer.setSingleShot(true);
	this->wmReconnectTimer.setInterval(2000);
	QObject::connect(&this->wmReconnectTimer, &QTimer::timeout, this, &HyprlandIpc::wmConnect);
	this->wmRefreshTimer.setSingleShot(true);
	this->wmRefreshTimer.setInterval(25);
	QObject::connect(&this->wmRefreshTimer, &QTimer::timeout, this, &HyprlandIpc::wmRefresh);
	this->wmConnect();
}

void HyprlandIpc::dispatch(const QString& request) { this->dispatcher->dispatch(request); }

ObjectModel<HyprlandMonitor>* HyprlandIpc::monitors() { return &this->mMonitors; }
ObjectModel<HyprlandWorkspace>* HyprlandIpc::workspaces() { return &this->mWorkspaces; }
ObjectModel<HyprlandToplevel>* HyprlandIpc::toplevels() { return &this->mToplevels; }

HyprlandMonitor* HyprlandIpc::monitorForScreen(QScreen* screen) const {
	if (screen == nullptr) return nullptr;

	for (auto* monitor: this->mMonitors.valueList()) {
		if (monitor->screen() == screen) return monitor;
	}

	return nullptr;
}

HyprlandMonitor* HyprlandIpc::monitorFor(QuickshellScreenInfo* screen) {
	if (screen == nullptr) return nullptr;
	if (auto* monitor = this->monitorForScreen(screen->screen)) return monitor;

	for (auto* monitor: this->mMonitors.valueList()) {
		if (monitor->bindableName().value() == screen->name()) return monitor;
	}

	return nullptr;
}

HyprlandWorkspace* HyprlandIpc::workspaceById(qint32 id) const {
	const auto& list = this->mAllWorkspaces;
	if (this->mWmMode) {
		// the WM's ids are sparse (1-10 per monitor block)
		auto it = std::ranges::find_if(list, [id](HyprlandWorkspace* w) { return w->bindableId().value() == id; });
		return it == list.end() ? nullptr : *it;
	}
	if (id < 1 || id > list.length()) return nullptr;
	auto* workspace = list[id - 1];
	return workspace->bindableId().value() == id ? workspace : nullptr;
}

HyprlandWorkspace* HyprlandIpc::workspaceByName(const QString& name) const {
	for (auto* workspace: this->mAllWorkspaces) {
		if (workspace->bindableName().value() == name) return workspace;
	}

	return nullptr;
}

HyprlandToplevel* HyprlandIpc::findToplevelByAddress(quint64 address) const {
	for (auto* toplevel: this->mToplevels.valueList()) {
		if (toplevel->address() == address) return toplevel;
	}

	return nullptr;
}

HyprlandToplevel* HyprlandIpc::toplevelForWindow(TrackedWindow* window) const {
	return window == nullptr ? nullptr : this->byWindow.value(window);
}

void HyprlandIpc::refreshMonitors() {
	for (auto* monitor: this->mMonitors.valueList()) monitor->refreshIpcObject();
}

void HyprlandIpc::refreshWorkspaces() {
	for (auto* workspace: this->mAllWorkspaces) workspace->refreshIpcObject();
}

void HyprlandIpc::refreshToplevels() {
	for (auto* toplevel: this->mToplevels.valueList()) toplevel->refreshIpcObject();
}

void HyprlandIpc::scheduleIpcObjectRefresh(HyprlandToplevel* toplevel) {
	this->dirtyIpcObjects.append(toplevel);
	if (!this->ipcObjectTimer.isActive()) this->ipcObjectTimer.start();
}

void HyprlandIpc::flushIpcObjects() {
	this->ipcObjectTimer.stop();
	auto dirty = std::exchange(this->dirtyIpcObjects, {});

	for (const auto& toplevel: dirty) {
		if (toplevel != nullptr) toplevel->flushIpcObject();
	}
}

void HyprlandIpc::scheduleGeometryEvent(bool prompt) {
	if (prompt) {
		if (!this->geometryTimer.isActive() || this->geometryTimer.interval() != 0) {
			this->geometryTimer.start(0);
		}
	} else if (!this->geometryTimer.isActive()) {
		this->geometryTimer.start(GEOMETRY_EVENT_MS);
	}
}

void HyprlandIpc::emitTitleEvents() {
	auto pending = std::exchange(this->pendingTitles, {});

	for (const auto& toplevel: pending) {
		if (toplevel == nullptr || toplevel->window() == nullptr) continue;
		if (!this->byWindow.contains(toplevel->window())) continue;

		auto address = toplevel->addressStr().toUtf8();
		this->emitEvent("windowtitlev2", address + "," + toplevel->window()->title().toUtf8());
		this->emitEvent("windowtitle", address);
	}
}

void HyprlandIpc::emitEvent(const QByteArray& name, const QByteArray& data) {
	this->flushIpcObjects();
	qCDebug(logHyprlandIpcEvents) << "Event" << name << data;
	this->event.name = name;
	this->event.data = data;
	emit this->rawEvent(&this->event);
}

void HyprlandIpc::emitGeometryEvent() { this->emitEvent("windowgeometry", ""); }

void HyprlandIpc::syncMonitors(bool initial) {
	auto screens = QGuiApplication::screens();
	QList<HyprlandMonitor*> ordered;
	QList<HyprlandMonitor*> added;

	for (qsizetype i = 0; i < screens.length(); i++) {
		auto* monitor = this->monitorForScreen(screens[i]);

		if (monitor == nullptr) {
			monitor = new HyprlandMonitor(this, screens[i]);
			added.append(monitor);
		}

		monitor->updateFromScreen(static_cast<qint32>(i));
		ordered.append(monitor);
	}

	QList<HyprlandMonitor*> removed;
	for (auto* monitor: this->mMonitors.valueList()) {
		if (!ordered.contains(monitor)) removed.append(monitor);
	}

	this->mMonitors.diffUpdate(ordered);
	this->bMonitorsVersion = this->bMonitorsVersion.value() + 1;

	for (auto* monitor: removed) {
		if (this->bFocusedMonitor.value() == monitor) this->bFocusedMonitor = nullptr;
		if (!initial) this->emitEvent("monitorremoved", monitor->bindableName().value().toUtf8());
		monitor->deleteLater();
	}

	for (auto* monitor: added) {
		if (initial) continue;
		auto name = monitor->bindableName().value().toUtf8();
		this->emitEvent(
		    "monitoraddedv2",
		    QByteArray::number(monitor->bindableId().value()) + "," + name + ","
		        + monitor->bindableDescription().value().toUtf8()
		);
		this->emitEvent("monitoradded", name);
	}

	if (this->bFocusedMonitor.value() == nullptr && !ordered.isEmpty()) {
		auto* start = this->cursorMonitor();
		if (start == nullptr) start = this->monitorForScreen(QGuiApplication::primaryScreen());
		this->updateFocusedMonitor(start == nullptr ? ordered.first() : start);
	}
}

void HyprlandIpc::syncWorkspaces(bool initial) {
	const auto& desktops = this->mDesktops->desktops();
	auto count = desktops.length();
	auto& list = this->mAllWorkspaces;

	auto nameFor = [&desktops](qsizetype index) {
		const auto& name = desktops[index].name;
		return name.isEmpty() ? QString::number(index + 1) : name;
	};

	QList<HyprlandWorkspace*> removed;
	while (list.length() > count) {
		auto* workspace = list.takeLast();
		if (this->bFocusedWorkspace.value() == workspace) this->bFocusedWorkspace = nullptr;
		removed.append(workspace);
	}

	for (qsizetype i = 0; i < list.length(); i++) {
		auto* workspace = list[i];
		auto name = nameFor(i);

		if (workspace->bindableName().value() != name) {
			workspace->bindableName().setValue(name);

			if (!initial && this->mWorkspaces.valueList().contains(workspace)) {
				this->emitEvent("renameworkspace", workspaceId(workspace) + "," + name.toUtf8());
			}
		}
	}

	for (auto i = list.length(); i < count; i++) {
		auto* workspace = new HyprlandWorkspace(this);
		workspace->updateInitial(static_cast<qint32>(i + 1), nameFor(i));
		list.append(workspace);
	}

	this->bWorkspacesVersion = this->bWorkspacesVersion.value() + 1;
	this->updateFocusedWorkspace();
	this->updateVisibleWorkspaces(initial);

	for (auto* workspace: removed) workspace->deleteLater();
}

void HyprlandIpc::updateVisibleWorkspaces(bool initial) {
	if (this->mWmMode) return; // the WM's j/workspaces is the list (wmApply)
	auto* focused = this->bFocusedWorkspace.value();
	QList<HyprlandWorkspace*> visible;

	for (auto* workspace: this->mAllWorkspaces) {
		if (workspace == focused || !workspace->toplevels()->valueList().isEmpty()) {
			visible.append(workspace);
		}
	}

	const auto& current = this->mWorkspaces.valueList();
	if (visible == current) return;

	QList<HyprlandWorkspace*> gone;
	for (auto* workspace: current) {
		if (!visible.contains(workspace)) gone.append(workspace);
	}

	QList<HyprlandWorkspace*> added;
	for (auto* workspace: visible) {
		if (!current.contains(workspace)) added.append(workspace);
	}

	this->mWorkspaces.diffUpdate(visible);
	if (initial) return;

	for (auto* workspace: gone) {
		auto id = workspaceId(workspace);
		auto name = workspaceName(workspace);
		this->emitEvent("destroyworkspacev2", id + "," + name);
		this->emitEvent("destroyworkspace", name);
	}

	for (auto* workspace: added) {
		this->emitEvent("createworkspacev2", workspaceId(workspace) + "," + workspaceName(workspace));
		this->emitEvent("createworkspace", workspaceName(workspace));
	}
}

void HyprlandIpc::updateFocusedWorkspace() {
	if (this->mWmMode) return;
	const auto& list = this->mAllWorkspaces;
	auto index = this->mDesktops->currentIndex();
	auto* workspace = index >= 0 && index < list.length() ? list[index] : nullptr;
	this->bFocusedWorkspace = workspace;
}

void HyprlandIpc::updateFocusedMonitor(HyprlandMonitor* monitor) {
	if (monitor == nullptr) return;
	qs::windows::setFocusedScreen(monitor->screen());
	if (monitor == this->bFocusedMonitor.value()) return;
	this->bFocusedMonitor = monitor;

	this->emitEvent(
	    "focusedmon",
	    monitor->bindableName().value().toUtf8() + "," + workspaceName(this->bFocusedWorkspace.value())
	);
}

void HyprlandIpc::onWindowAdded(TrackedWindow* window) {
	if (this->byWindow.contains(window)) return;

	auto* toplevel = new HyprlandToplevel(this, window);
	this->byWindow.insert(window, toplevel);
	this->mToplevels.insertObject(toplevel);

	auto address = toplevel->addressStr().toUtf8();

	QObject::connect(window, &TrackedWindow::titleChanged, toplevel, [this, toplevel]() {
		if (!this->pendingTitles.contains(toplevel)) this->pendingTitles.append(toplevel);
		if (!this->titleTimer.isActive()) this->titleTimer.start();
	});

	QObject::connect(toplevel, &HyprlandToplevel::workspaceChanged, this, [this, toplevel, address]() {
		if (this->mWmMode) return; // the WM sends movewindow itself
		auto* workspace = toplevel->bindableWorkspace().value();
		if (workspace == nullptr) return;
		this->emitEvent("movewindowv2", address + "," + workspaceId(workspace) + "," + workspaceName(workspace));
		this->emitEvent("movewindow", address + "," + workspaceName(workspace));
	});

	QObject::connect(window, &TrackedWindow::fullscreenChanged, this, [this, window]() {
		this->emitEvent("fullscreen", window->fullscreen() ? "1" : "0");
	});

	auto geometry = [this]() { this->scheduleGeometryEvent(false); };
	auto promptGeometry = [this]() { this->scheduleGeometryEvent(true); };

	QObject::connect(window, &TrackedWindow::rectChanged, this, geometry);
	QObject::connect(window, &TrackedWindow::minimizedChanged, this, promptGeometry);
	QObject::connect(window, &TrackedWindow::maximizedChanged, this, promptGeometry);
	QObject::connect(window, &TrackedWindow::screenChanged, this, promptGeometry);

	this->emitEvent(
	    "openwindow",
	    address + "," + workspaceName(toplevel->bindableWorkspace().value()) + ","
	        + window->appId().toUtf8() + "," + window->title().toUtf8()
	);

	emit this->toplevelAdded(toplevel);
}

void HyprlandIpc::onWindowRemoved(TrackedWindow* window) {
	auto* toplevel = this->byWindow.take(window);
	if (toplevel == nullptr) return;

	if (this->bActiveToplevel.value() == toplevel) this->bActiveToplevel = nullptr;
	this->mToplevels.removeObject(toplevel);
	toplevel->leaveWorkspace();
	this->pendingTitles.removeAll(toplevel);
	this->dirtyIpcObjects.removeAll(toplevel);

	this->emitEvent("closewindow", toplevel->addressStr().toUtf8());
	toplevel->deleteLater();
}

void HyprlandIpc::onActiveWindowChanged() {
	auto* toplevel = this->toplevelForWindow(this->mTracker->activeWindow());
	if (toplevel == this->bActiveToplevel.value() && toplevel != nullptr) return;

	this->bActiveToplevel = toplevel;

	if (toplevel == nullptr) {
		this->emitEvent("activewindowv2", "");
		this->emitEvent("activewindow", ",");
		return;
	}

	this->emitEvent("activewindowv2", toplevel->addressStr().toUtf8());
	this->emitEvent(
	    "activewindow",
	    toplevel->window()->appId().toUtf8() + "," + toplevel->window()->title().toUtf8()
	);
}

void HyprlandIpc::onTrackerFlushed() {
	if (this->geometryAfterFlush) {
		this->geometryAfterFlush = false;
		this->scheduleGeometryEvent(true);
	}
}

void HyprlandIpc::onDesktopsChanged() {
	if (this->mWmMode) return;
	this->syncWorkspaces(false);
}

void HyprlandIpc::onCurrentDesktopChanged() {
	if (this->mWmMode) return;
	auto* previous = this->bFocusedWorkspace.value();
	this->updateFocusedWorkspace();
	this->updateVisibleWorkspaces();
	auto* workspace = this->bFocusedWorkspace.value();
	if (workspace == nullptr || workspace == previous) return;

	this->emitEvent("workspacev2", workspaceId(workspace) + "," + workspaceName(workspace));
	this->emitEvent("workspace", workspaceName(workspace));

	if (auto* monitor = this->bFocusedMonitor.value()) {
		this->emitEvent(
		    "focusedmon",
		    monitor->bindableName().value().toUtf8() + "," + workspaceName(workspace)
		);
	}
}

void HyprlandIpc::onScreensChanged() {
	qs::windows::InputMaskTracker::instance()->resetCursorMonitor();
	this->syncMonitors(false);
	this->updateFocusedMonitor(this->cursorMonitor());
}

HyprlandMonitor* HyprlandIpc::cursorMonitor() const {
	POINT cursor {};
	if (!GetCursorPos(&cursor)) return nullptr;
	auto* screen = qs::windows::screenForMonitor(MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL));
	return screen == nullptr ? nullptr : this->monitorForScreen(screen);
}

void HyprlandIpc::onCursorMonitorChanged(quintptr monitor) {
	if (this->mWmMode) return; // the WM says which monitor has focus
	auto* screen = qs::windows::screenForMonitor(reinterpret_cast<HMONITOR>(monitor));
	if (screen != nullptr) this->updateFocusedMonitor(this->monitorForScreen(screen));
}

void HyprlandIpc::onButtonPressed(QPoint position, quint32 /*time*/) {
	if (this->mWmMode) return;
	auto point = POINT {.x = position.x(), .y = position.y()};
	auto* monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONULL);
	auto* screen = qs::windows::screenForMonitor(monitor);
	if (screen != nullptr) this->updateFocusedMonitor(this->monitorForScreen(screen));
}

// ------------------------------------------------------------------ Hyprland window manager link (wmMode)

namespace {

Q_LOGGING_CATEGORY(logWmLink, "quickshell.hyprland.wm", QtInfoMsg);

// one request on the WM's request socket (a named pipe), blocking: only ever called off the UI thread
QByteArray wmRequest(const std::wstring& pipe, const QByteArray& request) {
	if (!WaitNamedPipeW(pipe.c_str(), 500)) return {};
	auto* handle = CreateFileW(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (handle == INVALID_HANDLE_VALUE) return {};
	DWORD done = 0;
	WriteFile(handle, request.constData(), static_cast<DWORD>(request.size()), &done, nullptr);
	QByteArray reply;
	char buffer[65536];
	while (ReadFile(handle, buffer, sizeof(buffer), &done, nullptr) && done > 0) reply.append(buffer, done);
	CloseHandle(handle);
	return reply;
}

// the WM's events the shells get as they are (the emulation's own versions are off in wmMode); window events
// (open/close/active/title/fullscreen) keep coming from the emulation, which knows icons and app ids
bool wmForwarded(const QByteArray& name) {
	static const QList<QByteArray> names = {
	    "workspace", "workspacev2", "focusedmon", "focusedmonv2", "createworkspace", "createworkspacev2",
	    "destroyworkspace", "destroyworkspacev2", "movewindow", "movewindowv2", "renameworkspace", "submap",
	};
	return names.contains(name);
}

} // namespace

HyprlandWorkspace* HyprlandIpc::wmWorkspaceForAddress(quint64 address) const {
	auto it = this->wmClientWorkspace.constFind(address);
	return it == this->wmClientWorkspace.constEnd() ? nullptr : this->workspaceById(*it);
}

void HyprlandIpc::wmConnect() {
	const auto sig = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
	const auto runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
	if (sig.isEmpty() || runtime.isEmpty()) return;
	const auto dir = runtime + "/hypr/" + sig;
	this->wmEventPath = dir + "/.socket2.sock";
	this->wmRequestPipe = (QStringLiteral(R"(\\.\pipe\)") + dir + "/.socket.sock").toStdWString();

	if (this->wmSocket == nullptr) {
		this->wmSocket = new QLocalSocket(this);
		QObject::connect(this->wmSocket, &QLocalSocket::connected, this, [this]() {
			qCInfo(logWmLink) << "Connected to the Hyprland window manager at" << this->wmEventPath;
			this->wmRefresh();
		});
		QObject::connect(this->wmSocket, &QLocalSocket::readyRead, this, &HyprlandIpc::wmReadEvents);
		QObject::connect(this->wmSocket, &QLocalSocket::stateChanged, this, [this](QLocalSocket::LocalSocketState state) {
			if (state != QLocalSocket::UnconnectedState) return;
			this->wmLeave();
			this->wmReconnectTimer.start(); // the WM may come up later or restart
		});
	}

	if (this->wmSocket->state() == QLocalSocket::UnconnectedState) {
		this->wmSocket->connectToServer(this->wmEventPath, QLocalSocket::ReadOnly);
	}
}

void HyprlandIpc::wmReadEvents() {
	this->wmBuffer += this->wmSocket->readAll();
	bool any = false;
	for (auto nl = this->wmBuffer.indexOf('\n'); nl >= 0; nl = this->wmBuffer.indexOf('\n')) {
		auto line = this->wmBuffer.left(nl);
		this->wmBuffer.remove(0, nl + 1);
		auto separator = line.indexOf(">>");
		if (separator < 0) continue;
		auto name = line.left(separator);
		// Aether's own events ("aether>>wallpaper next" from a key): to QML at once, nothing to refresh for them
		if (name == "aether") {
			this->emitEvent(name, line.mid(separator + 2));
			continue;
		}
		if (wmForwarded(name)) this->wmPendingEvents.append({name, line.mid(separator + 2)});
		any = true;
	}
	if (any) this->wmRefreshTimer.start();
}

void HyprlandIpc::wmRefresh() {
	if (this->wmRefreshing) {
		this->wmRefreshAgain = true;
		return;
	}
	this->wmRefreshing = true;
	auto pipe = this->wmRequestPipe;
	std::thread([this, pipe]() {
		auto monitors = wmRequest(pipe, "j/monitors");
		auto workspaces = wmRequest(pipe, "j/workspaces");
		auto clients = wmRequest(pipe, "j/clients");
		QMetaObject::invokeMethod(
		    this,
		    [this, monitors, workspaces, clients]() {
			    this->wmRefreshing = false;
			    this->wmApply(monitors, workspaces, clients);
			    if (this->wmRefreshAgain) {
				    this->wmRefreshAgain = false;
				    this->wmRefreshTimer.start();
			    }
		    },
		    Qt::QueuedConnection
		);
	}).detach();
}

void HyprlandIpc::wmApply(const QByteArray& monitors, const QByteArray& workspaces, const QByteArray& clients) {
	const auto mons = QJsonDocument::fromJson(monitors).array();
	const auto spaces = QJsonDocument::fromJson(workspaces).array();
	const auto windows = QJsonDocument::fromJson(clients).array();
	if (mons.isEmpty() || spaces.isEmpty()) return; // no answer: keep what there is

	// the WM's monitors -> ours (Qt's screens, whose names the shells match against) by position, else by order
	QHash<qint32, HyprlandMonitor*> monitorOf;
	QHash<qint32, qint32> activeOf;
	HyprlandMonitor* focused = nullptr;
	qint32 focusedId = -1;
	const auto& ours = this->mMonitors.valueList();
	for (const auto& value: mons) {
		auto m = value.toObject();
		auto id = m["id"].toInt();
		auto x = m["x"].toInt();
		auto y = m["y"].toInt();
		HyprlandMonitor* match = nullptr;
		for (auto* monitor: ours) {
			auto geometry = monitor->screen()->geometry();
			if (geometry.x() == x && geometry.y() == y) {
				match = monitor;
				break;
			}
		}
		if (match == nullptr && id >= 0 && id < ours.length()) match = ours[id];
		monitorOf.insert(id, match);
		activeOf.insert(id, m["activeWorkspace"].toObject()["id"].toInt());
		if (m["focused"].toBool()) {
			focused = match;
			focusedId = id;
		}
	}

	const bool entering = !this->mWmMode;
	const auto previous = this->mAllWorkspaces;
	QList<HyprlandWorkspace*> list;
	for (const auto& value: spaces) {
		auto w = value.toObject();
		auto id = w["id"].toInt();
		auto name = w["name"].toString();
		HyprlandWorkspace* workspace = nullptr;
		if (!entering) {
			auto it = std::ranges::find_if(previous, [id](HyprlandWorkspace* p) { return p->bindableId().value() == id; });
			if (it != previous.end()) workspace = *it;
		}
		if (workspace == nullptr) {
			workspace = new HyprlandWorkspace(this);
			workspace->updateInitial(id, name);
		} else if (workspace->bindableName().value() != name) {
			workspace->bindableName().setValue(name);
		}
		auto monitorId = w["monitorID"].toInt();
		auto active = activeOf.value(monitorId, -1) == id;
		workspace->setWm(monitorOf.value(monitorId), active, active && monitorId == focusedId);
		list.append(workspace);
	}
	std::ranges::sort(list, [](HyprlandWorkspace* a, HyprlandWorkspace* b) {
		return a->bindableId().value() < b->bindableId().value();
	});

	this->wmClientWorkspace.clear();
	for (const auto& value: windows) {
		auto c = value.toObject();
		auto address = c["address"].toString().mid(2).toULongLong(nullptr, 16);
		this->wmClientWorkspace.insert(address, c["workspace"].toObject()["id"].toInt());
	}

	this->mWmMode = true;
	this->mAllWorkspaces = list;
	for (auto it = monitorOf.cbegin(); it != monitorOf.cend(); ++it) {
		if (it.value() != nullptr) it.value()->setWmActiveWorkspace(this->workspaceById(activeOf.value(it.key())));
	}
	if (focused != nullptr && focused != this->bFocusedMonitor.value()) {
		qs::windows::setFocusedScreen(focused->screen());
		this->bFocusedMonitor = focused;
	}
	this->bFocusedWorkspace = focused == nullptr ? nullptr : this->workspaceById(activeOf.value(focusedId));
	this->mWorkspaces.diffUpdate(list);
	this->bWorkspacesVersion = this->bWorkspacesVersion.value() + 1;
	this->bWmVersion = this->bWmVersion.value() + 1; // windows pick their workspace again

	for (auto* old: previous) {
		if (!list.contains(old)) old->deleteLater();
	}
	if (entering) {
		qCInfo(logWmLink) << "Workspaces now come from the Hyprland window manager:" << list.length() << "workspaces,"
		                  << this->wmClientWorkspace.size() << "windows";
	}

	// the WM's own events, now that the state they describe is in place
	auto events = std::exchange(this->wmPendingEvents, {});
	for (const auto& [name, data]: events) this->emitEvent(name, data);
}

void HyprlandIpc::wmLeave() {
	this->wmPendingEvents.clear();
	this->wmBuffer.clear();
	if (!this->mWmMode) return;
	qCInfo(logWmLink) << "The Hyprland window manager went away: back to Windows' own workspaces";
	this->mWmMode = false;
	auto old = std::exchange(this->mAllWorkspaces, {});
	this->wmClientWorkspace.clear();
	for (auto* monitor: this->mMonitors.valueList()) monitor->restoreBindings();
	this->bFocusedWorkspace = nullptr;
	this->syncWorkspaces(false);
	this->bWmVersion = this->bWmVersion.value() + 1;
	for (auto* workspace: old) workspace->deleteLater();
	if (auto* monitor = this->cursorMonitor()) this->updateFocusedMonitor(monitor);
}

} // namespace qs::hyprland::ipc
