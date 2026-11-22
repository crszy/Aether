#include "region_capture.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <qt_windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <inspectable.h>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qpoint.h>
#include <qrect.h>
#include <qstring.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include "../util.hpp"

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using winrt::Windows::Foundation::Metadata::ApiInformation;
using winrt::Windows::Graphics::SizeInt32;
using winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus;
using DxgiInterfaceAccess = ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;

namespace qs::windows::recorder {

namespace {
Q_LOGGING_CATEGORY(logRegionCapture, "quickshell.windows.recorder", QtWarningMsg);

constexpr DXGI_FORMAT FRAME_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr DirectXPixelFormat FRAME_PIXEL_FORMAT = DirectXPixelFormat::B8G8R8A8UIntNormalized;
constexpr int POOL_BUFFERS = 2;

QString hresultString(const winrt::hresult_error& e) {
	return QString("0x%1 %2")
	    .arg(static_cast<quint32>(e.code().value), 8, 16, QChar('0'))
	    .arg(QString::fromWCharArray(e.message().c_str()));
}

struct SessionFeatures {
	bool border = false;
	bool cursor = false;
};

SessionFeatures sessionFeatures() {
	static std::once_flag once;
	static SessionFeatures features;

	std::call_once(once, [] {
		const auto sessionClass = L"Windows.Graphics.Capture.GraphicsCaptureSession";

		try {
			features.border = ApiInformation::IsPropertyPresent(sessionClass, L"IsBorderRequired");
			features.cursor = ApiInformation::IsPropertyPresent(sessionClass, L"IsCursorCaptureEnabled");

			if (features.border
			    && ApiInformation::IsTypePresent(L"Windows.Graphics.Capture.GraphicsCaptureAccess"))
			{
				auto status =
				    GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::Borderless).get();
				if (status != AppCapabilityAccessStatus::Allowed) {
					qCInfo(logRegionCapture) << "Borderless capture not allowed:" << static_cast<int>(status);
				}
			}
		} catch (const winrt::hresult_error& e) {
			qCWarning(logRegionCapture) << "Capture feature queries failed:" << hresultString(e);
		}
	});

	return features;
}

BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC /*dc*/, LPRECT /*rect*/, LPARAM param) {
	reinterpret_cast<std::vector<HMONITOR>*>(param)->push_back(monitor); // NOLINT
	return TRUE;
}

} // namespace

struct MonitorCapture {
	HMONITOR handle = nullptr;
	QPoint origin;
	QRect crop;
	GraphicsCaptureItem item {nullptr};
	Direct3D11CaptureFramePool pool {nullptr};
	GraphicsCaptureSession session {nullptr};
	winrt::event_token frameToken {};
	winrt::event_token closedToken {};
	SizeInt32 poolSize {};
	bool delivered = false;
};

struct RegionCaptureState {
	std::mutex mutex;
	bool running = false;
	bool lost = false;
	int delivered = 0;

	QRect region;
	winrt::com_ptr<ID3D11DeviceContext> context;
	IDirect3DDevice winrtDevice {nullptr};
	winrt::com_ptr<ID3D11Texture2D> composite;
	std::vector<std::unique_ptr<MonitorCapture>> monitors;

	void onFrame(MonitorCapture* monitor, const Direct3D11CaptureFramePool& sender);
};

void RegionCaptureState::onFrame(
    MonitorCapture* monitor,
    const Direct3D11CaptureFramePool& sender
) {
	std::lock_guard lock(this->mutex);
	if (!this->running) return;

	try {
		auto frame = sender.TryGetNextFrame();
		if (!frame) return;

		auto content = frame.ContentSize();
		auto access = frame.Surface().as<DxgiInterfaceAccess>();
		winrt::com_ptr<ID3D11Texture2D> source;
		winrt::check_hresult(
		    access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), source.put_void())
		);

		D3D11_TEXTURE2D_DESC desc {};
		source->GetDesc(&desc);

		auto visible = QRect(
		    monitor->origin,
		    QSize(
		        std::min(content.Width, static_cast<qint32>(desc.Width)),
		        std::min(content.Height, static_cast<qint32>(desc.Height))
		    )
		);
		auto part = monitor->crop.intersected(visible);

		if (!part.isEmpty()) {
			D3D11_BOX box {
			    static_cast<UINT>(part.left() - monitor->origin.x()),
			    static_cast<UINT>(part.top() - monitor->origin.y()),
			    0,
			    static_cast<UINT>(part.right() + 1 - monitor->origin.x()),
			    static_cast<UINT>(part.bottom() + 1 - monitor->origin.y()),
			    1,
			};

			this->context->CopySubresourceRegion(
			    this->composite.get(),
			    0,
			    static_cast<UINT>(part.left() - this->region.left()),
			    static_cast<UINT>(part.top() - this->region.top()),
			    0,
			    source.get(),
			    0,
			    &box
			);
			this->context->Flush();

			if (!monitor->delivered) {
				monitor->delivered = true;
				this->delivered++;
			}
		}

		frame.Close();

		if ((content.Width != monitor->poolSize.Width || content.Height != monitor->poolSize.Height)
		    && content.Width > 0 && content.Height > 0)
		{
			sender.Recreate(this->winrtDevice, FRAME_PIXEL_FORMAT, POOL_BUFFERS, content);
			monitor->poolSize = content;
		}
	} catch (const winrt::hresult_error& e) {
		qCDebug(logRegionCapture) << "Frame callback failed:" << hresultString(e);
	}
}

RegionCapture::~RegionCapture() { this->stop(); }

bool RegionCapture::start(ID3D11Device* device, const QRect& region, bool cursor, QString* error) {
	auto state = std::make_shared<RegionCaptureState>();
	state->region = region;
	device->GetImmediateContext(state->context.put());

	D3D11_TEXTURE2D_DESC desc {};
	desc.Width = static_cast<UINT>(region.width());
	desc.Height = static_cast<UINT>(region.height());
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = FRAME_FORMAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;

	std::vector<quint32> black(static_cast<size_t>(region.width()) * region.height(), 0xff000000);
	D3D11_SUBRESOURCE_DATA initial {black.data(), static_cast<UINT>(region.width() * 4), 0};

	auto hr = device->CreateTexture2D(&desc, &initial, state->composite.put());
	if (FAILED(hr)) {
		*error = QString("cannot create the frame texture (0x%1)")
		             .arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
		return false;
	}

	std::vector<HMONITOR> handles;
	auto param = reinterpret_cast<LPARAM>(&handles); // NOLINT
	EnumDisplayMonitors(nullptr, nullptr, collectMonitor, param);

	for (auto* handle: handles) {
		auto rects = monitorRects(handle);
		if (!rects.valid) continue;

		auto crop = rects.monitor.intersected(region);
		if (crop.isEmpty()) continue;

		auto monitor = std::make_unique<MonitorCapture>();
		monitor->handle = handle;
		monitor->origin = rects.monitor.topLeft();
		monitor->crop = crop;
		state->monitors.push_back(std::move(monitor));
	}

	if (state->monitors.empty()) {
		*error = "the region is not on any screen";
		return false;
	}

	auto features = sessionFeatures();

	try {
		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		winrt::check_hresult(device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put())));
		winrt::com_ptr<::IInspectable> inspectable;
		winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
		state->winrtDevice = inspectable.as<IDirect3DDevice>();

		auto factory =
		    winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();

		for (auto& monitor: state->monitors) {
			winrt::check_hresult(factory->CreateForMonitor(
			    monitor->handle,
			    winrt::guid_of<GraphicsCaptureItem>(),
			    winrt::put_abi(monitor->item)
			));

			monitor->poolSize = monitor->item.Size();
			monitor->pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
			    state->winrtDevice,
			    FRAME_PIXEL_FORMAT,
			    POOL_BUFFERS,
			    monitor->poolSize
			);
			monitor->session = monitor->pool.CreateCaptureSession(monitor->item);

			if (features.border) {
				try {
					monitor->session.IsBorderRequired(false);
				} catch (const winrt::hresult_error& e) {
					qCInfo(logRegionCapture) << "IsBorderRequired(false) refused:" << hresultString(e);
				}
			}

			if (features.cursor) {
				try {
					monitor->session.IsCursorCaptureEnabled(cursor);
				} catch (const winrt::hresult_error& e) {
					qCWarning(logRegionCapture) << "IsCursorCaptureEnabled failed:" << hresultString(e);
				}
			}

			auto* raw = monitor.get();
			std::weak_ptr<RegionCaptureState> weak = state;

			monitor->frameToken =
			    monitor->pool.FrameArrived([weak, raw](auto const& sender, auto const&) {
				    if (auto s = weak.lock()) s->onFrame(raw, sender);
			    });

			monitor->closedToken = monitor->item.Closed([weak](auto const&, auto const&) {
				if (auto s = weak.lock()) {
					std::lock_guard lock(s->mutex);
					s->lost = true;
				}
			});
		}

		{
			std::lock_guard lock(state->mutex);
			state->running = true;
		}

		for (auto& monitor: state->monitors) monitor->session.StartCapture();
	} catch (const winrt::hresult_error& e) {
		*error = "screen capture failed to start: " + hresultString(e);
		qCWarning(logRegionCapture) << *error;
		this->state = state;
		this->stop();
		return false;
	}

	this->state = state;
	return true;
}

void RegionCapture::stop() {
	if (!this->state) return;
	auto state = std::move(this->state);

	{
		std::lock_guard lock(state->mutex);
		state->running = false;
	}

	for (auto& monitor: state->monitors) {
		try {
			if (monitor->pool) monitor->pool.FrameArrived(monitor->frameToken);
			if (monitor->item) monitor->item.Closed(monitor->closedToken);
			if (monitor->session) monitor->session.Close();
			if (monitor->pool) monitor->pool.Close();
		} catch (const winrt::hresult_error& e) {
			qCWarning(logRegionCapture) << "Stopping capture failed:" << hresultString(e);
		}

		monitor->session = nullptr;
		monitor->pool = nullptr;
		monitor->item = nullptr;
	}
}

ID3D11Texture2D* RegionCapture::composite() const {
	return this->state ? this->state->composite.get() : nullptr;
}

bool RegionCapture::primed() const {
	if (!this->state) return false;
	std::lock_guard lock(this->state->mutex);
	return this->state->delivered == static_cast<int>(this->state->monitors.size());
}

bool RegionCapture::lost() const {
	if (!this->state) return true;
	std::lock_guard lock(this->state->mutex);
	return this->state->lost;
}

} // namespace qs::windows::recorder
