#include "message_window.hpp"

#include <qt_windows.h>

#include <qcoreapplication.h>

namespace qs::windows::services {

namespace {
const wchar_t* const WINDOW_CLASS_NAME = L"QuickshellServiceMessageWindow";
}

ServiceMessageWindow::ServiceMessageWindow() {
	WNDCLASSEXW wc {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = &ServiceMessageWindow::wndProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = WINDOW_CLASS_NAME;

	this->mClassAtom = RegisterClassExW(&wc);

	this->mHwnd = CreateWindowExW(
	    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
	    WINDOW_CLASS_NAME,
	    L"",
	    WS_POPUP,
	    0,
	    0,
	    0,
	    0,
	    nullptr,
	    nullptr,
	    GetModuleHandleW(nullptr),
	    this
	);
}

ServiceMessageWindow::~ServiceMessageWindow() {
	if (this->mHwnd != nullptr) DestroyWindow(this->mHwnd);
	if (this->mClassAtom != 0) UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
}

ServiceMessageWindow* ServiceMessageWindow::instance() {
	static auto* instance = new ServiceMessageWindow(); // NOLINT
	return instance;
}

void ServiceMessageWindow::addHandler(UINT msg, Handler handler) {
	this->handlers[msg].push_back(std::move(handler));
}

LRESULT CALLBACK
ServiceMessageWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_CREATE) {
		auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam); // NOLINT
		SetWindowLongPtrW(
		    hwnd,
		    GWLP_USERDATA,
		    reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams) // NOLINT
		);
		return 0;
	}

	auto* self =
	    reinterpret_cast<ServiceMessageWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); // NOLINT

	if (self != nullptr) {
		auto iter = self->handlers.constFind(msg);
		if (iter != self->handlers.constEnd()) {
			for (const auto& handler: iter.value()) {
				handler(wParam, lParam);
			}
		}
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace qs::windows::services
