#pragma once

#include <functional>
#include <vector>

#include <qt_windows.h>

#include <qhash.h>
#include <qobject.h>
#include <qtclasshelpermacros.h>

namespace qs::windows::services {

class ServiceMessageWindow {
public:
	using Handler = std::function<void(WPARAM, LPARAM)>;

	static ServiceMessageWindow* instance();

	[[nodiscard]] HWND hwnd() const { return this->mHwnd; }

	void addHandler(UINT msg, Handler handler);

private:
	ServiceMessageWindow();
	~ServiceMessageWindow();
	Q_DISABLE_COPY_MOVE(ServiceMessageWindow);

	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	HWND mHwnd = nullptr;
	ATOM mClassAtom = 0;
	QHash<UINT, std::vector<Handler>> handlers;
};

} // namespace qs::windows::services
