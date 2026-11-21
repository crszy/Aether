#pragma once

#include <functional>
#include <vector>

#include <qt_windows.h>

#include <qimage.h>
#include <qstring.h>
#include <quuid.h>

namespace qs::windows::services::systray {

struct TrayIconMessage {
	DWORD message = 0;
	HWND hwnd = nullptr;
	UINT uid = 0;
	UINT flags = 0;
	UINT callbackMessage = 0;
	QImage icon;
	QString tip;
	DWORD state = 0;
	DWORD stateMask = 0;
	UINT version = 0;
	QUuid guid;

	bool seeded = false;
	QString exePath;
};

using TrayIconSink = std::function<void(TrayIconMessage)>;

class TrayHook {
public:
	static void start(TrayIconSink sink, std::function<void()> missedTraffic, bool brief);
	static void stop();

	static void announceTo(std::vector<HWND> owners);
};

void seedFromExplorer(TrayIconSink sink, std::function<void(bool ok)> done);
[[nodiscard]] QImage imageFromIcon(HICON icon);

struct ExplorerIconData {
	HWND hwnd = nullptr;
	UINT uid = 0;
	UINT callbackMessage = 0;
	UINT version = 0;
};

std::vector<ExplorerIconData> readExplorerToolbars();

} // namespace qs::windows::services::systray
