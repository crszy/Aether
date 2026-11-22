#pragma once

#include <qhash.h>
#include <qset.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtypes.h>

#include "../window_tracker.hpp"

namespace qs::hyprland::ipc {

class HyprlandIpc;

class Dispatcher {
public:
	explicit Dispatcher(HyprlandIpc* ipc): ipc(ipc) {}

	void dispatch(const QString& request);

private:
	struct LuaCall {
		QString function;
		QString scalar;
		QHash<QString, QString> table;
		QStringList positional;
		bool ok = false;
	};

	static LuaCall parseLua(const QString& request);
	static QString unquote(QString value);

	void dispatchLua(const LuaCall& call);
	void dispatchClassic(const QString& name, const QString& args);

	[[nodiscard]] qsizetype resolveWorkspace(const QString& arg, bool& create) const;
	[[nodiscard]] qs::windows::TrackedWindow* resolveWindow(const QString& arg) const;

	void focusWorkspace(const QString& arg);
	void moveToWorkspace(qs::windows::TrackedWindow* window, const QString& arg, bool follow);
	void fullscreen(qs::windows::TrackedWindow* window, int mode);
	void pin(qs::windows::TrackedWindow* window);
	void kill(qs::windows::TrackedWindow* window);
	void setSubmap(const QString& name);
	void toggleSpecial(const QString& name);
	void cycleNext(bool next, bool onlyTiled, bool onlyFloating);
	void focusMonitor(const QString& arg);
	void moveFocus(const QString& direction);
	void moveWindow(const QString& direction);
	void swapWindow(const QString& direction);
	void toggleFloating(qs::windows::TrackedWindow* window, const QString& action);
	void toggleSplit();
	void layoutMessage(const QString& message);
	void resizeActive(const QString& args, qs::windows::TrackedWindow* target = nullptr);
	void centerWindow(qs::windows::TrackedWindow* target = nullptr);
	void moveWindowPixel(qs::windows::TrackedWindow* window, const QString& x, const QString& y);
	void exec(const QString& command);

	void unknown(const QString& what);
	void unsupported(const QString& what);

	HyprlandIpc* ipc;
	QSet<QString> warned;
};

} // namespace qs::hyprland::ipc
