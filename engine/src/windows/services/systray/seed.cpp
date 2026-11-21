#include <atomic>
#include <functional>
#include <thread>
#include <utility>

#include <qt_windows.h>
#include <shellapi.h>
#include <unknwn.h>

#include <qimage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <quuid.h>

#include "../../../core/logcat.hpp"
#include "hook.hpp"

namespace {

QS_LOGGING_CATEGORY(logTraySeed, "quickshell.windows.systray", QtWarningMsg);

struct NotifyItem {
	PWSTR exeName;
	PWSTR tip;
	HICON icon;
	HWND hwnd;
	DWORD preference;
	UINT id;
	GUID guid;
};

MIDL_INTERFACE("D782CCBA-AFB0-43F1-94DB-FDA3779EACCB")
INotificationCB: public IUnknown {
public:
	virtual HRESULT STDMETHODCALLTYPE Notify(ULONG event, NotifyItem* item) = 0;
};

MIDL_INTERFACE("D133CE13-3537-48BA-93A7-AFCD5D2053B4")
ITrayNotifyWin8: public IUnknown {
public:
	virtual HRESULT STDMETHODCALLTYPE RegisterCallback(INotificationCB* callback, ULONG* handle) = 0;
	virtual HRESULT STDMETHODCALLTYPE UnregisterCallback(ULONG* handle) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPreference(const NotifyItem* item) = 0;
	virtual HRESULT STDMETHODCALLTYPE EnableAutoTray(BOOL enable) = 0;
	virtual HRESULT STDMETHODCALLTYPE DoAction(BOOL action) = 0;
};

const CLSID CLSID_TRAY_NOTIFY =
    {0x25dead04, 0x1eac, 0x4911, {0x9e, 0x3a, 0xad, 0x0a, 0x4a, 0xb5, 0x60, 0xfd}};

class NotificationCallback: public INotificationCB {
public:
	explicit NotificationCallback(qs::windows::services::systray::TrayIconSink sink)
	    : sink(std::move(sink)) {}

	virtual ~NotificationCallback() = default;
	Q_DISABLE_COPY_MOVE(NotificationCallback);

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
		if (object == nullptr) return E_POINTER;

		if (riid == __uuidof(IUnknown) || riid == __uuidof(INotificationCB)) {
			*object = static_cast<INotificationCB*>(this);
			this->AddRef();
			return S_OK;
		}

		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override { return ++this->refs; }

	ULONG STDMETHODCALLTYPE Release() override {
		auto refs = --this->refs;
		if (refs == 0) delete this;
		return refs;
	}

	HRESULT STDMETHODCALLTYPE Notify(ULONG /*event*/, NotifyItem* item) override {
		if (item == nullptr || item->hwnd == nullptr || !IsWindow(item->hwnd)) return S_OK;

		qs::windows::services::systray::TrayIconMessage message;
		message.message = NIM_ADD;
		message.seeded = true;
		message.hwnd = item->hwnd;
		message.uid = item->id;
		message.flags = NIF_ICON | NIF_TIP;
		message.icon = qs::windows::services::systray::imageFromIcon(item->icon);
		if (item->tip != nullptr) message.tip = QString::fromWCharArray(item->tip);
		if (item->exeName != nullptr) message.exePath = QString::fromWCharArray(item->exeName);

		if (item->guid != GUID_NULL) {
			message.flags |= NIF_GUID;
			message.guid = QUuid(item->guid);
		}

		this->sink(std::move(message));
		return S_OK;
	}

private:
	std::atomic<ULONG> refs = 1;
	qs::windows::services::systray::TrayIconSink sink;
};

bool seed(const qs::windows::services::systray::TrayIconSink& sink) {
	IUnknown* unknown = nullptr;
	auto hr = CoCreateInstance(
	    CLSID_TRAY_NOTIFY,
	    nullptr,
	    CLSCTX_LOCAL_SERVER,
	    __uuidof(IUnknown),
	    reinterpret_cast<void**>(&unknown) // NOLINT
	);

	if (FAILED(hr) || unknown == nullptr) {
		qCInfo(logTraySeed) << "No ITrayNotify from explorer, tray icons will appear as apps update"
		                    << "them:" << Qt::hex << static_cast<quint32>(hr);
		return false;
	}

	ITrayNotifyWin8* trayNotify = nullptr;
	hr = unknown->QueryInterface(
	    __uuidof(ITrayNotifyWin8),
	    reinterpret_cast<void**>(&trayNotify) // NOLINT
	);
	unknown->Release();

	if (FAILED(hr) || trayNotify == nullptr) {
		qCInfo(logTraySeed) << "Explorer's ITrayNotify has an unknown layout:" << Qt::hex
		                    << static_cast<quint32>(hr);
		return false;
	}

	auto* callback = new NotificationCallback(sink);
	ULONG handle = 0;

	hr = trayNotify->RegisterCallback(callback, &handle);
	if (SUCCEEDED(hr)) {
		trayNotify->UnregisterCallback(&handle);
	} else {
		qCInfo(logTraySeed) << "ITrayNotify::RegisterCallback failed:" << Qt::hex
		                    << static_cast<quint32>(hr);
	}

	CoDisconnectObject(callback, 0);
	callback->Release();
	trayNotify->Release();

	return SUCCEEDED(hr);
}

} // namespace

namespace qs::windows::services::systray {

void seedFromExplorer(TrayIconSink sink, std::function<void(bool ok)> done) {
	std::thread([sink = std::move(sink), done = std::move(done)]() {
		SetThreadDescription(GetCurrentThread(), L"qs tray seed");

		auto ok = false;
		if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
			ok = seed(sink);
			CoUninitialize();
		}

		done(ok);
	}).detach();
}

} // namespace qs::windows::services::systray
