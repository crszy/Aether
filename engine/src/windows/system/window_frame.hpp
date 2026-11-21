#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

namespace qs::windows::sys {

class WindowFrame: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit WindowFrame(QObject* parent = nullptr);

	Q_INVOKABLE static bool hideTitleBar(QObject* window);
	Q_INVOKABLE static bool showTitleBar(QObject* window);
	/// Shows, raises and focuses a window even when Windows' foreground rules would leave it behind others
	/// (a shell window opened from IPC or a hotkey otherwise appears under the app you are in).
	Q_INVOKABLE static bool bringToFront(QObject* window);
	/// Leaves the window out of screenshots, recordings and screen capture (WDA_EXCLUDEFROMCAPTURE), so a
	/// capture of the screen shows what is underneath it - a lock screen can show the desktop behind itself.
	Q_INVOKABLE static bool excludeFromCapture(QObject* window, bool exclude = true);
	/// Saves what the screen called `screenName` shows right now to `path` (PNG) with the classic GDI capture,
	/// which works where Windows.Graphics.Capture is refused (e.g. the built-in Administrator account).
	/// Returns the file URL, or "" on failure.
	Q_INVOKABLE static QString grabScreen(const QString& screenName, const QString& path);
	/// Keeps the window above every normal window (HWND_TOPMOST), or puts it back among them.
	Q_INVOKABLE static bool setTopmost(QObject* window, bool topmost = true);
	/// See-through window background drawn by Windows 11 (DWMWA_SYSTEMBACKDROP_TYPE), the way Zen and Alacritty
	/// get theirs without Explorer: "blur" (stays blurred unfocused), "acrylic", "mica", "tabbed", or "none". The window's own colour
	/// must be partly transparent for it to show. Also asks for dark window chrome when dark is true.
	Q_INVOKABLE static bool setBackdrop(QObject* window, const QString& kind, bool dark = true);
};

} // namespace qs::windows::sys
